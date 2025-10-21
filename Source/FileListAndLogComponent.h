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
                                 public juce::Button::Listener
{
public:
    FileListAndLogComponent(AppState& state);
    ~FileListAndLogComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // File drag and drop
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;

    // Button listener
    void buttonClicked(juce::Button* button) override;

    // Update from state
    void updateFromState();

    // Callbacks
    std::function<void(const juce::Array<juce::File>&)> onFilesAdded;
    std::function<void()> onPreviewClicked;
    std::function<void()> onProcessAllClicked;
    std::function<void()> onCopyLog;

private:
    AppState& appState;

    // File drop zone
    bool isDraggingOver = false;
    juce::Rectangle<int> dropZoneBounds;

    // Buttons
    juce::TextButton previewButton;
    juce::TextButton processAllButton;
    juce::TextButton copyLogButton;

    // Log display
    juce::TextEditor logDisplay;

    // File count label
    juce::Label fileCountLabel;

    void drawDropZone(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawFileList(juce::Graphics& g, juce::Rectangle<int> bounds);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FileListAndLogComponent)
};
