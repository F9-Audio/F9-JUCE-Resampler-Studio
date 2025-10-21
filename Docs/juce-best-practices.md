# JUCE Best Practices for Professional Multichannel Audio Applications
## Hardware Resampler Standalone Application

**Document Version:** 1.0  
**Target JUCE Version:** 8.x (Latest Stable)  
**Application Type:** Standalone Cross-Platform (macOS/Windows)  
**Purpose:** Batch hardware audio resampling with multichannel interface support

---

## Table of Contents

1. [Project Architecture & Structure](#project-architecture--structure)
2. [Audio Device Management](#audio-device-management)
3. [Real-Time Audio Thread Best Practices](#real-time-audio-thread-best-practices)
4. [Multichannel Audio Processing](#multichannel-audio-processing)
5. [Thread Communication Patterns](#thread-communication-patterns)
6. [File Operations & Audio Format Management](#file-operations--audio-format-management)
7. [Memory Management & Smart Pointers](#memory-management--smart-pointers)
8. [Error Handling & Robustness](#error-handling--robustness)
9. [Modern JUCE 8 API Usage](#modern-juce-8-api-usage)
10. [Cross-Platform Considerations](#cross-platform-considerations)
11. [Performance Optimization](#performance-optimization)
12. [Real-World Examples & References](#real-world-examples--references)

---

## 1. Project Architecture & Structure

### Recommended Project Organization

```
HardwareResampler/
├── Source/
│   ├── Main.cpp                          // JUCE application entry point
│   ├── MainComponent.h/cpp               // AudioAppComponent + audio engine
│   ├── Audio/
│   │   ├── AudioEngine.h/cpp             // Core audio processing logic
│   │   ├── AudioDeviceConfiguration.h/cpp // Device setup and channel routing
│   │   ├── LatencyMeasurement.h/cpp      // Latency detection system
│   │   └── FileProcessor.h/cpp           // Audio file batch processing
│   ├── State/
│   │   ├── ApplicationState.h/cpp        // Central state management
│   │   └── Settings.h/cpp                // User settings and configuration
│   ├── UI/
│   │   ├── DeviceSelectorPanel.h/cpp     // Audio interface selection
│   │   ├── FileListPanel.h/cpp           // File queue management UI
│   │   ├── ControlPanel.h/cpp            // Processing controls
│   │   └── ProgressDisplay.h/cpp         // Progress and logging
│   └── Utilities/
│       ├── ThreadSafeQueue.h             // Lock-free FIFO implementation
│       └── AudioBufferHelpers.h          // Buffer manipulation utilities
├── Resources/                             // Images, icons, fonts
└── HardwareResampler.jucer               // Projucer project file
```

### Use AudioAppComponent as Foundation

**AudioAppComponent** is the recommended base class for standalone audio applications as it provides:
- Built-in `AudioDeviceManager` (accessible via `deviceManager`)
- Automatic audio I/O lifecycle management
- Integration with `AudioSource` pattern

```cpp
class MainComponent : public juce::AudioAppComponent,
                      public juce::Timer
{
public:
    MainComponent()
    {
        // Set default audio channels (can be changed later)
        setAudioChannels(2, 2);  // 2 inputs, 2 outputs initially
        
        // Start timer for UI updates (NOT for audio processing)
        startTimer(50); // 20Hz UI refresh rate
    }
    
    ~MainComponent() override
    {
        shutdownAudio();
    }
    
    // Audio callbacks
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;
    
    // UI updates
    void timerCallback() override;
    
private:
    ApplicationState appState;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
```

**Key Point:** `AudioAppComponent` already contains an `AudioDeviceManager` member called `deviceManager`. **Never create your own separate instance** - always use the inherited one.

---

## 2. Audio Device Management

### Configuring Multichannel Interfaces

```cpp
void MainComponent::configureAudioDevice()
{
    // Get current audio device setup
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup(setup);
    
    // Select specific device by name
    setup.outputDeviceName = "RME Fireface UCX";  // Example
    setup.inputDeviceName = "RME Fireface UCX";
    
    // Set sample rate and buffer size
    setup.sampleRate = 48000.0;
    setup.bufferSize = 512;
    
    // Enable specific channels (BigInteger is channel bitset)
    // For channels 3-4 output (indices 2-3):
    setup.outputChannels.clear();
    setup.outputChannels.setBit(2, true);  // Output channel 3
    setup.outputChannels.setBit(3, true);  // Output channel 4
    
    // For channels 3-4 input:
    setup.inputChannels.clear();
    setup.inputChannels.setBit(2, true);   // Input channel 3
    setup.inputChannels.setBit(3, true);   // Input channel 4
    
    // Apply configuration
    juce::String error = deviceManager.setAudioDeviceSetup(setup, true);
    
    if (error.isNotEmpty())
    {
        // Handle error - device not available, channels not supported, etc.
        DBG("Audio device setup failed: " + error);
        showErrorDialog(error);
    }
}
```

### Dynamic Channel Configuration

```cpp
void MainComponent::setChannelPair(int stereoChannelPairIndex)
{
    // User selects "Output Pair 2" (channels 3-4)
    int outputStartChannel = stereoChannelPairIndex * 2;
    
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup(setup);
    
    // Clear and set only the selected stereo pair
    setup.outputChannels.clear();
    setup.outputChannels.setBit(outputStartChannel, true);
    setup.outputChannels.setBit(outputStartChannel + 1, true);
    
    setup.inputChannels.clear();
    setup.inputChannels.setBit(outputStartChannel, true);     // Match input to output
    setup.inputChannels.setBit(outputStartChannel + 1, true);
    
    deviceManager.setAudioDeviceSetup(setup, true);
    
    // Must call setAudioChannels after changing device setup
    shutdownAudio();
    setAudioChannels(2, 2);  // Logical 2-in/2-out (mapped to physical channels)
}
```

### Device Enumeration for UI

```cpp
juce::StringArray getAvailableOutputChannelPairs()
{
    juce::StringArray pairs;
    
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return pairs;
    
    auto outputChannelNames = device->getOutputChannelNames();
    
    // Create stereo pairs (skip monitoring outputs 1-2)
    for (int i = 2; i < outputChannelNames.size(); i += 2)
    {
        if (i + 1 < outputChannelNames.size())
        {
            juce::String pairName = outputChannelNames[i] + " / " + 
                                   outputChannelNames[i + 1];
            pairs.add(pairName);
        }
    }
    
    return pairs;
}
```

---

## 3. Real-Time Audio Thread Best Practices

### The Golden Rules of Real-Time Audio

#### ❌ **NEVER DO in getNextAudioBlock():**
1. **Memory allocation** - `new`, `delete`, `malloc`, `std::vector::push_back()`, `juce::String` creation
2. **File I/O** - Reading/writing files, disk access
3. **Locks** - `std::mutex`, `juce::CriticalSection`, `ScopedLock`
4. **System calls** - Network operations, OS API calls
5. **Memory allocation hidden in JUCE classes:**
   - `String` operations (concatenation, substring)
   - `Array::add()`, `Array::remove()`
   - `HeapBlock` allocation
   - `AudioBuffer` resizing

#### ✅ **SAFE Operations in getNextAudioBlock():**
1. Reading/writing to pre-allocated buffers
2. Simple arithmetic and DSP operations
3. Reading `std::atomic` variables
4. Writing to `std::atomic` variables (relaxed ordering)
5. Lock-free FIFO operations (when properly implemented)
6. Copying fixed-size data structures

### Correct Audio Callback Pattern

```cpp
void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    // ALWAYS clear the output buffer first
    bufferToFill.clearActiveBufferRegion();
    
    // State machine based on atomic flags
    if (appState.isProcessing.load(std::memory_order_relaxed))
    {
        processPlaybackAndRecording(bufferToFill);
    }
    else if (appState.isMeasuringLatency.load(std::memory_order_relaxed))
    {
        performLatencyMeasurement(bufferToFill);
    }
    else if (appState.isPreviewing.load(std::memory_order_relaxed))
    {
        performPreview(bufferToFill);
    }
    // else: output remains silent (already cleared)
}

void MainComponent::processPlaybackAndRecording(
    const juce::AudioSourceChannelInfo& bufferToFill)
{
    const int numSamples = bufferToFill.numSamples;
    const int numChannels = bufferToFill.buffer->getNumChannels();
    
    // Read from pre-loaded playback buffer
    int samplesToPlay = juce::jmin(numSamples, 
                                   playbackBuffer.getNumSamples() - playbackPosition);
    
    if (samplesToPlay > 0)
    {
        for (int channel = 0; channel < numChannels; ++channel)
        {
            bufferToFill.buffer->copyFrom(
                channel,                      // dest channel
                bufferToFill.startSample,     // dest start
                playbackBuffer,               // source buffer
                channel,                      // source channel
                playbackPosition,             // source start
                samplesToPlay                 // num samples
            );
        }
        
        playbackPosition += samplesToPlay;
    }
    
    // Record input (from hardware returning through loop)
    if (recordingBuffer.getNumSamples() > 0)
    {
        const int writePosition = recordingSampleCount.load(std::memory_order_relaxed);
        
        // Ensure we don't exceed pre-allocated recording buffer
        if (writePosition + numSamples <= recordingBuffer.getNumSamples())
        {
            for (int channel = 0; channel < numChannels; ++channel)
            {
                recordingBuffer.copyFrom(
                    channel,
                    writePosition,
                    *bufferToFill.buffer,
                    channel,
                    bufferToFill.startSample,
                    numSamples
                );
            }
            
            recordingSampleCount.fetch_add(numSamples, std::memory_order_relaxed);
        }
    }
    
    // Check completion WITHOUT heavy processing
    if (playbackPosition >= playbackBuffer.getNumSamples())
    {
        // Set flag for Timer to handle file saving
        shouldSaveRecording.store(true, std::memory_order_release);
        appState.isProcessing.store(false, std::memory_order_release);
    }
}
```

---

## 4. Multichannel Audio Processing

### Working with AudioBuffer

```cpp
// Pre-allocation pattern (in prepareToPlay or constructor)
void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    // Store for use in audio callback
    expectedBlockSize = samplesPerBlockExpected;
    currentSampleRate = sampleRate;
    
    // Pre-allocate buffers with maximum expected size
    const int maxSeconds = 60;  // Maximum file length
    const int maxSamples = static_cast<int>(sampleRate * maxSeconds);
    
    recordingBuffer.setSize(
        2,              // channels (stereo)
        maxSamples,     // samples
        false,          // keepExistingContent
        true,           // clearExtraSpace
        true            // avoidReallocating
    );
    
    playbackBuffer.setSize(2, maxSamples, false, true, true);
    
    // Clear buffers
    recordingBuffer.clear();
    playbackBuffer.clear();
}
```

### Channel Routing and Mapping

```cpp
void processWithChannelOffset(juce::AudioBuffer<float>& buffer, 
                             int physicalChannelOffset)
{
    // When device is configured for channels 3-4 (offset = 2),
    // but JUCE gives us a 2-channel buffer (logical channels 0-1)
    // The mapping is automatic - JUCE handles the offset internally
    
    // Access logical channels directly
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        float* channelData = buffer.getWritePointer(channel);
        
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            // Process sample
            channelData[sample] *= 0.5f; // Example: reduce gain
        }
    }
    
    // JUCE AudioDeviceManager automatically routes:
    // logical channel 0 -> physical channel 3 (if offset=2)
    // logical channel 1 -> physical channel 4
}
```

### Efficient Buffer Operations

```cpp
// ✅ CORRECT: Use JUCE's optimized methods
void copyChannelData(const juce::AudioBuffer<float>& source,
                    juce::AudioBuffer<float>& dest,
                    int numSamples)
{
    for (int channel = 0; channel < source.getNumChannels(); ++channel)
    {
        dest.copyFrom(
            channel,        // dest channel
            0,              // dest start sample
            source,         // source buffer
            channel,        // source channel
            0,              // source start sample
            numSamples      // number of samples
        );
    }
}

// ❌ INCORRECT: Manual sample-by-sample (slower)
void copyChannelDataSlow(const juce::AudioBuffer<float>& source,
                        juce::AudioBuffer<float>& dest,
                        int numSamples)
{
    for (int channel = 0; channel < source.getNumChannels(); ++channel)
    {
        const float* sourceData = source.getReadPointer(channel);
        float* destData = dest.getWritePointer(channel);
        
        for (int i = 0; i < numSamples; ++i)
            destData[i] = sourceData[i];  // Use JUCE methods instead!
    }
}
```

### Latency Compensation

```cpp
juce::AudioBuffer<float> trimLatency(const juce::AudioBuffer<float>& captured,
                                    int latencySamples,
                                    int originalLength)
{
    // This is critical for sample-accurate hardware processing
    const int capturedLength = captured.getNumSamples();
    const int startPos = latencySamples;
    
    // Handle edge case: not enough samples captured
    int samplesToExtract = originalLength;
    if (startPos + samplesToExtract > capturedLength)
    {
        samplesToExtract = juce::jmax(0, capturedLength - startPos);
    }
    
    // Create trimmed buffer
    juce::AudioBuffer<float> trimmed(captured.getNumChannels(), originalLength);
    trimmed.clear();
    
    if (samplesToExtract > 0 && startPos >= 0)
    {
        for (int ch = 0; ch < captured.getNumChannels(); ++ch)
        {
            trimmed.copyFrom(
                ch,             // dest channel
                0,              // dest start
                captured,       // source buffer
                ch,             // source channel
                startPos,       // source start (skip latency)
                samplesToExtract // samples to copy
            );
        }
    }
    
    return trimmed;
}
```

---

## 5. Thread Communication Patterns

### Pattern 1: Atomic Flags + Timer (Recommended for Simple State)

```cpp
class MainComponent : public juce::AudioAppComponent,
                      public juce::Timer
{
public:
    void startProcessing()
    {
        // Prepare data on message thread
        loadNextFile();
        
        // Signal audio thread
        isProcessing.store(true, std::memory_order_release);
        recordingSampleCount.store(0, std::memory_order_release);
        playbackPosition.store(0, std::memory_order_release);
    }
    
    void timerCallback() override
    {
        // Check flags set by audio thread
        if (shouldSaveRecording.load(std::memory_order_acquire))
        {
            shouldSaveRecording.store(false, std::memory_order_relaxed);
            
            // Safe to do file I/O here (message thread)
            saveRecordedAudio();
            
            // Update UI
            updateProgressBar();
            
            // Move to next file
            if (hasMoreFiles())
                startProcessing();
            else
                onProcessingComplete();
        }
    }
    
private:
    std::atomic<bool> isProcessing { false };
    std::atomic<bool> shouldSaveRecording { false };
    std::atomic<int> recordingSampleCount { 0 };
    std::atomic<int> playbackPosition { 0 };
};
```

### Pattern 2: Lock-Free FIFO (For Complex Data)

```cpp
class ThreadSafeAudioQueue
{
public:
    ThreadSafeAudioQueue(int numChannels, int numSamples)
        : abstractFifo(numBuffers)
    {
        for (auto& buffer : buffers)
            buffer.setSize(numChannels, numSamples);
    }
    
    // Called from audio thread
    bool push(const juce::AudioBuffer<float>& sourceBuffer)
    {
        int start1, size1, start2, size2;
        abstractFifo.prepareToWrite(1, start1, size1, start2, size2);
        
        if (size1 > 0)
        {
            // Copy to FIFO
            copyBuffer(sourceBuffer, buffers[start1]);
            abstractFifo.finishedWrite(size1);
            return true;
        }
        
        return false;  // Queue full
    }
    
    // Called from message thread
    bool pop(juce::AudioBuffer<float>& destBuffer)
    {
        int start1, size1, start2, size2;
        abstractFifo.prepareToRead(1, start1, size1, start2, size2);
        
        if (size1 > 0)
        {
            copyBuffer(buffers[start1], destBuffer);
            abstractFifo.finishedRead(size1);
            return true;
        }
        
        return false;  // Queue empty
    }
    
private:
    static constexpr int numBuffers = 4;
    juce::AbstractFifo abstractFifo;
    std::array<juce::AudioBuffer<float>, numBuffers> buffers;
    
    void copyBuffer(const juce::AudioBuffer<float>& src, 
                   juce::AudioBuffer<float>& dst)
    {
        const int numChannels = juce::jmin(src.getNumChannels(), 
                                          dst.getNumChannels());
        const int numSamples = juce::jmin(src.getNumSamples(), 
                                         dst.getNumSamples());
        
        for (int ch = 0; ch < numChannels; ++ch)
            dst.copyFrom(ch, 0, src, ch, 0, numSamples);
    }
};
```

### Pattern 3: AsyncUpdater (⚠️ Use With Caution)

**WARNING:** `AsyncUpdater` allocates memory and locks internally. **DO NOT trigger from audio thread** unless you accept potential glitches.

```cpp
// ❌ BAD: Called from audio thread
void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    // ... process audio ...
    
    // This can allocate and lock!
    triggerAsyncUpdate();  // DON'T DO THIS
}

// ✅ GOOD: Use atomic flag + timer instead
std::atomic<bool> needsUIUpdate { false };

void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    // ... process audio ...
    
    needsUIUpdate.store(true, std::memory_order_release);  // Safe
}

void timerCallback() override
{
    if (needsUIUpdate.load(std::memory_order_acquire))
    {
        needsUIUpdate.store(false, std::memory_order_relaxed);
        updateUI();
    }
}
```

---

## 6. File Operations & Audio Format Management

### AudioFormatManager Setup

```cpp
class FileProcessor
{
public:
    FileProcessor()
    {
        // Register common audio formats
        formatManager.registerBasicFormats();
        
        // Or register individually for specific formats only:
        // formatManager.registerFormat(new juce::WavAudioFormat(), true);
        // formatManager.registerFormat(new juce::AiffAudioFormat(), false);
    }
    
    bool loadAudioFile(const juce::File& file, 
                      juce::AudioBuffer<float>& buffer,
                      double& sampleRate)
    {
        std::unique_ptr<juce::AudioFormatReader> reader(
            formatManager.createReaderFor(file)
        );
        
        if (reader == nullptr)
        {
            DBG("Failed to create reader for: " + file.getFullPathName());
            return false;
        }
        
        // Get file info
        sampleRate = reader->sampleRate;
        const int numChannels = static_cast<int>(reader->numChannels);
        const int numSamples = static_cast<int>(reader->lengthInSamples);
        
        // Resize buffer (on message thread, not audio thread!)
        buffer.setSize(numChannels, numSamples);
        
        // Read entire file
        reader->read(&buffer, 0, numSamples, 0, true, true);
        
        return true;
    }
    
private:
    juce::AudioFormatManager formatManager;
};
```

### Writing Audio Files (Modern JUCE 8 API)

```cpp
bool saveAudioFile(const juce::File& outputFile,
                  const juce::AudioBuffer<float>& buffer,
                  double sampleRate,
                  int bitDepth = 24)
{
    // Delete existing file
    if (outputFile.exists())
        outputFile.deleteFile();
    
    // Create format writer
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::FileOutputStream> fileStream(
        outputFile.createOutputStream()
    );
    
    if (fileStream == nullptr)
    {
        DBG("Failed to create output stream for: " + outputFile.getFullPathName());
        return false;
    }
    
    // JUCE 8: Use Options struct (NOT deprecated constructors)
    juce::AudioFormatWriter::Options options;
    options.sampleRate = sampleRate;
    options.numChannels = static_cast<unsigned int>(buffer.getNumChannels());
    options.bitDepth = bitDepth;
    options.qualityOptionIndex = 0;  // Not used for WAV
    
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(
            fileStream.get(),
            options
        )
    );
    
    if (writer == nullptr)
    {
        DBG("Failed to create audio format writer");
        return false;
    }
    
    fileStream.release();  // Writer takes ownership
    
    // Write audio data
    bool success = writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    
    return success;
}
```

### Background File Writing (For Long Recordings)

```cpp
class BackgroundFileWriter : private juce::Thread
{
public:
    BackgroundFileWriter(juce::File file, double sampleRate, int numChannels)
        : Thread("AudioFileWriter"),
          outputFile(file),
          sampleRate(sampleRate),
          numChannels(numChannels)
    {
        startThread(juce::Thread::Priority::normal);
    }
    
    ~BackgroundFileWriter()
    {
        stopThread(1000);
    }
    
    void addBlock(const juce::AudioBuffer<float>& block)
    {
        // Push to lock-free queue for background writing
        audioQueue.push(block);
    }
    
private:
    void run() override
    {
        // Create writer on background thread
        setupWriter();
        
        while (!threadShouldExit())
        {
            juce::AudioBuffer<float> blockToWrite;
            
            if (audioQueue.pop(blockToWrite))
            {
                if (writer != nullptr)
                {
                    writer->writeFromAudioSampleBuffer(
                        blockToWrite, 
                        0, 
                        blockToWrite.getNumSamples()
                    );
                }
            }
            else
            {
                // Queue empty, wait a bit
                wait(10);
            }
        }
        
        // Cleanup
        writer.reset();
    }
    
    void setupWriter()
    {
        // ... create writer similar to saveAudioFile() ...
    }
    
    juce::File outputFile;
    double sampleRate;
    int numChannels;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    ThreadSafeAudioQueue audioQueue { numChannels, 512 };
};
```

---

## 7. Memory Management & Smart Pointers

### JUCE 8 Coding Standards

#### Use std::unique_ptr (Preferred)

```cpp
// ✅ GOOD: Modern C++
class AudioProcessor
{
public:
    AudioProcessor()
    {
        // Use std::make_unique (requires C++14)
        reader = std::make_unique<juce::AudioFormatReader>();
        
        // Or direct construction
        buffer = std::make_unique<juce::AudioBuffer<float>>(2, 512);
    }
    
    void process()
    {
        if (reader != nullptr)
        {
            // Use like normal pointer
            auto sampleRate = reader->sampleRate;
        }
    }
    
private:
    std::unique_ptr<juce::AudioFormatReader> reader;
    std::unique_ptr<juce::AudioBuffer<float>> buffer;
};
```

#### Passing Smart Pointers

```cpp
// ✅ GOOD: Pass raw pointer when not transferring ownership
void processAudio(juce::AudioBuffer<float>* buffer)
{
    // Function uses buffer but doesn't own it
    buffer->clear();
}

// ✅ GOOD: Pass by reference when pointer cannot be null
void processAudio(juce::AudioBuffer<float>& buffer)
{
    buffer.clear();  // Guaranteed valid
}

// ✅ GOOD: Return unique_ptr to transfer ownership
std::unique_ptr<juce::AudioFormatReader> createReader(const juce::File& file)
{
    return std::unique_ptr<juce::AudioFormatReader>(
        formatManager.createReaderFor(file)
    );
}

// ✅ GOOD: Accept unique_ptr to take ownership
void setReader(std::unique_ptr<juce::AudioFormatReader> newReader)
{
    reader = std::move(newReader);
}

// ❌ BAD: Don't pass smart pointers by value unless transferring ownership
void processAudio(std::unique_ptr<juce::AudioBuffer<float>> buffer)
{
    // This takes ownership - probably not what you want
}
```

#### RAII Pattern for Resources

```cpp
class AudioFileLoader
{
public:
    AudioFileLoader(const juce::File& file)
    {
        // Resources acquired in constructor
        inputStream = file.createInputStream();
        
        if (inputStream != nullptr)
        {
            reader.reset(formatManager.createReaderFor(inputStream.release()));
        }
    }
    
    // Destructor automatically cleans up
    ~AudioFileLoader() = default;
    
    bool isValid() const { return reader != nullptr; }
    
    // Deleted copy, move allowed
    AudioFileLoader(const AudioFileLoader&) = delete;
    AudioFileLoader& operator=(const AudioFileLoader&) = delete;
    AudioFileLoader(AudioFileLoader&&) = default;
    AudioFileLoader& operator=(AudioFileLoader&&) = default;
    
private:
    std::unique_ptr<juce::FileInputStream> inputStream;
    std::unique_ptr<juce::AudioFormatReader> reader;
    juce::AudioFormatManager formatManager;
};
```

### Avoiding Memory Allocation in Audio Callback

```cpp
class AudioEngine
{
public:
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate)
    {
        // Pre-allocate ALL buffers here
        workingBuffer.setSize(2, samplesPerBlockExpected);
        recordingBuffer.setSize(2, static_cast<int>(sampleRate * 60)); // 60 seconds
        
        // Pre-allocate vectors to maximum size
        fileQueue.reserve(1000);  // Maximum 1000 files
        
        // Clear contents
        workingBuffer.clear();
        recordingBuffer.clear();
    }
    
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
    {
        // NO allocation here - only use pre-allocated buffers
        workingBuffer.clear();
        
        // Copy data using pre-allocated buffer
        for (int ch = 0; ch < 2; ++ch)
        {
            workingBuffer.copyFrom(ch, 0, *bufferToFill.buffer, ch, 
                                  bufferToFill.startSample, 
                                  bufferToFill.numSamples);
        }
    }
    
private:
    juce::AudioBuffer<float> workingBuffer;
    juce::AudioBuffer<float> recordingBuffer;
    std::vector<juce::File> fileQueue;  // Pre-reserved
};
```

---

## 8. Error Handling & Robustness

### Defensive Audio Callback

```cpp
void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    // Safety check
    if (bufferToFill.buffer == nullptr)
    {
        jassertfalse;  // Debug builds will catch this
        return;
    }
    
    // Always clear first (prevents noise on error)
    bufferToFill.clearActiveBufferRegion();
    
    // Bounds checking
    const int numSamples = bufferToFill.numSamples;
    const int numChannels = bufferToFill.buffer->getNumChannels();
    
    if (numSamples <= 0 || numChannels <= 0)
        return;
    
    // Safe processing with bounds checks
    try
    {
        if (isProcessing.load(std::memory_order_relaxed))
        {
            const int samplesRemaining = playbackBuffer.getNumSamples() - 
                                        playbackPosition.load(std::memory_order_relaxed);
            
            if (samplesRemaining > 0)
            {
                const int samplesToPlay = juce::jmin(numSamples, samplesRemaining);
                const int position = playbackPosition.load(std::memory_order_relaxed);
                
                // Additional safety check
                if (position >= 0 && 
                    position + samplesToPlay <= playbackBuffer.getNumSamples())
                {
                    // Safe to copy
                    for (int ch = 0; ch < juce::jmin(numChannels, 
                                                     playbackBuffer.getNumChannels()); ++ch)
                    {
                        bufferToFill.buffer->copyFrom(
                            ch, bufferToFill.startSample,
                            playbackBuffer, ch, position, samplesToPlay
                        );
                    }
                    
                    playbackPosition.fetch_add(samplesToPlay, std::memory_order_relaxed);
                }
            }
        }
    }
    catch (...)
    {
        // Last resort - should never happen with proper bounds checking
        jassertfalse;
        bufferToFill.clearActiveBufferRegion();
    }
}
```

### File Operation Error Handling

```cpp
juce::Result loadAudioFileSafe(const juce::File& file, 
                               juce::AudioBuffer<float>& buffer)
{
    // Check file exists
    if (!file.existsAsFile())
        return juce::Result::fail("File does not exist: " + file.getFullPathName());
    
    // Check file size (prevent loading massive files)
    const int64 fileSize = file.getSize();
    const int64 maxFileSize = 500 * 1024 * 1024;  // 500 MB limit
    
    if (fileSize > maxFileSize)
        return juce::Result::fail("File too large: " + 
                                 juce::String(fileSize / (1024 * 1024)) + " MB");
    
    // Try to create reader
    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(file)
    );
    
    if (reader == nullptr)
        return juce::Result::fail("Unsupported audio format or corrupted file");
    
    // Check sample rate
    if (reader->sampleRate < 8000 || reader->sampleRate > 192000)
        return juce::Result::fail("Invalid sample rate: " + 
                                 juce::String(reader->sampleRate));
    
    // Check channel count
    if (reader->numChannels < 1 || reader->numChannels > 32)
        return juce::Result::fail("Invalid channel count: " + 
                                 juce::String(reader->numChannels));
    
    // All checks passed - load file
    const int numSamples = static_cast<int>(reader->lengthInSamples);
    buffer.setSize(static_cast<int>(reader->numChannels), numSamples);
    
    if (!reader->read(&buffer, 0, numSamples, 0, true, true))
        return juce::Result::fail("Failed to read audio data");
    
    return juce::Result::ok();
}
```

### User-Facing Error Display

```cpp
void showError(const juce::String& errorMessage)
{
    // Always run UI operations on message thread
    juce::MessageManager::callAsync([errorMessage]()
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Error",
            errorMessage,
            "OK"
        );
    });
}

// Usage:
auto result = loadAudioFileSafe(file, buffer);
if (result.failed())
{
    DBG("Load failed: " + result.getErrorMessage());
    showError(result.getErrorMessage());
}
```

---

## 9. Modern JUCE 8 API Usage

### Font Creation (JUCE 8 Update)

```cpp
// ❌ DEPRECATED: Old Font constructor
juce::Font oldFont("Arial", 16.0f, juce::Font::plain);

// ✅ CORRECT: Use FontOptions
juce::Font modernFont(juce::FontOptions()
    .withName("Arial")
    .withHeight(16.0f)
    .withStyle(juce::Font::plain));

// Or shorter:
juce::Font font(juce::FontOptions("Arial", 16.0f, juce::Font::plain));
```

### File Chooser (Modern Async API)

```cpp
// ✅ JUCE 8: Modern async file chooser
void openFileChooser()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select audio files to process",
        juce::File::getCurrentWorkingDirectory(),
        "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3",
        true  // useNativeDialogue
    );
    
    auto flags = juce::FileBrowserComponent::openMode |
                juce::FileBrowserComponent::canSelectMultipleItems;
    
    fileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        auto files = chooser.getResults();
        
        for (const auto& file : files)
        {
            addFileToQueue(file);
        }
    });
}

// ❌ OLD: Synchronous (blocks message thread)
void openFileChooserOld()
{
    juce::FileChooser chooser("Select files");
    if (chooser.browseForMultipleFilesToOpen())  // Blocks!
    {
        // ...
    }
}
```

### Component Resizing

```cpp
void resized() override
{
    auto bounds = getLocalBounds();
    
    // Modern FlexBox layout
    juce::FlexBox fb;
    fb.flexDirection = juce::FlexBox::Direction::column;
    fb.items.add(juce::FlexItem(deviceSelector).withFlex(0, 1, 60));
    fb.items.add(juce::FlexItem(fileList).withFlex(1));
    fb.items.add(juce::FlexItem(controlPanel).withFlex(0, 1, 80));
    
    fb.performLayout(bounds);
}
```

---

## 10. Cross-Platform Considerations

### File Path Handling

```cpp
// ✅ GOOD: Use juce::File for cross-platform paths
juce::File getDefaultOutputFolder()
{
    #if JUCE_MAC
        return juce::File::getSpecialLocation(
            juce::File::userMusicDirectory
        ).getChildFile("Hardware Resampler");
    #elif JUCE_WINDOWS
        return juce::File::getSpecialLocation(
            juce::File::userDocumentsDirectory
        ).getChildFile("Hardware Resampler");
    #else
        return juce::File::getSpecialLocation(
            juce::File::userHomeDirectory
        ).getChildFile("HardwareResampler");
    #endif
}

// ❌ BAD: Hardcoded paths
juce::File badPath("/Users/username/Music");  // macOS only!
```

### Audio Device Availability

```cpp
bool isAudioDeviceAvailable(const juce::String& deviceName)
{
    #if JUCE_MAC
        // CoreAudio devices
        auto& deviceManager = getAudioDeviceManager();
        auto deviceTypes = deviceManager.getAvailableDeviceTypes();
        
        for (auto* type : deviceTypes)
        {
            auto devices = type->getDeviceNames(false);  // output devices
            if (devices.contains(deviceName))
                return true;
        }
    #elif JUCE_WINDOWS
        // Check both ASIO and WASAPI
        // Similar enumeration logic
    #endif
    
    return false;
}
```

### Sample Rate Support

```cpp
std::vector<double> getSupportedSampleRates()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    
    if (device == nullptr)
        return { 44100.0, 48000.0 };  // Defaults
    
    auto rates = device->getAvailableSampleRates();
    
    std::vector<double> supported;
    for (auto rate : rates)
        supported.push_back(rate);
    
    return supported;
}
```

---

## 11. Performance Optimization

### Minimize UI Repaints

```cpp
class ProgressDisplay : public juce::Component,
                       private juce::Timer
{
public:
    ProgressDisplay()
    {
        startTimer(50);  // 20 Hz is enough for visual feedback
    }
    
    void setProgress(float newProgress)
    {
        // Only repaint if change is significant
        if (std::abs(newProgress - currentProgress) > 0.01f)
        {
            currentProgress = newProgress;
            repaint();
        }
    }
    
    void timerCallback() override
    {
        // Throttle updates
        auto now = juce::Time::getCurrentTime();
        if ((now - lastUpdate).inMilliseconds() > 50)
        {
            float progress = getProgressFromAudioThread();
            setProgress(progress);
            lastUpdate = now;
        }
    }
    
private:
    float currentProgress = 0.0f;
    juce::Time lastUpdate;
};
```

### Efficient Audio Processing

```cpp
// ✅ GOOD: Process entire buffer at once
void processBuffer(juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        float* data = buffer.getWritePointer(channel);
        
        // SIMD-friendly: continuous memory access
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            data[sample] *= 0.5f;
        }
    }
}

// ❌ BAD: Sample-by-sample with channel switching
void processBufferSlow(juce::AudioBuffer<float>& buffer)
{
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            // Poor cache locality
            float* data = buffer.getWritePointer(channel);
            data[sample] *= 0.5f;
        }
    }
}
```

### Pre-calculate Constants

```cpp
void prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    this->sampleRate = sampleRate;
    
    // Pre-calculate frequently used values
    samplesToMs = 1000.0 / sampleRate;
    msToSamples = sampleRate / 1000.0;
    
    // Pre-calculate buffer sizes
    silenceSamples = static_cast<int>(silenceDurationMs * msToSamples);
}

// In audio callback:
void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    // Use pre-calculated value (no division in real-time)
    if (silenceSampleCount >= silenceSamples)
    {
        startNextFile();
    }
}
```

---

## 12. Real-World Examples & References

### Known Working Multichannel JUCE Projects

1. **JUCE AudioRecordingDemo**
   - Location: `JUCE/examples/Audio/AudioRecordingDemo.h`
   - Shows proper file recording with multichannel support
   - Uses `AudioFormatWriter::ThreadedWriter` for background writing

2. **JUCE AudioPlaybackDemo**
   - Location: `JUCE/examples/Audio/AudioPlaybackDemo.h`
   - Demonstrates `AudioFormatReaderSource` usage
   - Shows proper `AudioTransportSource` implementation

3. **theaudioprogrammer/SimpleEQ**
   - GitHub: https://github.com/theaudioprogrammer/SimpleEQ
   - Shows modern JUCE plugin architecture
   - Good example of parameter management

4. **Tracktion Engine**
   - GitHub: https://github.com/Tracktion/tracktion_engine
   - Professional-grade multichannel audio engine
   - Built on JUCE, demonstrates advanced patterns

### Key Forum Discussions Referenced

- **Multichannel Audio Support**: JUCE Forum confirms full multichannel support limited only by OS/hardware
- **Thread Safety Best Practices**: Multiple discussions emphasize lock-free patterns and atomic operations
- **Real-Time Audio Guidelines**: Community consensus on no allocation/locks in audio callback
- **Smart Pointer Usage**: Transition from `ScopedPointer` to `std::unique_ptr` recommended

### Testing Multichannel Configurations

```cpp
void testMultichannelSetup()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    
    if (device == nullptr)
    {
        DBG("No audio device selected");
        return;
    }
    
    DBG("Device: " + device->getName());
    DBG("Sample rate: " + juce::String(device->getCurrentSampleRate()));
    DBG("Buffer size: " + juce::String(device->getCurrentBufferSizeSamples()));
    
    auto inputChannels = device->getInputChannelNames();
    auto outputChannels = device->getOutputChannelNames();
    
    DBG("Available input channels: " + juce::String(inputChannels.size()));
    for (int i = 0; i < inputChannels.size(); ++i)
        DBG("  [" + juce::String(i) + "] " + inputChannels[i]);
    
    DBG("Available output channels: " + juce::String(outputChannels.size()));
    for (int i = 0; i < outputChannels.size(); ++i)
        DBG("  [" + juce::String(i) + "] " + outputChannels[i]);
    
    auto activeInputs = device->getActiveInputChannels();
    auto activeOutputs = device->getActiveOutputChannels();
    
    DBG("Active input channels: " + activeInputs.toString(2));
    DBG("Active output channels: " + activeOutputs.toString(2));
}
```

---

## Summary Checklist

### Audio Thread Safety ✓
- [ ] No memory allocation in `getNextAudioBlock()`
- [ ] No file I/O in audio callback
- [ ] No locks in audio callback  
- [ ] Use `std::atomic` for simple state
- [ ] Use `AbstractFifo` for complex data transfer
- [ ] Pre-allocate all buffers in `prepareToPlay()`

### Device Management ✓
- [ ] Use inherited `AudioAppComponent::deviceManager`
- [ ] Configure channels via `AudioDeviceSetup`
- [ ] Support dynamic channel pair selection
- [ ] Handle device change/removal gracefully
- [ ] Test with different buffer sizes (64-2048)

### File Operations ✓
- [ ] Register audio formats with `AudioFormatManager`
- [ ] Use JUCE 8 `AudioFormatWriter::Options`
- [ ] Handle file errors with `juce::Result`
- [ ] Write files on message thread (via Timer)
- [ ] Consider `ThreadedWriter` for long recordings

### Memory Management ✓
- [ ] Use `std::unique_ptr` for ownership
- [ ] Pass raw pointers/references for non-owning
- [ ] Follow RAII patterns
- [ ] Avoid `new`/`delete` in application code
- [ ] Pre-allocate audio buffers

### Error Handling ✓
- [ ] Check buffer bounds in audio callback
- [ ] Validate file operations with `Result`
- [ ] Always clear output buffer first
- [ ] Use `jassert` for debug catching
- [ ] Display user-friendly errors

### Cross-Platform ✓
- [ ] Use `juce::File` for all paths
- [ ] Test on macOS and Windows
- [ ] Handle platform-specific audio APIs
- [ ] Check device/format availability
- [ ] Use platform constants (`JUCE_MAC`, `JUCE_WINDOWS`)

---

## Additional Resources

- **JUCE Documentation**: https://docs.juce.com/master/
- **JUCE Forum**: https://forum.juce.com/
- **JUCE GitHub**: https://github.com/juce-framework/JUCE
- **JUCE Tutorials**: https://juce.com/learn/tutorials
- **The Audio Programmer (YouTube)**: Excellent JUCE tutorials
- **Timur Doumler Talks**: "C++ in the Audio Industry" - lock-free programming

**Document End**