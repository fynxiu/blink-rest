import AppKit
import CoreGraphics
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
    func diagnosticSummary() -> String
    func windowServerDiagnosticSummary() -> String
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

    func diagnosticSummary() -> String {
        "display=\(displayID) window=\(windowNumber) visible=\(isVisible) key=\(isKeyWindow) main=\(isMainWindow) alpha=\(alphaValue) level=\(level.rawValue) collection=\(collectionBehavior.rawValue) frame=\(NSStringFromRect(frame))"
    }

    func windowServerDiagnosticSummary() -> String {
        let info = windowServerInfo
        let serverOnscreen = (info?[kCGWindowIsOnscreen as String] as? NSNumber)
            .map { String($0.boolValue) } ?? "unknown"
        let serverLayer = (info?[kCGWindowLayer as String] as? NSNumber)
            .map { String($0.intValue) } ?? "unknown"
        let ownerPID = (info?[kCGWindowOwnerPID as String] as? NSNumber)
            .map { String($0.intValue) } ?? "unknown"
        let bounds = info?[kCGWindowBounds as String]
            .map { String(describing: $0) } ?? "unknown"

        return "\(diagnosticSummary()) activeSpace=\(isOnActiveSpace) windowServerOnscreen=\(serverOnscreen) windowServerLayer=\(serverLayer) ownerPID=\(ownerPID) serverBounds=\(bounds) occlusion=\(occlusionState.rawValue)"
    }

    private var windowServerInfo: [String: Any]? {
        guard windowNumber > 0 else { return nil }
        guard let windows = CGWindowListCopyWindowInfo(
            .optionIncludingWindow,
            CGWindowID(windowNumber)
        ) as? [[String: Any]] else {
            return nil
        }

        return windows.first { info in
            (info[kCGWindowNumber as String] as? NSNumber)?.intValue == windowNumber
        }
    }

}

@MainActor
private final class BreakHostingView: NSHostingView<AnyView> {
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool {
        true
    }
}
