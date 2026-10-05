#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

ChopEasyProcessor::ChopEasyProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    formats.registerBasicFormats();
    for (auto& p : padPlaying)
        p.store (false);
}

void ChopEasyProcessor::prepareToPlay (double, int)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    for (auto& v : voices)
        v.active = false;
    scratchGain = 0.0f;
}

bool ChopEasyProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

juce::AudioProcessorEditor* ChopEasyProcessor::createEditor()
{
    return new ChopEasyEditor (*this);
}

// ---------------------------------------------------------------------------
// Detecao de batidas: mede o aumento de energia em 3 faixas (grave, medio, agudo).
// Bem leve: sem FFT, so filtros simples e somas.
// ---------------------------------------------------------------------------
static std::vector<float> computeOnsetEnvelope (const juce::AudioBuffer<float>& b, double sr, int hop)
{
    const int n = b.getNumSamples();
    const float* L = b.getReadPointer (0);
    const float* R = b.getReadPointer (1);
    const int frames = (n + hop - 1) / hop;
    std::vector<float> flux ((size_t) frames, 0.0f);

    const float a1 = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 150.0 / sr);
    const float a2 = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 1500.0 / sr);
    float lp1 = 0.0f, lp2 = 0.0f;
    float prev[3] = { 0.0f, 0.0f, 0.0f };

    for (int f = 0; f < frames; ++f)
    {
        const int s0 = f * hop;
        const int s1 = std::min (n, s0 + hop);
        double e[3] = { 0.0, 0.0, 0.0 };

        for (int i = s0; i < s1; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]);
            lp1 = a1 * lp1 + (1.0f - a1) * m;
            lp2 = a2 * lp2 + (1.0f - a2) * m;
            const float x0 = lp1, x1 = lp2 - lp1, x2 = m - lp2;
            e[0] += (double) (x0 * x0);
            e[1] += (double) (x1 * x1);
            e[2] += (double) (x2 * x2);
        }

        float sum = 0.0f;
        for (int k = 0; k < 3; ++k)
        {
            const float rms = (float) std::sqrt (e[k] / (double) (s1 - s0));
            const float c = std::log (1.0f + 100.0f * rms);
            sum += std::max (0.0f, c - prev[k]);
            prev[k] = c;
        }

        flux[(size_t) f] = sum;
    }

    return flux;
}

// ---------------------------------------------------------------------------
// Carregar sample
// ---------------------------------------------------------------------------
bool ChopEasyProcessor::loadFile (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples < 2 || reader->sampleRate <= 0.0)
        return false;

    const int n = (int) juce::jmin ((juce::int64) (maxSeconds * reader->sampleRate), reader->lengthInSamples);

    juce::AudioBuffer<float> temp (2, n);
    temp.clear();
    reader->read (&temp, 0, n, 0, true, reader->numChannels > 1);
    if (reader->numChannels == 1)
        temp.copyFrom (1, 0, temp, 0, 0, n);

    // curva de batidas para a deteccao automatica de cortes
    auto env = computeOnsetEnvelope (temp, reader->sampleRate, onsetHop);

    // picos para desenhar a waveform (barato: 2048 colunas)
    constexpr int buckets = 2048;
    std::vector<float> mn ((size_t) buckets, 0.0f), mx ((size_t) buckets, 0.0f);
    const float* ch0 = temp.getReadPointer (0);
    float biggest = 0.0f;

    for (int b = 0; b < buckets; ++b)
    {
        const int s0 = (int) ((juce::int64) b * n / buckets);
        const int s1 = juce::jmax (s0 + 1, (int) ((juce::int64) (b + 1) * n / buckets));
        const int stride = juce::jmax (1, (s1 - s0) / 64);
        float lo = 0.0f, hi = 0.0f;

        for (int s = s0; s < s1 && s < n; s += stride)
        {
            lo = juce::jmin (lo, ch0[s]);
            hi = juce::jmax (hi, ch0[s]);
        }

        mn[(size_t) b] = lo;
        mx[(size_t) b] = hi;
        biggest = juce::jmax (biggest, -lo, hi);
    }

    if (biggest > 0.0001f)
    {
        const float scale = 1.0f / biggest;
        for (auto& v : mn) v *= scale;
        for (auto& v : mx) v *= scale;
    }

    {
        const juce::SpinLock::ScopedLockType sl (lock);
        sample = std::move (temp);
        fileSR = reader->sampleRate;
        for (auto& v : voices)
            v.active = false;
        scratchPos = 0.0;
        scratchGain = 0.0f;
    }

    scratchTarget.store (0.0);
    onsetEnv = std::move (env);
    peakMin = std::move (mn);
    peakMax = std::move (mx);
    samplePath = file.getFullPathName();
    fileName = file.getFileName();

    detectChops (sensitivity.load()); // detecta os cortes sozinho
    ++loadCounter;
    return true;
}

// ---------------------------------------------------------------------------
// Pads / cortes
// ---------------------------------------------------------------------------
void ChopEasyProcessor::recalcEnds()
{
    const double len = (double) sample.getNumSamples();

    for (auto& p : pads)
    {
        if (! p.isSet)
            continue;

        double e = len;
        for (auto& q : pads)
            if (q.isSet && q.start > p.start && q.start < e)
                e = q.start;

        p.end = e;
    }
}

void ChopEasyProcessor::setPadStart (int pad, double pos)
{
    chopMode.store (3);
    if (pad < 0 || pad >= numPads)
        return;

    {
        const juce::SpinLock::ScopedLockType sl (lock);
        const int len = sample.getNumSamples();
        if (len < 4)
            return;

        auto& p = pads[(size_t) pad];
        p.start = juce::jlimit (0.0, (double) len - 3.0, pos);
        p.isSet = true;
        recalcEnds();
    }
    ++padsVersion;
}

void ChopEasyProcessor::autoSlice (int slices)
{
    chopMode.store (3);
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        const int len = sample.getNumSamples();
        if (len < 4)
            return;

        slices = juce::jlimit (1, numPads, slices);

        for (int i = 0; i < numPads; ++i)
        {
            auto& p = pads[(size_t) i];
            p.isSet = i < slices;
            p.start = p.isSet ? (double) len * (double) i / (double) slices : 0.0;
        }

        recalcEnds();
        for (auto& v : voices)
            v.active = false;
    }
    ++padsVersion;
}

void ChopEasyProcessor::randomSlice()
{
    chopMode.store (3);
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        const int len = sample.getNumSamples();
        if (len < 4)
            return;

        juce::Random rng;
        std::array<double, numPads> starts;
        for (auto& s : starts)
            s = rng.nextDouble() * ((double) len - 4.0);

        std::sort (starts.begin(), starts.end());
        starts[0] = 0.0;

        for (int i = 0; i < numPads; ++i)
        {
            auto& p = pads[(size_t) i];
            p.isSet = true;
            p.start = starts[(size_t) i];
        }

        recalcEnds();
        for (auto& v : voices)
            v.active = false;
    }
    ++padsVersion;
}

void ChopEasyProcessor::detectChops (float sens)
{
    const int len = sample.getNumSamples();
    const int frames = (int) onsetEnv.size();
    if (len < 4 || frames < 4)
        return;

    sens = juce::jlimit (0.0f, 1.0f, sens);
    const float factor = 2.6f - 1.6f * sens; // mais sensivel = limite mais baixo

    float gmax = 0.0f;
    for (auto v : onsetEnv)
        gmax = juce::jmax (gmax, v);

    auto fallback = [this]
    {
        autoSlice (16);
        chopMode.store (2);
        detectedCount.store (0);
    };

    if (gmax <= 0.0001f)
    {
        fallback();
        return;
    }

    const float floorValue = 0.08f * gmax;
    const int halfWin = juce::jmax (4, (int) (0.5 * fileSR / (double) onsetHop)); // media local de +-0,5 s

    std::vector<double> prefix ((size_t) frames + 1, 0.0);
    for (int i = 0; i < frames; ++i)
        prefix[(size_t) i + 1] = prefix[(size_t) i] + (double) onsetEnv[(size_t) i];

    struct Cand { int frame; float strength; };
    std::vector<Cand> cands;

    for (int f = 0; f < frames; ++f)
    {
        const float v = onsetEnv[(size_t) f];
        if (v < floorValue)
            continue;

        const int a = juce::jmax (0, f - halfWin);
        const int b = juce::jmin (frames, f + halfWin + 1);
        const float mean = (float) ((prefix[(size_t) b] - prefix[(size_t) a]) / (double) (b - a));
        if (v < mean * factor)
            continue;

        bool isMax = true;
        for (int k = juce::jmax (0, f - 2); k <= juce::jmin (frames - 1, f + 2); ++k)
            if (onsetEnv[(size_t) k] > v)
            {
                isMax = false;
                break;
            }

        if (isMax)
            cands.push_back ({ f, v });
    }

    // fica com as batidas mais fortes (maximo = numero de pads), com distancia minima de ~90 ms
    std::sort (cands.begin(), cands.end(), [] (const Cand& x, const Cand& y) { return x.strength > y.strength; });

    const int minGap = juce::jmax (2, (int) (0.09 * fileSR / (double) onsetHop));
    std::vector<int> picked;
    for (const auto& c : cands)
    {
        bool ok = true;
        for (int p : picked)
            if (std::abs (p - c.frame) < minGap)
            {
                ok = false;
                break;
            }

        if (ok)
        {
            picked.push_back (c.frame);
            if ((int) picked.size() >= numPads)
                break;
        }
    }

    if ((int) picked.size() < 4) // sample sem batidas claras (pad, vocal longo...): cortes iguais
    {
        fallback();
        return;
    }

    const float* L = sample.getReadPointer (0);
    const float* R = sample.getReadPointer (1);

    // acha o comeco exato da batida e encosta num cruzamento por zero (evita estalo)
    auto refine = [&] (int approx) -> int
    {
        const int from = juce::jmax (0, approx - onsetHop);
        const int to = juce::jmin (len - 1, approx + onsetHop);

        float peak = 0.0f;
        for (int i = from; i <= to; ++i)
            peak = juce::jmax (peak, std::abs (0.5f * (L[i] + R[i])));

        const float thr = 0.25f * peak;
        int pos = from;
        for (int i = from; i <= to; ++i)
            if (std::abs (0.5f * (L[i] + R[i])) >= thr)
            {
                pos = i;
                break;
            }

        const int preRoll = (int) (0.002 * fileSR);
        pos = juce::jmax (0, pos - preRoll);

        const int back = juce::jmax (0, pos - preRoll);
        for (int i = pos; i > back; --i)
            if ((L[i - 1] + R[i - 1]) * (L[i] + R[i]) <= 0.0f)
            {
                pos = i;
                break;
            }

        return pos;
    };

    std::vector<double> starts;
    for (int f : picked)
        starts.push_back ((double) refine (f * onsetHop));
    std::sort (starts.begin(), starts.end());

    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (int i = 0; i < numPads; ++i)
        {
            auto& p = pads[(size_t) i];
            p.isSet = i < (int) starts.size();
            p.start = p.isSet ? juce::jlimit (0.0, (double) len - 3.0, starts[(size_t) i]) : 0.0;
        }
        recalcEnds();
        for (auto& v : voices)
            v.active = false;
    }

    chopMode.store (1);
    detectedCount.store ((int) starts.size());
    ++padsVersion;
}

void ChopEasyProcessor::clearPads()
{
    chopMode.store (3);
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (auto& p : pads)
            p.isSet = false;
        for (auto& v : voices)
            v.active = false;
    }
    ++padsVersion;
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
void ChopEasyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    // eventos vindos dos pads clicados na interface
    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    const juce::SpinLock::ScopedTryLockType tryLock (lock);
    if (! tryLock.isLocked() || sample.getNumSamples() < 4)
        return;

    const int len = sample.getNumSamples();
    const float* srcL = sample.getReadPointer (0);
    const float* srcR = sample.getReadPointer (1);

    double hostBpm = 0.0;
    if (auto* ph = getPlayHead())
        if (auto position = ph->getPosition())
            if (auto bpm = position->getBpm())
                hostBpm = *bpm;

    const double outSR = getSampleRate();
    double rate = (fileSR / outSR) * std::pow (2.0, (double) pitchSemis.load() / 12.0);
    const double smpBpm = (double) sampleBpm.load();
    if (sync.load() && hostBpm > 0.0 && smpBpm > 1.0)
        rate *= hostBpm / smpBpm;

    const bool rev = reverse.load();
    const bool one = oneShot.load();
    const float vol = volume.load();
    const float atk = 1.0f / (float) (0.002 * outSR); //  2 ms (evita clique)
    const float rel = 1.0f / (float) (0.015 * outSR); // 15 ms

    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    auto fetch = [&] (double p, float& l, float& r)
    {
        if (p <= 0.0)
        {
            l = srcL[0];
            r = srcR[0];
            return;
        }

        const int i = (int) p;
        if (i >= len - 1)
        {
            l = srcL[len - 1];
            r = srcR[len - 1];
            return;
        }

        const float f = (float) (p - (double) i);
        l = srcL[i] + f * (srcL[i + 1] - srcL[i]);
        r = srcR[i] + f * (srcR[i + 1] - srcR[i]);
    };

    auto handle = [&] (const juce::MidiMessage& m)
    {
        if (m.isNoteOn())
        {
            const int pad = m.getNoteNumber() - firstNote;
            if (pad >= 0 && pad < numPads)
            {
                const auto& p = pads[(size_t) pad];
                if (p.isSet && p.end - p.start > 2.0)
                {
                    auto& v = voices[(size_t) pad];
                    v.active = true;
                    v.releasing = false;
                    v.reverse = rev;
                    v.start = p.start;
                    v.end = p.end;
                    v.pos = rev ? p.end - 1.0 : p.start;
                    v.env = 0.0f;
                    v.gain = m.getFloatVelocity();
                    lastVoice = pad;
                }
            }
        }
        else if (m.isNoteOff())
        {
            const int pad = m.getNoteNumber() - firstNote;
            if (pad >= 0 && pad < numPads && ! one)
                voices[(size_t) pad].releasing = true;
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            for (auto& v : voices)
                v.active = false;
        }
    };

    auto render = [&] (int from, int to)
    {
        for (auto& v : voices)
        {
            if (! v.active)
                continue;

            for (int i = from; i < to; ++i)
            {
                if (v.releasing)
                {
                    v.env -= rel;
                    if (v.env <= 0.0f)
                    {
                        v.active = false;
                        break;
                    }
                }
                else if (v.env < 1.0f)
                {
                    v.env = juce::jmin (1.0f, v.env + atk);
                }

                const double remaining = v.reverse ? (v.pos - v.start) : (v.end - v.pos);
                const float tail = (float) juce::jlimit (0.0, 1.0, remaining / 256.0);

                float l, r;
                fetch (v.pos, l, r);

                const float g = v.gain * v.env * tail * vol;
                outL[i] += l * g;
                if (outR != nullptr)
                    outR[i] += r * g;

                v.pos += v.reverse ? -rate : rate;
                if (v.pos >= v.end || v.pos <= v.start)
                {
                    v.active = false;
                    break;
                }
            }
        }
    };

    int cursor = 0;
    for (const auto metadata : midi)
    {
        const int t = juce::jlimit (cursor, numSamples, metadata.samplePosition);
        render (cursor, t);
        cursor = t;
        handle (metadata.getMessage());
    }
    render (cursor, numSamples);

    // --- Scratch: a posicao de leitura "persegue" a posicao do mouse.
    //     Velocidade = volume (parado = silencio), como um disco de vinil.
    bool scratchActive = false;
    {
        const double maxPos = (double) len - 1.0;
        const double target = juce::jlimit (0.0, maxPos, scratchTarget.load());

        if (scratchReset.exchange (false))
        {
            scratchPos = target;
            scratchGain = 0.0f;
        }

        if (scratching.load() || std::abs (target - scratchPos) > 0.5)
        {
            scratchActive = true;
            const double coef = 1.0 - std::exp (-1.0 / (0.012 * outSR));

            for (int i = 0; i < numSamples; ++i)
            {
                const double step = juce::jlimit (-8.0, 8.0, (target - scratchPos) * coef);
                scratchPos = juce::jlimit (0.0, maxPos, scratchPos + step);

                const float wanted = (float) juce::jmin (1.0, std::abs (step) * 3.0);
                scratchGain += (wanted - scratchGain) * 0.02f;

                float l, r;
                fetch (scratchPos, l, r);

                const float g = scratchGain * vol;
                outL[i] += l * g;
                if (outR != nullptr)
                    outR[i] += r * g;
            }
        }
    }

    // estado para a interface
    double ui = -1.0;
    if (scratchActive)
        ui = scratchPos;
    else if (voices[(size_t) lastVoice].active)
        ui = voices[(size_t) lastVoice].pos;

    uiPlayPos.store (ui);
    for (int i = 0; i < numPads; ++i)
        padPlaying[(size_t) i].store (voices[(size_t) i].active);
}

// ---------------------------------------------------------------------------
// Salvar / carregar projeto
// ---------------------------------------------------------------------------
void ChopEasyProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("CHOPEASY");
    xml.setAttribute ("file", samplePath);
    xml.setAttribute ("pitch", (double) pitchSemis.load());
    xml.setAttribute ("volume", (double) volume.load());
    xml.setAttribute ("bpm", (double) sampleBpm.load());
    xml.setAttribute ("sync", (int) sync.load());
    xml.setAttribute ("reverse", (int) reverse.load());
    xml.setAttribute ("oneshot", (int) oneShot.load());
    xml.setAttribute ("lang", language.load());
    xml.setAttribute ("sens", (double) sensitivity.load());

    for (int i = 0; i < numPads; ++i)
        if (pads[(size_t) i].isSet)
            xml.setAttribute ("pad" + juce::String (i), pads[(size_t) i].start);

    copyXmlToBinary (xml, destData);
}

void ChopEasyProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName ("CHOPEASY"))
        return;

    pitchSemis.store ((float) xml->getDoubleAttribute ("pitch", 0.0));
    volume.store ((float) xml->getDoubleAttribute ("volume", 0.8));
    sampleBpm.store ((float) xml->getDoubleAttribute ("bpm", 120.0));
    sync.store (xml->getBoolAttribute ("sync", false));
    reverse.store (xml->getBoolAttribute ("reverse", false));
    oneShot.store (xml->getBoolAttribute ("oneshot", true));
    language.store (xml->getIntAttribute ("lang", 0));
    sensitivity.store ((float) xml->getDoubleAttribute ("sens", 0.5));

    const auto path = xml->getStringAttribute ("file");
    if (path.isNotEmpty() && juce::File (path).existsAsFile() && loadFile (juce::File (path)))
    {
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            for (int i = 0; i < numPads; ++i)
            {
                auto& p = pads[(size_t) i];
                const auto key = "pad" + juce::String (i);
                p.isSet = xml->hasAttribute (key);
                p.start = p.isSet ? xml->getDoubleAttribute (key, 0.0) : 0.0;
            }
            recalcEnds();
            for (auto& v : voices)
                v.active = false;
        }
        chopMode.store (3);
        ++padsVersion;
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChopEasyProcessor();
}
