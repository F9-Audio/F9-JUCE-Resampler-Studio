# F9 Batch Resampler: JUCE Porting Guide for Non-Coders (AI Experiment)

## Disclaimer ⚠️

This is an **experiment**. Porting a real-time audio app is one of the most complex tasks in software development. This guide is designed for a **non-coder** to see how far they can get by treating an AI (like the one in Cursor) as their coder. Be patient, expect errors, and be prepared to ask the AI for help frequently.

---

## Your Toolkit 🧰

1.  **Cursor:** Your AI-powered code editor. You will be "Chatting" with your project.
2.  **Your Original Xcode Project:** The folder containing all your `.swift` and `.md` files. **Do not edit this folder.** You will only copy text *from* it to give context to the AI.
3.  **JUCE Framework:** The C++ library for building the cross-platform app.
4.  **Xcode (on Mac):** Needed for the C++ compiler.
    * Install from the Mac App Store.
    * After installing, open the **Terminal** app (search for it in Spotlight).
    * Type `xcode-select --install` and press Enter. Follow the prompts to install "Command Line Tools."
5.  **A New GitHub Repository:** Create a new, blank repository on GitHub specifically for this JUCE port.

---

## Phase 0: Setup Your New JUCE Project 🏗️

This phase creates the blank "shell" for your new cross-platform app.

1.  **Download JUCE:**
    * Go to the [JUCE website](https://juce.com/get-juce) and download the framework.
    * Unzip it to an easy-to-find folder (e.g., your `Documents` folder, then maybe a `JUCE` subfolder).

2.  **Run the Projucer:**
    * Inside the folder where you unzipped JUCE, find and double-click the **Projucer** application.

3.  **Create Your New App:**
    * In Projucer, click **File > New Project**.
    * Select the **"GUI Application"** template.
    * **Project Name:** `F9BatchResampler_JUCE`
    * **Project Folder:** Click "..." and choose a *new, empty folder* where your project will live (e.g., `~/Documents/Projects/F9BatchResampler_JUCE`).
    * Click **Create**.

4.  **Configure Modules (Add JUCE Features):**
    * Your new project window opens in Projucer.
    * On the left sidebar, click the gear icon labeled **"Modules"**.
    * In the main panel, click the **`+ Add a module...`** button.
    * From the list, add the following modules one by one:
        * `juce_audio_basics`
        * `juce_audio_devices`
        * `juce_audio_formats`
        * `juce_audio_processors`
        * `juce_audio_utils`
        * `juce_dsp`
    * Your modules list should now show these added modules.

5.  **Save and Prepare for Cursor:**
    * Click **File > Save Project**.
    * At the top right of the Projucer window, click the button that looks like a save icon with an arrow ( **"Save Project and Open in IDE..."**).
    * **IMPORTANT:** This might try to open Xcode. **Close Xcode if it opens.** We only needed Projucer to *create* the project files.
    * Open the **Cursor** application.
    * In Cursor, go to **File > Open Folder...**
    * Navigate to and select the **project folder** you created in step 3 (e.g., `~/Documents/Projects/F9BatchResampler_JUCE`).
    * You should now see the project structure in Cursor's left sidebar, including a `Source` folder with files like `Main.cpp` and `MainComponent.h`.

6.  **Connect to Git:**
    * Cursor might ask if you want to initialize a Git repository. Say yes.
    * Follow Cursor's prompts (usually via the Source Control tab on the left) to:
        * Initialize the repository.
        * Commit the initial files.
        * Publish/Push the repository to the new, blank GitHub repository you created earlier.

✅ **Setup Complete!** You now have a blank C++ JUCE application open in Cursor, ready for the AI to start building.

---

## Phase 1: Porting "The Brain" (Your App's State) 🧠

We'll guide the AI to create a central place to hold all the app's settings and current status, based on your original Swift code.

1.  **Create New File:** In Cursor's file explorer (left sidebar), right-click on the `Source` folder and choose "New File". Name it `AppState.h`.

2.  **Open `AppState.h`** in the editor.

3.  **Gather Context for AI:**
    * Find your *original* Xcode project folder on your computer.
    * Open these two files using a simple text editor or even Cursor itself (just don't save any changes to the originals!):
        * `F9-Batch-Resampler/Models/ProcessingSettings.swift`
        * `F9-Batch-Resampler/ViewModels/MainViewModel.swift`
    * Select and copy the *entire text content* of `ProcessingSettings.swift`.
    * Select and copy the *entire text content* of `MainViewModel.swift`.

4.  **Instruct the AI:**
    * In Cursor, open the Chat panel (usually on the right).
    * Paste the copied content of `ProcessingSettings.swift` into the chat.
    * Paste the copied content of `MainViewModel.swift` into the chat *immediately after the first paste*.
    * Now, **after** pasting both code blocks, copy and paste the following prompt into the chat:

        ```prompt
        Using the two Swift files above as a reference, please write the C++ code for my new `AppState.h` file.

        This file should define a C++ struct or class called `AppState` that will hold all the application's settings and current status information.

        1.  Convert the `ProcessingSettings` struct from Swift into a C++ struct, placing it inside `AppState.h`. Use appropriate JUCE C++ types like `int`, `float`, `juce::String`, and `juce::File`.
        2.  Port the `BufferSize` enum from Swift into a C++ enum class within `AppState.h`.
        3.  Look at all the variables defined near the top of `MainViewModel` (like `isProcessing`, `processingProgress`, `logLines`, `files`, `selectedDeviceID`, etc.). Add these as public member variables to the `AppState` class. Use corresponding JUCE C++ types like `bool`, `double`, `juce::StringArray`, `juce::Array<juce::File>`, `juce::String`, etc.
        4.  Port the `AudioFile` struct and the `StereoPair` struct from my Swift project into C++ structs within this `AppState.h` file as well.
        ```

5.  **Apply the Generated Code:**
    * The AI will generate the C++ code for `AppState.h`.
    * Review it briefly. Look for a button like "Apply" or "Replace" that Cursor provides, or manually copy the AI's code and paste it into the `AppState.h` file in your editor, replacing any placeholder content.

---

## Phase 2: Porting "The Engine" (Your Audio Logic) ⚙️

This is the most challenging phase. We'll ask the AI to translate the core audio input/output and processing logic into the main C++ component.

1.  **Open Core Files:** In Cursor's file explorer, open `Source/MainComponent.h` and `Source/MainComponent.cpp`.

2.  **Gather Context for AI:** This requires a lot of information! Open your *original* project folder and copy the *entire text content* of each of the following files, pasting them one after another into the Cursor chat:
    * `F9-Batch-Resampler/Services/AudioProcessingService.swift`
    * `F9-Batch-Resampler/Services/LatencyMeasurementService.swift`
    * `F9-Batch-Resampler/Services/HardwareLoopTestService.swift`
    * `F9-Batch-Resampler/Services/SineWaveGenerator.swift`
    * `LATENCY_TRIMMING_FIX.md` (Crucial for correct audio alignment!)
    * `REVERB_MODE_IMPLEMENTATION.md` (For the reverb tail logic)

3.  **Instruct the AI:** After pasting *all* the context above, paste this detailed prompt into the chat:

    ```prompt
    Now, please modify my `MainComponent.h` and `MainComponent.cpp` files to become the main audio engine for the application, based on all the Swift code and documentation I just provided.

    1.  In `MainComponent.h`, make sure to `#include "AppState.h"`. Add a public `AppState appState;` object as a member variable.
    2.  Modify the `MainComponent` class declaration in `.h` so it inherits from `public juce::AudioAppComponent` and `public juce::Timer`.
    3.  Implement the required `AudioAppComponent` functions in `.cpp`: `prepareToPlay(int samplesPerBlock, double sampleRate)`, `getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)`, and `releaseResources()`.
    4.  **Implement the core logic inside `getNextAudioBlock` in `.cpp`:** This function is called repeatedly by the audio system. It needs a state machine based on `appState`.
        * If `appState.isProcessing` is true, port the playback *and* capture logic from the `setOutputCallback` and `setInputCallback` within `AudioProcessingService.swift`. Read from the current file's data, write to `bufferToFill.buffer`, and copy the *input* data (`bufferToFill.buffer` also contains input when configured) into a temporary recording buffer. Check the stop condition (fixed length or reverb mode).
        * If `appState.isMeasuringLatency` is true, port the logic from `LatencyMeasurementService.swift`: send an impulse out (`bufferToFill.buffer`) and capture the input (`bufferToFill.buffer`) to find the peak.
        * If `appState.isPreviewing` is true, port the playback-only logic from `AudioProcessingService.previewFiles`.
        * If `appState.isTestingHardware` is true (you'll need to add this flag to `AppState`), port the sine wave generation and input capture from `HardwareLoopTestService`.
        * If none of the above are true, just fill `bufferToFill.buffer` with silence.
    5.  **Create C++ member functions in `MainComponent` (declare in `.h`, implement in `.cpp`) that replicate the *triggering* logic from the Swift services:**
        * `void startProcessing()`: Should set up the state for processing (load first file, set `appState.isProcessing = true`, etc.), replicating the start of `processAllFiles()`.
        * `void startLatencyMeasurement()`: Should set `appState.isMeasuringLatency = true`.
        * `void startPreview()`: Should load selected files, set `appState.isPreviewing = true`.
        * `void startHardwareTest()`: Should set `appState.isTestingHardware = true`.
        * `void stopAllAudio()`: A function to set all boolean flags (`isProcessing`, `isPreviewing`, etc.) to false.
    6.  **Implement Latency Trimming (CRITICAL):** Create a C++ function `juce::AudioBuffer<float> trimLatency(const juce::AudioBuffer<float>& capturedAudio, int latencySamplesToTrim, int sourceNumSamples)` inside `MainComponent.cpp`. Port the *exact logic* from the `trimLatency` function in `AudioProcessingService.swift`, using the description in `LATENCY_TRIMMING_FIX.md` to ensure you handle the sample counts correctly for `juce::AudioBuffer`. This function will be called *after* recording is finished (likely triggered from `getNextAudioBlock` via the `Timer` or `AsyncUpdater`).
    7.  **Implement Reverb Mode Logic (CRITICAL):** Create a C++ function `bool isReverbTailBelowNoiseFloor(const juce::AudioBuffer<float>& audioWindow)` inside `MainComponent.cpp`. Port the *exact logic* from the `isReverbTailBelowNoiseFloor` function in `AudioProcessingService.swift`, using the description in `REVERB_MODE_IMPLEMENTATION.md`. This will be called inside `getNextAudioBlock` when `appState.settings.useReverbMode` is true.
    8. Implement the `timerCallback()` function (from inheriting `juce::Timer`). Use this to handle tasks *after* audio processing finishes in `getNextAudioBlock`, like triggering the `trimLatency` function, saving the file, loading the next file, and applying the `silenceBetweenFilesMs` delay using `juce::Thread::sleep()`. Start the timer when processing begins.
    ```

4.  **Iterate and Debug with AI:**
    * This is a massive amount of complex code. **Apply the AI's changes.**
    * Expect errors (red underlines in Cursor, build errors later).
    * Use the Cursor chat:
        * Highlight code with errors and ask "What's wrong here?"
        * Paste error messages and ask "How do I fix this build error?"
        * Ask "You used a variable 'X' but didn't define it, can you add the definition?"
        * Ask "Can you explain what this part of the C++ code does?"
    * Be persistent. This phase requires the most back-and-forth with the AI.

---

## Phase 3: Porting "The UI" (The Interface) 🖼️

Now we create the visual buttons, sliders, and lists using JUCE components. We'll do this piece by piece.

**General Process for Each UI Part:**

1.  **Create Files:** In Cursor, create a new pair of files in the `Source` folder (e.g., `SettingsComponent.h` and `SettingsComponent.cpp`).
2.  **Gather Context:** Copy the *entire content* of the corresponding original `.swift` UI file into the Cursor chat.
3.  **Instruct AI:** Give the AI the specific prompt for that UI component (see below).
4.  **Apply Code:** Apply the generated code to the new `.h` and `.cpp` files.

---

### A. The Settings View

1.  **Files:** Create `SettingsComponent.h` and `SettingsComponent.cpp`.
2.  **Context:** Copy content of `F9-Batch-Resampler/Views/SettingsView.swift`.
3.  **Prompt:**

    ```prompt
    Create the C++ code for `SettingsComponent.h` and `SettingsComponent.cpp`. This class should inherit from `public juce::Component`.

    1.  It must take a reference to our `AppState` class in its constructor (e.g., `SettingsComponent(AppState& state)`). Store this reference.
    2.  Based on the `SettingsView.swift` file provided, declare private member variables in the `.h` file for these JUCE components:
        * `juce::ComboBox bufferSizeComboBox;`
        * `juce::TextButton measureLatencyButton { "Measure Latency" };`
        * `juce::Label latencyResultsLabel;`
        * `juce::TextButton selectFolderButton { "Select Destination Folder..." };`
        * `juce::Label folderPathLabel;`
        * `juce::Label postfixEditorLabel { "Filename Postfix:", "Filename Postfix:" };`
        * `juce::TextEditor postfixEditor;`
        * `juce::ToggleButton reverbModeToggle { "Reverb Mode (stop on noise floor)" };`
        * `juce::Slider noiseFloorMarginSlider;`
        * `juce::Slider silenceBetweenFilesSlider;`
        * `juce::Slider thresholdSlider;`
        * Add `juce::Label` components for all sliders to show their current values.
    3.  In the constructor in `.cpp`, use `addAndMakeVisible()` for all these components. Set up sliders (range, interval) based on the Swift code.
    4.  Implement the