#include "PluginEditor.h"

#include <cmath>

namespace
{
    const juce::Colour bg       { 0xff050914 };
    const juce::Colour panel    { 0xff080f20 };
    const juce::Colour panel2   { 0xff0b1428 };
    const juce::Colour border   { 0xff1a3158 };
    const juce::Colour cyan     { 0xff23d9ff };
    const juce::Colour blue     { 0xff287bff };
    const juce::Colour purple   { 0xffbd43ff };
    const juce::Colour text     { 0xffedf5ff };
    const juce::Colour muted    { 0xff8ca5ca };

    // juce::Font(float, int) is deprecated in JUCE 8; build fonts from FontOptions.
    juce::Font makeFont(float height, int styleFlags = juce::Font::plain)
    {
        return juce::Font(juce::FontOptions(height, styleFlags));
    }

    void drawPanel(juce::Graphics& g, juce::Rectangle<float> r, float radius = 14.0f)
    {
        g.setColour(juce::Colour(0x60000000));
        g.fillRoundedRectangle(r.translated(0.0f, 4.0f), radius);
        g.setColour(panel.withAlpha(0.98f));
        g.fillRoundedRectangle(r, radius);
        g.setColour(border.withAlpha(0.9f));
        g.drawRoundedRectangle(r, radius, 1.0f);
    }
}

DARVCEditor::DARVCEditor(DARVCProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(1180, 780);
    setResizable(true, true);
    setResizeLimits(900, 650, 1600, 1100);

    for (auto* b : { &fromDawButton, &fromFileButton, &chooseFileButton,
                     &browseModelButton, &convertButton, &outputButton,
                     &modelsButton, &libraryButton, &settingsButton })
        addAndMakeVisible(*b);

    styleButton(convertButton, true);
    for (auto* b : { &fromDawButton, &fromFileButton, &chooseFileButton,
                     &browseModelButton, &outputButton, &modelsButton,
                     &libraryButton, &settingsButton }) styleButton(*b);

    // The two input-mode buttons sit side by side, so only their touching
    // edges should be "connected" (square); all other buttons keep fully
    // rounded corners.
    fromDawButton.setConnectedEdges(juce::Button::ConnectedOnRight);
    fromFileButton.setConnectedEdges(juce::Button::ConnectedOnLeft);

    fromDawButton.setClickingTogglesState(true);
    fromFileButton.setClickingTogglesState(true);
    fromDawButton.setToggleState(true, juce::dontSendNotification);
    fromDawButton.onClick = [this]
    {
        inputMode = "From DAW";
        fromDawButton.setToggleState(true, juce::dontSendNotification);
        fromFileButton.setToggleState(false, juce::dontSendNotification);
        setStatus("DAW capture is not connected in this build");
    };
    fromFileButton.onClick = [this]
    {
        inputMode = "From File";
        fromFileButton.setToggleState(true, juce::dontSendNotification);
        fromDawButton.setToggleState(false, juce::dontSendNotification);
        chooseInputFile();
    };
    chooseFileButton.onClick = [this] { chooseInputFile(); };
    browseModelButton.onClick = [this] { chooseModelFile(); };
    convertButton.onClick = [this] { requestConversion(); };
    outputButton.onClick = [this]
    {
        if (inputFile.existsAsFile())
            inputFile.getParentDirectory().revealToUser();
        else
            setStatus("No output has been created yet");
    };
    modelsButton.onClick = [this] { setStatus("Model manager is not available in this build. Use Browse to pick a .pth file."); };
    libraryButton.onClick = [this] { setStatus("Library is not available in this build."); };
    settingsButton.onClick = [this] { setStatus("Settings are not available in this build."); };

    f0Box.addItem("RMVPE", 1);
    f0Box.addItem("Harvest", 2);
    f0Box.addItem("PM", 3);
    f0Box.setSelectedId(1);
    f0Box.setColour(juce::ComboBox::backgroundColourId, panel2);
    f0Box.setColour(juce::ComboBox::textColourId, text);
    f0Box.setColour(juce::ComboBox::outlineColourId, border);
    addAndMakeVisible(f0Box);

    setupSlider(pitchSlider, -12, 12, 0, " semitones");
    setupSlider(indexSlider, 0, 100, 75, "%");
    setupSlider(protectSlider, 0, 50, 33, "%");
    for (auto* s : { &pitchSlider, &indexSlider, &protectSlider }) addAndMakeVisible(*s);

    auto setupLabel = [this](juce::Label& l, const juce::String& t, int size, juce::Colour c = text)
    {
        l.setText(t, juce::dontSendNotification);
        l.setColour(juce::Label::textColourId, c);
        l.setFont(makeFont(static_cast<float>(size)));
        addAndMakeVisible(l);
    };
    setupLabel(inputLabel, "INPUT AUDIO", 12, cyan);
    setupLabel(modelLabel, selectedModel, 13, text);
    setupLabel(statusLabel, statusText, 12, muted);
    setupLabel(outputLabel, "Conversion Output  •  No output yet", 12, muted);
    setupLabel(pitchLabel, "Pitch Shift", 12, text);
    setupLabel(indexLabel, "Index Rate", 12, text);
    setupLabel(protectLabel, "Protect", 12, text);
}

DARVCEditor::~DARVCEditor() = default;

void DARVCEditor::setStatus(const juce::String& newStatus)
{
    statusText = newStatus;
    statusLabel.setText(statusText, juce::dontSendNotification);
    repaint();
}

void DARVCEditor::styleButton(juce::TextButton& b, bool primary)
{
    b.setColour(juce::TextButton::buttonColourId, primary ? juce::Colour(0xff173d91) : panel2);
    b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff164c86));
    b.setColour(juce::TextButton::textColourOffId, text);
    b.setColour(juce::TextButton::textColourOnId, text);
}

void DARVCEditor::setupSlider(juce::Slider& s, double min, double max, double value, const juce::String& suffix)
{
    s.setRange(min, max, 1.0);
    s.setValue(value, juce::dontSendNotification);
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 76, 22);
    s.setTextValueSuffix(suffix);
    s.setColour(juce::Slider::trackColourId, blue);
    s.setColour(juce::Slider::thumbColourId, cyan);
    s.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff14223b));
    s.setColour(juce::Slider::textBoxTextColourId, text);
    s.setColour(juce::Slider::textBoxBackgroundColourId, panel2);
    s.setColour(juce::Slider::textBoxOutlineColourId, border);
}

void DARVCEditor::paint(juce::Graphics& g)
{
    juce::ColourGradient background(juce::Colour(0xff0a1734), 0.0f, 0.0f,
                                    bg, static_cast<float>(getWidth()),
                                    static_cast<float>(getHeight()), false);
    g.setGradientFill(background);
    g.fillAll();

    // Header and main panels
    drawPanel(g, { 18.0f, 18.0f, static_cast<float>(getWidth()) - 36.0f, 72.0f }, 16.0f);
    drawPanel(g, { 18.0f, 102.0f, 435.0f, static_cast<float>(getHeight()) - 190.0f }, 16.0f);
    drawPanel(g, { 465.0f, 102.0f, static_cast<float>(getWidth()) - 483.0f, static_cast<float>(getHeight()) - 190.0f }, 16.0f);
    drawPanel(g, { 36.0f, static_cast<float>(getHeight()) - 76.0f, static_cast<float>(getWidth()) - 72.0f, 54.0f }, 13.0f);

    g.setColour(text);
    g.setFont(makeFont(30.0f));
    g.drawText("D A R V C", 38, 31, 220, 34, juce::Justification::centredLeft);
    g.setColour(muted);
    g.setFont(makeFont(10.5f));
    g.drawText("RVC VOICE CONVERTER   •   AI POWERED", 260, 36, 310, 24, juce::Justification::centredLeft);

    g.setColour(cyan.withAlpha(0.8f));
    g.fillRoundedRectangle(34.0f, 117.0f, 3.0f, 56.0f, 1.5f);
    g.setColour(cyan);
    g.setFont(makeFont(12.0f, juce::Font::bold));
    g.drawText("01   INPUT & MODEL", 52, 118, 350, 22, juce::Justification::centredLeft);
    g.setColour(muted);
    g.setFont(makeFont(11.0f));
    g.drawText("ADVANCED SETTINGS", 52, 358, 350, 22, juce::Justification::centredLeft);

    // Neon circular audio visualization (decorative preview)
    const float cx = 805.0f + (static_cast<float>(getWidth()) - 1180.0f) * 0.28f;
    const float cy = 350.0f;
    const float radius = juce::jmin(190.0f, static_cast<float>(getHeight()) * 0.27f);
    for (int ring = 0; ring < 4; ++ring)
    {
        const float r = radius - static_cast<float>(ring) * 18.0f;
        juce::Path path;
        for (int i = 0; i <= 240; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * static_cast<float>(i) / 240.0f;
            const float wobble = 1.0f + 0.045f * std::sin(a * (7.0f + static_cast<float>(ring)) + static_cast<float>(ring)) + 0.018f * std::sin(a * 13.0f);
            const float x = cx + std::cos(a) * r * wobble;
            const float y = cy + std::sin(a) * r * wobble;
            if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
        }
        path.closeSubPath();
        const auto colour = ring % 2 == 0 ? cyan : purple;
        g.setColour(colour.withAlpha(0.12f));
        g.strokePath(path, juce::PathStrokeType(10.0f));
        g.setColour(colour.withAlpha(0.82f));
        g.strokePath(path, juce::PathStrokeType(2.2f));
    }
    g.setColour(juce::Colour(0xff071126));
    g.fillEllipse(cx - 69.0f, cy - 69.0f, 138.0f, 138.0f);
    g.setColour(border);
    g.drawEllipse(cx - 69.0f, cy - 69.0f, 138.0f, 138.0f, 1.2f);
    g.setColour(text);
    g.setFont(makeFont(20.0f));
    g.drawText("DARVC", static_cast<int>(cx) - 58, static_cast<int>(cy) - 14, 116, 28, juce::Justification::centred);

    g.setColour(muted);
    g.setFont(makeFont(11.0f));
    g.drawText("OUTPUT PREVIEW", 500, 122, 200, 20, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xff43e6a8));
    g.fillEllipse(static_cast<float>(getWidth()) - 145.0f, 126.0f, 8.0f, 8.0f);
    g.setColour(text);
    g.drawText("READY", getWidth() - 130, 120, 90, 20, juce::Justification::centredLeft);

    g.setColour(muted);
    g.drawText("00:00  /  --:--", 505, getHeight() - 174, 160, 22, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xff172744));
    g.fillRoundedRectangle(505.0f, static_cast<float>(getHeight()) - 144.0f, static_cast<float>(getWidth()) - 550.0f, 4.0f, 2.0f);
    g.setColour(blue);
    g.fillRoundedRectangle(505.0f, static_cast<float>(getHeight()) - 144.0f, 24.0f, 4.0f, 2.0f);
}

void DARVCEditor::resized()
{
    const int w = getWidth();
    const int h = getHeight();
    fromDawButton.setBounds(48, 157, 175, 34);
    fromFileButton.setBounds(223, 157, 175, 34);
    chooseFileButton.setBounds(48, 204, 350, 40);
    modelLabel.setBounds(52, 260, 240, 26);
    browseModelButton.setBounds(316, 256, 82, 32);
    pitchLabel.setBounds(52, 390, 120, 20);
    pitchSlider.setBounds(52, 410, 350, 36);
    indexLabel.setBounds(52, 458, 120, 20);
    indexSlider.setBounds(52, 478, 350, 36);
    protectLabel.setBounds(52, 526, 120, 20);
    protectSlider.setBounds(52, 546, 350, 36);
    f0Box.setBounds(52, 600, 350, 34);
    convertButton.setBounds(w / 2 + 15, h - 126, 280, 48);
    outputButton.setBounds(w - 182, h - 66, 130, 30);
    modelsButton.setBounds(w - 370, 38, 76, 30);
    libraryButton.setBounds(w - 284, 38, 76, 30);
    settingsButton.setBounds(w - 198, 38, 92, 30);
    inputLabel.setBounds(52, 130, 300, 18);
    statusLabel.setBounds(52, h - 65, w - 280, 26);
    outputLabel.setBounds(52, h - 42, w - 280, 20);
}

void DARVCEditor::chooseInputFile()
{
    fileChooser = std::make_unique<juce::FileChooser>("Select input audio", juce::File{}, "*.wav;*.flac;*.aif;*.aiff");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<DARVCEditor>(this)](const juce::FileChooser& chooser)
        {
            // The editor may have been closed while the dialog was open.
            if (safeThis == nullptr)
                return;

            const auto file = chooser.getResult();
            if (file.existsAsFile())
            {
                safeThis->inputFile = file;
                safeThis->chooseFileButton.setButtonText(file.getFileName());
                safeThis->setStatus("Input selected. Conversion engine is not connected yet.");
            }
        });
}

void DARVCEditor::chooseModelFile()
{
    fileChooser = std::make_unique<juce::FileChooser>("Select RVC .pth model", juce::File{}, "*.pth");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<DARVCEditor>(this)](const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;

            const auto file = chooser.getResult();
            if (file.existsAsFile())
            {
                safeThis->selectedModel = file.getFileName();
                safeThis->modelLabel.setText(safeThis->selectedModel, juce::dontSendNotification);
                safeThis->setStatus("Model selected. Index file can be configured in a later build.");
            }
        });
}

void DARVCEditor::requestConversion()
{
    if (!inputFile.existsAsFile()) setStatus("Choose an input WAV first.");
    else if (selectedModel == "No model selected") setStatus("Choose an RVC .pth model first.");
    else setStatus("UI prototype only: RVC worker is not connected; no conversion was run.");
}
