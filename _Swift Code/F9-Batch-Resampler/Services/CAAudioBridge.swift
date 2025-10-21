//
//  CAAudioBridge.swift
//  F9-Batch-Resampler
//
//  Phase 2 scaffolding for the CA Audio Hardware integration.
//  Provides a protocol-oriented facade so the rest of the app can
//  stop talking to the low-level package directly.
//

import Foundation
import AVFoundation
import CoreAudio

#if canImport(CAAudioHardware)
import CAAudioHardware
import CoreAudioExtensions

private typealias HALAudioDevice = CAAudioHardware.AudioDevice
private typealias HALAudioStream = CAAudioHardware.AudioStream
private typealias HALPropertyScope = CAAudioHardware.PropertyScope
#endif

// MARK: - Descriptors

/// CA Audio Hardware device metadata normalised for the app.
struct CAAudioHardwareDeviceDescriptor: Identifiable, Equatable {
    let id: String
    let name: String
    let inputChannels: UInt32
    let outputChannels: UInt32
    let manufacturer: String?

    init(id: String, name: String, inputChannels: UInt32, outputChannels: UInt32, manufacturer: String? = nil) {
        self.id = id
        self.name = name
        self.inputChannels = inputChannels
        self.outputChannels = outputChannels
        self.manufacturer = manufacturer
    }
}

/// Placeholder for future stream configuration details (buffer size, sample rate, etc.).
struct CAAudioStreamConfiguration {
    let device: CAAudioHardwareDeviceDescriptor
    let sampleRate: Double
    let bufferSize: UInt32
    let inputChannels: [Int]
    let outputChannels: [Int]

    init(device: CAAudioHardwareDeviceDescriptor,
         sampleRate: Double,
         bufferSize: UInt32,
         inputChannels: [Int],
         outputChannels: [Int]) {
        self.device = device
        self.sampleRate = sampleRate
        self.bufferSize = bufferSize
        self.inputChannels = inputChannels
        self.outputChannels = outputChannels
    }
}

/// Streaming callbacks supplied by higher-level services.
struct CAAudioStreamingCallbacks {
    let input: ((UnsafeMutablePointer<Float>, UInt32, UInt32) -> Void)?
    let output: ((UnsafeMutablePointer<Float>, UInt32, UInt32) -> Void)?

    init(input: ((UnsafeMutablePointer<Float>, UInt32, UInt32) -> Void)? = nil,
         output: ((UnsafeMutablePointer<Float>, UInt32, UInt32) -> Void)? = nil) {
        self.input = input
        self.output = output
    }
}

// MARK: - Errors

enum CAAudioBridgeError: LocalizedError {
    case deviceNotFound
    case unsupportedChannelLayout
    case startStreamFailed
    case stopStreamFailed
    case notImplemented(String)

    var errorDescription: String? {
        switch self {
        case .deviceNotFound:
            return "CA Audio Hardware device not found."
        case .unsupportedChannelLayout:
            return "Unsupported channel layout returned by the hardware bridge."
        case .startStreamFailed:
            return "Failed to start CA Audio Hardware stream."
        case .stopStreamFailed:
            return "Failed to stop CA Audio Hardware stream."
        case .notImplemented(let detail):
            return "CA Audio Hardware bridge method not yet implemented: \(detail)."
        }
    }
}

// MARK: - Protocol

protocol CAAudioBridgeProtocol {
    /// Enumerate all Core Audio devices known to the bridge.
    func enumerateDevices() throws -> [CAAudioHardwareDeviceDescriptor]

    /// Prepare the device for streaming; future phases will return a handle for playback/record.
    func prepareStream(for deviceUID: String,
                       sampleRate: Double,
                       bufferSize: UInt32,
                       inputChannels: [Int],
                       outputChannels: [Int]) throws -> CAAudioStreamConfiguration

    /// Start streaming using the callbacks provided. Returns an opaque token for later teardown.
    func startStream(configuration: CAAudioStreamConfiguration,
                     callbacks: CAAudioStreamingCallbacks) throws -> UUID

    /// Stop streaming for the supplied token.
    func stopStream(token: UUID) throws

    /// Returns the most recent sample time reported by the hardware stream (if available).
    func currentSampleTime(token: UUID) -> UInt64?
}

// MARK: - Default factory

func makeDefaultCAAudioBridge() -> CAAudioBridgeProtocol {
    #if canImport(CAAudioHardware)
    print("CAAudioBridge: Using CAAudioHardwareBridge (real implementation)")
    return CAAudioHardwareBridge()
    #else
    print("CAAudioBridge: Using CAAudioHardwareStubBridge (stub implementation)")
    return CAAudioHardwareStubBridge()
    #endif
}

// MARK: - Stub Implementation

/// Temporary bridge that proxies device discovery via Core Audio while streaming remains stubbed.
final class CAAudioHardwareStubBridge: CAAudioBridgeProtocol {

    private let audioService = AudioService()
    private var cachedDescriptors: [CAAudioHardwareDeviceDescriptor] = []
    private var activeTokens: Set<UUID> = []
    private var activeTimers: [UUID: Timer] = [:]

    func enumerateDevices() throws -> [CAAudioHardwareDeviceDescriptor] {
        let devices = audioService.listDevices()
        cachedDescriptors = devices.map {
            CAAudioHardwareDeviceDescriptor(
                id: $0.uniqueID,
                name: $0.name,
                inputChannels: UInt32(max(0, $0.inputChannelCount)),
                outputChannels: UInt32(max(0, $0.outputChannelCount)),
                manufacturer: nil
            )
        }
        return cachedDescriptors
    }

    func prepareStream(for deviceUID: String,
                       sampleRate: Double,
                       bufferSize: UInt32,
                       inputChannels: [Int],
                       outputChannels: [Int]) throws -> CAAudioStreamConfiguration {
        if cachedDescriptors.isEmpty {
            _ = try? enumerateDevices()
        }
        guard let descriptor = cachedDescriptors.first(where: { $0.id == deviceUID }) else {
            throw CAAudioBridgeError.deviceNotFound
        }
        return CAAudioStreamConfiguration(
            device: descriptor,
            sampleRate: sampleRate,
            bufferSize: bufferSize,
            inputChannels: inputChannels,
            outputChannels: outputChannels
        )
    }

    func startStream(configuration: CAAudioStreamConfiguration,
                     callbacks: CAAudioStreamingCallbacks) throws -> UUID {
        if callbacks.output == nil && callbacks.input == nil {
            throw CAAudioBridgeError.notImplemented("Callbacks must be supplied before starting the stub stream.")
        }
        let token = UUID()
        activeTokens.insert(token)
        
        print("CAAudioHardwareStubBridge: Starting stream with token \(token)")
        print("CAAudioHardwareStubBridge: Input channels: \(configuration.inputChannels)")
        print("CAAudioHardwareStubBridge: Output channels: \(configuration.outputChannels)")
        print("CAAudioHardwareStubBridge: Buffer size: \(configuration.bufferSize)")
        print("CAAudioHardwareStubBridge: Sample rate: \(configuration.sampleRate)")
        
        // Start a timer to simulate audio callbacks with real audio data
        let timer = Timer.scheduledTimer(withTimeInterval: Double(configuration.bufferSize) / configuration.sampleRate, repeats: true) { _ in
            guard self.activeTokens.contains(token) else { return }
            
            let frameCount = UInt32(configuration.bufferSize)
            let inputChannelCount = UInt32(configuration.inputChannels.count)
            let outputChannelCount = UInt32(configuration.outputChannels.count)
            
            // Simulate input callback with test audio data
            if let inputCallback = callbacks.input, !configuration.inputChannels.isEmpty {
                let buffer = UnsafeMutablePointer<Float>.allocate(capacity: Int(frameCount * inputChannelCount))
                defer { buffer.deallocate() }
                
                // Generate test audio: 1kHz sine wave for hardware loop test
                let sampleRate = configuration.sampleRate
                let frequency = 1000.0 // 1kHz
                let amplitude = 0.5
                
                for frame in 0..<Int(frameCount) {
                    for channel in 0..<Int(inputChannelCount) {
                        let time = Double(frame) / sampleRate
                        let sample = Float(sin(2.0 * .pi * frequency * time) * amplitude)
                        buffer[frame * Int(inputChannelCount) + channel] = sample
                    }
                }
                
                print("CAAudioHardwareStubBridge: Calling input callback with \(frameCount) frames, \(inputChannelCount) channels")
                inputCallback(buffer, frameCount, inputChannelCount)
            }
            
            // Simulate output callback
            if let outputCallback = callbacks.output, !configuration.outputChannels.isEmpty {
                let buffer = UnsafeMutablePointer<Float>.allocate(capacity: Int(frameCount * outputChannelCount))
                defer { buffer.deallocate() }
                
                // Generate test audio: 1kHz sine wave for hardware loop test
                let sampleRate = configuration.sampleRate
                let frequency = 1000.0 // 1kHz
                let amplitude = 0.5
                
                for frame in 0..<Int(frameCount) {
                    for channel in 0..<Int(outputChannelCount) {
                        let time = Double(frame) / sampleRate
                        let sample = Float(sin(2.0 * .pi * frequency * time) * amplitude)
                        buffer[frame * Int(outputChannelCount) + channel] = sample
                    }
                }
                
                print("CAAudioHardwareStubBridge: Calling output callback with \(frameCount) frames, \(outputChannelCount) channels")
                outputCallback(buffer, frameCount, outputChannelCount)
            }
        }
        
        activeTimers[token] = timer
        
        return token
    }

    func stopStream(token: UUID) throws {
        print("CAAudioHardwareStubBridge: Stopping stream with token \(token)")
        activeTokens.remove(token)
        
        if let timer = activeTimers.removeValue(forKey: token) {
            timer.invalidate()
        }
    }

    func currentSampleTime(token: UUID) -> UInt64? {
        activeTokens.contains(token) ? 0 : nil
    }
}

// MARK: - Real implementation placeholder

#if canImport(CAAudioHardware)
/// Placeholder that will be fleshed out in Phase 3 once the CA Audio Hardware
/// APIs are available locally. For now it throws to highlight any accidental use.
final class CAAudioHardwareBridge: CAAudioBridgeProtocol {
    private let lock = NSLock()
    private var activeStreams: [UUID: ActiveStream] = [:]
    private var cachedDescriptors: [CAAudioHardwareDeviceDescriptor] = []

    func enumerateDevices() throws -> [CAAudioHardwareDeviceDescriptor] {
        let devices = try HALAudioDevice.devices
        let descriptors = try devices.map { device -> CAAudioHardwareDeviceDescriptor in
            let id = try device.deviceUID
            let inputChannels = try Self.channelCount(for: device, scope: .input)
            let outputChannels = try Self.channelCount(for: device, scope: .output)
            let manufacturer = try? device.manufacturer
            return CAAudioHardwareDeviceDescriptor(
                id: id,
                name: (try? device.name) ?? "Unknown Device",
                inputChannels: UInt32(inputChannels),
                outputChannels: UInt32(outputChannels),
                manufacturer: manufacturer
            )
        }
        cachedDescriptors = descriptors
        return descriptors
    }

    func prepareStream(for deviceUID: String,
                       sampleRate: Double,
                       bufferSize: UInt32,
                       inputChannels: [Int],
                       outputChannels: [Int]) throws -> CAAudioStreamConfiguration {
        guard let device = try HALAudioDevice.makeDevice(forUID: deviceUID) else {
            throw CAAudioBridgeError.deviceNotFound
        }

        guard !inputChannels.isEmpty || !outputChannels.isEmpty else {
            throw CAAudioBridgeError.unsupportedChannelLayout
        }

        let availableInput = try Self.channelCount(for: device, scope: .input)
        let availableOutput = try Self.channelCount(for: device, scope: .output)
        guard inputChannels.allSatisfy({ $0 >= 1 && $0 <= availableInput }) else {
            throw CAAudioBridgeError.unsupportedChannelLayout
        }
        guard outputChannels.allSatisfy({ $0 >= 1 && $0 <= availableOutput }) else {
            throw CAAudioBridgeError.unsupportedChannelLayout
        }

        let clampedBufferSize = try Self.configureBufferSize(for: device, requested: Int(bufferSize))
        try Self.configureSampleRate(for: device, requested: sampleRate)
        try Self.configureStreamFormats(for: device, sampleRate: sampleRate)

        // Ensure cached descriptors are populated so startStream can reuse them.
        if cachedDescriptors.isEmpty {
            _ = try? enumerateDevices()
        }
        let descriptor = cachedDescriptors.first { $0.id == deviceUID } ??
            CAAudioHardwareDeviceDescriptor(
                id: deviceUID,
                name: (try? device.name) ?? "Unknown Device",
                inputChannels: UInt32(availableInput),
                outputChannels: UInt32(availableOutput),
                manufacturer: try? device.manufacturer
            )

        return CAAudioStreamConfiguration(
            device: descriptor,
            sampleRate: sampleRate,
            bufferSize: UInt32(clampedBufferSize),
            inputChannels: inputChannels,
            outputChannels: outputChannels
        )
    }

    func startStream(configuration: CAAudioStreamConfiguration,
                     callbacks: CAAudioStreamingCallbacks) throws -> UUID {
        guard let device = try HALAudioDevice.makeDevice(forUID: configuration.device.id) else {
            throw CAAudioBridgeError.deviceNotFound
        }
        if callbacks.output == nil && callbacks.input == nil {
            throw CAAudioBridgeError.startStreamFailed
        }

        let active = try ActiveStream(
            device: device,
            callbacks: callbacks,
            configuration: configuration
        )

        let token = UUID()
        let status = AudioDeviceCreateIOProcID(
            device.objectID,
            CAAudioHardwareBridge.ioProc,
            Unmanaged.passUnretained(active).toOpaque(),
            &active.ioProcID
        )

        guard status == noErr, let ioProcID = active.ioProcID else {
            throw CAAudioBridgeError.startStreamFailed
        }

        try active.applyStreamUsage(ioProcID: ioProcID)
        try device.start(ioProcID: ioProcID)

        lock.lock()
        activeStreams[token] = active
        lock.unlock()

        return token
    }

    func stopStream(token: UUID) throws {
        lock.lock()
        guard let active = activeStreams.removeValue(forKey: token) else {
            lock.unlock()
            return
        }
        lock.unlock()

        if let ioProcID = active.ioProcID {
            try active.device.stop(ioProcID: ioProcID)
            AudioDeviceDestroyIOProcID(active.device.objectID, ioProcID)
        }
    }

    func currentSampleTime(token: UUID) -> UInt64? {
        lock.lock()
        let sampleTime = activeStreams[token]?.latestSampleTime
        lock.unlock()
        return sampleTime
    }

    // MARK: - Helpers

    private static func channelCount(for device: HALAudioDevice, scope: HALPropertyScope) throws -> Int {
        let config = try device.streamConfiguration(inScope: scope)
        return config.buffers.reduce(0) { $0 + Int($1.mNumberChannels) }
    }

    private static func configureBufferSize(for device: HALAudioDevice, requested: Int) throws -> Int {
        let range = try device.bufferFrameSizeRange
        let clamped = min(max(requested, range.lowerBound), range.upperBound)
        try device.setBufferFrameSize(clamped)
        return clamped
    }

    private static func configureSampleRate(for device: HALAudioDevice, requested: Double) throws {
        let current = try device.nominalSampleRate
        if abs(current - requested) > 0.1 {
            try device.setNominalSampleRate(requested)
        }
    }

    private static func configureStreamFormats(for device: HALAudioDevice, sampleRate: Double) throws {
        let streams = try device.streams(inScope: .input) + (try device.streams(inScope: .output))
        let format = AudioStreamBasicDescription(
            mSampleRate: sampleRate,
            mFormatID: kAudioFormatLinearPCM,
            mFormatFlags: kAudioFormatFlagIsFloat | kAudioFormatFlagIsNonInterleaved,
            mBytesPerPacket: 4,
            mFramesPerPacket: 1,
            mBytesPerFrame: 4,
            mChannelsPerFrame: 1,
            mBitsPerChannel: 32,
            mReserved: 0
        )

        for stream in streams {
            if let currentVirtual = try? stream.virtualFormat,
               !Self.formatsEqual(currentVirtual, format) {
                try? stream.setVirtualFormat(format)
            }
            if let currentPhysical = try? stream.physicalFormat,
               !Self.formatsEqual(currentPhysical, format) {
                try? stream.setPhysicalFormat(format)
            }
        }
    }

    private static func formatsEqual(_ lhs: AudioStreamBasicDescription,
                                     _ rhs: AudioStreamBasicDescription) -> Bool {
        return lhs.mSampleRate == rhs.mSampleRate &&
               lhs.mFormatID == rhs.mFormatID &&
               lhs.mFormatFlags == rhs.mFormatFlags &&
               lhs.mBytesPerPacket == rhs.mBytesPerPacket &&
               lhs.mFramesPerPacket == rhs.mFramesPerPacket &&
               lhs.mBytesPerFrame == rhs.mBytesPerFrame &&
               lhs.mChannelsPerFrame == rhs.mChannelsPerFrame &&
               lhs.mBitsPerChannel == rhs.mBitsPerChannel
    }

    private static let ioProc: AudioDeviceIOProc = { (_, ioTimestamp, inputData, _, outputData, _, clientData) -> OSStatus in
        guard let clientData else { return noErr }
        let active = Unmanaged<ActiveStream>.fromOpaque(clientData).takeUnretainedValue()
        active.handleIO(ioTimestamp: ioTimestamp, inputData: inputData, outputData: outputData)
        return noErr
    }

    // MARK: - Active Stream

    private final class ActiveStream {
        let device: HALAudioDevice
        let callbacks: CAAudioStreamingCallbacks
        let configuration: CAAudioStreamConfiguration
        let selectedInputChannels: [Int]
        let selectedOutputChannels: [Int]
        let inputChannelIndex: [Int: Int]
        let outputChannelIndex: [Int: Int]
        let inputStreams: [HALAudioStream]
        let outputStreams: [HALAudioStream]
        let bufferFrameSize: UInt32
        var ioProcID: AudioDeviceIOProcID?
        var latestSampleTime: UInt64 = 0

        private var inputScratch: [Float]
        private var outputScratch: [Float]

        init(device: HALAudioDevice,
             callbacks: CAAudioStreamingCallbacks,
             configuration: CAAudioStreamConfiguration) throws {
            self.device = device
            self.callbacks = callbacks
            self.configuration = configuration

            self.selectedInputChannels = configuration.inputChannels
            self.selectedOutputChannels = configuration.outputChannels
            self.inputChannelIndex = Dictionary(uniqueKeysWithValues: configuration.inputChannels.enumerated().map { ($0.element, $0.offset) })
            self.outputChannelIndex = Dictionary(uniqueKeysWithValues: configuration.outputChannels.enumerated().map { ($0.element, $0.offset) })
            self.inputStreams = try device.streams(inScope: .input)
            self.outputStreams = try device.streams(inScope: .output)

            let actualBufferSize = UInt32(try device.bufferFrameSize)
            self.bufferFrameSize = actualBufferSize

            let framesPerBuffer = Int(max(actualBufferSize, 1))
            self.inputScratch = configuration.inputChannels.isEmpty ? [] : Array(repeating: 0, count: framesPerBuffer * configuration.inputChannels.count)
            self.outputScratch = configuration.outputChannels.isEmpty ? [] : Array(repeating: 0, count: framesPerBuffer * configuration.outputChannels.count)
        }

        func applyStreamUsage(ioProcID: AudioDeviceIOProcID) throws {
            try setStreamUsage(for: .input, streams: inputStreams, selectedChannels: selectedInputChannels, ioProcID)
            try setStreamUsage(for: .output, streams: outputStreams, selectedChannels: selectedOutputChannels, ioProcID)
        }

        func handleIO(ioTimestamp: UnsafePointer<AudioTimeStamp>?,
                      inputData: UnsafePointer<AudioBufferList>?,
                      outputData: UnsafeMutablePointer<AudioBufferList>?) {
            if let timestamp = ioTimestamp {
                let flags = timestamp.pointee.mFlags
                if flags.contains(.sampleTimeValid) {
                    latestSampleTime = UInt64(timestamp.pointee.mSampleTime)
                }
            }

            if let inputData,
               let inputCallback = callbacks.input,
               !selectedInputChannels.isEmpty {
                let inputList = UnsafeMutableAudioBufferListPointer(UnsafeMutablePointer(mutating: inputData))
                let usableFrames = min(Self.framesPerBuffer(in: inputList), Int(bufferFrameSize))
                if usableFrames > 0 {
                    fillInputScratch(from: inputList, frames: usableFrames)
                    inputScratch.withUnsafeMutableBufferPointer { buffer in
                        if let baseAddress = buffer.baseAddress {
                            inputCallback(baseAddress, UInt32(usableFrames), UInt32(selectedInputChannels.count))
                        }
                    }
                }
            }

            if let outputData {
                let outputList = UnsafeMutableAudioBufferListPointer(outputData)
                let framesPerCallback = Int(bufferFrameSize)
                let usableFrames = min(Self.framesPerBuffer(in: outputList), framesPerCallback)
                if !selectedOutputChannels.isEmpty,
                   let outputCallback = callbacks.output {
                    for idx in outputScratch.indices { outputScratch[idx] = 0 }
                    outputScratch.withUnsafeMutableBufferPointer { buffer in
                        if let baseAddress = buffer.baseAddress {
                            outputCallback(baseAddress, UInt32(usableFrames), UInt32(selectedOutputChannels.count))
                        }
                    }
                }
                emitOutput(to: outputList, frames: usableFrames)
            }
        }

        private func fillInputScratch(from buffers: UnsafeMutableAudioBufferListPointer, frames: Int) {
            guard !selectedInputChannels.isEmpty else { return }
            let stride = selectedInputChannels.count
            inputScratch.withUnsafeMutableBufferPointer { buffer in
                guard let destBase = buffer.baseAddress else { return }
                for (index, audioBuffer) in buffers.enumerated() {
                    guard index < inputStreams.count else { continue }
                    guard let data = audioBuffer.mData?.assumingMemoryBound(to: Float.self) else { continue }
                    let stream = inputStreams[index]
                    guard let startElement = try? stream.startingChannel else { continue }
                    let streamStart = Int(startElement.rawValue)
                    let channelsInBuffer = max(Int(audioBuffer.mNumberChannels), 1)
                    let framesAvailable = min(frames, Int(audioBuffer.mDataByteSize) / (channelsInBuffer * MemoryLayout<Float>.size))
                    guard framesAvailable > 0 else { continue }
                    for channelOffset in 0..<channelsInBuffer {
                        let channelNumber = streamStart + channelOffset
                        guard let destChannelIndex = inputChannelIndex[channelNumber] else { continue }
                        for frame in 0..<framesAvailable {
                            let sourceIndex = frame * channelsInBuffer + channelOffset
                            destBase[frame * stride + destChannelIndex] = data[sourceIndex]
                        }
                    }
                }
            }
        }

        private func emitOutput(to buffers: UnsafeMutableAudioBufferListPointer, frames: Int) {
            if selectedOutputChannels.isEmpty {
                for buffer in buffers {
                    guard let data = buffer.mData else { continue }
                    memset(data, 0, Int(buffer.mDataByteSize))
                }
                return
            }
            let stride = max(selectedOutputChannels.count, 1)
            for (index, audioBuffer) in buffers.enumerated() {
                guard index < outputStreams.count else { continue }
                guard let data = audioBuffer.mData?.assumingMemoryBound(to: Float.self) else { continue }
                let stream = outputStreams[index]
                guard let startElement = try? stream.startingChannel else { continue }
                let streamStart = Int(startElement.rawValue)
                let channelsInBuffer = max(Int(audioBuffer.mNumberChannels), 1)
                let framesAvailable = min(frames, Int(audioBuffer.mDataByteSize) / (channelsInBuffer * MemoryLayout<Float>.size))
                guard framesAvailable > 0 else {
                    memset(data, 0, Int(audioBuffer.mDataByteSize))
                    continue
                }

                for channelOffset in 0..<channelsInBuffer {
                    let channelNumber = streamStart + channelOffset
                    let destBaseIndex = channelOffset
                    if let sourceChannelIndex = outputChannelIndex[channelNumber],
                       !outputScratch.isEmpty {
                        outputScratch.withUnsafeBufferPointer { buffer in
                            guard let srcBase = buffer.baseAddress else { return }
                            for frame in 0..<framesAvailable {
                                let destIndex = frame * channelsInBuffer + destBaseIndex
                                let srcIndex = frame * stride + sourceChannelIndex
                                data[destIndex] = srcBase[srcIndex]
                            }
                        }
                    } else {
                        for frame in 0..<framesAvailable {
                            let destIndex = frame * channelsInBuffer + destBaseIndex
                            data[destIndex] = 0
                        }
                    }
                }
            }
        }

        private static func framesPerBuffer(in buffers: UnsafeMutableAudioBufferListPointer) -> Int {
            var minFrames = Int.max
            for buffer in buffers {
                let channels = max(Int(buffer.mNumberChannels), 1)
                let bytesPerFrame = channels * MemoryLayout<Float>.size
                guard bytesPerFrame > 0 else { continue }
                let frames = Int(buffer.mDataByteSize) / bytesPerFrame
                minFrames = min(minFrames, frames)
            }
            return minFrames == Int.max ? 0 : minFrames
        }

        private func setStreamUsage(for scope: HALPropertyScope,
                                    streams: [HALAudioStream],
                                    selectedChannels: [Int],
                                    _ procID: AudioDeviceIOProcID) throws {
            guard !streams.isEmpty else { return }
            let streamCount = streams.count
            let byteCount = AudioHardwareIOProcStreamUsage.sizeInBytes(maximumStreams: streamCount)
            let raw = UnsafeMutablePointer<UInt8>.allocate(capacity: byteCount)
            raw.initialize(repeating: 0, count: byteCount)
            defer { raw.deallocate() }

            let usage: UnsafeMutablePointer<AudioHardwareIOProcStreamUsage> = raw.withMemoryRebound(to: AudioHardwareIOProcStreamUsage.self, capacity: 1) { ptr in
                ptr.pointee.mIOProc = unsafeBitCast(CAAudioHardwareBridge.ioProc, to: UnsafeMutableRawPointer.self)
                ptr.pointee.mNumberStreams = UInt32(streamCount)
                return ptr
            }

            let offset = MemoryLayout<AudioHardwareIOProcStreamUsage>.offset(of: \AudioHardwareIOProcStreamUsage.mStreamIsOn)!
            let flagsPtr = UnsafeMutableRawPointer(raw).advanced(by: offset).assumingMemoryBound(to: UInt32.self)

            for (index, stream) in streams.enumerated() {
                guard let startElement = try? stream.startingChannel else {
                    flagsPtr[index] = 0
                    continue
                }
                let start = Int(startElement.rawValue)
                let channelCount = (try? stream.virtualFormat).map { Int($0.mChannelsPerFrame) } ?? 0
                let end = start + max(channelCount, 0)
                let enabled = !selectedChannels.isEmpty && selectedChannels.contains(where: { $0 >= start && $0 < end })
                flagsPtr[index] = enabled ? 1 : 0
            }

            try device.setIOProcStreamUsage(UnsafePointer(usage), inScope: scope)
        }
    }
}
#endif
