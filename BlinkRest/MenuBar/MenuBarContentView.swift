import AppKit
import SwiftUI

enum MenuBarPhase: Equatable, Sendable {
    case running
    case warning
    case paused
    case breaking
}

struct MenuBarPresentation: Equatable, Sendable {
    var phase: MenuBarPhase
    var remainingSeconds: Int
    var progress: Double

    init(phase: MenuBarPhase, remainingSeconds: Int, progress: Double) {
        self.phase = phase
        self.remainingSeconds = max(0, remainingSeconds)
        self.progress = min(max(progress, 0), 1)
    }

    var symbolName: String {
        switch phase {
        case .running:
            "eye"
        case .warning:
            "eye.fill"
        case .paused:
            "eye.slash"
        case .breaking:
            "eye.circle.fill"
        }
    }

    var accessibilityLabel: String {
        switch phase {
        case .running:
            return String(
                format: String(localized: "menu.accessibility.running"),
                Self.spokenDuration(remainingSeconds)
            )
        case .warning:
            return String(
                format: String(localized: "menu.accessibility.warning"),
                Int64(remainingSeconds)
            )
        case .paused:
            return String(
                format: String(localized: "menu.accessibility.paused"),
                Self.spokenDuration(remainingSeconds)
            )
        case .breaking:
            return String(localized: "menu.accessibility.breaking")
        }
    }

    var countdownText: String {
        let minutes = remainingSeconds / 60
        let seconds = remainingSeconds % 60
        return String(format: "%02d:%02d", minutes, seconds)
    }

    private static func spokenDuration(_ seconds: Int) -> String {
        let formatter = DateComponentsFormatter()
        formatter.allowedUnits = seconds >= 60 ? [.hour, .minute, .second] : [.second]
        formatter.unitsStyle = .full
        formatter.maximumUnitCount = 2
        return formatter.string(from: TimeInterval(seconds)) ?? "\(seconds) seconds"
    }
}

struct MenuBarContentView: View {
    let presentation: MenuBarPresentation
    let onTakeBreakNow: () -> Void
    let onPause: (_ duration: TimeInterval) -> Void
    let onResume: () -> Void
    let onOpenSettings: () -> Void
    let onQuit: () -> Void

    var body: some View {
        VStack(spacing: 0) {
            header

            countdown
                .padding(.top, DesignTokens.grid * 2)
                .padding(.bottom, DesignTokens.grid * 2.25)

            controls

            Divider()
                .padding(.vertical, DesignTokens.grid * 1.5)

            footer
        }
        .padding(.horizontal, DesignTokens.popoverHorizontalPadding)
        .padding(.vertical, DesignTokens.popoverVerticalPadding)
        .frame(width: DesignTokens.popoverWidth)
    }

    private var header: some View {
        HStack(spacing: DesignTokens.grid) {
            Text("app.name")
                .font(.headline)

            Spacer(minLength: DesignTokens.grid)

            Text(statusKey)
                .font(.caption.weight(.medium))
                .foregroundStyle(statusColor)
        }
    }

    private var countdown: some View {
        ZStack {
            ProgressRing(
                progress: presentation.progress,
                tint: presentation.phase == .paused ? .secondary : .accentColor
            )

            VStack(spacing: 2) {
                Text(presentation.countdownText)
                    .font(.system(size: 31, weight: .semibold, design: .rounded))
                    .monospacedDigit()
                    .lineLimit(1)

                Text(countdownCaptionKey)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(presentation.accessibilityLabel)
    }

    @ViewBuilder
    private var controls: some View {
        VStack(spacing: DesignTokens.grid) {
            if presentation.phase == .paused {
                Button(action: onResume) {
                    Text("menu.resume")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.large)
                .accessibilityIdentifier("resume-button")
            } else {
                Button(action: onTakeBreakNow) {
                    Text(primaryActionKey)
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.large)
                .disabled(presentation.phase == .breaking)
                .accessibilityIdentifier("take-break-button")

                Menu {
                    Button("menu.pause30") {
                        onPause(30 * 60)
                    }

                    Button("menu.pause60") {
                        onPause(60 * 60)
                    }
                } label: {
                    HStack {
                        Text("menu.pause")
                        Spacer()
                    }
                    .frame(maxWidth: .infinity)
                }
                .menuStyle(.borderlessButton)
                .fixedSize(horizontal: false, vertical: true)
                .disabled(presentation.phase == .breaking)
                .accessibilityIdentifier("pause-menu")
            }
        }
    }

    private var footer: some View {
        HStack(spacing: DesignTokens.grid) {
            Button("menu.settings", action: onOpenSettings)
                .buttonStyle(.plain)
                .keyboardShortcut(",", modifiers: .command)

            Spacer()

            Button("menu.quit", action: onQuit)
                .buttonStyle(.plain)
                .keyboardShortcut("q", modifiers: .command)
        }
        .font(.callout)
    }

    private var statusKey: LocalizedStringKey {
        switch presentation.phase {
        case .running, .warning:
            "menu.active"
        case .paused:
            "menu.paused"
        case .breaking:
            "menu.breaking"
        }
    }

    private var primaryActionKey: LocalizedStringKey {
        presentation.phase == .breaking ? "menu.breaking" : "menu.takeBreakNow"
    }

    private var statusColor: Color {
        switch presentation.phase {
        case .running:
            .green
        case .warning:
            .orange
        case .paused:
            .secondary
        case .breaking:
            .accentColor
        }
    }

    private var countdownCaptionKey: LocalizedStringKey {
        switch presentation.phase {
        case .running:
            "menu.untilNextBreak"
        case .warning:
            "menu.breakIn"
        case .paused:
            "menu.untilResume"
        case .breaking:
            "menu.breaking"
        }
    }
}
