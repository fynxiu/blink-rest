import AppKit
import OSLog
import SwiftUI

struct OverlayDisplay: Equatable, Hashable, Sendable {
    let id: UInt32
    let frame: CGRect
}

@MainActor
protocol OverlayDisplayProviding: AnyObject {
    func currentDisplays() -> [OverlayDisplay]
}

@MainActor
final class SystemOverlayDisplayProvider: OverlayDisplayProviding {
    func currentDisplays() -> [OverlayDisplay] {
        NSScreen.screens.enumerated().map { index, screen in
            let screenNumber = screen.deviceDescription[
                NSDeviceDescriptionKey("NSScreenNumber")
            ] as? NSNumber

            return OverlayDisplay(
                id: screenNumber?.uint32Value ?? UInt32(index),
                frame: screen.frame
            )
        }
    }
}

@MainActor
protocol BreakWindowMaking: AnyObject {
    func makeWindow(for display: OverlayDisplay, rootView: AnyView) -> any BreakWindowManaging
}

@MainActor
final class SystemBreakWindowFactory: BreakWindowMaking {
    func makeWindow(for display: OverlayDisplay, rootView: AnyView) -> any BreakWindowManaging {
        BreakWindow(
            displayID: display.id,
            frame: display.frame,
            rootView: rootView
        )
    }
}

@MainActor
final class OverlayWindowCoordinator: OverlayPresenting, FrontmostApplicationManaging {
    var onSkipRequested: (() -> Void)?

    private(set) var isPresented = false
    private(set) var managedDisplayIDs: Set<UInt32> = []

    private let displayProvider: any OverlayDisplayProviding
    private let windowFactory: any BreakWindowMaking
    private let escapeHoldController: EscapeHoldController
    private let presentationModel = BreakOverlayPresentationModel()
    private var windows: [UInt32: any BreakWindowManaging] = [:]
    private var displayOrder: [UInt32] = []
    private var activeSession: BreakSession?
    private var previouslyFrontmostApplication: NSRunningApplication?

    private let logger = Logger(
        subsystem: Bundle.main.bundleIdentifier ?? "com.fynxiu.BlinkRest",
        category: "overlay"
    )

    convenience init() {
        self.init(
            displayProvider: SystemOverlayDisplayProvider(),
            windowFactory: SystemBreakWindowFactory(),
            escapeHoldController: EscapeHoldController()
        )
    }

    init(
        displayProvider: any OverlayDisplayProviding,
        windowFactory: any BreakWindowMaking,
        escapeHoldController: EscapeHoldController
    ) {
        self.displayProvider = displayProvider
        self.windowFactory = windowFactory
        self.escapeHoldController = escapeHoldController
    }

    func present(session: BreakSession) {
        if isPresented, activeSession == session {
            reconcileScreens()
            return
        }

        presentationModel.reset()
        activeSession = session
        presentationModel.update(session: session, at: session.startedAt)
        isPresented = true

        reconcileScreens()
        NSApp.activate()

        if let keyWindow = displayOrder.compactMap({ windows[$0] }).first {
            keyWindow.present(makeKey: true)
        }

        escapeHoldController.activate { [weak self] in
            self?.requestSkip()
        }
        presentationModel.announceCurrentStage()
    }

    func update(session: BreakSession, at now: MonotonicInstant) {
        guard isPresented else { return }
        activeSession = session
        presentationModel.update(session: session, at: now)
    }

    func dismiss() {
        guard isPresented || windows.values.contains(where: \.isVisible) else {
            escapeHoldController.deactivate()
            return
        }

        isPresented = false
        activeSession = nil
        escapeHoldController.deactivate()
        windows.values.forEach { $0.dismiss() }
    }

    func reconcileScreens() {
        let displays = displayProvider.currentDisplays()
        let displayIDs = Set(displays.map(\.id))
        displayOrder = displays.map(\.id)

        for removedID in Set(windows.keys).subtracting(displayIDs) {
            windows.removeValue(forKey: removedID)?.closePermanently()
        }

        for display in displays {
            if let window = windows[display.id] {
                window.updateFrame(display.frame)
            } else if isPresented {
                let window = windowFactory.makeWindow(
                    for: display,
                    rootView: makeRootView()
                )
                windows[display.id] = window
            }

            if isPresented {
                windows[display.id]?.present(makeKey: false)
            }
        }

        if isPresented,
           !windows.values.contains(where: \.isKeyWindow),
           let keyWindow = displayOrder.compactMap({ windows[$0] }).first {
            NSApp.activate()
            keyWindow.present(makeKey: true)
        }

        managedDisplayIDs = Set(windows.keys)
        logger.debug("Overlay display set reconciled: \(displayIDs.count, privacy: .public) display(s)")
    }

    func cancelEscapeHold() {
        escapeHoldController.cancelHold()
    }

    func captureFrontmostApplication() {
        let frontmost = NSWorkspace.shared.frontmostApplication
        if frontmost?.processIdentifier == ProcessInfo.processInfo.processIdentifier {
            previouslyFrontmostApplication = nil
        } else {
            previouslyFrontmostApplication = frontmost
        }
    }

    func discardFrontmostApplication() {
        previouslyFrontmostApplication = nil
    }

    func restoreFrontmostApplication() {
        guard let application = previouslyFrontmostApplication else {
            if NSWorkspace.shared.frontmostApplication?.processIdentifier
                == ProcessInfo.processInfo.processIdentifier {
                NSApp.deactivate()
            }
            return
        }
        previouslyFrontmostApplication = nil

        guard NSWorkspace.shared.frontmostApplication?.processIdentifier
            == ProcessInfo.processInfo.processIdentifier else { return }
        guard !application.isTerminated else {
            NSApp.deactivate()
            return
        }
        NSApp.yieldActivation(to: application)
        let didRequestActivation = application.activate(
            from: NSRunningApplication.current,
            options: [.activateAllWindows]
        )
        if !didRequestActivation {
            NSApp.deactivate()
        }
    }

    private func requestSkip() {
        guard isPresented else { return }
        escapeHoldController.cancelHold()
        onSkipRequested?()
    }

    private func makeRootView() -> AnyView {
        AnyView(
            BreakOverlayView(
                presentation: presentationModel,
                escapeHoldController: escapeHoldController,
                onPointerSkip: { [weak self] in
                    self?.requestSkip()
                }
            )
        )
    }

}
