import Foundation
import CoreAudio
import AudioToolbox
import AVFoundation

// Lightweight Core Audio device lister for macOS
final class AudioService {

    // MARK: - Mic permission (needed for recording later)
    func requestMicrophoneAccess(completion: @escaping (Bool) -> Void) {
        // macOS uses AVCaptureDevice for mic permission
        switch AVCaptureDevice.authorizationStatus(for: .audio) {
        case .authorized:
            completion(true)
        case .notDetermined:
            AVCaptureDevice.requestAccess(for: .audio) { granted in
                completion(granted)
            }
        default:
            completion(false)
        }
    }

    // MARK: - Public API

    func listDevices() -> [AudioDevice] {
        var result: [AudioDevice] = []
        let ids = allDeviceIDs()

        for devID in ids {
            let name = deviceName(devID) ?? "Unknown Device"
            let uid  = deviceUID(devID) ?? "unknown-\(devID)"
            let inCh = channelCount(devID, scope: kAudioObjectPropertyScopeInput)
            let outCh = channelCount(devID, scope: kAudioObjectPropertyScopeOutput)

            result.append(AudioDevice(
                name: name,
                inputChannelCount: inCh,
                outputChannelCount: outCh,
                uniqueID: uid
            ))
        }

        // Keep a predictable sort: multichannel first, then name
        result.sort {
            let aMulti = ($0.inputChannelCount + $0.outputChannelCount) > 2
            let bMulti = ($1.inputChannelCount + $1.outputChannelCount) > 2
            if aMulti != bMulti { return aMulti && !bMulti }
            return $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending
        }
        return result
    }

    // MARK: - Core Audio helpers

    private func allDeviceIDs() -> [AudioDeviceID] {
        var addr = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDevices,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )

        var dataSize: UInt32 = 0
        var status = AudioObjectGetPropertyDataSize(
            AudioObjectID(kAudioObjectSystemObject),
            &addr,
            0,
            nil,
            &dataSize
        )
        guard status == noErr else { return [] }

        let count = Int(dataSize) / MemoryLayout<AudioDeviceID>.size
        var deviceIDs = Array(repeating: AudioDeviceID(0), count: count)

        status = AudioObjectGetPropertyData(
            AudioObjectID(kAudioObjectSystemObject),
            &addr,
            0,
            nil,
            &dataSize,
            &deviceIDs
        )
        guard status == noErr else { return [] }
        return deviceIDs
    }

    private func deviceName(_ id: AudioDeviceID) -> String? {
        var addr = AudioObjectPropertyAddress(
            mSelector: kAudioObjectPropertyName,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        return copyCFStringProperty(id, &addr)
    }

    private func deviceUID(_ id: AudioDeviceID) -> String? {
        var addr = AudioObjectPropertyAddress(
            mSelector: kAudioDevicePropertyDeviceUID,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        return copyCFStringProperty(id, &addr)
    }

    private func channelCount(_ id: AudioDeviceID, scope: AudioObjectPropertyScope) -> Int {
        var addr = AudioObjectPropertyAddress(
            mSelector: kAudioDevicePropertyStreamConfiguration,
            mScope: scope,
            mElement: kAudioObjectPropertyElementMain
        )

        var dataSize: UInt32 = 0
        var status = AudioObjectGetPropertyDataSize(id, &addr, 0, nil, &dataSize)
        if status != noErr || dataSize == 0 { return 0 }

        // Allocate a single AudioBufferList large enough for the returned size
        let bufferListPtr = UnsafeMutablePointer<AudioBufferList>.allocate(capacity: 1)
        defer { bufferListPtr.deallocate() }

        status = AudioObjectGetPropertyData(id, &addr, 0, nil, &dataSize, bufferListPtr)
        if status != noErr { return 0 }

        let abl = UnsafeMutableAudioBufferListPointer(bufferListPtr)
        var channels = 0
        for buf in abl {
            channels += Int(buf.mNumberChannels)
        }
        return channels
    }


    private func copyCFStringProperty(_ id: AudioObjectID,
                                      _ addr: inout AudioObjectPropertyAddress) -> String? {
        var dataSize: UInt32 = 0
        var status = AudioObjectGetPropertyDataSize(id, &addr, 0, nil, &dataSize)
        if status != noErr || dataSize == 0 { return nil }

        var cfStr: CFString?
        status = withUnsafeMutablePointer(to: &cfStr) { ptr in
            AudioObjectGetPropertyData(id, &addr, 0, nil, &dataSize, ptr)
        }
        guard status == noErr, let s = cfStr as String? else { return nil }
        return s
    }

}

