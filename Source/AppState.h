#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
 * Buffer size options for audio processing
 * Maps directly to Swift's BufferSize enum
 */
enum class BufferSize : int
{
    samples128 = 128,
    samples256 = 256,
    samples512 = 512,
    samples1024 = 1024
};

//==============================================================================
/**
 * Processing status for individual audio files
 * Maps directly to Swift's AudioFile.ProcessingStatus enum
 */
enum class ProcessingStatus
{
    pending,
    processing,
    completed,
    failed,
    invalidSampleRate
};

//==============================================================================
/**
 * Represents an audio device (hardware interface)
 * Port of Swift's AudioDevice struct
 */
struct AudioDevice
{
    juce::String name;
    int inputChannelCount = 0;
    int outputChannelCount = 0;
    juce::String uniqueID;        // Real identifier from getIdentifierString()
    juce::String deviceTypeName;  // Type name (e.g., "CoreAudio", "ASIO")

    /** Returns true if this is a built-in Apple device */
    bool isBuiltIn() const
    {
        const juce::StringArray builtInKeywords = {
            "built-in", "internal", "macbook", "imac",
            "mac mini", "mac pro", "mac studio"
        };

        juce::String lowerName = name.toLowerCase();
        for (const auto& keyword : builtInKeywords)
        {
            if (lowerName.contains(keyword))
                return true;
        }
        return false;
    }

    bool operator==(const AudioDevice& other) const
    {
        return uniqueID == other.uniqueID && deviceTypeName == other.deviceTypeName;
    }
};

//==============================================================================
/**
 * Represents a stereo pair of channels on a device
 * Port of Swift's StereoPair struct
 */
struct StereoPair
{
    juce::String id;
    int leftChannel = 0;
    int rightChannel = 0;
    AudioDevice device;

    StereoPair() = default;

    StereoPair(int left, int right, const AudioDevice& dev)
        : leftChannel(left), rightChannel(right), device(dev)
    {
        id = device.uniqueID + "-" + juce::String(leftChannel) + "-" + juce::String(rightChannel);
    }

    juce::String getDisplayName() const
    {
        return device.name + " - Channels " + juce::String(leftChannel) + "-" + juce::String(rightChannel);
    }

    juce::String getDeviceUID() const
    {
        return device.uniqueID;
    }

    juce::Array<int> getChannels() const
    {
        return { leftChannel, rightChannel };
    }

    bool operator==(const StereoPair& other) const
    {
        return id == other.id;
    }
};

//==============================================================================
/**
 * Represents an audio file to be processed
 * Port of Swift's AudioFile struct
 */
class AudioFile
{
public:
    AudioFile() : id(juce::Uuid().toString()) {}

    explicit AudioFile(const juce::File& file)
        : id(juce::Uuid().toString()), url(file)
    {
        loadMetadata();
    }

    juce::String id;
    juce::File url;
    ProcessingStatus status = ProcessingStatus::pending;
    bool isSelected = false;
    double sampleRate = 0.0;
    juce::int64 durationSamples = 0;

    juce::String getFileName() const
    {
        return url.getFileName();
    }

    /** Returns true if file sample rate matches target (44.1kHz) */
    bool isValid() const
    {
        return std::abs(sampleRate - 44100.0) < 1.0; // Allow small tolerance
    }

    /** Loads audio file metadata (sample rate, duration) */
    void loadMetadata()
    {
        if (!url.existsAsFile())
        {
            status = ProcessingStatus::failed;
            return;
        }

        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(url));

        if (reader == nullptr)
        {
            status = ProcessingStatus::failed;
            return;
        }

        sampleRate = reader->sampleRate;
        durationSamples = reader->lengthInSamples;

        if (!isValid())
        {
            status = ProcessingStatus::invalidSampleRate;
        }
    }

    bool operator==(const AudioFile& other) const
    {
        return id == other.id;
    }
};

//==============================================================================
/**
 * Global processing settings
 * Port of Swift's ProcessingSettings struct
 */
struct ProcessingSettings
{
    // MARK: - Global Audio Settings

    /** Sample rate for all audio operations (user-selectable, defaults to 44.1kHz) */
    double sampleRate = 44100.0;

    // MARK: - Processing Settings

    BufferSize bufferSize = BufferSize::samples256;
    int measuredLatencySamples = -1;  // -1 means not measured
    BufferSize lastBufferSizeWhenMeasured = BufferSize::samples256;
    float measuredNoiseFloorDb = 0.0f;
    bool hasNoiseFloorMeasurement = false;

    // Processing mode settings
    bool useReverbMode = false;  // Stop on noise floor instead of fixed length
    float noiseFloorMarginPercent = 10.0f;  // % above noise floor to stop recording
    int silenceBetweenFilesMs = 150;  // Gap between files in preview/processing
    float thresholdDb = -40.0f;

    // Output settings
    juce::String outputFolderPath;
    juce::String outputPostfix;  // Empty = same filename

    // Monitoring settings
    bool enableMonitoring = true;  // Monitor preview/process through main outputs
    juce::Array<int> monitoringChannels = { 1, 2 };  // Default to channels 1+2

    // Advanced settings (from Swift)
    int sendOutputBusRangeStart = 3;
    int sendOutputBusRangeEnd = 4;
    int returnInputBus = 3;
    bool blockStereoOut = true;
    bool trimEnabled = true;
    bool dcRemovalEnabled = true;
    int postPlaybackSafetyMs = 250;

    /** Returns true if latency needs to be re-measured (buffer size changed) */
    bool needsLatencyRemeasurement() const
    {
        if (measuredLatencySamples < 0)
            return true;  // Never measured

        return lastBufferSizeWhenMeasured != bufferSize;
    }

    /** Returns the measured latency in milliseconds */
    double getLatencyInMs() const
    {
        if (measuredLatencySamples < 0)
            return 0.0;

        return (static_cast<double>(measuredLatencySamples) / sampleRate) * 1000.0;
    }

    /** Returns the recording length in samples for a given source file length */
    int getRecordingLength(int sourceFileSamples, int latencySamples) const
    {
        return sourceFileSamples + latencySamples + (latencySamples * 4);
    }

    /** Returns the threshold in linear amplitude for the given dB value */
    float getThresholdLinear() const
    {
        return std::pow(10.0f, thresholdDb / 20.0f);
    }

    /** Returns the noise floor threshold for reverb mode (noise floor + margin) */
    float getNoiseFloorThresholdDb() const
    {
        if (!hasNoiseFloorMeasurement)
            return -80.0f;  // Fallback threshold

        return measuredNoiseFloorDb + (measuredNoiseFloorDb * noiseFloorMarginPercent / 100.0f);
    }
};

//==============================================================================
/**
 * Central application state container
 * Replaces Swift's MainViewModel data members
 */
struct AppState
{
    // Settings
    ProcessingSettings settings;

    // Device management
    juce::Array<AudioDevice> devices;
    juce::String selectedDeviceID;
    StereoPair selectedInputPair;
    StereoPair selectedOutputPair;
    bool hasInputPair = false;
    bool hasOutputPair = false;

    // File management
    juce::Array<AudioFile> files;
    int currentFileIndex = 0;

    // Operation flags
    bool isProcessing = false;
    bool isMeasuringLatency = false;
    bool isPreviewing = false;
    bool isTestingHardware = false;

    // Audio buffers (for processing)
    juce::AudioBuffer<float> currentPlaybackBuffer;
    juce::AudioBuffer<float> recordingBuffer;

    // Progress tracking
    double processingProgress = 0.0;
    juce::String currentProcessingFile;
    int currentPreviewFileIndex = -1;
    double previewProgress = 0.0;
    juce::Array<juce::String> previewPlaylist;

    // Logging
    juce::StringArray logLines;

    // Playback state (for getNextAudioBlock)
    juce::int64 playbackPosition = 0;
    juce::int64 recordingPosition = 0;
    bool shouldSaveFile = false;

    // Latency measurement state
    bool impulseNotYetSent = true;
    juce::AudioBuffer<float> latencyCaptureBuffer;
    int latencyPeakPosition = -1;

    // Hardware test state
    float hardwareTestPhase = 0.0f;

    /** Helper to get selected device */
    AudioDevice* getSelectedDevice()
    {
        for (auto& device : devices)
        {
            if (device.uniqueID == selectedDeviceID)
                return &device;
        }
        return nullptr;
    }

    /** Helper to get available input pairs from selected device */
    juce::Array<StereoPair> getAvailableInputPairs() const
    {
        juce::Array<StereoPair> pairs;

        for (const auto& device : devices)
        {
            if (device.uniqueID == selectedDeviceID)
            {
                int channelCount = device.inputChannelCount;
                if (channelCount >= 2)
                {
                    for (int start = 1; start <= channelCount - 1; start += 2)
                    {
                        pairs.add(StereoPair(start, start + 1, device));
                    }
                }
                break;
            }
        }

        return pairs;
    }

    /** Helper to get available output pairs from selected device */
    juce::Array<StereoPair> getAvailableOutputPairs() const
    {
        juce::Array<StereoPair> pairs;

        for (const auto& device : devices)
        {
            if (device.uniqueID == selectedDeviceID)
            {
                int channelCount = device.outputChannelCount;
                if (channelCount >= 2)
                {
                    for (int start = 1; start <= channelCount - 1; start += 2)
                    {
                        pairs.add(StereoPair(start, start + 1, device));
                    }
                }
                break;
            }
        }

        return pairs;
    }

    /** Returns true if both input and output pairs are selected */
    bool canMeasureLatency() const
    {
        return !selectedDeviceID.isEmpty() && hasInputPair && hasOutputPair;
    }

    /** Add a log message with timestamp */
    void appendLog(const juce::String& message)
    {
        juce::Time now = juce::Time::getCurrentTime();
        juce::String timestamp = now.formatted("[%Y-%m-%dT%H:%M:%S]");
        logLines.add(timestamp + " " + message);
    }
};
