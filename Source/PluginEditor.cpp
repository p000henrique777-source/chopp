#include "PluginEditor.h"

ChopEasyEditor::ChopEasyEditor (ChopEasyProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    setSize (760, 590);

    wave = std::make_unique<WaveformView> (proc);
    wave->onCuePlaced = [this] (int pad)
    {
        selectedPad = (pad + 1) % ChopEasyProcessor::numPads; // avanca para o proximo pad
        refreshPads();
    };
    addAndMakeVisible (*wave);

    for (int i = 0; i < ChopEasyProcessor::numPads; ++i)
    {
        auto pb = std::make_unique<PadButton> (proc, i);
        pb->onSelect = [this] (int idx)
        {
            selectedPad = idx;
            refreshPads();
        };
        addAndMakeVisible (*pb);
        padButtons[(size_t) i] = std::move (pb);
    }

    // --- botoes principais ---
    addAndMakeVisible (loadButton);
    loadButton.onClick = [this] { chooseFile(); };

    addAndMakeVisible (langButton);
    langButton.onClick = [this]
    {
        proc.language.store (proc.language.load() == 0 ? 1 : 0);
        updateTexts();
    };

    for (auto* b : { &chopBtn, &scratchBtn })
    {
        addAndMakeVisible (*b);
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1);
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2f9e6b));
    }
    chopBtn.setToggleState (true, juce::dontSendNotification);

    auto modeChanged = [this]
    {
        wave->scratchMode = scratchBtn.getToggleState();
        updateTexts();
    };
    chopBtn.onClick = modeChanged;
    scratchBtn.onClick = modeChanged;

    // --- auto-corte ---
    slice4.setButtonText ("4");
    slice8.setButtonText ("8");
    slice16.setButtonText ("16");
    slice4.onClick = [this] { proc.autoSlice (4); };
    slice8.onClick = [this] { proc.autoSlice (8); };
    slice16.onClick = [this] { proc.autoSlice (16); };
    randomBtn.onClick = [this] { proc.randomSlice(); };
    clearBtn.onClick = [this] { proc.clearPads(); };
    detectBtn.onClick = [this] { proc.detectChops (proc.sensitivity.load()); };
    for (auto* b : { &detectBtn, &slice4, &slice8, &slice16, &randomBtn, &clearBtn })
        addAndMakeVisible (*b);

    // --- sliders ---
    auto setupSlider = [this] (juce::Slider& s, double lo, double hi, double step, double def, double value)
    {
        addAndMakeVisible (s);
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
        s.setRange (lo, hi, step);
        s.setDoubleClickReturnValue (true, def);
        s.setValue (value, juce::dontSendNotification);
    };

    setupSlider (sensSlider, 0.0, 100.0, 1.0, 50.0, (double) proc.sensitivity.load() * 100.0);
    sensSlider.onValueChange = [this]
    {
        proc.sensitivity.store ((float) (sensSlider.getValue() / 100.0));
        proc.detectChops (proc.sensitivity.load());
    };

    setupSlider (pitchSlider, -12.0, 12.0, 1.0, 0.0, (double) proc.pitchSemis.load());
    pitchSlider.onValueChange = [this] { proc.pitchSemis.store ((float) pitchSlider.getValue()); };

    setupSlider (volSlider, 0.0, 1.0, 0.01, 0.8, (double) proc.volume.load());
    volSlider.onValueChange = [this] { proc.volume.store ((float) volSlider.getValue()); };

    setupSlider (bpmSlider, 40.0, 220.0, 0.1, 120.0, (double) proc.sampleBpm.load());
    bpmSlider.onValueChange = [this] { proc.sampleBpm.store ((float) bpmSlider.getValue()); };

    for (auto* l : { &sensLabel, &pitchLabel, &volLabel, &bpmLabel })
        addAndMakeVisible (*l);

    // --- toggles ---
    for (auto* t : { &syncToggle, &reverseToggle, &oneShotToggle })
        addAndMakeVisible (*t);

    syncToggle.setToggleState (proc.sync.load(), juce::dontSendNotification);
    reverseToggle.setToggleState (proc.reverse.load(), juce::dontSendNotification);
    oneShotToggle.setToggleState (proc.oneShot.load(), juce::dontSendNotification);
    syncToggle.onClick = [this] { proc.sync.store (syncToggle.getToggleState()); };
    reverseToggle.onClick = [this] { proc.reverse.store (reverseToggle.getToggleState()); };
    oneShotToggle.onClick = [this] { proc.oneShot.store (oneShotToggle.getToggleState()); };

    // --- textos ---
    addAndMakeVisible (fileLabel);
    addAndMakeVisible (infoLabel);
    addAndMakeVisible (hintLabel);
    infoLabel.setColour (juce::Label::textColourId, juce::Colour (0xff6fd6a4));
    infoLabel.setJustificationType (juce::Justification::topLeft);
    fileLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    hintLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    hintLabel.setFont (13.0f);
    hintLabel.setJustificationType (juce::Justification::topLeft);

    updateTexts();
    refreshPads();
    startTimerHz (30);
}

//==============================================================================
void ChopEasyEditor::updateTexts()
{
    loadButton.setButtonText (tr ("Load sample", "Carregar sample"));
    langButton.setButtonText (proc.language.load() == 0 ? "PT-BR" : "EN");
    chopBtn.setButtonText (tr ("Chop", "Cortar"));
    scratchBtn.setButtonText ("Scratch");

    detectBtn.setButtonText (tr ("Detect", "Detectar"));
    sensLabel.setText (tr ("Chop sensitivity", "Sensibilidade dos cortes"), juce::dontSendNotification);
    randomBtn.setButtonText (tr ("Random", "Aleatório"));
    clearBtn.setButtonText (tr ("Clear", "Limpar"));

    pitchLabel.setText (tr ("Pitch (semitones)", "Tom (semitons)"), juce::dontSendNotification);
    volLabel.setText (tr ("Volume", "Volume"), juce::dontSendNotification);
    bpmLabel.setText (tr ("Sample BPM", "BPM do sample"), juce::dontSendNotification);

    syncToggle.setButtonText (tr ("Sync to project BPM (changes pitch)",
                                  "Sincronizar com o BPM do projeto (muda o tom)"));
    reverseToggle.setButtonText (tr ("Reverse", "Reverso"));
    oneShotToggle.setButtonText (tr ("Play the whole chop", "Tocar o corte inteiro"));

    wave->emptyText = tr ("Drop an audio file here or click 'Load sample'",
                          "Solte um arquivo de áudio aqui ou clique em 'Carregar sample'");

    if (wave->scratchMode)
        hintLabel.setText (tr ("SCRATCH: click and drag left/right on the waveform. Hold still = silence, like a record.",
                               "SCRATCH: clique e arraste para os lados na waveform. Parado = silêncio, como num disco."),
                           juce::dontSendNotification);
    else
        hintLabel.setText (tr ("AUTO: chops are detected when you load a sample (use Sensitivity for more or fewer). MANUAL: click the waveform to set the start of the selected pad (it jumps to the next pad). "
                               "Drag to fine-tune. Play pads with the mouse or MIDI notes from C3 (FL Studio) upwards.",
                               "AUTO: os cortes são detectados sozinhos ao carregar o sample (use a Sensibilidade para mais ou menos cortes). MANUAL: clique na waveform para marcar o início do pad selecionado (pula para o próximo pad). "
                               "Arraste para ajustar. Toque os pads com o mouse ou com notas MIDI a partir de C3 (FL Studio)."),
                           juce::dontSendNotification);

    updateFileLabel();
    repaint();
}

void ChopEasyEditor::updateFileLabel()
{
    fileLabel.setText (proc.fileName.isEmpty() ? tr ("No sample loaded", "Nenhum sample carregado")
                                               : proc.fileName,
                       juce::dontSendNotification);

    juce::String info;
    if (proc.getNumLoaded() >= 4)
    {
        const int mode = proc.chopMode.load();
        if (mode == 1)
            info = juce::String (proc.detectedCount.load()) + tr (" chops detected automatically", " cortes detectados automaticamente");
        else if (mode == 2)
            info = tr ("No clear hits found: using equal slices", "Sem batidas claras: usando cortes iguais");
    }
    infoLabel.setText (info, juce::dontSendNotification);
}

void ChopEasyEditor::refreshPads()
{
    wave->selectedPad = selectedPad;

    for (int i = 0; i < ChopEasyProcessor::numPads; ++i)
    {
        auto& pb = *padButtons[(size_t) i];
        pb.isSet = proc.pads[(size_t) i].isSet;
        pb.selected = (i == selectedPad);
        pb.repaint();
    }

    wave->repaint();
}

void ChopEasyEditor::timerCallback()
{
    const int lc = proc.loadCounter.load();
    const int pv = proc.padsVersion.load();
    const double pp = proc.uiPlayPos.load();

    const bool changed = (lc != lastLoad) || (pv != lastPads);
    if (changed)
    {
        lastLoad = lc;
        lastPads = pv;
        updateFileLabel();
        refreshPads();
    }

    if (pp != lastPlay)
    {
        lastPlay = pp;
        wave->repaint();
    }

    for (int i = 0; i < ChopEasyProcessor::numPads; ++i)
    {
        const bool playing = proc.padPlaying[(size_t) i].load();
        auto& pb = *padButtons[(size_t) i];
        if (playing != pb.playing)
        {
            pb.playing = playing;
            pb.repaint();
        }
    }
}

//==============================================================================
void ChopEasyEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> (tr ("Choose a sample", "Escolha um sample"),
                                                    juce::File(),
                                                    "*.wav;*.mp3;*.flac;*.ogg;*.aif;*.aiff;*.m4a");

    juce::Component::SafePointer<ChopEasyEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (safe != nullptr && f.existsAsFile())
                                  safe->loadFromFile (f);
                          });
}

void ChopEasyEditor::loadFromFile (const juce::File& f)
{
    if (proc.loadFile (f))
    {
        selectedPad = 0;
        updateFileLabel();
        refreshPads();
    }
    else
    {
        fileLabel.setText (tr ("Could not read this file", "Não consegui ler este arquivo"),
                           juce::dontSendNotification);
    }
}

bool ChopEasyEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() > 0;
}

void ChopEasyEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.size() > 0)
        loadFromFile (juce::File (files[0]));
}

//==============================================================================
void ChopEasyEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0f1115));

    g.setColour (juce::Colours::white);
    g.setFont (24.0f);
    g.drawText ("ChopEasy", 12, 10, 140, 34, juce::Justification::centredLeft);
}

void ChopEasyEditor::resized()
{
    auto r = getLocalBounds().reduced (10);

    auto top = r.removeFromTop (34);
    langButton.setBounds (top.removeFromRight (80));
    top.removeFromLeft (150);
    loadButton.setBounds (top.removeFromLeft (140));
    fileLabel.setBounds (top.reduced (8, 0));

    r.removeFromTop (6);
    wave->setBounds (r.removeFromTop (150));

    r.removeFromTop (8);
    auto modes = r.removeFromTop (28);
    chopBtn.setBounds (modes.removeFromLeft (110));
    modes.removeFromLeft (6);
    scratchBtn.setBounds (modes.removeFromLeft (110));

    r.removeFromTop (8);

    // pads 4x4 (pad 1 embaixo a esquerda, como numa MPC)
    auto padArea = r.removeFromLeft (290);
    const int cell = 70;
    for (int i = 0; i < ChopEasyProcessor::numPads; ++i)
    {
        const int col = i % 4;
        const int row = 3 - (i / 4);
        padButtons[(size_t) i]->setBounds (padArea.getX() + col * cell, padArea.getY() + row * cell, cell, cell);
    }

    infoLabel.setBounds (padArea.getX(), padArea.getY() + 4 * cell + 6, padArea.getWidth(), 40);

    // painel da direita
    r.removeFromLeft (10);
    auto nextRow = [&r] (int h)
    {
        auto x = r.removeFromTop (h);
        r.removeFromTop (6);
        return x;
    };

    {
        auto a = nextRow (28);
        detectBtn.setBounds (a.removeFromLeft (100));
        a.removeFromLeft (8);
        slice4.setBounds (a.removeFromLeft (40));
        a.removeFromLeft (4);
        slice8.setBounds (a.removeFromLeft (40));
        a.removeFromLeft (4);
        slice16.setBounds (a.removeFromLeft (40));
        a.removeFromLeft (10);
        randomBtn.setBounds (a.removeFromLeft (90));
        a.removeFromLeft (4);
        clearBtn.setBounds (a.removeFromLeft (70));
    }

    auto sliderRow = [&nextRow] (juce::Label& l, juce::Slider& s)
    {
        auto a = nextRow (26);
        l.setBounds (a.removeFromLeft (150));
        s.setBounds (a);
    };
    sliderRow (sensLabel, sensSlider);
    sliderRow (pitchLabel, pitchSlider);
    sliderRow (volLabel, volSlider);
    sliderRow (bpmLabel, bpmSlider);

    syncToggle.setBounds (nextRow (24));
    reverseToggle.setBounds (nextRow (24));
    oneShotToggle.setBounds (nextRow (24));

    hintLabel.setBounds (r);
}
