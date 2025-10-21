import Foundation
import AVFoundation

// Simple file processing service for loading/saving audio files
final class FileProcessingService {
    
    // Load audio file and return PCM buffer
    static func loadAudioFile(url: URL) throws -> AVAudioPCMBuffer {
        let audioFile = try AVAudioFile(forReading: url)
        
        guard let buffer = AVAudioPCMBuffer(
            pcmFormat: audioFile.processingFormat,
            frameCapacity: AVAudioFrameCount(audioFile.length)
        ) else {
            throw NSError(domain: "FileProcessingService", code: -1,
                         userInfo: [NSLocalizedDescriptionKey: "Failed to create audio buffer"])
        }
        
        try audioFile.read(into: buffer)
        return buffer
    }
    
    // Save PCM buffer to file
    static func saveAudioFile(buffer: AVAudioPCMBuffer, to url: URL) throws {
        // Create parent directory if needed
        let parent = url.deletingLastPathComponent()
        try FileManager.default.createDirectory(at: parent, withIntermediateDirectories: true)
        
        // Remove existing file
        if FileManager.default.fileExists(atPath: url.path) {
            try FileManager.default.removeItem(at: url)
        }
        
        // Create audio file
        let settings: [String: Any] = [
            AVFormatIDKey: kAudioFormatLinearPCM,
            AVSampleRateKey: buffer.format.sampleRate,
            AVNumberOfChannelsKey: buffer.format.channelCount,
            AVLinearPCMBitDepthKey: 32,
            AVLinearPCMIsFloatKey: true,
            AVLinearPCMIsNonInterleaved: buffer.format.isInterleaved == false
        ]
        
        let audioFile = try AVAudioFile(
            forWriting: url,
            settings: settings,
            commonFormat: .pcmFormatFloat32,
            interleaved: buffer.format.isInterleaved
        )
        
        try audioFile.write(from: buffer)
    }
}

