//
//  AudioFile.swift
//  F9-Batch-Resampler
//
//  Created by James Wiltshire on 09/09/2025.
//

import Foundation
import AVFoundation

struct AudioFile: Identifiable, Hashable {
    let id = UUID()
    let url: URL
    var status: ProcessingStatus = .pending
    var isSelected: Bool = false
    var sampleRate: Double?
    var durationSamples: Int?

    enum ProcessingStatus: String {
        case pending = "Pending"
        case processing = "Processing"
        case completed = "Completed"
        case failed = "Failed"
        case invalidSampleRate = "Invalid Sample Rate"
    }
    
    var fileName: String {
        url.lastPathComponent
    }
    
    var isValid: Bool {
        guard let rate = sampleRate else { return false }
        return abs(rate - ProcessingSettings.sampleRate) < 1.0 // Allow small tolerance
    }
    
    /// Loads audio file metadata (sample rate, duration)
    mutating func loadMetadata() {
        guard let audioFile = try? AVAudioFile(forReading: url) else {
            status = .failed
            return
        }
        
        sampleRate = audioFile.fileFormat.sampleRate
        durationSamples = Int(audioFile.length)
        
        if !isValid {
            status = .invalidSampleRate
        }
    }
}
