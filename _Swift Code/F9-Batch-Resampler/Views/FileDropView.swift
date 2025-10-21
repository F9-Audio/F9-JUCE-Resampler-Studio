//
//  FileDropView.swift
//  F9-Batch-Resampler
//
//  Created by James Wiltshire on 09/09/2025.
//

import SwiftUI
import UniformTypeIdentifiers

struct FileDropView: View {
    @ObservedObject var vm: MainViewModel

    var body: some View {
        VStack(spacing: 12) {
            GroupBox("Files") {
                VStack(alignment: .leading, spacing: 10) {
                    // Drop zone
                    ZStack {
                        RoundedRectangle(cornerRadius: 8)
                            .stroke(style: StrokeStyle(lineWidth: 2, dash: [6]))
                            .foregroundColor(vm.isProcessing ? .secondary : .blue)
                            .frame(maxWidth: .infinity, minHeight: 80)
                        
                        VStack(spacing: 4) {
                            Image(systemName: "arrow.down.doc")
                                .font(.title)
                            Text("Drag audio files here")
                                .font(.caption)
                        }
                        .foregroundStyle(.secondary)
                    }
                    .onDrop(of: [.fileURL], isTargeted: nil) { providers in
                        handleDrop(providers: providers)
                        return true
                    }
                    .disabled(vm.isProcessing)

                    // File list with selection
                    if vm.files.isEmpty {
                        Text("No files added")
                            .foregroundStyle(.secondary)
                            .frame(maxWidth: .infinity, alignment: .center)
                            .padding()
                    } else {
                        VStack(spacing: 8) {
                            // Selection controls
                            HStack {
                                Button("Select All") { vm.selectAllFiles() }
                                    .buttonStyle(.borderless)
                                Button("Deselect All") { vm.deselectAllFiles() }
                                    .buttonStyle(.borderless)
                                
                                Spacer()
                                
                                Text("\(vm.files.count) file(s)")
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                            }
                            
                            // Scrollable file list
                            ScrollView {
                                VStack(spacing: 4) {
                                    ForEach(vm.files) { file in
                                        FileRowView(file: file, vm: vm)
                                    }
                                }
                            }
                            .frame(maxHeight: 200)
                            .background(Color(nsColor: .controlBackgroundColor))
                            .cornerRadius(6)
                        }
                    }
                }
                .padding(8)
            }
            
            // Processing controls
            HStack(spacing: 12) {
                // Preview button
                Button {
                    vm.togglePreview()
                } label: {
                    HStack {
                        Image(systemName: vm.isPreviewing ? "stop.fill" : "play.fill")
                        Text(vm.isPreviewing ? "Stop Preview" : "Preview Selected")
                    }
                    .frame(maxWidth: .infinity)
                }
                .disabled(vm.isProcessing || vm.files.filter { $0.isSelected }.isEmpty)
                .keyboardShortcut(.space, modifiers: [])
                
                // Process all button
                Button {
                    Task {
                        await vm.processAllFiles()
                    }
                } label: {
                    HStack {
                        if vm.isProcessing {
                            ProgressView()
                                .scaleEffect(0.7)
                                .frame(width: 16, height: 16)
                        } else {
                            Image(systemName: "waveform.circle")
                        }
                        Text(vm.isProcessing ? "Processing..." : "Process All")
                    }
                    .frame(maxWidth: .infinity)
                }
                .disabled(vm.isProcessing || vm.isPreviewing || vm.files.isEmpty || vm.settings.outputFolderPath == nil)
            }
            .buttonStyle(.borderedProminent)

            // Show helpful hints based on what's missing
            if !vm.files.isEmpty {
                if vm.settings.outputFolderPath == nil {
                    HStack {
                        Image(systemName: "info.circle")
                            .foregroundStyle(.orange)
                        Text("Select a destination folder in Settings to begin processing")
                            .font(.caption)
                            .foregroundStyle(.orange)
                    }
                } else if vm.settings.measuredLatencySamples == nil || vm.settings.needsLatencyRemeasurement {
                    HStack {
                        Image(systemName: "info.circle")
                            .foregroundStyle(.blue)
                        Text(vm.settings.measuredLatencySamples == nil ? "Latency will be measured automatically before processing" : "Buffer size changed - latency will be re-measured before processing")
                            .font(.caption)
                            .foregroundStyle(.blue)
                    }
                }
            }

            if vm.isPreviewing {
                VStack(spacing: 4) {
                    ProgressView(value: vm.previewProgress)
                        .frame(maxWidth: 200)
                    if let index = vm.currentPreviewFileIndex,
                       index < vm.previewPlaylist.count,
                       let file = vm.files.first(where: { $0.id == vm.previewPlaylist[index] }) {
                        Text("Previewing: \(file.fileName)")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
            }
            // Processing indicator
            else if vm.isProcessing {
                VStack(spacing: 4) {
                    ProgressView(value: min(1.0, max(0.0, vm.processingProgress)))
                    Text("Processing: \(vm.currentProcessingFile)")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
        }
    }
    
    private func handleDrop(providers: [NSItemProvider]) {
        var urls: [URL] = []
        let group = DispatchGroup()
        
        for provider in providers {
            group.enter()
            _ = provider.loadObject(ofClass: URL.self) { url, _ in
                if let url = url {
                    urls.append(url)
                }
                group.leave()
            }
        }
        
        group.notify(queue: .main) {
            vm.addFiles(urls: urls)
        }
    }
}

struct FileRowView: View {
    let file: AudioFile
    @ObservedObject var vm: MainViewModel
    
    var body: some View {
        let isCurrentlyPreviewing = isPreviewFile
        let backgroundColor: Color = {
            if isCurrentlyPreviewing { return Color.green.opacity(0.18) }
            if file.isSelected { return Color.blue.opacity(0.1) }
            return Color.clear
        }()
        
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 8) {
                Button {
                    vm.toggleFileSelection(file)
                } label: {
                    Image(systemName: file.isSelected ? "checkmark.square.fill" : "square")
                        .foregroundStyle(file.isSelected ? .blue : .secondary)
                }
                .buttonStyle(.borderless)
                
                VStack(alignment: .leading, spacing: 2) {
                    Text(file.fileName)
                        .lineLimit(1)
                        .font(.system(.body, design: .default))
                    
                    HStack(spacing: 8) {
                        if let sampleRate = file.sampleRate {
                            Text("\(String(format: "%.1f", sampleRate / 1000.0))kHz")
                                .font(.caption)
                                .foregroundColor(file.isValid ? .secondary : .red)
                        }
                        
                        Text(file.status.rawValue)
                            .font(.caption)
                            .foregroundStyle(statusColor(for: file.status))
                    }
                }
                
                Spacer()
                statusIcon(for: file.status)
            }
            
            if isCurrentlyPreviewing {
                ProgressView(value: vm.previewProgress)
                    .progressViewStyle(.linear)
            }
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 6)
        .background(backgroundColor)
        .cornerRadius(4)
    }
    
    private var isPreviewFile: Bool {
        guard let currentIndex = vm.currentPreviewFileIndex,
              currentIndex < vm.previewPlaylist.count else { return false }
        return vm.previewPlaylist[currentIndex] == file.id
    }
    
    private func statusColor(for status: AudioFile.ProcessingStatus) -> Color {
        switch status {
        case .pending: return .secondary
        case .processing: return .blue
        case .completed: return .green
        case .failed: return .red
        case .invalidSampleRate: return .orange
        }
    }
    
    private func statusIcon(for status: AudioFile.ProcessingStatus) -> some View {
        Group {
            switch status {
            case .pending:
                Image(systemName: "clock")
                    .foregroundStyle(.secondary)
            case .processing:
                ProgressView()
                    .scaleEffect(0.7)
            case .completed:
                Image(systemName: "checkmark.circle.fill")
                    .foregroundStyle(.green)
            case .failed:
                Image(systemName: "xmark.circle.fill")
                    .foregroundStyle(.red)
            case .invalidSampleRate:
                Image(systemName: "exclamationmark.triangle.fill")
                    .foregroundStyle(.orange)
            }
        }
    }
}
