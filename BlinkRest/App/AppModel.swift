import Combine
import Foundation

@MainActor
final class AppModel: ObservableObject {
    let settingsStore: SettingsStore
    let loginItemService: LoginItemService
    let sessionController: SessionController

    private let scheduleProvider: RuntimeScheduleProvider
    private let overlayCoordinator: OverlayWindowCoordinator
    private let warningCoordinator: WarningPanelCoordinator
    private let lifecycleObserver: WorkspaceLifecycleObserver
    private var cancellables: Set<AnyCancellable> = []

    init(arguments: [String] = ProcessInfo.processInfo.arguments) {
        let options = DebugLaunchOptions.parse(arguments)
        let diagnosticsEnabled = arguments.contains("--diagnostics")
        Self.resetDefaultsIfNeeded(options)

        let settingsStore = SettingsStore()
        let loginItemService = LoginItemService()
        let scheduleProvider = RuntimeScheduleProvider(
            settingsStore: settingsStore,
            launchOptions: options
        )
        let overlayCoordinator = OverlayWindowCoordinator(
            diagnosticsEnabled: diagnosticsEnabled
        )
        let warningCoordinator = WarningPanelCoordinator()
        let lifecycleObserver = WorkspaceLifecycleObserver()
        let controller = SessionController(
            timeSource: SystemTimeSource(),
            scheduleProvider: scheduleProvider,
            pausePersistence: settingsStore,
            overlayPresenter: overlayCoordinator,
            warningPresenter: warningCoordinator,
            frontmostApplicationManager: overlayCoordinator,
            scheduler: RunLoopSessionScheduler(),
            diagnosticsEnabled: diagnosticsEnabled,
            automaticallyStarts: false
        )

        self.settingsStore = settingsStore
        self.loginItemService = loginItemService
        self.scheduleProvider = scheduleProvider
        self.overlayCoordinator = overlayCoordinator
        self.warningCoordinator = warningCoordinator
        self.lifecycleObserver = lifecycleObserver
        sessionController = controller

        overlayCoordinator.onSkipRequested = { [weak controller] in
            controller?.escapeHoldCompleted()
        }
        settingsStore.onWorkIntervalChange = { [weak controller] _ in
            controller?.workIntervalChanged()
        }
        settingsStore.onBreakDurationChange = { [weak controller] _ in
            controller?.breakDurationChanged()
        }
        lifecycleObserver.onEvent = { [weak controller] event in
            controller?.dispatch(event.sessionEvent)
        }

        controller.objectWillChange
            .sink { [weak self] in
                self?.objectWillChange.send()
            }
            .store(in: &cancellables)

        lifecycleObserver.start()
        controller.start()
    }

    var menuPresentation: MenuBarPresentation {
        let remaining = sessionController.remainingSeconds
        switch sessionController.state {
        case .running:
            return MenuBarPresentation(
                phase: .running,
                remainingSeconds: remaining,
                progress: sessionController.workCycleProgress
            )
        case .warning:
            return MenuBarPresentation(
                phase: .warning,
                remainingSeconds: remaining,
                progress: sessionController.workCycleProgress
            )
        case .paused, .suspended:
            return MenuBarPresentation(
                phase: .paused,
                remainingSeconds: remaining,
                progress: 0
            )
        case .breaking:
            let progress = sessionController.currentBreakSession?.progress(
                at: sessionController.currentMonotonicTime
            ) ?? 0
            return MenuBarPresentation(
                phase: .breaking,
                remainingSeconds: remaining,
                progress: progress
            )
        }
    }

    private static func resetDefaultsIfNeeded(_ options: DebugLaunchOptions) {
        guard options.resetsDefaults else { return }
        let defaults = UserDefaults.standard
        defaults.removeObject(forKey: SettingsStore.Keys.schemaVersion)
        defaults.removeObject(forKey: SettingsStore.Keys.workIntervalSeconds)
        defaults.removeObject(forKey: SettingsStore.Keys.breakDurationSeconds)
        defaults.removeObject(forKey: SettingsStore.Keys.persistedPauseUntil)
        defaults.removeObject(forKey: SettingsStore.Keys.legacyLaunchAtLogin)
    }
}
