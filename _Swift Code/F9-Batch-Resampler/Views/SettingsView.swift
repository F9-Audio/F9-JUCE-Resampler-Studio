//
//  SettingsView.swift
//  F9-Batch-Resampler
//
//  Created by James Wiltshire on 09/09/2025.
//

import SwiftUI

struct SettingsView: View {
    @ObservedObject var vm: MainViewModel

    var body: some View {
        VStack(spacing: 16) {
            // Audio Interface Settings
            GroupBox("Audio Interface Settings") {
                VStack(alignment: .leading, spacing: 12) {
                    // Buffer Size
                    VStack(alignment: .leading, spacing: 6) {
                        Text("Buffer Size:")
                            .font(.headline)
                        
                        Picker("", selection: Binding(
                            get: { vm.settings.bufferSize },
                            set: { newValue in
                                vm.settings.bufferSize = newValue
                                vm.onBufferSizeChanged()
                            }
                        )) {
                            ForEach(BufferSize.allCases) { size in
                                Text(size.displayName).tag(size)
                            }
                        }
                        .labelsHidden()
                        .disabled(!vm.canMeasureLatency)
                        
                        if !vm.canMeasureLatency {
                            Text("Select input and output stereo pairs to enable")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    }
                    
                    Divider()
                    
                    // Latency Measurement
                    VStack(alignment: .leading, spacing: 8) {
                        Text("Round-Trip Latency:")
                            .font(.headline)
                        
                        if let latencySamples = vm.settings.measuredLatencySamples {
                            HStack {
                                Image(systemName: "checkmark.circle.fill")
                                    .foregroundStyle(.green)
                                VStack(alignment: .leading, spacing: 2) {
                                    Text("\(latencySamples) samples")
                                        .font(.system(.body, design: .monospaced))
                                    Text("(\(String(format: "%.2f", vm.settings.latencyInMs(sampleRate: ProcessingSettings.sampleRate) ?? 0)) ms @ 44.1kHz)")
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                }
                            }
                            
                            if vm.settings.needsLatencyRemeasurement {
                                HStack {
                                    Image(systemName: "exclamationmark.triangle.fill")
                                        .foregroundStyle(.orange)
                                    Text("Buffer size changed - please re-measure")
                                        .font(.caption)
                                        .foregroundStyle(.orange)
                                }
                            }
                        } else {
                            Text("Not measured")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                        
                        Button {
                            Task {
                                await vm.measureLatency()
                            }
                        } label: {
                            HStack {
                                if vm.isMeasuringLatency {
                                    ProgressView()
                                        .scaleEffect(0.7)
                                        .frame(width: 16, height: 16)
                                } else {
                                    Image(systemName: "waveform.circle")
                                }
                                Text(vm.isMeasuringLatency ? "Measuring..." : "Measure Latency")
                            }
                        }
                        .disabled(!vm.canMeasureLatency || vm.isMeasuringLatency)
                        
                        Text("Connect a cable from output to input to measure")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
                .padding(12)
            }
            
            // Output Settings
            GroupBox("Output Settings") {
                VStack(alignment: .leading, spacing: 10) {
                    VStack(alignment: .leading, spacing: 4) {
                        Text("Output Folder:")
                            .font(.headline)

                        if let folderPath = vm.settings.outputFolderPath {
                            HStack {
                                Text(folderPath)
                                    .font(.caption)
                                    .foregroundStyle(.primary)
                                    .lineLimit(1)
                                    .truncationMode(.middle)

                                Spacer()

                                Button("Change...") {
                                    selectOutputFolder()
                                }
                                .buttonStyle(.borderless)
                            }
                        } else {
                            VStack(alignment: .leading, spacing: 6) {
                                HStack {
                                    Image(systemName: "exclamationmark.triangle.fill")
                                        .foregroundStyle(.orange)
                                    Text("No destination folder selected")
                                        .font(.caption)
                                        .foregroundStyle(.orange)
                                }

                                Button("Select Destination Folder...") {
                                    selectOutputFolder()
                                }
                            }
                        }

                        Text("Required - processed files will never overwrite originals")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }

                    Divider()

                    VStack(alignment: .leading, spacing: 4) {
                        Text("Filename Postfix:")
                            .font(.headline)

                        TextField("e.g. _processed", text: Binding(
                            get: { vm.settings.outputPostfix },
                            set: { vm.settings.outputPostfix = $0 }
                        ))
                        .textFieldStyle(.roundedBorder)

                        Text("Leave empty to keep original filename")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
                .padding(8)
            }
            
            // Processing Settings
            GroupBox("Processing Settings") {
                VStack(alignment: .leading, spacing: 10) {
                    Toggle("Reverb Mode (stop on noise floor)", isOn: Binding(
                        get: { vm.settings.useReverbMode },
                        set: { newValue in
                            vm.settings.useReverbMode = newValue
                            // Auto-measure latency and noise floor when enabling reverb mode
                            if newValue && vm.settings.measuredNoiseFloorDb == nil {
                                Task {
                                    await vm.measureLatency()
                                }
                            }
                        }
                    ))

                    if vm.settings.useReverbMode {
                        VStack(alignment: .leading, spacing: 4) {
                            HStack {
                                Text("Noise floor margin:")
                                Spacer()
                                Text("\(String(format: "%.0f", vm.settings.noiseFloorMarginPercent))%")
                                    .monospaced()
                            }

                            Slider(value: Binding(
                                get: { vm.settings.noiseFloorMarginPercent },
                                set: { vm.settings.noiseFloorMarginPercent = $0 }
                            ), in: 0...50, step: 5)

                            if let noiseFloor = vm.settings.measuredNoiseFloorDb {
                                Text("Measured noise floor: \(String(format: "%.1f", noiseFloor)) dB")
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                            } else {
                                HStack {
                                    Image(systemName: "exclamationmark.triangle.fill")
                                        .foregroundStyle(.orange)
                                    Text("Noise floor will be measured automatically when processing")
                                        .font(.caption)
                                        .foregroundStyle(.orange)
                                }
                            }
                        }
                        .padding(.leading, 20)
                    }
                    
                    Divider()
                    
                    HStack {
                        Text("Silence between files:")
                        Spacer()
                        Text("\(vm.settings.silenceBetweenFilesMs) ms")
                            .monospaced()
                    }
                    
                    Slider(value: Binding(
                        get: { Double(vm.settings.silenceBetweenFilesMs) },
                        set: { vm.settings.silenceBetweenFilesMs = Int($0) }
                    ), in: 0...2000, step: 100)
                    
                    Divider()
                    
                    Toggle("Trim silence", isOn: Binding(
                        get: { vm.settings.trimEnabled },
                        set: { vm.settings.trimEnabled = $0 }
                    ))
                    Toggle("DC removal", isOn: Binding(
                        get: { vm.settings.dcRemovalEnabled },
                        set: { vm.settings.dcRemovalEnabled = $0 }
                    ))
                    
                    HStack {
                        Text("Threshold:")
                        Spacer()
                        Text(String(format: "%.1f dB", vm.settings.thresholdDb))
                            .monospaced()
                    }
                    
                    Slider(value: Binding(
                        get: { vm.settings.thresholdDb },
                        set: { vm.settings.thresholdDb = $0 }
                    ), in: -60...(-20), step: 1)
                }
                .padding(8)
            }
        }
        .alert("Latency Re-measurement Required", isPresented: $vm.showLatencyRemeasureAlert) {
            Button("Measure Now") {
                Task {
                    await vm.measureLatency()
                }
            }
            Button("Later", role: .cancel) { }
        } message: {
            Text("The buffer size has changed since the last latency measurement. Please re-measure for accurate processing.")
        }
    }
    
    private func selectOutputFolder() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.canCreateDirectories = true  // Allow creating new folders
        panel.allowsMultipleSelection = false
        panel.prompt = "Select Destination Folder"
        panel.message = "Choose where to save processed audio files. You can create a new folder."

        // Set initial directory to Documents if no folder selected yet
        if vm.settings.outputFolderPath == nil {
            if let documentsURL = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first {
                panel.directoryURL = documentsURL
            }
        } else if let currentPath = vm.settings.outputFolderPath {
            panel.directoryURL = URL(fileURLWithPath: currentPath)
        }

        if panel.runModal() == .OK, let url = panel.url {
            vm.settings.outputFolderPath = url.path
            vm.appendLog("Output folder set: \(url.lastPathComponent)")
        }
    }
}
