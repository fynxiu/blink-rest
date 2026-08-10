import AppKit
import Combine
import SwiftUI

enum BreakOverlayStage: Equatable, Sendable {
    case lookFar
    case blink
    case closeEyes

    var titleKey: LocalizedStringKey {
        switch self {
        case .lookFar:
            "break.lookFar.title"
        case .blink:
            "break.blink.title"
        case .closeEyes:
            "break.closeEyes.title"
        }
    }

    var detailKey: LocalizedStringKey {
        switch self {
        case .lookFar:
            "break.lookFar.detail"
        case .blink:
            "break.blink.detail"
        case .closeEyes:
            "break.closeEyes.detail"
        }
    }

    var symbolName: String {
        switch self {
        case .lookFar:
            "eye"
        case .blink:
            "eye.fill"
        case .closeEyes:
            "eye.slash"
        }
    }

    var accessibilityIdentifier: String {
        switch self {
        case .lookFar:
            "break-stage-look-far"
        case .blink:
            "break-stage-blink"
        case .closeEyes:
            "break-stage-close-eyes"
        }
    }

    var localizedTitle: String {
        switch self {
        case .lookFar:
            String(localized: "break.lookFar.title")
        case .blink:
            String(localized: "break.blink.title")
        case .closeEyes:
            String(localized: "break.closeEyes.title")
        }
    }
}

@MainActor
final class BreakOverlayPresentationModel: ObservableObject {
    @Published private(set) var stage: BreakOverlayStage = .lookFar
    @Published private(set) var totalProgress: Double = 0
    @Published private(set) var stageProgress: Double = 0
    @Published private(set) var remainingSeconds: Int = 0

    private var hasPresentedSession = false

    func update(session: BreakSession, at instant: MonotonicInstant) {
        let phase = session.phase(at: instant)
        let newStage = BreakOverlayStage(phase.stage)

        if hasPresentedSession, newStage != stage {
            NSHapticFeedbackManager.defaultPerformer.perform(
                .alignment,
                performanceTime: .now
            )
            announce(newStage)
        }

        stage = newStage
        totalProgress = session.progress(at: instant)
        stageProgress = phase.progress
        remainingSeconds = Int(ceil(session.remaining(at: instant)))
        hasPresentedSession = true
    }

    func reset() {
        stage = .lookFar
        totalProgress = 0
        stageProgress = 0
        remainingSeconds = 0
        hasPresentedSession = false
    }

    func announceCurrentStage() {
        announce(stage)
    }

    private func announce(_ stage: BreakOverlayStage) {
        NSAccessibility.post(
            element: NSApp as Any,
            notification: .announcementRequested,
            userInfo: [
                .announcement: stage.localizedTitle,
                .priority: NSAccessibilityPriorityLevel.medium.rawValue,
            ]
        )
    }
}

private extension BreakOverlayStage {
    init(_ stage: BreakStage) {
        switch stage {
        case .lookFar:
            self = .lookFar
        case .blink:
            self = .blink
        case .closeEyes:
            self = .closeEyes
        }
    }
}

struct BreakOverlayView: View {
    @ObservedObject var presentation: BreakOverlayPresentationModel
    @ObservedObject var escapeHoldController: EscapeHoldController

    let onPointerSkip: () -> Void

    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Environment(\.accessibilityDifferentiateWithoutColor) private var differentiateWithoutColor
    @Environment(\.colorSchemeContrast) private var colorSchemeContrast

    var body: some View {
        ZStack(alignment: .topTrailing) {
            DesignTokens.overlayBackground
                .ignoresSafeArea()

            stageContent
                .frame(maxWidth: 620)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .padding(40)

            SkipControls(
                escapeHoldController: escapeHoldController,
                onPointerSkip: onPointerSkip
            )
            .padding(30)
        }
        .foregroundStyle(DesignTokens.overlayPrimaryText)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("break-overlay")
        .animation(
            reduceMotion ? nil : .easeInOut(duration: 0.18),
            value: presentation.stage
        )
    }

    private var stageContent: some View {
        VStack(spacing: DesignTokens.grid * 2) {
            OverlayProgressRing(
                progress: presentation.totalProgress,
                stage: presentation.stage,
                reduceMotion: reduceMotion,
                strongerTrack: differentiateWithoutColor || colorSchemeContrast == .increased
            )
            .padding(.bottom, DesignTokens.grid)

            VStack(spacing: DesignTokens.grid) {
                Text(presentation.stage.titleKey)
                    .font(.system(size: 40, weight: .semibold))
                    .multilineTextAlignment(.center)
                    .lineLimit(2)
                    .minimumScaleFactor(0.8)
                    .foregroundStyle(DesignTokens.overlayPrimaryText)
                    .accessibilityIdentifier(
                        presentation.stage.accessibilityIdentifier
                    )

                Text(presentation.stage.detailKey)
                    .font(.system(size: 16, weight: .regular))
                    .multilineTextAlignment(.center)
                    .foregroundStyle(DesignTokens.overlaySecondaryText)
                    .lineLimit(2)
                    .fixedSize(horizontal: false, vertical: true)
            }
            .id(presentation.stage)
            .transition(.opacity)
        }
        .accessibilityValue(
            String(
                format: String(localized: "break.remaining.format"),
                Int64(presentation.remainingSeconds)
            )
        )
    }
}

private struct OverlayProgressRing: View {
    let progress: Double
    let stage: BreakOverlayStage
    let reduceMotion: Bool
    let strongerTrack: Bool

    private var clampedProgress: Double {
        min(max(progress, 0), 1)
    }

    var body: some View {
        ZStack {
            if stage == .blink {
                BreathingHalo(isPaused: reduceMotion)
            }

            Circle()
                .stroke(
                    strongerTrack ? Color.white.opacity(0.3) : DesignTokens.overlayTrack,
                    lineWidth: 4
                )

            Circle()
                .trim(from: 0, to: clampedProgress)
                .stroke(
                    DesignTokens.overlayAccent,
                    style: StrokeStyle(lineWidth: 4, lineCap: .round)
                )
                .rotationEffect(.degrees(-90))

            Image(systemName: stage.symbolName)
                .font(.system(size: 42, weight: .light))
                .symbolRenderingMode(.monochrome)
                .foregroundStyle(DesignTokens.overlayPrimaryText)
                .contentTransition(.symbolEffect(.replace))
        }
        .frame(width: 188, height: 188)
        .accessibilityHidden(true)
    }
}

private struct BreathingHalo: View {
    let isPaused: Bool

    var body: some View {
        TimelineView(.animation(minimumInterval: 1.0 / 30.0, paused: isPaused)) { context in
            let interval = context.date.timeIntervalSinceReferenceDate
            let wave = (1 - cos(interval * 2 * .pi / 1.8)) / 2
            let scale = isPaused ? 1 : 1 + (0.035 * wave)

            Circle()
                .stroke(DesignTokens.overlayAccent.opacity(0.16), lineWidth: 2)
                .scaleEffect(scale)
        }
    }
}

private struct SkipControls: View {
    @ObservedObject var escapeHoldController: EscapeHoldController
    let onPointerSkip: () -> Void

    var body: some View {
        VStack(alignment: .trailing, spacing: DesignTokens.grid) {
            HStack(spacing: DesignTokens.grid) {
                Text("break.skip.hint")
                    .font(.caption)
                    .foregroundStyle(DesignTokens.overlaySecondaryText)
                    .lineLimit(2)
                    .multilineTextAlignment(.trailing)

                KeyboardHoldProgress(controller: escapeHoldController)
            }

            PointerHoldToSkipButton(
                holdDuration: escapeHoldController.holdDuration,
                action: onPointerSkip
            )
        }
        .frame(maxWidth: 270, alignment: .trailing)
    }
}

private struct KeyboardHoldProgress: View {
    @ObservedObject var controller: EscapeHoldController

    var body: some View {
        TimelineView(
            .animation(minimumInterval: 1.0 / 30.0, paused: !controller.isHolding)
        ) { _ in
            ZStack {
                Circle()
                    .stroke(DesignTokens.overlayTrack, lineWidth: 2)

                Circle()
                    .trim(from: 0, to: controller.progress())
                    .stroke(
                        DesignTokens.overlayAccent,
                        style: StrokeStyle(lineWidth: 2, lineCap: .round)
                    )
                    .rotationEffect(.degrees(-90))

                Image(systemName: "escape")
                    .font(.system(size: 12, weight: .medium))
            }
            .frame(width: 28, height: 28)
        }
        .accessibilityHidden(true)
    }
}

private struct PointerHoldToSkipButton: View {
    let holdDuration: TimeInterval
    let action: () -> Void

    @State private var pressProgress: Double = 0

    var body: some View {
        HStack(spacing: DesignTokens.grid) {
            ZStack {
                Circle()
                    .stroke(DesignTokens.overlayTrack, lineWidth: 2)
                Circle()
                    .trim(from: 0, to: pressProgress)
                    .stroke(
                        DesignTokens.overlayAccent,
                        style: StrokeStyle(lineWidth: 2, lineCap: .round)
                    )
                    .rotationEffect(.degrees(-90))
                Image(systemName: "hand.tap")
                    .font(.system(size: 11, weight: .medium))
            }
            .frame(width: 24, height: 24)

            Text("break.skip.button")
                .font(.caption.weight(.medium))
                .lineLimit(1)
        }
        .padding(.horizontal, 12)
        .frame(height: 36)
        .background(DesignTokens.overlaySecondarySurface)
        .clipShape(RoundedRectangle(cornerRadius: DesignTokens.smallCornerRadius))
        .overlay {
            RoundedRectangle(cornerRadius: DesignTokens.smallCornerRadius)
                .stroke(Color.white.opacity(0.14), lineWidth: 1)
        }
        .contentShape(Rectangle())
        .onLongPressGesture(
            minimumDuration: holdDuration,
            maximumDistance: 20,
            perform: {
                pressProgress = 0
                action()
            },
            onPressingChanged: { isPressing in
                withAnimation(
                    isPressing
                        ? .linear(duration: holdDuration)
                        : .easeOut(duration: 0.12)
                ) {
                    pressProgress = isPressing ? 1 : 0
                }
            }
        )
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(Text("break.skip.button"))
        .accessibilityHint(Text("break.skip.accessibilityHint"))
        .accessibilityAddTraits(.isButton)
        .accessibilityAction {
            action()
        }
        .accessibilityIdentifier("hold-to-skip-button")
    }
}
