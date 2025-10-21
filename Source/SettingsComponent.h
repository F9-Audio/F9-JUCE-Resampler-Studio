#pragma once

#include <JuceHeader.h>
#include "AppState.h"

//==============================================================================
/**
 * Settings Component - Left sidebar
 * Port of Swift's SettingsView
 */
class SettingsComponent : public juce::Component,
                          public juce::ComboBox::Listener,
                          public juce::Button::Listener,
                          public juce::Slider::Listener
{
public:
    SettingsComponent(AppState& state);
    ~SettingsComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Callbacks
    void comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged) override;
    void buttonClicked(juce::Button* button) override;
    void sliderValueChanged(juce::Slider* slider) override;

    // Update UI from state
    void updateFromState();

    // Callbacks for actions
    std::function<void()> onRefreshDevices;
    std::function<void()> onMeasureLatency;
    std::function<void()> onStartLoopTest;
    std::function<void()> onStopLoopTest;
    std::function<void(const juce::String&)> onDeviceSelected;
    std::function<void(int)> onInputPairSelected;
    std::function<void(int)> onOutputPairSelected;
    std::function<void()> onOutputFolderSelected;
    std::function<void()> onDeviceNeedsReconfiguration;

private:
    AppState& appState;

    // Device Selection Section
    juce::Label deviceLabel;
    juce::ComboBox deviceCombo;
    juce::Label deviceInfoLabel;

    juce::Label inputPairLabel;
    juce::ComboBox inputPairCombo;
    juce::Label inputInfoLabel;

    juce::Label outputPairLabel;
    juce::ComboBox outputPairCombo;
    juce::Label outputInfoLabel;

    // Hardware Test Section
    juce::Label loopTestLabel;
    juce::TextButton startLoopTestButton;
    juce::TextButton stopLoopTestButton;
    juce::TextButton refreshDevicesButton;
    juce::Label builtInWarningLabel;

    // Audio Settings Section
    juce::Label sampleRateLabel;
    juce::ComboBox sampleRateCombo;

    juce::Label bufferSizeLabel;
    juce::ComboBox bufferSizeCombo;

    juce::Label latencyLabel;
    juce::Label latencyValueLabel;
    juce::TextButton measureLatencyButton;

    // Output Settings Section
    juce::Label outputFolderLabel;
    juce::Label outputFolderPathLabel;
    juce::TextButton chooseOutputFolderButton;

    juce::Label filenamePostfixLabel;
    juce::TextEditor filenamePostfixEditor;
    juce::Label postfixHintLabel;

    // Processing Settings Section
    juce::ToggleButton reverbModeToggle;
    juce::Label noiseFloorMarginLabel;
    juce::Slider noiseFloorMarginSlider;
    juce::Label noiseFloorValueLabel;

    juce::Label silenceDelayLabel;
    juce::Slider silenceDelaySlider;
    juce::Label silenceDelayValueLabel;

    juce::ToggleButton trimSilenceToggle;

    // Section separators
    void drawSectionHeader(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsComponent)
};
