//
//  HardwareLoopTestService.swift
//  F9-Batch-Resampler
//
//  Tests hardware loop by generating sine wave and monitoring input
//

import Foundation
import CoreAudio
import AVFoundation

/// Error types for hardware loop testing
enum HardwareLoopTestError: LocalizedError {
    case noDeviceSelected
    case audioSystemNotInitialized
    case testFailed(String)
    
    var errorDescription: String? {
        switch self {
        case .noDeviceSelected:
            return "No audio device selected for testing"
        case .audioSystemNotInitialized:
            return "Audio system not initialized"
        case .testFailed(let message):
            return "Hardware loop test failed: \(message)"
        }
    }
}

/// Service for testing hardware loop with sine wave generation
@MainActor
class HardwareLoopTestService: ObservableObject {
    
    // MARK: - Published Properties
    
    @Published var isRunning = false
    @Published var testResults: String = ""
    @Published var inputLevel: Float = 0.0
    @Published var outputLevel: Float = 0.0
    @Published var isLoopDetected = false
    
    // MARK: - Private Properties
    
    private let audioSystem: CAAudioHardwareSystem
    private let sineWaveGenerator = SineWaveGenerator(frequency: 1000.0, sampleRate: ProcessingSettings.sampleRate)
    private var testStartTime: Date?
    private var inputSamples: [Float] = []
    private var outputSamples: [Float] = []
    private var maxInputLevel: Float = 0.0
    private var maxOutputLevel: Float = 0.0
    
    // MARK: - Initialization
    
    init(audioSystem: CAAudioHardwareSystem) {
        self.audioSystem = audioSystem
    }
    
    // MARK: - Public Methods
    
    /// Start hardware loop test
    /// - Parameters:
    ///   - outputPair: Output stereo pair
    ///   - inputPair: Input stereo pair
    ///   - bufferSize: Buffer size
    func startTest(
        outputPair: StereoPair,
        inputPair: StereoPair,
        bufferSize: BufferSize
    ) async throws {
        guard outputPair.deviceUID != nil else {
            throw HardwareLoopTestError.noDeviceSelected
        }
        
        guard audioSystem.isInitialized else {
            throw HardwareLoopTestError.audioSystemNotInitialized
        }
        
        // Reset test state
        isRunning = true
        testResults = "Starting hardware loop test..."
        inputLevel = 0.0
        outputLevel = 0.0
        isLoopDetected = false
        testStartTime = Date()
        inputSamples.removeAll()
        outputSamples.removeAll()
        maxInputLevel = 0.0
        maxOutputLevel = 0.0
        
        // Set up audio callbacks
        audioSystem.setOutputCallback { [weak self] buffer, frameCount, channelCount in
            self?.handleOutputCallback(buffer: buffer, frameCount: frameCount, channelCount: channelCount)
        }
        
        audioSystem.setInputCallback { [weak self] buffer, frameCount, channelCount in
            self?.handleInputCallback(buffer: buffer, frameCount: frameCount, channelCount: channelCount)
        }
        
        // Start audio system
        try audioSystem.start()
        
        // Update UI
        testResults = "Hardware loop test running... Generating 1 kHz sine wave"
        
        // Run test for 5 seconds
        try await Task.sleep(nanoseconds: 5_000_000_000) // 5 seconds
        
        // Stop test
        await stopTest()
    }
    
    /// Stop hardware loop test
    func stopTest() async {
        audioSystem.stop()
        isRunning = false
        
        // Analyze results
        analyzeTestResults()
    }
    
    // MARK: - Private Methods
    
    private func handleOutputCallback(buffer: UnsafeMutablePointer<Float>, frameCount: UInt32, channelCount: UInt32) {
        // Generate sine wave
        sineWaveGenerator.generateSineWave(
            buffer: buffer,
            frameCount: frameCount,
            channelCount: channelCount,
            amplitude: 0.5
        )
        
        // Calculate output level
        let outputLevel = calculateRMSLevel(buffer: buffer, frameCount: frameCount, channelCount: channelCount)
        
        Task { @MainActor in
            self.outputLevel = outputLevel
            self.maxOutputLevel = max(self.maxOutputLevel, outputLevel)
        }
        
        // Store output samples for analysis
        let samples = Array(UnsafeBufferPointer(start: buffer, count: Int(frameCount * channelCount)))
        outputSamples.append(contentsOf: samples)
    }
    
    private func handleInputCallback(buffer: UnsafeMutablePointer<Float>, frameCount: UInt32, channelCount: UInt32) {
        // Calculate input level
        let inputLevel = calculateRMSLevel(buffer: buffer, frameCount: frameCount, channelCount: channelCount)
        
        Task { @MainActor in
            self.inputLevel = inputLevel
            self.maxInputLevel = max(self.maxInputLevel, inputLevel)
        }
        
        // Store input samples for analysis
        let samples = Array(UnsafeBufferPointer(start: buffer, count: Int(frameCount * channelCount)))
        inputSamples.append(contentsOf: samples)
    }
    
    private func calculateRMSLevel(buffer: UnsafeMutablePointer<Float>, frameCount: UInt32, channelCount: UInt32) -> Float {
        var sum: Float = 0.0
        let sampleCount = Int(frameCount * channelCount)
        
        for i in 0..<sampleCount {
            let sample = buffer[i]
            sum += sample * sample
        }
        
        return sqrt(sum / Float(sampleCount))
    }
    
    private func analyzeTestResults() {
        guard !inputSamples.isEmpty && !outputSamples.isEmpty else {
            testResults = "❌ No audio data captured during test"
            return
        }
        
        let inputPeak = inputSamples.map { abs($0) }.max() ?? 0.0
        let outputPeak = outputSamples.map { abs($0) }.max() ?? 0.0
        
        // Check if we detected the loop
        let hasInput = inputPeak > 0.01 // Threshold for detecting input
        let hasOutput = outputPeak > 0.01 // Threshold for detecting output
        
        if hasInput && hasOutput {
            isLoopDetected = true
            testResults = """
            ✅ Hardware loop detected!
            
            Input Peak: \(String(format: "%.3f", inputPeak))
            Output Peak: \(String(format: "%.3f", outputPeak))
            Max Input Level: \(String(format: "%.3f", maxInputLevel))
            Max Output Level: \(String(format: "%.3f", maxOutputLevel))
            
            Audio is flowing through the hardware successfully.
            """
        } else if hasOutput && !hasInput {
            testResults = """
            ⚠️ Output detected but no input
            
            Output Peak: \(String(format: "%.3f", outputPeak))
            Max Output Level: \(String(format: "%.3f", maxOutputLevel))
            
            Check input connections and device settings.
            """
        } else if !hasOutput && !hasInput {
            testResults = """
            ❌ No audio detected
            
            Check device selection and audio system initialization.
            """
        } else {
            testResults = """
            ⚠️ Unexpected state
            
            Input Peak: \(String(format: "%.3f", inputPeak))
            Output Peak: \(String(format: "%.3f", outputPeak))
            """
        }
    }
}

