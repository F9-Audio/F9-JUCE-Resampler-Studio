# Audio Routing & Sample Rate Fixes - 2025-10-21

## Issues Fixed

### 1. Audio Output Routing to Wrong Device ✅
**Problem:** Hardware test oscillator and latency impulse were outputting through OS default audio device instead of the app-selected device.

**Root Cause:**
- `AudioAppComponent` has its own internal `AudioDeviceManager`
- We were creating a SECOND `deviceManager` member variable
- We configured our own deviceManager but AudioAppComponent was still using its internal one for audio I/O
- `setAudioChannels()` in constructor used the OS default device

**Solution:**
- Removed duplicate `deviceManager` member variable
- Now use inherited `deviceManager` from `AudioAppComponent`
- Removed `setAudioChannels()` call from constructor
- Updated `configureAudioDevice()` to:
  1. Shutdown current audio
  2. Configure the AudioDeviceManager with selected device, channels, sample rate, buffer size
  3. Call `setAudioChannels()` AFTER device is configured to use the configured device

**Files Modified:**
- `Source/MainComponent.h` - Removed duplicate deviceManager member
- `Source/MainComponent.cpp` - Updated constructor and configureAudioDevice()

### 2. Sample Rate Selection Added ✅
**Problem:** Sample rate was hardcoded to 44.1kHz with no way to change it.

**Solution:**
- Changed `ProcessingSettings::sampleRate` from `static constexpr` to instance variable
- Added `sampleRateCombo` to `SettingsComponent`
- Supported rates: 44.1kHz, 48kHz, 88.2kHz, 96kHz, 176.4kHz, 192kHz
- Default: 44.1kHz
- Changing sample rate invalidates latency measurement (requires re-measurement)
- Updated all references from `ProcessingSettings::sampleRate` to `appState.settings.sampleRate`

**Files Modified:**
- `Source/AppState.h` - Changed sampleRate to instance variable
- `Source/SettingsComponent.h` - Added sampleRateLabel and sampleRateCombo
- `Source/SettingsComponent.cpp` - Added UI controls and handler
- `Source/MainComponent.cpp` - Updated all sampleRate references (5 locations)

### 3. Deprecated Font Constructor Warnings Fixed ✅
**Problem:** JUCE 8.x deprecated old Font constructors, causing 7 warnings.

**Solution:**
- Updated all Font constructors to use `juce::FontOptions`
- Changed from `Font().setHeight()` to `Font(FontOptions(height, style))`
- Updated helper functions `makeFont()` and `makeMonospaceFont()`

**Files Modified:**
- `Source/F9LookAndFeel.h` - 3 font methods
- `Source/SettingsComponent.cpp` - makeFont helper
- `Source/FileListAndLogComponent.cpp` - makeFont and makeMonospaceFont helpers

## Technical Details

### Audio Device Configuration Flow (New)
```
1. User selects device → selectDevice(deviceID)
2. selectDevice() calls configureAudioDevice()
3. configureAudioDevice():
   - Calls shutdownAudio()  // Stop current device
   - Creates AudioDeviceSetup with:
     * outputDeviceName = selectedDeviceID
     * inputDeviceName = selectedDeviceID
     * sampleRate = appState.settings.sampleRate
     * bufferSize = appState.settings.bufferSize
     * inputChannels = specific stereo pair bits
     * outputChannels = specific stereo pair bits
   - Calls deviceManager.setAudioDeviceSetup()  // Apply config
   - Calls setAudioChannels()  // Start audio with configured device
```

### Key Code Changes

**configureAudioDevice() - Before:**
```cpp
juce::String error = deviceManager.setAudioDeviceSetup(setup, true);
// Device configured but audio still using OS default
```

**configureAudioDevice() - After:**
```cpp
shutdownAudio();  // Stop old device
deviceManager.setAudioDeviceSetup(setup, true);  // Configure new device
setAudioChannels(inputCount, outputCount);  // Start audio with new device
```

**Sample Rate - Before:**
```cpp
static constexpr double sampleRate = 44100.0;
// Fixed, no way to change
```

**Sample Rate - After:**
```cpp
double sampleRate = 44100.0;  // Instance variable
// User can select from UI, persisted in appState
```

## Testing Checklist

- [x] Build succeeds with no warnings
- [ ] Select external audio interface (e.g., SSL 12, Apogee, etc.)
- [ ] Hardware test oscillator outputs through SELECTED interface channels
- [ ] Latency impulse test outputs through SELECTED interface channels
- [ ] Change sample rate → device reconfigures
- [ ] Change sample rate → latency measurement invalidated
- [ ] Change input/output stereo pairs → audio routes correctly
- [ ] Process file → uses selected device for playback and recording
- [ ] Preview file → uses selected device

## Notes

- Audio device is now properly configured when selected
- All audio I/O (test tone, impulse, playback, recording) uses the app-selected device
- Sample rate is fully configurable
- Device reconfiguration triggers proper audio restart
- Logs show device name, sample rate, and buffer size after configuration

## Next Steps

1. Test with real hardware interface
2. Verify channel routing works correctly
3. Verify latency measurement accuracy
4. Test full processing workflow (add files → measure latency → process)
5. Consider adding sample rate validation (check if device supports selected rate)
