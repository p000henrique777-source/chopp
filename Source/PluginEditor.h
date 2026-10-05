#pragma once

#include "PluginProcessor.h"
#include <functional>
#include <memory>

inline juce::Colour padColour (int i)
{
    return juce::Colour::fromHSV ((float) i / (float) ChopEasyProcessor::numPads, 0.6f, 0.95f, 1.0f);
}

//==============================================================================
// Waveform: no modo Cortar, clique marca o corte do pad selecionado.
// No modo Scratch, arrastar para os lados faz scratch.
class WaveformView : public juce::Component
{
public:
    explicit WaveformView (ChopEasyProcessor& p) : proc (p) {}

    int selectedPad = 0;
    bool scratchMode = false;
    juce::String emptyText;
    std::function<void (int)> onCuePlaced;

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff15171c));
        g.fillRoundedRectangle (b, 6.0f);

        const int len = proc.getNumLoaded();
        if (len < 4)
        {
            g.setColour (juce::Colours::grey);
            g.setFont (16.0f);
            g.drawFittedText (emptyText, getLocalBounds().reduced (20), juce::Justification::centred, 3);
            return;
        }

        const float w = b.getWidth();
        const float h = b.getHeight();
        const float mid = h * 0.5f;

        for (int i = 0; i < ChopEasyProcessor::numPads; ++i)
        {
            const auto& p = proc.pads[(size_t) i];
            if (! p.isSet)
                continue;

            const float x0 = (float) (p.start / (double) len * w);
            const float x1 = (float) (p.end / (double) len * w);

            g.setColour (padColour (i).withAlpha (i == selectedPad ? 0.38f : 0.16f));
            g.fillRect (x0, 0.0f, juce::jmax (1.0f, x1 - x0), h);

            g.setColour (padColour (i));
            g.drawLine (x0, 0.0f, x0, h, 2.0f);

            g.setFont (12.0f);
            g.drawText (juce::String (i + 1), (int) x0 + 3, 2, 26, 14, juce::Justification::centredLeft);
        }

        g.setColour (juce::Colours::white.withAlpha (0.75f));
        const int nb = (int) proc.peakMax.size();
        const int iw = (int) w;
        if (nb > 0 && iw > 0)
        {
            for (int x = 0; x < iw; ++x)
            {
                const int idx = juce::jlimit (0, nb - 1, (int) ((juce::int64) x * nb / iw));
                const float top = mid - proc.peakMax[(size_t) idx] * mid * 0.95f;
                const float bottom = mid - proc.peakMin[(size_t) idx] * mid * 0.95f;
                g.drawLine ((float) x, top, (float) x, juce::jmax (bottom, top + 1.0f));
            }
        }

        const double pp = proc.uiPlayPos.load();
        if (pp >= 0.0)
        {
            const float x = (float) (pp / (double) len * w);
            g.setColour (juce::Colours::yellow);
            g.drawLine (x, 0.0f, x, h, 2.0f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (proc.getNumLoaded() < 4)
            return;

        if (scratchMode)
        {
            const double pos = xToSample (e.position.x);
            scratchAnchor = pos;
            scratchDownX = e.position.x;
            proc.scratchTarget.store (pos);
            proc.scratchReset.store (true);
            proc.scratching.store (true);
        }
        else
        {
            dragPad = selectedPad;
            proc.setPadStart (dragPad, xToSample (e.position.x));
            proc.keyboardState.noteOn (1, ChopEasyProcessor::firstNote + dragPad, 0.9f); // preview
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (proc.getNumLoaded() < 4)
            return;

        if (scratchMode)
        {
            // 6 ms de audio por pixel arrastado
            const double delta = (double) (e.position.x - scratchDownX) * 0.006 * proc.getFileSampleRate();
            proc.scratchTarget.store (scratchAnchor + delta);
        }
        else if (dragPad >= 0)
        {
            proc.setPadStart (dragPad, xToSample (e.position.x));
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (scratchMode)
        {
            proc.scratching.store (false);
        }
        else if (dragPad >= 0)
        {
            proc.keyboardState.noteOff (1, ChopEasyProcessor::firstNote + dragPad, 0.0f);
            const int done = dragPad;
            dragPad = -1;
            if (onCuePlaced)
                onCuePlaced (done);
        }
    }

private:
    double xToSample (float x) const
    {
        const int len = proc.getNumLoaded();
        const double frac = juce::jlimit (0.0, 1.0, (double) x / (double) juce::jmax (1, getWidth()));
        return frac * (double) len;
    }

    ChopEasyProcessor& proc;
    double scratchAnchor = 0.0;
    float scratchDownX = 0.0f;
    int dragPad = -1;
};

//==============================================================================
class PadButton : public juce::Component
{
public:
    PadButton (ChopEasyProcessor& p, int idx) : proc (p), index (idx) {}

    bool selected = false, isSet = false, playing = false;
    std::function<void (int)> onSelect;

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (3.0f);

        g.setColour (isSet ? padColour (index).withAlpha (playing ? 1.0f : 0.55f)
                           : juce::Colour (0xff2a2d35));
        g.fillRoundedRectangle (b, 8.0f);

        if (selected)
        {
            g.setColour (juce::Colours::white);
            g.drawRoundedRectangle (b, 8.0f, 2.5f);
        }

        g.setColour (juce::Colours::white.withAlpha (isSet ? 1.0f : 0.4f));
        g.setFont (20.0f);
        g.drawText (juce::String (index + 1), getLocalBounds(), juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (onSelect)
            onSelect (index);
        proc.keyboardState.noteOn (1, ChopEasyProcessor::firstNote + index, 1.0f);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        proc.keyboardState.noteOff (1, ChopEasyProcessor::firstNote + index, 0.0f);
    }

private:
    ChopEasyProcessor& proc;
    int index;
};

//==============================================================================
class ChopEasyEditor : public juce::AudioProcessorEditor,
                       public juce::FileDragAndDropTarget,
                       private juce::Timer
{
public:
    explicit ChopEasyEditor (ChopEasyProcessor&);
    ~ChopEasyEditor() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    juce::String tr (const char* en, const char* pt) const
    {
        return juce::String::fromUTF8 (proc.language.load() == 1 ? pt : en);
    }

    void timerCallback() override;
    void updateTexts();
    void updateFileLabel();
    void refreshPads();
    void chooseFile();
    void loadFromFile (const juce::File& f);

    ChopEasyProcessor& proc;

    std::unique_ptr<WaveformView> wave;
    std::array<std::unique_ptr<PadButton>, ChopEasyProcessor::numPads> padButtons;

    juce::TextButton loadButton, langButton, chopBtn, scratchBtn;
    juce::TextButton detectBtn, slice4, slice8, slice16, randomBtn, clearBtn;
    juce::Label fileLabel, infoLabel, sensLabel, pitchLabel, volLabel, bpmLabel, hintLabel;
    juce::Slider sensSlider, pitchSlider, volSlider, bpmSlider;
    juce::ToggleButton syncToggle, reverseToggle, oneShotToggle;

    std::unique_ptr<juce::FileChooser> chooser;

    int selectedPad = 0;
    int lastLoad = -1, lastPads = -1;
    double lastPlay = -2.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChopEasyEditor)
};
