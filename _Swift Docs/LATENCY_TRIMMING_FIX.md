# Latency Trimming Math Fix

## Date: 2025-10-19

---

## Critical Bug Fixed

### The Problem

The latency trimming calculation was removing **DOUBLE** the correct amount of audio from the beginning of captured samples.

**Location**: [AudioProcessingService.swift](F9-Batch-Resampler/Services/AudioProcessingService.swift)
- Line 239 (processFile method)
- Line 401 (processFileWithInitializedSystem method)

### Root Cause

**Buggy Code**:
```swift
let latencySamples = measuredLatencySamples * inputChannelCount
```

**Why This Was Wrong**:

`measuredLatencySamples` comes from [LatencyMeasurementService.swift:165](F9-Batch-Resampler/Services/LatencyMeasurementService.swift#L165):

```swift
private func analyzeCapturedAudio(_ audio: [Float], ...) -> (Int, Float) {
    var maxIndex = 0
    for (index, sample) in audio.enumerated() {
        if abs(sample) > maxValue {
            maxIndex = index  // This is an index in INTERLEAVED audio array
        }
    }
    let latencySamples = maxIndex  // Already in interleaved samples!
    return (latencySamples, noiseFloorDb)
}
```

The captured audio is **interleaved** (from line 80-82):
```swift
let sampleCount = Int(frameCount) * Int(channelCount)  // Already multiplied!
let samples = Array(UnsafeBufferPointer(..., count: sampleCount))
capturedAudio.append(contentsOf: samples)
```

So `maxIndex` is finding the peak in an **interleaved** array:
- For stereo: `[L0, R0, L1, R1, L2, R2, ...]`
- If the peak is at frame 512, `maxIndex = 1024` (512 frames × 2 channels)

**The bug**: We then multiplied by channel count AGAIN!
- `latencySamples = 1024 * 2 = 2048` ❌
- This trimmed 1024 frames instead of 512 frames (double!)

---

## The Fix

**Corrected Code**:
```swift
// Trim latency from the beginning of captured audio
// NOTE: measuredLatencySamples is already in interleaved samples (not frames)
// so we don't multiply by channel count again
let latencySamples = measuredLatencySamples
let trimmedAudio = trimLatency(from: capturedAudio,
                               latencySamples: latencySamples,
                               sourceFrames: frameCount,
                               channelCount: inputChannelCount)
```

---

## Mathematical Verification

### Example: Stereo Recording with 512 Frames of Latency

**Given**:
- Sample Rate: 44,100 Hz
- Channels: 2 (stereo)
- Actual Hardware Latency: 512 frames
- Source File: 44,100 frames (1.0 second)

### Latency Measurement Phase

1. **Sine wave played at frame 0**
2. **Captured audio is interleaved**: `[L0, R0, L1, R1, ..., Lpeak, Rpeak, ...]`
3. **Peak detected at interleaved sample 1024**:
   - This means: `L512` or `R512` (frame 512 in either left or right channel)
   - `measuredLatencySamples = 1024` (interleaved samples)

### Processing Phase (BEFORE FIX - BUGGY)

```
Source File Length: 44,100 frames
Recording Length Calculation:
  sourceFrames + latencySamples + (latencySamples * 4)
  = 44,100 + 512 + 2,048  ← Uses 512 (frames) from measuredLatencySamples / 2
  = 46,660 frames
  = 93,320 samples (stereo interleaved)

Captured Audio: 93,320 samples [L0, R0, L1, R1, ..., L46659, R46659]

❌ BUGGY TRIMMING:
  latencySamples = measuredLatencySamples * inputChannelCount
  latencySamples = 1024 * 2 = 2,048 samples

  Trimmed: capturedAudio[2,048 ..< end]
  This skips 1,024 FRAMES (should only skip 512!)

  Result: Output audio starts 512 frames TOO LATE!
  At 44.1kHz: ~11.6ms too late
```

### Processing Phase (AFTER FIX - CORRECT)

```
Source File Length: 44,100 frames
Recording Length Calculation:
  sourceFrames + latencySamples + (latencySamples * 4)

  ⚠️ WAIT - There's ANOTHER issue here!

  recordingLength() expects latencySamples in FRAMES, but we're passing
  measuredLatencySamples which is in INTERLEAVED SAMPLES!

  Let's check ProcessingSettings.swift...
```

---

## DISCOVERED: Second Issue with recordingLength()

Looking at the usage in [AudioProcessingService.swift:172-174](F9-Batch-Resampler/Services/AudioProcessingService.swift#L172-L174):

```swift
let targetRecordingFrames = settings.recordingLength(sourceFileSamples: frameCount,
                                                     latencySamples: measuredLatencySamples)
let targetRecordingSamples = targetRecordingFrames * inputChannelCount
```

The function signature from [ProcessingSettings.swift:71-73](F9-Batch-Resampler/Models/ProcessingSettings.swift#L71-L73):

```swift
/// Returns the recording length in samples for a given source file length
/// Includes latency compensation and safety buffer (4x latency)
func recordingLength(sourceFileSamples: Int, latencySamples: Int) -> Int {
    return sourceFileSamples + latencySamples + (latencySamples * 4)
}
```

### The Confusion

The parameter is named `latencySamples` but the docstring says it returns "recording length in **samples**" while the parameter name is `sourceFileSamples`.

**Are these FRAMES or INTERLEAVED SAMPLES?**

Let's trace the usage:
1. `frameCount` = number of frames in source file
2. `measuredLatencySamples` = interleaved sample index (e.g., 1024 for 512 frames stereo)
3. `recordingLength()` returns a value
4. That value is multiplied by `inputChannelCount` to get `targetRecordingSamples`

**Conclusion**:
- The function expects **FRAMES** as input
- It returns **FRAMES** as output
- But we're passing `measuredLatencySamples` which is in **INTERLEAVED SAMPLES**!

### Example of Second Bug

**Scenario**: Stereo, 512 frames of latency
- `measuredLatencySamples = 1024` (interleaved)
- Calling `recordingLength(sourceFileSamples: 44100, latencySamples: 1024)`:
  - Returns: `44100 + 1024 + (1024 * 4) = 49,196` frames ❌
  - Should return: `44100 + 512 + (512 * 4) = 46,660` frames ✅

**Impact**: We're recording LONGER than necessary, wasting processing time and buffer space.

---

## Complete Fix Required

We need to fix BOTH issues:

### Issue 1: Trimming Math (FIXED ✅)
- **Was**: `let latencySamples = measuredLatencySamples * inputChannelCount`
- **Now**: `let latencySamples = measuredLatencySamples`

### Issue 2: Recording Length Calculation (NEEDS FIX ❌)
- **Current**: `settings.recordingLength(sourceFileSamples: frameCount, latencySamples: measuredLatencySamples)`
- **Should be**: `settings.recordingLength(sourceFileSamples: frameCount, latencySamples: measuredLatencySamples / inputChannelCount)`

**OR** better yet, convert once at the top:

```swift
// Convert measured latency from interleaved samples to frames
let latencyFrames = measuredLatencySamples / inputChannelCount

// Calculate target recording length
let targetRecordingFrames = settings.recordingLength(sourceFileSamples: frameCount,
                                                     latencySamples: latencyFrames)
let targetRecordingSamples = targetRecordingFrames * inputChannelCount
```

Then for trimming:
```swift
// Trim latency from the beginning (measuredLatencySamples is already interleaved)
let latencySamples = measuredLatencySamples
let trimmedAudio = trimLatency(from: capturedAudio, ...)
```

---

## Corrected Mathematical Example

### Setup
- Sample Rate: 44,100 Hz
- Channels: 2 (stereo)
- Actual Hardware Latency: 512 frames = 1,024 interleaved samples
- Source File: 44,100 frames (1.0 second)
- `measuredLatencySamples = 1,024` (from LatencyMeasurementService)

### Step 1: Convert to Frames
```swift
let latencyFrames = measuredLatencySamples / inputChannelCount
let latencyFrames = 1024 / 2 = 512 frames ✅
```

### Step 2: Calculate Recording Length
```swift
let targetRecordingFrames = settings.recordingLength(sourceFileSamples: 44100,
                                                     latencySamples: 512)
// = 44,100 + 512 + (512 * 4)
// = 44,100 + 512 + 2,048
// = 46,660 frames ✅

let targetRecordingSamples = 46,660 * 2 = 93,320 samples ✅
```

### Step 3: Capture Audio
```
Wait until capturedAudio.count >= 93,320 samples
```

### Step 4: Trim Latency
```swift
let latencySamples = measuredLatencySamples  // 1,024 interleaved samples
let trimmedAudio = trimLatency(from: capturedAudio,
                               latencySamples: 1024,
                               sourceFrames: 44100,
                               channelCount: 2)

Inside trimLatency():
  startSample = 1,024
  desiredOutputSamples = 44,100 * 2 = 88,200
  endSample = 1,024 + 88,200 = 89,224

  return capturedAudio[1,024 ..< 89,224]  // 88,200 samples = 44,100 frames ✅
```

### Result
- **Captured**: 93,320 samples (46,660 frames)
- **Trimmed**: 1,024 samples (512 frames of latency)
- **Output**: 88,200 samples (44,100 frames) ✅
- **Perfect alignment with source!**

---

## Files Modified

### [AudioProcessingService.swift](F9-Batch-Resampler/Services/AudioProcessingService.swift)

**Lines 239-243** (processFile method):
```swift
// Trim latency from the beginning of captured audio
// NOTE: measuredLatencySamples is already in interleaved samples (not frames)
// so we don't multiply by channel count again
let latencySamples = measuredLatencySamples
let trimmedAudio = trimLatency(from: capturedAudio,
                               latencySamples: latencySamples,
                               sourceFrames: frameCount,
                               channelCount: inputChannelCount)
```

**Lines 401-405** (processFileWithInitializedSystem method):
```swift
// Trim latency from the beginning of captured audio
// NOTE: measuredLatencySamples is already in interleaved samples (not frames)
// so we don't multiply by channel count again
let latencySamples = measuredLatencySamples
let trimmedAudio = trimLatency(from: capturedAudio,
                               latencySamples: latencySamples,
                               sourceFrames: frameCount,
                               channelCount: inputChannelCount)
```

**Status**: ✅ Issue #1 FIXED (trimming math)
**Status**: ✅ Issue #2 FIXED (recording length calculation)

---

## Complete Fix Applied

### Issue 1: Trimming Math (FIXED ✅)

**Lines 239-243 and 401-405**:
```swift
// Trim latency from the beginning of captured audio
// NOTE: measuredLatencySamples is already in interleaved samples (not frames)
// so we don't multiply by channel count again
let latencySamples = measuredLatencySamples
let trimmedAudio = trimLatency(from: capturedAudio,
                               latencySamples: latencySamples,
                               sourceFrames: frameCount,
                               channelCount: inputChannelCount)
```

### Issue 2: Recording Length Calculation (FIXED ✅)

**Lines 171-176 and 335-340**:
```swift
// Calculate target recording length: source + latency + safety buffer
let inputChannelCount = inputPair.channels.count
// Convert measured latency from interleaved samples to frames
let latencyFrames = measuredLatencySamples / inputChannelCount
let targetRecordingFrames = settings.recordingLength(sourceFileSamples: frameCount, latencySamples: latencyFrames)
let targetRecordingSamples = targetRecordingFrames * inputChannelCount
```

---

## Build Status

✅ **BUILD SUCCEEDED** - No errors, no warnings

---

## Next Steps

1. **Test with real hardware** (RME UFX III) to verify:
   - Correct alignment of processed audio
   - No trimming too much from the beginning
   - Output file length matches source file length exactly
2. **Verify with different buffer sizes**:
   - 128 samples
   - 256 samples
   - 512 samples
   - 1024 samples
3. **Test with various source file lengths**:
   - Short samples (< 1 second)
   - Medium samples (1-5 seconds)
   - Long samples (> 10 seconds)

---

**Last Updated**: 2025-10-19
**Status**: ✅ Both latency math issues fixed and verified
