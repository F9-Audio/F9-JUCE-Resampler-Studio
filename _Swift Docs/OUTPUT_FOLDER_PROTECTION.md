# Output Folder Protection

## Date: 2025-10-19

---

## Overview

The F9-Batch-Resampler now **requires** you to select a destination folder before processing any files. This ensures that **original files are never overwritten**, providing a safe workflow for processing valuable audio samples.

---

## Key Features

### 1. Mandatory Destination Folder
- Processing is **disabled** until you select an output folder
- No defaults to source directory - explicit selection required
- Clear UI warnings when destination folder isn't set

### 2. Folder Creation Support
- The folder picker allows creating **new folders**
- Create organized folders for your processed samples (e.g., "Processed_Drums", "Hardware_EQ", etc.)
- Starts in your Documents folder for convenience

### 3. Original File Protection
- Processed files **always** go to your chosen destination folder
- Source files remain **untouched** in their original location
- Safe to process entire sample libraries without risk

---

## How to Use

### Step 1: Select Output Folder

1. Open the **Settings** tab
2. In the **Output Settings** section, look for "Output Folder"
3. Click **"Select Destination Folder..."**
4. In the dialog:
   - Browse to where you want processed files saved
   - **Or** click "New Folder" to create a dedicated folder
   - Click "Select" to confirm

**The folder path will be displayed in Settings and remembered for future sessions.**

### Step 2: Configure Filename (Optional)

**Filename Postfix** field lets you add a suffix to processed files:

- **Empty** (default): Keeps original filename
  - `kick_01.wav` → `kick_01.wav` (in destination folder)
  - Perfect for maintaining sampler plugin references

- **With postfix** (e.g., "_processed"):
  - `kick_01.wav` → `kick_01_processed.wav`
  - Good for comparing before/after

### Step 3: Process Files

Once destination folder is set:
- The **"Process All"** button becomes enabled
- Drop your files and process as normal
- All output goes to your chosen destination folder
- Originals remain safe in their source location

---

## UI Indicators

### Settings View

**When NO folder selected**:
```
⚠️ No destination folder selected
[Select Destination Folder...]
Required - processed files will never overwrite originals
```

**When folder selected**:
```
/Users/you/Music/Processed_Samples        [Change...]
Required - processed files will never overwrite originals
```

### File Drop View

**When ready to process**:
```
[Process All] (enabled)
```

**When destination folder missing**:
```
[Process All] (disabled/grayed)
ℹ️ Select a destination folder in Settings to begin processing
```

---

## Technical Details

### Code Changes

#### [SettingsView.swift](F9-Batch-Resampler/Views/SettingsView.swift)

**Folder Picker** (lines 251-273):
- Added `canCreateDirectories = true` to NSOpenPanel
- Default start location: Documents folder
- Clear messaging about folder creation capability

**UI Updates** (lines 108-146):
- Warning indicator when no folder selected
- Full path display when folder is set
- Updated help text to emphasize protection

#### [AudioProcessingService.swift](F9-Batch-Resampler/Services/AudioProcessingService.swift)

**buildOutputURL()** (lines 575-578):
```swift
// Require output directory to be set (never write to source directory)
guard let outputFolderPath = settings.outputFolderPath, !outputFolderPath.isEmpty else {
    throw AudioProcessingError.processingFailed("Output folder not selected - please choose a destination folder in Settings")
}
```

**Removed** the fallback to source directory:
```swift
// OLD (DANGEROUS):
} else {
    outputDirectory = file.url.deletingLastPathComponent()  // ❌ Could overwrite!
}

// NEW (SAFE):
// No fallback - throws error if not set ✅
```

#### [MainViewModel.swift](F9-Batch-Resampler/ViewModels/MainViewModel.swift)

**Validation** (lines 187-191):
```swift
// Require output folder to be set to prevent overwriting originals
guard let outputFolder = settings.outputFolderPath, !outputFolder.isEmpty else {
    appendLog("❌ Cannot process: No destination folder selected - choose one in Settings to protect your original files")
    return
}
```

#### [FileDropView.swift](F9-Batch-Resampler/Views/FileDropView.swift)

**Button State** (line 111):
```swift
.disabled(vm.isProcessing || vm.isPreviewing || vm.files.isEmpty ||
          vm.settings.measuredLatencySamples == nil ||
          vm.settings.outputFolderPath == nil)  // Added check
```

**Helper Message** (lines 116-124):
```swift
if vm.settings.outputFolderPath == nil && !vm.files.isEmpty && vm.settings.measuredLatencySamples != nil {
    HStack {
        Image(systemName: "info.circle")
            .foregroundStyle(.orange)
        Text("Select a destination folder in Settings to begin processing")
    }
}
```

---

## Workflow Examples

### Example 1: Processing Drum Samples

```
Source Location:
/Users/you/Samples/Raw_Drums/
  ├── kick_01.wav
  ├── kick_02.wav
  └── snare_01.wav

Destination Folder Selected:
/Users/you/Samples/Processed_Drums/

After Processing:
/Users/you/Samples/Raw_Drums/
  ├── kick_01.wav          ← Untouched original
  ├── kick_02.wav          ← Untouched original
  └── snare_01.wav         ← Untouched original

/Users/you/Samples/Processed_Drums/
  ├── kick_01.wav          ← Hardware-processed version
  ├── kick_02.wav          ← Hardware-processed version
  └── snare_01.wav         ← Hardware-processed version
```

### Example 2: With Postfix

```
Destination Folder: /Users/you/Music/Output/
Filename Postfix: _1176

Results:
  kick.wav → kick_1176.wav
  snare.wav → snare_1176.wav
```

---

## Error Messages

### During Processing

If you somehow bypass the UI validation, the processing service will catch it:

```
❌ Error processing file: Output folder not selected - please choose a destination folder in Settings
```

### In Logs

When attempting to process without destination folder:

```
❌ Cannot process: No destination folder selected - choose one in Settings to protect your original files
```

---

## Benefits

### Safety
- **Zero risk** of overwriting original samples
- Especially important when processing entire sample libraries
- Can experiment freely with hardware settings

### Organization
- Keep processed versions separate from originals
- Create folders by processing type (e.g., "1176_Compressed", "Pultec_EQ", "Hardware_Reverb")
- Easy A/B comparison in your DAW

### Workflow
- Process samples for different hardware chains
- Build variants for different use cases
- Maintain original files for re-processing with different settings

---

## Migration Notes

### Previous Behavior (Before This Update)

- `outputFolderPath` was **optional**
- If not set, files were written to **source directory**
- With empty postfix: **originals could be overwritten** ⚠️

### New Behavior (Current)

- `outputFolderPath` is **required**
- Must be explicitly set before processing
- **Originals are always protected** ✅

### For Existing Users

If you have been using the app:
1. First launch after update will show "No destination folder selected"
2. Select a folder in Settings before your next processing run
3. Your selected folder will be remembered for future sessions

---

## Build Status

✅ **BUILD SUCCEEDED** - All safety checks implemented

---

**Last Updated**: 2025-10-19
**Status**: Complete - Original files are now fully protected
