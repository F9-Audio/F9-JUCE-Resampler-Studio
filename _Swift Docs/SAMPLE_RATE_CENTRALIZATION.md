# Sample Rate Centralization - 44.1kHz Fix

## Date: 2025-10-19

---

## Problem Identified

There was sample rate confusion throughout the codebase:
- **48kHz shown in UI** (SettingsView) ❌
- **44.1kHz hardcoded in multiple places**
- **No central source of truth** for sample rate

This caused confusion in latency calculations and could lead to incorrect processing.

---

## Solution Implemented

### Centralized Sample Rate Constant

**File**: [ProcessingSettings.swift](F9-Batch-Resampler/Models/ProcessingSettings.swift#L24-L29)

Added a single source of truth for sample rate:

```swift
struct ProcessingSettings: Equatable {
    // MARK: - Global Audio Settings

    /// Standard sample rate for all audio operations (44.1kHz)
    /// NOTE: For prototype, everything is fixed at 44.1kHz
    /// TODO: Make this configurable to support multiple sample rates (48kHz, 88.2kHz, 96kHz, etc.)
    static let sampleRate: Double = 44100.0

    // MARK: - Processing Settings
    // ...
}
```

**Key Features**:
- `static let` - Compile-time constant, zero runtime overhead
- Clear TODO for future multi-sample-rate support
- Accessible from anywhere as `ProcessingSettings.sampleRate`

---

## All References Updated

### 1. ✅ SettingsView.swift (Line 58)
**FIXED THE 48kHz BUG!**

**Before**:
```swift
Text("(\(String(format: "%.2f", vm.settings.latencyInMs(sampleRate: 48000.0) ?? 0)) ms @ 48kHz)")
```

**After**:
```swift
Text("(\(String(format: "%.2f", vm.settings.latencyInMs(sampleRate: ProcessingSettings.sampleRate) ?? 0)) ms @ 44.1kHz)")
```

**Impact**: UI now correctly shows latency at 44.1kHz, not 48kHz!

---

### 2. ✅ MainViewModel.swift (Line 161)

**Before**:
```swift
let latencyMs = settings.latencyInMs(sampleRate: 44100.0) ?? 0
```

**After**:
```swift
let latencyMs = settings.latencyInMs(sampleRate: ProcessingSettings.sampleRate) ?? 0
```

---

### 3. ✅ CAAudioHardwareSystem.swift (Line 25)

**Before**:
```swift
private let sampleRate: Double = 44100.0
```

**After**:
```swift
private let sampleRate: Double = ProcessingSettings.sampleRate
```

**Impact**: Audio system initialization uses centralized rate

---

### 4. ✅ AudioProcessingService.swift (Multiple Locations)

**Before** (Lines 250, 266, 412, 428, 473):
```swift
standardFormatWithSampleRate: 44100.0
AVSampleRateKey: 44100.0
let sampleRate = 44100.0
```

**After**:
```swift
standardFormatWithSampleRate: ProcessingSettings.sampleRate
AVSampleRateKey: ProcessingSettings.sampleRate
let sampleRate = ProcessingSettings.sampleRate
```

**Impact**:
- Output file creation uses correct rate
- Preview mode uses correct rate
- All AVAudioFormat instances consistent

---

### 5. ✅ LatencyMeasurementService.swift (Line 122)

**Before**:
```swift
sampleRate: 44100.0
```

**After**:
```swift
sampleRate: ProcessingSettings.sampleRate
```

**Impact**: Latency calculations now use centralized constant

---

### 6. ✅ SineWaveGenerator.swift (Line 22)

**Before**:
```swift
init(frequency: Double = 1000.0, sampleRate: Double = 44100.0)
```

**After**:
```swift
init(frequency: Double = 1000.0, sampleRate: Double = ProcessingSettings.sampleRate)
```

**Impact**: Default sample rate for sine wave test signals

---

### 7. ✅ HardwareLoopTestService.swift (Line 45)

**Before**:
```swift
private let sineWaveGenerator = SineWaveGenerator(frequency: 1000.0, sampleRate: 44100.0)
```

**After**:
```swift
private let sineWaveGenerator = SineWaveGenerator(frequency: 1000.0, sampleRate: ProcessingSettings.sampleRate)
```

---

### 8. ✅ AudioFile.swift (Line 33)

**Before**:
```swift
return abs(rate - 44100.0) < 1.0
```

**After**:
```swift
return abs(rate - ProcessingSettings.sampleRate) < 1.0
```

**Impact**: File validation checks against centralized sample rate

---

## Files Modified Summary

| File | Lines Changed | Purpose |
|------|---------------|---------|
| ProcessingSettings.swift | Added 24-29 | Central constant definition |
| SettingsView.swift | 58 | **Fixed 48kHz display bug** |
| MainViewModel.swift | 161 | Latency logging |
| CAAudioHardwareSystem.swift | 25 | Audio system config |
| AudioProcessingService.swift | 250, 266, 412, 428, 473 | File writing & preview |
| LatencyMeasurementService.swift | 122 | Latency analysis |
| SineWaveGenerator.swift | 22 | Default parameter |
| HardwareLoopTestService.swift | 45 | Test signal generation |
| AudioFile.swift | 33 | File validation |

**Total**: 9 files, ~15 occurrences replaced

---

## Benefits

### 1. **Consistency** ✅
- All audio operations use exactly 44.1kHz
- No more confusion between 44.1kHz and 48kHz
- Single source of truth

### 2. **Correctness** ✅
- Latency calculations accurate
- File writing uses correct sample rate
- UI displays correct values (no more 48kHz!)

### 3. **Maintainability** ✅
- Easy to change in one place
- Clear TODO for future multi-rate support
- Self-documenting code

### 4. **Future-Proof** ✅
- Easy path to variable sample rate:
  ```swift
  // Future implementation:
  var sampleRate: Double = 44100.0  // Make instance variable
  // Then support: 44.1, 48, 88.2, 96, 192 kHz
  ```

---

## Verification

### Build Status
✅ **BUILD SUCCEEDED** - No errors, no warnings

### Testing Checklist
- [ ] Latency measurement shows correct ms value at 44.1kHz
- [ ] UI displays "@ 44.1kHz" (not 48kHz)
- [ ] Output files are 44.1kHz sample rate
- [ ] Preview mode works correctly
- [ ] Hardware loop test uses 44.1kHz

---

## Future Enhancements

### Multi-Sample-Rate Support

**To implement variable sample rate**:

1. **Make it instance variable** in ProcessingSettings:
   ```swift
   var sampleRate: Double = 44100.0  // Default
   ```

2. **Add UI picker** in SettingsView:
   ```swift
   Picker("Sample Rate", selection: $vm.settings.sampleRate) {
       Text("44.1 kHz").tag(44100.0)
       Text("48 kHz").tag(48000.0)
       Text("88.2 kHz").tag(88200.0)
       Text("96 kHz").tag(96000.0)
   }
   ```

3. **Update all services** to use instance var:
   ```swift
   // Instead of: ProcessingSettings.sampleRate (static)
   // Use: settings.sampleRate (instance)
   ```

4. **Add sample rate conversion** if needed:
   ```swift
   // Convert source file to target sample rate
   // Use AVAudioConverter for resampling
   ```

---

## Impact on Latency Calculations

### Example Calculation at 44.1kHz:

**Given**:
- Measured Latency: 512 samples
- Sample Rate: 44,100 Hz (now centralized!)

**Calculation**:
```
Latency (ms) = (512 samples / 44,100 Hz) × 1000
             = 11.61 ms ✓
```

**Before (if using 48kHz by mistake)**:
```
Latency (ms) = (512 samples / 48,000 Hz) × 1000
             = 10.67 ms ✗ WRONG!
```

**Difference**: ~0.94ms error per 512 samples (significant for tight timing!)

---

## Console Output Verification

You should now see consistent messages:

### Latency Measurement:
```
✅ Latency measured: 512 samples (11.61 ms @ 44.1kHz)
```

### UI Display:
```
512 samples
(11.61 ms @ 44.1kHz)
```

**No more 48kHz references anywhere!**

---

## Summary

✅ **Sample rate centralized** to `ProcessingSettings.sampleRate`
✅ **All 15+ occurrences updated** across 9 files
✅ **48kHz UI bug fixed** - now correctly shows 44.1kHz
✅ **Latency calculations accurate**
✅ **Build successful** with no errors
✅ **Future-proof** with clear path to multi-rate support

**Everything now runs at exactly 44.1kHz for the prototype!**

---

**Last Updated**: 2025-10-19
**Status**: Complete - All sample rate references centralized
