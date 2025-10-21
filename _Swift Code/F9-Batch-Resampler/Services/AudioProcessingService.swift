//
//  AudioProcessingService.swift
//  F9-Batch-Resampler
//
//  Handles audio file processing (playback and recording through hardware) via the bridge
//

import Foundation
import AVFoundation
import CoreAudio

@preconcurrency import AVFoundation

// MARK: - Error Types

enum AudioProcessingError: LocalizedError {
    case noDeviceSelected
    case deviceNotFound
    case interfaceDisconnected
    case fileReadError
    case fileWriteError
    case invalidFormat
    case audioUnitSetupFailed
    case noAudioCaptured
    case processingFailed(String)
    case audioSystemError(AudioSystemError)
    
    var errorDescription: String? {
        switch self {
        case .noDeviceSelected:
            return "No audio device selected"
        case .deviceNotFound:
            return "Audio device not found"
        case .interfaceDisconnected:
            return "Audio interface disconnected"
        case .fileReadError:
            return "Failed to read audio file"
        case .fileWriteError:
            return "Failed to write audio file"
        case .invalidFormat:
            return "Invalid audio format"
        case .audioUnitSetupFailed:
            return "Failed to setup audio unit"
        case .noAudioCaptured:
            return "No audio was captured"
        case .processingFailed(let message):
            return "Processing failed: \(message)"
        case .audioSystemError(let error):
            return error.errorDescription
        }
    }
}

// MARK: - Service

@MainActor
class AudioProcessingService: ObservableObject {
    
    private let audioSystem = CAAudioHardwareSystem()
    private var isProcessing = false
    
    // MARK: - Public Methods

    /// Process multiple audio files through hardware in batch mode (more efficient)
    /// Initializes audio system once for all files
    func processFiles(
        _ files: [AudioFile],
        outputPair: StereoPair,
        inputPair: StereoPair,
        settings: ProcessingSettings,
        progressHandler: ((AudioFile, Double) -> Void)? = nil
    ) async throws -> [URL] {
        guard !files.isEmpty else { return [] }
        guard let deviceUID = outputPair.deviceUID else {
            throw AudioProcessingError.noDeviceSelected
        }

        isProcessing = true
        defer { isProcessing = false }

        // Initialize audio system ONCE for all files
        try await audioSystem.initialize(
            deviceUID: deviceUID,
            inputChannels: inputPair.channels,
            outputChannels: outputPair.channels,
            bufferSize: UInt32(settings.bufferSize.rawValue)
        )
        defer {
            audioSystem.cleanup()
        }

        var processedURLs: [URL] = []

        for (index, file) in files.enumerated() {
            let outputURL = try await processFileWithInitializedSystem(
                file,
                inputPair: inputPair,
                settings: settings
            ) { progress in
                progressHandler?(file, progress)
            }
            processedURLs.append(outputURL)

            // Add delay between files (except after the last one)
            // Allows time-domain processing (compressors/limiters) to reset
            if index < files.count - 1 {
                let delayNanoseconds = UInt64(settings.silenceBetweenFilesMs) * 1_000_000
                try await Task.sleep(nanoseconds: delayNanoseconds)
            }
        }

        return processedURLs
    }

    /// Process a single audio file through hardware (initializes system for this file)
    /// - Parameters:
    ///   - file: Audio file to process
    ///   - outputPair: Output stereo pair
    ///   - inputPair: Input stereo pair
    ///   - settings: Processing settings
    ///   - progressHandler: Optional progress callback (0.0 to 1.0)
    /// - Returns: URL of the processed audio file
    func processFile(
        _ file: AudioFile,
        outputPair: StereoPair,
        inputPair: StereoPair,
        settings: ProcessingSettings,
        progressHandler: ((Double) -> Void)? = nil
    ) async throws -> URL {
        
        isProcessing = true
        defer { isProcessing = false }
        
        guard let deviceUID = outputPair.deviceUID else {
            throw AudioProcessingError.noDeviceSelected
        }
        
        // Initialize audio system with device
        try await audioSystem.initialize(deviceUID: deviceUID,
                                         inputChannels: inputPair.channels,
                                         outputChannels: outputPair.channels,
                                         bufferSize: UInt32(settings.bufferSize.rawValue))
        
        // Load audio file
        let audioFile = try AVAudioFile(forReading: file.url)
        guard let buffer = AVAudioPCMBuffer(pcmFormat: audioFile.processingFormat, frameCapacity: AVAudioFrameCount(audioFile.length)) else {
            throw AudioProcessingError.fileReadError
        }
        try audioFile.read(into: buffer)
        
        // Convert to interleaved float format
        guard let floatChannelData = buffer.floatChannelData else {
            throw AudioProcessingError.invalidFormat
        }
        
        let frameCount = Int(buffer.frameLength)
        let channelCount = Int(buffer.format.channelCount)
        var interleavedData = [Float](repeating: 0, count: frameCount * channelCount)

        for frame in 0..<frameCount {
            for channel in 0..<channelCount {
                interleavedData[frame * channelCount + channel] = floatChannelData[channel][frame]
            }
        }

        // Get measured latency (required for processing)
        guard let measuredLatencySamples = settings.measuredLatencySamples else {
            throw AudioProcessingError.processingFailed("Latency not measured - please measure latency first")
        }

        // Calculate target recording length: source + latency + safety buffer
        let inputChannelCount = inputPair.channels.count
        // Convert measured latency from interleaved samples to frames
        let latencyFrames = measuredLatencySamples / inputChannelCount
        let targetRecordingFrames = settings.recordingLength(sourceFileSamples: frameCount, latencySamples: latencyFrames)
        let targetRecordingSamples = targetRecordingFrames * inputChannelCount

        // Set up audio callbacks for processing
        var capturedAudio: [Float] = []
        var currentFrame = 0
        let totalFrames = frameCount
        
        audioSystem.setInputCallback { buffer, frameCount, channelCount in
            // Capture audio data
            let samples = Array(UnsafeBufferPointer(start: UnsafePointer(buffer), count: Int(frameCount * channelCount)))
            capturedAudio.append(contentsOf: samples)
        }
        
        audioSystem.setOutputCallback { buffer, frameCount, channelCount in
            // Play audio data
            let samplesPerChannel = Int(frameCount)
            let channels = Int(channelCount)
            
            for frame in 0..<samplesPerChannel {
                for channel in 0..<channels {
                    let sampleIndex = frame * channels + channel
                    let sourceIndex = currentFrame * channels + channel
                    
                    if currentFrame < totalFrames && sourceIndex < interleavedData.count {
                        buffer[sampleIndex] = interleavedData[sourceIndex]
                    } else {
                        buffer[sampleIndex] = 0.0
                    }
                }
                currentFrame += 1
            }
            
            // Update progress
            if let progressHandler = progressHandler {
                let progress = min(1.0, Double(currentFrame) / Double(max(totalFrames, 1)))
                DispatchQueue.main.async {
                    progressHandler(progress)
                }
            }
        }
        
        // Start audio system
        try audioSystem.start()
        defer { audioSystem.stop() }

        // Wait for recording to complete based on mode
        if settings.useReverbMode {
            // Reverb mode: Stop when tail falls below noise floor
            // First wait for minimum length (source + latency)
            let minimumSamples = (frameCount + latencyFrames) * inputChannelCount
            while capturedAudio.count < minimumSamples {
                try await Task.sleep(nanoseconds: 10_000_000) // 10ms
            }

            // Then monitor for silence (reverb tail below noise floor)
            let checkWindowSamples = Int(ProcessingSettings.sampleRate * 0.1) * inputChannelCount // 100ms window
            var consecutiveSilentChecks = 0
            let requiredSilentChecks = 3 // Need 3 consecutive silent windows (300ms)

            while consecutiveSilentChecks < requiredSilentChecks {
                try await Task.sleep(nanoseconds: 50_000_000) // 50ms between checks

                // Check the most recent window for silence
                if capturedAudio.count >= checkWindowSamples {
                    let recentAudio = Array(capturedAudio.suffix(checkWindowSamples))
                    if isReverbTailBelowNoiseFloor(recentAudio, settings: settings) {
                        consecutiveSilentChecks += 1
                    } else {
                        consecutiveSilentChecks = 0 // Reset if we detect sound again
                    }
                }

                // Safety check: maximum 60 seconds of recording
                if capturedAudio.count > Int(ProcessingSettings.sampleRate * 60) * inputChannelCount {
                    throw AudioProcessingError.processingFailed("Reverb mode exceeded 60 second maximum")
                }
            }

        } else {
            // Fixed length mode: Stop at target length
            while capturedAudio.count < targetRecordingSamples {
                try await Task.sleep(nanoseconds: 10_000_000) // 10ms

                // Safety check: if we've captured way more than expected, something is wrong
                if capturedAudio.count > targetRecordingSamples * 2 {
                    throw AudioProcessingError.processingFailed("Recording exceeded expected length")
                }
            }
        }

        // Stop audio system
        audioSystem.stop()
        try await Task.sleep(nanoseconds: 50_000_000) // 50ms to ensure final buffers are captured

        // Validate captured audio
        guard !capturedAudio.isEmpty else {
            throw AudioProcessingError.noAudioCaptured
        }

        // Trim latency from the beginning of captured audio
        // NOTE: measuredLatencySamples is already in interleaved samples (not frames)
        // so we don't multiply by channel count again
        let latencySamples = measuredLatencySamples
        let trimmedAudio = trimLatency(from: capturedAudio,
                                       latencySamples: latencySamples,
                                       sourceFrames: frameCount,
                                       channelCount: inputChannelCount)

        // Create output file URL
        let outputURL = try buildOutputURL(for: file, settings: settings)

        // Create output format
        let outputFormat = AVAudioFormat(
            standardFormatWithSampleRate: ProcessingSettings.sampleRate,
            channels: AVAudioChannelCount(audioSystem.getInputChannelCount())
        )!

        // Ensure output directory exists
        let outputDirectory = outputURL.deletingLastPathComponent()
        try FileManager.default.createDirectory(at: outputDirectory, withIntermediateDirectories: true, attributes: nil)

        // Remove existing file if present
        if FileManager.default.fileExists(atPath: outputURL.path) {
            try FileManager.default.removeItem(at: outputURL)
        }

        // Write processed audio to file with proper settings
        let audioFileSettings: [String: Any] = [
            AVFormatIDKey: kAudioFormatLinearPCM,
            AVSampleRateKey: ProcessingSettings.sampleRate,
            AVNumberOfChannelsKey: audioSystem.getInputChannelCount(),
            AVLinearPCMBitDepthKey: 24,
            AVLinearPCMIsFloatKey: false,
            AVLinearPCMIsNonInterleaved: false
        ]
        let outputFile = try AVAudioFile(forWriting: outputURL, settings: audioFileSettings)
        
        // Convert trimmed data to non-interleaved for writing
        let trimmedFrameCount = trimmedAudio.count / inputChannelCount
        let outputBuffer = AVAudioPCMBuffer(pcmFormat: outputFormat, frameCapacity: AVAudioFrameCount(trimmedFrameCount))!
        outputBuffer.frameLength = AVAudioFrameCount(trimmedFrameCount)

        guard let outputChannelData = outputBuffer.floatChannelData else {
            throw AudioProcessingError.fileWriteError
        }

        // Convert interleaved trimmed audio to non-interleaved buffer format
        for frame in 0..<trimmedFrameCount {
            for channel in 0..<inputChannelCount {
                let sourceIndex = frame * inputChannelCount + channel
                if sourceIndex < trimmedAudio.count {
                    outputChannelData[channel][frame] = trimmedAudio[sourceIndex]
                }
            }
        }
        
        try outputFile.write(from: outputBuffer)

        return outputURL
    }

    /// Process a single file using an already-initialized audio system
    /// Used by processFiles() for batch processing without re-initialization
    private func processFileWithInitializedSystem(
        _ file: AudioFile,
        inputPair: StereoPair,
        settings: ProcessingSettings,
        progressHandler: ((Double) -> Void)? = nil
    ) async throws -> URL {
        // Load audio file
        let audioFile = try AVAudioFile(forReading: file.url)
        guard let buffer = AVAudioPCMBuffer(pcmFormat: audioFile.processingFormat, frameCapacity: AVAudioFrameCount(audioFile.length)) else {
            throw AudioProcessingError.fileReadError
        }
        try audioFile.read(into: buffer)

        // Convert to interleaved float format
        guard let floatChannelData = buffer.floatChannelData else {
            throw AudioProcessingError.invalidFormat
        }

        let frameCount = Int(buffer.frameLength)
        let channelCount = Int(buffer.format.channelCount)
        var interleavedData = [Float](repeating: 0, count: frameCount * channelCount)

        for frame in 0..<frameCount {
            for channel in 0..<channelCount {
                interleavedData[frame * channelCount + channel] = floatChannelData[channel][frame]
            }
        }

        // Get measured latency (required for processing)
        guard let measuredLatencySamples = settings.measuredLatencySamples else {
            throw AudioProcessingError.processingFailed("Latency not measured - please measure latency first")
        }

        // Calculate target recording length: source + latency + safety buffer
        let inputChannelCount = inputPair.channels.count
        // Convert measured latency from interleaved samples to frames
        let latencyFrames = measuredLatencySamples / inputChannelCount
        let targetRecordingFrames = settings.recordingLength(sourceFileSamples: frameCount, latencySamples: latencyFrames)
        let targetRecordingSamples = targetRecordingFrames * inputChannelCount

        // Set up audio callbacks for processing
        var capturedAudio: [Float] = []
        var currentFrame = 0
        let totalFrames = frameCount

        audioSystem.setInputCallback { buffer, frameCount, channelCount in
            // Capture audio data
            let samples = Array(UnsafeBufferPointer(start: UnsafePointer(buffer), count: Int(frameCount * channelCount)))
            capturedAudio.append(contentsOf: samples)
        }

        audioSystem.setOutputCallback { buffer, frameCount, channelCount in
            // Play audio data
            let samplesPerChannel = Int(frameCount)
            let channels = Int(channelCount)

            for frame in 0..<samplesPerChannel {
                for channel in 0..<channels {
                    let sampleIndex = frame * channels + channel
                    let sourceIndex = currentFrame * channels + channel

                    if currentFrame < totalFrames && sourceIndex < interleavedData.count {
                        buffer[sampleIndex] = interleavedData[sourceIndex]
                    } else {
                        buffer[sampleIndex] = 0.0
                    }
                }
                currentFrame += 1
            }

            // Update progress
            if let progressHandler = progressHandler {
                let progress = min(1.0, Double(currentFrame) / Double(max(totalFrames, 1)))
                DispatchQueue.main.async {
                    progressHandler(progress)
                }
            }
        }

        // Start audio system
        try audioSystem.start()
        defer { audioSystem.stop() }

        // Wait for recording to complete based on mode
        if settings.useReverbMode {
            // Reverb mode: Stop when tail falls below noise floor
            // First wait for minimum length (source + latency)
            let minimumSamples = (frameCount + latencyFrames) * inputChannelCount
            while capturedAudio.count < minimumSamples {
                try await Task.sleep(nanoseconds: 10_000_000) // 10ms
            }

            // Then monitor for silence (reverb tail below noise floor)
            let checkWindowSamples = Int(ProcessingSettings.sampleRate * 0.1) * inputChannelCount // 100ms window
            var consecutiveSilentChecks = 0
            let requiredSilentChecks = 3 // Need 3 consecutive silent windows (300ms)

            while consecutiveSilentChecks < requiredSilentChecks {
                try await Task.sleep(nanoseconds: 50_000_000) // 50ms between checks

                // Check the most recent window for silence
                if capturedAudio.count >= checkWindowSamples {
                    let recentAudio = Array(capturedAudio.suffix(checkWindowSamples))
                    if isReverbTailBelowNoiseFloor(recentAudio, settings: settings) {
                        consecutiveSilentChecks += 1
                    } else {
                        consecutiveSilentChecks = 0 // Reset if we detect sound again
                    }
                }

                // Safety check: maximum 60 seconds of recording
                if capturedAudio.count > Int(ProcessingSettings.sampleRate * 60) * inputChannelCount {
                    throw AudioProcessingError.processingFailed("Reverb mode exceeded 60 second maximum")
                }
            }

        } else {
            // Fixed length mode: Stop at target length
            while capturedAudio.count < targetRecordingSamples {
                try await Task.sleep(nanoseconds: 10_000_000) // 10ms

                // Safety check: if we've captured way more than expected, something is wrong
                if capturedAudio.count > targetRecordingSamples * 2 {
                    throw AudioProcessingError.processingFailed("Recording exceeded expected length")
                }
            }
        }

        // Stop audio system
        audioSystem.stop()
        try await Task.sleep(nanoseconds: 50_000_000) // 50ms to ensure final buffers are captured

        // Validate captured audio
        guard !capturedAudio.isEmpty else {
            throw AudioProcessingError.noAudioCaptured
        }

        // Trim latency from the beginning of captured audio
        // NOTE: measuredLatencySamples is already in interleaved samples (not frames)
        // so we don't multiply by channel count again
        let latencySamples = measuredLatencySamples
        let trimmedAudio = trimLatency(from: capturedAudio,
                                       latencySamples: latencySamples,
                                       sourceFrames: frameCount,
                                       channelCount: inputChannelCount)

        // Create output file URL
        let outputURL = try buildOutputURL(for: file, settings: settings)

        // Create output format
        let outputFormat = AVAudioFormat(
            standardFormatWithSampleRate: ProcessingSettings.sampleRate,
            channels: AVAudioChannelCount(audioSystem.getInputChannelCount())
        )!

        // Ensure output directory exists
        let outputDirectory = outputURL.deletingLastPathComponent()
        try FileManager.default.createDirectory(at: outputDirectory, withIntermediateDirectories: true, attributes: nil)

        // Remove existing file if present
        if FileManager.default.fileExists(atPath: outputURL.path) {
            try FileManager.default.removeItem(at: outputURL)
        }

        // Write processed audio to file with proper settings
        let audioFileSettings: [String: Any] = [
            AVFormatIDKey: kAudioFormatLinearPCM,
            AVSampleRateKey: ProcessingSettings.sampleRate,
            AVNumberOfChannelsKey: audioSystem.getInputChannelCount(),
            AVLinearPCMBitDepthKey: 24,
            AVLinearPCMIsFloatKey: false,
            AVLinearPCMIsNonInterleaved: false
        ]
        let outputFile = try AVAudioFile(forWriting: outputURL, settings: audioFileSettings)

        // Convert trimmed data to non-interleaved for writing
        let trimmedFrameCount = trimmedAudio.count / inputChannelCount
        let outputBuffer = AVAudioPCMBuffer(pcmFormat: outputFormat, frameCapacity: AVAudioFrameCount(trimmedFrameCount))!
        outputBuffer.frameLength = AVAudioFrameCount(trimmedFrameCount)

        guard let outputChannelData = outputBuffer.floatChannelData else {
            throw AudioProcessingError.fileWriteError
        }

        // Convert interleaved trimmed audio to non-interleaved buffer format
        for frame in 0..<trimmedFrameCount {
            for channel in 0..<inputChannelCount {
                let sourceIndex = frame * inputChannelCount + channel
                if sourceIndex < trimmedAudio.count {
                    outputChannelData[channel][frame] = trimmedAudio[sourceIndex]
                }
            }
        }

        try outputFile.write(from: outputBuffer)

        return outputURL
    }

    /// Preview multiple files through hardware
    func previewFiles(
        _ files: [AudioFile],
        outputPair: StereoPair,
        inputPair: StereoPair,
        settings: ProcessingSettings,
        onFileChange: ((Int) -> Void)? = nil,
        onProgress: ((Int, Double) -> Void)? = nil
    ) async throws {
        guard let deviceUID = outputPair.deviceUID else {
            throw AudioProcessingError.noDeviceSelected
        }
        let outputChannelCount = max(outputPair.channels.count, 1)
        let sampleRate = ProcessingSettings.sampleRate
        let silenceFrames = Int(Double(settings.silenceBetweenFilesMs) / 1000.0 * sampleRate)
        
        let previewItems: [PreviewPlaybackContext.Item] = try files.map { file in
            let audioFile = try AVAudioFile(forReading: file.url)
            let targetChannels = outputChannelCount
            let frames = Int(audioFile.length)
            guard frames > 0 else {
                throw AudioProcessingError.processingFailed("Empty audio file: \(file.fileName)")
            }
            
            guard let sourceBuffer = AVAudioPCMBuffer(pcmFormat: audioFile.processingFormat,
                                                      frameCapacity: AVAudioFrameCount(frames)) else {
                throw AudioProcessingError.fileReadError
            }
            try audioFile.read(into: sourceBuffer)
            
            guard let channelData = sourceBuffer.floatChannelData else {
                throw AudioProcessingError.invalidFormat
            }
            
            let sourceChannels = Int(sourceBuffer.format.channelCount)
            let frameLength = Int(sourceBuffer.frameLength)
            var samples = [Float](repeating: 0, count: frameLength * targetChannels)
            
            samples.withUnsafeMutableBufferPointer { dest in
                for frame in 0..<frameLength {
                    for channel in 0..<targetChannels {
                        let sourceChannel = min(channel, max(sourceChannels - 1, 0))
                        dest[frame * targetChannels + channel] = channelData[sourceChannel][frame]
                    }
                }
            }
            
            return PreviewPlaybackContext.Item(samples: samples,
                                               frameCount: frameLength,
                                               fileName: file.fileName)
        }
        
        guard !previewItems.isEmpty else { return }
        
        let context = PreviewPlaybackContext(items: previewItems,
                                             channelCount: outputChannelCount,
                                             silenceFrames: silenceFrames,
                                             onFileChange: onFileChange,
                                             onProgress: onProgress)
        
        try await audioSystem.initialize(deviceUID: deviceUID,
                                         inputChannels: inputPair.channels,
                                         outputChannels: outputPair.channels,
                                         bufferSize: UInt32(settings.bufferSize.rawValue))
        
        audioSystem.setInputCallback { _, _, _ in /* no capture required */ }
        audioSystem.setOutputCallback { buffer, frameCount, channelCount in
            // Clear buffer first
            let totalSamples = Int(frameCount * channelCount)
            buffer.initialize(repeating: 0.0, count: totalSamples)

            // Render to selected output channels (for hardware send)
            let outputChannelIndices = outputPair.channels.map { $0 - 1 } // Convert 1-based to 0-based
            context.renderToChannels(into: buffer,
                                    frameCount: Int(frameCount),
                                    channelCount: Int(channelCount),
                                    targetChannels: outputChannelIndices)

            // Also render to monitoring channels (if enabled)
            if settings.enableMonitoring {
                let monitoringIndices = settings.monitoringChannels.map { $0 - 1 } // Convert 1-based to 0-based
                // Check if monitoring channels are within bounds
                if monitoringIndices.allSatisfy({ $0 < channelCount }) {
                    context.renderToChannels(into: buffer,
                                            frameCount: Int(frameCount),
                                            channelCount: Int(channelCount),
                                            targetChannels: monitoringIndices)
                }
            }
        }
        
        context.notifyStart()
        try audioSystem.start()
        defer {
            audioSystem.stop()
            context.cancel()
        }
        
        do {
            while true {
                try await Task.sleep(nanoseconds: 50_000_000)
                if Task.isCancelled {
                    context.cancel()
                    break
                }
            }
        } catch is CancellationError {
            context.cancel()
        }
    }

    // MARK: - Helper Methods

    /// Builds the output URL for a processed file based on settings
    private func buildOutputURL(for file: AudioFile, settings: ProcessingSettings) throws -> URL {
        let sourceFileName = file.url.deletingPathExtension().lastPathComponent
        let fileExtension = "wav"

        // Determine output filename
        let outputFileName: String
        if settings.outputPostfix.isEmpty {
            outputFileName = "\(sourceFileName).\(fileExtension)"
        } else {
            outputFileName = "\(sourceFileName)\(settings.outputPostfix).\(fileExtension)"
        }

        // Require output directory to be set (never write to source directory)
        guard let outputFolderPath = settings.outputFolderPath, !outputFolderPath.isEmpty else {
            throw AudioProcessingError.processingFailed("Output folder not selected - please choose a destination folder in Settings")
        }

        let outputDirectory = URL(fileURLWithPath: outputFolderPath)

        return outputDirectory.appendingPathComponent(outputFileName)
    }

    /// Trim latency samples from the beginning of captured audio
    /// - Parameters:
    ///   - capturedAudio: Raw captured audio (interleaved)
    ///   - latencySamples: Number of samples to trim (includes all channels)
    ///   - sourceFrames: Original source file frame count
    ///   - channelCount: Number of channels in captured audio
    /// - Returns: Trimmed audio matching source length
    private func trimLatency(from capturedAudio: [Float],
                             latencySamples: Int,
                             sourceFrames: Int,
                             channelCount: Int) -> [Float] {
        // Calculate where to start and how much to extract
        let startSample = latencySamples
        let desiredOutputSamples = sourceFrames * channelCount

        // Safety bounds checking
        guard startSample < capturedAudio.count else {
            // If we somehow don't have enough captured audio, return what we have
            print("Warning: Not enough captured audio to trim latency. Expected at least \(startSample) samples, got \(capturedAudio.count)")
            return Array(capturedAudio.prefix(desiredOutputSamples))
        }

        let endSample = min(startSample + desiredOutputSamples, capturedAudio.count)
        let trimmedAudio = Array(capturedAudio[startSample..<endSample])

        // Log trimming info for debugging
        print("Trim: Captured \(capturedAudio.count) samples, trimmed \(latencySamples) samples, output \(trimmedAudio.count) samples (expected \(desiredOutputSamples))")

        return trimmedAudio
    }

    /// Checks if the reverb tail in the audio window has fallen below the measured noise floor
    /// - Parameters:
    ///   - audioWindow: Array of interleaved audio samples to check
    ///   - settings: Processing settings containing noise floor measurement
    /// - Returns: true if the audio is below the noise floor threshold, false otherwise
    private func isReverbTailBelowNoiseFloor(_ audioWindow: [Float], settings: ProcessingSettings) -> Bool {
        // If no noise floor measured, fall back to simple threshold
        guard let noiseFloorDb = settings.measuredNoiseFloorDb else {
            // Use a very low threshold as fallback (-80 dB)
            let fallbackThreshold: Float = 0.0001 // approximately -80 dB
            let maxAbsSample = audioWindow.map { abs($0) }.max() ?? 0
            return maxAbsSample < fallbackThreshold
        }

        // Calculate threshold: noise floor + margin
        let thresholdDb = noiseFloorDb + (noiseFloorDb * settings.noiseFloorMarginPercent / 100.0)

        // Find maximum absolute sample in window
        let maxAbsSample = audioWindow.map { abs($0) }.max() ?? 0

        // Convert to dB for comparison
        let maxDb = maxAbsSample > 0 ? 20.0 * log10(maxAbsSample) : -160.0

        // Check if below threshold
        let isBelowThreshold = maxDb < thresholdDb

        // Debug logging (can be removed in production)
        if isBelowThreshold {
            print("Reverb tail detected: \(String(format: "%.1f", maxDb)) dB < threshold \(String(format: "%.1f", thresholdDb)) dB")
        }

        return isBelowThreshold
    }
}

// MARK: - Preview Playback Context

private final class PreviewPlaybackContext {
    struct Item {
        let samples: [Float]
        let frameCount: Int
        let fileName: String
    }
    
    private let items: [Item]
    private let channelCount: Int
    private let silenceFrames: Int
    
    private var currentItemIndex: Int = 0
    private var frameOffset: Int = 0
    private var silenceRemaining: Int = 0
    private var isCancelled: Bool = false
    
    private let onFileChange: ((Int) -> Void)?
    private let onProgress: ((Int, Double) -> Void)?
    
    init(items: [Item],
         channelCount: Int,
         silenceFrames: Int,
         onFileChange: ((Int) -> Void)?,
         onProgress: ((Int, Double) -> Void)?) {
        self.items = items
        self.channelCount = channelCount
        self.silenceFrames = silenceFrames
        self.onFileChange = onFileChange
        self.onProgress = onProgress
    }
    
    func notifyStart() {
        onFileChange?(currentItemIndex)
        onProgress?(currentItemIndex, 0)
    }
    
    func cancel() {
        isCancelled = true
    }
    
    /// Renders audio into the buffer for ALL channels (original behavior)
    func render(into buffer: UnsafeMutablePointer<Float>,
                frameCount: Int,
                channelCount: Int) {
        renderToChannels(into: buffer,
                        frameCount: frameCount,
                        channelCount: channelCount,
                        targetChannels: Array(0..<channelCount))
    }

    /// Renders audio into specific target channels only
    /// - Parameters:
    ///   - buffer: Output buffer (interleaved)
    ///   - frameCount: Number of frames to render
    ///   - channelCount: Total number of channels in buffer
    ///   - targetChannels: Array of channel indices to write to (0-based)
    func renderToChannels(into buffer: UnsafeMutablePointer<Float>,
                         frameCount: Int,
                         channelCount: Int,
                         targetChannels: [Int]) {
        let totalSamples = frameCount * channelCount
        let destination = UnsafeMutableBufferPointer(start: buffer, count: totalSamples)
        guard let dstBase = destination.baseAddress else { return }

        // Validate target channels
        guard !targetChannels.isEmpty,
              targetChannels.allSatisfy({ $0 >= 0 && $0 < channelCount }) else {
            return
        }

        var framesRemaining = frameCount
        var currentFrameIndex = 0

        while framesRemaining > 0 {
            if isCancelled {
                return
            }

            if silenceRemaining > 0 {
                let copyFrames = min(silenceRemaining, framesRemaining)
                // Write silence to target channels only
                for frame in 0..<copyFrames {
                    let frameBaseIndex = (currentFrameIndex + frame) * channelCount
                    for targetChannel in targetChannels {
                        dstBase[frameBaseIndex + targetChannel] = 0.0
                    }
                }
                silenceRemaining -= copyFrames
                framesRemaining -= copyFrames
                currentFrameIndex += copyFrames
                continue
            }

            let item = items[currentItemIndex]
            let framesAvailable = item.frameCount - frameOffset
            let copyFrames = min(framesAvailable, framesRemaining)

            item.samples.withUnsafeBufferPointer { source in
                guard let srcBase = source.baseAddress else { return }

                for frame in 0..<copyFrames {
                    let srcFrameIndex = (frameOffset + frame) * self.channelCount
                    let dstFrameIndex = (currentFrameIndex + frame) * channelCount

                    // Map source channels to target channels
                    for (targetIndex, targetChannel) in targetChannels.enumerated() {
                        // Use modulo to handle mono->stereo or mismatched channel counts
                        let sourceChannel = targetIndex % self.channelCount
                        let srcSampleIndex = srcFrameIndex + sourceChannel
                        let dstSampleIndex = dstFrameIndex + targetChannel

                        if srcSampleIndex < source.count {
                            dstBase[dstSampleIndex] = srcBase[srcSampleIndex]
                        }
                    }
                }
            }

            frameOffset += copyFrames
            framesRemaining -= copyFrames
            currentFrameIndex += copyFrames

            let progress = Double(frameOffset) / Double(max(item.frameCount, 1))
            onProgress?(currentItemIndex, progress)

            if frameOffset >= item.frameCount {
                frameOffset = 0
                silenceRemaining = silenceFrames
                currentItemIndex = (currentItemIndex + 1) % items.count
                onFileChange?(currentItemIndex)
                onProgress?(currentItemIndex, 0)
            }
        }
    }
}
