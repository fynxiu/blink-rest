import AppKit
import OSLog
import SwiftUI

@MainActor
protocol OverlayDiagnosticProbeScheduling {
    func schedule(
        after delay: TimeInterval,
        action: @escaping @MainActor @Sendable () -> Void
    )
}

@MainActor
struct RunLoopOverlayDiagnosticProbeScheduler: OverlayDiagnosticProbeScheduling {
    func schedule(
        after delay: TimeInterval,
        action: @escaping @MainActor @Sendable () -> Void
    ) {
        let timer = Timer(timeInterval: delay, repeats: false) { _ in
            MainActor.assumeIsolated { action() }
        }
        RunLoop.main.add(timer, forMode: .common)
    }
}

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
protocol OverlayApplicationActivating: AnyObject {
    var isActive: Bool { get }
    func activate()
}

@MainActor
final class SystemOverlayApplicationActivator: OverlayApplicationActivating {
    var isActive: Bool { NSApp.isActive }

    func activate() {
        NSApp.activate()
    }
}

@MainActor
final class OverlayWindowCoordinator: OverlayPresenting, FrontmostApplicationManaging {
    var onSkipRequested: (() -> Void)?

    private(set) var isPresented = false
    private(set) var managedDisplayIDs: Set<UInt32> = []

    private let displayProvider: any OverlayDisplayProviding
    private let windowFactory: any BreakWindowMaking
    private let applicationActivator: any OverlayApplicationActivating
    private let escapeHoldController: EscapeHoldController
    private let diagnosticProbeScheduler: any OverlayDiagnosticProbeScheduling
    private let diagnosticsEnabled: Bool
    private let presentationModel = BreakOverlayPresentationModel()
    private var windows: [UInt32: any BreakWindowManaging] = [:]
    private var displayOrder: [UInt32] = []
    private var activeSession: BreakSession?
    private var previouslyFrontmostApplication: NSRunningApplication?
    private var presentationGeneration = 0

    private let logger = Logger(
        subsystem: Bundle.main.bundleIdentifier ?? "com.fynxiu.BlinkRest",
        category: "overlay"
    )

    convenience init(diagnosticsEnabled: Bool = false) {
        self.init(
            displayProvider: SystemOverlayDisplayProvider(),
            windowFactory: SystemBreakWindowFactory(),
            applicationActivator: SystemOverlayApplicationActivator(),
            escapeHoldController: EscapeHoldController(),
            diagnosticProbeScheduler: RunLoopOverlayDiagnosticProbeScheduler(),
            diagnosticsEnabled: diagnosticsEnabled
        )
    }

    init(
        displayProvider: any OverlayDisplayProviding,
        windowFactory: any BreakWindowMaking,
        applicationActivator: any OverlayApplicationActivating = SystemOverlayApplicationActivator(),
        escapeHoldController: EscapeHoldController,
        diagnosticProbeScheduler: any OverlayDiagnosticProbeScheduling = RunLoopOverlayDiagnosticProbeScheduler(),
        diagnosticsEnabled: Bool = false
    ) {
        self.displayProvider = displayProvider
        self.windowFactory = windowFactory
        self.applicationActivator = applicationActivator
        self.escapeHoldController = escapeHoldController
        self.diagnosticProbeScheduler = diagnosticProbeScheduler
        self.diagnosticsEnabled = diagnosticsEnabled
    }

    func present(session: BreakSession) {
        logDiagnostic("present.begin")
        if isPresented, activeSession == session {
            logger.debug("Overlay presentation refreshed for existing break session")
            reconcileScreens()
            logDiagnostic("present.refresh.end")
            return
        }

        presentationModel.reset()
        activeSession = session
        presentationModel.update(session: session, at: session.startedAt)
        isPresented = true
        presentationGeneration += 1

        logger.debug("Overlay presentation started")
        // Activate before the first order-front. On macOS, ordering an accessory-app
        // window while inactive can initially attach it only to the app's old Space
        // even with .canJoinAllSpaces; a later Space change then repairs it.
        applicationActivator.activate()
        logDiagnostic("present.afterActivate")
        reconcileScreens()
        logDiagnostic("present.afterReconcile")

        if let keyWindow = displayOrder.compactMap({ windows[$0] }).first {
            keyWindow.present(makeKey: true)
        }
        logDiagnostic("present.afterKeyWindow")

        escapeHoldController.activate { [weak self] in
            self?.requestSkip()
        }
        presentationModel.announceCurrentStage()
        logDiagnostic("present.end")
    }

    func update(session: BreakSession, at now: MonotonicInstant) {
        guard isPresented else { return }
        activeSession = session
        presentationModel.update(session: session, at: now)
    }

    func dismiss() {
        logDiagnostic("dismiss.begin")
        guard isPresented || windows.values.contains(where: \.isVisible) else {
            escapeHoldController.deactivate()
            logDiagnostic("dismiss.noop")
            return
        }

        isPresented = false
        activeSession = nil
        presentationGeneration += 1
        escapeHoldController.deactivate()
        logger.debug("Overlay presentation dismissed")
        windows.values.forEach { $0.dismiss() }
        logDiagnostic("dismiss.end")
    }

    func discardCachedWindowsAfterWake() {
        logDiagnostic("wake.discardWindows.begin")
        presentationGeneration += 1
        windows.values.forEach { $0.closePermanently() }
        windows.removeAll()
        displayOrder.removeAll()
        managedDisplayIDs.removeAll()
        logDiagnostic("wake.discardWindows.end")
    }

    func reconcileScreens() {
        logDiagnostic("reconcile.begin")
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
            applicationActivator.activate()
            keyWindow.present(makeKey: true)
        }

        managedDisplayIDs = Set(windows.keys)
        let visibleCount = windows.values.filter { $0.isVisible }.count
        let keyCount = windows.values.filter { $0.isKeyWindow }.count
        logger.debug(
            "Overlay reconciled: \(displayIDs.count, privacy: .public) display(s), \(visibleCount, privacy: .public) visible window(s), \(keyCount, privacy: .public) key window(s), appActive=\(NSApp.isActive, privacy: .public)"
        )
        logDiagnostic("reconcile.end")
        scheduleDiagnosticProbes()
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

    private func scheduleDiagnosticProbes() {
        guard diagnosticsEnabled, isPresented else { return }
        let generation = presentationGeneration
        for delay in [0.25, 1.0] {
            diagnosticProbeScheduler.schedule(after: delay) { [weak self] in
                self?.logWindowServerProbe(generation: generation, delay: delay)
            }
        }
    }

    private func logWindowServerProbe(generation: Int, delay: TimeInterval) {
        guard diagnosticsEnabled, isPresented, generation == presentationGeneration else { return }

        let windowState = displayOrder
            .compactMap { windows[$0]?.windowServerDiagnosticSummary() }
            .joined(separator: " | ")
        let currentPID = ProcessInfo.processInfo.processIdentifier
        let frontmostIsBlinkRest = NSWorkspace.shared.frontmostApplication?.processIdentifier == currentPID
        let keyWindowNumber = NSApp.keyWindow?.windowNumber ?? -1
        let mainWindowNumber = NSApp.mainWindow?.windowNumber ?? -1
        let delayMilliseconds = Int((delay * 1_000).rounded())

        logger.notice(
            "DIAGNOSTIC probe.windowServer delayMs=\(delayMilliseconds, privacy: .public) generation=\(generation, privacy: .public) presented=\(self.isPresented, privacy: .public) appActive=\(NSApp.isActive, privacy: .public) frontmostIsBlinkRest=\(frontmostIsBlinkRest, privacy: .public) keyWindow=\(keyWindowNumber, privacy: .public) mainWindow=\(mainWindowNumber, privacy: .public) windows=[\(windowState, privacy: .public)]"
        )
    }

    private func logDiagnostic(_ event: String) {
        guard diagnosticsEnabled else { return }

        let windowState = displayOrder
            .compactMap { windows[$0]?.diagnosticSummary() }
            .joined(separator: " | ")
        let keyWindowNumber = NSApp.keyWindow?.windowNumber ?? -1
        let mainWindowNumber = NSApp.mainWindow?.windowNumber ?? -1
        let activationPolicy = NSApp.activationPolicy().rawValue

        logger.notice(
            "DIAGNOSTIC \(event, privacy: .public) presented=\(self.isPresented, privacy: .public) activeSession=\(self.activeSession != nil, privacy: .public) appActive=\(NSApp.isActive, privacy: .public) activationPolicy=\(activationPolicy, privacy: .public) keyWindow=\(keyWindowNumber, privacy: .public) mainWindow=\(mainWindowNumber, privacy: .public) displays=\(self.displayOrder.count, privacy: .public) windows=[\(windowState, privacy: .public)]"
        )
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
