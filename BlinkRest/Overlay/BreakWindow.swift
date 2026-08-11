import AppKit
import QuartzCore
import SwiftUI

@MainActor
protocol BreakWindowManaging: AnyObject {
    var displayID: UInt32 { get }
    var frame: NSRect { get }
    var isVisible: Bool { get }
    var isKeyWindow: Bool { get }

    func updateFrame(_ frame: NSRect)
    func present(makeKey: Bool)
    func dismiss()
    func closePermanently()
}

@MainActor
final class BreakWindow: NSWindow, BreakWindowManaging {
    let displayID: UInt32
    static let requiredCollectionBehavior: NSWindow.CollectionBehavior = [
        .canJoinAllSpaces,
        .canJoinAllApplications,
        .fullScreenAuxiliary,
        .stationary,
        .ignoresCycle,
    ]

    private var visibilityGeneration = 0

    init(displayID: UInt32, frame: NSRect, rootView: AnyView) {
        self.displayID = displayID
        super.init(
            contentRect: frame,
            styleMask: [.borderless],
            backing: .buffered,
            defer: false
        )

        level = .screenSaver
        collectionBehavior = Self.requiredCollectionBehavior
        isOpaque = true
        hasShadow = false
        backgroundColor = DesignTokens.overlayBackgroundColor
        isReleasedWhenClosed = false
        animationBehavior = .none
        acceptsMouseMovedEvents = true
        contentView = BreakHostingView(rootView: rootView)
        setFrame(frame, display: false)
    }

    override var canBecomeKey: Bool { true }
    override var canBecomeMain: Bool { true }

    func updateFrame(_ frame: NSRect) {
        guard self.frame != frame else { return }
        setFrame(frame, display: true)
    }

    func present(makeKey: Bool) {
        visibilityGeneration += 1

        let wasVisible = isVisible
        let reduceMotion = NSWorkspace.shared.accessibilityDisplayShouldReduceMotion
        if !wasVisible, !reduceMotion {
            alphaValue = 0
        } else {
            alphaValue = 1
        }

        if makeKey {
            makeKeyAndOrderFront(nil)
        } else {
            orderFrontRegardless()
        }

        guard !wasVisible, !reduceMotion else { return }

        NSAnimationContext.runAnimationGroup { context in
            context.duration = 0.22
            context.timingFunction = CAMediaTimingFunction(name: .easeOut)
            animator().alphaValue = 1
        }
    }

    func dismiss() {
        guard isVisible else {
            alphaValue = 1
            return
        }

        visibilityGeneration += 1
        if NSWorkspace.shared.accessibilityDisplayShouldReduceMotion {
            orderOut(nil)
            alphaValue = 1
            return
        }

        let generation = visibilityGeneration
        NSAnimationContext.runAnimationGroup { context in
            context.duration = 0.18
            context.timingFunction = CAMediaTimingFunction(name: .easeIn)
            animator().alphaValue = 0
        } completionHandler: { [weak self] in
            MainActor.assumeIsolated {
                guard let self, self.visibilityGeneration == generation else { return }
                self.orderOut(nil)
                self.alphaValue = 1
            }
        }
    }

    func closePermanently() {
        visibilityGeneration += 1
        orderOut(nil)
        contentView = nil
        close()
    }

}

@MainActor
private final class BreakHostingView: NSHostingView<AnyView> {
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool {
        true
    }
}
