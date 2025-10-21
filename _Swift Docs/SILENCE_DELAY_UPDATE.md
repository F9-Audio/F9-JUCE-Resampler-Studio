# Silence Between Files - Implementation Summary

## Changes Made

### 1. Default Value Update
**File**: [ProcessingSettings.swift](F9-Batch-Resampler/Models/ProcessingSettings.swift#L41)

Changed default silence between files from **500ms to 150ms**:
```swift
var silenceBetweenFilesMs: Int = 150 // Gap between files in preview/processing (for compressor/limiter reset)
```

**Rationale**:
- 150ms is sufficient for most compressor/limiter gain reduction to reset
- Shorter delay = faster workflow for drum sampling
- Still long enough to prevent transient interactions between samples

---

### 2. Batch Processing Delay Implementation
**File**: [MainViewModel.swift](F9-Batch-Resampler/ViewModels/MainViewModel.swift#L219-L223)

Added delay between files during batch processing:
```swift
// Add silence between files to allow time-domain processing (compressors/limiters) to reset
if index < filesToProcess.count - 1 { // Don't wait after the last file
    let delayNanoseconds = UInt64(settings.silenceBetweenFilesMs) * 1_000_000
    try await Task.sleep(nanoseconds: delayNanoseconds)
}
```

**Implementation Details**:
- Delay is inserted **after** each file completes (except the last one)
- Uses `Task.sleep()` for async/await compatibility
- Respects user-configurable `silenceBetweenFilesMs` setting

---

## Where the Delay is Used

### ✅ Preview Mode
**File**: [AudioProcessingService.swift](F9-Batch-Resampler/Services/AudioProcessingService.swift#L223)

The preview system already uses this setting:
```swift
let silenceFrames = Int(Double(settings.silenceBetweenFilesMs) / 1000.0 * sampleRate)
```

The `PreviewPlaybackContext` inserts silence between files in the audio callback (line 398):
```swift
if frameOffset >= item.frameCount {
    frameOffset = 0
    silenceRemaining = silenceFrames  // Inserts silence here
    currentItemIndex = (currentItemIndex + 1) % items.count
    onFileChange?(currentItemIndex)
}
```

### ✅ Batch Processing Mode
**File**: [MainViewModel.swift](F9-Batch-Resampler/ViewModels/MainViewModel.swift#L219-L223)

Now implemented with this update. The delay happens **between** individual file processing operations.

---

## User Control

### Settings UI
**File**: [SettingsView.swift](F9-Batch-Resampler/Views/SettingsView.swift#L181-L190)

Users can adjust the delay via a slider:
- **Range**: 0ms to 2000ms
- **Step**: 100ms increments
- **Default**: 150ms
- **Display**: Real-time value shown in milliseconds

```swift
HStack {
    Text("Silence between files:")
    Spacer()
    Text("\(vm.settings.silenceBetweenFilesMs) ms")
        .monospaced()
}

Slider(value: Binding(
    get: { Double(vm.settings.silenceBetweenFilesMs) },
    set: { vm.settings.silenceBetweenFilesMs = Int($0) }
), in: 0...2000, step: 100)
```

---

## Use Cases

### Why This Matters for Drum Processing

When processing drum samples through hardware compressors/limiters:

1. **Compressor Attack/Release**:
   - A compressor applies gain reduction when a transient hits
   - The release time determines how long it takes for gain to return to unity
   - Without silence between samples, the compressor may still be in release phase when the next sample plays
   - This creates inconsistent processing across samples

2. **Limiter Reset**:
   - Limiters need time to release gain reduction after a peak
   - Consecutive samples without spacing = inconsistent peak limiting
   - 150ms is typically sufficient for most limiter release times

3. **Example Workflow**:
   ```
   Sample 1 (kick) → 150ms silence → Sample 2 (snare) → 150ms silence → Sample 3 (hi-hat)
   ```

   Each sample gets identical processing because the compressor/limiter starts from the same state.

4. **Adjustability**:
   - Fast compressors with short release: Use 50-100ms
   - Slow vintage compressors: Use 300-500ms
   - Reverb/delay processing: Use longer delays (1000-2000ms)

---

## Technical Implementation Notes

### Preview Mode (Real-time Audio Callback)
- Silence is inserted **within the audio callback**
- Silence frames calculated based on sample rate: `silenceMs / 1000.0 * sampleRate`
- Zero samples written to audio buffer during silence period
- No CPU usage during silence (just writes zeros)

### Batch Processing Mode (File-by-File)
- Silence is a **system sleep** between file operations
- Allows audio interface hardware to settle
- Prevents buffer overruns from rapid restarts
- Also beneficial for USB/Thunderbolt devices that need time between stops/starts

---

## Build Status
✅ **Build Successful** - No errors or critical warnings

---

## Future Enhancements

### Potential Improvements:
1. **Per-file delay override**: Allow different delays for specific samples
2. **Auto-detect compressor release**: Analyze output to suggest optimal delay
3. **Presets**: Common delay presets (Fast Comp: 100ms, Vintage: 500ms, etc.)
4. **Visual feedback**: Show countdown timer during delay in UI
5. **Delay randomization**: Slight random variation (+/- 10ms) for more "natural" feel

---

**Last Updated**: 2025-10-19
**Status**: Implemented and tested
