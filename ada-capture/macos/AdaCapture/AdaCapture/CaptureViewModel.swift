import AppKit
import AVFoundation
import Foundation

@MainActor
final class CaptureViewModel: ObservableObject {
    @Published var targetPath: String = ""
    @Published var argsText: String = ""
    @Published var outputPath: String = ""
    @Published var mode: SessionMode = .launch
    @Published var pidText: String = ""
    @Published var isSessionActive: Bool = false
    @Published var isVoiceActive: Bool = false
    @Published var statusMessage: String = "Idle"
    @Published var bundles: [BundleItem] = []
    @Published var traceSessionPath: String? = nil

    private var daemon: DaemonClient?
    private var voiceStartDate: Date?
    private var audioRecorder: AVAudioRecorder?

    init() {
        outputPath = defaultOutputPath()
        do {
            daemon = try DaemonClient()
        } catch {
            statusMessage = "Failed to start daemon: \(error)"
        }
    }

    func chooseTarget() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = true
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false

        if panel.runModal() == .OK {
            targetPath = panel.url?.path ?? ""
        }
    }

    func chooseOutput() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false

        if panel.runModal() == .OK {
            outputPath = panel.url?.path ?? ""
        }
    }

    func toggleRecording() {
        Task {
            if isSessionActive || isVoiceActive {
                await stopRecording()
            } else {
                await startRecording()
            }
        }
    }

    func startSession() {
        guard let daemon else { return }
        let output = outputPath.isEmpty ? nil : outputPath
        let args = parseArgs(argsText)
        let (binary, pid) = resolveStartMode()

        if mode == .attach && pid == nil {
            statusMessage = "Invalid PID"
            return
        }

        Task {
            do {
                statusMessage = "Starting session..."
                let info = try daemon.startSession(binary: binary, pid: pid, args: args, output: output)
                isSessionActive = true
                isVoiceActive = info.is_voice_active
                traceSessionPath = info.trace_session
                statusMessage = "Session ready"
            } catch {
                statusMessage = "Start session failed: \(error)"
            }
        }
    }

    func stopSession() {
        guard let daemon else { return }
        Task {
            do {
                stopLocalVoiceRecording()
                statusMessage = "Stopping session..."
                let info = try daemon.stopSession()
                isSessionActive = info.is_session_active
                isVoiceActive = info.is_voice_active
                statusMessage = "Session stopped"
            } catch {
                statusMessage = "Stop session failed: \(error)"
            }
        }
    }

    func toggleVoice() {
        guard let daemon else { return }
        Task {
            if isVoiceActive {
                do {
                    statusMessage = "Stopping voice..."
                    stopLocalVoiceRecording()
                    let bundleInfo = try daemon.stopVoice()
                    isVoiceActive = false
                    traceSessionPath = bundleInfo.trace_session
                    let end = Date()
                    let start = voiceStartDate ?? end
                    voiceStartDate = nil
                    bundles.insert(
                        BundleItem(bundlePath: bundleInfo.bundle_path, startedAt: start, endedAt: end),
                        at: 0
                    )
                    statusMessage = "Bundle ready"
                } catch {
                    statusMessage = "Stop voice failed: \(error)"
                }
            } else {
                do {
                    statusMessage = "Starting voice..."
                    try await ensureMicrophoneAccess()
                    let info = try daemon.startVoice()
                    do {
                        try startLocalVoiceRecording(at: info.voice_path)
                    } catch {
                        let _ = try? daemon.stopVoice()
                        isVoiceActive = false
                        throw error
                    }
                    isVoiceActive = info.is_voice_active
                    voiceStartDate = Date()
                    statusMessage = "Voice recording"
                } catch {
                    stopLocalVoiceRecording()
                    statusMessage = "Start voice failed: \(error)"
                }
            }
        }
    }

    private func startRecording() async {
        guard let daemon else { return }
        let output = outputPath.isEmpty ? nil : outputPath
        let args = parseArgs(argsText)
        let (binary, pid) = resolveStartMode()

        if mode == .attach && pid == nil {
            statusMessage = "Invalid PID"
            return
        }

        var sessionStarted = false

        do {
            statusMessage = "Starting session..."
            let info = try daemon.startSession(binary: binary, pid: pid, args: args, output: output)
            sessionStarted = true
            isSessionActive = true
            traceSessionPath = info.trace_session

            statusMessage = "Starting voice..."
            try await ensureMicrophoneAccess()
            let voiceInfo = try daemon.startVoice()
            do {
                try startLocalVoiceRecording(at: voiceInfo.voice_path)
            } catch {
                let _ = try? daemon.stopVoice()
                throw error
            }

            isVoiceActive = voiceInfo.is_voice_active
            voiceStartDate = Date()
            statusMessage = "Recording"
        } catch {
            stopLocalVoiceRecording()
            if sessionStarted {
                let _ = try? daemon.stopSession()
            }
            isSessionActive = false
            isVoiceActive = false
            statusMessage = "Start recording failed: \(error)"
        }
    }

    private func stopRecording() async {
        guard let daemon else { return }
        var voiceError: Error?

        if isVoiceActive {
            do {
                statusMessage = "Stopping voice..."
                stopLocalVoiceRecording()
                let bundleInfo = try daemon.stopVoice()
                isVoiceActive = false
                traceSessionPath = bundleInfo.trace_session
                let end = Date()
                let start = voiceStartDate ?? end
                voiceStartDate = nil
                bundles.insert(
                    BundleItem(bundlePath: bundleInfo.bundle_path, startedAt: start, endedAt: end),
                    at: 0
                )
            } catch {
                voiceError = error
            }
        }

        do {
            statusMessage = "Stopping session..."
            let info = try daemon.stopSession()
            isSessionActive = info.is_session_active
            isVoiceActive = info.is_voice_active
            if let voiceError {
                statusMessage = "Stop voice failed: \(voiceError)"
            } else {
                statusMessage = "Recording stopped"
            }
        } catch {
            statusMessage = "Stop session failed: \(error)"
        }
    }

    func revealBundle(_ item: BundleItem) {
        let url = URL(fileURLWithPath: item.bundlePath)
        NSWorkspace.shared.activateFileViewerSelecting([url])
    }

    func revealTrace() {
        guard let traceSessionPath else { return }
        let url = URL(fileURLWithPath: traceSessionPath)
        NSWorkspace.shared.activateFileViewerSelecting([url])
    }

    private func resolveExecutablePath(_ path: String) -> String {
        let url = URL(fileURLWithPath: path)
        if url.pathExtension == "app" {
            let infoPlist = url.appendingPathComponent("Contents/Info.plist")
            if let dict = NSDictionary(contentsOf: infoPlist),
               let executable = dict["CFBundleExecutable"] as? String {
                return url
                    .appendingPathComponent("Contents/MacOS")
                    .appendingPathComponent(executable)
                    .path
            }
        }
        return path
    }

    private func resolveStartMode() -> (String?, Int?) {
        switch mode {
        case .launch:
            return (resolveExecutablePath(targetPath), nil)
        case .attach:
            let pid = Int(pidText.trimmingCharacters(in: .whitespaces))
            return (nil, pid)
        }
    }

    private func parseArgs(_ text: String) -> [String] {
        text.split(separator: " ").map { String($0) }
    }

    private func defaultOutputPath() -> String {
        let home = FileManager.default.homeDirectoryForCurrentUser
        return home.appendingPathComponent("ADA-Captures").path
    }

    private func ensureMicrophoneAccess() async throws {
        let status = AVCaptureDevice.authorizationStatus(for: .audio)
        switch status {
        case .authorized:
            return
        case .notDetermined:
            let granted = await withCheckedContinuation { continuation in
                AVCaptureDevice.requestAccess(for: .audio) { allowed in
                    continuation.resume(returning: allowed)
                }
            }
            if !granted {
                throw AudioRecordingError.permissionDenied
            }
        case .denied, .restricted:
            throw AudioRecordingError.permissionDenied
        @unknown default:
            throw AudioRecordingError.permissionDenied
        }
    }

    private func startLocalVoiceRecording(at path: String) throws {
        let url = URL(fileURLWithPath: path)
        let settings: [String: Any] = [
            AVFormatIDKey: kAudioFormatLinearPCM,
            AVSampleRateKey: 48_000,
            AVNumberOfChannelsKey: 1,
            AVLinearPCMBitDepthKey: 16,
            AVLinearPCMIsFloatKey: false,
            AVLinearPCMIsBigEndianKey: false,
        ]

        let recorder = try AVAudioRecorder(url: url, settings: settings)
        recorder.isMeteringEnabled = false
        recorder.prepareToRecord()
        guard recorder.record() else {
            throw AudioRecordingError.startFailed
        }
        audioRecorder = recorder
    }

    private func stopLocalVoiceRecording() {
        audioRecorder?.stop()
        audioRecorder = nil
    }
}

private enum AudioRecordingError: Error, CustomStringConvertible {
    case permissionDenied
    case startFailed

    var description: String {
        switch self {
        case .permissionDenied:
            return "microphone permission denied"
        case .startFailed:
            return "failed to start microphone recording"
        }
    }
}
