## Best Practices for JUCE 8.x Standalone Multichannel Audio Applications

This document consolidates real-world, conflict-resolved practices drawn from multiple expert-generated sources and proven examples (e.g., GitHub projects, JUCE forum posts) for developing professional, standalone multichannel audio applications using JUCE 8.x. The guidance is structured for GitHub rendering and AI agent parsing.

---

### Table of Contents
1. [Project Architecture and Build System](#project-architecture-and-build-system)
2. [Audio Device Management](#audio-device-management)
3. [Real-Time Audio Callback Design](#real-time-audio-callback-design)
4. [Audio Routing Strategy](#audio-routing-strategy)
5. [File I/O and Asynchronous Processing](#file-io-and-asynchronous-processing)
6. [Cross-Platform Compatibility](#cross-platform-compatibility)
7. [UI and UX Considerations](#ui-and-ux-considerations)

---

### Project Architecture and Build System

- **Use CMake over Projucer**
  - All modern, scalable JUCE projects adopt CMake for better version control, IDE flexibility, and CI compatibility.
  - Recommended IDEs: CLion, VS2022, Xcode with CMake integration.
  - Project layout should separate core modules (`/src`, `/modules`, `/audio`, `/gui`).

- **Use `juce::AudioAppComponent` for Standalone Apps**
  - Avoid `StandalonePluginHolder` and plugin-specific wrappers.
  - Derive main content from `AudioAppComponent` to access `deviceManager` directly and clearly handle `prepareToPlay`, `getNextAudioBlock`, and `releaseResources`.

- **Main Lifecycle Class: `JUCEApplication`**
  - Keep logic light in constructor.
  - Use `initialise()` for component creation, and `shutdown()` for teardown.
  - Handle `systemRequestedQuit()` to protect batch processes.

---

### Audio Device Management

- **Device Initialization**
  - Call `deviceManager.initialise(numInputs, numOutputs, nullptr, true);` with large-enough channel count (e.g., 256) to support wide I/O setups.
  - Always persist and restore device state using `deviceManager.createStateXml()` and `deviceManager.initialise(...)` with an XML config.

- **Custom Audio Device UI**
  - Replace `AudioDeviceSelectorComponent` with a custom component using `ComboBox` for:
    - Driver type (CoreAudio, ASIO)
    - Device
    - Sample rate / buffer size
    - Stereo input and output pairings (e.g., 3–4)

- **Multichannel Mapping**
  - Group device channels into stereo pairs.
  - Translate ComboBox selections into `juce::BigInteger` masks for input/output channel sets.

- **Platform Nuances**
  - macOS: Aggregate devices supported by CoreAudio without custom code.
  - Windows: Prefer ASIO for multichannel routing. WASAPI is now improved in JUCE 8 but historically unreliable beyond 1–2 channels.
  - Handle device loss by using `audioDeviceListChanged()` and fallback logic.

---

### Real-Time Audio Callback Design

- **Keep `getNextAudioBlock()` Lightweight**
  - Delegate playback and recording to helper classes.
  - Avoid allocations, locks, logging, or any blocking I/O.

- **Architecture**
```cpp
void getNextAudioBlock (const AudioSourceChannelInfo& bufferToFill) override {
    player.process(bufferToFill);
    recorder.capture(bufferToFill);
    clearUnusedChannels(bufferToFill);
}
```

- **Channel Mapping**
  - Resolve input/output indices in `prepareToPlay()` once.
  - Use fixed channel indices for buffer access in callback.

---

### Audio Routing Strategy

- **Dynamic Channel Mapping**
  - Cache active channel indices (e.g., `inputLeft = 2`, `inputRight = 3`) post-device setup.
  - Copy audio from or to temporary buffers.

- **Example Routing Logic**
```cpp
auto* writeL = buffer.getWritePointer(outputLeft);
auto* readL = tempPlaybackBuffer.getReadPointer(0);
FloatVectorOperations::copy(writeL, readL, numSamples);
```

---

### File I/O and Asynchronous Processing

- **Playback**
  - Use `AudioFormatReader` + `AudioTransportSource`.
  - Control transport from message thread.
  - Monitor transport state via `ChangeListener`.

- **Recording**
  - Implement FIFO: audio thread pushes buffers, worker thread consumes them.
  - Use `juce::AudioFormatWriter::ThreadedWriter` for safe background writing.

- **File Preloading**
  - Load small/medium files fully into RAM on a background thread using `AudioSampleBuffer`.
  - Use `ReferenceCountedObjectPtr` to pass ownership safely to the audio thread.

- **Fallback to Streaming**
  - For large files, use `BufferingAudioReader` and `TimeSliceThread`.

- **Error Handling**
  - Wrap `createInputStream()`, `createWriterFor()` with validity checks.
  - Warn users with helpful messages.

---

### Cross-Platform Compatibility

- **Audio Drivers**
  - macOS: CoreAudio
  - Windows: ASIO preferred; optionally fall back to WASAPI

- **Device Queries**
  - Use `AudioIODeviceType::getDeviceNames()` and `getInputChannelNames()` / `getOutputChannelNames()`

- **Build Config**
  - Use CMake toolchain files to configure platform-specific flags.
  - Enable ASIO SDK manually on Windows; requires license acceptance.
  - Test using hardware from RME, Focusrite, MOTU, Apogee for compatibility.

---

### UI and UX Considerations

- **Device UI**
  - Use logical groupings like "Playback 3–4", not bitmask toggles.

- **Batch Process Progress**
  - Visual queue or table of files being processed
  - Show current source, output path, and status (✔, ✖, ⏳)

- **Graceful Shutdown**
  - Warn on quit if a process is active
  - Flush all buffers and finalize file writing

---

This document is optimized for long-term maintenance and extensibility of high-performance, standalone JUCE apps that work with real-world multichannel interfaces. It is intended for integration in internal documentation, public GitHub repos, or LLM ingestion.

