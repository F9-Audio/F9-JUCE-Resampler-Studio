import SwiftUI

struct DeviceSelectionView: View {
    @ObservedObject var vm: MainViewModel

    var body: some View {
        GroupBox("Audio Interface Selection") {
            VStack(alignment: .leading, spacing: 12) {
                // Device Selection
                VStack(alignment: .leading, spacing: 6) {
                    Text("Audio Device:")
                        .font(.headline)
                    
                    if vm.devices.isEmpty {
                        Text("No external audio interfaces detected")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .padding(.vertical, 4)
                    } else {
                        Picker("", selection: $vm.selectedDeviceID) {
                            Text("Select audio device...")
                                .tag(nil as String?)
                            
                            ForEach(vm.devices) { device in
                                Text("\(device.name) (\(device.inputChannelCount)in/\(device.outputChannelCount)out)")
                                    .tag(device.uniqueID as String?)
                            }
                        }
                        .labelsHidden()
                        .frame(maxWidth: .infinity)
                        
                        if let selected = vm.selectedDevice {
                            HStack {
                                Image(systemName: "checkmark.circle.fill")
                                    .foregroundStyle(.green)
                                Text("\(selected.name) - \(selected.inputChannelCount) inputs, \(selected.outputChannelCount) outputs")
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                            }
                        }
                    }
                }
                
                Divider()
                
                // Input Stereo Pair Selection
                VStack(alignment: .leading, spacing: 6) {
                    Text("Input Stereo Pair:")
                        .font(.headline)
                    
                    if vm.selectedDevice == nil {
                        Text("Select a device first")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .padding(.vertical, 4)
                    } else if vm.availableInputPairs.isEmpty {
                        Text("Selected device has no stereo inputs")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .padding(.vertical, 4)
                    } else {
                        Picker("", selection: $vm.selectedInputPair) {
                            Text("Select input stereo pair...")
                                .tag(nil as StereoPair?)
                            
                            ForEach(vm.availableInputPairs) { pair in
                                Text("Channels \(pair.leftChannel)-\(pair.rightChannel)")
                                    .tag(pair as StereoPair?)
                            }
                        }
                        .labelsHidden()
                        .frame(maxWidth: .infinity)
                        
                        if let selected = vm.selectedInputPair {
                            HStack {
                                Image(systemName: "checkmark.circle.fill")
                                    .foregroundStyle(.green)
                                Text("Ch \(selected.leftChannel) (L) + Ch \(selected.rightChannel) (R)")
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                            }
                        }
                    }
                }
                
                Divider()
                
                // Output Stereo Pair Selection
                VStack(alignment: .leading, spacing: 6) {
                    Text("Output Stereo Pair:")
                        .font(.headline)
                    
                    if vm.selectedDevice == nil {
                        Text("Select a device first")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .padding(.vertical, 4)
                    } else if vm.availableOutputPairs.isEmpty {
                        Text("Selected device has no stereo outputs")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .padding(.vertical, 4)
                    } else {
                        Picker("", selection: $vm.selectedOutputPair) {
                            Text("Select output stereo pair...")
                                .tag(nil as StereoPair?)
                            
                            ForEach(vm.availableOutputPairs) { pair in
                                Text("Channels \(pair.leftChannel)-\(pair.rightChannel)")
                                    .tag(pair as StereoPair?)
                            }
                        }
                        .labelsHidden()
                        .frame(maxWidth: .infinity)
                        
                        if let selected = vm.selectedOutputPair {
                            HStack {
                                Image(systemName: "checkmark.circle.fill")
                                    .foregroundStyle(.green)
                                Text("Ch \(selected.leftChannel) (L) + Ch \(selected.rightChannel) (R)")
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                            }
                        }
                    }
                }
                
                Divider()
                
                // Hardware Loop Test
                VStack(alignment: .leading, spacing: 8) {
                    Text("Hardware Loop Test:")
                        .font(.headline)
                    
                    Text("Generate 1 kHz sine wave to test audio connection")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    
                    HStack {
                        Button {
                            vm.startHardwareLoopTest()
                        } label: {
                            HStack {
                                Image(systemName: "waveform")
                                Text("Start Loop Test")
                            }
                        }
                        .disabled(vm.selectedInputPair == nil || vm.selectedOutputPair == nil)
                        
                        Button {
                            vm.stopHardwareLoopTest()
                        } label: {
                            HStack {
                                Image(systemName: "stop.circle")
                                Text("Stop Test")
                            }
                        }
                        .disabled(!vm.hardwareLoopTest.isRunning)
                        
                        Spacer()
                        
                        if vm.hardwareLoopTest.isRunning {
                            HStack {
                                Circle()
                                    .fill(.green)
                                    .frame(width: 8, height: 8)
                                Text("Test Running")
                                    .font(.caption)
                                    .foregroundStyle(.green)
                            }
                        }
                    }
                    
                    if !vm.hardwareLoopTest.testResults.isEmpty {
                        Text(vm.hardwareLoopTest.testResults)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .padding(.top, 4)
                    }
                }
                
                Divider()
                
                // Refresh and Info
                HStack {
                    Button {
                        vm.refreshDevices()
                    } label: {
                        HStack {
                            Image(systemName: "arrow.clockwise")
                            Text("Refresh Devices")
                        }
                    }
                    
                    Spacer()
                    
                    HStack(spacing: 4) {
                        Image(systemName: "info.circle")
                            .foregroundStyle(.secondary)
                        Text("Built-in Apple audio devices are hidden")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
            }
            .padding(12)
        }
    }
}
