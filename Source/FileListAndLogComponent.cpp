#include "JUCEIteratorFix.h"  // MUST be first - Fix for StrideIterator compatibility
#include "FileListAndLogComponent.h"

namespace
{
juce::Font makeFont(float height, bool bold = false)
{
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font makeMonospaceFont(float height)
{
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
}

juce::String makeUTF8(const char* text)
{
    return juce::String{ juce::CharPointer_UTF8(text) };
}
}

//==============================================================================
FileListAndLogComponent::FileListAndLogComponent(AppState& state)
    : appState(state)
{
    // File list box
    fileListBox.setModel(this);
    fileListBox.setRowHeight(32);
    fileListBox.setMultipleSelectionEnabled(true);
    fileListBox.setClickingTogglesRowSelection(true);
    fileListBox.setColour(juce::ListBox::backgroundColourId, juce::Colours::white);
    addAndMakeVisible(fileListBox);

    // Select All button
    selectAllButton.setButtonText("Select All");
    selectAllButton.addListener(this);
    addAndMakeVisible(selectAllButton);

    // Preview button
    previewButton.setButtonText(makeUTF8("\xE2\x96\xB6 Preview Selected"));
    previewButton.addListener(this);
    addAndMakeVisible(previewButton);

    // Process All button
    processAllButton.setButtonText(makeUTF8("\xE2\x9A\x99 Process All"));
    processAllButton.addListener(this);
    addAndMakeVisible(processAllButton);

    // Copy Log button
    copyLogButton.setButtonText("Copy Log");
    copyLogButton.addListener(this);
    addAndMakeVisible(copyLogButton);

    // Log display
    logDisplay.setMultiLine(true);
    logDisplay.setReadOnly(true);
    logDisplay.setScrollbarsShown(true);
    logDisplay.setCaretVisible(false);
    logDisplay.setFont(makeMonospaceFont(11.0f));
    addAndMakeVisible(logDisplay);

    // File count label
    fileCountLabel.setText("No files added", juce::dontSendNotification);
    fileCountLabel.setJustificationType(juce::Justification::centred);
    fileCountLabel.setColour(juce::Label::textColourId, juce::Colour(0xff86868b));
    addAndMakeVisible(fileCountLabel);
}

FileListAndLogComponent::~FileListAndLogComponent()
{
}

void FileListAndLogComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::white);

    // Calculate areas
    auto bounds = getLocalBounds();
    constexpr int buttonAreaHeight = 45;
    constexpr int baseLogHeight = 200;

    int availableHeight = bounds.getHeight() - buttonAreaHeight - baseLogHeight;
    int dropZoneHeight = juce::jmax(0, availableHeight / 2);
    int logAreaHeight = baseLogHeight + juce::jmax(0, availableHeight - dropZoneHeight);

    auto fileAreaBounds = bounds.removeFromTop(dropZoneHeight);
    bounds.removeFromBottom(buttonAreaHeight);  // Reserve space for buttons (layout handled in resized())
    auto logBounds = bounds.removeFromBottom(logAreaHeight);

    // Draw file area - only show drop zone when empty
    if (appState.files.isEmpty())
    {
        drawDropZone(g, fileAreaBounds.reduced(20));
    }

    // Draw log section header
    g.setColour(juce::Colour(0xfff5f5f7));
    g.fillRect(logBounds.removeFromTop(30));
    g.setColour(juce::Colour(0xff1d1d1f));
    g.setFont(makeFont(12.0f, true));
    g.drawText("Log", logBounds.getX() + 10, logBounds.getY() - 25, 100, 20, juce::Justification::centredLeft);
}

void FileListAndLogComponent::resized()
{
    auto bounds = getLocalBounds();

    constexpr int buttonAreaHeight = 45;
    constexpr int baseLogHeight = 200;

    int availableHeight = bounds.getHeight() - buttonAreaHeight - baseLogHeight;
    int dropZoneHeight = juce::jmax(0, availableHeight / 2);
    int logAreaHeight = baseLogHeight + juce::jmax(0, availableHeight - dropZoneHeight);

    // File/drop area - store for drag/drop and mouse handling
    fileAreaBounds = bounds.removeFromTop(dropZoneHeight);
    dropZoneBounds = fileAreaBounds.reduced(20);

    // Show/hide components based on whether files exist
    if (appState.files.isEmpty())
    {
        fileListBox.setVisible(false);
        selectAllButton.setVisible(false);
        fileCountLabel.setVisible(true);
        fileCountLabel.setBounds(dropZoneBounds.withSizeKeepingCentre(300, 40));
    }
    else
    {
        fileListBox.setVisible(true);
        selectAllButton.setVisible(true);
        fileCountLabel.setVisible(false);

        // File list header with "Select All" button
        auto fileListArea = fileAreaBounds.reduced(10);
        auto headerArea = fileListArea.removeFromTop(30);
        selectAllButton.setBounds(headerArea.removeFromRight(100).reduced(2));

        // File list box - IMPORTANT: Set intercepts mouse clicks to false so parent can handle double-clicks
        fileListBox.setBounds(fileListArea);
    }

    // Buttons area
    auto buttonsBounds = bounds.removeFromBottom(buttonAreaHeight).reduced(20, 8);
    int buttonWidth = (buttonsBounds.getWidth() - 16) / 2;
    previewButton.setBounds(buttonsBounds.removeFromLeft(buttonWidth));
    buttonsBounds.removeFromLeft(8);
    processAllButton.setBounds(buttonsBounds.removeFromLeft(buttonWidth));
    buttonsBounds.removeFromLeft(8);

    // Log area
    auto logBounds = bounds.removeFromBottom(logAreaHeight).reduced(10);
    auto logHeaderBounds = logBounds.removeFromTop(30);
    copyLogButton.setBounds(logHeaderBounds.removeFromRight(80).reduced(0, 5));
    logDisplay.setBounds(logBounds);
}

void FileListAndLogComponent::mouseDown(const juce::MouseEvent& event)
{
    // Check if double-click in the drop zone area
    if (event.getNumberOfClicks() == 2 && dropZoneBounds.contains(event.getPosition()))
    {
        showFileChooser();
    }
}

void FileListAndLogComponent::showFileChooser()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Select Audio Files",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.wav;*.aif;*.aiff"
    );

    auto flags = juce::FileBrowserComponent::openMode |
                 juce::FileBrowserComponent::canSelectFiles |
                 juce::FileBrowserComponent::canSelectMultipleItems;

    chooser->launchAsync(flags, [this, chooser](const juce::FileChooser& fc)
    {
        auto results = fc.getResults();
        if (results.isEmpty())
            return;

        juce::Array<juce::File> audioFiles;
        for (const auto& file : results)
        {
            if (file.hasFileExtension(".wav;.aif;.aiff"))
            {
                audioFiles.add(file);
            }
        }

        if (!audioFiles.isEmpty() && onFilesAdded)
        {
            onFilesAdded(audioFiles);
        }
    });
}

void FileListAndLogComponent::buttonClicked(juce::Button* button)
{
    if (button == &previewButton)
    {
        if (onPreviewClicked)
            onPreviewClicked();
    }
    else if (button == &processAllButton)
    {
        if (onProcessAllClicked)
            onProcessAllClicked();
    }
    else if (button == &copyLogButton)
    {
        if (onCopyLog)
            onCopyLog();
    }
    else if (button == &selectAllButton)
    {
        // Toggle between select all and deselect all
        bool allSelected = true;
        for (const auto& file : appState.files)
        {
            if (!file.isSelected)
            {
                allSelected = false;
                break;
            }
        }

        // Toggle selection for all files
        for (auto& file : appState.files)
        {
            file.isSelected = !allSelected;
        }

        // Update button text
        selectAllButton.setButtonText(allSelected ? "Select All" : "Deselect All");

        // Update list box display
        fileListBox.updateContent();
        fileListBox.repaint();
    }
}

bool FileListAndLogComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    // Accept audio files
    for (const auto& file : files)
    {
        if (file.endsWith(".wav") || file.endsWith(".aif") || file.endsWith(".aiff"))
            return true;
    }
    return false;
}

void FileListAndLogComponent::filesDropped(const juce::StringArray& files, int x, int y)
{
    isDraggingOver = false;

    juce::Array<juce::File> audioFiles;
    for (const auto& file : files)
    {
        juce::File f(file);
        if (f.hasFileExtension(".wav;.aif;.aiff"))
        {
            audioFiles.add(f);
        }
    }

    if (!audioFiles.isEmpty() && onFilesAdded)
    {
        onFilesAdded(audioFiles);
    }

    repaint();
}

void FileListAndLogComponent::fileDragEnter(const juce::StringArray&, int, int)
{
    isDraggingOver = true;
    repaint();
}

void FileListAndLogComponent::fileDragExit(const juce::StringArray&)
{
    isDraggingOver = false;
    repaint();
}

int FileListAndLogComponent::getNumRows()
{
    return appState.files.size();
}

void FileListAndLogComponent::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowNumber >= appState.files.size())
        return;

    const auto& file = appState.files.getReference(rowNumber);

    // Background - use OS-style selection color when selected
    if (rowIsSelected || file.isSelected)
    {
        g.setColour(juce::Colour(0xff007aff).withAlpha(0.2f));
        g.fillRect(0, 0, width, height);
    }

    // Checkbox area (left side)
    auto checkboxBounds = juce::Rectangle<int>(8, height / 2 - 8, 16, 16);
    g.setColour(juce::Colour(0xffc7c7cc));
    g.drawRoundedRectangle(checkboxBounds.toFloat(), 3.0f, 1.5f);

    if (file.isSelected)
    {
        g.setColour(juce::Colour(0xff007aff));
        g.fillRoundedRectangle(checkboxBounds.reduced(2).toFloat(), 2.0f);
    }

    // Status indicator
    juce::Colour statusColour;
    juce::String statusText;
    switch (file.status)
    {
        case ProcessingStatus::pending:
            statusColour = juce::Colour(0xff86868b);
            statusText = makeUTF8("\xE2\x8F\xB8"); // ⏸
            break;
        case ProcessingStatus::processing:
            statusColour = juce::Colour(0xff007aff);
            statusText = makeUTF8("\xE2\x9A\x99"); // ⚙
            break;
        case ProcessingStatus::completed:
            statusColour = juce::Colour(0xff34c759);
            statusText = makeUTF8("\xE2\x9C\x93"); // ✓
            break;
        case ProcessingStatus::failed:
            statusColour = juce::Colour(0xffff3b30);
            statusText = makeUTF8("\xE2\x9C\x97"); // ✗
            break;
        case ProcessingStatus::invalidSampleRate:
            statusColour = juce::Colour(0xffff9500);
            statusText = makeUTF8("\xE2\x9A\xA0"); // ⚠
            break;
    }

    g.setColour(statusColour);
    g.setFont(makeFont(16.0f));
    g.drawText(statusText, 32, 0, 24, height, juce::Justification::centred);

    // Filename
    g.setColour(juce::Colour(0xff1d1d1f));
    g.setFont(makeFont(13.0f));
    g.drawText(file.getFileName(), 64, 0, width - 184, height, juce::Justification::centredLeft, true);

    // Sample rate
    if (file.sampleRate > 0)
    {
        g.setColour(file.isValid() ? juce::Colour(0xff34c759) : juce::Colour(0xffff3b30));
        g.setFont(makeFont(11.0f));
        g.drawText(juce::String(file.sampleRate / 1000.0, 1) + " kHz",
                  width - 120, 0, 100, height, juce::Justification::centredRight);
    }
}

void FileListAndLogComponent::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= appState.files.size())
        return;

    // Toggle selection
    appState.files.getReference(row).isSelected = !appState.files.getReference(row).isSelected;

    // Update "Select All" button text
    bool allSelected = true;
    for (const auto& file : appState.files)
    {
        if (!file.isSelected)
        {
            allSelected = false;
            break;
        }
    }
    selectAllButton.setButtonText(allSelected ? "Deselect All" : "Select All");

    // Repaint to show checkbox change
    fileListBox.repaintRow(row);
}

juce::Component* FileListAndLogComponent::refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component* existingComponentToUpdate)
{
    // We're doing custom painting, so no component needed
    return nullptr;
}

void FileListAndLogComponent::updateFromState()
{
    // Update file count
    if (appState.files.isEmpty())
    {
        fileCountLabel.setText("No files added", juce::dontSendNotification);
    }
    else
    {
        fileCountLabel.setText(juce::String(appState.files.size()) + " file(s) added",
                              juce::dontSendNotification);
    }

    // Update file list
    fileListBox.updateContent();

    // Update "Select All" button text
    bool allSelected = !appState.files.isEmpty();
    for (const auto& file : appState.files)
    {
        if (!file.isSelected)
        {
            allSelected = false;
            break;
        }
    }
    selectAllButton.setButtonText(allSelected ? "Deselect All" : "Select All");

    // Update log
    juce::String logText;
    for (const auto& line : appState.logLines)
    {
        logText += line + "\n";
    }
    logDisplay.setText(logText, false);
    logDisplay.moveCaretToEnd();

    // Update button states
    previewButton.setEnabled(!appState.files.isEmpty() && !appState.isProcessing);
    processAllButton.setEnabled(!appState.files.isEmpty() && !appState.isProcessing);

    resized();  // Trigger layout update
    repaint();
}

void FileListAndLogComponent::drawDropZone(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    // Dashed border like original
    g.setColour(isDraggingOver ? juce::Colour(0xff007aff) : juce::Colour(0xffc7c7cc));

    // Draw dashed rectangle
    juce::Path dashedPath;
    float dashLength = 8.0f;
    float gapLength = 8.0f;

    // Top
    for (float x = bounds.getX(); x < bounds.getRight(); x += dashLength + gapLength)
    {
        dashedPath.addRectangle(x, bounds.getY(), juce::jmin(dashLength, bounds.getRight() - x), 2.0f);
    }
    // Right
    for (float y = bounds.getY(); y < bounds.getBottom(); y += dashLength + gapLength)
    {
        dashedPath.addRectangle(bounds.getRight() - 2, y, 2.0f, juce::jmin(dashLength, bounds.getBottom() - y));
    }
    // Bottom
    for (float x = bounds.getX(); x < bounds.getRight(); x += dashLength + gapLength)
    {
        dashedPath.addRectangle(x, bounds.getBottom() - 2, juce::jmin(dashLength, bounds.getRight() - x), 2.0f);
    }
    // Left
    for (float y = bounds.getY(); y < bounds.getBottom(); y += dashLength + gapLength)
    {
        dashedPath.addRectangle(bounds.getX(), y, 2.0f, juce::jmin(dashLength, bounds.getBottom() - y));
    }

    g.fillPath(dashedPath);

    // Draw icon and text
    auto centerBounds = bounds.withSizeKeepingCentre(240, 120);

    // Document icon
    juce::Path docIcon;
    auto iconBounds = centerBounds.removeFromTop(50).withSizeKeepingCentre(40, 50);
    docIcon.addRoundedRectangle(iconBounds.toFloat(), 4.0f);
    g.setColour(juce::Colour(0xffc7c7cc));
    g.fillPath(docIcon);

    // Text - primary instruction
    g.setColour(juce::Colour(0xff86868b));
    g.setFont(makeFont(14.0f));
    g.drawText("Drag audio files here", centerBounds.removeFromTop(25), juce::Justification::centred);

    // Text - secondary instruction
    centerBounds.removeFromTop(5);  // Small gap
    g.setFont(makeFont(12.0f));
    g.setColour(juce::Colour(0xffb0b0b5));
    g.drawText("or double-click to browse", centerBounds.removeFromTop(20), juce::Justification::centred);
}

