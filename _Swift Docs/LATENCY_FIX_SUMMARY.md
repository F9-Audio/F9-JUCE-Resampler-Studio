# Latency Math Fix - Summary

## Date: 2025-10-19

---

## What Was Fixed

Two critical bugs in latency compensation math that were causing:
1. **Trimming double the correct amount** from the beginning of processed audio
2. **Recording longer than necessary** due to incorrect latency units

---

## The Root Cause

`measuredLatencySamples` from [LatencyMeasurementService](F9-Batch-Resampler/Services/LatencyMeasurementService.swift) returns an **interleaved sample index**, not a frame count.

**Example** (Stereo, 512 frames of latency):
- Interleaved audio: `[L0, R0, L1, R1, ..., L511, R511, L512, R512, ...]`
- Peak detected at frame 512 (left or right channel)
- `measuredLatencySamples = 1024` (interleaved sample index)

The code was treating this as **frames** in some places and **interleaved samples** in others, causing inconsistent calculations.

---

## Fix #1: Trimming Math

### Before (WRONG ❌)
```swift
let latencySamples = measuredLatencySamples * inputChannelCount
// For stereo: 1024 * 2 = 2048 samples
// This trims 1024 FRAMES instead of 512 FRAMES!
```

### After (CORRECT ✅)
```swift
// NOTE: measuredLatencySamples is already in interleaved samples (not frames)
// so we don't multiply by channel count again
let latencySamples = measuredLatencySamples
// For stereo: 1024 samples = 512 frames ✅
```

**Impact**: Audio now starts at the correct position, not ~11.6ms too late

---

## Fix #2: Recording Length Calculation

### Before (WRONG ❌)
```swift
let targetRecordingFrames = settings.recordingLength(
    sourceFileSamples: frameCount,
    latencySamples: measuredLatencySamples  // 1024 (interleaved)
)
// recordingLength() expects FRAMES but receives INTERLEAVED SAMPLES
// Returns: 44,100 + 1,024 + 4,096 = 49,220 frames (TOO LONG!)
```

### After (CORRECT ✅)
```swift
// Convert measured latency from interleaved samples to frames
let latencyFrames = measuredLatencySamples / inputChannelCount
let targetRecordingFrames = settings.recordingLength(
    sourceFileSamples: frameCount,
    latencySamples: latencyFrames  // 512 (frames)
)
// Returns: 44,100 + 512 + 2,048 = 46,660 frames ✅
```

**Impact**:
- Correct recording length (saves processing time and memory)
- Proper safety buffer calculation

---

## Example: Before vs After

**Scenario**: Stereo recording, 512 frames of actual latency, 1-second source file

| Metric | Before (Buggy) | After (Fixed) | Unit |
|--------|---------------|---------------|------|
| Measured latency | 1,024 | 1,024 | interleaved samples |
| Latency in frames | N/A (bug!) | 512 | frames |
| Target recording | 49,220 | 46,660 | frames |
| Target recording | 98,440 | 93,320 | interleaved samples |
| Trimmed amount | 2,048 | 1,024 | interleaved samples |
| Trimmed frames | 1,024 ❌ | 512 ✅ | frames |
| Output length | 44,100 | 44,100 | frames |
| Alignment error | ~11.6ms late ❌ | Perfect ✅ | |

---

## Files Modified

### [AudioProcessingService.swift](F9-Batch-Resampler/Services/AudioProcessingService.swift)

**4 locations updated**:

1. **Lines 171-176** (processFile - recording length)
2. **Lines 239-243** (processFile - trimming)
3. **Lines 335-340** (processFileWithInitializedSystem - recording length)
4. **Lines 401-405** (processFileWithInitializedSystem - trimming)

---

## Build Status

✅ **BUILD SUCCEEDED**

No errors, no warnings

---

## Ready for Testing

The latency math is now correct. You should test with:

1. **Real hardware** (RME UFX III)
2. **Drum samples** with sharp transients
3. **Different buffer sizes** (128, 256, 512, 1024)
4. **Verify**:
   - Output files match source length exactly
   - Transients align with source (no time shift)
   - Console log shows correct sample counts

**Example console output to expect**:
```
Latency measured: 1024 samples (512 frames, 11.61 ms @ 44.1kHz)
Target recording: 46660 frames (93320 samples for stereo)
Trim: Captured 93320 samples, trimmed 1024 samples, output 88200 samples
Output: 44100 frames ✅
```

---

**Last Updated**: 2025-10-19
**Status**: ✅ Complete - Both math bugs fixed
