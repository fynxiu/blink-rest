import AppKit
import SwiftUI
import XCTest
@testable import BlinkRest

@MainActor
final class OverlayWindowCoordinatorTests: XCTestCase {
    func testBreakWindowPolicyCoversSpacesFullScreenAndStageManager() {
        let behavior = BreakWindow.requiredCollectionBehavior

        XCTAssertTrue(behavior.contains(.canJoinAllSpaces))
        XCTAssertTrue(behavior.contains(.canJoinAllApplications))
        XCTAssertTrue(behavior.contains(.fullScreenAuxiliary))
        XCTAssertTrue(behavior.contains(.stationary))
        XCTAssertTrue(behavior.contains(.ignoresCycle))
        XCTAssertFalse(behavior.contains(.moveToActiveSpace))
        XCTAssertFalse(behavior.contains(.primary))
        XCTAssertFalse(behavior.contains(.auxiliary))
    }

    func testReconcileReordersExistingWindowAfterSpaceChange() throws {
        let harness = makeHarness(displays: [
            OverlayDisplay(id: 1, frame: NSRect(x: 0, y: 0, width: 1280, height: 800))
        ])
        harness.coordinator.present(session: breakSession)
        let window = try XCTUnwrap(harness.factory.windows[1])
        let previousPresentCount = window.presentCount

        harness.coordinator.reconcileScreens()

        XCTAssertEqual(window.presentCount, previousPresentCount + 1)
    }

    func testDelayedDiagnosticProbeDoesNotReplaceWindow() throws {
        let harness = makeHarness(
            displays: [OverlayDisplay(id: 1, frame: NSRect(x: 0, y: 0, width: 1280, height: 800))],
            diagnosticsEnabled: true
        )
        harness.coordinator.present(session: breakSession)
        let original = try XCTUnwrap(harness.factory.windows[1])
        original.windowServerOnscreen = false
        harness.diagnosticProbeScheduler.fireNext()
        XCTAssertTrue(original === harness.factory.windows[1])
        XCTAssertEqual(harness.factory.makeCount, 1)
        XCTAssertEqual(original.closeCount, 0)
    }

    func testDiagnosticsDisabledDoesNotScheduleWindowServerProbe() {
        let harness = makeHarness(displays: [OverlayDisplay(id: 1, frame: NSRect(x: 0, y: 0, width: 1280, height: 800))])
        harness.coordinator.present(session: breakSession)
        XCTAssertEqual(harness.diagnosticProbeScheduler.pendingCount, 0)
    }

    func testWakeDiscardClosesCachedWindowAndNextBreakCreatesFreshWindow() throws {
        let harness = makeHarness(displays: [
            OverlayDisplay(id: 1, frame: NSRect(x: 0, y: 0, width: 1280, height: 800))
        ])
        harness.coordinator.present(session: breakSession)
        let original = try XCTUnwrap(harness.factory.windows[1])

        harness.coordinator.dismiss()
        harness.coordinator.discardCachedWindowsAfterWake()

        XCTAssertEqual(original.closeCount, 1)
        XCTAssertTrue(harness.coordinator.managedDisplayIDs.isEmpty)

        harness.coordinator.present(session: breakSession)
        let replacement = try XCTUnwrap(harness.factory.windows[1])
        XCTAssertFalse(original === replacement)
        XCTAssertEqual(harness.factory.makeCount, 2)
    }

    func testPresentAndRepeatedPresentReuseOneWindowPerDisplay() {
        let harness = makeHarness(displays: [
            OverlayDisplay(id: 1, frame: NSRect(x: 0, y: 0, width: 1280, height: 800)),
            OverlayDisplay(id: 2, frame: NSRect(x: 1280, y: 0, width: 1440, height: 900)),
        ])

        harness.coordinator.present(session: breakSession)
        harness.coordinator.present(session: breakSession)

        XCTAssertEqual(harness.factory.makeCount, 2)
        XCTAssertEqual(harness.coordinator.managedDisplayIDs, [1, 2])
        XCTAssertTrue(harness.factory.windows.values.allSatisfy(\.isVisible))
        XCTAssertEqual(harness.eventMonitor.installCount, 1)
    }

    func testReconcileAddsRemovesAndUpdatesDisplays() throws {
        let originalFrame = NSRect(x: 0, y: 0, width: 1280, height: 800)
        let harness = makeHarness(displays: [OverlayDisplay(id: 1, frame: originalFrame)])
        harness.coordinator.present(session: breakSession)

        let updatedFrame = NSRect(x: -300, y: 50, width: 1600, height: 1000)
        harness.displayProvider.displays = [
            OverlayDisplay(id: 1, frame: updatedFrame),
            OverlayDisplay(id: 3, frame: NSRect(x: 1300, y: 0, width: 1024, height: 768)),
        ]
        harness.coordinator.reconcileScreens()

        XCTAssertEqual(try XCTUnwrap(harness.factory.windows[1]).frame, updatedFrame)
        XCTAssertNotNil(harness.factory.windows[3])
        XCTAssertEqual(harness.coordinator.managedDisplayIDs, [1, 3])

        let removed = try XCTUnwrap(harness.factory.windows[1])
        harness.displayProvider.displays = [
            OverlayDisplay(id: 3, frame: NSRect(x: 1300, y: 0, width: 1024, height: 768))
        ]
        harness.coordinator.reconcileScreens()

        XCTAssertEqual(removed.closeCount, 1)
        XCTAssertEqual(harness.coordinator.managedDisplayIDs, [3])
    }

    func testDismissIsIdempotentAndStopsConsumingEscape() throws {
        let harness = makeHarness(displays: [
            OverlayDisplay(id: 1, frame: NSRect(x: 0, y: 0, width: 1280, height: 800))
        ])
        harness.coordinator.present(session: breakSession)
        let window = try XCTUnwrap(harness.factory.windows[1])

        harness.coordinator.dismiss()
        harness.coordinator.dismiss()

        XCTAssertEqual(window.dismissCount, 1)
        XCTAssertFalse(window.isVisible)
        XCTAssertEqual(harness.eventMonitor.installCount, 1)
        XCTAssertEqual(harness.eventMonitor.removeCount, 1)
    }

    private var breakSession: BreakSession {
        BreakSession(
            startedAt: .zero,
            endsAt: .zero.advanced(by: 20),
            durationSnapshot: 20
        )
    }

    private func makeHarness(
        displays: [OverlayDisplay],
        diagnosticsEnabled: Bool = false
    ) -> OverlayHarness {
        let displayProvider = FakeOverlayDisplayProvider(displays: displays)
        let factory = FakeBreakWindowFactory()
        let scheduler = FakeOverlayEscapeScheduler()
        let eventMonitor = FakeOverlayEventMonitor()
        let diagnosticProbeScheduler = FakeOverlayDiagnosticProbeScheduler()
        let escapeController = EscapeHoldController(
            holdDuration: EscapeHoldController.defaultHoldDuration,
            scheduler: scheduler,
            eventMonitoring: eventMonitor
        )
        let coordinator = OverlayWindowCoordinator(
            displayProvider: displayProvider,
            windowFactory: factory,
            escapeHoldController: escapeController,
            diagnosticProbeScheduler: diagnosticProbeScheduler,
            diagnosticsEnabled: diagnosticsEnabled
        )
        return OverlayHarness(
            coordinator: coordinator,
            displayProvider: displayProvider,
            factory: factory,
            eventMonitor: eventMonitor,
            diagnosticProbeScheduler: diagnosticProbeScheduler
        )
    }
}

@MainActor
private struct OverlayHarness {
    let coordinator: OverlayWindowCoordinator
    let displayProvider: FakeOverlayDisplayProvider
    let factory: FakeBreakWindowFactory
    let eventMonitor: FakeOverlayEventMonitor
    let diagnosticProbeScheduler: FakeOverlayDiagnosticProbeScheduler
}

@MainActor
private final class FakeOverlayDisplayProvider: OverlayDisplayProviding {
    var displays: [OverlayDisplay]

    init(displays: [OverlayDisplay]) {
        self.displays = displays
    }

    func currentDisplays() -> [OverlayDisplay] {
        displays
    }
}

@MainActor
private final class FakeBreakWindowFactory: BreakWindowMaking {
    private(set) var windows: [UInt32: FakeBreakWindow] = [:]
    private(set) var makeCount = 0

    func makeWindow(
        for display: OverlayDisplay,
        rootView: AnyView
    ) -> any BreakWindowManaging {
        makeCount += 1
        let window = FakeBreakWindow(displayID: display.id, frame: display.frame)
        windows[display.id] = window
        return window
    }
}

@MainActor
private final class FakeBreakWindow: BreakWindowManaging {
    let displayID: UInt32
    private(set) var frame: NSRect
    private(set) var isVisible = false
    private(set) var isKeyWindow = false
    var windowServerOnscreen = true
    private(set) var presentCount = 0
    private(set) var dismissCount = 0
    private(set) var closeCount = 0

    init(displayID: UInt32, frame: NSRect) {
        self.displayID = displayID
        self.frame = frame
    }

    func updateFrame(_ frame: NSRect) {
        self.frame = frame
    }

    func present(makeKey: Bool) {
        presentCount += 1
        isVisible = true
        if makeKey {
            isKeyWindow = true
        }
    }

    func dismiss() {
        guard isVisible else { return }
        dismissCount += 1
        isVisible = false
        isKeyWindow = false
    }

    func closePermanently() {
        closeCount += 1
        isVisible = false
        isKeyWindow = false
    }

    func diagnosticSummary() -> String {
        "display=\(displayID) visible=\(isVisible) key=\(isKeyWindow)"
    }

    func windowServerDiagnosticSummary() -> String {
        "\(diagnosticSummary()) windowServerOnscreen=\(windowServerOnscreen)"
    }
}

@MainActor
private final class FakeOverlayDiagnosticProbeScheduler: OverlayDiagnosticProbeScheduling {
    private var actions: [@MainActor @Sendable () -> Void] = []
    var pendingCount: Int { actions.count }
    func schedule(after delay: TimeInterval, action: @escaping @MainActor @Sendable () -> Void) {
        actions.append(action)
    }
    func fireNext() {
        guard !actions.isEmpty else { return }
        actions.removeFirst()()
    }
}

@MainActor
private final class FakeOverlayCancellation: EscapeHoldCancellation {
    func cancel() {}
}

@MainActor
private struct FakeOverlayEscapeScheduler: EscapeHoldScheduling {
    func schedule(
        after duration: TimeInterval,
        action: @escaping @MainActor @Sendable () -> Void
    ) -> any EscapeHoldCancellation {
        FakeOverlayCancellation()
    }
}

@MainActor
private final class FakeOverlayEventMonitor: EscapeLocalEventMonitoring {
    private(set) var installCount = 0
    private(set) var removeCount = 0

    func install(
        handler: @escaping @MainActor @Sendable (NSEvent) -> Bool
    ) -> Any? {
        installCount += 1
        return NSObject()
    }

    func remove(_ token: Any) {
        removeCount += 1
    }
}
