# CA Audio Hardware Integration Guide

This document captures the patterns we standardised while migrating **F9 Batch Resampler** to the [CA Audio Hardware](https://github.com/sbooth/CAAudioHardware) stack. Treat it as the playbook for future projects that need deterministic, multichannel Core Audio control from Swift.

---

## 1. Package Layout & Dependencies

- Vendor both packages as Git submodules to avoid network fetches:
  - `Vendor/CAAudioHardware` @ `d927963dcfbb819da6ed6f17b83f17ffbc689280`
  - `Vendor/CoreAudioExtensions` @ `6786ff0074ae44e6c1c053d113218aeca47a acc`
- Add them via *File ▸ Add Packages… ▸ Add Local…*, link the products `CAAudioHardware` and `CoreAudioExtensions` to the app target.
- Keep the vendor setup instructions in `Docs/CA_AUDIO_VENDOR_SETUP.md` synced whenever you bump commits.

---

## 2. Architecture Overview

```
MainViewModel ──▶ LatencyMeasurementService
                │   AudioProcessingService
                │       │
                ▼       ▼
       CAAudioHardwareSystem  ──▶  CAAudioBridgeProtocol
                                             │
                              ┌──────────────┴───────────────┐
                              │                              │
                CAAudioHardwareBridge (real HAL)   CAAudioHardwareStubBridge (sim)
```

- **CAAudioHardwareSystem** wraps the bridge and keeps device lists, stream tokens, and callbacks on the main actor.
- **CAAudioHardwareBridge**
  - Enumerates `CAAudioHardware.AudioDevice` objects, converts them into plain descriptors for the UI.
  - Configures buffer size, sample rate, and stream formats (32-bit float, non-interleaved) before starting the IOProc.
  - Maps selected channel IDs to actual HAL streams and sets `AudioHardwareIOProcStreamUsage` so only the requested busses run.
  - Handles buffer marshaling between HAL’s planar format and the app’s interleaved callbacks.
- **Stub Bridge** generates deterministic sine-wave data and keeps API-compatible scaffolding for automated tests or environments without hardware.

---

## 3. Key Code Files

| Path | Responsibility |
| ---- | -------------- |
| `Services/CAAudioBridge.swift` | Defines `CAAudioBridgeProtocol`, the stub, and the real bridge implementation. |
| `Services/CAAudioHardwareSystem.swift` | Owns bridge selection, exposes `initialize`, `start`, `stop`, `currentSampleTime`, and propagates channel/buffer selections. |
| `Services/LatencyMeasurementService.swift` | Requests a specific device/pair/buffer from the system, captures IOProc data to locate the hardware impulse. |
| `Services/AudioProcessingService.swift` | Uses the same system callbacks to push batch playback and capture results. |
| `Docs/CA_AUDIO_VENDOR_SETUP.md` | Hands-on instructions for vendoring the packages. |
| `Docs/PHASE3_IMPLEMENTATION_NOTES.md` | Lessons learned from wiring the IOProc. |

---

## 4. Usage Pattern

1. **Enumerate Devices**
   ```swift
   await system.loadAvailableDevices()
   // `system.availableDevices` now contains UI-friendly descriptors.
   ```

2. **Initialise the Hardware Loop**
   ```swift
   try await system.initialize(
       deviceUID: selectedPair.deviceUID!,
       inputChannels: selectedInput.channels,
       outputChannels: selectedOutput.channels,
       bufferSize: UInt32(settings.bufferSize.rawValue)
   )
   ```

3. **Register IO Callbacks**
   ```swift
   system.setInputCallback { buffer, frames, channels in … }  // process hardware return
   system.setOutputCallback { buffer, frames, channels in … } // fill playback payload
   ```

4. **Start / Stop**
   ```swift
   try system.start()
   … // run measurement or batch processing
   system.stop()   // automatically tears down IOProc & stream usage
   ```

---

## 5. Channel Mapping & Stream Usage

- We pass channel indices all the way from the UI into `CAAudioStreamConfiguration`.
- `CAAudioHardwareBridge`:
  - Builds lookup tables from channel number → scratch buffer offset.
  - Aggregates `AudioDevice.streams(inScope:)` to capture both input/output halves.
  - Allocates `AudioHardwareIOProcStreamUsage`, marks streams as on/off based on whether the user-selected channels fall within the stream’s `startingChannel ... startingChannel + mChannelsPerFrame`.
  - Marshal planar → interleaved (input) and interleaved → planar (output) inside the IOProc.

If you see “No impulse detected”:
- Confirm `CAAudioBridge` logs the real bridge (`CAAudioHardwareBridge`) at launch.
- Verify selected channel numbers match the device’s physical layout.
- Use the stub bridge (`makeDefaultCAAudioBridge()` toggled manually) to sanity-check the callback pipeline without hardware.

---

## 6. Testing & Diagnostics

- **SineWaveGenerator** and **HardwareLoopTestService** provide synthetic data for rapid loopback testing without external gear.
- `getCurrentSampleTime()` exposes HAL sample counters so you can log clock behaviour during long captures.
- Add temporary logging in the IOProc to print peak levels or stream enablement when bringing up new hardware.

---

## 7. Future Enhancements

- Replace the per-buffer interleaving copies with channel-remap vectors or ring buffers once profiling data is available.
- Promote stream usage & device configuration utilities into shared helper types if multiple apps will consume them.
- Build automated loopback diagnostics (impulse detection + noise floor) via the stub generator to catch regressions.

---

## 8. Checklist for New Projects

1. Vendor CA Audio Hardware + CoreAudioExtensions (`Docs/CA_AUDIO_VENDOR_SETUP.md`).
2. Add package products to the Xcode target and confirm `CAAudioHardwareBridge` is selected at runtime.
3. Copy `CAAudioBridge.swift` / `CAAudioHardwareSystem.swift` as starting points; adjust buffer size & sample rate strategy to your requirements.
4. Implement service layers following the callback template; keep all HAL access inside the bridge/system duo.
5. Provide a stub bridge or generator for deterministic testing.
6. Document device channel layouts as early as possible—there is no substitute for verifying with real hardware.

With this setup, we have predictable multichannel IO, reliable channel selection, and a clean separation between UI state and low-level HAL control—all using pure Swift. Reuse the pattern wholesale for any future hardware-slinging projects.
