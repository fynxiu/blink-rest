import AppKit
import Combine
import Foundation

@MainActor
protocol EscapeHoldCancellation: AnyObject {
    func cancel()
}

@MainActor
protocol EscapeHoldScheduling {
    func schedule(
        after duration: TimeInterval,
        action: @escaping @MainActor @Sendable () -> Void
    ) -> any EscapeHoldCancellation
}

@MainActor
protocol EscapeLocalEventMonitoring {
    func install(
        handler: @escaping @MainActor (NSEvent) -> Bool
    ) -> Any?

    func remove(_ token: Any)
}

@MainActor
private final class RunLoopTimerCancellation: EscapeHoldCancellation {
    private var timer: Timer?

    init(timer: Timer) {
        self.timer = timer
    }

    func cancel() {
        timer?.invalidate()
        timer = nil
    }
}

@MainActor
struct RunLoopEscapeHoldScheduler: EscapeHoldScheduling {
    func schedule(
        after duration: TimeInterval,
        action: @escaping @MainActor @Sendable () -> Void
    ) -> any EscapeHoldCancellation {
        let timer = Timer(timeInterval: duration, repeats: false) { _ in
            Task { @MainActor in
                action()
            }
        }
        RunLoop.main.add(timer, forMode: .common)
        return RunLoopTimerCancellation(timer: timer)
    }
}

@MainActor
struct AppKitEscapeLocalEventMonitor: EscapeLocalEventMonitoring {
    func install(
        handler: @escaping @MainActor (NSEvent) -> Bool
    ) -> Any? {
        NSEvent.addLocalMonitorForEvents(
            matching: [
                .keyDown,
                .keyUp,
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
        ) { event in
            let eventBox = UncheckedEvent(event)
            let shouldConsume = MainActor.assumeIsolated {
                handler(eventBox.value)
            }
            return shouldConsume ? nil : event
        }
    }

    func remove(_ token: Any) {
        NSEvent.removeMonitor(token)
    }
}

private struct UncheckedEvent: @unchecked Sendable {
    let value: NSEvent

    init(_ value: NSEvent) {
        self.value = value
    }
}

@MainActor
final class EscapeHoldController: NSObject, ObservableObject {
    nonisolated static let defaultHoldDuration: TimeInterval = 1.5
    nonisolated static let escapeKeyCode: UInt16 = 53

    @Published private(set) var isHolding = false
    @Published private(set) var isActive = false

    let holdDuration: TimeInterval

    private let scheduler: any EscapeHoldScheduling
    private let eventMonitoring: any EscapeLocalEventMonitoring
    private var eventMonitorToken: Any?
    private var holdCancellation: (any EscapeHoldCancellation)?
    private var holdStartedAt: TimeInterval?
    private var completion: (() -> Void)?

    override convenience init() {
        self.init(holdDuration: Self.defaultHoldDuration)
    }

    convenience init(holdDuration: TimeInterval) {
        self.init(
            holdDuration: holdDuration,
            scheduler: RunLoopEscapeHoldScheduler(),
            eventMonitoring: AppKitEscapeLocalEventMonitor()
        )
    }

    init(
        holdDuration: TimeInterval,
        scheduler: any EscapeHoldScheduling,
        eventMonitoring: any EscapeLocalEventMonitoring
    ) {
        self.holdDuration = max(0.1, holdDuration)
        self.scheduler = scheduler
        self.eventMonitoring = eventMonitoring
        super.init()
    }

    func activate(onCompleted: @escaping () -> Void) {
        completion = onCompleted

        guard !isActive else { return }
        isActive = true

        eventMonitorToken = eventMonitoring.install { [weak self] event in
            if Self.blocksTrackpadGesture(event.type) {
                return true
            }

            guard event.keyCode == Self.escapeKeyCode else { return false }

            switch event.type {
            case .keyDown:
                self?.handleEscapeKeyDown(isRepeat: event.isARepeat)
            case .keyUp:
                self?.handleEscapeKeyUp()
            default:
                break
            }

            return true
        }

        NotificationCenter.default.addObserver(
            self,
            selector: #selector(cancelForInterruption),
            name: NSApplication.didResignActiveNotification,
            object: nil
        )
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(cancelForInterruption),
            name: NSWindow.didResignKeyNotification,
            object: nil
        )
    }

    nonisolated static func blocksTrackpadGesture(_ type: NSEvent.EventType) -> Bool {
        switch type {
        case .scrollWheel,
             .beginGesture,
             .endGesture,
             .magnify,
             .swipe,
             .rotate,
             .gesture,
             .smartMagnify,
             .pressure:
            true
        default:
            false
        }
    }

    func deactivate() {
        guard isActive else {
            completion = nil
            cancelHold()
            return
        }

        cancelHold()
        if let eventMonitorToken {
            eventMonitoring.remove(eventMonitorToken)
            self.eventMonitorToken = nil
        }
        NotificationCenter.default.removeObserver(self)
        completion = nil
        isActive = false
    }

    func handleEscapeKeyDown(isRepeat: Bool) {
        guard isActive, !isRepeat, !isHolding else { return }

        holdStartedAt = ProcessInfo.processInfo.systemUptime
        isHolding = true
        holdCancellation = scheduler.schedule(after: holdDuration) { [weak self] in
            self?.completeHold()
        }
    }

    func handleEscapeKeyUp() {
        cancelHold()
    }

    func cancelHold() {
        holdCancellation?.cancel()
        holdCancellation = nil
        holdStartedAt = nil
        isHolding = false
    }

    func progress(at uptime: TimeInterval = ProcessInfo.processInfo.systemUptime) -> Double {
        guard isHolding, let holdStartedAt else { return 0 }
        return min(max((uptime - holdStartedAt) / holdDuration, 0), 1)
    }

    @objc
    private func cancelForInterruption() {
        cancelHold()
    }

    private func completeHold() {
        guard isActive, isHolding else { return }

        holdCancellation = nil
        holdStartedAt = nil
        isHolding = false
        let completion = self.completion
        completion?()
    }
}
