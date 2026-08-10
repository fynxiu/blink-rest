import AppKit
import Foundation
import XCTest
@testable import BlinkRest

@MainActor
final class SettingsStoreTests: XCTestCase {
    func testMissingDefaultsUseAndPersistCanonicalValues() {
        withDefaults { defaults in
            let store = SettingsStore(defaults: defaults)

            XCTAssertEqual(store.workIntervalSeconds, 20 * 60)
            XCTAssertEqual(store.breakDurationSeconds, 20)
            XCTAssertEqual(
                defaults.integer(forKey: SettingsStore.Keys.schemaVersion),
                AppSettings.schemaVersion
            )
            XCTAssertEqual(
                defaults.double(forKey: SettingsStore.Keys.workIntervalSeconds),
                AppSettings.defaultWorkIntervalSeconds
            )
            XCTAssertEqual(
                defaults.double(forKey: SettingsStore.Keys.breakDurationSeconds),
                AppSettings.defaultBreakDurationSeconds
            )
        }
    }

    func testAllowedStoredValuesAreRestored() {
        withDefaults { defaults in
            defaults.set(45 * 60, forKey: SettingsStore.Keys.workIntervalSeconds)
            defaults.set(30, forKey: SettingsStore.Keys.breakDurationSeconds)

            let store = SettingsStore(defaults: defaults)

            XCTAssertEqual(store.workIntervalSeconds, 45 * 60)
            XCTAssertEqual(store.breakDurationSeconds, 30)
        }
    }

    func testInvalidStoredValuesFallBackWithoutNSNumberCoercion() {
        let invalidPairs: [(Any, Any)] = [
            (true, false),
            ("1200", "20"),
            (1200.5, 20.5),
            (Double.nan, Double.infinity),
            (15 * 60, 25),
        ]

        for (workValue, breakValue) in invalidPairs {
            withDefaults { defaults in
                defaults.set(workValue, forKey: SettingsStore.Keys.workIntervalSeconds)
                defaults.set(breakValue, forKey: SettingsStore.Keys.breakDurationSeconds)

                let store = SettingsStore(defaults: defaults)

                XCTAssertEqual(store.workIntervalSeconds, AppSettings.defaultWorkIntervalSeconds)
                XCTAssertEqual(store.breakDurationSeconds, AppSettings.defaultBreakDurationSeconds)
                XCTAssertEqual(
                    defaults.double(forKey: SettingsStore.Keys.workIntervalSeconds),
                    AppSettings.defaultWorkIntervalSeconds
                )
                XCTAssertEqual(
                    defaults.double(forKey: SettingsStore.Keys.breakDurationSeconds),
                    AppSettings.defaultBreakDurationSeconds
                )
            }
        }
    }

    func testWorkAndBreakChangesUseDistinctCallbacks() {
        withDefaults { defaults in
            let store = SettingsStore(defaults: defaults)
            var workChanges: [TimeInterval] = []
            var breakChanges: [TimeInterval] = []
            store.onWorkIntervalChange = { workChanges.append($0) }
            store.onBreakDurationChange = { breakChanges.append($0) }

            store.setWorkIntervalSeconds(30 * 60)

            XCTAssertEqual(workChanges, [30 * 60])
            XCTAssertTrue(breakChanges.isEmpty)

            store.setBreakDurationSeconds(30)

            XCTAssertEqual(workChanges, [30 * 60])
            XCTAssertEqual(breakChanges, [30])
        }
    }

    func testSettingSameValuePersistsWithoutEmittingChange() {
        withDefaults { defaults in
            let store = SettingsStore(defaults: defaults)
            var callbackCount = 0
            store.onWorkIntervalChange = { _ in callbackCount += 1 }
            store.onBreakDurationChange = { _ in callbackCount += 1 }

            store.setWorkIntervalSeconds(AppSettings.defaultWorkIntervalSeconds)
            store.setBreakDurationSeconds(AppSettings.defaultBreakDurationSeconds)

            XCTAssertEqual(callbackCount, 0)
        }
    }

    func testInvalidSetterInputIsCanonicalized() {
        withDefaults { defaults in
            let store = SettingsStore(defaults: defaults)
            store.setWorkIntervalSeconds(30 * 60)
            store.setBreakDurationSeconds(30)

            store.setWorkIntervalSeconds(1)
            store.setBreakDurationSeconds(.nan)

            XCTAssertEqual(store.workIntervalSeconds, AppSettings.defaultWorkIntervalSeconds)
            XCTAssertEqual(store.breakDurationSeconds, AppSettings.defaultBreakDurationSeconds)
        }
    }

    func testPauseUntilRoundTripsAndCanBeCleared() throws {
        try withDefaults { defaults in
            let store = SettingsStore(defaults: defaults)
            let pauseUntil = Date(timeIntervalSince1970: 2_000_000_000.25)

            store.persistedPauseUntil = pauseUntil

            XCTAssertEqual(
                try XCTUnwrap(store.persistedPauseUntil).timeIntervalSince1970,
                pauseUntil.timeIntervalSince1970,
                accuracy: 0.001
            )

            store.persistedPauseUntil = nil

            XCTAssertNil(store.persistedPauseUntil)
            XCTAssertNil(defaults.object(forKey: SettingsStore.Keys.persistedPauseUntil))
        }
    }

    func testInvalidPersistedPauseIsRemoved() {
        withDefaults { defaults in
            defaults.set("not a date", forKey: SettingsStore.Keys.persistedPauseUntil)
            let store = SettingsStore(defaults: defaults)

            XCTAssertNil(store.persistedPauseUntil)
            XCTAssertNil(defaults.object(forKey: SettingsStore.Keys.persistedPauseUntil))
        }
    }

    func testLegacyLaunchAtLoginValueIsRemoved() {
        withDefaults { defaults in
            defaults.set(true, forKey: SettingsStore.Keys.legacyLaunchAtLogin)

            _ = SettingsStore(defaults: defaults)

            XCTAssertNil(defaults.object(forKey: SettingsStore.Keys.legacyLaunchAtLogin))
        }
    }

    func testLoginItemRequiresApprovalIsAnAuthoritativeRegisteredState() {
        let manager = FakeLoginItemManager(status: .requiresApproval)
        let service = LoginItemService(manager: manager)

        XCTAssertEqual(service.status, .requiresApproval)
        XCTAssertTrue(service.isRegistered)
    }

    func testLoginItemRegistrationRefreshesFromManagerStatus() {
        let manager = FakeLoginItemManager(status: .disabled)
        let service = LoginItemService(manager: manager)

        service.setRegistered(true)

        XCTAssertEqual(manager.registerCallCount, 1)
        XCTAssertEqual(service.status, .enabled)
        XCTAssertTrue(service.isRegistered)
        XCTAssertNil(service.errorMessage)
    }

    func testLoginItemFailureRollsBackToManagerStatus() {
        let manager = FakeLoginItemManager(status: .disabled)
        manager.errorToThrow = StubError.operationFailed
        let service = LoginItemService(manager: manager)

        service.setRegistered(true)

        XCTAssertEqual(manager.registerCallCount, 1)
        XCTAssertEqual(service.status, .disabled)
        XCTAssertFalse(service.isRegistered)
        XCTAssertNotNil(service.errorMessage)
    }

    func testLoginItemCanUnregisterWhileAwaitingApproval() {
        let manager = FakeLoginItemManager(status: .requiresApproval)
        let service = LoginItemService(manager: manager)

        service.setRegistered(false)

        XCTAssertEqual(manager.unregisterCallCount, 1)
        XCTAssertEqual(service.status, .disabled)
        XCTAssertFalse(service.isRegistered)
    }

    func testLifecycleObserverEmitsConcreteEventsInOrder() {
        let workspaceCenter = NotificationCenter()
        let applicationCenter = NotificationCenter()
        var events: [WorkspaceLifecycleEvent] = []
        let observer = WorkspaceLifecycleObserver(
            workspaceNotificationCenter: workspaceCenter,
            applicationNotificationCenter: applicationCenter,
            onEvent: { events.append($0) }
        )
        observer.start()

        workspaceCenter.post(name: NSWorkspace.willSleepNotification, object: nil)
        workspaceCenter.post(name: NSWorkspace.screensDidSleepNotification, object: nil)
        workspaceCenter.post(name: NSWorkspace.sessionDidResignActiveNotification, object: nil)
        workspaceCenter.post(name: NSWorkspace.didWakeNotification, object: nil)
        workspaceCenter.post(name: NSWorkspace.screensDidWakeNotification, object: nil)
        workspaceCenter.post(name: NSWorkspace.sessionDidBecomeActiveNotification, object: nil)
        applicationCenter.post(
            name: NSApplication.didChangeScreenParametersNotification,
            object: nil
        )

        XCTAssertEqual(
            events,
            [
                .systemWillSleep,
                .screensDidSleep,
                .sessionDidResignActive,
                .systemDidWake,
                .screensDidWake,
                .sessionDidBecomeActive,
                .displaysChanged,
            ]
        )
    }

    func testLifecycleObserverStartAndStopAreIdempotent() {
        let workspaceCenter = NotificationCenter()
        var events: [WorkspaceLifecycleEvent] = []
        let observer = WorkspaceLifecycleObserver(
            workspaceNotificationCenter: workspaceCenter,
            applicationNotificationCenter: NotificationCenter(),
            onEvent: { events.append($0) }
        )

        observer.start()
        observer.start()
        workspaceCenter.post(name: NSWorkspace.willSleepNotification, object: nil)
        observer.stop()
        observer.stop()
        workspaceCenter.post(name: NSWorkspace.didWakeNotification, object: nil)

        XCTAssertEqual(events, [.systemWillSleep])
    }

    private func withDefaults(
        _ operation: (UserDefaults) throws -> Void
    ) rethrows {
        let suiteName = "SettingsStoreTests.\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: suiteName)!
        defaults.removePersistentDomain(forName: suiteName)
        defer { defaults.removePersistentDomain(forName: suiteName) }
        try operation(defaults)
    }
}

@MainActor
private final class FakeLoginItemManager: LoginItemManaging {
    var status: LoginItemStatus
    var errorToThrow: Error?
    private(set) var registerCallCount = 0
    private(set) var unregisterCallCount = 0
    private(set) var openSettingsCallCount = 0

    init(status: LoginItemStatus) {
        self.status = status
    }

    func register() throws {
        registerCallCount += 1
        if let errorToThrow {
            throw errorToThrow
        }
        status = .enabled
    }

    func unregister() throws {
        unregisterCallCount += 1
        if let errorToThrow {
            throw errorToThrow
        }
        status = .disabled
    }

    func openSystemSettings() {
        openSettingsCallCount += 1
    }
}

private enum StubError: Error {
    case operationFailed
}
