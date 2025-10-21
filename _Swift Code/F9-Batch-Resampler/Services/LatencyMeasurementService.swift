//
//  LatencyMeasurementService.swift
//  F9-Batch-Resampler
//
//  Handles latency measurement and noise floor detection using the bridge
//

import Foundation
import AVFoundation
import CoreAudio

@preconcurrency import AVFoundation

// MARK: - Error Types

enum LatencyMeasurementError: LocalizedError {
    case noDeviceSelected
    case deviceNotFound
    case audioUnitSetupFailed
    case timeout
    case noImpulseDetected
    case audioSystemError(Error)
    
    var errorDescription: String? {
        switch self {
        case .noDeviceSelected:
            return "No audio device selected"
        case .deviceNotFound:
            return "Audio device not found"
        case .audioUnitSetupFailed:
            return "Failed to setup audio unit"
        case .timeout:
            return "Measurement timed out"
        case .noImpulseDetected:
            return "No impulse detected in recording"
        case .audioSystemError(let error):
            return "Audio system error: \(error.localizedDescription)"
        }
    }
}

// MARK: - Service

@MainActor
class LatencyMeasurementService: ObservableObject {
    
    private let audioSystem = CAAudioHardwareSystem()
    
    // MARK: - Public Methods
    
    /// Measure round-trip latency and noise floor
    /// - Parameters:
    ///   - outputPair: Output stereo pair
    ///   - inputPair: Input stereo pair
    ///   - bufferSize: Buffer size enum
    /// - Returns: Tuple of (latency in samples, noise floor in dB)
    func measureLatency(
        outputPair: StereoPair,
        inputPair: StereoPair,
        bufferSize: BufferSize
    ) async throws -> (latencySamples: Int, noiseFloorDb: Float) {
        
        guard let deviceUID = outputPair.deviceUID else {
            throw LatencyMeasurementError.noDeviceSelected
        }
        
        // Initialize audio system with device
        try await audioSystem.initialize(deviceUID: deviceUID,
                                         inputChannels: inputPair.channels,
                                         outputChannels: outputPair.channels,
                                         bufferSize: UInt32(bufferSize.rawValue))
        
        // Set up audio callbacks for latency measurement
        var capturedAudio: [Float] = []
        var impulseSent = false
        var impulseTime: UInt64 = 0
        
        audioSystem.setInputCallback { buffer, frameCount, channelCount in
            // Capture audio data for analysis
            let sampleCount = Int(frameCount) * Int(channelCount)
            let samples = Array(UnsafeBufferPointer(start: UnsafePointer(buffer), count: sampleCount))
            capturedAudio.append(contentsOf: samples)
        }
        
        audioSystem.setOutputCallback { buffer, frameCount, channelCount in
            // Send impulse signal
            if !impulseSent {
                impulseTime = self.audioSystem.getCurrentSampleTime()
                self.sendImpulse(buffer: buffer, frameCount: frameCount, channelCount: channelCount)
                impulseSent = true
            } else {
                // Send silence after impulse
                memset(buffer, 0, Int(frameCount * channelCount) * MemoryLayout<Float>.size)
            }
        }
        
        // Start audio system
        try audioSystem.start()
        defer { audioSystem.stop() }
        
        // Wait for measurement to complete
        let timeoutSeconds = 5.0
        let startTime = Date()
        
        while Date().timeIntervalSince(startTime) < timeoutSeconds {
            if capturedAudio.count > 100000 { // Enough samples captured
                break
            }
            try await Task.sleep(nanoseconds: 10_000_000) // 10ms
        }
        
        audioSystem.stop()
        
        // Analyze captured audio for impulse detection
        guard !capturedAudio.isEmpty else {
            throw LatencyMeasurementError.noImpulseDetected
        }
        
        let (latencySamples, noiseFloorDb) = try analyzeCapturedAudio(
            capturedAudio,
            impulseTime: impulseTime,
            sampleRate: ProcessingSettings.sampleRate
        )
        
        return (latencySamples: latencySamples, noiseFloorDb: noiseFloorDb)
    }
    
    // MARK: - Private Methods
    
    private func sendImpulse(buffer: UnsafeMutablePointer<Float>, frameCount: UInt32, channelCount: UInt32) {
        let amplitude: Float = 0.9
        let samplesPerChannel = Int(frameCount)
        
        for frame in 0..<samplesPerChannel {
            for channel in 0..<Int(channelCount) {
                let sampleIndex = frame * Int(channelCount) + channel
                if frame == 0 {
                    // Send impulse on first frame
                    buffer[sampleIndex] = amplitude
                } else {
                    buffer[sampleIndex] = 0.0
                }
            }
        }
    }
    
    private func analyzeCapturedAudio(_ audio: [Float], impulseTime: UInt64, sampleRate: Double) throws -> (Int, Float) {
        // Simple peak detection for impulse
        let threshold: Float = 0.1
        var maxValue: Float = 0.0
        var maxIndex = 0
        
        for (index, sample) in audio.enumerated() {
            if abs(sample) > maxValue {
                maxValue = abs(sample)
                maxIndex = index
            }
        }
        
        guard maxValue > threshold else {
            throw LatencyMeasurementError.noImpulseDetected
        }
        
        // Calculate latency (simplified - in real implementation you'd use cross-correlation)
        let latencySamples = maxIndex
        
        // Calculate noise floor (RMS of quiet sections)
        let noiseFloorDb = calculateNoiseFloor(audio)
        
        return (latencySamples, noiseFloorDb)
    }
    
    private func calculateNoiseFloor(_ audio: [Float]) -> Float {
        // Calculate RMS of the audio signal
        let sumOfSquares = audio.reduce(0.0) { $0 + $1 * $1 }
        let rms = sqrt(sumOfSquares / Float(audio.count))
        
        // Convert to dB
        let db = 20.0 * log10(max(rms, 1e-6))
        return Float(db)
    }
}
