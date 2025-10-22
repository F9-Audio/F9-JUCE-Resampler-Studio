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

    // Request microphone permissions on macOS
    appState.appendLog("Requesting microphone permissions...");
    juce::RuntimePermissions::request(
        juce::RuntimePermissions::recordAudio,
        [this](bool granted)
        {
            if (granted)
                appState.appendLog("Microphone access GRANTED");
            else
                appState.appendLog("ERROR: Microphone access DENIED - please enable in System Settings");
        }
    );

    // Initialize audio system with basic stereo I/O
    // setAudioChannels will set up deviceManager
    setAudioChannels(2, 2);  // 2 inputs, 2 outputs

    // Register our custom callback to capture input audio
    // This runs ALONGSIDE AudioAppComponent's callback
    deviceManager.addAudioCallback(&customCallback);

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
    fileListAndLogComponent.onPreviewClicked = [this]() 
    { 
        if (appState.isPreviewing)
            stopPreview();
        else
            startPreview();
    };
    fileListAndLogComponent.onProcessAllClicked = [this]() { startProcessing(); };
    fileListAndLogComponent.onCopyLog = [this]()
    {
        juce::String logText;
        for (const auto& line : appState.logLines)
            logText += line + "\n";
        juce::SystemClipboard::copyTextToClipboard(logText);
        appState.appendLog("Log copied to clipboard");
    };
    fileListAndLogComponent.onClearAll = [this]()
    {
        clearFiles();
        fileListAndLogComponent.updateFromState();
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
    deviceManager.removeAudioCallback(&customCallback);
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

void MainComponent::handleAudioCallback(const float* const* inputChannelData,
                                        int numInputChannels,
                                        float* const* outputChannelData,
                                        int numOutputChannels,
                                        int numSamples)
{
    // ============================================================================
    // RAW AUDIO CALLBACK - We get both input and output here!
    // ============================================================================

    // First, copy input to our input buffer for processing
    if (numInputChannels > 0 && inputChannelData != nullptr)
    {
        inputBuffer.setSize(numInputChannels, numSamples, false, false, true);

        for (int ch = 0; ch < numInputChannels; ++ch)
        {
            if (inputChannelData[ch] != nullptr)
            {
                inputBuffer.copyFrom(ch, 0, inputChannelData[ch], numSamples);
            }
        }
    }
    else
    {
        // No input available
        inputBuffer.setSize(0, 0);
    }

    // Now call the standard getNextAudioBlock for output processing
    // Create an AudioSourceChannelInfo wrapping the output buffers
    juce::AudioBuffer<float> outputBuffer(outputChannelData, numOutputChannels, numSamples);
    juce::AudioSourceChannelInfo bufferToFill;
    bufferToFill.buffer = &outputBuffer;
    bufferToFill.startSample = 0;
    bufferToFill.numSamples = numSamples;

    getNextAudioBlock(bufferToFill);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    // ============================================================================
    // AUDIO CALLBACK - Real-time audio processing state machine
    // ============================================================================

    const int numSamples = bufferToFill.numSamples;
    const int numChannels = bufferToFill.buffer->getNumChannels();

    // NOTE: Input audio is captured by our CustomAudioCallback in handleAudioCallback()
    // The inputBuffer is already populated before this function is called

    // Clear all outputs by default
    bufferToFill.clearActiveBufferRegion();

    // ============================================================================
    // STATE MACHINE: Route audio based on operation mode
    // ============================================================================

    if (appState.isMeasuringLatency)
    {
        // ================================================================
        // LATENCY MEASUREMENT MODE
        // Send impulse, capture response, detect peak position
        // ================================================================

        if (!impulseSent)
        {
            // Send impulse on first buffer (single sample at max amplitude)
            // IMPORTANT: Output buffer contains only ACTIVE channels in sequential order
            // If we enabled output channels 3-4, they appear as indices 0-1 in the buffer
            if (numChannels >= 2 && appState.hasOutputPair)
            {
                // Output channels are always sequential starting from 0
                int leftCh = 0;
                int rightCh = 1;

                float* leftOut = bufferToFill.buffer->getWritePointer(leftCh, bufferToFill.startSample);
                float* rightOut = bufferToFill.buffer->getWritePointer(rightCh, bufferToFill.startSample);

                // Single impulse at max amplitude
                leftOut[0] = 1.0f;
                rightOut[0] = 1.0f;

                impulseSent = true;
                capturedSamplesSinceImpulse = 0;
                appState.latencyCaptureBuffer.clear();
                appState.latencyCaptureBuffer.setSize(2, static_cast<int>(appState.settings.sampleRate * 5), false, true);
            }
        }
        else
        {
            // Capture input and look for impulse return
            // IMPORTANT: inputBuffer contains only the ACTIVE channels in sequential order (0, 1, 2...)
            // NOT the physical channel numbers. If we enabled channels 3-4, they appear as indices 0-1.

            if (inputBuffer.getNumChannels() >= 2 && appState.hasInputPair)
            {
                // Input channels are always sequential starting from 0
                int leftInCh = 0;
                int rightInCh = 1;

                // Copy input to capture buffer
                int writePos = capturedSamplesSinceImpulse;
                int samplesToWrite = juce::jmin(numSamples, appState.latencyCaptureBuffer.getNumSamples() - writePos);

                if (samplesToWrite > 0)
                {
                    appState.latencyCaptureBuffer.copyFrom(0, writePos, inputBuffer, leftInCh, 0, samplesToWrite);
                    appState.latencyCaptureBuffer.copyFrom(1, writePos, inputBuffer, rightInCh, 0, samplesToWrite);
                }

                capturedSamplesSinceImpulse += numSamples;

                // Look for peak in the captured audio
                int peakPosition = findPeakPosition(appState.latencyCaptureBuffer, 0.5f);

                if (peakPosition >= 0)
                {
                    // Found the impulse return! Calculate latency
                    // peakPosition is in frames, convert to interleaved samples for consistency with Swift
                    int numChannels = appState.latencyCaptureBuffer.getNumChannels();
                    appState.settings.measuredLatencySamples = peakPosition * numChannels;
                    appState.settings.lastBufferSizeWhenMeasured = appState.settings.bufferSize;
                    needsToCompleteLatencyMeasurement = true;
                    appState.isMeasuringLatency = false;
                }
                else if (capturedSamplesSinceImpulse > appState.settings.sampleRate * 5)
                {
                    // Timeout - no peak found
                    appState.settings.measuredLatencySamples = -1;
                    needsToCompleteLatencyMeasurement = true;
                    appState.isMeasuringLatency = false;
                }
            }
            else
            {
                // Not enough input channels - fail immediately
                if (capturedSamplesSinceImpulse > numSamples * 10) // Give it a few buffers
                {
                    DBG("ERROR: No input channels available for latency measurement!");
                    appState.settings.measuredLatencySamples = -1;
                    needsToCompleteLatencyMeasurement = true;
                    appState.isMeasuringLatency = false;
                }
                capturedSamplesSinceImpulse += numSamples;
            }
        }
    }
    else if (appState.isTestingHardware)
    {
        // ================================================================
        // HARDWARE TEST MODE: Generate 1kHz sine wave
        // ================================================================
        // IMPORTANT: Output buffer contains only ACTIVE channels sequentially

        if (numChannels >= 2 && appState.hasOutputPair)
        {
            // Output channels are always sequential starting from 0
            int leftCh = 0;
            int rightCh = 1;

            const float amplitude = 0.3f;  // -10dB to be safe
            const float phaseIncrement = (sineFrequency * 2.0f * juce::MathConstants<float>::pi) /
                                         (float)appState.settings.sampleRate;

            float* leftData = bufferToFill.buffer->getWritePointer(leftCh, bufferToFill.startSample);
            float* rightData = bufferToFill.buffer->getWritePointer(rightCh, bufferToFill.startSample);

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
    else if (appState.isProcessing)
    {
        // ================================================================
        // PROCESSING MODE: Play file through output, record from input
        // Uses selected input AND output pairs with latency compensation
        // ================================================================

        // Check if we have enough input and output channels
        int numInputChannels = inputBuffer.getNumChannels();
        
        if (numChannels >= 2 && numInputChannels >= 2 && 
            appState.hasOutputPair && appState.hasInputPair)
        {
            // Output and input channels are sequential starting from 0
            int leftOutCh = 0;
            int rightOutCh = 1;
            int leftInCh = 0;
            int rightInCh = 1;

            // Check if we're in a gap between files
            if (isInProcessingGap)
            {
                // Play silence during gap
                int samplesToSilence = (int)juce::jmin((juce::int64)numSamples, processingGapSamplesRemaining);
                processingGapSamplesRemaining -= samplesToSilence;

                // Still record during gap (captures any residual tail)
                if (recordingSamplePosition < appState.recordingBuffer.getNumSamples())
                {
                    int samplesToRecord = juce::jmin(numSamples, 
                                                     appState.recordingBuffer.getNumSamples() - (int)recordingSamplePosition);
                    
                    if (samplesToRecord > 0 && inputBuffer.getNumChannels() >= 2)
                    {
                        appState.recordingBuffer.copyFrom(0, (int)recordingSamplePosition, 
                                                         inputBuffer, leftInCh, 0, samplesToRecord);
                        appState.recordingBuffer.copyFrom(1, (int)recordingSamplePosition, 
                                                         inputBuffer, rightInCh, 0, samplesToRecord);
                        recordingSamplePosition += samplesToRecord;
                    }
                }

                if (processingGapSamplesRemaining <= 0)
                {
                    // Gap finished - trigger save and load next file
                    isInProcessingGap = false;
                    needsToSaveCurrentFile = true;
                }
            }
            else
            {
                // Play current file AND record input simultaneously
                const int totalPlaybackSamples = appState.currentPlaybackBuffer.getNumSamples();
                
                // PLAYBACK: Send audio to output
                if (playbackSamplePosition < totalPlaybackSamples)
                {
                    int samplesToPlay = juce::jmin(numSamples, (int)(totalPlaybackSamples - playbackSamplePosition));

                    float* leftOut = bufferToFill.buffer->getWritePointer(leftOutCh, bufferToFill.startSample);
                    float* rightOut = bufferToFill.buffer->getWritePointer(rightOutCh, bufferToFill.startSample);

                    const float* leftSrc = appState.currentPlaybackBuffer.getReadPointer(0, (int)playbackSamplePosition);
                    const float* rightSrc = appState.currentPlaybackBuffer.getReadPointer(1, (int)playbackSamplePosition);

                    for (int i = 0; i < samplesToPlay; ++i)
                    {
                        leftOut[i] = leftSrc[i];
                        rightOut[i] = rightSrc[i];
                    }

                    playbackSamplePosition += samplesToPlay;
                }

                // RECORDING: Capture input
                if (recordingSamplePosition < appState.recordingBuffer.getNumSamples())
                {
                    int samplesToRecord = juce::jmin(numSamples, 
                                                     appState.recordingBuffer.getNumSamples() - (int)recordingSamplePosition);
                    
                    if (samplesToRecord > 0 && inputBuffer.getNumChannels() >= 2)
                    {
                        appState.recordingBuffer.copyFrom(0, (int)recordingSamplePosition, 
                                                         inputBuffer, leftInCh, 0, samplesToRecord);
                        appState.recordingBuffer.copyFrom(1, (int)recordingSamplePosition, 
                                                         inputBuffer, rightInCh, 0, samplesToRecord);
                        recordingSamplePosition += samplesToRecord;
                    }
                }

                // Check if we've captured enough (reached target including latency buffer)
                if (recordingSamplePosition >= targetRecordingSamples)
                {
                    // File finished - start gap before saving
                    playbackSamplePosition = 0;
                    recordingSamplePosition = 0;
                    isInProcessingGap = true;
                    
                    // Calculate gap in samples
                    processingGapSamplesRemaining = (juce::int64)((appState.settings.silenceBetweenFilesMs / 1000.0) * 
                                                                  appState.settings.sampleRate);
                    
                    // If no gap, save immediately
                    if (processingGapSamplesRemaining <= 0)
                    {
                        isInProcessingGap = false;
                        needsToSaveCurrentFile = true;
                    }
                }
            }
        }
    }
    else if (appState.isPreviewing)
    {
        // ================================================================
        // PREVIEW MODE: Play selected files (no recording)
        // Routes audio through selected OUTPUT channels only
        // ================================================================

        if (numChannels >= 2 && appState.hasOutputPair)
        {
            // Output channels are always sequential starting from 0
            int leftCh = 0;
            int rightCh = 1;

            // Check if we're in a gap between files
            if (isInPreviewGap)
            {
                // Play silence during gap
                int samplesToSilence = (int)juce::jmin((juce::int64)numSamples, previewGapSamplesRemaining);
                previewGapSamplesRemaining -= samplesToSilence;

                if (previewGapSamplesRemaining <= 0)
                {
                    // Gap finished - load next file
                    isInPreviewGap = false;
                    needsToLoadNextFile = true;
                }
                // Output already cleared, so we're outputting silence
            }
            else
            {
                // Play current file
                const int totalSamples = appState.currentPlaybackBuffer.getNumSamples();
                
                if (playbackSamplePosition < totalSamples)
                {
                    // Calculate how many samples we can play in this buffer
                    int samplesToPlay = juce::jmin(numSamples, (int)(totalSamples - playbackSamplePosition));

                    // Copy audio to output channels
                    float* leftOut = bufferToFill.buffer->getWritePointer(leftCh, bufferToFill.startSample);
                    float* rightOut = bufferToFill.buffer->getWritePointer(rightCh, bufferToFill.startSample);

                    const float* leftSrc = appState.currentPlaybackBuffer.getReadPointer(0, (int)playbackSamplePosition);
                    const float* rightSrc = appState.currentPlaybackBuffer.getReadPointer(1, (int)playbackSamplePosition);

                    // Copy samples
                    for (int i = 0; i < samplesToPlay; ++i)
                    {
                        leftOut[i] = leftSrc[i];
                        rightOut[i] = rightSrc[i];
                    }

                    playbackSamplePosition += samplesToPlay;

                    // Check if file finished
                    if (playbackSamplePosition >= totalSamples)
                    {
                        // File finished - start gap before next file
                        playbackSamplePosition = 0;
                        isInPreviewGap = true;
                        
                        // Calculate gap in samples
                        previewGapSamplesRemaining = (juce::int64)((appState.settings.silenceBetweenFilesMs / 1000.0) * 
                                                                   appState.settings.sampleRate);
                        
                        // If no gap, load next file immediately
                        if (previewGapSamplesRemaining <= 0)
                        {
                            isInPreviewGap = false;
                            needsToLoadNextFile = true;
                        }
                    }
                }
                else
                {
                    // Buffer position is invalid - stop preview
                    appState.isPreviewing = false;
                }
            }
        }
    }
}

//==============================================================================
// Timer Callback (Message Thread)

void MainComponent::timerCallback()
{
    // Handle latency measurement completion
    if (needsToCompleteLatencyMeasurement)
    {
        needsToCompleteLatencyMeasurement = false;

        if (appState.settings.measuredLatencySamples >= 0)
        {
            double latencyMs = appState.settings.getLatencyInMs();
            appState.appendLog("SUCCESS: Latency measurement complete!");
            appState.appendLog("  Measured latency: " + juce::String(appState.settings.measuredLatencySamples) +
                             " samples (" + juce::String(latencyMs, 2) + " ms)");
            appState.appendLog("  Audio loop detected and working correctly");

            // Also measure noise floor while we have captured audio
            float noiseFloorDb = calculateNoiseFloorDb(appState.latencyCaptureBuffer);
            appState.settings.measuredNoiseFloorDb = noiseFloorDb;
            appState.settings.hasNoiseFloorMeasurement = true;
            appState.appendLog("  Noise floor: " + juce::String(noiseFloorDb, 1) + " dB");
        }
        else
        {
            appState.appendLog("FAILED: Latency measurement - no audio loop detected");
            appState.appendLog("  Please check:");
            appState.appendLog("  1. Hardware loopback cable is connected");
            appState.appendLog("  2. Correct input/output pairs are selected");
            appState.appendLog("  3. Input monitoring is enabled on your interface");
        }

        settingsComponent.updateFromState();
    }

    // Handle file saving (triggered by audio thread)
    if (needsToSaveCurrentFile)
    {
        needsToSaveCurrentFile = false;
        
        // Save the current recording
        saveCurrentRecording();

        // Move to next file or finish
        appState.currentFileIndex++;
        
        // Update progress
        if (appState.files.size() > 0)
        {
            appState.processingProgress = (double)appState.currentFileIndex / appState.files.size();
        }

        if (appState.currentFileIndex < appState.files.size())
        {
            // Try to load next file
            bool loadedSuccessfully = loadNextFileForProcessing();
            
            if (loadedSuccessfully)
            {
                // Reset for next file
                playbackSamplePosition = 0;
                recordingSamplePosition = 0;
                isInProcessingGap = false;
                processingGapSamplesRemaining = 0;
                appState.isProcessing = true;
            }
            else
            {
                // File failed to load - skip it and try next one
                appState.appendLog("Skipping to next file...");
                needsToSaveCurrentFile = true;  // Trigger loading of next file
            }
        }
        else
        {
            // All files processed
            appState.isProcessing = false;
            appState.processingProgress = 1.0;
            
            // Count successes and failures
            int completed = 0, failed = 0;
            for (const auto& file : appState.files)
            {
                if (file.status == ProcessingStatus::completed)
                    completed++;
                else if (file.status == ProcessingStatus::failed)
                    failed++;
            }
            
            appState.appendLog("================================");
            appState.appendLog("Batch processing COMPLETE");
            appState.appendLog("  Successful: " + juce::String(completed) + " file(s)");
            if (failed > 0)
                appState.appendLog("  Failed: " + juce::String(failed) + " file(s)");
            appState.appendLog("================================");
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
            if (loadNextFileForPreview())
            {
                playbackSamplePosition = 0;
                isInPreviewGap = false;  // Reset gap flag
                appState.isPreviewing = true;
            }
            else
            {
                // Failed to load - skip to next
                needsToLoadNextFile = true;
            }
        }
        else
        {
            // All files played - loop back to start (Round Robin)
            appState.currentPreviewFileIndex = -1;  // Will be incremented to 0
            needsToLoadNextFile = true;  // Load first file again
            appState.appendLog("Preview looping...");
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
        appState.appendLog("Enabled input channels: " + juce::String(appState.selectedInputPair.leftChannel) +
                         ", " + juce::String(appState.selectedInputPair.rightChannel));
    }

    if (appState.hasOutputPair)
    {
        setStereoBits(setup.outputChannels, appState.selectedOutputPair);
        appState.appendLog("Enabled output channels: " + juce::String(appState.selectedOutputPair.leftChannel) +
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

    // IMPORTANT: Re-register our custom callback after device reconfiguration
    // The device reconfiguration may have cleared callbacks
    deviceManager.removeAudioCallback(&customCallback);
    deviceManager.addAudioCallback(&customCallback);

    appState.appendLog("Device configured successfully");
}

//==============================================================================
// File Management

void MainComponent::addFiles(const juce::Array<juce::File>& files)
{
    if (files.isEmpty())
        return;

    int validCount = 0;
    int invalidCount = 0;

    for (const auto& file : files)
    {
        AudioFile audioFile(file);
        appState.files.add(audioFile);

        if (audioFile.isValid())
        {
            appState.appendLog("Added: " + audioFile.getFileName() +
                             " (" + juce::String(audioFile.sampleRate / 1000.0, 1) + " kHz)");
            validCount++;
        }
        else
        {
            appState.appendLog("Warning: Invalid sample rate - " + audioFile.getFileName() +
                             " (" + juce::String(audioFile.sampleRate / 1000.0, 1) + " kHz, expected 44.1 kHz)");
            invalidCount++;
        }
    }

    // Summary log
    juce::String summary = "Loaded " + juce::String(files.size()) + " file(s)";
    if (validCount > 0)
        summary += " (" + juce::String(validCount) + " valid";
    if (invalidCount > 0)
        summary += ", " + juce::String(invalidCount) + " invalid sample rate";
    if (validCount > 0)
        summary += ")";

    appState.appendLog(summary);

    // Update UI
    fileListAndLogComponent.updateFromState();
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
    // Validation checks
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

    // Validate output folder
    if (!validateOutputFolder())
    {
        return;  // Error message logged in validateOutputFolder()
    }

    // Initialize processing state
    appState.currentFileIndex = 0;
    appState.isProcessing = false; // Will be set to true after first file loads
    appState.processingProgress = 0.0;
    isInProcessingGap = false;
    processingGapSamplesRemaining = 0;

    // Load first file and start processing
    if (loadNextFileForProcessing())
    {
        playbackSamplePosition = 0;
        recordingSamplePosition = 0;
        appState.recordingBuffer.clear();
        appState.isProcessing = true;

        appState.appendLog("Starting batch processing of " +
                         juce::String(appState.files.size()) + " file(s)");
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
    // Check if we have output configured
    if (!appState.hasOutputPair)
    {
        appState.appendLog("Error: Please select an output device first");
        return;
    }

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

    // Initialize preview state
    appState.currentPreviewFileIndex = -1;  // Will be incremented to 0 in timerCallback
    playbackSamplePosition = 0;
    isInPreviewGap = false;
    previewGapSamplesRemaining = 0;
    
    // Trigger first file load on message thread
    needsToLoadNextFile = true;
    
    appState.appendLog("Preview started with " + juce::String(appState.previewPlaylist.size()) + " file(s)");
}

void MainComponent::stopPreview()
{
    appState.isPreviewing = false;
    appState.previewPlaylist.clear();
    appState.currentPreviewFileIndex = -1;
    playbackSamplePosition = 0;
    isInPreviewGap = false;
    previewGapSamplesRemaining = 0;
    needsToLoadNextFile = false;  // Cancel any pending file loads
    appState.appendLog("Preview stopped");
    
    // Update UI to reflect stopped state
    fileListAndLogComponent.updateFromState();
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
        file.status = ProcessingStatus::failed;
        return false;
    }

    auto* reader = formatManager.createReaderFor(file.url);
    if (reader == nullptr)
    {
        appState.appendLog("Error: Could not read file - " + file.getFileName());
        file.status = ProcessingStatus::failed;
        return false;
    }

    // Get source file length
    int sourceFrames = (int)reader->lengthInSamples;
    
    // Load file into playback buffer (stereo)
    appState.currentPlaybackBuffer.setSize(2, sourceFrames);
    appState.currentPlaybackBuffer.clear();
    reader->read(&appState.currentPlaybackBuffer, 0, sourceFrames, 0, true, true);
    delete reader;

    // Calculate recording buffer size with latency compensation
    // measuredLatencySamples is in interleaved samples, convert to frames
    int inputChannelCount = 2;  // We're always recording stereo
    int latencyFrames = appState.settings.measuredLatencySamples / inputChannelCount;
    
    // Formula: sourceFrames + latencyFrames + (latencyFrames * 4) for safety buffer
    int recordingFrames = sourceFrames + latencyFrames + (latencyFrames * 4);
    
    // Allocate recording buffer
    appState.recordingBuffer.setSize(inputChannelCount, recordingFrames);
    appState.recordingBuffer.clear();
    
    // Set target recording length in frames
    targetRecordingSamples = recordingFrames;
    
    file.status = ProcessingStatus::processing;
    appState.currentProcessingFile = file.getFileName();
    appState.appendLog("Processing: " + file.getFileName());
    appState.appendLog("  Source: " + juce::String(sourceFrames) + " frames");
    appState.appendLog("  Latency: " + juce::String(latencyFrames) + " frames (" + 
                      juce::String(appState.settings.measuredLatencySamples) + " interleaved samples)");
    appState.appendLog("  Recording: " + juce::String(recordingFrames) + " frames (includes latency + safety buffer)");

    return true;
}

bool MainComponent::loadNextFileForPreview()
{
    // Get the file ID from the preview playlist
    if (!juce::isPositiveAndBelow(appState.currentPreviewFileIndex, appState.previewPlaylist.size()))
        return false;

    juce::String fileID = appState.previewPlaylist[appState.currentPreviewFileIndex];

    // Find the file by ID in the main file list
    AudioFile* fileToPreview = nullptr;
    for (auto& file : appState.files)
    {
        if (file.id == fileID)
        {
            fileToPreview = &file;
            break;
        }
    }

    if (fileToPreview == nullptr)
    {
        appState.appendLog("Error: Preview file not found - " + fileID);
        return false;
    }

    if (!fileToPreview->isValid())
    {
        appState.appendLog("Skipping invalid preview file: " + fileToPreview->getFileName());
        return false;
    }

    // Load the audio file
    auto* reader = formatManager.createReaderFor(fileToPreview->url);
    if (reader == nullptr)
    {
        appState.appendLog("Error: Could not read preview file - " + fileToPreview->getFileName());
        return false;
    }

    // Set playback buffer to stereo and the correct length
    appState.currentPlaybackBuffer.setSize(2, (int)reader->lengthInSamples);
    appState.currentPlaybackBuffer.clear();

    // Read the file into the stereo buffer
    // If the file is mono, reader->read() will correctly copy it to both L/R channels
    // If the file is stereo, it will copy L->L and R->R
    reader->read(&appState.currentPlaybackBuffer,
                 0,
                 (int)reader->lengthInSamples,
                 0,
                 true,  // use L channel
                 true); // use R channel

    delete reader;

    appState.appendLog("Preview: " + fileToPreview->getFileName());

    return true;
}

bool MainComponent::validateOutputFolder()
{
    juce::File outputFolder(appState.settings.outputFolderPath);
    
    // Check if output folder exists
    if (!outputFolder.exists())
    {
        appState.appendLog("Error: Output folder does not exist: " + outputFolder.getFullPathName());
        return false;
    }
    
    // Check if output folder is writable
    if (!outputFolder.hasWriteAccess())
    {
        appState.appendLog("Error: No write access to output folder: " + outputFolder.getFullPathName());
        return false;
    }
    
    // Check if any source file is in the same folder as output folder
    for (const auto& file : appState.files)
    {
        if (file.url.getParentDirectory().getFullPathName() == outputFolder.getFullPathName())
        {
            appState.appendLog("ERROR: Output folder is same as source file folder!");
            appState.appendLog("  This could overwrite your source files.");
            appState.appendLog("  Please select a different output folder.");
            return false;
        }
    }
    
    return true;
}

void MainComponent::saveCurrentRecording()
{
    if (appState.currentFileIndex >= appState.files.size())
        return;

    AudioFile& sourceFile = appState.files.getReference(appState.currentFileIndex);

    // Get original source length in frames
    int originalLength = appState.currentPlaybackBuffer.getNumSamples();
    
    // Log the trimming parameters for debugging
    appState.appendLog("  Trimming: RecordedFrames=" + juce::String(appState.recordingBuffer.getNumSamples()) +
                      ", LatencyInterleaved=" + juce::String(appState.settings.measuredLatencySamples) +
                      ", OriginalFrames=" + juce::String(originalLength));
    
    // Check recording buffer before trimming
    float recordingMaxLevel = 0.0f;
    for (int ch = 0; ch < appState.recordingBuffer.getNumChannels(); ++ch)
    {
        const float* data = appState.recordingBuffer.getReadPointer(ch);
        for (int i = 0; i < appState.recordingBuffer.getNumSamples(); ++i)
        {
            recordingMaxLevel = juce::jmax(recordingMaxLevel, std::abs(data[i]));
        }
    }
    DBG("Recording buffer max level: " << recordingMaxLevel);
    
    // Trim latency from recording (measuredLatencySamples is in interleaved samples)
    juce::AudioBuffer<float> trimmed = trimLatency(
        appState.recordingBuffer,
        appState.settings.measuredLatencySamples,
        originalLength
    );
    
    // Check trimmed buffer
    float trimmedMaxLevel = 0.0f;
    for (int ch = 0; ch < trimmed.getNumChannels(); ++ch)
    {
        const float* data = trimmed.getReadPointer(ch);
        for (int i = 0; i < trimmed.getNumSamples(); ++i)
        {
            trimmedMaxLevel = juce::jmax(trimmedMaxLevel, std::abs(data[i]));
        }
    }
    DBG("Trimmed buffer max level: " << trimmedMaxLevel);

    // Apply DC removal if enabled (commented out for now per user request #2)
    // if (appState.settings.dcRemovalEnabled)
    // {
    //     removeDCOffset(trimmed);
    // }

    // Generate output file path
    juce::File outputFile = generateOutputFile(sourceFile);

    // Check if file already exists
    if (outputFile.exists())
    {
        appState.appendLog("Warning: Overwriting existing file - " + outputFile.getFileName());
    }

    // Write file
    std::unique_ptr<juce::OutputStream> fileStream(outputFile.createOutputStream());

    if (fileStream == nullptr)
    {
        sourceFile.status = ProcessingStatus::failed;
        appState.appendLog("ERROR: Could not create output file - " + outputFile.getFileName());
        return;
    }

    // Write as 24-bit WAV using JUCE 8 API
    juce::WavAudioFormat wavFormat;
    
    juce::AudioFormatWriter* rawWriter = wavFormat.createWriterFor(
        fileStream.release(),  // OutputStream* - writer takes ownership
        appState.settings.sampleRate,
        (unsigned int)trimmed.getNumChannels(),
        24,  // bits per sample
        {},  // metadata StringPairArray
        0    // quality hint
    );
    
    if (rawWriter == nullptr)
    {
        sourceFile.status = ProcessingStatus::failed;
        appState.appendLog("ERROR: Could not initialize WAV writer for - " + outputFile.getFileName());
        return;
    }
    
    std::unique_ptr<juce::AudioFormatWriter> writer(rawWriter);

    // Write audio data
    if (!writer->writeFromAudioSampleBuffer(trimmed, 0, trimmed.getNumSamples()))
    {
        sourceFile.status = ProcessingStatus::failed;
        appState.appendLog("ERROR: Failed to write audio data - " + outputFile.getFileName());
        return;
    }

    writer.reset(); // Flush and close

    sourceFile.status = ProcessingStatus::completed;
    appState.appendLog("Saved: " + outputFile.getFileName() + 
                      " (" + juce::String(trimmed.getNumSamples()) + " samples)");
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
    
    // Debug: Log the trimming calculation
    DBG("trimLatency: latencySamples=" << latencySamples << 
        ", latencyFrames=" << latencyFrames <<
        ", capturedFrames=" << capturedFrames << 
        ", startFrame=" << startFrame <<
        ", originalLength=" << originalLength <<
        ", framesToCopy=" << framesToCopy);

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
