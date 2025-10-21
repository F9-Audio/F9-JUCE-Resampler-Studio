# Phase 3 – CA Audio Hardware Bridge Notes

## Summary
- `CAAudioHardwareBridge` now queries devices through the CA Audio Hardware wrappers, clamps buffer size and sample rate, and configures all streams for 32‑bit float I/O at the requested rate.
- A real HAL IOProc is registered per stream token; interleaved float buffers are marshalled to/from the driver’s planar buffers before invoking the app callbacks.
- `CAAudioHardwareSystem` delegates start/stop/currentSampleTime to the bridge, so higher layers can track live sample counters without touching HAL APIs.

## Outstanding Items
- Replace the per-buffer interleave/deinterleave copy with a true ring buffer or channel mapping once performance profiling is complete.
- Surface underrun/overrun diagnostics from the IOProc (e.g., zero-fill counts, timestamp jumps) and expose them via structured errors.
- `AudioProcessingService` / `LatencyMeasurementService` still reuse the legacy flow; migrate them to the bridge callbacks and drop the old simulated pipelines.
- Investigate devices that refuse non-interleaved float formats and add a fallback configuration path.

## Verification Checklist
1. `File ▸ Clean Build Folder`, then run the app with a multichannel interface.
2. Confirm the device dropdown lists all HAL devices with correct channel counts (read from CA Audio Hardware).
3. Start a processing pass and monitor the console for IOProc errors; ensure the IOProc keeps running after repeated starts/stops.
4. (Optional) Log sample times via `CAAudioHardwareSystem.getCurrentSampleTime()` to confirm the counter increments while streaming.
