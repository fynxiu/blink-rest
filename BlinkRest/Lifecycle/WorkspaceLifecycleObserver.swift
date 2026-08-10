import AppKit
import Foundation
import OSLog

enum WorkspaceLifecycleEvent: Equatable, Sendable {
    case systemWillSleep
    case systemDidWake
    case screensDidSleep
    case screensDidWake
    case sessionDidResignActive
    case sessionDidBecomeActive
    case activeSpaceDidChange
    case displaysChanged

    var sessionEvent: SessionEvent {
        switch self {
        case .systemWillSleep:
            .systemWillSleep
        case .systemDidWake:
            .systemDidWake
        case .screensDidSleep:
            .screensDidSleep
        case .screensDidWake:
            .screensDidWake
        case .sessionDidResignActive:
            .sessionDidResignActive
        case .sessionDidBecomeActive:
            .sessionDidBecomeActive
        case .activeSpaceDidChange:
            .activeSpaceDidChange
        case .displaysChanged:
            .displaysChanged
        }
    }
}

@MainActor
final class WorkspaceLifecycleObserver {
    typealias EventHandler = @MainActor (WorkspaceLifecycleEvent) -> Void

    var onEvent: EventHandler?
    private(set) var isStarted = false

    private let workspaceNotificationCenter: NotificationCenter
    private let applicationNotificationCenter: NotificationCenter
    // Registration and mutation are main-actor confined; deinit must still be
    // able to synchronously remove Foundation's non-Sendable observer tokens.
    nonisolated(unsafe) private var observations: [
        (center: NotificationCenter, token: NSObjectProtocol)
    ] = []
    private let logger = Logger(
        subsystem: Bundle.main.bundleIdentifier ?? "com.fynxiu.BlinkRest",
        category: "lifecycle"
    )

    init(
        workspaceNotificationCenter: NotificationCenter = NSWorkspace.shared.notificationCenter,
        applicationNotificationCenter: NotificationCenter = .default,
        onEvent: EventHandler? = nil
    ) {
        self.workspaceNotificationCenter = workspaceNotificationCenter
        self.applicationNotificationCenter = applicationNotificationCenter
        self.onEvent = onEvent
    }

    deinit {
        for observation in observations {
            observation.center.removeObserver(observation.token)
        }
    }

    func start() {
        guard !isStarted else { return }
        isStarted = true

        observe(
            center: workspaceNotificationCenter,
            name: NSWorkspace.willSleepNotification,
            event: .systemWillSleep
        )
        observe(
            center: workspaceNotificationCenter,
            name: NSWorkspace.didWakeNotification,
            event: .systemDidWake
        )
        observe(
            center: workspaceNotificationCenter,
            name: NSWorkspace.screensDidSleepNotification,
            event: .screensDidSleep
        )
        observe(
            center: workspaceNotificationCenter,
            name: NSWorkspace.screensDidWakeNotification,
            event: .screensDidWake
        )
        observe(
            center: workspaceNotificationCenter,
            name: NSWorkspace.sessionDidResignActiveNotification,
            event: .sessionDidResignActive
        )
        observe(
            center: workspaceNotificationCenter,
            name: NSWorkspace.sessionDidBecomeActiveNotification,
            event: .sessionDidBecomeActive
        )
        observe(
            center: workspaceNotificationCenter,
            name: NSWorkspace.activeSpaceDidChangeNotification,
            event: .activeSpaceDidChange
        )
        observe(
            center: applicationNotificationCenter,
            name: NSApplication.didChangeScreenParametersNotification,
            event: .displaysChanged
        )
    }

    func stop() {
        guard isStarted else { return }

        for observation in observations {
            observation.center.removeObserver(observation.token)
        }
        observations.removeAll()
        isStarted = false
    }

    private func observe(
        center: NotificationCenter,
        name: Notification.Name,
        event: WorkspaceLifecycleEvent
    ) {
        let token = center.addObserver(
            forName: name,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            MainActor.assumeIsolated {
                self?.logger.debug(
                    "Received \(String(describing: event), privacy: .public)"
                )
                self?.onEvent?(event)
            }
        }
        observations.append((center, token))
    }
}
