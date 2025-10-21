#pragma once

#include <JuceHeader.h>
#include "AppState.h"

//==============================================================================
/**
 * File List and Log Component - Main content area
 * Combines file drop zone, file list, and log display
 * Port of Swift's FileListView and log display
 */
class FileListAndLogComponent : public juce::Component,
                                 public juce::FileDragAndDropTarget,
                                 public juce::Button::Listener,
                                 public juce::ListBoxModel
{
public:
    FileListAndLogComponent(AppState& state);
    ~FileListAndLogComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

    // File drag and drop
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;

    // Button listener
    void buttonClicked(juce::Button* button) override;

    // ListBoxModel overrides
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& e) override;
    juce::Component* refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component* existingComponentToUpdate) override;

    // Update from state
    void updateFromState();

    // Callbacks
    std::function<void(const juce::Array<juce::File>&)> onFilesAdded;
    std::function<void()> onPreviewClicked;
    std::function<void()> onProcessAllClicked;
    std::function<void()> onCopyLog;
    std::function<void()> onClearAll;

private:
    AppState& appState;

    // File drop zone
    bool isDraggingOver = false;
    juce::Rectangle<int> dropZoneBounds;
    juce::Rectangle<int> fileAreaBounds;

    // File list display
    juce::ListBox fileListBox;
    juce::TextButton selectAllButton;
    juce::TextButton clearAllButton;

    // Buttons
    juce::TextButton previewButton;
    juce::TextButton processAllButton;
    juce::TextButton copyLogButton;

    // Log display
    juce::TextEditor logDisplay;

    // File count label
    juce::Label fileCountLabel;

    void drawDropZone(juce::Graphics& g, juce::Rectangle<int> bounds);
    void showFileChooser();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FileListAndLogComponent)
};
