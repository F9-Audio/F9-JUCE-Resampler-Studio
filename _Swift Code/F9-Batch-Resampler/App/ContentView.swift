//
//  ContentView.swift
//  F9-Batch-Resampler
//
//  Created by James Wiltshire on 09/09/2025.
//


import SwiftUI

struct ContentView: View {
    @StateObject private var vm = MainViewModel()

    var body: some View {
        NavigationSplitView {
            ScrollView {
                VStack(spacing: 16) {
                    DeviceSelectionView(vm: vm)
                    SettingsView(vm: vm)
                }
                .padding()
                .padding(.top, 8)  // Extra top padding to clear traffic lights
            }
            .navigationTitle("F9 Batch Resampler")
        } detail: {
            VStack(spacing: 16) {
                FileDropView(vm: vm)
                StatusLogView(lines: vm.logLines)
            }
            .padding()
        }
        .frame(minWidth: 1000, minHeight: 650)
        .onAppear { vm.appendLog("App launched") }
    }
}

