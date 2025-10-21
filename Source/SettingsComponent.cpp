#include "JUCEIteratorFix.h"  // MUST be first - Fix for StrideIterator compatibility
#include "SettingsComponent.h"

namespace
{
juce::Font makeFont(float height, bool bold = false)
{
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}
}

//==============================================================================
SettingsComponent::SettingsComponent(AppState& state)
    : appState(state)
{
    // Device Selection
    deviceLabel.setText("Audio Device:", juce::dontSendNotification);
    deviceLabel.setFont(makeFont(13.0f, true));
    addAndMakeVisible(deviceLabel);

    deviceCombo.setTextWhenNothingSelected("Select audio interface...");
    deviceCombo.addListener(this);
    addAndMakeVisible(deviceCombo);

    deviceInfoLabel.setFont(makeFont(11.0f));
    deviceInfoLabel.setColour(juce::Label::textColourId, juce::Colour(0xff34c759)); // Green
    addAndMakeVisible(deviceInfoLabel);

    inputPairLabel.setText("Input Stereo Pair:", juce::dontSendNotification);
    addAndMakeVisible(inputPairLabel);

    inputPairCombo.addListener(this);
    addAndMakeVisible(inputPairCombo);

    inputInfoLabel.setFont(makeFont(11.0f));
    inputInfoLabel.setColour(juce::Label::textColourId, juce::Colour(0xff34c759));
    addAndMakeVisible(inputInfoLabel);

    outputPairLabel.setText("Output Stereo Pair:", juce::dontSendNotification);
    addAndMakeVisible(outputPairLabel);

    outputPairCombo.addListener(this);
    addAndMakeVisible(outputPairCombo);

    outputInfoLabel.setFont(makeFont(11.0f));
    outputInfoLabel.setColour(juce::Label::textColourId, juce::Colour(0xff34c759));
    addAndMakeVisible(outputInfoLabel);

    // Hardware Test
    loopTestLabel.setText("Hardware Loop Test:", juce::dontSendNotification);
    loopTestLabel.setFont(makeFont(11.0f));
    loopTestLabel.setColour(juce::Label::textColourId, juce::Colour(0xff86868b));
    addAndMakeVisible(loopTestLabel);

    startLoopTestButton.setButtonText("Start Loop Test");
    startLoopTestButton.addListener(this);
    addAndMakeVisible(startLoopTestButton);

    stopLoopTestButton.setButtonText("Stop Test");
    stopLoopTestButton.addListener(this);
    stopLoopTestButton.setEnabled(false);
    addAndMakeVisible(stopLoopTestButton);

    refreshDevicesButton.setButtonText("Refresh Devices");
    refreshDevicesButton.addListener(this);
    addAndMakeVisible(refreshDevicesButton);

    builtInWarningLabel.setText("Built-in Apple audio devices are hidden", juce::dontSendNotification);
    builtInWarningLabel.setFont(makeFont(10.0f));
    builtInWarningLabel.setColour(juce::Label::textColourId, juce::Colour(0xff86868b));
    addAndMakeVisible(builtInWarningLabel);

    // Sample Rate
    sampleRateLabel.setText("Sample Rate:", juce::dontSendNotification);
    addAndMakeVisible(sampleRateLabel);

    sampleRateCombo.addItem("44.1 kHz", 1);
    sampleRateCombo.addItem("48 kHz", 2);
    sampleRateCombo.addItem("88.2 kHz", 3);
    sampleRateCombo.addItem("96 kHz", 4);
    sampleRateCombo.addItem("176.4 kHz", 5);
    sampleRateCombo.addItem("192 kHz", 6);
    sampleRateCombo.setSelectedId(1); // Default 44.1 kHz
    sampleRateCombo.addListener(this);
    addAndMakeVisible(sampleRateCombo);

    // Buffer Size
    bufferSizeLabel.setText("Buffer Size:", juce::dontSendNotification);
    addAndMakeVisible(bufferSizeLabel);

    bufferSizeCombo.addItem("128 samples", 1);
    bufferSizeCombo.addItem("256 samples", 2);
    bufferSizeCombo.addItem("512 samples", 3);
    bufferSizeCombo.addItem("1024 samples", 4);
    bufferSizeCombo.setSelectedId(2); // Default 256
    bufferSizeCombo.addListener(this);
    addAndMakeVisible(bufferSizeCombo);

    // Latency
    latencyLabel.setText("Round-Trip Latency:", juce::dontSendNotification);
    addAndMakeVisible(latencyLabel);

    latencyValueLabel.setText("Not measured", juce::dontSendNotification);
    latencyValueLabel.setColour(juce::Label::textColourId, juce::Colour(0xffff3b30)); // Red
    addAndMakeVisible(latencyValueLabel);

    measureLatencyButton.setButtonText("Measure Latency");
    measureLatencyButton.addListener(this);
    addAndMakeVisible(measureLatencyButton);

    // Output Folder
    outputFolderLabel.setText("Output Folder:", juce::dontSendNotification);
    addAndMakeVisible(outputFolderLabel);

    outputFolderPathLabel.setText("Not set", juce::dontSendNotification);
    outputFolderPathLabel.setFont(makeFont(11.0f));
    outputFolderPathLabel.setColour(juce::Label::textColourId, juce::Colour(0xff86868b));
    addAndMakeVisible(outputFolderPathLabel);

    chooseOutputFolderButton.setButtonText("Change...");
    chooseOutputFolderButton.addListener(this);
    addAndMakeVisible(chooseOutputFolderButton);

    // Filename Postfix
    filenamePostfixLabel.setText("Filename Postfix:", juce::dontSendNotification);
    addAndMakeVisible(filenamePostfixLabel);

    filenamePostfixEditor.setText("_processed");
    filenamePostfixEditor.setFont(makeFont(13.0f));
    addAndMakeVisible(filenamePostfixEditor);

    postfixHintLabel.setText("Leave empty to keep original filename", juce::dontSendNotification);
    postfixHintLabel.setFont(makeFont(10.0f));
    postfixHintLabel.setColour(juce::Label::textColourId, juce::Colour(0xff86868b));
    addAndMakeVisible(postfixHintLabel);

    // Reverb Mode
    reverbModeToggle.setButtonText("Reverb Mode (stop on noise floor)");
    reverbModeToggle.addListener(this);
    addAndMakeVisible(reverbModeToggle);

    noiseFloorMarginLabel.setText("Noise floor margin:", juce::dontSendNotification);
    addAndMakeVisible(noiseFloorMarginLabel);

    noiseFloorMarginSlider.setRange(0, 50, 5);
    noiseFloorMarginSlider.setValue(10);
    noiseFloorMarginSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    noiseFloorMarginSlider.addListener(this);
    addAndMakeVisible(noiseFloorMarginSlider);

    noiseFloorValueLabel.setText("10%", juce::dontSendNotification);
    addAndMakeVisible(noiseFloorValueLabel);

    // Silence Delay
    silenceDelayLabel.setText("Silence between files:", juce::dontSendNotification);
    addAndMakeVisible(silenceDelayLabel);

    silenceDelaySlider.setRange(0, 1000, 50);
    silenceDelaySlider.setValue(150);
    silenceDelaySlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    silenceDelaySlider.addListener(this);
    addAndMakeVisible(silenceDelaySlider);

    silenceDelayValueLabel.setText("150 ms", juce::dontSendNotification);
    addAndMakeVisible(silenceDelayValueLabel);

    // Trim Silence
    trimSilenceToggle.setButtonText("Trim silence");
    trimSilenceToggle.setToggleState(true, juce::dontSendNotification);
    trimSilenceToggle.addListener(this);
    addAndMakeVisible(trimSilenceToggle);
}

SettingsComponent::~SettingsComponent()
{
}

void SettingsComponent::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(juce::Colour(0xfff5f5f7));

    // Section headers
    int yPos = 10;
    drawSectionHeader(g, juce::Rectangle<int>(10, yPos, getWidth() - 20, 20), "Audio Interface Selection");
    yPos += 280;
    drawSectionHeader(g, juce::Rectangle<int>(10, yPos, getWidth() - 20, 20), "Audio Interface Settings");
    yPos += 120;
    drawSectionHeader(g, juce::Rectangle<int>(10, yPos, getWidth() - 20, 20), "Output Settings");
    yPos += 180;
    drawSectionHeader(g, juce::Rectangle<int>(10, yPos, getWidth() - 20, 20), "Processing Settings");
}

void SettingsComponent::resized()
{
    auto bounds = getLocalBounds().reduced(10);
    int yPos = 30;
    int itemHeight = 24;
    int spacing = 8;
    int sectionSpacing = 30;

    // Audio Interface Selection
    deviceLabel.setBounds(bounds.getX(), yPos, bounds.getWidth(), itemHeight);
    yPos += itemHeight + 4;
    deviceCombo.setBounds(bounds.getX(), yPos, bounds.getWidth(), itemHeight);
    yPos += itemHeight + 2;
    deviceInfoLabel.setBounds(bounds.getX(), yPos, bounds.getWidth(), 16);
    yPos += 16 + spacing;

    inputPairLabel.setBounds(bounds.getX(), yPos, bounds.getWidth(), itemHeight);
    yPos += itemHeight + 4;
    inputPairCombo.setBounds(bounds.getX(), yPos, bounds.getWidth(), itemHeight);
    yPos += itemHeight + 2;
    inputInfoLabel.setBounds(bounds.getX(), yPos, bounds.getWidth(), 16);
    yPos += 16 + spacing;

    outputPairLabel.setBounds(bounds.getX(), yPos, bounds.getWidth(), itemHeight);
    yPos += itemHeight + 4;
    outputPairCombo.setBounds(bounds.getX(), yPos, bounds.getWidth(), itemHeight);
    yPos += itemHeight + 2;
    outputInfoLabel.setBounds(bounds.getX(), yPos, bounds.getWidth(), 16);
    yPos += 16 + spacing;

    // Hardware test
    loopTestLabel.setBounds(bounds.getX(), yPos, bounds.getWidth(), 16);
    yPos += 16 + 4;
    int buttonWidth = (bounds.getWidth() - 8) / 2;
    startLoopTestButton.setBounds(bounds.getX(), yPos, buttonWidth, itemHeight);
    stopLoopTestButton.setBounds(bounds.getX() + buttonWidth + 8, yPos, buttonWidth, itemHeight);
    yPos += itemHeight + 6;
    refreshDevicesButton.setBounds(bounds.getX(), yPos, bounds.getWidth(), itemHeight);
    yPos += itemHeight + 4;
    builtInWarningLabel.setBounds(bounds.getX(), yPos, bounds.getWidth(), 14);
    yPos += 14 + sectionSpacing;

    // Audio Interface Settings
    sampleRateLabel.setBounds(bounds.getX(), yPos, bounds.getWidth(), itemHeight);
    yPos += itemHeight + 4;
    sampleRateCombo.setBounds(bounds.getX(), yPos, bounds.getWidth(), itemHeight);
    yPos += itemHeight + spacing;

    bufferSizeLabel.setBounds(bounds.getX(), yPos, bounds.getWidth(), itemHeight);
    yPos += itemHeight + 4;
    bufferSizeCombo.setBounds(bounds.getX(), yPos, bounds.getWidth(), itemHeight);
    yPos += itemHeight + spacing;

    latencyLabel.setBounds(bounds.getX(), yPos, bounds.getWidth(), itemHeight);
    yPos += itemHeight + 4;
    latencyValueLabel.setBounds(bounds.getX(), yPos, bounds.getWidth(), 16);
    yPos += 16 + 4;
    measureLatencyButton.setBounds(bounds.getX(), yPos, bounds.getWidth(), itemHeight);
    yPos += itemHeight + sectionSpacing;

    // Output Settings
    outputFolderLabel.setBounds(bounds.getX(), yPos, bounds.getWidth(), itemHeight);
    yPos += itemHeight + 4;
    int pathWidth = bounds.getWidth() - 80;
    outputFolderPathLabel.setBounds(bounds.getX(), yPos, pathWidth, itemHeight);
    chooseOutputFolderButton.setBounds(bounds.getX() + pathWidth + 8, yPos, 72, itemHeight);
    yPos += itemHeight + spacing;

    filenamePostfixLabel.setBounds(bounds.getX(), yPos, bounds.getWidth(), itemHeight);
    yPos += itemHeight + 4;
    filenamePostfixEditor.setBounds(bounds.getX(), yPos, bounds.getWidth(), itemHeight);
    yPos += itemHeight + 2;
    postfixHintLabel.setBounds(bounds.getX(), yPos, bounds.getWidth(), 14);
    yPos += 14 + sectionSpacing;

    // Processing Settings
    reverbModeToggle.setBounds(bounds.getX(), yPos, bounds.getWidth(), itemHeight);
    yPos += itemHeight + spacing;

    noiseFloorMarginLabel.setBounds(bounds.getX(), yPos, bounds.getWidth() - 50, itemHeight);
    noiseFloorValueLabel.setBounds(bounds.getRight() - 40, yPos, 40, itemHeight);
    yPos += itemHeight + 4;
    noiseFloorMarginSlider.setBounds(bounds.getX(), yPos, bounds.getWidth(), itemHeight);
    yPos += itemHeight + spacing;

    silenceDelayLabel.setBounds(bounds.getX(), yPos, bounds.getWidth() - 60, itemHeight);
    silenceDelayValueLabel.setBounds(bounds.getRight() - 50, yPos, 50, itemHeight);
    yPos += itemHeight + 4;
    silenceDelaySlider.setBounds(bounds.getX(), yPos, bounds.getWidth(), itemHeight);
    yPos += itemHeight + spacing;

    trimSilenceToggle.setBounds(bounds.getX(), yPos, bounds.getWidth(), itemHeight);
}

void SettingsComponent::comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged)
{
    if (comboBoxThatHasChanged == &deviceCombo)
    {
        int selectedIndex = deviceCombo.getSelectedItemIndex();
        if (selectedIndex >= 0 && selectedIndex < appState.devices.size())
        {
            if (onDeviceSelected)
                onDeviceSelected(appState.devices[selectedIndex].uniqueID);
        }
    }
    else if (comboBoxThatHasChanged == &inputPairCombo)
    {
        if (onInputPairSelected)
            onInputPairSelected(inputPairCombo.getSelectedItemIndex());
    }
    else if (comboBoxThatHasChanged == &outputPairCombo)
    {
        if (onOutputPairSelected)
            onOutputPairSelected(outputPairCombo.getSelectedItemIndex());
    }
    else if (comboBoxThatHasChanged == &sampleRateCombo)
    {
        int selectedId = sampleRateCombo.getSelectedId();
        switch (selectedId)
        {
            case 1: appState.settings.sampleRate = 44100.0; break;
            case 2: appState.settings.sampleRate = 48000.0; break;
            case 3: appState.settings.sampleRate = 88200.0; break;
            case 4: appState.settings.sampleRate = 96000.0; break;
            case 5: appState.settings.sampleRate = 176400.0; break;
            case 6: appState.settings.sampleRate = 192000.0; break;
        }
        // Warn user that latency needs re-measurement
        appState.appendLog("Sample rate changed to " + juce::String(appState.settings.sampleRate) + " Hz - reconfiguring device...");
        appState.settings.measuredLatencySamples = -1; // Invalidate latency

        // CRITICAL: Reconfigure audio device with new sample rate
        if (onDeviceNeedsReconfiguration)
            onDeviceNeedsReconfiguration();
    }
    else if (comboBoxThatHasChanged == &bufferSizeCombo)
    {
        int selectedId = bufferSizeCombo.getSelectedId();
        switch (selectedId)
        {
            case 1: appState.settings.bufferSize = BufferSize::samples128; break;
            case 2: appState.settings.bufferSize = BufferSize::samples256; break;
            case 3: appState.settings.bufferSize = BufferSize::samples512; break;
            case 4: appState.settings.bufferSize = BufferSize::samples1024; break;
        }
    }
}

void SettingsComponent::buttonClicked(juce::Button* button)
{
    if (button == &measureLatencyButton)
    {
        if (onMeasureLatency)
            onMeasureLatency();
    }
    else if (button == &startLoopTestButton)
    {
        if (onStartLoopTest)
            onStartLoopTest();
        stopLoopTestButton.setEnabled(true);
        startLoopTestButton.setEnabled(false);
    }
    else if (button == &stopLoopTestButton)
    {
        if (onStopLoopTest)
            onStopLoopTest();
        stopLoopTestButton.setEnabled(false);
        startLoopTestButton.setEnabled(true);
    }
    else if (button == &refreshDevicesButton)
    {
        if (onRefreshDevices)
            onRefreshDevices();
    }
    else if (button == &chooseOutputFolderButton)
    {
        if (onOutputFolderSelected)
            onOutputFolderSelected();
    }
    else if (button == &reverbModeToggle)
    {
        appState.settings.useReverbMode = reverbModeToggle.getToggleState();
    }
    else if (button == &trimSilenceToggle)
    {
        appState.settings.trimEnabled = trimSilenceToggle.getToggleState();
    }
}

void SettingsComponent::sliderValueChanged(juce::Slider* slider)
{
    if (slider == &noiseFloorMarginSlider)
    {
        float value = (float)noiseFloorMarginSlider.getValue();
        appState.settings.noiseFloorMarginPercent = value;
        noiseFloorValueLabel.setText(juce::String((int)value) + "%", juce::dontSendNotification);
    }
    else if (slider == &silenceDelaySlider)
    {
        int value = (int)silenceDelaySlider.getValue();
        appState.settings.silenceBetweenFilesMs = value;
        silenceDelayValueLabel.setText(juce::String(value) + " ms", juce::dontSendNotification);
    }
}

void SettingsComponent::updateFromState()
{
    auto rebuildComboIfNeeded = [](juce::ComboBox& combo, const auto& itemsProvider)
    {
        const auto items = itemsProvider();
        bool needsRefresh = (combo.getNumItems() != items.size());

        if (!needsRefresh)
        {
            for (int i = 0; i < items.size(); ++i)
            {
                if (combo.getItemText(i) != items[i])
                {
                    needsRefresh = true;
                    break;
                }
            }
        }

        if (needsRefresh)
        {
            combo.clear(juce::dontSendNotification);
            for (int i = 0; i < items.size(); ++i)
                combo.addItem(items[i], i + 1);
        }

        return items;
    };

    // Update device list
    auto deviceNames = rebuildComboIfNeeded(deviceCombo, [&]()
    {
        juce::StringArray names;
        for (const auto& device : appState.devices)
            names.add(device.name);
        return names;
    });

    if (!appState.selectedDeviceID.isEmpty())
    {
        for (int i = 0; i < appState.devices.size(); ++i)
        {
            if (appState.devices[i].uniqueID == appState.selectedDeviceID)
            {
                if (deviceCombo.getSelectedItemIndex() != i)
                    deviceCombo.setSelectedItemIndex(i, juce::dontSendNotification);

                const auto& device = appState.devices.getReference(i);
                deviceInfoLabel.setText(juce::String(device.inputChannelCount) + " inputs, " +
                                       juce::String(device.outputChannelCount) + " outputs",
                                       juce::dontSendNotification);
                break;
            }
        }
    }
    else if (deviceNames.isEmpty())
    {
        deviceInfoLabel.setText("No devices found", juce::dontSendNotification);
    }

    // Update input pairs
    auto inputPairs = appState.getAvailableInputPairs();
    auto inputItems = rebuildComboIfNeeded(inputPairCombo, [&]()
    {
        juce::StringArray names;
        for (const auto& pair : inputPairs)
            names.add(pair.getDisplayName());
        return names;
    });

    if (appState.hasInputPair)
    {
        for (int i = 0; i < inputPairs.size(); ++i)
        {
            if (inputPairs[i] == appState.selectedInputPair)
            {
                if (inputPairCombo.getSelectedItemIndex() != i)
                    inputPairCombo.setSelectedItemIndex(i, juce::dontSendNotification);
                break;
            }
        }

        inputInfoLabel.setText("Ch " + juce::String(appState.selectedInputPair.leftChannel) +
                              " (L) + Ch " + juce::String(appState.selectedInputPair.rightChannel) + " (R)",
                              juce::dontSendNotification);
    }
    else if (inputItems.isEmpty())
    {
        inputInfoLabel.setText("No input channels available", juce::dontSendNotification);
    }
    else
    {
        inputInfoLabel.setText("No input pair selected", juce::dontSendNotification);
    }

    // Update output pairs
    auto outputPairs = appState.getAvailableOutputPairs();
    auto outputItems = rebuildComboIfNeeded(outputPairCombo, [&]()
    {
        juce::StringArray names;
        for (const auto& pair : outputPairs)
            names.add(pair.getDisplayName());
        return names;
    });

    if (appState.hasOutputPair)
    {
        for (int i = 0; i < outputPairs.size(); ++i)
        {
            if (outputPairs[i] == appState.selectedOutputPair)
            {
                if (outputPairCombo.getSelectedItemIndex() != i)
                    outputPairCombo.setSelectedItemIndex(i, juce::dontSendNotification);
                break;
            }
        }

        outputInfoLabel.setText("Ch " + juce::String(appState.selectedOutputPair.leftChannel) +
                               " (L) + Ch " + juce::String(appState.selectedOutputPair.rightChannel) + " (R)",
                               juce::dontSendNotification);
    }
    else if (outputItems.isEmpty())
    {
        outputInfoLabel.setText("No output channels available", juce::dontSendNotification);
    }
    else
    {
        outputInfoLabel.setText("No output pair selected", juce::dontSendNotification);
    }

    // Update latency display
    if (appState.settings.measuredLatencySamples >= 0)
    {
        latencyValueLabel.setText(juce::String(appState.settings.measuredLatencySamples) + " samples (" +
                                 juce::String(appState.settings.getLatencyInMs(), 2) + " ms)",
                                 juce::dontSendNotification);
        latencyValueLabel.setColour(juce::Label::textColourId, juce::Colour(0xff34c759)); // Green
    }

    // Update output folder
    if (appState.settings.outputFolderPath.isNotEmpty())
    {
        juce::File folder(appState.settings.outputFolderPath);
        outputFolderPathLabel.setText(folder.getFileName(), juce::dontSendNotification);
        outputFolderPathLabel.setColour(juce::Label::textColourId, juce::Colour(0xff1d1d1f));
    }

    // Update postfix
    if (appState.settings.outputPostfix.isNotEmpty())
    {
        filenamePostfixEditor.setText(appState.settings.outputPostfix);
    }

    // Update reverb mode
    reverbModeToggle.setToggleState(appState.settings.useReverbMode, juce::dontSendNotification);
    noiseFloorMarginSlider.setValue(appState.settings.noiseFloorMarginPercent, juce::dontSendNotification);
    silenceDelaySlider.setValue(appState.settings.silenceBetweenFilesMs, juce::dontSendNotification);
    trimSilenceToggle.setToggleState(appState.settings.trimEnabled, juce::dontSendNotification);
}

void SettingsComponent::drawSectionHeader(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title)
{
    g.setColour(juce::Colour(0xff1d1d1f));
    g.setFont(makeFont(12.0f, true));
    g.drawText(title, bounds, juce::Justification::centredLeft);
}
