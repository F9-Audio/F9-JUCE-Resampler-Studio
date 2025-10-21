How the MCFX App Wrapper Handles Multichannel Audio
The MCFX application uses a standard JUCE class, StandalonePluginHolder, as its core audio engine. This object is responsible for all communication with the computer's audio hardware on both Mac and PC.

The process is based on hardware abstraction—the application code is cross-platform, and JUCE handles the platform-specific details.

1. The Core Audio Engine: StandalonePluginHolder
In the MCFX project, the main application window (CustomStandaloneFilterWindow) does not inherit from AudioAppComponent (which is the class you are using).

Instead, it creates and owns an instance of StandalonePluginHolder. This single class acts as the application's entire audio engine.

This StandalonePluginHolder is responsible for:

Owning and managing the AudioDeviceManager.

Displaying the "Audio Settings..." dialog box.

Running the high-priority audio callback.

2. Cross-Platform Driver Management (Mac vs. PC)
The key to its cross-platform ability is the AudioDeviceManager (which the StandalonePluginHolder controls). This JUCE class is an abstraction layer that communicates with the native audio drivers of the operating system.

On macOS: The AudioDeviceManager talks to CoreAudio. When the user selects their multichannel interface, it uses CoreAudio to open all available inputs and outputs.

On Windows: The AudioDeviceManager talks to ASIO, WASAPI, or DirectSound. For professional multichannel hardware, it will almost always use the manufacturer's ASIO driver.

The application code in Main.cpp and CustomStandaloneFilterWindow.h never has to worry about which OS it's on. It simply tells the AudioDeviceManager what it wants, and the manager handles the translation to CoreAudio or ASIO.

3. Requesting and Opening Multichannel Devices
The MCFX application is configured to request a specific, large number of channels from the audio hardware.

Configuration: When the application starts, it tells the StandalonePluginHolder what its ideal channel layout is (this is passed as the constrainToConfiguration parameter).

Device Setup: The user opens the "Audio Settings..." dialog and selects their multichannel audio interface.

Opening the Device: The AudioDeviceManager then attempts to open that device (e.g., an 18-in/20-out interface) with the full channel count that the application requested.

4. The Multichannel Audio Callback
Once the device is open, the audio callback starts running. This is where the simultaneous multichannel processing happens.

The operating system's driver (CoreAudio or ASIO) sends a block of audio data to the AudioDeviceManager.

This data is not just stereo. It is a single, large AudioSampleBuffer that contains all active channels (e.g., 16 input channels and 16 output channels).

The StandalonePluginHolder receives this large buffer and passes it to its internal processing logic (in this case, the mcfx_filter's processBlock function).

That logic then runs its algorithms (filters, etc.) on all channels in the buffer at once before passing the modified buffer back to the hardware.

Summary
The MCFX application wrapper achieves multichannel I/O by using the StandalonePluginHolder as its engine. This holder's AudioDeviceManager abstracts away the OS-specific drivers (CoreAudio on Mac, ASIO on PC) and opens the selected hardware with a specific, large channel count. The application's audio callback then receives a single buffer containing all channels, allowing it to process them simultaneously.

/// Code Examples

Here is a markdown document extracting the exact code that enables the MCFX standalone application to handle multichannel audio on a Mac or PC.

The application's multichannel capability is achieved by a collaboration between two main parts:

The Application Wrapper (StandalonePluginHolder): This is the engine that talks to the computer's hardware (CoreAudio on Mac, ASIO on PC).

The Audio Processor (LowhighpassAudioProcessor): This is the "brain" that tells the wrapper how many channels it needs.

1. The Wrapper: Creating the Audio Engine
The application's entry point (Main.cpp) creates a window (CustomStandaloneFilterWindow), which in turn creates the master audio engine: StandalonePluginHolder. This object manages all audio I/O.

File: kronihias/mcfx/mcfx-c9d2d54181c13199b029ec381f8389044d7267c3/standalone-filter/CustomStandaloneFilterWindow.h

C++

// ... inside CustomStandaloneFilterWindow constructor ...
pluginHolder.reset (new StandalonePluginHolder (settingsToUse, takeOwnershipOfSettings,
                                                preferredDefaultDeviceName, preferredSetupOptions,
                                                constrainToConfiguration, autoOpenMidiDevices));
How it works:

pluginHolder (an instance of StandalonePluginHolder) is the object that starts and runs the audio device.

It is responsible for opening the audio hardware (CoreAudio/ASIO) and running the high-priority audio callback.

Crucially, it is passed a constrainToConfiguration variable. This tells the audio engine what channel layout to request from the hardware.

2. The Request: Defining the Multichannel Layout
The StandalonePluginHolder doesn't know how many channels to ask for. It gets this information from the AudioProcessor it is built to host. This is where the multichannel requirement is defined.

File: kronihias/mcfx/mcfx-c9d2d54181c13199b029ec381f8389044d7267c3/mcfx_filter/Source/PluginProcessor.cpp

C++

LowhighpassAudioProcessor::LowhighpassAudioProcessor() :
    AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::discreteChannels(NUM_CHANNELS), true)
        .withOutput ("Output", juce::AudioChannelSet::discreteChannels(NUM_CHANNELS), true)
    ),
// ...
File: kronihias/mcfx/mcfx-c9d2d54181c13199b029ec381f8389044d7267c3/mcfx_filter/Source/FilterInfo.h

C++

#define NUM_CHANNELS 16
How it works:

The AudioProcessor's constructor explicitly tells the host (the StandalonePluginHolder) that it wants a layout of discrete channels.

It uses NUM_CHANNELS (defined as 16) to request 16 inputs and 16 outputs.

When the user selects an audio interface, the StandalonePluginHolder (wrapper) will try to open that device with this 16x16 layout.

3. The Gatekeeper: Enforcing the Multichannel Layout
This is the most critical part. The application wrapper (via StandalonePluginHolder) asks the audio processor if the user's selected hardware is acceptable. The processor rejects any device that cannot provide the full 16x16 layout.

File: kronihias/mcfx/mcfx-c9d2d54181c13199b029ec381f8389044d7267c3/mcfx_filter/Source/PluginProcessor.cpp

C++

bool LowhighpassAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return ((layouts.getMainOutputChannelSet().size() == NUM_CHANNELS) &&
            (layouts.getMainInputChannelSet().size() == NUM_CHANNELS));
}
How it works:

If the user selects a 2-channel sound card, the wrapper asks, "Is a 2-in, 2-out layout okay?"

This function will check layouts.getMainOutputChannelSet().size() == 16 (which is false) and return false. The audio engine will fail to start.

If the user selects a 16+ channel interface, the wrapper asks, "Is a 16-in, 16-out layout okay?"

This function returns true, and the StandalonePluginHolder successfully opens the device with 16 active channels.

4. The Processing: Handling the Multichannel Buffer
Once the device is open, the wrapper's audio callback runs. It receives a single, large buffer containing all 16 channels from the hardware and passes it to the processBlock function to be processed simultaneously.

File: kronihias/mcfx/mcfx-c9d2d54181c13199b029ec381f8389044d7267c3/mcfx_filter/Source/PluginProcessor.cpp

C++

void LowhighpassAudioProcessor::processBlock (AudioSampleBuffer& buffer, MidiBuffer& midiMessages)
{
    // ... (analysis code) ...

    // LOW CUT - bypass is not clickfree
    if (_lc_on_param > 0.5f) {
        _LC_IIR_1.processBlock(buffer);

        // second stage -> 4th order butterworth
        if (_lc_order_param > 0.5f)
            _LC_IIR_2.processBlock(buffer);
    }
    
    // ... (other filters) ...

    _PF_IIR_1.processBlock(buffer);
    _PF_IIR_2.processBlock(buffer);

    _LS_IIR.processBlock(buffer);
    _HS_IIR.processBlock(buffer);

    // ... (analysis code) ...
}
How it works:

The buffer variable passed into this function is not stereo; it is the full 16-channel buffer from the hardware.

The filter objects (e.g., _LC_IIR_1) are instances of SmoothIIRFilter, which is templated with NUM_CHANNELS.

When _LC_IIR_1.processBlock(buffer) is called, the filter's own processBlock iterates through all 16 channels and applies its algorithm to each one.