# CA Audio Hardware Integration Plan

## Goals
- Replace the current ad-hoc Core Audio / AVAudioEngine stack with the CA Audio Hardware project to obtain predictable HAL access from Swift.
- Simplify device/channel management so multichannel interfaces (e.g. SSL12) expose all usable I/O pairs to the app.
- Stabilise latency measurement, routing, and batch capture with a minimal, testable surface area.

## Prerequisites
- Fork or vendor the CA Audio Hardware repository as a Git submodule (preferred for internal-only distribution) and document its commit hash (currently `d927963dcfbb819da6ed6f17b83f17ffbc689280`, tag `0.7.1`).
- Mirror the `CoreAudioExtensions` dependency alongside it (commit `6786ff0074ae44e6c1c053d113218aeca47eaccc`, tag `0.3.0`).
- Audit licensing and attribution requirements for redistribution inside the company.
- Confirm the project builds for macOS 13+ with Swift 5.9 and matches our deployment targets.

## Phase 1 – Baseline Assessment
1. Catalogue the existing audio-related sources (`AudioCore/Sources`, `AudioService`, `LatencyMeasurementService`, `AudioProcessingService`, HAL C modules) and note which ones are salvageable.
2. Capture the current unit/UI test coverage and identify gaps affecting audio routing or latency logic.
3. Record functional regressions caused by prior attempts (crash logs, incorrect channel counts, buffer overruns) so we can prove fixes later.

## Phase 2 – Library Integration
1. Add the CA Audio Hardware project to the workspace via the vendored submodule (fallback: Xcode subproject if tighter Xcode integration is required).
2. Expose a Swift façade (`CAAudioBridge`) that wraps the library’s key capabilities (device enumeration, stream configuration, HAL callback hooks). Phase 2 delivers the protocol and stub; Phase 3 will attach the real implementation once the library is vendored.
3. Design thread-safe data models (`HardwareDevice`, `StreamLayout`, `ChannelMap`) populated solely from the CA Audio Hardware API.
4. Deprecate the hand-written HAL C code and mark it for removal once the new bridge passes smoke tests.

## Phase 3 – Core Services Rewrite
1. Rebuild `AudioService` on top of `CAAudioBridge`, ensuring:
   - Accurate input/output channel counts for arbitrary multichannel hardware.
   - Explicit stream direction separation and support for aggregate devices.
   - Notification hooks for device hot-plug events.
   - *Status:* Bridge now enumerates via CA Audio Hardware and clamps sample rate / buffer size.
2. Re-implement `LatencyMeasurementService` using the bridge:
   - Use HAL-level render/capture callbacks instead of simulated values.
   - Persist per-buffer-size latency/phase alignment data.
   - Provide health diagnostics (buffer underruns, timestamp drift).
   - *Status:* Hardware bridge now exposes the IOProc pipeline and reports live sample time.
3. Rewrite `AudioProcessingService` to manage batch playback/record:
   - Configure simultaneous playback/record graphs with deterministic buffer sizes.
   - Inject latency compensation and trimming based on Phase 2 data.
   - Return structured results (success, clip detected, noise floor analysis).
   - *Status:* Services still rely on the legacy implementation; hook into new bridge next.

## Phase 4 – UI & ViewModel Alignment
1. Update view models to consume the new `HardwareDevice`/`StreamLayout` models and present all input/output channel pairs.
2. Refresh settings screens to allow advanced routing (offset, mono > stereo duplication, custom naming).
3. Refactor logging to surface bridge-level diagnostics and expose a developer toggle.

## Phase 5 – Validation & Cleanup
1. Implement unit tests for the bridge wrappers (mocked device descriptors, channel map calculations).
2. Add integration tests with a loopback harness to verify latency stability across common hardware.
3. Remove the deprecated HAL C sources, update technical documentation, and ensure the build scripts reference only the new modules.
4. Produce a migration report summarising behavioural improvements and remaining edge cases.

## Deliverables
- Updated Swift sources with CA Audio Hardware integration.
- Revised documentation (`TECHNICAL_DOCUMENTATION.md` + release notes) reflecting the simplified architecture.
- Automated test suites covering latency measurement, routing selection, and batch processing success paths.
- Rollback plan (tags/branches) so we can revert to the pre-migration state if critical issues arise.
