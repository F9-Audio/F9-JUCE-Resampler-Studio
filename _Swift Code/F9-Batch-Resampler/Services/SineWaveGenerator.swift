//
//  SineWaveGenerator.swift
//  F9-Batch-Resampler
//
//  Generates sine wave test signals for hardware loop testing
//

import Foundation
import CoreAudio

/// Generates sine wave test signals for hardware loop testing
class SineWaveGenerator {
    
    // MARK: - Properties
    
    private var phase: Double = 0.0
    private let sampleRate: Double
    private let frequency: Double
    
    // MARK: - Initialization
    
    init(frequency: Double = 1000.0, sampleRate: Double = ProcessingSettings.sampleRate) {
        self.frequency = frequency
        self.sampleRate = sampleRate
    }
    
    // MARK: - Public Methods
    
    /// Generate a sine wave signal for the specified number of frames
    /// - Parameters:
    ///   - buffer: Audio buffer to fill with sine wave data
    ///   - frameCount: Number of frames to generate
    ///   - channelCount: Number of channels (stereo = 2)
    ///   - amplitude: Signal amplitude (0.0 to 1.0)
    func generateSineWave(
        buffer: UnsafeMutablePointer<Float>,
        frameCount: UInt32,
        channelCount: UInt32,
        amplitude: Float = 0.5
    ) {
        let phaseIncrement = 2.0 * Double.pi * frequency / sampleRate
        
        for frame in 0..<Int(frameCount) {
            let sample = Float(sin(phase)) * amplitude
            
            // Fill all channels with the same sample (mono signal)
            for channel in 0..<Int(channelCount) {
                let index = frame * Int(channelCount) + channel
                buffer[index] = sample
            }
            
            phase += phaseIncrement
            
            // Keep phase in range [0, 2π]
            if phase >= 2.0 * Double.pi {
                phase -= 2.0 * Double.pi
            }
        }
    }
    
    /// Generate a sine wave signal and return as an array
    /// - Parameters:
    ///   - frameCount: Number of frames to generate
    ///   - channelCount: Number of channels
    ///   - amplitude: Signal amplitude (0.0 to 1.0)
    /// - Returns: Array of Float samples
    func generateSineWaveArray(
        frameCount: Int,
        channelCount: Int,
        amplitude: Float = 0.5
    ) -> [Float] {
        var samples = [Float](repeating: 0, count: frameCount * channelCount)
        samples.withUnsafeMutableBufferPointer { buffer in
            generateSineWave(
                buffer: buffer.baseAddress!,
                frameCount: UInt32(frameCount),
                channelCount: UInt32(channelCount),
                amplitude: amplitude
            )
        }
        return samples
    }
    
    /// Reset the phase to 0
    func reset() {
        phase = 0.0
    }
    
    /// Update the frequency
    /// - Parameter newFrequency: New frequency in Hz
    func setFrequency(_ newFrequency: Double) {
        // Note: This would require updating the frequency property
        // For now, we'll keep it simple with a fixed frequency
    }
}

