//
//  ProcessingSettings.swift
//  F9-Batch-Resampler
//
//  Created by James Wiltshire on 09/09/2025.
//

import Foundation

enum BufferSize: Int, CaseIterable, Identifiable {
    case samples128 = 128
    case samples256 = 256
    case samples512 = 512
    case samples1024 = 1024

    var id: Int { rawValue }

    var displayName: String {
        "\(rawValue) samples"
    }
}

struct ProcessingSettings: Equatable {
    // MARK: - Global Audio Settings

    /// Standard sample rate for all audio operations (44.1kHz)
    /// NOTE: For prototype, everything is fixed at 44.1kHz
    /// TODO: Make this configurable to support multiple sample rates (48kHz, 88.2kHz, 96kHz, etc.)
    static let sampleRate: Double = 44100.0

    // MARK: - Processing Settings
    var sendOutputBusRange: ClosedRange<Int> = 3...4
    var returnInputBus: Int = 3
    var blockStereoOut: Bool = true
    var trimEnabled: Bool = true
    var dcRemovalEnabled: Bool = true
    var postPlaybackSafetyMs: Int = 250
    var thresholdDb: Float = -40.0
    
    // Audio interface settings
    var bufferSize: BufferSize = .samples256
    var measuredLatencySamples: Int? = nil
    var lastBufferSizeWhenMeasured: BufferSize? = nil
    var measuredNoiseFloorDb: Float? = nil
    
    // Processing settings
    var useReverbMode: Bool = false // Stop on noise floor instead of fixed length
    var noiseFloorMarginPercent: Float = 10.0 // % above noise floor to stop recording
    var silenceBetweenFilesMs: Int = 150 // Gap between files in preview/processing (for compressor/limiter reset)
    
    // Output settings
    var outputFolderPath: String? = nil
    var outputPostfix: String = "" // Empty = same filename

    // Monitoring settings
    var enableMonitoring: Bool = true // Monitor preview/process through main outputs
    var monitoringChannels: [Int] = [1, 2] // Default to channels 1+2
    
    /// Returns true if latency needs to be re-measured (buffer size changed since last measurement)
    var needsLatencyRemeasurement: Bool {
        guard let lastMeasured = lastBufferSizeWhenMeasured else {
            return measuredLatencySamples != nil // If we have a measurement but no record of buffer size, remeasure
        }
        return lastMeasured != bufferSize
    }
    
    /// Returns the measured latency in milliseconds at the current sample rate
    func latencyInMs(sampleRate: Double) -> Double? {
        guard let samples = measuredLatencySamples else { return nil }
        return (Double(samples) / sampleRate) * 1000.0
    }
    
    /// Returns the recording length in samples for a given source file length
    /// Includes latency compensation and safety buffer (4x latency)
    func recordingLength(sourceFileSamples: Int, latencySamples: Int) -> Int {
        return sourceFileSamples + latencySamples + (latencySamples * 4)
    }
    
    /// Returns the threshold in linear amplitude for the given dB value
    var thresholdLinear: Float {
        return pow(10.0, thresholdDb / 20.0)
    }
    
    /// Returns the noise floor threshold for reverb mode (noise floor + margin)
    var noiseFloorThresholdDb: Float? {
        guard let noiseFloor = measuredNoiseFloorDb else { return nil }
        return noiseFloor + (noiseFloor * noiseFloorMarginPercent / 100.0)
    }
}
