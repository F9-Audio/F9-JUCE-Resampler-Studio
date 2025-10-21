//
//  AudioDevice.swift
//  F9-Batch-Resampler
//
//  Created by James Wiltshire on 09/09/2025.
//

import Foundation

struct AudioDevice: Identifiable, Hashable {
    var id: String { uniqueID }
    let name: String
    let inputChannelCount: Int
    let outputChannelCount: Int
    let uniqueID: String
    
    /// Returns true if this is a built-in Apple device (speakers, microphone, etc.)
    var isBuiltIn: Bool {
        let builtInKeywords = [
            "built-in",
            "internal",
            "macbook",
            "imac",
            "mac mini",
            "mac pro",
            "mac studio"
        ]
        let lowerName = name.lowercased()
        return builtInKeywords.contains { lowerName.contains($0) }
    }
    
    /// Returns available stereo pairs for this device
    func stereoPairs(isInput: Bool) -> [StereoPair] {
        let channelCount = isInput ? inputChannelCount : outputChannelCount
        guard channelCount >= 2 else { return [] }
        
        return stride(from: 1, through: channelCount - 1, by: 2).map { start in
            StereoPair(
                leftChannel: start,
                rightChannel: start + 1,
                device: self
            )
        }
    }
}

struct StereoPair: Identifiable, Hashable {
    let id: String
    let leftChannel: Int
    let rightChannel: Int
    let device: AudioDevice
    
    var displayName: String {
        "\(device.name) - Channels \(leftChannel)-\(rightChannel)"
    }
    
    var deviceUID: String? {
        device.uniqueID
    }
    
    var channels: [Int] {
        [leftChannel, rightChannel]
    }
    
    init(leftChannel: Int, rightChannel: Int, device: AudioDevice) {
        self.leftChannel = leftChannel
        self.rightChannel = rightChannel
        self.device = device
        self.id = "\(device.uniqueID)-\(leftChannel)-\(rightChannel)"
    }
}
