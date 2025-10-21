import Foundation

@MainActor
final class MainViewModel: ObservableObject {
    @Published var devices: [AudioDevice] = []
    @Published var selectedDeviceID: String? {
        didSet {
            syncSelectionForDeviceChange(previousDeviceID: oldValue)
        }
    }
    @Published var selectedInputPair: StereoPair?
    @Published var selectedOutputPair: StereoPair?

    @Published var files: [AudioFile] = []
    @Published var settings = ProcessingSettings()

    @Published var logLines: [String] = []
    @Published var isMeasuringLatency = false
    @Published var showLatencyRemeasureAlert = false
    @Published var isProcessing = false
    @Published var isPreviewing = false
    @Published var processingProgress: Double = 0.0
    @Published var currentProcessingFile: String = ""
    @Published var currentPreviewFileIndex: Int?
    @Published var previewProgress: Double = 0.0
    @Published var previewPlaylist: [UUID] = []

    private let audio = AudioService()
    private let latencyService = LatencyMeasurementService()
    private let processingService = AudioProcessingService()
    private let audioSystem = CAAudioHardwareSystem()
    lazy var hardwareLoopTest = HardwareLoopTestService(audioSystem: audioSystem)
    private var previewTask: Task<Void, Never>?
    private var lastSyncedDeviceID: String?

    init() {
        // Populate list early so the UI shows items even on non-studio Macs
        Task {
            await refreshDevicesAsync()
            appendLog("Enumerated \(devices.count) audio device(s)")
        }
        requestMic()
    }

    func refreshDevicesAsync() async {
        let previousDeviceID = selectedDeviceID
        let previousInputPairID = selectedInputPair?.id
        let previousOutputPairID = selectedOutputPair?.id
        
        // Wait for the audio system to load devices
        while audioSystem.availableDevices.isEmpty {
            try? await Task.sleep(nanoseconds: 100_000_000) // 100ms
        }
        
        // Use the new CAAudioHardwareSystem for device enumeration
        devices = audioSystem.availableDevices
            .filter { !$0.isBuiltIn } // Filter out built-in Apple devices
        
        appendLog("Found \(devices.count) external audio device(s)")
        
        // Restore previous selections
        if let previousDeviceID = previousDeviceID,
           devices.contains(where: { $0.uniqueID == previousDeviceID }) {
            selectedDeviceID = previousDeviceID
        }
        
        if let previousInputPairID = previousInputPairID,
           let device = devices.first(where: { $0.uniqueID == selectedDeviceID }) {
            let inputPairs = device.stereoPairs(isInput: true)
            if inputPairs.contains(where: { $0.id == previousInputPairID }) {
                selectedInputPair = inputPairs.first { $0.id == previousInputPairID }
            }
        }
        
        if let previousOutputPairID = previousOutputPairID,
           let device = devices.first(where: { $0.uniqueID == selectedDeviceID }) {
            let outputPairs = device.stereoPairs(isInput: false)
            if outputPairs.contains(where: { $0.id == previousOutputPairID }) {
                selectedOutputPair = outputPairs.first { $0.id == previousOutputPairID }
            }
        }
    }
    
    func refreshDevices() {
        let previousDeviceID = selectedDeviceID
        let previousInputPairID = selectedInputPair?.id
        let previousOutputPairID = selectedOutputPair?.id
        
        // Use the new CAAudioHardwareSystem for device enumeration
        devices = audioSystem.availableDevices
            .filter { !$0.isBuiltIn } // Filter out built-in Apple devices
        
        appendLog("Found \(devices.count) external audio device(s)")
        
        if devices.isEmpty {
            selectedDeviceID = nil
            selectedInputPair = nil
            selectedOutputPair = nil
            appendLog("⚠️ No external audio device available for selection")
            return
        }
        
        if let previousID = previousDeviceID,
           devices.contains(where: { $0.uniqueID == previousID }) {
            selectedDeviceID = previousID
            restoreChannelPairsIfNeeded(previousInputID: previousInputPairID, previousOutputID: previousOutputPairID)
        } else if let symDevice = devices.first(where: { $0.name.localizedCaseInsensitiveContains("symphony") }) {
            selectedDeviceID = symDevice.uniqueID
        } else {
            selectedDeviceID = devices.first?.uniqueID
        }
    }
    
    var selectedDevice: AudioDevice? {
        guard let id = selectedDeviceID else { return nil }
        return devices.first(where: { $0.uniqueID == id })
    }
    
    /// Get all available input stereo pairs from selected device
    var availableInputPairs: [StereoPair] {
        guard let device = selectedDevice else { return [] }
        return device.stereoPairs(isInput: true)
    }
    
    /// Get all available output stereo pairs from selected device
    var availableOutputPairs: [StereoPair] {
        guard let device = selectedDevice else { return [] }
        return device.stereoPairs(isInput: false)
    }
    
    /// Returns true if both input and output pairs are selected
    var canMeasureLatency: Bool {
        selectedDevice != nil && selectedInputPair != nil && selectedOutputPair != nil
    }
    
    /// Measures the round-trip latency and noise floor through the hardware loop
    func measureLatency() async {
        guard let inputPair = selectedInputPair,
              let outputPair = selectedOutputPair else {
            appendLog("❌ Cannot measure latency: No input/output selected")
            return
        }
        
        isMeasuringLatency = true
        appendLog("🔄 Measuring latency and noise floor...")
        appendLog("   Output: \(outputPair.displayName)")
        appendLog("   Input: \(inputPair.displayName)")
        appendLog("   Buffer: \(settings.bufferSize.rawValue) samples")
        
        do {
            let result = try await latencyService.measureLatency(
                outputPair: outputPair,
                inputPair: inputPair,
                bufferSize: settings.bufferSize
            )
            
            settings.measuredLatencySamples = result.latencySamples
            settings.measuredNoiseFloorDb = result.noiseFloorDb
            settings.lastBufferSizeWhenMeasured = settings.bufferSize

            let latencyMs = settings.latencyInMs(sampleRate: ProcessingSettings.sampleRate) ?? 0
            appendLog("✅ Latency measured: \(result.latencySamples) samples (\(String(format: "%.2f", latencyMs)) ms @ 44.1kHz)")
            appendLog("✅ Noise floor measured: \(String(format: "%.1f", result.noiseFloorDb)) dB")
            
        } catch let error as LatencyMeasurementError {
            appendLog("❌ Latency measurement failed: \(error.localizedDescription)")
        } catch {
            appendLog("❌ Latency measurement failed: \(error.localizedDescription)")
        }
        
        isMeasuringLatency = false
    }
    
    /// Process all files through the hardware loop
    func processAllFiles() async {
        guard let inputPair = selectedInputPair,
              let outputPair = selectedOutputPair else {
            appendLog("❌ Cannot process: No input/output selected")
            return
        }

        // Require output folder to be set to prevent overwriting originals
        guard let outputFolder = settings.outputFolderPath, !outputFolder.isEmpty else {
            appendLog("❌ Cannot process: No destination folder selected - choose one in Settings to protect your original files")
            return
        }

        let filesToProcess = files.filter { $0.isValid }
        guard !filesToProcess.isEmpty else {
            appendLog("❌ No valid files to process")
            return
        }

        // Auto-measure latency if not measured or if buffer size changed
        if settings.measuredLatencySamples == nil || settings.needsLatencyRemeasurement {
            appendLog("⚠️ Latency measurement required before processing")
            appendLog("🔄 Auto-measuring latency...")

            await measureLatency()

            // Check if measurement succeeded
            guard settings.measuredLatencySamples != nil else {
                appendLog("❌ Processing cancelled: Latency measurement failed")
                return
            }
        }

        isProcessing = true
        appendLog("🎬 Starting batch processing of \(filesToProcess.count) file(s)")

        do {
            // Use batch processing method - initializes once for all files
            let outputURLs = try await processingService.processFiles(
                filesToProcess,
                outputPair: outputPair,
                inputPair: inputPair,
                settings: settings
            ) { file, progress in
                Task { @MainActor in
                    self.currentProcessingFile = file.fileName
                    self.processingProgress = progress
                }
            }

            // Mark all as completed and log
            for (file, url) in zip(filesToProcess, outputURLs) {
                if let fileIndex = files.firstIndex(where: { $0.id == file.id }) {
                    files[fileIndex].status = .completed
                }
                appendLog("✅ Completed: \(url.lastPathComponent)")
            }

        } catch let error as AudioProcessingError {
            appendLog("❌ Batch failed: \(error.errorDescription ?? error.localizedDescription)")
            // Mark remaining files as failed
            for file in filesToProcess {
                if let fileIndex = files.firstIndex(where: { $0.id == file.id }),
                   files[fileIndex].status != .completed {
                    files[fileIndex].status = .failed
                }
            }
        } catch {
            appendLog("❌ Batch failed: \(error.localizedDescription)")
            for file in filesToProcess {
                if let fileIndex = files.firstIndex(where: { $0.id == file.id }),
                   files[fileIndex].status != .completed {
                    files[fileIndex].status = .failed
                }
            }
        }
        
        isProcessing = false
        currentProcessingFile = ""
        appendLog("🏁 Batch processing complete")
    }
    
    /// Toggle preview mode
    func togglePreview() {
        if isPreviewing {
            stopPreview()
        } else {
            startPreview()
        }
    }
    
    /// Start previewing selected files
    private func startPreview() {
        guard let inputPair = selectedInputPair,
              let outputPair = selectedOutputPair else {
            appendLog("❌ Cannot preview: No input/output selected")
            return
        }
        
        let selectedFiles = files.filter { $0.isSelected && $0.isValid }
        guard !selectedFiles.isEmpty else {
            appendLog("❌ No valid files selected for preview")
            return
        }
        
        previewPlaylist = selectedFiles.map { $0.id }
        currentPreviewFileIndex = 0
        previewProgress = 0
        isPreviewing = true
        appendLog("▶️ Preview started with \(selectedFiles.count) file(s)")
        
        previewTask = Task {
            do {
                try await processingService.previewFiles(
                    selectedFiles,
                    outputPair: outputPair,
                    inputPair: inputPair,
                    settings: settings,
                    onFileChange: { index in
                        Task { @MainActor in
                            if index < self.previewPlaylist.count {
                                self.currentPreviewFileIndex = index
                                self.previewProgress = 0
                            }
                        }
                    },
                    onProgress: { index, progress in
                        Task { @MainActor in
                            if index < self.previewPlaylist.count {
                                self.currentPreviewFileIndex = index
                                self.previewProgress = progress
                            }
                        }
                    }
                )
            } catch is CancellationError {
                // Expected when user stops preview
            } catch {
                await MainActor.run {
                    self.appendLog("❌ Preview error: \(error.localizedDescription)")
                }
            }
            await MainActor.run {
                self.isPreviewing = false
                self.previewPlaylist = []
                self.currentPreviewFileIndex = nil
                self.previewProgress = 0
            }
        }
    }
    
    /// Stop previewing
    private func stopPreview() {
        isPreviewing = false
        previewTask?.cancel()
        previewTask = nil
        previewPlaylist = []
        currentPreviewFileIndex = nil
        previewProgress = 0
        appendLog("⏸️ Preview stopped")
    }
    
    /// Add files from URLs
    func addFiles(urls: [URL]) {
        for url in urls {
            var file = AudioFile(url: url)
            file.loadMetadata()
            files.append(file)
            
            if file.isValid {
                appendLog("📄 Added: \(file.fileName) (\(String(format: "%.1f", file.sampleRate ?? 0))kHz)")
            } else {
                appendLog("⚠️ Invalid sample rate: \(file.fileName)")
            }
        }
    }
    
    /// Toggle selection for a file
    func toggleFileSelection(_ file: AudioFile) {
        if let index = files.firstIndex(where: { $0.id == file.id }) {
            files[index].isSelected.toggle()
        }
    }
    
    /// Select all files
    func selectAllFiles() {
        for index in files.indices {
            files[index].isSelected = true
        }
    }
    
    /// Deselect all files
    func deselectAllFiles() {
        for index in files.indices {
            files[index].isSelected = false
        }
    }
    
    /// Called when buffer size changes to check if remeasurement is needed
    func onBufferSizeChanged() {
        if settings.needsLatencyRemeasurement && settings.measuredLatencySamples != nil {
            showLatencyRemeasureAlert = true
            appendLog("⚠️ Buffer size changed - latency should be re-measured")
        }
    }

    func appendLog(_ s: String) {
        let ts = ISO8601DateFormatter().string(from: Date())
        logLines.append("[\(ts)] \(s)")
    }

    private func requestMic() {
        audio.requestMicrophoneAccess { [weak self] granted in
            Task { @MainActor in
                if granted {
                    self?.appendLog("Microphone access granted")
                } else {
                    self?.appendLog("Microphone access denied - recording will not work")
                }
            }
        }
    }
    
    private func syncSelectionForDeviceChange(previousDeviceID: String?) {
        let previousInputID = selectedInputPair?.id
        let previousOutputID = selectedOutputPair?.id
        let newDeviceID = selectedDeviceID
        
        guard let device = selectedDevice else {
            if previousDeviceID != nil {
                appendLog("Audio device deselected")
            }
            selectedInputPair = nil
            selectedOutputPair = nil
            lastSyncedDeviceID = nil
            return
        }
        
        let deviceChanged = previousDeviceID != newDeviceID || lastSyncedDeviceID != newDeviceID
        lastSyncedDeviceID = newDeviceID
        
        let inputPairs = device.stereoPairs(isInput: true)
        let outputPairs = device.stereoPairs(isInput: false)
        
        if deviceChanged {
            let prefix = previousDeviceID == nil ? "Auto-selected device" : "Selected device"
            appendLog("\(prefix): \(device.name)")
        }
        
        if deviceChanged {
            if let firstInput = inputPairs.first {
                selectedInputPair = firstInput
                appendLog("Auto-selected input: \(firstInput.displayName)")
            } else {
                selectedInputPair = nil
                appendLog("⚠️ \(device.name) has no stereo input pairs")
            }
            
            if let firstOutput = outputPairs.first {
                selectedOutputPair = firstOutput
                appendLog("Auto-selected output: \(firstOutput.displayName)")
            } else {
                selectedOutputPair = nil
                appendLog("⚠️ \(device.name) has no stereo output pairs")
            }
        } else {
            if let prevInputID = previousInputID,
               let match = inputPairs.first(where: { $0.id == prevInputID }) {
                selectedInputPair = match
            } else if let firstInput = inputPairs.first {
                selectedInputPair = firstInput
                appendLog("Auto-selected input: \(firstInput.displayName)")
            } else {
                selectedInputPair = nil
                appendLog("⚠️ \(device.name) has no stereo input pairs")
            }
            
            if let prevOutputID = previousOutputID,
               let match = outputPairs.first(where: { $0.id == prevOutputID }) {
                selectedOutputPair = match
            } else if let firstOutput = outputPairs.first {
                selectedOutputPair = firstOutput
                appendLog("Auto-selected output: \(firstOutput.displayName)")
            } else {
                selectedOutputPair = nil
                appendLog("⚠️ \(device.name) has no stereo output pairs")
            }
        }
    }
    
    private func restoreChannelPairsIfNeeded(previousInputID: String?, previousOutputID: String?) {
        guard let device = selectedDevice else { return }
        
        let inputPairs = device.stereoPairs(isInput: true)
        if let prevID = previousInputID,
           let match = inputPairs.first(where: { $0.id == prevID }) {
            selectedInputPair = match
        } else if selectedInputPair == nil {
            selectedInputPair = inputPairs.first
        }
        
        let outputPairs = device.stereoPairs(isInput: false)
        if let prevID = previousOutputID,
           let match = outputPairs.first(where: { $0.id == prevID }) {
            selectedOutputPair = match
        } else if selectedOutputPair == nil {
            selectedOutputPair = outputPairs.first
        }
    }
    
    // MARK: - Hardware Loop Test
    
    func startHardwareLoopTest() {
        guard let outputPair = selectedOutputPair,
              let inputPair = selectedInputPair else {
            appendLog("❌ Please select both input and output pairs for hardware loop test")
            return
        }
        
        Task {
            do {
                // Initialize audio system first
                try await audioSystem.initialize(
                    deviceUID: outputPair.deviceUID!,
                    inputChannels: inputPair.channels,
                    outputChannels: outputPair.channels,
                    bufferSize: UInt32(settings.bufferSize.rawValue)
                )
                
                appendLog("🔧 Starting hardware loop test with 1 kHz sine wave...")
                
                // Start the test
                try await hardwareLoopTest.startTest(
                    outputPair: outputPair,
                    inputPair: inputPair,
                    bufferSize: settings.bufferSize
                )
                
                appendLog("✅ Hardware loop test completed")
                appendLog(hardwareLoopTest.testResults)
                
            } catch {
                appendLog("❌ Hardware loop test failed: \(error.localizedDescription)")
            }
        }
    }
    
    func stopHardwareLoopTest() {
        Task {
            await hardwareLoopTest.stopTest()
            appendLog("🛑 Hardware loop test stopped")
        }
    }
}
