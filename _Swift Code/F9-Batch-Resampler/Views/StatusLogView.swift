//
//  StatusLogView.swift
//  F9-Batch-Resampler
//
//  Created by James Wiltshire on 09/09/2025.
//

import SwiftUI
import AppKit

struct StatusLogView: View {
    let lines: [String]
    @State private var showCopiedConfirmation = false

    var body: some View {
        GroupBox {
            VStack(spacing: 0) {
                // Header with title and copy button
                HStack {
                    Text("Log")
                        .font(.headline)
                    Spacer()
                    Button(action: copyLogToClipboard) {
                        HStack(spacing: 4) {
                            Image(systemName: showCopiedConfirmation ? "checkmark" : "doc.on.doc")
                            Text(showCopiedConfirmation ? "Copied!" : "Copy Log")
                        }
                        .font(.caption)
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.small)
                }
                .padding(.horizontal, 8)
                .padding(.vertical, 4)

                Divider()

                // Scrollable log content
                ScrollView {
                    VStack(alignment: .leading, spacing: 4) {
                        ForEach(lines, id: \.self) { line in
                            Text(line)
                                .font(.system(size: 12, design: .monospaced))
                                .textSelection(.enabled)
                                .frame(maxWidth: .infinity, alignment: .leading)
                        }
                    }
                    .padding(8)
                }
                .frame(minHeight: 160)
            }
        }
    }

    private func copyLogToClipboard() {
        let logText = lines.joined(separator: "\n")
        let pasteboard = NSPasteboard.general
        pasteboard.clearContents()
        pasteboard.setString(logText, forType: .string)

        // Show confirmation
        showCopiedConfirmation = true

        // Reset after 2 seconds
        DispatchQueue.main.asyncAfter(deadline: .now() + 2) {
            showCopiedConfirmation = false
        }
    }
}
