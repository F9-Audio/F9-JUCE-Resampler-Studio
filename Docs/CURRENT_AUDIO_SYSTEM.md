# Current Audio System Documentation

**Date:** 2025-01-21
**Status:** ✅ WORKING - Basic stereo I/O operational
**App:** F9 JUCE Batch Resampler Studio

---

## Overview

The audio system is based on **JUCE's AudioAppComponent** class, which provides a simplified interface for real-time audio I/O. The current implementation supports basic stereo input/output with the ability to switch devices and change sample rates.

---

## Architecture

### Class Hierarchy

```
MainComponent
├── juce::AudioAppComponent (audio I/O)
└── juce::Timer (UI updates)
```

### Key Components

1. **AudioAppComponent** - Provides:
   - `deviceManager` - Manages audio devices and settings
   - `prepareToPlay()` - Called when audio starts
   - `getNextAudioBlock()` - Real-time audio callback
   - `releaseResources()` - Called when audio stops

2. **AppState** - Contains:
   - Audio buffers (playback, recording, latency capture)
   - Device and channel settings
   - Processing flags (isProcessing, isMeasuringLatency, etc.)

---

## Audio Initialization

### Location: `MainComponent::MainComponent()`
**File:** [Source/MainComponent.cpp:5-87](../Source/MainComponent.cpp#L5-L87)

```cpp
MainComponent::MainComponent()
    : settingsComponent(appState),
      fileListAndLogComponent(appState)
{
    // Register audio formats
    formatManager.registerBasicFormats();

    // Initialize audio system with basic stereo I/O
    // This will use the default device temporarily until user selects one
    setAudioChannels(2, 2);  // 2 inputs, 2 outputs

    // ... UI setup ...
}
```

**Key Points:**
- ✅ Calls `setAudioChannels(2, 2)` to request stereo I/O
- ✅ Uses system default audio device at startup
- ✅ Registers audio format readers (WAV, AIFF, etc.)

---

## Audio Preparation

### Location: `MainComponent::prepareToPlay()`
**File:** [Source/MainComponent.cpp:97-119](../Source/MainComponent.cpp#L97-L119)

```cpp
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
```

**Called When:**
- Audio device starts
- Sample rate changes
- Buffer size changes
- Device is switched

**What It Does:**
1. ✅ Syncs AppState sample rate and buffer size with actual device
2. ✅ Allocates input buffer based on device's active input channel count
3. ✅ Allocates playback buffer (100 blocks worth)
4. ✅ Allocates recording buffer (60 seconds at current sample rate)
5. ✅ Allocates latency capture buffer (5 seconds)

---

## Real-Time Audio Callback

### Location: `MainComponent::getNextAudioBlock()`
**File:** [Source/MainComponent.cpp:127-169](../Source/MainComponent.cpp#L127-L169)

```cpp
void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    // ============================================================================
    // AUDIO CALLBACK - Multichannel routing approach
    // bufferToFill.buffer = OUTPUT buffer (what goes to hardware)
    // ============================================================================

    const int numSamples = bufferToFill.numSamples;
    const int numChannels = bufferToFill.buffer->getNumChannels();

    // Clear all outputs by default
    bufferToFill.clearActiveBufferRegion();

    // State machine based on appState flags

    if (appState.isTestingHardware)
    {
        // HARDWARE TEST MODE: Generate 1kHz sine wave (stereo)
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
```

**Critical Concepts:**

1. **bufferToFill.buffer** = OUTPUT buffer
   - Write to this to send audio to hardware outputs
   - Contains `numChannels` channels (typically 2 for stereo)
   - Contains `numSamples` samples per channel

2. **bufferToFill.startSample** = Starting offset in buffer
   - Usually 0, but can be non-zero

3. **Thread Safety:**
   - ⚠️ This runs on the audio thread (high priority)
   - ⚠️ Must be fast and lock-free
   - ⚠️ No file I/O, no UI updates, no allocations
   - ✅ Can read/write audio buffers
   - ✅ Can set atomic flags

**Current Implementation:**
- ✅ **Hardware Test Mode** - Generates 1kHz sine wave on channels 0+1
- ⏳ **Processing Mode** - TODO: Play file + record input
- ⏳ **Latency Measurement** - TODO: Send impulse + capture return
- ⏳ **Preview Mode** - TODO: Play files only

---

## Device Configuration

### Location: `MainComponent::configureAudioDevice()`
**File:** [Source/MainComponent.cpp:424-539](../Source/MainComponent.cpp#L424-L539)

```cpp
void MainComponent::configureAudioDevice()
{
    // Find the selected device
    AudioDevice* selectedDevice = appState.getSelectedDevice();
    if (selectedDevice == nullptr) return;

    // CRITICAL: Set the device type FIRST
    deviceManager.setCurrentAudioDeviceType(selectedDevice->deviceTypeName, true);

    // Get current setup and modify it
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup(setup);

    // Update setup with our selected device and settings
    setup.outputDeviceName = selectedDevice->name;
    setup.inputDeviceName = selectedDevice->name;
    setup.sampleRate = appState.settings.sampleRate;
    setup.bufferSize = static_cast<int>(appState.settings.bufferSize);

    // Configure channels
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = false;
    setup.inputChannels.clear();
    setup.outputChannels.clear();

    // Set specific input/output channel pairs
    if (appState.hasInputPair)
    {
        setStereoBits(setup.inputChannels, appState.selectedInputPair);
    }

    if (appState.hasOutputPair)
    {
        setStereoBits(setup.outputChannels, appState.selectedOutputPair);
    }

    // Apply the setup
    juce::String error = deviceManager.setAudioDeviceSetup(setup, true);

    // Verify device opened correctly
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device != nullptr)
    {
        double actualSampleRate = device->getCurrentSampleRate();
        int actualBufferSize = device->getCurrentBufferSizeSamples();

        appState.settings.sampleRate = actualSampleRate;
        appState.settings.bufferSize = static_cast<BufferSize>(actualBufferSize);
    }

    // Apply device setup
    juce::String error2 = deviceManager.setAudioDeviceSetup(setup, true);
}
```

**What Happens When User Selects Device:**

1. ✅ Sets device type (CoreAudio on Mac, ASIO on Windows)
2. ✅ Gets current setup (preserves existing settings)
3. ✅ Updates device name, sample rate, buffer size
4. ✅ Configures specific channel pairs (e.g., 3+4 for in/out)
5. ✅ Applies setup to AudioDeviceManager
6. ✅ Verifies device opened with actual settings
7. ✅ Syncs AppState with actual device parameters
8. ✅ Triggers `prepareToPlay()` callback

---

## Sample Rate Changes

### How It Works:

1. **User Changes Sample Rate in UI**
   - `SettingsComponent::onSampleRateChanged()` called
   - Updates `appState.settings.sampleRate`
   - Calls `settingsComponent.onDeviceNeedsReconfiguration()`

2. **Device Reconfiguration Triggered**
   - `MainComponent::configureAudioDevice()` called
   - New `AudioDeviceSetup` created with new sample rate
   - `deviceManager.setAudioDeviceSetup()` called

3. **AudioDeviceManager Responds**
   - Stops current audio device
   - Calls `releaseResources()`
   - Reopens device with new sample rate
   - Calls `prepareToPlay()` with new parameters

4. **Buffers Reallocated**
   - `prepareToPlay()` receives new sample rate
   - All buffers resized based on new rate:
     - `recordingBuffer`: 60 seconds at new rate
     - `latencyCaptureBuffer`: 5 seconds at new rate
     - `inputBuffer`: buffer size at new rate

### Location: `SettingsComponent.onDeviceNeedsReconfiguration`
**File:** [Source/MainComponent.cpp:71-75](../Source/MainComponent.cpp#L71-L75)

```cpp
// Wire up device reconfiguration callback
settingsComponent.onDeviceNeedsReconfiguration = [this]()
{
    configureAudioDevice();
};
```

**Supported Sample Rates:**
- 44100 Hz
- 48000 Hz
- 88200 Hz
- 96000 Hz
- 176400 Hz
- 192000 Hz

---

## Buffer Management

### Buffer Allocation Strategy

| Buffer | Size | Purpose | Allocated In |
|--------|------|---------|--------------|
| `inputBuffer` | numInputChannels × bufferSize | Temporary input capture | `prepareToPlay()` |
| `currentPlaybackBuffer` | 2 × (bufferSize × 100) | Source file to play | `prepareToPlay()` |
| `recordingBuffer` | 2 × (sampleRate × 60) | Captured audio (60 sec max) | `prepareToPlay()` |
| `latencyCaptureBuffer` | 2 × (sampleRate × 5) | Impulse response capture | `prepareToPlay()` |

### Why These Sizes?

**currentPlaybackBuffer (bufferSize × 100):**
- Large enough for most audio files in memory
- Reloaded for each file during processing

**recordingBuffer (60 seconds):**
- Max recording time in reverb mode
- Safety limit to prevent runaway recording

**latencyCaptureBuffer (5 seconds):**
- Plenty of time to capture impulse return
- Typical latency is < 500 samples at 96kHz

---

## Channel Configuration

### Current Approach: Stereo Only

```cpp
setAudioChannels(2, 2);  // 2 inputs, 2 outputs
```

**Channels Used:**
- Input Channels: 0, 1 (left, right)
- Output Channels: 0, 1 (left, right)

### User-Selected Channels (Stored but Not Yet Used)

```cpp
// AppState stores user's preferred channels
appState.selectedInputPair;   // e.g., {leftChannel: 3, rightChannel: 4}
appState.selectedOutputPair;  // e.g., {leftChannel: 3, rightChannel: 4}

// These are configured in setAudioDeviceSetup but not yet routed in callback
```

**⏳ TODO:** Implement multichannel routing in `getNextAudioBlock()` to:
1. Read from user-selected input channels
2. Write to user-selected output channels
3. Leave other channels silent

---

## State Machine

### Audio Processing Modes

The audio callback routes audio based on flags in `AppState`:

| Flag | Mode | Status | Description |
|------|------|--------|-------------|
| `isTestingHardware` | Hardware Test | ✅ Working | Generates 1kHz sine wave |
| `isProcessing` | File Processing | ⏳ TODO | Plays file + records return |
| `isMeasuringLatency` | Latency Measurement | ⏳ TODO | Sends impulse + captures return |
| `isPreviewing` | Preview | ⏳ TODO | Plays files without recording |
| (none) | Idle | ✅ Working | Outputs silence |

### State Transitions

```
User Action          →  Flag Set             →  Audio Callback Behavior
──────────────────────────────────────────────────────────────────────────
"Start Loop Test"    →  isTestingHardware   →  Generate 1kHz sine
"Stop Loop Test"     →  (all flags false)   →  Output silence
"Measure Latency"    →  isMeasuringLatency  →  TODO: Send impulse
"Process All"        →  isProcessing        →  TODO: Play + record
"Preview"            →  isPreviewing        →  TODO: Play only
```

---

## Known Limitations

### Current Constraints

1. **Stereo Only**
   - Only channels 0+1 are used
   - User-selected channels (e.g., 3+4) are NOT routed yet
   - `setAudioChannels(2, 2)` limits us to 2 channels

2. **Input Capture Not Implemented**
   - `inputBuffer` allocated but not filled
   - AudioAppComponent doesn't expose inputs directly in `getNextAudioBlock()`
   - Need to implement input capture mechanism

3. **Processing Modes Incomplete**
   - Only hardware test works
   - File playback not implemented
   - Latency measurement not implemented
   - Preview mode not implemented

### Why Stereo Only Right Now?

**AudioAppComponent Limitation:**
- `getNextAudioBlock()` receives **output buffer** only
- Input data is NOT automatically provided
- Need additional code to capture inputs

**MCFX Uses Different Approach:**
- Inherits from `AudioIODeviceCallback` directly
- Gets raw `inputChannelData` and `outputChannelData` pointers
- Can access all channels directly

**Our Next Step:**
- Either: Use `AudioIODeviceCallback` instead of `AudioAppComponent`
- Or: Request more channels via `setAudioChannels(16, 16)` and manually route

---

## Working Features ✅

### 1. Device Selection
- ✅ Scans and lists all external audio devices
- ✅ Displays device channel counts
- ✅ Filters out built-in devices
- ✅ Switches devices without crash

### 2. Sample Rate Changes
- ✅ Changes sample rate on the fly
- ✅ Reallocates buffers automatically
- ✅ Triggers `prepareToPlay()` correctly
- ✅ Syncs UI with actual device rate

### 3. Hardware Test (1kHz Sine)
- ✅ Generates clean sine wave
- ✅ Outputs to stereo channels 0+1
- ✅ Starts/stops cleanly
- ✅ No clicks or pops

### 4. Buffer Management
- ✅ Allocates buffers based on sample rate
- ✅ Resizes on sample rate change
- ✅ No memory leaks
- ✅ Proper cleanup in destructor

---

## Next Steps 🚧

### To Enable Multichannel Routing:

**Option A: Use AudioIODeviceCallback**
```cpp
class MainComponent : public juce::AudioIODeviceCallback, public juce::Timer
{
    void audioDeviceIOCallback(const float** inputChannelData, int numInputChannels,
                               float** outputChannelData, int numOutputChannels,
                               int numSamples) override;
};
```
- ✅ Gives direct access to all channels
- ✅ Separate input and output pointers
- ❌ More complex setup (no AudioAppComponent helper)

**Option B: Request More Channels in AudioAppComponent**
```cpp
setAudioChannels(16, 16);  // Request all channels
// Then manually route in getNextAudioBlock()
```
- ✅ Keeps AudioAppComponent simplicity
- ❌ Still need to access inputs (not provided in callback)

### To Implement Input Capture:

Need to investigate how AudioAppComponent provides inputs, or switch to low-level callback.

---

## File References

### Key Source Files

| File | Purpose |
|------|---------|
| [Source/MainComponent.h](../Source/MainComponent.h) | Main component class declaration |
| [Source/MainComponent.cpp](../Source/MainComponent.cpp) | Audio system implementation |
| [Source/AppState.h](../Source/AppState.h) | State and buffer storage |
| [Source/SettingsComponent.h](../Source/SettingsComponent.h) | UI for device/channel selection |

### Key Methods

| Method | Line | Purpose |
|--------|------|---------|
| `MainComponent()` | 5 | Initialize audio system |
| `prepareToPlay()` | 97 | Allocate buffers when device starts |
| `getNextAudioBlock()` | 127 | Real-time audio callback |
| `releaseResources()` | 121 | Cleanup when device stops |
| `configureAudioDevice()` | 424 | Switch devices and configure channels |
| `refreshDevices()` | 329 | Scan for available audio devices |

---

## Troubleshooting

### No Audio Output

**Check:**
1. Is `isTestingHardware` flag set? (Click "Start Loop Test")
2. Is device opened? (Check console logs for "Audio system prepared")
3. Is sample rate correct? (Check device supports requested rate)
4. Are channels selected? (Should auto-select first pair)

### Device Won't Switch

**Check:**
1. Is device being used by another app?
2. Does device support requested sample rate?
3. Check console logs for error messages from `setAudioDeviceSetup()`

### Sample Rate Won't Change

**Check:**
1. Does device support the requested rate?
2. Check if device is in exclusive mode (Windows ASIO)
3. Try setting rate in device's control panel first

---

## Summary

**What Works:**
- ✅ Basic stereo I/O initialization
- ✅ Device selection and switching
- ✅ Sample rate changes
- ✅ Buffer allocation and management
- ✅ Hardware test mode (1kHz sine wave)

**What's Next:**
- 🚧 Implement multichannel routing
- 🚧 Enable input capture
- 🚧 Complete processing modes
- 🚧 Add latency measurement
- 🚧 Add file playback + recording

**Architecture:**
- Built on JUCE's AudioAppComponent
- State machine driven by AppState flags
- Buffers allocated in `prepareToPlay()`
- Audio routing in `getNextAudioBlock()`
- Device management via AudioDeviceManager

---

*Last Updated: 2025-01-21*
*Status: Basic stereo I/O functional, ready for multichannel expansion*
