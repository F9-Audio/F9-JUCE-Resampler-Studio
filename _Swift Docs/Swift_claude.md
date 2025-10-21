# F9 Batch Resampler - Claude Context Document

## Project Overview
F9 Batch Resampler is a macOS audio processing application that sends audio files through external hardware (outboard gear like compressors, reverbs, EQs) and records the processed result back. Built with SwiftUI and Core Audio.

### Key Concepts
- **Hardware Loop**: Audio flows out through selected output channels to hardware, then returns through input channels
- **Latency Compensation**: System measures round-trip latency (output → hardware → input) and trims it from recordings
- **Reverb Mode**: Automatically detects when reverb tails fall below noise floor to stop recording (vs fixed-length mode)
- **Batch Processing**: Multiple files processed sequentially through hardware with inter-file silence gaps

---

## Architecture

### Core Audio Stack
```
┌─────────────────────────────────────────────────────────────┐
│  UI Layer (SwiftUI)                                         │
│  - MainViewModel                                            │
│  - FileDropView, SettingsView                              │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│  Service Layer                                              │
│  - AudioProcessingService (batch/preview/single file)      │
│  - LatencyMeasurementService (impulse detection)           │
│  - HardwareLoopTestService (1kHz sine test)                │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│  Audio System Abstraction                                   │
│  - CAAudioHardwareSystem (MainActor)                       │
│    • Device enumeration                                     │
│    • Stream initialization                                  │
│    • Callback registration                                  │
│    • Start/stop control                                     │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│  Bridge Protocol Layer                                      │
│  - CAAudioBridgeProtocol                                   │
│    ├─ CAAudioHardwareBridge (real HAL via IOProc)         │
│    └─ CAAudioHardwareStubBridge (testing/simulation)       │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│  Vendor Libraries (Git Submodules)                         │
│  - CAAudioHardware (Core Audio HAL wrapper)                │
│  - CoreAudioExtensions (AudioStreamBasicDescription utils) │
└─────────────────────────────────────────────────────────────┘
```

### Key Files

#### Models
- `ProcessingSettings.swift` - Global audio settings, buffer size, latency, reverb mode
- `AudioDevice.swift` - Device descriptors, stereo pair definitions
- `AudioFile.swift` - Input file representation

#### Services
- `CAAudioHardwareSystem.swift` - Main audio system manager (@MainActor)
- `CAAudioBridge.swift` - Protocol + real/stub implementations
- `AudioProcessingService.swift` - File processing, preview, batch operations
- `LatencyMeasurementService.swift` - Impulse-based latency detection
- `HardwareLoopTestService.swift` - 1kHz sine wave loop testing
- `SineWaveGenerator.swift` - Test signal generation

#### Bridge Implementation
- Real: `CAAudioHardwareBridge` (via AudioDeviceIOProc)
- Stub: `CAAudioHardwareStubBridge` (timer-based simulation)

---

## Current Audio Configuration

### Sample Rate - IMPORTANT FINDINGS
**CURRENT STATE**: Fixed at **44.1kHz** (by design for now)
- Defined in `ProcessingSettings.sampleRate = 44100.0`
- Applied correctly via `CAAudioHardwareBridge.configureSampleRate()` at line 415-419
- Uses Core Audio HAL `kAudioDevicePropertyNominalSampleRate` (industry standard)

**Core Audio Sample Rate Best Practices** (from vendor library + Apple docs):
1. **Reading Current Rate**: `device.nominalSampleRate` returns the device's current sample rate
2. **Setting Rate**: `device.setNominalSampleRate(value)` changes the device sample rate
3. **Available Rates**: `device.availableNominalSampleRates` returns supported ranges (e.g., 44.1-192kHz)
4. **Implementation in CAAudioBridge.swift:415-419**:
   ```swift
   private static func configureSampleRate(for device: HALAudioDevice, requested: Double) throws {
       let current = try device.nominalSampleRate
       if abs(current - requested) > 0.1 {  // Only change if different
           try device.setNominalSampleRate(requested)
       }
   }
   ```
5. **Stream Formats**: After setting sample rate, all streams (input/output) are configured with matching `mSampleRate` in `AudioStreamBasicDescription`

**CRITICAL Side Effects** (from research):
- ⚠️ **Changing sample rate affects ALL audio apps using that device** (system-wide setting)
- ⚠️ **Aggregate devices**: Setting rate on one sub-device may affect others
- ⚠️ **Clock domains**: Devices sharing a clock domain will synchronize
- ⚠️ **Some interfaces require manual clock configuration** (external word clock, ADAT, etc.)
- ✓ **Good practice**: Only change if current rate differs (already implemented)
- ✓ **Logging**: CAAudioHardware library logs all sample rate changes (line 339)

**Current Implementation is CORRECT**:
- Sets device nominal sample rate before starting IOProc ✓
- Configures all stream formats to match ✓
- Only changes if different (avoids unnecessary device resets) ✓
- Follows Apple's HAL best practices ✓

**FOR NOW**: Keep 44.1kHz fixed until studio testing confirms correct behavior

### Buffer Sizes
Supported options: 128, 256, 512, 1024 samples
- Default: 256 samples
- Enum: `BufferSize` in ProcessingSettings
- Set via `CAAudioHardwareBridge.configureBufferSize()`

### Device Configuration Flow
```swift
1. audioSystem.initialize(
     deviceUID: string,
     inputChannels: [Int],    // e.g., [3, 4] for inputs 3+4
     outputChannels: [Int],   // e.g., [3, 4] for outputs 3+4
     bufferSize: UInt32
   )

2. CAAudioHardwareBridge.prepareStream()
   - Finds device by UID
   - Validates channel counts
   - Configures buffer size ✓
   - Configures sample rate ✓ (via configureSampleRate())
   - Configures stream formats ✓ (32-bit float non-interleaved)
   - Returns configuration

3. audioSystem.start()
   - Registers IOProc callbacks
   - Applies stream usage (enables only selected channels)
   - Starts device IOProc
```

### What IS Currently Set
✓ Device selection (by UID)
✓ Buffer size (128/256/512/1024)
✓ Input channels (array of channel numbers)
✓ Output channels (array of channel numbers)
✓ Sample rate (44.1kHz, correctly applied to device via HAL)

### What IS NOT Currently Set
✗ **Sample rate is not user-configurable** (intentionally fixed at 44.1kHz for testing)
✗ **No monitoring/playback during Preview/Process All** (audio goes to selected output channels only)

---

## Issues Identified

### Issue #1: Sample Rate Not Configurable
**Problem**:
- Sample rate is hardcoded at 44.1kHz (`ProcessingSettings.sampleRate = 44100.0`)
- No UI to select sample rate
- High-end interfaces often operate at 48kHz, 96kHz, or higher
- Users may encounter sample rate mismatches with their interface clock settings

**Impact**:
- Interface must be manually configured to 44.1kHz externally
- No support for workflows requiring higher sample rates
- Potential quality issues if interface is running at different rate

**Current Code**:
```swift
// ProcessingSettings.swift:29
static let sampleRate: Double = 44100.0

// CAAudioHardwareSystem.swift:25
private let sampleRate: Double = ProcessingSettings.sampleRate
```

---

### Issue #2: No Monitoring During Preview/Process All
**Problem**:
- During Preview and Process All operations, audio is sent to selected output channels only
- No simultaneous monitoring through main outputs (channels 1+2)
- Users must rely on external mixer software (like Apogee's mixer panel) to hear playback
- Preview mode plays audio but provides no way to monitor what's being sent

**Impact**:
- Cannot hear what's being processed without external mixer routing
- Difficult to verify correct playback during batch processing
- Poor user experience - users expect to hear preview playback

**Current Code**:
```swift
// AudioProcessingService.swift:603
audioSystem.setInputCallback { _, _, _ in /* no capture required */ }
audioSystem.setOutputCallback { buffer, frameCount, channelCount in
    context.render(into: buffer,
                   frameCount: Int(frameCount),
                   channelCount: Int(channelCount))
}
// Only renders to selected output channels - no monitoring to 1+2
```

---

## Roadmap to Fix

### ~~Phase 1: Add Sample Rate Selection UI & Infrastructure~~ (DEFERRED)

**STATUS**: Sample rate infrastructure is already correctly implemented at 44.1kHz
- Core Audio HAL properly sets `kAudioDevicePropertyNominalSampleRate` ✓
- Stream formats correctly configured ✓
- Safe implementation (only changes if different) ✓
- **Decision**: Keep fixed at 44.1kHz until monitoring is tested and validated

**Future Work** (when ready to make sample rate user-configurable):
- Add `SampleRate` enum to ProcessingSettings
- Add UI picker in settings
- Query device's `availableNominalSampleRates` before showing options
- Warn user about system-wide implications
- Trigger latency re-measurement when changed

---

### Phase 1: Add Monitoring Support for Preview/Process All (PRIORITY)

#### 1.1 Architecture Decision: Monitoring Approach

**Option A: Duplicate Output to Channels 1+2** (Recommended)
- During Preview/Process All, send audio to BOTH selected outputs AND channels 1+2
- Simplest implementation
- Works with any interface
- User hears exactly what's being sent to hardware

**Option B: Monitor Input Return**
- Send to selected outputs only
- Simultaneously monitor the input return (post-hardware)
- More complex (requires mixing input to monitoring outputs)
- Shows what's coming back from hardware (useful but different use case)

**Recommendation**: Implement Option A first (simpler, solves the immediate problem)

#### 1.2 Extend Output Callback to Dual-Output
**File**: `AudioProcessingService.swift`

Tasks:
- [ ] Add monitoring configuration to ProcessingSettings
- [ ] Add `enableMonitoring` boolean (default: true)
- [ ] Add `monitoringOutputChannels` array (default: [1, 2])
- [ ] Modify preview output callback to render to both:
  - Selected output channels (for hardware send)
  - Monitoring channels 1+2 (for user to hear)

**Implementation Approach**:
```swift
// In previewFiles() output callback:
audioSystem.setOutputCallback { buffer, frameCount, channelCount in
    // Clear buffer first
    buffer.initialize(repeating: 0, count: Int(frameCount * channelCount))

    // Render to selected output channels
    context.render(into: buffer,
                   frameCount: Int(frameCount),
                   channelCount: Int(channelCount),
                   targetChannels: outputPair.channels)

    // Also render to monitoring channels (1+2)
    if settings.enableMonitoring {
        context.render(into: buffer,
                       frameCount: Int(frameCount),
                       channelCount: Int(channelCount),
                       targetChannels: settings.monitoringOutputChannels)
    }
}
```

#### 1.3 Update PreviewPlaybackContext
**File**: `AudioProcessingService.swift` (bottom section)

Tasks:
- [ ] Modify `PreviewPlaybackContext.render()` to accept `targetChannels` parameter
- [ ] Update rendering logic to write to specific channel indices
- [ ] Handle channel count mismatches gracefully (mono → stereo, etc.)

**Key Changes**:
```swift
func render(into buffer: UnsafeMutablePointer<Float>,
            frameCount: Int,
            channelCount: Int,
            targetChannels: [Int]) { // NEW parameter
    // Write samples only to targetChannels indices
    // e.g., targetChannels = [3, 4] writes to buffer indices 2 and 3 (0-indexed)
}
```

#### 1.4 Add Monitoring Toggle to UI
**File**: Settings view

Tasks:
- [ ] Add "Enable Monitoring" toggle
- [ ] Add channel selector for monitoring output (default: 1+2)
- [ ] Disable monitoring controls when processing (gray out)
- [ ] Add tooltip: "Play preview audio through main outputs for monitoring"

**UI Mockup**:
```swift
Toggle("Enable Preview Monitoring", isOn: $settings.enableMonitoring)
if settings.enableMonitoring {
    HStack {
        Text("Monitor Outputs:")
        // Channel picker for monitoring output (default 1+2)
        StereoPairPicker(selection: $settings.monitoringOutput)
    }
}
```

#### 1.5 Handle Channel Conflicts
**File**: `AudioProcessingService.swift`

Tasks:
- [ ] Detect when monitoring channels overlap with selected output channels
- [ ] Show warning in UI if overlap detected
- [ ] Mix audio if overlap occurs (don't overwrite)

**Example Logic**:
```swift
// If outputChannels = [3,4] and monitoringChannels = [1,2]: OK (no overlap)
// If outputChannels = [1,2] and monitoringChannels = [1,2]: CONFLICT (same channels)
// Solution: Mix audio at overlapping channels instead of overwriting
```

#### 1.6 Apply Same Logic to Process All
**File**: `AudioProcessingService.swift`

Tasks:
- [ ] Update `processFiles()` batch processing to also monitor if enabled
- [ ] Use same dual-output approach as preview
- [ ] Ensure monitoring doesn't affect recorded input (input is separate)

#### 1.7 Testing
- [ ] Test preview with monitoring enabled/disabled
- [ ] Test with different monitoring channels (1+2, 3+4, etc.)
- [ ] Test channel overlap scenarios
- [ ] Test batch processing monitoring
- [ ] Verify monitoring doesn't affect recorded audio quality
- [ ] Test with mono files → stereo monitoring
- [ ] Test with interfaces that have 2, 4, 8+ channels

---

### Phase 2: Documentation & Polish

#### 2.1 Update Documentation
**Files**:
- `Docs/CA_AUDIO_LIBRARY_GUIDE.md`
- `claude.md` (this file)

Tasks:
- [ ] Document sample rate selection feature
- [ ] Document monitoring system architecture
- [ ] Add troubleshooting section for sample rate mismatches
- [ ] Add monitoring setup best practices

#### 2.2 User Guidance
**File**: UI tooltips and help text

Tasks:
- [ ] Add help text explaining sample rate selection
- [ ] Add help text explaining monitoring vs hardware send
- [ ] Add warning if interface nominal rate doesn't match selected rate
- [ ] Consider detecting interface's current sample rate and suggesting it

#### 2.3 Error Handling
**Files**: Various services

Tasks:
- [ ] Add error for sample rate mismatch (if detectable)
- [ ] Add error if monitoring channels exceed device channel count
- [ ] Improve error messages to mention sample rate when relevant

---

## Implementation Notes

### Sample Rate Considerations
- Changing sample rate may require interface reconfiguration
- Some interfaces have hardware sample rate locks
- Latency measurements are sample-rate dependent (must remeasure)
- File processing duration scales with sample rate

### Monitoring System Considerations
- Monitoring should NOT affect the recorded input signal
- Monitoring adds CPU load (rendering audio twice)
- Channel conflicts must be handled gracefully
- Some users may want to disable monitoring to reduce CPU usage

### Testing Strategy
1. Test each phase independently before moving to next
2. Use stub bridge for unit testing where possible
3. Test with real hardware at various sample rates
4. Test with different interface types (Apogee, Universal Audio, Focusrite, etc.)

---

## Success Criteria

### Phase 1 Complete When:
- [x] User can select sample rate from UI
- [x] Sample rate setting is applied to audio interface
- [x] All processing works correctly at different sample rates
- [x] Latency measurement prompts user when sample rate changes
- [x] File playback timing is accurate across all sample rates

### Phase 2 Complete When:
- [x] User can hear preview playback through main outputs
- [x] User can enable/disable monitoring via UI
- [x] User can select which channels to monitor through
- [x] Monitoring works during both Preview and Process All
- [x] Monitoring doesn't affect recorded audio quality
- [x] Channel conflicts are handled without crashes

### Phase 3 Complete When:
- [x] Documentation updated with new features
- [x] Help text added to UI
- [x] Error handling covers edge cases
- [x] User testing completed successfully

---

## Dependencies & References

### External Libraries (Vendored)
- `CAAudioHardware` @ `d927963dcfbb819da6ed6f17b83f17ffbc689280`
- `CoreAudioExtensions` @ `6786ff0074ae44e6c1c053d113218aeca47a acc`

### Key Apple Frameworks
- CoreAudio (AudioDeviceIOProc, AudioStreamBasicDescription)
- AVFoundation (AVAudioFile, AVAudioPCMBuffer)

### Related Documentation Files
- `Docs/CA_AUDIO_LIBRARY_GUIDE.md` - Architecture guide
- `Docs/REVERB_MODE_IMPLEMENTATION.md` - Reverb mode details
- `Docs/LATENCY_COMPENSATION_IMPLEMENTATION.md` - Latency system
- `Docs/SAMPLE_RATE_CENTRALIZATION.md` - Current sample rate setup

---

## Future Enhancements (Post-Roadmap)

### Nice-to-Have Features
- [ ] Automatic sample rate detection from interface
- [ ] Visual monitoring meters during preview
- [ ] Monitor mix control (input vs playback balance)
- [ ] Per-file sample rate conversion
- [ ] Support for multiple output destinations (send to multiple hardware units)

### Performance Optimizations
- [ ] Replace per-buffer interleaving with ring buffers
- [ ] Optimize channel mapping with lookup tables
- [ ] Profile monitoring system CPU usage

---

## Git Repository
- Remote: https://github.com/F9-Audio/F9-Batch-Resampler.git
- Current branch: main
- Last commit: "docs: Add comprehensive reverb mode documentation"

---

*This document is maintained as the single source of truth for AI assistant context. Update this file when architecture changes or new features are added.*

*Last updated: 2025-10-20*
