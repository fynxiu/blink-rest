import AppKit
import SwiftUI

@MainActor
protocol WarningPanelManaging: AnyObject {
    var isVisible: Bool { get }

    func update(secondsRemaining: Int)
    func setFrame(_ frame: NSRect)
    func show()
    func hide()
}

@MainActor
final class WarningPanel: NSPanel, WarningPanelManaging {
    private let hostingView: NSHostingView<AnyView>

    init(secondsRemaining: Int) {
        hostingView = NSHostingView(
            rootView: AnyView(
                WarningPillView(secondsRemaining: secondsRemaining)
            )
        )

        super.init(
            contentRect: NSRect(origin: .zero, size: WarningPanelCoordinator.panelSize),
            styleMask: [.borderless, .nonactivatingPanel],
            backing: .buffered,
            defer: false
        )

        level = .statusBar
        collectionBehavior = [
            .canJoinAllSpaces,
            .fullScreenAuxiliary,
            .stationary,
            .transient,
            .ignoresCycle
        ]
        isFloatingPanel = true
        becomesKeyOnlyIfNeeded = true
        hidesOnDeactivate = false
        ignoresMouseEvents = true
        isOpaque = false
        hasShadow = true
        backgroundColor = .clear
        isReleasedWhenClosed = false
        animationBehavior = .none
        contentView = hostingView
    }

    override var canBecomeKey: Bool { false }
    override var canBecomeMain: Bool { false }

    func update(secondsRemaining: Int) {
        hostingView.rootView = AnyView(
            WarningPillView(secondsRemaining: secondsRemaining)
        )
    }

    func setFrame(_ frame: NSRect) {
        setFrame(frame, display: true)
    }

    func show() {
        orderFrontRegardless()
    }

    func hide() {
        orderOut(nil)
    }
}

private struct WarningPillView: View {
    let secondsRemaining: Int

    @Environment(\.accessibilityReduceTransparency) private var reduceTransparency
    @Environment(\.colorSchemeContrast) private var colorSchemeContrast

    var body: some View {
        HStack(spacing: DesignTokens.grid) {
            Image(systemName: "eye.fill")
                .symbolRenderingMode(.monochrome)
                .foregroundStyle(.secondary)

            Text(
                String(
                    format: String(localized: "warning.eyeBreakIn.format"),
                    Int64(max(0, secondsRemaining))
                )
            )
            .font(.system(size: 14, weight: .medium))
            .monospacedDigit()
            .lineLimit(1)
        }
        .padding(.horizontal, 16)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(backgroundStyle)
        .clipShape(RoundedRectangle(cornerRadius: DesignTokens.warningCornerRadius))
        .overlay {
            RoundedRectangle(cornerRadius: DesignTokens.warningCornerRadius)
                .strokeBorder(
                    Color(nsColor: .separatorColor).opacity(
                        colorSchemeContrast == .increased ? 0.8 : 0.45
                    ),
                    lineWidth: 1
                )
        }
        .accessibilityElement(children: .combine)
        .accessibilityIdentifier("break-warning")
    }

    private var backgroundStyle: AnyShapeStyle {
        if reduceTransparency {
            AnyShapeStyle(Color(nsColor: .windowBackgroundColor))
        } else {
            AnyShapeStyle(.regularMaterial)
        }
    }
}
