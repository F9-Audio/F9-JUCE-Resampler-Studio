# CA Audio Hardware Vendor Setup

This project targets macOS 13+ and ships only inside the company, so we vendor our Core Audio dependencies as Git submodules to avoid network fetches and guarantee deterministic builds.

## Submodule Layout

```
F9-Batch-Resampler/
├─ Vendor/
│  ├─ CAAudioHardware/        # https://github.com/sbooth/CAAudioHardware @ d927963d…
│  └─ CoreAudioExtensions/    # https://github.com/sbooth/CoreAudioExtensions @ 6786ff0…
```

## One-Time Setup

```bash
cd F9-Batch-Resampler
mkdir -p Vendor
git submodule add -f https://github.com/sbooth/CAAudioHardware Vendor/CAAudioHardware
git submodule add -f https://github.com/sbooth/CoreAudioExtensions Vendor/CoreAudioExtensions
cd Vendor/CAAudioHardware && git checkout d927963dcfbb819da6ed6f17b83f17ffbc689280 && cd ../..
cd Vendor/CoreAudioExtensions && git checkout 6786ff0074ae44e6c1c053d113218aeca47eaccc && cd ../..
```

Commit the resulting `.gitmodules` entry and vendor folders.

## Xcode Integration

1. In Xcode, choose *File ▸ Add Packages…* and select “Add Local…” to point at `Vendor/CAAudioHardware`.
2. Repeat for `Vendor/CoreAudioExtensions`.
3. Link the `CAAudioHardware` product to the `F9-Batch-Resampler` target (Frameworks phase).

Alternatively, add the two packages to `Package.swift` when a pure SwiftPM workspace is available.

## Maintenance

- When pulling updates, run `git submodule update --init --recursive`.
- To bump the vendor versions, perform a normal `git fetch` inside each submodule, check out the desired tag/commit, and update this document with the new hashes.

## Derived Data Hygiene

If Xcode still attempts to fetch the remote packages, clear Derived Data (`rm -rf ~/Library/Developer/Xcode/DerivedData`) and reopen the project. The local submodule paths should now take precedence.
