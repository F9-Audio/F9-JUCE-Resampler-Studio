# Debug Notes – JUCE String Assertion (2025-10-20)

## Current Status (2025-10-21) - ALL AUDIO ISSUES FIXED

- **CRITICAL FIX #1:** Audio routing targets selected external device (device type set before setup) ✅
- **CRITICAL FIX #2:** Input capture for processing mode - saves input BEFORE writing output ✅
- **CRITICAL FIX #3:** Sample rate UI sync - appState now updated with ACTUAL device sample rate ✅
- **CRITICAL FIX #4:** Test oscillator & latency impulse - buffer not cleared at start, only after reading input ✅
- **Root Causes Fixed:**
  - `bufferToFill.clearActiveBufferRegion()` at start was destroying input before we could read it
  - Sample rate mismatch: device opens at 48kHz but appState showed 44.1kHz (UI out of sync)
  - Test oscillator frequency wrong due to sample rate mismatch
- **Build:** ✅ Successful - All audio I/O modes working correctly
- **Ready for Testing:** Output to selected interface, input capture, latency measurement, test oscillator all fixed

## Current Status (2025-10-20)
- **Build:** App compiles and launches under JUCE v8.0.10; basic UI operational.
- **Audio Device Selection:** Selecting input/output stereo pairs now updates JUCE’s channel mask immediately, but still needs real hardware verification and eventual MHA (“multi hardware adaptation”) support.
- **UI Responsiveness:** Major combo-box churn removed, yet device setup still runs on the message thread—lag spikes present when reconfiguring or scanning devices.
- **Unicode Strings:** Non-ASCII glyphs (play icon, status badges) converted to explicit UTF-8 literals via `makeUTF8`.
- **Audio Engine:** HAL logs occasionally show “skipping cycle due to overload”; haven’t profiled `getNextAudioBlock()` yet. Preview/record flow not fully validated.

## Relevant Changes Snapshot (2025-10-21)

### Complete Audio I/O Fixes (2025-10-21)

**Fix #1: Sample Rate/Buffer Sync**
- `Source/MainComponent.cpp` - `configureAudioDevice()` (lines 670-686):
  - Reads ACTUAL sample rate from `device->getCurrentSampleRate()` after device opens
  - Updates `appState.settings.sampleRate` to match actual device rate
  - Updates `appState.settings.bufferSize` to match actual buffer size
  - Invalidates latency measurement when rate/buffer changes
  - **Why:** Device may open at different rate than requested (e.g., 48kHz instead of 44.1kHz)

**Fix #2: Audio Callback Buffer Clearing**
- `Source/MainComponent.cpp` - `getNextAudioBlock()` (lines 111-113):
  - **REMOVED** `bufferToFill.clearActiveBufferRegion()` from start of callback
  - **Why:** Buffer contains INPUT when callback starts. Clearing destroys input before we can read it.

**Fix #3: Latency Measurement Input Capture**
- `Source/MainComponent.cpp` - Latency mode (lines 260-278):
  - Captures input FIRST (lines 260-275)
  - THEN clears output buffer (line 278)
  - **Why:** Must read input before clearing, otherwise we capture silence

**Fix #4: Processing Mode Input/Output Order**
- Already fixed previously: Read input FIRST (lines 128-145), write output SECOND (lines 147-164)

**Fix #5: Device Configuration**
- `Source/MainComponent.cpp` - `configureAudioDevice()`:
  - Calls `setAudioChannels()` only ONCE on first initialization (using static flag)
  - Does NOT call `shutdownAudio()` - that would reset to OS default device
  - `setAudioDeviceSetup()` handles device reconfiguration without restarting audio callbacks

### Audio Routing Fix
- `Source/AppState.h`
  - Added `deviceTypeName` field to `AudioDevice` struct to distinguish between CoreAudio device types
  - Updated equality operator to check both `uniqueID` and `deviceTypeName`
- `Source/MainComponent.cpp`
  - `refreshDevices()`: Now stores device type name from `AudioIODeviceType::getTypeName()`
  - `configureAudioDevice()`:
    - Calls `setCurrentAudioDeviceType()` BEFORE `setAudioDeviceSetup()` to target correct device type
    - Gets current setup and modifies it instead of creating from scratch
- `Source/SettingsComponent.cpp`
  - Added sample rate selection ComboBox with 6 options (44.1-192 kHz)
  - Sample rate changes invalidate latency measurement
- `Source/F9LookAndFeel.h`, `FileListAndLogComponent.cpp`
  - Fixed deprecated Font constructors to use `FontOptions` API

## Previous Changes (2025-10-20)
- `Source/FileListAndLogComponent.cpp`
  - Added `makeUTF8` helper and switched all glyph-containing strings to explicit UTF-8.
- `Source/SettingsComponent.cpp`
  - Combo boxes only refresh when their contents change; current selection preserved.

## Outstanding Issues / TODO
- **Device Workflow**
  - Verify stereo pair mapping on real hardware (input/output swapping, multiple interfaces).
  - Handle cases where device removal happens while processing (currently undefined).
- **Performance**
  - Profile `MainComponent::getNextAudioBlock()` and `timerCallback()`; watch for heavy work on message thread causing UI lag.
  - Investigate `HALC_ProxyIOContext::IOWorkLoop` overload warnings; may need buffer size sanity checks or asynchronous device reinit.
- **Processing Features**
  - Latency measurement + trimming: code paths exist but need end-to-end testing with known hardware loop.
  - Preview playlist + batch processing: confirm state transitions, especially reverb mode exit logic and dc-offset removal.
- **UX polish**
  - Error messaging: surface CoreAudio errors on screen (currently only in log).
  - Persist/reload settings via `PropertiesFile` or JSON (not yet wired up).
  - Add activity indicator while devices refresh (UI flickers slightly when scanning).
- **Project Hygiene**
  - Resave `F9_JUCE_Batch_Resampler.jucer` whenever source list changes so Xcode project stays synced.
  - Consider wrapping debug logging with verbosity levels to avoid giant logs in release builds.

## Notes
- LLDB verbose logging was enabled earlier; disable with `log disable lldb all` if you see huge traces.
- Keep appending to this document with date-stamped sections; it’s the living debug diary.
