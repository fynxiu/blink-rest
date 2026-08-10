import AppKit
import XCTest
@testable import BlinkRest

@MainActor
final class EscapeHoldControllerTests: XCTestCase {
    func testBreakBlocksTrackpadGesturesButNotPointerEvents() {
        let blocked: [NSEvent.EventType] = [
            .scrollWheel,
            .beginGesture,
            .endGesture,
            .magnify,
            .swipe,
            .rotate,
            .gesture,
            .smartMagnify,
            .pressure,
        ]

        for type in blocked {
            XCTAssertTrue(
                EscapeHoldController.blocksTrackpadGesture(type),
                "Expected \(type) to be blocked during a break"
            )
        }

        let allowed: [NSEvent.EventType] = [
            .mouseMoved,
            .leftMouseDown,
            .leftMouseDragged,
            .leftMouseUp,
            .rightMouseDown,
            .rightMouseUp,
        ]

        for type in allowed {
            XCTAssertFalse(
                EscapeHoldController.blocksTrackpadGesture(type),
                "Expected \(type) to remain available during a break"
            )
        }
    }

    func testShortPressCancelsWithoutCompleting() {
        let harness = makeHarness()
        var completionCount = 0
        harness.controller.activate { completionCount += 1 }

        harness.controller.handleEscapeKeyDown(isRepeat: false)
        XCTAssertTrue(harness.controller.isHolding)
        harness.controller.handleEscapeKeyUp()

        XCTAssertFalse(harness.controller.isHolding)
        XCTAssertEqual(harness.scheduler.cancellations.first?.cancelCount, 1)
        harness.scheduler.fireLatest()
        XCTAssertEqual(completionCount, 0)
    }

    func testHeldEscapeCompletesExactlyOnce() {
        let harness = makeHarness()
        var completionCount = 0
        harness.controller.activate { completionCount += 1 }

        harness.controller.handleEscapeKeyDown(isRepeat: false)
        harness.scheduler.fireLatest()
        harness.scheduler.fireLatest()

        XCTAssertEqual(completionCount, 1)
        XCTAssertFalse(harness.controller.isHolding)
    }

    func testKeyRepeatAndDuplicateKeyDownDoNotCreateMoreTasks() {
        let harness = makeHarness()
        harness.controller.activate {}

        harness.controller.handleEscapeKeyDown(isRepeat: false)
        harness.controller.handleEscapeKeyDown(isRepeat: true)
        harness.controller.handleEscapeKeyDown(isRepeat: false)

        XCTAssertEqual(harness.scheduler.scheduleCount, 1)
    }

    func testInactiveControllerIgnoresKeys() {
        let harness = makeHarness()

        harness.controller.handleEscapeKeyDown(isRepeat: false)

        XCTAssertFalse(harness.controller.isHolding)
        XCTAssertEqual(harness.scheduler.scheduleCount, 0)
    }

    func testInterruptionCancelsHold() {
        let harness = makeHarness()
        harness.controller.activate {}
        harness.controller.handleEscapeKeyDown(isRepeat: false)

        NotificationCenter.default.post(
            name: NSApplication.didResignActiveNotification,
            object: nil
        )

        XCTAssertFalse(harness.controller.isHolding)
        XCTAssertEqual(harness.scheduler.cancellations.first?.cancelCount, 1)
    }

    func testActivateAndDeactivateManageOneMonitorIdempotently() {
        let harness = makeHarness()
        harness.controller.activate {}
        harness.controller.activate {}

        XCTAssertTrue(harness.controller.isActive)
        XCTAssertEqual(harness.monitor.installCount, 1)

        harness.controller.deactivate()
        harness.controller.deactivate()

        XCTAssertFalse(harness.controller.isActive)
        XCTAssertEqual(harness.monitor.removeCount, 1)
    }

    func testNonEscapeEventPassesThroughButEscapeIsConsumed() throws {
        let harness = makeHarness()
        harness.controller.activate {}

        let otherKey = try XCTUnwrap(
            NSEvent.keyEvent(
                with: .keyDown,
                location: .zero,
                modifierFlags: [],
                timestamp: 0,
                windowNumber: 0,
                context: nil,
                characters: "a",
                charactersIgnoringModifiers: "a",
                isARepeat: false,
                keyCode: 0
            )
        )
        XCTAssertEqual(harness.monitor.handler?(otherKey), false)

        let escape = try XCTUnwrap(
            NSEvent.keyEvent(
                with: .keyDown,
                location: .zero,
                modifierFlags: [],
                timestamp: 0,
                windowNumber: 0,
                context: nil,
                characters: "\u{1b}",
                charactersIgnoringModifiers: "\u{1b}",
                isARepeat: false,
                keyCode: EscapeHoldController.escapeKeyCode
            )
        )
        XCTAssertEqual(harness.monitor.handler?(escape), true)
        XCTAssertTrue(harness.controller.isHolding)
    }

    func testProgressIsClampedAndResetOnRelease() {
        let harness = makeHarness()
        harness.controller.activate {}
        harness.controller.handleEscapeKeyDown(isRepeat: false)

        XCTAssertGreaterThanOrEqual(
            harness.controller.progress(at: ProcessInfo.processInfo.systemUptime + 10),
            0
        )
        XCTAssertLessThanOrEqual(
            harness.controller.progress(at: ProcessInfo.processInfo.systemUptime + 10),
            1
        )

        harness.controller.handleEscapeKeyUp()
        XCTAssertEqual(harness.controller.progress(), 0)
    }

    private func makeHarness() -> EscapeHarness {
        let scheduler = FakeEscapeHoldScheduler()
        let monitor = FakeEscapeEventMonitor()
        let controller = EscapeHoldController(
            holdDuration: 1.5,
            scheduler: scheduler,
            eventMonitoring: monitor
        )
        return EscapeHarness(
            controller: controller,
            scheduler: scheduler,
            monitor: monitor
        )
    }
}

@MainActor
private struct EscapeHarness {
    let controller: EscapeHoldController
    let scheduler: FakeEscapeHoldScheduler
    let monitor: FakeEscapeEventMonitor
}

@MainActor
private final class FakeEscapeCancellation: EscapeHoldCancellation {
    var cancelCount = 0

    func cancel() {
        cancelCount += 1
    }
}

@MainActor
private final class FakeEscapeHoldScheduler: EscapeHoldScheduling {
    var scheduleCount = 0
    var cancellations: [FakeEscapeCancellation] = []
    private var actions: [@MainActor @Sendable () -> Void] = []

    func schedule(
        after duration: TimeInterval,
        action: @escaping @MainActor @Sendable () -> Void
    ) -> any EscapeHoldCancellation {
        XCTAssertEqual(duration, 1.5)
        let cancellation = FakeEscapeCancellation()
        cancellations.append(cancellation)
        actions.append(action)
        scheduleCount += 1
        return cancellation
    }

    func fireLatest() {
        guard let action = actions.last,
              cancellations.last?.cancelCount == 0 else { return }
        action()
    }
}

@MainActor
private final class FakeEscapeEventMonitor: EscapeLocalEventMonitoring {
    var installCount = 0
    var removeCount = 0
    var handler: (@MainActor (NSEvent) -> Bool)?
    private let token = NSObject()

    func install(
        handler: @escaping @MainActor (NSEvent) -> Bool
    ) -> Any? {
        installCount += 1
        self.handler = handler
        return token
    }

    func remove(_ token: Any) {
        removeCount += 1
        handler = nil
    }
}
