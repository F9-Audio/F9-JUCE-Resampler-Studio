//
//  CAAudioHardwareSystem.swift
//  F9-Batch-Resampler
//
//  Created by AI Assistant on 2024-01-XX.
//  Copyright © 2024 F9 Audio. All rights reserved.
//

import Foundation
import CoreAudio
import AVFoundation

/// Swift-based audio system for robust device management
@MainActor
class CAAudioHardwareSystem: ObservableObject {
    
    // MARK: - Properties
    
    @Published var isInitialized = false
    @Published var isRunning = false
    @Published var currentDevice: AudioDevice?
    @Published var availableDevices: [AudioDevice] = []
    
    // Audio configuration
    private let sampleRate: Double = ProcessingSettings.sampleRate
    private var bufferSize: UInt32 = 256

    // Bridge
    private let bridge: CAAudioBridgeProtocol
    private var preparedConfiguration: CAAudioStreamConfiguration?
    private var activeStreamToken: UUID?
    
    // Callbacks
    private var inputCallback: ((UnsafeMutablePointer<Float>, UInt32, UInt32) -> Void)?
    private var outputCallback: ((UnsafeMutablePointer<Float>, UInt32, UInt32) -> Void)?
    
    // MARK: - Initialization
    
    init(bridge: CAAudioBridgeProtocol = makeDefaultCAAudioBridge()) {
        self.bridge = bridge
        print("CAAudioHardwareSystem: Initialized with bridge type: \(type(of: bridge))")
        Task { await loadAvailableDevices() }
    }
    
    deinit {
        Task { @MainActor [weak self] in
            self?.stop()
            self?.cleanup()
        }
    }
    
    // MARK: - Public Methods
    
    func initialize(deviceUID: String,
                    inputChannels: [Int],
                    outputChannels: [Int],
                    bufferSize: UInt32) async throws {
        self.bufferSize = bufferSize
        let configuration = try bridge.prepareStream(for: deviceUID,
                                                     sampleRate: sampleRate,
                                                     bufferSize: bufferSize,
                                                     inputChannels: inputChannels,
                                                     outputChannels: outputChannels)
        preparedConfiguration = configuration
        
        let descriptor = configuration.device
        currentDevice = AudioDevice(name: descriptor.name,
                                    inputChannelCount: Int(descriptor.inputChannels),
                                    outputChannelCount: Int(descriptor.outputChannels),
                                    uniqueID: descriptor.id)
        isInitialized = true
    }
    
    func start() throws {
        guard preparedConfiguration != nil else {
            throw AudioSystemError.notInitialized
        }
        guard inputCallback != nil || outputCallback != nil else {
            throw AudioSystemError.startFailed
        }
        
        let callbacks = CAAudioStreamingCallbacks(input: inputCallback,
                                                  output: outputCallback)
        let token = try bridge.startStream(configuration: preparedConfiguration!,
                                           callbacks: callbacks)
        activeStreamToken = token
        isRunning = true
    }
    
    func stop() {
        if let token = activeStreamToken {
            try? bridge.stopStream(token: token)
            activeStreamToken = nil
        }
        isRunning = false
    }
    
    func cleanup() {
        stop()
        preparedConfiguration = nil
        currentDevice = nil
        isInitialized = false
    }
    
    // MARK: - Callback Management
    
    func setInputCallback(_ callback: @escaping (UnsafeMutablePointer<Float>, UInt32, UInt32) -> Void) {
        inputCallback = callback
    }
    
    func setOutputCallback(_ callback: @escaping (UnsafeMutablePointer<Float>, UInt32, UInt32) -> Void) {
        outputCallback = callback
    }
    
    // MARK: - Device Information
    
    func getInputChannelCount() -> UInt32 {
        return UInt32(preparedConfiguration?.inputChannels.count ?? 2)
    }
    
    func getOutputChannelCount() -> UInt32 {
        return UInt32(preparedConfiguration?.outputChannels.count ?? 2)
    }
    
    func getCurrentSampleTime() -> UInt64 {
        guard let token = activeStreamToken else { return 0 }
        return bridge.currentSampleTime(token: token) ?? 0
    }
    
    // MARK: - Private Methods
    
    private func loadAvailableDevices() async {
        print("CAAudioHardwareSystem: Loading available devices...")
        let descriptors: [CAAudioHardwareDeviceDescriptor]
        do {
            descriptors = try bridge.enumerateDevices()
            print("CAAudioHardwareSystem: Found \(descriptors.count) devices")
        } catch {
            descriptors = []
            print("CAAudioHardwareSystem failed to enumerate devices: \(error.localizedDescription)")
        }
        
        let models = descriptors.map { descriptor in
            AudioDevice(
                name: descriptor.name,
                inputChannelCount: Int(descriptor.inputChannels),
                outputChannelCount: Int(descriptor.outputChannels),
                uniqueID: descriptor.id
            )
        }
        
        await MainActor.run {
            self.availableDevices = models
        }
    }
}

// MARK: - Error Types

enum AudioSystemError: LocalizedError {
    case notInitialized
    case deviceNotFound
    case initializationFailed
    case startFailed
    case stopFailed
    
    var errorDescription: String? {
        switch self {
        case .notInitialized:
            return "Audio system not initialized"
        case .deviceNotFound:
            return "Audio device not found"
        case .initializationFailed:
            return "Failed to initialize audio system"
        case .startFailed:
            return "Failed to start audio system"
        case .stopFailed:
            return "Failed to stop audio system"
        }
    }
}
