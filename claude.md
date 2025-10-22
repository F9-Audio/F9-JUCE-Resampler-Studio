# F9 Batch Resampler: Swift to JUCE/C++ Port Context

## IMPORTANT: Response Guidelines
**Keep all responses concise - maximum 3-5 lines per summary. Focus on what changed and why.**

---

## ✅ IMPLEMENTED: Preview Audio System (2025-10-21)

### Overview
Complete Round Robin preview playback system that loops through selected files with configurable gaps.

### Features Implemented
1. **Round Robin Loop**: Preview continuously loops through selected files until stopped
2. **Dynamic Button**: Changes between "▶ Preview Selected" and "■ Stop Preview"
3. **Gap Control**: Respects `silenceBetweenFilesMs` slider for silence between files
4. **Proper Routing**: Uses selected stereo output pair (just like latency measurement)
5. **Clean UI**: Simplified drag-and-drop area with large down arrow (⬇️)

### Key Implementation Details

**Audio Callback (Preview Mode)**:
```cpp
// In getNextAudioBlock() - preview playback with gaps
if (isInPreviewGap) {
    // Play silence between files
    previewGapSamplesRemaining -= samplesToSilence;
    if (gap complete) load next file
} else {
    // Play current file through selected output pair
    copy from currentPlaybackBuffer to output channels
    if (file complete) start gap
}
```

**File Loading**:
```cpp
bool loadNextFileForPreview() {
    // Finds file by ID in preview playlist
    // Loads into stereo currentPlaybackBuffer
    // Handles mono → stereo conversion automatically
}
```

**Loop Logic**:
```cpp
// When all files played, loop back to start
if (all files complete) {
    currentPreviewFileIndex = -1;
    needsToLoadNextFile = true;  // Restart from first file
}
```

**UI Management**:
- No files: Show drag-and-drop area with down arrow
- Files loaded: Show file list with "Clear All" button to return to drop zone
- No overlapping components = no event conflicts

**Files Modified**:
- `Source/MainComponent.h` - Added preview state variables
- `Source/MainComponent.cpp` - Preview playback logic in audio callback
- `Source/FileListAndLogComponent.cpp` - Simplified drop zone, dynamic button text
- `Source/AppState.h` - Preview playlist and progress tracking

---

## JUCE Version & Best Practices
**JUCE Version:** 8.x (current stable)
**Always use latest JUCE APIs:**
- ✅ `juce::FontOptions` for fonts (NOT deprecated `Font()` constructors)
- ✅ `AudioFormatWriter::Options{}` for audio file writing
- ✅ `AudioAppComponent::deviceManager` (inherited, don't create duplicate)
- ✅ Modern lambda-based file choosers with `launchAsync()`
- ⚠️ Check JUCE 8.x docs for any API changes before implementation

## ⚠️ CRITICAL: macOS Microphone Permissions

**PROBLEM:** On macOS 10.14+, apps MUST request microphone permission or audio input will be SILENT (all zeros).

**SOLUTION - 3 Required Steps:**

### 1. Projucer Configuration (.jucer file)
```xml
<XCODE_MAC targetFolder="Builds/MacOSX"
           microphonePermissionNeeded="1"
           microphonePermissionsText="App needs microphone access to capture audio."
           bundleIdentifier="com.yourcompany.YourApp"
           version="1.0.0">
```

### 2. Runtime Permission Request (in constructor/startup)
```cpp
juce::RuntimePermissions::request(
    juce::RuntimePermissions::recordAudio,
    [this](bool granted)
    {
        if (granted)
            DBG("Microphone access GRANTED");
        else
            DBG("Microphone access DENIED");
    }
);
```

### 3. Testing Permission Changes
- Changing bundle identifier forces macOS to treat app as "new" → prompts for permission again
- Delete app from `/System/Settings/Privacy & Security/Microphone` to reset
- Clean build folder and rebuild after Projucer regeneration

**Symptoms of Missing Permission:**
- `inputBuffer` receives all zeros even when hardware is working
- `numInputChannels` is correct but `inputChannelData[ch][i]` = 0.0
- Hardware test shows output but latency measurement fails (no input detected)

## Project Overview

**Original:** Swift/macOS - **Target:** JUCE/C++ (Cross-platform)
**Purpose:** Batch process audio files through hardware with latency compensation, reverb mode detection, and real-time preview.

---

## Architecture Overview

### Original Swift Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        SwiftUI Views                         │
│  (ContentView, SettingsView, FileListView, AudioOutputPicker)│
└────────────────┬────────────────────────────────────────────┘
                 │
┌────────────────▼────────────────────────────────────────────┐
│                      MainViewModel                           │
│            (Application State Coordinator)                   │
└─┬──────────┬──────────┬──────────────┬─────────────────────┘
  │          │          │              │
  │          │          │              │
┌─▼──────┐ ┌▼──────────▼┐ ┌───────────▼──────┐ ┌────────────▼──────┐
│Models  │ │  Services   │ │LatencyMeasurement│ │HardwareLoopTest   │
│        │ │             │ │Service           │ │Service            │
│Process │ │AudioProcess │ └──────────────────┘ └───────────────────┘
│Settings│ │Service      │
│        │ │             │
│Audio   │ │SineWave     │
│File    │ │Generator    │
│        │ └─────────────┘
│Stereo  │
│Pair    │
└────────┘
```

### Target JUCE/C++ Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                   JUCE Components (UI)                       │
│  (MainComponent, SettingsComponent, FileListComponent, etc.) │
└────────────────┬────────────────────────────────────────────┘
                 │
┌────────────────▼────────────────────────────────────────────┐
│                      MainComponent                           │
│         (AudioAppComponent + Timer + UI Container)           │
│                                                              │
│  • Manages audio I/O via getNextAudioBlock()                │
│  • Contains state machine for different audio modes          │
│  • Coordinates all services via AppState                     │
└────────────────┬────────────────────────────────────────────┘
                 │
┌────────────────▼────────────────────────────────────────────┐
│                         AppState                             │
│              (Central State Container)                       │
│                                                              │
│  • ProcessingSettings (all user settings)                   │
│  • Current operation flags (isProcessing, isPreviewing, etc)│
│  • File lists, buffers, progress tracking                   │
│  • Audio device configuration                                │
└──────────────────────────────────────────────────────────────┘
```

---

## Core Components

### 1. AppState (appState.h)
**Purpose:** Central state container - replaces Swift's `MainViewModel` and `ProcessingSettings`

**Key Responsibilities:**
- Hold all processing settings (buffer size, sample rate, postfix, folder paths, etc.)
- Track current operation state (isProcessing, isMeasuringLatency, isPreviewing, isTestingHardware)
- Maintain file lists and processing queue
- Store latency measurement results
- Hold temporary audio buffers for recording

**Swift Sources:**
- `Models/ProcessingSettings.swift`
- `ViewModels/MainViewModel.swift` (member variables)
- `Models/AudioFile.swift`
- `Models/StereoPair.swift`

**Key Data Members:**
```cpp
struct ProcessingSettings {
    int bufferSize;
    int sampleRate;
    juce::String destinationFolder;
    juce::String filenamePostfix;
    bool useReverbMode;
    double noiseFloorMarginDB;
    int silenceBetweenFilesMs;
    double thresholdDB;
};

struct AppState {
    ProcessingSettings settings;
    
    // Operation flags
    bool isProcessing = false;
    bool isMeasuringLatency = false;
    bool isPreviewing = false;
    bool isTestingHardware = false;
    
    // File management
    juce::Array<juce::File> files;
    int currentFileIndex = 0;
    
    // Audio buffers
    juce::AudioBuffer<float> currentPlaybackBuffer;
    juce::AudioBuffer<float> recordingBuffer;
    
    // Progress tracking
    double processingProgress = 0.0;
    juce::StringArray logLines;
    
    // Latency
    int measuredLatencySamples = 0;
    
    // Device selection
    juce::String selectedDeviceID;
};
```

---

### 2. MainComponent (MainComponent.h/cpp)
**Purpose:** The audio engine - replaces all Swift service classes

**Inheritance:**
```cpp
class MainComponent : public juce::AudioAppComponent,
                      public juce::Timer
```

**Key Responsibilities:**
- **Audio I/O Management:** Implements `getNextAudioBlock()` - the real-time audio callback
- **State Machine:** Routes audio processing based on AppState flags
- **Service Logic:** Contains all audio processing, latency measurement, preview, and testing logic
- **File Management:** Loads, processes, and saves audio files in sequence
- **UI Coordination:** Updates UI via Timer callback after audio operations complete

**Swift Sources:**
- `Services/AudioProcessingService.swift` → Core processing logic
- `Services/LatencyMeasurementService.swift` → Latency detection
- `Services/HardwareLoopTestService.swift` → Hardware testing
- `Services/SineWaveGenerator.swift` → Test signal generation

**Critical Methods:**

```cpp
// Audio callback - called repeatedly by audio system
void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

// State machine triggers
void startProcessing();
void startLatencyMeasurement();
void startPreview();
void startHardwareTest();
void stopAllAudio();

// Processing helpers
juce::AudioBuffer<float> trimLatency(const juce::AudioBuffer<float>& captured,
                                      int latencySamples, 
                                      int originalLength);
bool isReverbTailBelowNoiseFloor(const juce::AudioBuffer<float>& window);
void saveProcessedFile(const juce::AudioBuffer<float>& audio);

// Timer for post-processing tasks
void timerCallback() override;
```

**State Machine in getNextAudioBlock():**
```cpp
void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) {
    if (appState.isProcessing) {
        // Play + Record: output current file, capture input
        // Check stop conditions (fixed length or reverb mode)
    }
    else if (appState.isMeasuringLatency) {
        // Send impulse, detect return peak
    }
    else if (appState.isPreviewing) {
        // Play only, no recording
    }
    else if (appState.isTestingHardware) {
        // Generate sine wave, monitor input
    }
    else {
        // Silence
        bufferToFill.clearActiveBufferRegion();
    }
}
```

---

### 3. UI Components

#### SettingsComponent
**Purpose:** User configuration interface
**Swift Source:** `Views/SettingsView.swift`

**Key Controls:**
- Buffer size selector (ComboBox)
- Latency measurement button and results display
- Destination folder picker
- Filename postfix editor
- Reverb mode toggle
- Noise floor margin slider
- Silence between files slider
- Threshold slider

#### FileListComponent  
**Purpose:** Display and manage file queue
**Swift Source:** `Views/FileListView.swift`

**Key Controls:**
- File list display with stereo pair grouping
- Add files button
- Remove files button
- Clear all button
- Progress indicators per file
- Drag & drop support

#### MainComponent (UI portion)
**Purpose:** Top-level layout and control
**Swift Source:** `Views/ContentView.swift`

**Key Controls:**
- Audio device selector
- Start/Stop processing button
- Preview button
- Hardware test button
- Log/Console output display
- Status indicators
- Layout management for all sub-components

---

## Critical Implementation Details

### 🚨 Latency Trimming (MUST READ)
**Source:** `LATENCY_TRIMMING_FIX.md`

**Problem:** When capturing audio, the recorded buffer contains the latency offset at the beginning

**Solution:** The `trimLatency()` function must:
1. Skip the first `latencySamples` from the captured audio
2. Copy exactly `originalSourceLength` samples after the latency
3. Handle the case where `capturedLength < latencySamples + originalSourceLength`

**Swift Implementation:**
```swift
func trimLatency(capturedAudio: AVAudioPCMBuffer, 
                 latencySamples: Int, 
                 sourceNumSamples: Int) -> AVAudioPCMBuffer
```

**JUCE Implementation:**
```cpp
juce::AudioBuffer<float> trimLatency(const juce::AudioBuffer<float>& captured,
                                     int latencySamplesToTrim,
                                     int sourceNumSamples) {
    int capturedLength = captured.getNumSamples();
    int startPos = latencySamplesToTrim;
    int samplesToExtract = sourceNumSamples;
    
    // Handle insufficient capture
    if (startPos + samplesToExtract > capturedLength) {
        samplesToExtract = juce::jmax(0, capturedLength - startPos);
    }
    
    juce::AudioBuffer<float> trimmed(captured.getNumChannels(), sourceNumSamples);
    trimmed.clear();
    
    if (samplesToExtract > 0 && startPos >= 0) {
        for (int ch = 0; ch < captured.getNumChannels(); ++ch) {
            trimmed.copyFrom(ch, 0, captured, ch, startPos, samplesToExtract);
        }
    }
    
    return trimmed;
}
```

---

### 🚨 Reverb Mode Logic (MUST READ)
**Source:** `REVERB_MODE_IMPLEMENTATION.md`

**Purpose:** Intelligently detect when reverb tail has decayed to noise floor

**Algorithm:**
1. Take a sliding window (e.g., 2048 samples) at the current recording position
2. Calculate RMS level of that window
3. Convert to dB: `20 * log10(rms + epsilon)`
4. Compare to noise floor: `windowDB < (noiseFloorDB + marginDB)`
5. If below threshold for sufficient duration, stop recording

**Swift Implementation:**
```swift
func isReverbTailBelowNoiseFloor(_ audioWindow: AVAudioPCMBuffer) -> Bool
```

**JUCE Implementation:**
```cpp
bool isReverbTailBelowNoiseFloor(const juce::AudioBuffer<float>& audioWindow) {
    double rmsSum = 0.0;
    int totalSamples = 0;
    
    for (int ch = 0; ch < audioWindow.getNumChannels(); ++ch) {
        const float* data = audioWindow.getReadPointer(ch);
        for (int i = 0; i < audioWindow.getNumSamples(); ++i) {
            rmsSum += data[i] * data[i];
            totalSamples++;
        }
    }
    
    double rms = std::sqrt(rmsSum / totalSamples);
    double windowDB = 20.0 * std::log10(rms + 1e-10);
    
    double noiseFloorDB = appState.settings.noiseFloorMarginDB;
    
    return windowDB < noiseFloorDB;
}
```

**Usage in getNextAudioBlock():**
```cpp
if (appState.settings.useReverbMode) {
    // Check every N samples (e.g., every buffer)
    if (recordingSampleCount > minimumRecordingSamples) {
        // Extract last 2048 samples into window buffer
        juce::AudioBuffer<float> window = extractLastWindow(appState.recordingBuffer, 2048);
        
        if (isReverbTailBelowNoiseFloor(window)) {
            consecutiveBelowThresholdBuffers++;
            
            if (consecutiveBelowThresholdBuffers >= requiredConsecutiveBuffers) {
                // Stop recording, trigger save via timer
                appState.isProcessing = false;
                startTimer(10); // Trigger post-processing
            }
        } else {
            consecutiveBelowThresholdBuffers = 0;
        }
    }
}
```

---

## Audio Processing Flow

### Standard Processing Mode (Fixed Length)

```
1. User clicks "Start Processing"
   ↓
2. startProcessing() called
   - Load first file into currentPlaybackBuffer
   - Set isProcessing = true
   - Reset recording buffer
   - Set playbackPosition = 0
   - Start timer
   ↓
3. getNextAudioBlock() called repeatedly by audio system
   - Read from currentPlaybackBuffer → output buffer
   - Copy input buffer → append to recordingBuffer
   - Increment playbackPosition
   - If playbackPosition >= sourceLength: set flag to save
   ↓
4. timerCallback() detects save flag
   - Call trimLatency() on recordingBuffer
   - Save trimmed audio to file (with postfix)
   - Add log entry
   - If more files: sleep(silenceBetweenFilesMs), load next file, repeat
   - If done: set isProcessing = false, update UI
```

### Reverb Mode (Noise Floor Detection)

```
1-2. Same as standard mode
   ↓
3. getNextAudioBlock() with reverb mode enabled
   - Read from currentPlaybackBuffer → output buffer
   - Copy input buffer → append to recordingBuffer
   - Increment playbackPosition
   - If playbackPosition >= sourceLength:
     * Source has finished playing
     * Continue recording input only (output silence)
     * Check noise floor every buffer:
       - Extract last 2048 samples
       - Call isReverbTailBelowNoiseFloor()
       - If below threshold for N consecutive buffers: set flag to save
   ↓
4. timerCallback() same as standard mode
```

---

## Latency Measurement Flow

```
1. User clicks "Measure Latency"
   ↓
2. startLatencyMeasurement() called
   - Set isMeasuringLatency = true
   - Prepare impulse buffer (single sample at max amplitude)
   - Clear capture buffer
   - Set impulseNotYetSent = true
   ↓
3. getNextAudioBlock() called repeatedly
   - If impulseNotYetSent:
     * Write impulse to output buffer[0]
     * Set impulseNotYetSent = false
     * Start capturing input
   - Else:
     * Output silence
     * Copy input → append to capture buffer
     * Search capture buffer for peak above threshold
     * If peak found: record sample position, set flag
   ↓
4. timerCallback() detects completion
   - Calculate latency: measuredLatencySamples = peakPosition
   - Update UI with result
   - Set isMeasuringLatency = false
```

---

## JUCE-Specific Considerations

### Audio Device Configuration
```cpp
void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    // Called before audio starts
    // Configure internal buffers to match device settings
    
    // Update appState with actual device settings
    appState.settings.sampleRate = (int)sampleRate;
    appState.settings.bufferSize = samplesPerBlockExpected;
}

void MainComponent::releaseResources() {
    // Called when audio stops
    // Clean up resources
}
```

### Thread Safety
**Critical:** `getNextAudioBlock()` runs on the **audio thread** (real-time priority)
- Must be fast, no blocking operations
- No file I/O, no UI updates, no memory allocation (if possible)
- Use `Timer` or `AsyncUpdater` to defer heavy work to message thread

**Pattern:**
```cpp
// In audio thread (getNextAudioBlock)
if (processingComplete) {
    shouldSaveFile = true;  // Set flag only
}

// In message thread (timerCallback)
if (shouldSaveFile) {
    saveProcessedFile();  // Safe to do file I/O here
    shouldSaveFile = false;
}
```

### Buffer Management
**JUCE AudioBuffer:**
```cpp
juce::AudioBuffer<float> buffer(numChannels, numSamples);

// Access samples
float* channelData = buffer.getWritePointer(channelIndex);
const float* readData = buffer.getReadPointer(channelIndex);

// Copy between buffers
buffer.copyFrom(destChannel, destStartSample, 
                sourceBuffer, sourceChannel, sourceStartSample, 
                numSamplesToCopy);

// Clear
buffer.clear();
buffer.clear(startSample, numSamples);

// Apply gain
buffer.applyGain(gainValue);
```

---

## UI-to-Engine Communication

### Pattern: UI → AppState → Audio Engine

```cpp
// In SettingsComponent (UI, message thread)
void SettingsComponent::bufferSizeComboBoxChanged() {
    int selectedSize = bufferSizeComboBox.getSelectedId();
    appState.settings.bufferSize = selectedSize;  // Update state
    
    // Notify MainComponent to restart audio with new buffer size
    mainComponent->deviceSettingsChanged();
}

// In MainComponent
void MainComponent::deviceSettingsChanged() {
    shutdownAudio();  // Stop current audio
    
    // Reconfigure device with new settings
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.bufferSize = appState.settings.bufferSize;
    deviceManager.setAudioDeviceSetup(setup, true);
    
    setAudioChannels(2, 2);  // Restart audio
}
```

### Pattern: Audio Engine → UI (via Timer)

```cpp
// In MainComponent::timerCallback() (message thread)
void MainComponent::timerCallback() {
    // Safe to update UI here
    
    // Update progress
    if (appState.isProcessing) {
        double progress = (double)appState.currentFileIndex / appState.files.size();
        progressBar.setValue(progress);
    }
    
    // Update log
    if (appState.logLines.size() > lastLogLineCount) {
        for (int i = lastLogLineCount; i < appState.logLines.size(); ++i) {
            logTextEditor.insertTextAtCaret(appState.logLines[i] + "\n");
        }
        lastLogLineCount = appState.logLines.size();
    }
}
```

---

## File Organization

```
F9BatchResampler_JUCE/
├── Source/
│   ├── Main.cpp                    // Entry point (JUCE generated)
│   ├── MainComponent.h             // Main window + audio engine
│   ├── MainComponent.cpp
│   ├── AppState.h                  // Central state container
│   ├── SettingsComponent.h         // Settings UI
│   ├── SettingsComponent.cpp
│   ├── FileListComponent.h         // File queue UI
│   ├── FileListComponent.cpp
│   └── AudioOutputPicker.h         // Device selector (optional separate)
│       AudioOutputPicker.cpp
├── Resources/                       // Images, icons, etc.
├── JuceLibraryCode/                 // JUCE framework (auto-generated)
├── Builds/                          // Platform-specific project files
│   ├── MacOSX/                      // Xcode project
│   ├── Windows/                     // Visual Studio
│   └── Linux/                       // Makefile
└── F9BatchResampler_JUCE.jucer     // Projucer project file
```

---

## Build Configuration

### Required JUCE Modules
- `juce_audio_basics` - Basic audio data structures
- `juce_audio_devices` - Audio device access (I/O)
- `juce_audio_formats` - Audio file reading/writing (WAV, AIFF, etc.)
- `juce_audio_processors` - Audio processing utilities
- `juce_audio_utils` - High-level audio components
- `juce_dsp` - DSP utilities (optional, for advanced processing)
- `juce_gui_basics` - GUI components
- `juce_gui_extra` - Additional GUI components
- `juce_core` - Core utilities (included automatically)
- `juce_data_structures` - Data structures (included automatically)

### Preprocessor Definitions
```cpp
JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=0
JUCE_MODAL_LOOPS_PERMITTED=1
```

---

## Testing Strategy

### Phase 1: Core Audio I/O
1. Implement basic audio passthrough (input → output)
2. Verify device selection works
3. Test buffer size changes

### Phase 2: File Playback
1. Load a single WAV file
2. Play it through output
3. Verify sample-accurate playback

### Phase 3: Recording
1. Capture input while playing output
2. Save captured audio to file
3. Verify recorded file length

### Phase 4: Latency Compensation
1. Implement latency measurement
2. Test with hardware loopback cable
3. Verify trimLatency() produces correct results

### Phase 5: Batch Processing
1. Process a queue of 2-3 files
2. Verify each file is processed correctly
3. Test silence between files

### Phase 6: Reverb Mode
1. Test with audio containing reverb tail
2. Verify noise floor detection stops at correct point
3. Compare results with fixed-length mode

### Phase 7: Full Integration
1. Test all UI controls
2. Test error handling (missing files, device errors, etc.)
3. Stress test with large file queues

---

## Common Pitfalls & Solutions

### Pitfall: Audio glitches or dropouts
**Cause:** Doing too much work in `getNextAudioBlock()`
**Solution:** Move heavy operations to `Timer::timerCallback()`

### Pitfall: Incorrect recorded file length
**Cause:** Off-by-one errors in sample counting
**Solution:** Carefully track `playbackPosition` and use exact sample counts

### Pitfall: Latency trim produces wrong results
**Cause:** Not accounting for buffer boundaries
**Solution:** Use the exact algorithm from `LATENCY_TRIMMING_FIX.md`

### Pitfall: Reverb mode cuts off too early/late
**Cause:** Noise floor threshold too high/low, or not enough consecutive buffers
**Solution:** Add UI controls to tune `noiseFloorMarginDB` and consecutive buffer count

### Pitfall: UI freezes during processing
**Cause:** Running long operations on message thread
**Solution:** Keep processing in audio thread, only update UI via timer

### Pitfall: Memory leaks with large file queues
**Cause:** Not releasing buffers after processing
**Solution:** Use `clear()` and RAII patterns

---

## Development Phases (Summary)

### Phase 0: Setup ✅
- Install JUCE, Xcode, Projucer
- Create blank JUCE GUI Application project
- Add required modules
- Configure build settings

### Phase 1: State Management 🧠
- Port `ProcessingSettings` → C++ struct
- Port `MainViewModel` data → `AppState` class
- Port `AudioFile` and `StereoPair` structs

### Phase 2: Audio Engine ⚙️
- Implement `AudioAppComponent` interface
- Port audio I/O logic from Swift services
- Implement state machine in `getNextAudioBlock()`
- Port latency trimming logic (**critical**)
- Port reverb mode logic (**critical**)
- Add Timer for post-processing tasks

### Phase 3: UI Components 🖼️
- Create `SettingsComponent` (port `SettingsView.swift`)
- Create `FileListComponent` (port `FileListView.swift`)
- Build `MainComponent` UI layout (port `ContentView.swift`)
- Wire up all UI callbacks to `AppState` and audio engine

### Phase 4: Integration & Testing 🔧
- Connect all components
- Test each feature individually
- Debug and refine
- Optimize performance

---

## Key Swift → C++ Type Mappings

| Swift                          | JUCE/C++                          |
|-------------------------------|-----------------------------------|
| `String`                      | `juce::String`                    |
| `[String]`                    | `juce::StringArray`               |
| `URL`                         | `juce::File`                      |
| `[URL]`                       | `juce::Array<juce::File>`         |
| `Double`, `Float`             | `double`, `float`                 |
| `Int`                         | `int`                             |
| `Bool`                        | `bool`                            |
| `AVAudioPCMBuffer`            | `juce::AudioBuffer<float>`        |
| `AVAudioFile`                 | `juce::AudioFormatReader/Writer`  |
| `@Published` variable         | Regular member + manual UI update |
| `enum BufferSize`             | `enum class BufferSize`           |
| `ObservableObject` class      | Regular C++ class                 |

---

## Important Notes for AI Assistant

1. **Real-time Audio is Critical:** `getNextAudioBlock()` must be fast and lock-free
2. **Sample Accuracy Matters:** Off-by-one errors will cause audible artifacts
3. **Latency Trimming:** Follow `LATENCY_TRIMMING_FIX.md` exactly
4. **Reverb Mode:** Follow `REVERB_MODE_IMPLEMENTATION.md` exactly
5. **Thread Safety:** Audio thread vs. message thread separation is crucial
6. **JUCE Idioms:** Use JUCE's built-in components and patterns
7. **Error Handling:** Check for null pointers, invalid file paths, device errors
8. **Memory Management:** Use RAII, avoid manual new/delete when possible
9. **Testing:** Test each component before integrating
10. **Iteration:** This is complex - expect multiple rounds of refinement

---

## Reference Documentation

- **JUCE Tutorials:** https://docs.juce.com/master/tutorial_simple_synth_noise.html
- **JUCE API:** https://docs.juce.com/master/classes.html
- **AudioAppComponent:** https://docs.juce.com/master/classAudioAppComponent.html
- **AudioBuffer:** https://docs.juce.com/master/classAudioBuffer.html
- **Original Swift Project Files:** (User will provide)

---

## Success Criteria

✅ Application builds without errors on Mac
✅ Audio devices can be selected and configured
✅ Single file can be played through audio output
✅ Audio input is recorded while playing output
✅ Latency measurement produces accurate results
✅ Latency trimming produces sample-accurate output files
✅ Batch processing completes entire file queue
✅ Reverb mode stops at correct point
✅ All UI controls work and update state correctly
✅ No audio glitches, dropouts, or crashes during processing

---

*This document serves as the complete context for AI-assisted porting of F9 Batch Resampler from Swift to JUCE/C++. Refer to this document throughout the development process.*
