Here’s a version of your markdown document “watered” (i.e. structured and annotated) so that external developers/agents can understand *exactly* how and where the [CAAudioHardware bridge](https://github.com/sbooth/CAAudioHardware) is expected to integrate into the project. I’ve preserved all original technical detail, but added:

* Explicit integration points for the CA bridge
* Clarified responsibilities and ownership boundaries
* Notes to prevent common errors when mixing Swift and Core Audio

---

# F9 Batch Resampler – Technical Documentation

## Purpose & Scope

* Run batches of stereo 44.1 kHz audio files out through external hardware, record the return, and write compensated files.
* Provide a macOS-native UI for selecting devices, configuring buffer size, adjusting trimming/DC removal, and monitoring progress.
* Support repeatable latency measurement per buffer size and enforce the measured settings while processing.
* Use the [CAAudioHardware](https://github.com/sbooth/CAAudioHardware) bridge to access low-level HAL properties (e.g., buffer size, channel routing, latency) where AVFoundation is insufficient.

## Platform Constraints & Assumptions

* macOS target using SwiftUI + AVFoundation/Core Audio.
* Sandboxed app with microphone entitlement; assumes the user grants audio input access on launch.
* Stereo-only signal path; channel routing uses contiguous L/R pairs.
* Fixed project sample rate of 44.1 kHz to simplify latency maths and avoid sample-rate conversion.
* Relies on the selected Core Audio device staying connected during a batch.

## Architectural Overview

* **UI Layer (SwiftUI):** `ContentView` composes navigation; `DeviceSelectionView`, `SettingsView`, `FileDropView`, and `StatusLogView` provide focused panes for interaction.
* **State Layer (MVVM):** `MainViewModel` is the single source of truth, exposing device lists, user selections, file metadata, progress, and log strings.
* **Domain Models:** Plain value types (`AudioDevice`, `StereoPair`, `AudioFile`, `ProcessingSettings`) capture audio metadata, device capabilities, and persistent settings.
* **Service Layer:** Swift classes encapsulating Core Audio/AVFoundation work (`AudioService`, `LatencyMeasurementService`, `AudioProcessingService`, `FileProcessingService`).
* **Core Audio Bridge Layer (NEW):** Lightweight C++/Objective-C++ bridge wrapping `CAAudioHardware` for:

  * Enumerating devices, querying nominal sample rate, channel count, and I/O latency.
  * Setting buffer frame size and validating it before engine start.
  * Applying channel routing using `kAudioOutputUnitProperty_ChannelMap` at the HAL level.
* **Concurrency:** Latency measurement, preview, and batch processing use async/await + `Task`; logging occurs on the main actor.

---

## Key Modules

### App Layer

* `App/F9BatchResamplerApp.swift`: Declares the SwiftUI app entry point.
* `App/ContentView.swift`: Hosts a `NavigationSplitView` with devices/settings and file management/logging.

### Models

* `Models/AudioDevice.swift`: Wraps device info from Core Audio via CA bridge.
* `Models/AudioFile.swift`: Caches per-file state and validates 44.1 kHz constraint.
* `Models/ProcessingSettings.swift`: Aggregates tweakable preferences and derived helpers.

### ViewModel

* `ViewModels/MainViewModel.swift`: Central orchestrator:

  * Boot-time: device enumeration and selection via `CAAudioHardware` bridge.
  * Latency: triggers measurement, persists results, warns on drift.
  * File management: drag/drop ingestion, validation, selection utilities.
  * Processing: coordinates batch execution and progress reporting.
  * Logging: timestamped event/error messages.

### Services

#### `Services/AudioService.swift`

* **Current:** Thin wrapper for listing devices and metadata.
* **Upgrade:** Replace device enumeration and buffer-size configuration with calls to the `CAAudioHardware` bridge.

  * Example bridge usage:

    ```swift
    let devices = CAAudioHardware.shared.devices()
    let selected = devices.first { $0.name == "Symphony" }
    selected?.setBufferSize(frames: settings.bufferSize)
    ```
* Expose I/O latency, buffer size, nominal sample rate, and channel capabilities.

#### `Services/LatencyMeasurementService.swift`

* Replace simulated results with real impulse-loopback measurement.
* Use `CAAudioHardware` bridge to:

  * Lock buffer size before `AVAudioEngine` start.
  * Query precise I/O latency from HAL and factor into sample offset calculations.
* Impulse detection, noise floor measurement, and result struct remain as planned.

#### `Services/AudioProcessingService.swift`

* Integrate `CAAudioHardware` for channel mapping and device validation before playback/recording.
* Workflow:

  1. Set buffer size → verify via CA bridge.
  2. Configure channel map → confirm assignment.
  3. Launch playback/record with `AVAudioEngine`.
  4. Capture and trim → write compensated file.

#### `Services/FileProcessingService.swift` (Planned)

* Orchestrate batch job scheduling and output-folder management.
* No direct bridge usage required, but depends on `AudioProcessingService` correctness.

---

## Bridge Integration Strategy

### Project Setup

* Add `CAAudioHardware` source to a new target group: `Bridge/CAAudioHardwareBridge.mm` and `.h`.
* Create a thin Swift wrapper: `CAHardware.swift` exposing static methods and typed models.
* Expose only stable, high-level calls to the rest of the app:

  * `devices() -> [AudioDevice]`
  * `setBufferSize(deviceID:AudioObjectID, frames:UInt32)`
  * `setChannelMap(deviceID:AudioObjectID, map:[UInt32])`
  * `getLatency(deviceID:AudioObjectID) -> Int`

### Ownership Boundaries

* **AVFoundation:** Playback, recording, rendering, trimming, and offline processing.
* **CAAudioHardware:** Device control, HAL state management, routing, and low-level latency queries.
* **SwiftUI / MVVM:** State propagation, user preferences, and logging.

---

## Audio Pipeline Implementation Details

* **Buffer Size:** Must be set with `CAAudioHardware` before engine start.
* **Channel Routing:** Use `CAAudioHardware` to write to `kAudioOutputUnitProperty_ChannelMap`.
* **Sample Format:** Always float32 PCM at 44.1 kHz.
* **Latency Compensation:** Use measured latency samples (from measurement service).
* **Noise Floor Measurement:** Capture and convert RMS to dBFS, clamped to -120 dB.
* **DC Removal:** Single-pole HPF `y[n] = x[n] - x[n-1] + R*y[n-1]`.
* **Progress Reporting:** Elapsed frames vs total frames, main-actor updates.

---

## Settings Reference

| Setting                   | Source          | Purpose                              |
| ------------------------- | --------------- | ------------------------------------ |
| `bufferSize`              | `SettingsView`  | Must be applied to HAL via CA bridge |
| `measuredLatencySamples`  | Latency service | Compensation offset                  |
| `measuredNoiseFloorDb`    | Latency service | Stop point in reverb mode            |
| `useReverbMode`           | Toggle          | Extends capture                      |
| `noiseFloorMarginPercent` | Slider          | Stop margin                          |
| `silenceBetweenFilesMs`   | Slider          | Gap between previews                 |
| `trimEnabled`             | Toggle          | Enables silence trimming             |
| `dcRemovalEnabled`        | Toggle          | High-pass filter                     |
| `thresholdDb`             | Slider          | Manual trim threshold                |
| `outputFolderPath`        | Folder picker   | Output destination                   |
| `outputPostfix`           | Text field      | Output naming                        |

---

## Error Handling & Logging

* Errors bubble as `LocalizedError`.
* `AudioProcessingError.interfaceDisconnected` aborts batches.
* `StatusLogView` logs all events chronologically.

---

## Pending Work & Roadmap

1. **Core Playback/Recording:** Implement real engine path and HAL buffer control.
2. **Latency Measurement:** Replace simulation, integrate CA latency info.
3. **Processing Pipeline:** Implement trimming, callbacks, robust error handling.
4. **Advanced Features:** Reverb-stop logic, seamless preview, job queue service, metadata stamping, verbose logging.

---

## Testing & Diagnostics

* Unit-test trimming and latency math.
* Integration-test latency with hardware loopback.
* UI tests for state transitions.
* Diagnostic mode should dump CAAudioHardware routing and latency info.

---

## Build & Deployment Notes

* Requires microphone permission (`NSMicrophoneUsageDescription`).
* Ensure signing profile includes audio input entitlement.
* Bridge layer requires Objective-C++ compilation (`.mm`) and proper bridging header inclusion.

---

## Glossary

* **Latency Samples:** Frames between playback and impulse return.
* **Noise Floor:** Background RMS in dBFS.
* **DC Offset:** Mean drift in signal; removal prevents headroom loss.

---

Would you like me to generate a sample `CAAudioHardwareBridge.mm` and Swift wrapper to include directly in your project structure? (That would help your agents start wiring it in immediately.)
