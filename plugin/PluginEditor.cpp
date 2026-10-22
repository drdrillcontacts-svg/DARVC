#include "PluginEditor.h"

#include <cmath>
#include <utility>

using namespace DarvcColours;

namespace
{
    void drawPanel(juce::Graphics& g, juce::Rectangle<float> r, float radius = 14.0f)
    {
        g.setColour(juce::Colour(0x60000000));
        g.fillRoundedRectangle(r.translated(0.0f, 4.0f), radius);
        g.setColour(panel.withAlpha(0.98f));
        g.fillRoundedRectangle(r, radius);
        g.setColour(border.withAlpha(0.9f));
        g.drawRoundedRectangle(r, radius, 1.0f);
    }

    juce::String formatDuration(double seconds)
    {
        if (!(seconds > 0.0))
            return "--:--";
        const int total = static_cast<int>(seconds + 0.5);
        return juce::String::formatted("%02d:%02d", total / 60, total % 60);
    }

    //==========================================================================
    /** Dimmed full-editor overlay with a centred card. The Models, Library
        and Settings panels are built on this instead of separate windows,
        which hosts handle much more reliably for plug-in editors. */
    class PanelBase : public juce::Component
    {
    public:
        PanelBase(juce::String titleToUse, std::function<void()> closeCallback)
            : title(std::move(titleToUse)), onClose(std::move(closeCallback))
        {
            closeButton.onClick = [this] { if (onClose) onClose(); };
            addAndMakeVisible(closeButton);
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colours::black.withAlpha(0.68f));
            const auto card = cardBounds().toFloat();
            g.setColour(juce::Colour(0xf2080f20));
            g.fillRoundedRectangle(card, 16.0f);
            g.setColour(border);
            g.drawRoundedRectangle(card, 16.0f, 1.0f);
            g.setColour(cyan);
            g.setFont(darvcFont(15.0f, juce::Font::bold));
            g.drawText(title, cardBounds().getX() + 20, cardBounds().getY() + 14,
                       400, 24, juce::Justification::centredLeft);
        }

        void resized() final
        {
            const auto card = cardBounds();
            closeButton.setBounds(card.getRight() - 96, card.getY() + 14, 76, 28);
            layoutContent(contentBounds());
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            if (!cardBounds().contains(e.getPosition()) && onClose)
                onClose();
        }

        virtual void refresh() {}

    protected:
        virtual void layoutContent(juce::Rectangle<int>) = 0;

        [[nodiscard]] juce::Rectangle<int> cardBounds() const
        {
            const auto bounds = getLocalBounds();
            const int w = juce::jmin(850, bounds.getWidth() - 48);
            const int h = juce::jmin(570, bounds.getHeight() - 48);
            return { (bounds.getWidth() - w) / 2, (bounds.getHeight() - h) / 2, w, h };
        }

        [[nodiscard]] juce::Rectangle<int> contentBounds() const
        {
            return cardBounds().reduced(20).withTrimmedTop(30);
        }

    private:
        juce::String title;
        std::function<void()> onClose;
        juce::TextButton closeButton { "Close" };
    };

    //==========================================================================
    class ModelsPanel final : public PanelBase, private juce::ListBoxModel
    {
    public:
        ModelsPanel(DARVCProcessor& procToUse,
                    std::function<void()> closeCallback,
                    std::function<void(juce::File)> modelChosen,
                    std::function<void(juce::String)> statusCallback)
            : PanelBase("MODELS", std::move(closeCallback)),
              proc(procToUse),
              onModelChosen(std::move(modelChosen)),
              onStatus(std::move(statusCallback))
        {
            list.setModel(this);
            list.setRowHeight(46);
            list.setColour(juce::ListBox::backgroundColourId, panel2);
            addAndMakeVisible(list);

            hint.setText("Models folder: " + proc.settings.modelsDir.getFullPathName(),
                         juce::dontSendNotification);
            hint.setColour(juce::Label::textColourId, muted);
            hint.setFont(darvcFont(11.5f));
            addAndMakeVisible(hint);

            for (auto* b : { &addButton, &folderButton, &openButton, &useButton })
                addAndMakeVisible(*b);

            addButton.onClick = [this] { addFiles(); };
            folderButton.onClick = [this] { chooseFolder(); };
            openButton.onClick = [this] { proc.settings.modelsDir.revealToUser(); };
            useButton.onClick = [this] { chooseRow(list.getSelectedRow()); };

            refresh();
        }

        void refresh() override
        {
            auto found = proc.scanModels();
            if (found != files)
            {
                files = std::move(found);
                list.updateContent();
            }
            const int current = files.indexOf(proc.settings.modelFile);
            if (current >= 0 && list.getSelectedRow() != current)
                list.selectRow(current, false, true);
            hint.setText("Models folder: " + proc.settings.modelsDir.getFullPathName(),
                         juce::dontSendNotification);
        }

    private:
        DARVCProcessor& proc;
        std::function<void(juce::File)> onModelChosen;
        std::function<void(juce::String)> onStatus;
        juce::Array<juce::File> files;
        juce::ListBox list;
        juce::Label hint;
        juce::TextButton addButton { "Add Files..." }, folderButton { "Folder..." },
            openButton { "Open Folder" }, useButton { "Use Selected" };
        std::unique_ptr<juce::FileChooser> chooser;

        void layoutContent(juce::Rectangle<int> area) override
        {
            hint.setBounds(area.removeFromTop(20));
            area.removeFromTop(6);
            auto buttons = area.removeFromBottom(34);
            area.removeFromBottom(8);
            list.setBounds(area);
            addButton.setBounds(buttons.removeFromLeft(120));
            buttons.removeFromLeft(8);
            folderButton.setBounds(buttons.removeFromLeft(110));
            buttons.removeFromLeft(8);
            openButton.setBounds(buttons.removeFromLeft(120));
            useButton.setBounds(buttons.removeFromRight(130));
        }

        int getNumRows() override { return files.size(); }

        void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
        {
            if (!juce::isPositiveAndBelow(row, files.size()))
                return;
            const auto& file = files[row];
            if (selected)
            {
                g.setColour(juce::Colour(0xff164c86).withAlpha(0.55f));
                g.fillRect(0, 0, width, height);
            }
            const bool inUse = file == proc.settings.modelFile;
            g.setColour(inUse ? cyan : text);
            g.setFont(darvcFont(14.0f, inUse ? juce::Font::bold : juce::Font::plain));
            g.drawText(file.getFileName(), 12, 4, width - 24, 20,
                       juce::Justification::centredLeft, true);

            auto detail = file.getParentDirectory().getFileName() + "  " + darvcBullet() + "  "
                          + juce::File::descriptionOfSizeInBytes(file.getSize());
            if (DARVCProcessor::findSiblingIndex(file).existsAsFile())
                detail += "  " + darvcBullet() + "  index file";
            if (inUse)
                detail += "  " + darvcBullet() + "  in use";
            g.setColour(muted);
            g.setFont(darvcFont(11.5f));
            g.drawText(detail, 12, 24, width - 24, 18, juce::Justification::centredLeft, true);
        }

        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override { chooseRow(row); }

        void chooseRow(int row)
        {
            if (juce::isPositiveAndBelow(row, files.size()) && onModelChosen)
                onModelChosen(files[row]);
        }

        void addFiles()
        {
            chooser = std::make_unique<juce::FileChooser>(
                "Add model files (.pth / .index / .zip)", juce::File{}, "*.pth;*.index;*.zip");
            chooser->launchAsync(
                juce::FileBrowserComponent::openMode
                    | juce::FileBrowserComponent::canSelectFiles
                    | juce::FileBrowserComponent::canSelectMultipleItems,
                [safeThis = juce::Component::SafePointer<ModelsPanel>(this)](
                    const juce::FileChooser& fc) {
                    if (safeThis == nullptr)
                        return;
                    juce::Array<juce::File> chosen;
                    for (const auto& file : fc.getResults())
                        chosen.add(file);
                    safeThis->importFiles(chosen);
                });
        }

        void importFiles(const juce::Array<juce::File>& chosen)
        {
            juce::File firstModel;
            int imported = 0;
            for (const auto& file : chosen)
            {
                if (!file.existsAsFile())
                    continue;
                if (file.hasFileExtension("zip"))
                {
                    juce::File modelFromZip;
                    imported += extractModelZip(file, modelFromZip);
                    if (!firstModel.existsAsFile())
                        firstModel = modelFromZip;
                }
                else if (file.getParentDirectory() == proc.settings.modelsDir)
                {
                    if (file.hasFileExtension("pth") && !firstModel.existsAsFile())
                        firstModel = file;
                }
                else
                {
                    const auto target = proc.settings.modelsDir
                        .getNonexistentChildFile(file.getFileNameWithoutExtension(),
                                                 file.getFileExtension(), false);
                    if (file.copyFileTo(target))
                    {
                        ++imported;
                        if (target.hasFileExtension("pth") && !firstModel.existsAsFile())
                            firstModel = target;
                    }
                }
            }
            refresh();
            if (onStatus)
                onStatus(imported > 0 ? "Imported " + juce::String(imported) + " model file(s)."
                                      : "Nothing was imported.");
            if (firstModel.existsAsFile())
                chooseRow(files.indexOf(firstModel));
        }

        int extractModelZip(const juce::File& zipFile, juce::File& modelOut)
        {
            juce::ZipFile zip(zipFile);
            const auto dest = proc.settings.modelsDir.getChildFile(zipFile.getFileNameWithoutExtension());
            dest.createDirectory();
            int count = 0;
            for (int i = 0; i < zip.getNumEntries(); ++i)
            {
                const auto* entry = zip.getEntry(i);
                if (entry == nullptr || entry->filename.endsWithChar('/'))
                    continue;
                // Only model files, flattened (also blocks zip-slip paths).
                const juce::File entryName(entry->filename);
                if (!entryName.hasFileExtension("pth;index"))
                    continue;
                const auto target = dest.getChildFile(entryName.getFileName());
                if (auto stream = zip.createStreamForEntry(i))
                {
                    juce::FileOutputStream out(target);
                    if (out.openedOk() && out.writeFromInputStream(*stream, -1) >= 0)
                    {
                        ++count;
                        if (target.hasFileExtension("pth"))
                            modelOut = target;
                    }
                }
            }
            return count;
        }

        void chooseFolder()
        {
            chooser = std::make_unique<juce::FileChooser>("Models folder", proc.settings.modelsDir);
            chooser->launchAsync(
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                [safeThis = juce::Component::SafePointer<ModelsPanel>(this)](
                    const juce::FileChooser& fc) {
                    if (safeThis == nullptr)
                        return;
                    const auto dir = fc.getResult();
                    if (dir.isDirectory())
                    {
                        safeThis->proc.settings.modelsDir = dir;
                        safeThis->proc.saveSettings();
                        safeThis->refresh();
                    }
                });
        }
    };

    //==========================================================================
    class LibraryPanel final : public PanelBase, private juce::ListBoxModel
    {
    public:
        LibraryPanel(DARVCProcessor& procToUse,
                     std::function<void()> closeCallback,
                     std::function<void(juce::File)> loadAsInput,
                     std::function<void(juce::String)> statusCallback)
            : PanelBase("LIBRARY", std::move(closeCallback)),
              proc(procToUse),
              onLoadAsInput(std::move(loadAsInput)),
              onStatus(std::move(statusCallback))
        {
            list.setModel(this);
            list.setRowHeight(46);
            list.setColour(juce::ListBox::backgroundColourId, panel2);
            addAndMakeVisible(list);

            hint.setText("Converted files are saved here: " + proc.settings.libraryDir.getFullPathName(),
                         juce::dontSendNotification);
            hint.setColour(juce::Label::textColourId, muted);
            hint.setFont(darvcFont(11.5f));
            addAndMakeVisible(hint);

            for (auto* b : { &loadButton, &revealButton, &deleteButton, &openButton })
                addAndMakeVisible(*b);

            loadButton.onClick = [this] { loadRow(list.getSelectedRow()); };
            revealButton.onClick = [this] {
                const int row = list.getSelectedRow();
                if (juce::isPositiveAndBelow(row, files.size()))
                    files[row].revealToUser();
            };
            deleteButton.onClick = [this] {
                const int row = list.getSelectedRow();
                if (juce::isPositiveAndBelow(row, files.size()))
                {
                    const auto name = files[row].getFileName();
                    if (files[row].moveToTrash() && onStatus)
                        onStatus("Moved to trash: " + name);
                    refresh();
                }
            };
            openButton.onClick = [this] { proc.settings.libraryDir.revealToUser(); };

            refresh();
        }

        void refresh() override
        {
            auto found = proc.scanLibrary();
            if (found != files)
            {
                files = std::move(found);
                list.updateContent();
                list.repaint();
            }
        }

    private:
        DARVCProcessor& proc;
        std::function<void(juce::File)> onLoadAsInput;
        std::function<void(juce::String)> onStatus;
        juce::Array<juce::File> files;
        juce::ListBox list;
        juce::Label hint;
        juce::TextButton loadButton { "Load as Input" }, revealButton { "Reveal" },
            deleteButton { "Delete" }, openButton { "Open Folder" };

        void layoutContent(juce::Rectangle<int> area) override
        {
            hint.setBounds(area.removeFromTop(20));
            area.removeFromTop(6);
            auto buttons = area.removeFromBottom(34);
            area.removeFromBottom(8);
            list.setBounds(area);
            loadButton.setBounds(buttons.removeFromLeft(130));
            buttons.removeFromLeft(8);
            revealButton.setBounds(buttons.removeFromLeft(100));
            buttons.removeFromLeft(8);
            deleteButton.setBounds(buttons.removeFromLeft(100));
            openButton.setBounds(buttons.removeFromRight(120));
        }

        int getNumRows() override { return files.size(); }

        void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
        {
            if (!juce::isPositiveAndBelow(row, files.size()))
                return;
            const auto& file = files[row];
            if (selected)
            {
                g.setColour(juce::Colour(0xff164c86).withAlpha(0.55f));
                g.fillRect(0, 0, width, height);
            }
            g.setColour(text);
            g.setFont(darvcFont(14.0f));
            g.drawText(file.getFileName(), 12, 4, width - 24, 20,
                       juce::Justification::centredLeft, true);
            const auto detail = file.getLastModificationTime().toString(true, true, false, true)
                                + "  " + darvcBullet() + "  "
                                + juce::File::descriptionOfSizeInBytes(file.getSize());
            g.setColour(muted);
            g.setFont(darvcFont(11.5f));
            g.drawText(detail, 12, 24, width - 24, 18, juce::Justification::centredLeft, true);
        }

        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override { loadRow(row); }

        void loadRow(int row)
        {
            if (juce::isPositiveAndBelow(row, files.size()) && onLoadAsInput)
                onLoadAsInput(files[row]);
        }
    };

    //==========================================================================
    class SettingsPanel final : public PanelBase
    {
    public:
        SettingsPanel(DARVCProcessor& procToUse,
                      std::function<void()> closeCallback,
                      std::function<void()> settingsChanged)
            : PanelBase("SETTINGS", std::move(closeCallback)),
              proc(procToUse),
              onSettingsChanged(std::move(settingsChanged))
        {
            auto setupField = [this](juce::Label& label, const juce::String& textToUse,
                                     juce::TextEditor& editor, juce::TextButton& browse) {
                label.setText(textToUse, juce::dontSendNotification);
                label.setColour(juce::Label::textColourId, muted);
                label.setFont(darvcFont(12.0f));
                addAndMakeVisible(label);
                editor.setFont(darvcFont(12.5f));
                addAndMakeVisible(editor);
                addAndMakeVisible(browse);
            };
            setupField(pythonLabel, "Python (blank = auto)", pythonEd, pythonBrowse);
            setupField(modelsLabel, "Models folder", modelsEd, modelsBrowse);
            setupField(libraryLabel, "Output / Library folder", libraryEd, libraryBrowse);

            pythonEd.setText(proc.settings.pythonExe.getFullPathName(), juce::dontSendNotification);
            modelsEd.setText(proc.settings.modelsDir.getFullPathName(), juce::dontSendNotification);
            libraryEd.setText(proc.settings.libraryDir.getFullPathName(), juce::dontSendNotification);
            for (auto* ed : { &pythonEd, &modelsEd, &libraryEd })
                ed->onFocusLost = [this] { apply(); };

            pythonBrowse.onClick = [this] { browseFor(pythonEd, false); };
            modelsBrowse.onClick = [this] { browseFor(modelsEd, true); };
            libraryBrowse.onClick = [this] { browseFor(libraryEd, true); };

            deviceLabel.setText("Device", juce::dontSendNotification);
            deviceLabel.setColour(juce::Label::textColourId, muted);
            deviceLabel.setFont(darvcFont(12.0f));
            addAndMakeVisible(deviceLabel);
            deviceBox.addItem("Auto", 1);
            deviceBox.addItem("CPU", 2);
            deviceBox.addItem("NVIDIA CUDA", 3);
            deviceBox.addItem("Apple MPS", 4);
            deviceBox.setSelectedId(proc.settings.device == "cpu" ? 2
                                        : proc.settings.device == "cuda" ? 3
                                        : proc.settings.device == "mps" ? 4 : 1,
                                    juce::dontSendNotification);
            deviceBox.onChange = [this] { apply(); };
            addAndMakeVisible(deviceBox);

            for (auto* b : { &checkButton, &installButton, &cancelButton })
                addAndMakeVisible(*b);
            checkButton.onClick = [this] { apply(); proc.engine.startCheck(); refresh(); };
            installButton.onClick = [this] {
                apply();
                proc.engine.startInstall(deviceBox.getSelectedId() == 3);
                refresh();
            };
            cancelButton.onClick = [this] { proc.engine.cancel(); };

            engineStatus.setColour(juce::Label::textColourId, text);
            engineStatus.setFont(darvcFont(12.5f));
            addAndMakeVisible(engineStatus);

            logBox.setMultiLine(true);
            logBox.setReadOnly(true);
            logBox.setScrollbarsShown(true);
            logBox.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                                        11.0f, juce::Font::plain)));
            addAndMakeVisible(logBox);

            note.setText("The RVC engine (PyTorch + rvc-python, ~2-5 GB) is installed once into the "
                         "DARVC data folder and needs Python 3.10-3.12 plus an internet connection. "
                         "Choose NVIDIA CUDA before installing if you have a compatible GPU.",
                         juce::dontSendNotification);
            note.setColour(juce::Label::textColourId, muted);
            note.setFont(darvcFont(11.5f));
            note.setJustificationType(juce::Justification::topLeft);
            addAndMakeVisible(note);

            refresh();
        }

        void refresh() override
        {
            const bool busy = proc.engine.isBusy();
            checkButton.setEnabled(!busy);
            installButton.setEnabled(!busy);
            cancelButton.setEnabled(busy);

            auto message = busy ? proc.engine.getStatusMessage() : proc.engine.getLastMessage();
            if (message.isEmpty())
                message = "Press 'Check Engine' to test the current Python environment.";
            engineStatus.setText(message, juce::dontSendNotification);

            const auto logs = proc.engine.getRecentLog();
            if (logs.size() != lastLogCount)
            {
                lastLogCount = logs.size();
                logBox.setText(logs.joinIntoString("\n"), juce::dontSendNotification);
                logBox.moveCaretToEnd();
            }
        }

    private:
        DARVCProcessor& proc;
        std::function<void()> onSettingsChanged;
        juce::Label pythonLabel, modelsLabel, libraryLabel, deviceLabel, engineStatus, note;
        juce::TextEditor pythonEd, modelsEd, libraryEd, logBox;
        juce::TextButton pythonBrowse { "..." }, modelsBrowse { "..." }, libraryBrowse { "..." };
        juce::TextButton checkButton { "Check Engine" },
            installButton { "Install / Repair Engine" }, cancelButton { "Cancel Task" };
        juce::ComboBox deviceBox;
        std::unique_ptr<juce::FileChooser> chooser;
        int lastLogCount = -1;

        void layoutContent(juce::Rectangle<int> area) override
        {
            auto row = [&area](juce::Label& label, juce::TextEditor& editor, juce::TextButton& browse) {
                auto r = area.removeFromTop(32);
                label.setBounds(r.removeFromLeft(150));
                browse.setBounds(r.removeFromRight(40));
                r.removeFromRight(6);
                editor.setBounds(r);
                area.removeFromTop(8);
            };
            row(pythonLabel, pythonEd, pythonBrowse);
            row(modelsLabel, modelsEd, modelsBrowse);
            row(libraryLabel, libraryEd, libraryBrowse);

            auto deviceRow = area.removeFromTop(32);
            deviceLabel.setBounds(deviceRow.removeFromLeft(150));
            deviceBox.setBounds(deviceRow.removeFromLeft(170));
            deviceRow.removeFromLeft(12);
            checkButton.setBounds(deviceRow.removeFromLeft(120));
            deviceRow.removeFromLeft(8);
            installButton.setBounds(deviceRow.removeFromLeft(190));
            deviceRow.removeFromLeft(8);
            cancelButton.setBounds(deviceRow.removeFromLeft(110));
            area.removeFromTop(10);

            engineStatus.setBounds(area.removeFromTop(22));
            area.removeFromTop(6);
            note.setBounds(area.removeFromBottom(44));
            logBox.setBounds(area);
        }

        void apply()
        {
            proc.settings.pythonExe = { pythonEd.getText().trim() };
            if (modelsEd.getText().trim().isNotEmpty())
                proc.settings.modelsDir = { modelsEd.getText().trim() };
            if (libraryEd.getText().trim().isNotEmpty())
                proc.settings.libraryDir = { libraryEd.getText().trim() };
            proc.settings.modelsDir.createDirectory();
            proc.settings.libraryDir.createDirectory();
            switch (deviceBox.getSelectedId())
            {
                case 2: proc.settings.device = "cpu"; break;
                case 3: proc.settings.device = "cuda"; break;
                case 4: proc.settings.device = "mps"; break;
                default: proc.settings.device = "auto"; break;
            }
            proc.saveSettings();
            if (onSettingsChanged)
                onSettingsChanged();
        }

        void browseFor(juce::TextEditor& editor, bool directory)
        {
            chooser = std::make_unique<juce::FileChooser>("Select", juce::File(editor.getText()));
            const int flags = juce::FileBrowserComponent::openMode
                              | (directory ? juce::FileBrowserComponent::canSelectDirectories
                                           : juce::FileBrowserComponent::canSelectFiles);
            chooser->launchAsync(flags,
                [safeThis = juce::Component::SafePointer<SettingsPanel>(this), &editor](
                    const juce::FileChooser& fc) {
                    if (safeThis == nullptr)
                        return;
                    const auto result = fc.getResult();
                    if (result != juce::File{})
                    {
                        editor.setText(result.getFullPathName(), juce::dontSendNotification);
                        safeThis->apply();
                    }
                });
        }
    };
} // namespace

//==============================================================================
DARVCEditor::DARVCEditor(DARVCProcessor& p)
    : AudioProcessorEditor(&p), processor(p),
      thumbnail(512, formatManager, thumbnailCache)
{
    setLookAndFeel(&lookAndFeel);
    setSize(1180, 780);
    setResizable(true, true);
    setResizeLimits(900, 650, 1600, 1100);

    formatManager.registerBasicFormats();
    thumbnail.addChangeListener(this);

    for (auto* b : { &fromDawButton, &fromFileButton, &chooseFileButton,
                     &browseModelButton, &convertButton, &outputButton,
                     &modelsButton, &libraryButton, &settingsButton })
        addAndMakeVisible(*b);

    styleButton(convertButton, true);
    for (auto* b : { &fromDawButton, &fromFileButton, &chooseFileButton,
                     &browseModelButton, &outputButton, &modelsButton,
                     &libraryButton, &settingsButton })
        styleButton(*b);

    convertButton.setTooltip("Convert the selected input with the selected voice model");
    modelsButton.setTooltip("Browse and manage voice models (.pth)");
    libraryButton.setTooltip("Browse converted files");
    settingsButton.setTooltip("Engine, Python and folder settings");

    fromDawButton.setConnectedEdges(juce::Button::ConnectedOnRight);
    fromFileButton.setConnectedEdges(juce::Button::ConnectedOnLeft);
    fromDawButton.setClickingTogglesState(true);
    fromFileButton.setClickingTogglesState(true);
    fromDawButton.setToggleState(true, juce::dontSendNotification);

    fromDawButton.onClick = [this] {
        fromDawButton.setToggleState(true, juce::dontSendNotification);
        fromFileButton.setToggleState(false, juce::dontSendNotification);
        setStatus("DAW capture is not available: bounce/render your track to a WAV file "
                  "and use 'From File'.");
    };
    fromFileButton.onClick = [this] {
        fromFileButton.setToggleState(true, juce::dontSendNotification);
        fromDawButton.setToggleState(false, juce::dontSendNotification);
        chooseInputFile();
    };
    chooseFileButton.onClick = [this] {
        fromFileButton.setToggleState(true, juce::dontSendNotification);
        fromDawButton.setToggleState(false, juce::dontSendNotification);
        chooseInputFile();
    };
    browseModelButton.onClick = [this] { chooseModelFile(); };
    convertButton.onClick = [this] { requestConversion(); };
    outputButton.onClick = [this] {
        if (outputFile.existsAsFile())
            outputFile.revealToUser();
        else
            setStatus("No output has been created yet. Conversions are saved to the Library.");
    };
    modelsButton.onClick = [this] { showModelsPanel(); };
    libraryButton.onClick = [this] { showLibraryPanel(); };
    settingsButton.onClick = [this] { showSettingsPanel(); };

    f0Box.addItem("RMVPE", 1);
    f0Box.addItem("Harvest", 2);
    f0Box.addItem("PM", 3);
    f0Box.addItem("Crepe", 4);
    f0Box.setTooltip("Pitch (F0) extraction method");
    f0Box.onChange = [this] { processor.settings.f0Id = f0Box.getSelectedId(); };
    addAndMakeVisible(f0Box);

    versionBox.addItem("RVC v2", 2);
    versionBox.addItem("RVC v1", 1);
    versionBox.setTooltip("Model architecture the .pth was trained with");
    versionBox.onChange = [this] {
        processor.settings.modelVersion = versionBox.getSelectedId() == 1 ? "v1" : "v2";
        processor.saveSettings();
    };
    addAndMakeVisible(versionBox);

    setupSlider(pitchSlider, -24, 24, 0, " semitones");
    setupSlider(indexSlider, 0, 100, 75, "%");
    setupSlider(protectSlider, 0, 50, 33, "%");
    pitchSlider.onValueChange = [this] {
        processor.settings.pitch = static_cast<int>(pitchSlider.getValue());
    };
    indexSlider.onValueChange = [this] {
        processor.settings.indexPercent = static_cast<int>(indexSlider.getValue());
    };
    protectSlider.onValueChange = [this] {
        processor.settings.protectPercent = static_cast<int>(protectSlider.getValue());
    };
    for (auto* s : { &pitchSlider, &indexSlider, &protectSlider })
        addAndMakeVisible(*s);

    auto setupLabel = [this](juce::Label& l, const juce::String& t, float size, juce::Colour c = text) {
        l.setText(t, juce::dontSendNotification);
        l.setColour(juce::Label::textColourId, c);
        l.setFont(darvcFont(size));
        addAndMakeVisible(l);
    };
    setupLabel(inputLabel, "INPUT AUDIO", 11.0f, muted);
    setupLabel(modelLabel, "No model selected", 13.0f, text);
    setupLabel(indexLabel, "", 11.5f, muted);
    setupLabel(engineLabel, "RVC engine: checking...", 11.5f, muted);
    setupLabel(statusLabel, "", 12.0f, text);
    setupLabel(outputLabel, "Conversion Output  " + darvcBullet() + "  No output yet", 12.0f, muted);
    setupLabel(pitchLabel, "Pitch Shift", 12.0f, text);
    setupLabel(indexRateLabel, "Index Rate", 12.0f, text);
    setupLabel(protectLabel, "Protect", 12.0f, text);

    processor.engine.onChange = [safeThis = juce::Component::SafePointer<DARVCEditor>(this)] {
        if (auto* editor = safeThis.getComponent())
            editor->refreshFromEngine();
    };

    refreshFromSettings();
    wasEngineBusy = processor.engine.isBusy();
    if (!wasEngineBusy)
        processor.engine.startCheck();
    refreshFromEngine();
    startTimer(250);
}

DARVCEditor::~DARVCEditor()
{
    stopTimer();
    thumbnail.removeChangeListener(this);
    processor.engine.onChange = nullptr;
    setLookAndFeel(nullptr);
}

//==============================================================================
void DARVCEditor::setStatus(const juce::String& newStatus)
{
    statusText = newStatus;
    statusLabel.setText(statusText, juce::dontSendNotification);
    repaint();
}

void DARVCEditor::styleButton(juce::TextButton& b, bool primary)
{
    if (primary)
        b.setName("primary");
    b.setColour(juce::TextButton::buttonColourId, primary ? juce::Colour(0xff173d91) : panel2);
    b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff164c86));
    b.setColour(juce::TextButton::textColourOffId, text);
    b.setColour(juce::TextButton::textColourOnId, text);
}

void DARVCEditor::setupSlider(juce::Slider& s, double min, double max, double value,
                              const juce::String& suffix)
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

//==============================================================================
void DARVCEditor::refreshFromSettings()
{
    const auto& s = processor.settings;
    pitchSlider.setValue(s.pitch, juce::dontSendNotification);
    indexSlider.setValue(s.indexPercent, juce::dontSendNotification);
    protectSlider.setValue(s.protectPercent, juce::dontSendNotification);
    f0Box.setSelectedId(s.f0Id, juce::dontSendNotification);
    versionBox.setSelectedId(s.modelVersion == "v1" ? 1 : 2, juce::dontSendNotification);

    if (s.inputFile.existsAsFile())
    {
        chooseFileButton.setButtonText(s.inputFile.getFileName());
        chooseFileButton.setTooltip(s.inputFile.getFullPathName());
        fromFileButton.setToggleState(true, juce::dontSendNotification);
        fromDawButton.setToggleState(false, juce::dontSendNotification);
    }
    updateModelLabels();
    updatePreviewSource();
}

void DARVCEditor::updateModelLabels()
{
    const auto& model = processor.settings.modelFile;
    if (model.existsAsFile())
    {
        modelLabel.setText(model.getFileName(), juce::dontSendNotification);
        modelLabel.setTooltip(model.getFullPathName());
        const auto index = processor.settings.indexFile.existsAsFile()
                               ? processor.settings.indexFile
                               : DARVCProcessor::findSiblingIndex(model);
        indexLabel.setText(index.existsAsFile() ? "Index: " + index.getFileName()
                                                : "Index: none found (conversion still works)",
                           juce::dontSendNotification);
        indexLabel.setTooltip(index.getFullPathName());
    }
    else
    {
        modelLabel.setText("No model selected", juce::dontSendNotification);
        modelLabel.setTooltip({});
        indexLabel.setText("Index: -", juce::dontSendNotification);
    }
}

void DARVCEditor::updatePreviewSource()
{
    const auto& preview = outputFile.existsAsFile() ? outputFile : processor.settings.inputFile;
    if (preview.existsAsFile())
        thumbnail.setSource(new juce::FileInputSource(preview));
    else
        thumbnail.setSource(nullptr);
    repaint();
}

void DARVCEditor::refreshFromEngine()
{
    auto& engine = processor.engine;
    const bool busy = engine.isBusy();
    const bool stateChanged = (busy != wasEngineBusy);

    convertButton.setButtonText(busy ? "Cancel" : "Convert");

    if (busy)
    {
        const auto message = engine.getStatusMessage();
        if (message.isNotEmpty() && message != statusText)
            setStatus(message);
    }
    else if (wasEngineBusy) // a task just finished
    {
        const auto message = engine.getLastMessage();
        if (message.isNotEmpty())
            setStatus(message);
        const auto produced = engine.getLastOutput();
        if (produced.existsAsFile() && produced != outputFile)
        {
            outputFile = produced;
            outputLabel.setText("Conversion Output  " + darvcBullet() + "  " + produced.getFileName(),
                                juce::dontSendNotification);
            outputLabel.setTooltip(produced.getFullPathName());
            updatePreviewSource();
        }
        if (activePanelRefresh)
            activePanelRefresh();
    }

    const auto check = engine.getLastCheckInfo();
    if (check.isObject())
    {
        const bool ready = static_cast<bool>(check.getProperty("engine_ready", false));
        engineLabel.setText(ready ? "RVC engine: ready" : "RVC engine: not installed (see Settings)",
                            juce::dontSendNotification);
        engineLabel.setColour(juce::Label::textColourId, ready ? success : warning);
    }
    else if (busy)
    {
        engineLabel.setText("RVC engine: working...", juce::dontSendNotification);
        engineLabel.setColour(juce::Label::textColourId, muted);
    }

    wasEngineBusy = busy;
    // The 250 ms timer calls this constantly; only repaint while something
    // is actually animating/changing, to keep idle CPU use near zero.
    if (busy || stateChanged)
        repaint();
}

void DARVCEditor::timerCallback()
{
    refreshFromEngine();
    if (activePanelRefresh && processor.engine.isBusy())
        activePanelRefresh();
}

void DARVCEditor::changeListenerCallback(juce::ChangeBroadcaster*)
{
    repaint();
}

//==============================================================================
void DARVCEditor::setInputFile(juce::File file)
{
    processor.settings.inputFile = file;
    chooseFileButton.setButtonText(file.getFileName());
    chooseFileButton.setTooltip(file.getFullPathName());
    updatePreviewSource();
    setStatus("Input selected: " + file.getFileName());
}

void DARVCEditor::setModelFile(juce::File file)
{
    processor.settings.modelFile = file;
    processor.settings.indexFile = DARVCProcessor::findSiblingIndex(file);
    processor.saveSettings();
    updateModelLabels();
    setStatus("Model selected: " + file.getFileName());
}

void DARVCEditor::chooseInputFile()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select input audio", processor.settings.inputFile,
        "*.wav;*.flac;*.aif;*.aiff;*.mp3;*.ogg");
    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<DARVCEditor>(this)](
            const juce::FileChooser& chooser) {
            if (safeThis == nullptr)
                return;
            const auto file = chooser.getResult();
            if (file.existsAsFile())
                safeThis->setInputFile(file);
        });
}

void DARVCEditor::chooseModelFile()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select RVC .pth model", processor.settings.modelFile, "*.pth");
    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<DARVCEditor>(this)](
            const juce::FileChooser& chooser) {
            if (safeThis == nullptr)
                return;
            const auto file = chooser.getResult();
            if (file.existsAsFile())
                safeThis->setModelFile(file);
        });
}

void DARVCEditor::requestConversion()
{
    auto& engine = processor.engine;
    if (engine.isBusy())
    {
        engine.cancel();
        setStatus("Cancelling conversion...");
        return;
    }

    if (const auto problem = processor.validateForConversion(); problem.isNotEmpty())
    {
        setStatus(problem);
        return;
    }

    const auto job = processor.buildJob();
    if (engine.startConversion(job))
        setStatus("Converting " + job.inputFile.getFileName() + " with "
                  + job.modelFile.getFileName() + "...");
    refreshFromEngine();
}

//==============================================================================
void DARVCEditor::showPanel(std::unique_ptr<juce::Component> panel, std::function<void()> refresh)
{
    closePanel();
    activePanel = std::move(panel);
    activePanelRefresh = std::move(refresh);
    addAndMakeVisible(*activePanel);
    activePanel->setBounds(getLocalBounds());
    activePanel->toFront(true);
}

void DARVCEditor::closePanel()
{
    activePanelRefresh = nullptr;
    if (activePanel != nullptr)
    {
        // Closing is often triggered from inside the panel's own button or
        // file-chooser callbacks; destroying the component synchronously
        // from there would delete objects whose methods are still on the
        // stack. Detach now, delete on the next message-loop turn.
        auto* panel = activePanel.release();
        removeChildComponent(panel);
        panel->setVisible(false);
        juce::MessageManager::callAsync([panel] { delete panel; });
    }
}

void DARVCEditor::showModelsPanel()
{
    auto panel = std::make_unique<ModelsPanel>(
        processor,
        [safeThis = juce::Component::SafePointer<DARVCEditor>(this)] {
            if (safeThis != nullptr) safeThis->closePanel();
        },
        [safeThis = juce::Component::SafePointer<DARVCEditor>(this)](juce::File model) {
            if (safeThis == nullptr) return;
            safeThis->setModelFile(model);
            safeThis->closePanel();
        },
        [safeThis = juce::Component::SafePointer<DARVCEditor>(this)](juce::String message) {
            if (safeThis != nullptr) safeThis->setStatus(message);
        });
    auto* raw = panel.get();
    showPanel(std::move(panel), [raw] { raw->refresh(); });
}

void DARVCEditor::showLibraryPanel()
{
    auto panel = std::make_unique<LibraryPanel>(
        processor,
        [safeThis = juce::Component::SafePointer<DARVCEditor>(this)] {
            if (safeThis != nullptr) safeThis->closePanel();
        },
        [safeThis = juce::Component::SafePointer<DARVCEditor>(this)](juce::File file) {
            if (safeThis == nullptr) return;
            safeThis->setInputFile(file);
            safeThis->closePanel();
        },
        [safeThis = juce::Component::SafePointer<DARVCEditor>(this)](juce::String message) {
            if (safeThis != nullptr) safeThis->setStatus(message);
        });
    auto* raw = panel.get();
    showPanel(std::move(panel), [raw] { raw->refresh(); });
}

void DARVCEditor::showSettingsPanel()
{
    auto panel = std::make_unique<SettingsPanel>(
        processor,
        [safeThis = juce::Component::SafePointer<DARVCEditor>(this)] {
            if (safeThis != nullptr) safeThis->closePanel();
        },
        [safeThis = juce::Component::SafePointer<DARVCEditor>(this)] {
            if (safeThis == nullptr) return;
            safeThis->versionBox.setSelectedId(
                safeThis->processor.settings.modelVersion == "v1" ? 1 : 2,
                juce::dontSendNotification);
            safeThis->refreshFromEngine();
        });
    auto* raw = panel.get();
    showPanel(std::move(panel), [raw] { raw->refresh(); });
}

//==============================================================================
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
    g.setFont(darvcFont(30.0f));
    g.drawText("D A R V C", 38, 31, 220, 34, juce::Justification::centredLeft);
    g.setColour(muted);
    g.setFont(darvcFont(10.5f));
    g.drawText("RVC VOICE CONVERTER   " + darvcBullet() + "   AI POWERED",
               260, 36, 340, 24, juce::Justification::centredLeft);

    g.setColour(cyan.withAlpha(0.8f));
    g.fillRoundedRectangle(34.0f, 117.0f, 3.0f, 56.0f, 1.5f);
    g.setColour(cyan);
    g.setFont(darvcFont(12.0f, juce::Font::bold));
    g.drawText("01   INPUT & MODEL", 52, 118, 350, 20, juce::Justification::centredLeft);
    g.setColour(muted);
    g.setFont(darvcFont(11.0f));
    g.drawText("ADVANCED SETTINGS", 52, 356, 350, 20, juce::Justification::centredLeft);

    //--- right panel geometry ------------------------------------------
    const float previewLeft = 505.0f;
    const float previewRight = static_cast<float>(getWidth()) - 45.0f;
    const float waveTop = static_cast<float>(getHeight()) - 212.0f;
    const float waveHeight = 38.0f;

    // Neon circular audio visualization (decorative preview)
    const float cx = (465.0f + static_cast<float>(getWidth()) - 18.0f) * 0.5f;
    const float cy = (122.0f + waveTop) * 0.5f;
    const float radius = juce::jmin(190.0f, (waveTop - 122.0f) * 0.5f - 12.0f);
    for (int ring = 0; ring < 4; ++ring)
    {
        const float r = radius - static_cast<float>(ring) * 18.0f;
        if (r <= 10.0f)
            break;
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
    g.setFont(darvcFont(20.0f));
    g.drawText("DARVC", static_cast<int>(cx) - 58, static_cast<int>(cy) - 14, 116, 28, juce::Justification::centred);

    g.setColour(muted);
    g.setFont(darvcFont(11.0f));
    g.drawText("OUTPUT PREVIEW", 495, 122, 200, 20, juce::Justification::centredLeft);

    const bool busy = processor.engine.isBusy();
    g.setColour(busy ? cyan : success);
    g.fillEllipse(static_cast<float>(getWidth()) - 160.0f, 127.0f, 8.0f, 8.0f);
    g.setColour(text);
    g.drawText(busy ? "CONVERTING" : "READY", getWidth() - 146, 121, 110, 20,
               juce::Justification::centredLeft);

    // Waveform of the current output (or input) file.
    const auto waveBounds = juce::Rectangle<float>(previewLeft, waveTop,
                                                   previewRight - previewLeft, waveHeight);
    g.setColour(panel2.withAlpha(0.85f));
    g.fillRoundedRectangle(waveBounds, 8.0f);
    g.setColour(border.withAlpha(0.7f));
    g.drawRoundedRectangle(waveBounds, 8.0f, 1.0f);
    if (thumbnail.getNumChannels() > 0 && thumbnail.getTotalLength() > 0.0)
    {
        g.setColour(cyan.withAlpha(0.85f));
        thumbnail.drawChannels(g, waveBounds.reduced(6.0f, 5.0f).toNearestInt(),
                               0.0, thumbnail.getTotalLength(), 1.0f);
    }
    else
    {
        g.setColour(muted.withAlpha(0.6f));
        g.setFont(darvcFont(11.0f));
        g.drawText("Waveform preview appears here after choosing a file",
                   waveBounds.toNearestInt(), juce::Justification::centred);
    }

    // Duration + conversion progress.
    const auto total = formatDuration(thumbnail.getTotalLength());
    g.setColour(muted);
    g.setFont(darvcFont(11.0f));
    auto timeText = "00:00  /  " + total;
    if (busy)
    {
        const int percent = processor.engine.getProgressPercent();
        timeText += percent >= 0 ? "   " + darvcBullet() + "   Converting " + juce::String(percent) + "%"
                                 : "   " + darvcBullet() + "   Working...";
    }
    g.drawText(timeText, static_cast<int>(previewLeft), static_cast<int>(waveTop + waveHeight + 6.0f),
               320, 20, juce::Justification::centredLeft);

    const float progressY = waveTop + waveHeight + 30.0f;
    g.setColour(juce::Colour(0xff172744));
    g.fillRoundedRectangle(previewLeft, progressY, previewRight - previewLeft, 4.0f, 2.0f);
    float progress = 0.0f;
    if (busy)
        progress = juce::jlimit(0, 100, processor.engine.getProgressPercent()) / 100.0f;
    else if (outputFile.existsAsFile())
        progress = 1.0f;
    if (progress > 0.0f)
    {
        juce::ColourGradient fillGrad(blue, previewLeft, progressY, cyan, previewRight, progressY, false);
        g.setGradientFill(fillGrad);
        g.fillRoundedRectangle(previewLeft, progressY, (previewRight - previewLeft) * progress, 4.0f, 2.0f);
    }
}

void DARVCEditor::resized()
{
    const int w = getWidth();
    const int h = getHeight();
    // Section heading is painted at y=118 (height 20); the INPUT AUDIO label
    // used to sit at y=130 and overlap it - keep a clear gap now.
    inputLabel.setBounds(52, 140, 300, 16);
    fromDawButton.setBounds(48, 162, 175, 34);
    fromFileButton.setBounds(223, 162, 175, 34);
    chooseFileButton.setBounds(48, 208, 350, 38);
    modelLabel.setBounds(52, 260, 252, 26);
    browseModelButton.setBounds(316, 256, 82, 32);
    indexLabel.setBounds(52, 290, 346, 18);
    engineLabel.setBounds(52, 312, 346, 18);
    pitchLabel.setBounds(52, 386, 120, 20);
    pitchSlider.setBounds(52, 406, 350, 36);
    indexRateLabel.setBounds(52, 454, 120, 20);
    indexSlider.setBounds(52, 474, 350, 36);
    protectLabel.setBounds(52, 522, 120, 20);
    protectSlider.setBounds(52, 542, 350, 36);
    f0Box.setBounds(52, 596, 214, 34);
    versionBox.setBounds(274, 596, 128, 34);

    // Centre Convert within the right-hand panel (previously it was offset
    // towards the middle of the whole window and drifted when resizing).
    const int rightPanelCentre = 465 + (w - 483) / 2;
    convertButton.setBounds(rightPanelCentre - 140, h - 126, 280, 48);

    outputButton.setBounds(w - 182, h - 66, 130, 30);
    modelsButton.setBounds(w - 370, 38, 76, 30);
    libraryButton.setBounds(w - 284, 38, 76, 30);
    settingsButton.setBounds(w - 198, 38, 92, 30);
    statusLabel.setBounds(52, h - 65, w - 280, 26);
    outputLabel.setBounds(52, h - 42, w - 280, 20);

    if (activePanel != nullptr)
        activePanel->setBounds(getLocalBounds());
}
