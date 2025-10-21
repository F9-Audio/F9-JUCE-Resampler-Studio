#include "JUCEIteratorFix.h"  // MUST be first - Fix for StrideIterator compatibility
#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent()
    : settingsComponent(appState),
      fileListAndLogComponent(appState)
{
    // Apply custom look and feel
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);

    // Register audio formats
    formatManager.registerBasicFormats();

    // Initialize audio system with basic stereo I/O
    // This will use the default device temporarily until user selects one
    setAudioChannels(2, 2);  // 2 inputs, 2 outputs

    // Set window size (more compact than original)
    setSize(1100, 650);

    // Add UI components
    addAndMakeVisible(settingsComponent);
    addAndMakeVisible(fileListAndLogComponent);

    // Wire up callbacks
    settingsComponent.onRefreshDevices = [this]() { refreshDevices(); };
    settingsComponent.onMeasureLatency = [this]() { startLatencyMeasurement(); };
    settingsComponent.onStartLoopTest = [this]() { startHardwareTest(); };
    settingsComponent.onStopLoopTest = [this]() { stopHardwareTest(); };
    settingsComponent.onDeviceSelected = [this](const juce::String& deviceID) { selectDevice(deviceID); };
    settingsComponent.onInputPairSelected = [this](int index)
    {
        auto pairs = appState.getAvailableInputPairs();
        if (index >= 0 && index < pairs.size())
            selectInputPair(pairs[index]);
    };
    settingsComponent.onOutputPairSelected = [this](int index)
    {
        auto pairs = appState.getAvailableOutputPairs();
        if (index >= 0 && index < pairs.size())
            selectOutputPair(pairs[index]);
    };
    settingsComponent.onOutputFolderSelected = [this]()
    {
        auto chooser = std::make_shared<juce::FileChooser>("Select Output Folder");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                            [this, chooser](const juce::FileChooser& fc)
        {
            auto folder = fc.getResult();
            if (folder.exists())
            {
                appState.settings.outputFolderPath = folder.getFullPathName();
                settingsComponent.updateFromState();
                appState.appendLog("Output folder set: " + folder.getFullPathName());
            }
        });
    };

    fileListAndLogComponent.onFilesAdded = [this](const juce::Array<juce::File>& files) { addFiles(files); };
    fileListAndLogComponent.onPreviewClicked = [this]() { startPreview(); };
    fileListAndLogComponent.onProcessAllClicked = [this]() { startProcessing(); };
    fileListAndLogComponent.onCopyLog = [this]()
    {
        juce::String logText;
        for (const auto& line : appState.logLines)
            logText += line + "\n";
        juce::SystemClipboard::copyTextToClipboard(logText);
        appState.appendLog("Log copied to clipboard");
    };

    // CRITICAL: Wire up device reconfiguration callback for sample rate/buffer changes
    settingsComponent.onDeviceNeedsReconfiguration = [this]()
    {
        configureAudioDevice();
    };

    // Start timer for UI updates (30 Hz)
    startTimer(33);

    // Log startup
    appState.appendLog("F9 Batch Resampler started");

    // Populate device list
    refreshDevices();
    settingsComponent.updateFromState();
    fileListAndLogComponent.updateFromState();
}

MainComponent::~MainComponent()
{
    shutdownAudio();
}

//==============================================================================
// AudioAppComponent Overrides

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    // CRITICAL: Update appState with ACTUAL device settings
    appState.settings.sampleRate = sampleRate;
    appState.settings.bufferSize = static_cast<BufferSize>(samplesPerBlockExpected);

    // Allocate our input buffer (for capturing device inputs)
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device != nullptr)
    {
        int numInputChannels = device->getActiveInputChannels().countNumberOfSetBits();
        inputBuffer.setSize(numInputChannels, samplesPerBlockExpected);
        appState.appendLog("Input buffer allocated: " + juce::String(numInputChannels) + " channels");
    }

    // Allocate buffers for audio processing
    appState.currentPlaybackBuffer.setSize(2, samplesPerBlockExpected * 100);
    appState.recordingBuffer.setSize(2, static_cast<int>(sampleRate * 60));
    appState.latencyCaptureBuffer.setSize(2, static_cast<int>(sampleRate * 5));

    appState.appendLog("Audio system prepared: " + juce::String(sampleRate) + " Hz, " +
                       juce::String(samplesPerBlockExpected) + " samples/block");
}

void MainComponent::releaseResources()
{
    appState.appendLog("Audio resources released");
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    // ============================================================================
    // SIMPLE AUDIO CALLBACK - Just test basic stereo output first
    // ============================================================================

    const int numSamples = bufferToFill.numSamples;
    const int numChannels = bufferToFill.buffer->getNumChannels();

    // Clear all outputs by default
    bufferToFill.clearActiveBufferRegion();

    // State machine based on appState flags

    if (appState.isTestingHardware)
    {
        // ============================================================
        // HARDWARE TEST MODE: Generate 1kHz sine wave (stereo for now)
        // ============================================================

        if (numChannels >= 2)
        {
            const float amplitude = 0.5f;
            const float phaseIncrement = (sineFrequency * 2.0f * juce::MathConstants<float>::pi) /
                                         (float)appState.settings.sampleRate;

            float* leftData = bufferToFill.buffer->getWritePointer(0, bufferToFill.startSample);
            float* rightData = bufferToFill.buffer->getWritePointer(1, bufferToFill.startSample);

            for (int i = 0; i < numSamples; ++i)
            {
                float sample = amplitude * std::sin(sinePhase);
                leftData[i] = sample;
                rightData[i] = sample;

                sinePhase += phaseIncrement;
                if (sinePhase >= 2.0f * juce::MathConstants<float>::pi)
                    sinePhase -= 2.0f * juce::MathConstants<float>::pi;
            }
        }
    }
    // TODO: Implement other modes
}

//==============================================================================
// Timer Callback (Message Thread)

void MainComponent::timerCallback()
{
    // Handle file saving (triggered by audio thread)
    if (needsToSaveCurrentFile)
    {
        needsToSaveCurrentFile = false;
        saveCurrentRecording();

        // Move to next file or finish
        appState.currentFileIndex++;
        if (appState.currentFileIndex < appState.files.size())
        {
            // Load next file
            if (loadNextFileForProcessing())
            {
                // Reset for next file
                playbackSamplePosition = 0;
                recordingSamplePosition = 0;
                appState.recordingBuffer.clear();
                appState.isProcessing = true;

                // Add silence delay
                juce::Thread::sleep(appState.settings.silenceBetweenFilesMs);
            }
        }
        else
        {
            // All files processed
            appState.appendLog("Batch processing complete");
            appState.currentFileIndex = 0;
            appState.processingProgress = 0.0;
        }
    }

    // Handle next file loading for preview
    if (needsToLoadNextFile)
    {
        needsToLoadNextFile = false;
        appState.currentPreviewFileIndex++;

        if (appState.currentPreviewFileIndex < appState.previewPlaylist.size())
        {
            // Load next preview file
            // Find file by ID
            for (int i = 0; i < appState.files.size(); ++i)
            {
                if (appState.files[i].id == appState.previewPlaylist[appState.currentPreviewFileIndex])
                {
                    // Load this file
                    auto reader = formatManager.createReaderFor(appState.files[i].url);
                    if (reader != nullptr)
                    {
                        // --- THIS IS THE NEW, CORRECTED CODE ---
                        
                        // 1. Set our playback buffer to be STEREO and the correct length.
                        appState.currentPlaybackBuffer.setSize(2, (int)reader->lengthInSamples);
                        appState.currentPlaybackBuffer.clear();

                        // 2. Read the file into the stereo buffer.
                        //    If the file is mono, reader->read() will correctly copy it to both L/R channels.
                        //    If the file is stereo, it will copy L->L and R->R.
                        reader->read(&appState.currentPlaybackBuffer,    // dest buffer
                                     0,                                // dest start sample
                                     (int)reader->lengthInSamples,     // num samples
                                     0,                                // file start sample
                                     true,                             // use L channel
                                     true);                            // use R channel
                        
                        // --- END OF NEW CODE ---
                        delete reader;

                        playbackSamplePosition = 0;
                        appState.isPreviewing = true;

                        // Add silence delay
                        juce::Thread::sleep(appState.settings.silenceBetweenFilesMs);
                    }
                    break;
                }
            }
        }
        else
        {
            // Preview finished
            appState.isPreviewing = false;
            appState.currentPreviewFileIndex = -1;
            appState.appendLog("Preview complete");
        }
    }

    // Handle latency measurement completion
    if (needsToCompleteLatencyMeasurement)
    {
        needsToCompleteLatencyMeasurement = false;

        // Find peak in captured audio
        int peakPosition = findPeakPosition(appState.latencyCaptureBuffer, 0.1f);

        if (peakPosition >= 0)
        {
            // Convert from frames to samples (interleaved equivalent)
            appState.settings.measuredLatencySamples = peakPosition * 2; // Assuming stereo
            appState.settings.lastBufferSizeWhenMeasured = appState.settings.bufferSize;

            // Measure noise floor
            float noiseFloorDb = calculateNoiseFloorDb(appState.latencyCaptureBuffer);
            appState.settings.measuredNoiseFloorDb = noiseFloorDb;
            appState.settings.hasNoiseFloorMeasurement = true;

            appState.appendLog("Latency measured: " + juce::String(peakPosition) + " samples (" +
                             juce::String(appState.settings.getLatencyInMs(), 2) + " ms)");
            appState.appendLog("Noise floor measured: " + juce::String(noiseFloorDb, 1) + " dB");
        }
        else
        {
            appState.appendLog("Error: Could not detect impulse in captured audio");
        }

        // Clear capture buffer
        appState.latencyCaptureBuffer.clear();
    }

    // Update progress
    if (appState.isProcessing && appState.files.size() > 0)
    {
        appState.processingProgress = (double)appState.currentFileIndex / appState.files.size();
    }

    if (appState.isPreviewing && appState.previewPlaylist.size() > 0)
    {
        appState.previewProgress = (double)appState.currentPreviewFileIndex / appState.previewPlaylist.size();
    }

    // Update UI components
    settingsComponent.updateFromState();
    fileListAndLogComponent.updateFromState();

    // Trigger UI repaint
    repaint();
}

//==============================================================================
// UI Methods

void MainComponent::paint(juce::Graphics& g)
{
    // Background is handled by components
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();

    // Left sidebar (settings) - fixed width
    settingsComponent.setBounds(bounds.removeFromLeft(340));

    // Right side (file list + log)
    fileListAndLogComponent.setBounds(bounds);
}

//==============================================================================
// Device Management

void MainComponent::refreshDevices()
{
    appState.devices.clear();

    // Get audio device types
    const juce::OwnedArray<juce::AudioIODeviceType>& deviceTypes = deviceManager.getAvailableDeviceTypes();

    for (auto* type : deviceTypes)
    {
        type->scanForDevices();
        juce::StringArray deviceNames = type->getDeviceNames(false); // false = output devices

        for (const auto& name : deviceNames)
        {
            AudioDevice device;
            device.name = name;
            device.deviceTypeName = type->getTypeName();

            // Get channel counts - use device name as uniqueID
            // Device type + name together form the unique identifier
            device.uniqueID = name;

            if (auto* deviceInfo = type->createDevice(name, name))
            {
                device.outputChannelCount = deviceInfo->getOutputChannelNames().size();
                device.inputChannelCount = deviceInfo->getInputChannelNames().size();
                delete deviceInfo;
            }

            // Filter out built-in devices
            if (!device.isBuiltIn())
            {
                appState.devices.add(device);
            }
        }
    }

    appState.appendLog("Found " + juce::String(appState.devices.size()) + " external audio devices");
}

void MainComponent::selectDevice(const juce::String& deviceID)
{
    appState.selectedDeviceID = deviceID;

    // Auto-select first input and output pairs
    auto inputPairs = appState.getAvailableInputPairs();
    auto outputPairs = appState.getAvailableOutputPairs();

    if (!inputPairs.isEmpty())
    {
        appState.selectedInputPair = inputPairs[0];
        appState.hasInputPair = true;
        appState.appendLog("Auto-selected input: " + inputPairs[0].getDisplayName());
    }
    else
    {
        appState.hasInputPair = false;
    }

    if (!outputPairs.isEmpty())
    {
        appState.selectedOutputPair = outputPairs[0];
        appState.hasOutputPair = true;
        appState.appendLog("Auto-selected output: " + outputPairs[0].getDisplayName());
    }
    else
    {
        appState.hasOutputPair = false;
    }

    configureAudioDevice();
}

void MainComponent::selectInputPair(const StereoPair& pair)
{
    appState.selectedInputPair = pair;
    appState.hasInputPair = true;
    appState.appendLog("Selected input: " + pair.getDisplayName());
    configureAudioDevice();
}

void MainComponent::selectOutputPair(const StereoPair& pair)
{
    appState.selectedOutputPair = pair;
    appState.hasOutputPair = true;
    appState.appendLog("Selected output: " + pair.getDisplayName());
    configureAudioDevice();
}

void MainComponent::populateDeviceList()
{
    // Implemented in refreshDevices()
}

void MainComponent::configureAudioDevice()
{
    // Configure the selected device
    if (appState.selectedDeviceID.isEmpty())
    {
        appState.appendLog("Warning: No device selected");
        return;
    }

    // Find the selected device to get its type name
    AudioDevice* selectedDevice = appState.getSelectedDevice();
    if (selectedDevice == nullptr)
    {
        appState.appendLog("Error: Selected device not found");
        return;
    }

    // CRITICAL: Set the device type FIRST, before calling setAudioDeviceSetup
    // This ensures we're targeting the correct CoreAudio device type
    deviceManager.setCurrentAudioDeviceType(selectedDevice->deviceTypeName, true);
    appState.appendLog("Set device type: " + selectedDevice->deviceTypeName);

    // Get current setup and modify it (don't create from scratch)
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup(setup);

    // Update setup with our selected device and settings
    setup.outputDeviceName = selectedDevice->name;
    setup.inputDeviceName = selectedDevice->name;
    setup.sampleRate = appState.settings.sampleRate;
    setup.bufferSize = static_cast<int>(appState.settings.bufferSize);

    // Don't use default channels - we set specific ones
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = false;

    setup.inputChannels.clear();
    setup.outputChannels.clear();

    auto setStereoBits = [](juce::BigInteger& bitset, const StereoPair& pair)
    {
        // Channels are 1-indexed in UI, but 0-indexed in JUCE
        bitset.setBit(juce::jmax(0, pair.leftChannel - 1));
        bitset.setBit(juce::jmax(0, pair.rightChannel - 1));
    };

    // Set the specific input and output channels we want
    if (appState.hasInputPair)
    {
        setStereoBits(setup.inputChannels, appState.selectedInputPair);
        appState.appendLog("Input channels: " + juce::String(appState.selectedInputPair.leftChannel) +
                         ", " + juce::String(appState.selectedInputPair.rightChannel));
    }

    if (appState.hasOutputPair)
    {
        setStereoBits(setup.outputChannels, appState.selectedOutputPair);
        appState.appendLog("Output channels: " + juce::String(appState.selectedOutputPair.leftChannel) +
                         ", " + juce::String(appState.selectedOutputPair.rightChannel));
    }

    // Apply the setup - this will reconfigure the already-running audio system
    juce::String error = deviceManager.setAudioDeviceSetup(setup, true);

    if (error.isNotEmpty())
    {
        appState.appendLog("Error configuring device: " + error);
        return;
    }

    // Verify the device was opened correctly
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
    {
        appState.appendLog("Error: Device failed to open");
        return;
    }

    // CRITICAL: Update appState to match ACTUAL device settings
    // The device may not support the requested sample rate and will open at its preferred rate
    double actualSampleRate = device->getCurrentSampleRate();
    int actualBufferSize = device->getCurrentBufferSizeSamples();

    appState.settings.sampleRate = actualSampleRate;
    appState.settings.bufferSize = static_cast<BufferSize>(actualBufferSize);

    // Log success with actual device info
    appState.appendLog("Device configured: " + device->getName());
    appState.appendLog("Device type: " + selectedDevice->deviceTypeName);
    appState.appendLog("Sample rate: " + juce::String(actualSampleRate) + " Hz");
    appState.appendLog("Buffer size: " + juce::String(actualBufferSize) + " samples");

    // Invalidate latency measurement if sample rate or buffer changed
    appState.settings.measuredLatencySamples = -1;
    appState.settings.hasNoiseFloorMeasurement = false;

    // Apply device setup
    juce::String error2 = deviceManager.setAudioDeviceSetup(setup, true);

    if (!error2.isEmpty())
    {
        appState.appendLog("Error applying device setup: " + error2);
    }
    else
    {
        appState.appendLog("Device configured successfully");
    }
}

//==============================================================================
// File Management

void MainComponent::addFiles(const juce::Array<juce::File>& files)
{
    for (const auto& file : files)
    {
        AudioFile audioFile(file);
        appState.files.add(audioFile);

        if (audioFile.isValid())
        {
            appState.appendLog("Added: " + audioFile.getFileName() +
                             " (" + juce::String(audioFile.sampleRate / 1000.0, 1) + " kHz)");
        }
        else
        {
            appState.appendLog("Warning: Invalid sample rate - " + audioFile.getFileName());
        }
    }
}

void MainComponent::clearFiles()
{
    appState.files.clear();
    appState.appendLog("File list cleared");
}

void MainComponent::toggleFileSelection(int fileIndex)
{
    if (juce::isPositiveAndBelow(fileIndex, appState.files.size()))
    {
        appState.files.getReference(fileIndex).isSelected =
            !appState.files.getReference(fileIndex).isSelected;
    }
}

//==============================================================================
// Processing Operations

void MainComponent::startProcessing()
{
    if (!appState.canMeasureLatency())
    {
        appState.appendLog("Error: Please select input and output devices first");
        return;
    }

    if (appState.settings.measuredLatencySamples < 0)
    {
        appState.appendLog("Error: Latency not measured - please measure latency first");
        return;
    }

    if (appState.files.isEmpty())
    {
        appState.appendLog("Error: No files to process");
        return;
    }

    if (appState.settings.outputFolderPath.isEmpty())
    {
        appState.appendLog("Error: No output folder selected");
        return;
    }

    // Start processing
    appState.currentFileIndex = 0;
    appState.isProcessing = false; // Will be set to true after first file loads

    if (loadNextFileForProcessing())
    {
        playbackSamplePosition = 0;
        recordingSamplePosition = 0;
        appState.recordingBuffer.clear();
        appState.isProcessing = true;

        appState.appendLog("Starting batch processing of " +
                         juce::String(appState.files.size()) + " files");
    }
}

void MainComponent::stopAllAudio()
{
    appState.isProcessing = false;
    appState.isPreviewing = false;
    appState.isMeasuringLatency = false;
    appState.isTestingHardware = false;

    playbackSamplePosition = 0;
    recordingSamplePosition = 0;

    appState.appendLog("Stopped");
}

void MainComponent::startLatencyMeasurement()
{
    if (!appState.canMeasureLatency())
    {
        appState.appendLog("Error: Please select input and output devices first");
        return;
    }

    appState.latencyCaptureBuffer.clear();
    impulseSent = false;
    capturedSamplesSinceImpulse = 0;
    appState.isMeasuringLatency = true;

    appState.appendLog("Measuring latency...");
}

void MainComponent::startPreview()
{
    // Build playlist from selected files
    appState.previewPlaylist.clear();
    for (const auto& file : appState.files)
    {
        if (file.isSelected && file.isValid())
        {
            appState.previewPlaylist.add(file.id);
        }
    }

    if (appState.previewPlaylist.isEmpty())
    {
        appState.appendLog("Error: No files selected for preview");
        return;
    }

    appState.currentPreviewFileIndex = 0;
    needsToLoadNextFile = true; // Trigger first file load
    appState.appendLog("Preview started with " + juce::String(appState.previewPlaylist.size()) + " files");
}

void MainComponent::stopPreview()
{
    appState.isPreviewing = false;
    appState.previewPlaylist.clear();
    appState.currentPreviewFileIndex = -1;
    appState.appendLog("Preview stopped");
}

void MainComponent::startHardwareTest()
{
    if (!appState.canMeasureLatency())
    {
        appState.appendLog("Error: Please select input and output devices first");
        return;
    }

    sinePhase = 0.0f;
    appState.isTestingHardware = true;
    appState.appendLog("Hardware loop test started (1 kHz sine wave)");
}

void MainComponent::stopHardwareTest()
{
    appState.isTestingHardware = false;
    appState.appendLog("Hardware loop test stopped");
}

//==============================================================================
// File Processing Helpers

bool MainComponent::loadNextFileForProcessing()
{
    if (!juce::isPositiveAndBelow(appState.currentFileIndex, appState.files.size()))
        return false;

    AudioFile& file = appState.files.getReference(appState.currentFileIndex);

    if (!file.isValid())
    {
        appState.appendLog("Skipping invalid file: " + file.getFileName());
        return false;
    }

    auto* reader = formatManager.createReaderFor(file.url);
    if (reader == nullptr)
    {
        appState.appendLog("Error: Could not read file - " + file.getFileName());
        return false;
    }

    // --- THIS IS THE NEW, CORRECTED CODE ---
    
    // 1. Set our playback buffer to be STEREO and the correct length.
    appState.currentPlaybackBuffer.setSize(2, (int)reader->lengthInSamples);
    appState.currentPlaybackBuffer.clear();

    // 2. Read the file into the stereo buffer.
    //    If the file is mono, reader->read() will correctly copy it to both L/R channels.
    //    If the file is stereo, it will copy L->L and R->R.
    reader->read(&appState.currentPlaybackBuffer,    // dest buffer
                 0,                                // dest start sample
                 (int)reader->lengthInSamples,     // num samples
                 0,                                // file start sample
                 true,                             // use L channel
                 true);                            // use R channel
    
    // --- END OF NEW CODE ---
    delete reader;

    file.status = ProcessingStatus::processing;
    appState.currentProcessingFile = file.getFileName();
    appState.appendLog("Processing: " + file.getFileName());

    return true;
}

void MainComponent::saveCurrentRecording()
{
    if (appState.currentFileIndex >= appState.files.size())
        return;

    AudioFile& sourceFile = appState.files.getReference(appState.currentFileIndex);

    // Trim latency from recording
    juce::AudioBuffer<float> trimmed = trimLatency(
        appState.recordingBuffer,
        appState.settings.measuredLatencySamples,
        appState.currentPlaybackBuffer.getNumSamples()
    );

    // Apply DC removal if enabled
    if (appState.settings.dcRemovalEnabled)
    {
        removeDCOffset(trimmed);
    }

    // Generate output file path
    juce::File outputFile = generateOutputFile(sourceFile);

    // Write file
    std::unique_ptr<juce::OutputStream> fileStream(outputFile.createOutputStream());

    if (fileStream == nullptr)
    {
        sourceFile.status = ProcessingStatus::failed;
        appState.appendLog("Error: Could not create output stream for file - " + outputFile.getFileName());
        return;
    }

    juce::WavAudioFormat wavFormat;
    auto writer = wavFormat.createWriterFor(
        fileStream,
        juce::AudioFormatWriter::Options{}
            .withSampleRate(appState.settings.sampleRate)
            .withNumChannels(trimmed.getNumChannels())
            .withBitsPerSample(24)
    );

    if (writer == nullptr)
    {
        sourceFile.status = ProcessingStatus::failed;
        appState.appendLog("Error: Could not initialise writer for file - " + outputFile.getFileName());
        return;
    }

    writer->writeFromAudioSampleBuffer(trimmed, 0, trimmed.getNumSamples());
    writer.reset(); // Flush and close

    sourceFile.status = ProcessingStatus::completed;
    appState.appendLog("Saved: " + outputFile.getFileName());
}

juce::File MainComponent::generateOutputFile(const AudioFile& sourceFile)
{
    juce::File outputFolder(appState.settings.outputFolderPath);
    juce::String baseName = sourceFile.url.getFileNameWithoutExtension();
    juce::String extension = sourceFile.url.getFileExtension();

    if (appState.settings.outputPostfix.isNotEmpty())
    {
        baseName += appState.settings.outputPostfix;
    }

    return outputFolder.getChildFile(baseName + extension);
}

//==============================================================================
// Critical Audio Algorithms

juce::AudioBuffer<float> MainComponent::trimLatency(
    const juce::AudioBuffer<float>& captured,
    int latencySamples,
    int originalLength)
{
    // CRITICAL: This implements the exact algorithm from LATENCY_TRIMMING_FIX.md
    // latencySamples is in INTERLEAVED samples (already multiplied by channel count)
    // originalLength is in FRAMES

    const int numChannels = captured.getNumChannels();
    const int capturedFrames = captured.getNumSamples();
    const int latencyFrames = latencySamples / numChannels;

    // Skip latency frames, extract exactly originalLength frames
    const int startFrame = latencyFrames;
    int framesToCopy = originalLength;

    // Handle insufficient capture
    if (startFrame + framesToCopy > capturedFrames)
    {
        framesToCopy = juce::jmax(0, capturedFrames - startFrame);
    }

    // Create output buffer
    juce::AudioBuffer<float> trimmed(numChannels, originalLength);
    trimmed.clear();

    // Copy samples
    if (framesToCopy > 0 && startFrame >= 0)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            trimmed.copyFrom(ch, 0, captured, ch, startFrame, framesToCopy);
        }
    }

    return trimmed;
}

bool MainComponent::isReverbTailBelowNoiseFloor(const juce::AudioBuffer<float>& audioWindow)
{
    // Calculate RMS of window
    float rms = calculateRMS(audioWindow);

    // Convert to dB
    float windowDb = 20.0f * std::log10(juce::jmax(rms, 1e-10f));

    // Get threshold
    float thresholdDb = appState.settings.getNoiseFloorThresholdDb();

    bool isBelowThreshold = windowDb < thresholdDb;

    if (isBelowThreshold)
    {
        DBG("Reverb tail detected: " << windowDb << " dB < threshold " << thresholdDb << " dB");
    }

    return isBelowThreshold;
}

void MainComponent::removeDCOffset(juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* data = buffer.getWritePointer(ch);
        const int numSamples = buffer.getNumSamples();

        // Calculate DC offset (mean)
        float sum = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            sum += data[i];

        float dcOffset = sum / numSamples;

        // Remove offset
        for (int i = 0; i < numSamples; ++i)
            data[i] -= dcOffset;
    }
}

//==============================================================================
// Signal Generation

void MainComponent::generateSineWave(juce::AudioBuffer<float>& buffer, int numSamples)
{
    const float amplitude = 0.5f;
    const float phaseIncrement = (sineFrequency * 2.0f * juce::MathConstants<float>::pi) /
                                 (float)appState.settings.sampleRate;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* data = buffer.getWritePointer(ch);
        float phase = sinePhase;

        for (int i = 0; i < numSamples; ++i)
        {
            data[i] = amplitude * std::sin(phase);
            phase += phaseIncrement;

            // Wrap phase
            if (phase >= 2.0f * juce::MathConstants<float>::pi)
                phase -= 2.0f * juce::MathConstants<float>::pi;
        }
    }

    sinePhase += phaseIncrement * numSamples;
    if (sinePhase >= 2.0f * juce::MathConstants<float>::pi)
        sinePhase -= 2.0f * juce::MathConstants<float>::pi;
}

void MainComponent::generateImpulse(juce::AudioBuffer<float>& buffer)
{
    buffer.clear();

    const float amplitude = 0.9f;

    // Set first sample of all channels to impulse
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        buffer.setSample(ch, 0, amplitude);
    }
}

//==============================================================================
// Analysis Helpers

int MainComponent::findPeakPosition(const juce::AudioBuffer<float>& buffer, float threshold)
{
    float maxValue = 0.0f;
    int maxPosition = -1;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        const int numSamples = buffer.getNumSamples();

        for (int i = 0; i < numSamples; ++i)
        {
            float absValue = std::abs(data[i]);
            if (absValue > maxValue)
            {
                maxValue = absValue;
                maxPosition = i;
            }
        }
    }

    if (maxValue > threshold)
        return maxPosition;

    return -1; // No peak found
}

float MainComponent::calculateNoiseFloorDb(const juce::AudioBuffer<float>& buffer)
{
    float rms = calculateRMS(buffer);
    return 20.0f * std::log10(juce::jmax(rms, 1e-6f));
}

float MainComponent::calculateRMS(const juce::AudioBuffer<float>& buffer)
{
    double sumOfSquares = 0.0;
    int totalSamples = 0;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        const int numSamples = buffer.getNumSamples();

        for (int i = 0; i < numSamples; ++i)
        {
            sumOfSquares += data[i] * data[i];
            totalSamples++;
        }
    }

    if (totalSamples == 0)
        return 0.0f;

    return std::sqrt(sumOfSquares / totalSamples);
}
