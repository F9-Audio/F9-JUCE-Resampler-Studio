#pragma once

#include <JuceHeader.h>
#include "AppState.h"
#include "F9LookAndFeel.h"
#include "SettingsComponent.h"
#include "FileListAndLogComponent.h"

//==============================================================================
/**
 * Main Component - Audio Engine and UI Container
 *
 * This is the heart of the application. It:
 * - Inherits from AudioAppComponent to handle real-time audio I/O
 * - Inherits from Timer to handle post-processing tasks on the message thread
 * - Contains the state machine that routes audio based on AppState flags
 * - Replaces all Swift service classes (AudioProcessingService, LatencyMeasurementService, etc.)
 *
 * Port of Swift's MainViewModel + all Services combined
 */
class MainComponent : public juce::AudioAppComponent,
                      public juce::Timer
{
public:
    //==============================================================================
    MainComponent();
    ~MainComponent() override;

    //==============================================================================
    // AudioAppComponent overrides (Real-time audio thread)

    /** Called before audio processing starts */
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;

    /** Called when audio processing stops */
    void releaseResources() override;

    /**
     * Real-time audio callback - THE CORE STATE MACHINE
     * Routes audio processing based on AppState flags
     * CRITICAL: Must be fast and lock-free!
     *
     * NOTE: We access the AudioDeviceManager directly to get separate input buffers
     */
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;

    //==============================================================================
    // Timer override (Message thread - safe for UI updates and file I/O)

    /**
     * Called periodically on message thread
     * Used for post-processing tasks like saving files, updating UI, etc.
     */
    void timerCallback() override;

    //==============================================================================
    // Component overrides (UI)

    void paint (juce::Graphics&) override;
    void resized() override;

    //==============================================================================
    // Public API - Device Management

    /** Refresh list of available audio devices */
    void refreshDevices();

    /** Select device by unique ID */
    void selectDevice(const juce::String& deviceID);

    /** Select input stereo pair */
    void selectInputPair(const StereoPair& pair);

    /** Select output stereo pair */
    void selectOutputPair(const StereoPair& pair);

    //==============================================================================
    // Public API - File Management

    /** Add files to processing queue */
    void addFiles(const juce::Array<juce::File>& files);

    /** Clear all files from queue */
    void clearFiles();

    /** Toggle file selection */
    void toggleFileSelection(int fileIndex);

    //==============================================================================
    // Public API - Operations

    /** Start processing all files */
    void startProcessing();

    /** Stop any active operation */
    void stopAllAudio();

    /** Measure latency and noise floor */
    void startLatencyMeasurement();

    /** Start preview of selected files */
    void startPreview();

    /** Stop preview */
    void stopPreview();

    /** Start hardware loop test (1kHz sine wave) */
    void startHardwareTest();

    /** Stop hardware loop test */
    void stopHardwareTest();

    //==============================================================================
    // Public API - State Access

    /** Get reference to application state (for UI binding) */
    AppState& getAppState() { return appState; }
    const AppState& getAppState() const { return appState; }

private:
    //==============================================================================
    // Core State

    AppState appState;

    // NOTE: AudioAppComponent provides its own deviceManager member
    // We access it via this->deviceManager (inherited from AudioAppComponent)

    // Format manager for reading/writing audio files
    juce::AudioFormatManager formatManager;

    // UI Components
    F9LookAndFeel lookAndFeel;
    SettingsComponent settingsComponent;
    FileListAndLogComponent fileListAndLogComponent;

    //==============================================================================
    // Processing State (for getNextAudioBlock)

    // Playback state
    juce::int64 playbackSamplePosition = 0;
    juce::int64 recordingSamplePosition = 0;

    // Latency measurement state
    bool impulseSent = false;
    int capturedSamplesSinceImpulse = 0;

    // Reverb mode state
    int consecutiveSilentBuffers = 0;
    int requiredConsecutiveSilentBuffers = 3;

    // Hardware test state
    float sinePhase = 0.0f;
    float sineFrequency = 1000.0f; // 1kHz test tone

    // File saving state (triggered by audio thread, processed by timer)
    bool needsToSaveCurrentFile = false;
    bool needsToLoadNextFile = false;
    bool needsToCompleteLatencyMeasurement = false;

    // Input buffer for capturing hardware inputs
    juce::AudioBuffer<float> inputBuffer;

    //==============================================================================
    // Helper Methods - Device Management

    /** Populate device list from AudioDeviceManager */
    void populateDeviceList();

    /** Configure audio device settings */
    void configureAudioDevice();

    //==============================================================================
    // Helper Methods - File Processing

    /** Load next file from queue into playback buffer */
    bool loadNextFileForProcessing();

    /** Save the current recording buffer to file */
    void saveCurrentRecording();

    /** Generate output filename with postfix */
    juce::File generateOutputFile(const AudioFile& sourceFile);

    //==============================================================================
    // Helper Methods - Critical Audio Algorithms

    /**
     * Trim latency from captured audio (CRITICAL - must be exact!)
     * See: LATENCY_TRIMMING_FIX.md
     *
     * @param captured The recorded audio buffer (includes latency at beginning)
     * @param latencySamples Number of samples to skip (interleaved)
     * @param originalLength Original source file length in frames
     * @return Trimmed audio buffer matching source length
     */
    juce::AudioBuffer<float> trimLatency(
        const juce::AudioBuffer<float>& captured,
        int latencySamples,
        int originalLength
    );

    /**
     * Check if reverb tail has fallen below noise floor
     * See: REVERB_MODE_IMPLEMENTATION.md
     *
     * @param audioWindow The audio window to analyze (e.g., last 2048 samples)
     * @return true if window is below noise floor threshold
     */
    bool isReverbTailBelowNoiseFloor(const juce::AudioBuffer<float>& audioWindow);

    /**
     * Apply DC offset removal to audio buffer
     * Removes any DC bias from the signal
     */
    void removeDCOffset(juce::AudioBuffer<float>& buffer);

    //==============================================================================
    // Helper Methods - Signal Generation

    /** Generate sine wave for hardware loop testing */
    void generateSineWave(juce::AudioBuffer<float>& buffer, int numSamples);

    /** Generate impulse for latency measurement */
    void generateImpulse(juce::AudioBuffer<float>& buffer);

    //==============================================================================
    // Helper Methods - Analysis

    /** Find peak position in captured audio (for latency detection) */
    int findPeakPosition(const juce::AudioBuffer<float>& buffer, float threshold);

    /** Calculate noise floor in dB */
    float calculateNoiseFloorDb(const juce::AudioBuffer<float>& buffer);

    /** Calculate RMS level of audio buffer */
    float calculateRMS(const juce::AudioBuffer<float>& buffer);

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
