# Windows Porting Strategy for F9 Batch Resampler

This guide outlines the key decisions, tooling, and implementation steps required to bring the macOS-only **F9 Batch Resampler** project to Windows. It assumes you are starting from the public GitHub repository after the Core Audio (HAL) rewrite and that you want to preserve the project’s high-level behaviour: batch hardware loop processing, latency measurement, preview playback, and the existing UI/UX patterns.

---

## 1. Platform Targets & Language Choices

| Layer | Current Implementation | Windows Recommendation | Notes |
|-------|------------------------|------------------------|-------|
| Audio engine | Swift + CA Audio Hardware (Core Audio HAL) | ✅ **C++17/20** with JUCE or PortAudio/RtAudio + WASAPI ASIO wrappers | Gives low-level device routing, multichannel access, and cross-platform future proofing. |
| UI | SwiftUI app shell | ✅ **C++ JUCE GUI** or **C#/.NET WPF/WinUI** (if engine wrapped) | JUCE allows single-language stack; C# offers faster UI dev if engine exposed as DLL. |
| Business logic | Swift (ViewModels, services) | ✅ Mirror structure in chosen language; consider MVVM on C# side or Model/Controller classes in C++ | Keep same responsibilities (device enumeration, file queues, logging). |
| Preview/Processing pipeline | Custom Swift bridging over HAL | ✅ Re-implement using the selected audio framework’s callback model | Re-use buffer scheduling approach (playback, capture, per-file silence). |
| Packaging | Xcode app bundle | ✅ MSIX installer or traditional EXE distribution | Include ASIO redistributables if needed. |

**Why C++ with JUCE?**  
JUCE (https://juce.com/) provides:
- Cross-platform audio I/O (ASIO, WASAPI, DirectSound) with explicit per-device channel routing.
- High-level `AudioDeviceManager`, `AudioTransportSource`, `AudioIODeviceCallback` APIs similar to what the Core Audio IOProc handled.
- UI toolkit (optional).  
Alternative: **PortAudio + RtAudio** (https://www.portaudio.com/, https://www.rtmidi.org/) if you prefer lighter dependency + custom UI (e.g., .NET, Qt).

---

## 2. Audio Layer Migration Plan

### 2.1 Device & Channel Enumeration
1. Use `AudioDeviceManager::getAvailableDeviceTypes()` (JUCE) or `Pa_GetDeviceInfo` (PortAudio) to list devices.
2. Map to a `DeviceDescriptor` struct mirroring `CAAudioHardwareDeviceDescriptor` (name, UID, input/output channel counts).
3. Store GUID/endpoint IDs for re-selection at launch (WASAPI) or driver names (ASIO).

### 2.2 Latency Measurement Loop
1. Create a dedicated `AudioIODeviceCallback` or PortAudio stream for latency tests.
2. Stages:
   - Start output stream, inject impulse (single buffer).
   - Record input buffer concurrently; capture timestamp or sample index when impulse crosses threshold.
   - Compute round-trip in samples; apply device sample rate from stream configuration.
3. Save `noiseFloor` and `latencySamples` into settings JSON.

### 2.3 Batch Processing
1. Build a playback/capture engine:
   - Playback: push PCM buffers to output device (non-interleaved or interleaved per API).
   - Capture: append incoming buffers to ring buffer/vector for each channel pair.
2. Apply the existing post-processing (DC removal, trimming) once capture finishes.
3. Export WAV using a library like libsndfile or JUCE’s `AudioFormatWriter`.
4. Honor the `silenceBetweenFilesMs` delay between items by scheduling zero buffers.

### 2.4 Preview Mode
Reuse the preview scheduler logic already implemented in Swift:  
- Maintain playlist state, silence frames between files, and progress callbacks.  
- On Windows, convert slider input to frame counts using the active device sample rate reported by JUCE/PortAudio.

---

## 3. UI Options

### A. JUCE Only (Single Language C++)
- Pros: One codebase, integrated audio & GUI, cross-platform (future Linux, macOS, Windows).
- Cons: Need C++ expertise for complex UI; styling may need custom work.

### B. Hybrid: C++ Engine + C# Frontend
- Build the audio layer as a DLL exposing C ABI functions (device enumeration, startPreview, processBatch, etc.).
- Use P/Invoke in a WPF/WinUI shell.
- Pros: Rapid UI iteration, easy data binding (MVVM).
- Cons: More interop glue; must design thread-safe crossing between managed/unmanaged worlds.

### C. Alternative Shells
- If you prefer web tech, embed the engine in a Node/Electron process using native addons (NAP), but that reintroduces audio latency risk. Not recommended for tight-loop hardware work.

---

## 4. Project Structure Proposal

```
F9BatchResampler.Windows/
├─ audio_engine/
│  ├─ CMakeLists.txt              // builds static or shared lib
│  ├─ include/
│  │  └─ EngineAPI.h             // C interface for UI
│  └─ src/
│     ├─ DeviceManager.cpp       // wraps JUCE/PortAudio device APIs
│     ├─ LatencyService.cpp
│     ├─ ProcessingService.cpp
│     ├─ PreviewService.cpp
│     ├─ DSP/Trimming.cpp
│     └─ Utils/WavWriter.cpp
├─ ui/
│  ├─ JUCEApp/ or WPFApp/        // UI project (Visual Studio solution)
│  └─ Resources/
└─ docs/
   └─ BUILD_WINDOWS.md
```

- Use CMake to build the engine; integrate with Visual Studio solution for UI.
- Keep JSON settings in `%APPDATA%\F9BatchResampler\settings.json`.

---

## 5. Conversion Steps Summary

1. **Set up environment**: Install Visual Studio 2022 (with Desktop C++), CMake, JUCE or PortAudio.
2. **Port audio engine**:
   - Recreate Swift services (`CAAudioHardwareSystem`, `AudioProcessingService`, `LatencyMeasurementService`) as C++ classes.
   - Implement `EngineAPI` that mirrors the existing Swift API surface (startPreview, stopPreview, processFile, etc.).
3. **UI rewrite**:
   - Match existing layout (device selection, file list, settings panels, log output).
   - Implement preview & processing commands by calling the engine on background threads.
4. **Testing**:
   - Validate latency measurement accuracy with loopback rig (ASIO & WASAPI exclusive).
   - Test preview/batch with varying buffer sizes and sample rates (44.1kHz baseline).
   - Ensure trimming/DC removal matches macOS output (bit-for-bit on known samples).
5. **Packaging**:
   - Bundle required ASIO drivers or provide installation guidance.
   - Use MSIX or installer builder (Inno Setup, WiX) to deliver dependencies.
6. **Documentation**:
   - Provide BUILD_WINDOWS.md with prerequisites, build commands, and troubleshooting (driver access, sample-rate mismatches).

---

## 6. Additional Considerations

- **Audio Driver Support**: For professional hardware, ASIO is often essential. JUCE handles ASIO out of the box; PortAudio requires ASIO SDK (with Steinberg licensing). If ASIO is unavailable, fall back to WASAPI exclusive mode.
- **Multichannel Safety**: Validate channel enumerations (some drivers expose ADAT/S/PDIF channels with sparse numbering). Provide UI guardrails similar to the macOS split: channel pairs listed 1–2, 3–4, etc.
- **Async Concurrency**: Replace Swift concurrency with `std::future`, `std::async`, or a lightweight task system (e.g., `juce::Thread`, `juce::ThreadPoolJob`). Keep UI updates marshalled onto the main thread (dispatcher in WPF, message thread in JUCE).
- **DSP Parity**: Reuse the algorithms for DC removal and trimming. Port the Swift loops to C++ functions (still O(N) operations). Confirm floating-point precision is consistent (prefer 32-bit floats as on macOS).
- **Logging**: Implement a cross-platform logger (e.g., spdlog) and surface events in the UI log panel.

---

## 7. Recommended Timeline

| Phase | Duration | Deliverables |
|-------|----------|--------------|
| Planning & scaffolding | 1 week | Decide toolchain, set up repo, create stub engine + UI. |
| Audio engine core | 2–3 weeks | Device enumeration, latency measurement, batch playback/record. |
| UI feature parity | 2 weeks | Device selection, file management, preview controls, logging. |
| QA & polish | 1 week | Loopback validation, edge cases, packaging. |

Total: ~6–7 weeks for a solid first Windows release (adjust per team size).

---

## 8. Quick Decision Checklist

- [ ] Choose audio framework (JUCE vs PortAudio) and licensing.
- [ ] Pick UI stack (JUCE GUI or C# WPF) to align with team skillset.
- [ ] Confirm target hardware (ASIO availability, multichannel counts).
- [ ] Define testing hardware loop to replicate macOS validation.
- [ ] Mirror the macOS feature set (preview, batch, trimming, logging).

---

With these steps you can deliver a functionally equivalent Windows build while preserving the workflow your users already know. The C++ + JUCE route keeps everything in one codebase and remains extensible if you later need cross-platform Linux/macOS desktop support. If the team prefers managed UI development, isolate the engine behind a small C API and expose it to a WPF/WinUI front end.
