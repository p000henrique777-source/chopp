#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <vector>

// ChopEasy - sampler leve com 16 pads, cortes (chops) e scratch.
class ChopEasyProcessor : public juce::AudioProcessor
{
public:
    static constexpr int numPads = 16;
    static constexpr int firstNote = 36;        // pad 1 = nota MIDI 36 (C3 no FL Studio)
    static constexpr double maxSeconds = 240.0; // limite de duracao do sample (poupa memoria)

    struct Pad
    {
        double start = 0.0;
        double end = 0.0;
        bool isSet = false;
    };

    ChopEasyProcessor();
    ~ChopEasyProcessor() override = default;

    // --- API usada pela interface (thread de UI) ---
    bool loadFile (const juce::File& file);
    void setPadStart (int pad, double samplePos);
    void autoSlice (int slices);
    void randomSlice();
    void clearPads();
    void detectChops (float sensitivity); // detecta as batidas e posiciona os pads
    int getNumLoaded() const { return sample.getNumSamples(); }
    double getFileSampleRate() const { return fileSR; }

    // dados visiveis para a UI
    std::array<Pad, numPads> pads;
    std::vector<float> peakMin, peakMax;
    juce::String fileName, samplePath;
    juce::MidiKeyboardState keyboardState;

    std::atomic<float> pitchSemis { 0.0f };
    std::atomic<float> volume { 0.8f };
    std::atomic<float> sampleBpm { 120.0f };
    std::atomic<bool> sync { false };
    std::atomic<bool> reverse { false };
    std::atomic<bool> oneShot { true };
    std::atomic<int> language { 0 }; // 0 = English, 1 = Portugues (BR)
    std::atomic<float> sensitivity { 0.5f }; // 0..1 sensibilidade da deteccao de cortes
    std::atomic<int> chopMode { 0 };         // 0 nada, 1 detectado, 2 cortes iguais (sem batidas claras), 3 manual
    std::atomic<int> detectedCount { 0 };

    std::atomic<bool> scratching { false };
    std::atomic<bool> scratchReset { false };
    std::atomic<double> scratchTarget { 0.0 };

    std::atomic<double> uiPlayPos { -1.0 };
    std::array<std::atomic<bool>, numPads> padPlaying;
    std::atomic<int> loadCounter { 0 };
    std::atomic<int> padsVersion { 0 };

    // --- AudioProcessor ---
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ChopEasy"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    struct Voice
    {
        bool active = false;
        bool releasing = false;
        bool reverse = false;
        double pos = 0.0, start = 0.0, end = 0.0;
        float env = 0.0f;
        float gain = 1.0f;
    };

    void recalcEnds(); // chamar com o lock ativo

    juce::AudioFormatManager formats;
    juce::SpinLock lock;
    juce::AudioBuffer<float> sample; // sempre estereo
    double fileSR = 44100.0;

    static constexpr int onsetHop = 512;
    std::vector<float> onsetEnv; // "forca da batida" a cada onsetHop amostras

    std::array<Voice, numPads> voices;
    int lastVoice = 0;

    double scratchPos = 0.0;
    float scratchGain = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChopEasyProcessor)
};
