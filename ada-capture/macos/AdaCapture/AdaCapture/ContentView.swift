import SwiftUI

struct ContentView: View {
    @StateObject private var viewModel = CaptureViewModel()
    @State private var appear = false

    private let titleFont = Font.custom("Avenir Next", size: 24).weight(.semibold)
    private let sectionFont = Font.custom("Avenir Next", size: 12).weight(.semibold)
    private let bodyFont = Font.custom("Avenir Next", size: 13)
    private let accent = Color(red: 0.84, green: 0.18, blue: 0.16)

    private var isRecording: Bool {
        viewModel.isSessionActive || viewModel.isVoiceActive
    }

    private var canStart: Bool {
        switch viewModel.mode {
        case .launch:
            return !viewModel.targetPath.isEmpty
        case .attach:
            return !viewModel.pidText.isEmpty
        }
    }

    private var recordDisabled: Bool {
        !isRecording && !canStart
    }

    var body: some View {
        ZStack {
            background
            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    header
                        .modifier(Reveal(appear: appear, delay: 0.0))

                    recordCard
                        .modifier(Reveal(appear: appear, delay: 0.05))

                    targetCard
                        .modifier(Reveal(appear: appear, delay: 0.1))

                    outputCard
                        .modifier(Reveal(appear: appear, delay: 0.15))

                    bundlesCard
                        .modifier(Reveal(appear: appear, delay: 0.2))
                }
                .padding(24)
            }
        }
        .frame(minWidth: 520, minHeight: 720)
        .onAppear {
            withAnimation(.easeOut(duration: 0.45)) {
                appear = true
            }
        }
    }

    private var background: some View {
        ZStack {
            LinearGradient(
                colors: [
                    Color(red: 0.96, green: 0.95, blue: 0.92),
                    Color(red: 0.92, green: 0.95, blue: 0.97)
                ],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )
            Circle()
                .fill(Color(red: 0.87, green: 0.93, blue: 0.91).opacity(0.5))
                .frame(width: 280, height: 280)
                .blur(radius: 30)
                .offset(x: -160, y: -140)
            Circle()
                .fill(Color(red: 0.97, green: 0.9, blue: 0.88).opacity(0.45))
                .frame(width: 220, height: 220)
                .blur(radius: 30)
                .offset(x: 180, y: 120)
        }
        .ignoresSafeArea()
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("ADA Capture")
                .font(titleFont)
            Text("Minimal recorder for sessions and voice.")
                .font(bodyFont)
                .foregroundColor(.secondary)
        }
    }

    private var recordCard: some View {
        SectionCard(title: "Recorder", titleFont: sectionFont) {
            VStack(alignment: .leading, spacing: 12) {
                Button(action: viewModel.toggleRecording) {
                    HStack(spacing: 14) {
                        RecordGlyph(isRecording: isRecording, accent: accent)
                        VStack(alignment: .leading, spacing: 2) {
                            Text(isRecording ? "Recording" : "Record")
                                .font(Font.custom("Avenir Next", size: 16).weight(.semibold))
                            Text(isRecording ? "Tap to stop" : "Tap to start")
                                .font(bodyFont)
                                .foregroundColor(.secondary)
                        }
                    }
                    .padding(.vertical, 10)
                    .padding(.horizontal, 12)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .background(
                        RoundedRectangle(cornerRadius: 14, style: .continuous)
                            .fill(Color.white.opacity(0.75))
                    )
                    .overlay(
                        RoundedRectangle(cornerRadius: 14, style: .continuous)
                            .stroke(Color.black.opacity(0.08), lineWidth: 1)
                    )
                }
                .buttonStyle(.plain)
                .disabled(recordDisabled)

                Text(viewModel.statusMessage)
                    .font(bodyFont)
                    .foregroundColor(.secondary)

                if let trace = viewModel.traceSessionPath {
                    HStack(spacing: 8) {
                        Button("Reveal Trace") {
                            viewModel.revealTrace()
                        }
                        .buttonStyle(PillButtonStyle())
                        Text(trace)
                            .font(Font.custom("Avenir Next", size: 11))
                            .foregroundColor(.secondary)
                            .lineLimit(2)
                    }
                }
            }
        }
    }

    private var targetCard: some View {
        SectionCard(title: "Target", titleFont: sectionFont) {
            VStack(alignment: .leading, spacing: 10) {
                Picker("Mode", selection: $viewModel.mode) {
                    ForEach(SessionMode.allCases) { mode in
                        Text(mode.rawValue).tag(mode)
                    }
                }
                .pickerStyle(.segmented)

                if viewModel.mode == .launch {
                    HStack(spacing: 8) {
                        TextField("/path/to/App.app or binary", text: $viewModel.targetPath)
                            .font(bodyFont)
                            .frame(maxWidth: .infinity)
                            .modifier(FieldStyle())
                        Button("Browse") {
                            viewModel.chooseTarget()
                        }
                        .buttonStyle(PillButtonStyle())
                    }
                    TextField("Args", text: $viewModel.argsText)
                        .font(bodyFont)
                        .modifier(FieldStyle())
                } else {
                    TextField("PID", text: $viewModel.pidText)
                        .font(bodyFont)
                        .modifier(FieldStyle())
                }
            }
        }
    }

    private var outputCard: some View {
        SectionCard(title: "Output", titleFont: sectionFont) {
            HStack(spacing: 8) {
                TextField("/path/to/captures", text: $viewModel.outputPath)
                    .font(bodyFont)
                    .frame(maxWidth: .infinity)
                    .modifier(FieldStyle())
                Button("Browse") {
                    viewModel.chooseOutput()
                }
                .buttonStyle(PillButtonStyle())
            }
        }
    }

    private var bundlesCard: some View {
        SectionCard(title: "Bundles", titleFont: sectionFont) {
            if viewModel.bundles.isEmpty {
                Text("No bundles yet")
                    .font(bodyFont)
                    .foregroundColor(.secondary)
            } else {
                ScrollView {
                    LazyVStack(spacing: 10) {
                        ForEach(viewModel.bundles) { item in
                            BundleRow(item: item) {
                                viewModel.revealBundle(item)
                            }
                        }
                    }
                }
                .frame(height: 180)
            }
        }
    }
}

private struct SectionCard<Content: View>: View {
    let title: String
    let titleFont: Font
    let content: Content

    init(title: String, titleFont: Font, @ViewBuilder content: () -> Content) {
        self.title = title
        self.titleFont = titleFont
        self.content = content()
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(title.uppercased())
                .font(titleFont)
                .foregroundColor(.secondary)
            content
        }
        .padding(16)
        .background(
            RoundedRectangle(cornerRadius: 16, style: .continuous)
                .fill(Color.white.opacity(0.78))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 16, style: .continuous)
                .stroke(Color.black.opacity(0.08), lineWidth: 1)
        )
    }
}

private struct RecordGlyph: View {
    let isRecording: Bool
    let accent: Color
    @State private var pulse = false

    var body: some View {
        ZStack {
            Circle()
                .fill(Color.white.opacity(0.9))
            Circle()
                .stroke(Color.black.opacity(0.1), lineWidth: 1)

            if isRecording {
                Circle()
                    .stroke(accent.opacity(0.4), lineWidth: 2)
                    .scaleEffect(pulse ? 1.35 : 1.0)
                    .opacity(pulse ? 0.0 : 0.6)
                    .animation(.easeOut(duration: 1.2).repeatForever(autoreverses: false), value: pulse)
                    .onAppear {
                        pulse = true
                    }
                    .onDisappear {
                        pulse = false
                    }
            }

            Circle()
                .fill(isRecording ? accent : accent.opacity(0.7))
                .frame(width: isRecording ? 18 : 16, height: isRecording ? 18 : 16)
        }
        .frame(width: 52, height: 52)
    }
}

private struct BundleRow: View {
    let item: BundleItem
    let reveal: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(item.bundlePath)
                .font(Font.custom("Avenir Next", size: 12))
                .lineLimit(2)
            Text("\(item.startedAt.formatted()) -> \(item.endedAt.formatted())")
                .font(Font.custom("Avenir Next", size: 11))
                .foregroundColor(.secondary)
            Button("Reveal") {
                reveal()
            }
            .buttonStyle(PillButtonStyle())
        }
        .padding(10)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .fill(Color.white.opacity(0.65))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .stroke(Color.black.opacity(0.08), lineWidth: 1)
        )
    }
}

private struct FieldStyle: ViewModifier {
    func body(content: Content) -> some View {
        content
            .textFieldStyle(.plain)
            .padding(.vertical, 8)
            .padding(.horizontal, 10)
            .background(
                RoundedRectangle(cornerRadius: 10, style: .continuous)
                    .fill(Color.white.opacity(0.85))
            )
            .overlay(
                RoundedRectangle(cornerRadius: 10, style: .continuous)
                    .stroke(Color.black.opacity(0.08), lineWidth: 1)
            )
    }
}

private struct PillButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(Font.custom("Avenir Next", size: 12).weight(.semibold))
            .padding(.vertical, 6)
            .padding(.horizontal, 10)
            .background(
                RoundedRectangle(cornerRadius: 10, style: .continuous)
                    .fill(Color.white.opacity(configuration.isPressed ? 0.6 : 0.85))
            )
            .overlay(
                RoundedRectangle(cornerRadius: 10, style: .continuous)
                    .stroke(Color.black.opacity(0.08), lineWidth: 1)
            )
    }
}

private struct Reveal: ViewModifier {
    let appear: Bool
    let delay: Double

    func body(content: Content) -> some View {
        content
            .opacity(appear ? 1 : 0)
            .offset(y: appear ? 0 : 8)
            .animation(.easeOut(duration: 0.45).delay(delay), value: appear)
    }
}
