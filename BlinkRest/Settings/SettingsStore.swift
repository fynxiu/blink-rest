import Combine
import CoreFoundation
import Foundation
import OSLog

@MainActor
final class SettingsStore: ObservableObject {
    enum Keys {
        static let schemaVersion = "settings.schemaVersion"
        static let workIntervalSeconds = "settings.workIntervalSeconds"
        static let breakDurationSeconds = "settings.breakDurationSeconds"
        static let persistedPauseUntil = "runtime.pauseUntilEpochSeconds"
        static let legacyLaunchAtLogin = "settings.launchAtLogin"
    }

    @Published private(set) var workIntervalSeconds: TimeInterval
    @Published private(set) var breakDurationSeconds: TimeInterval

    var onWorkIntervalChange: (@MainActor (TimeInterval) -> Void)?
    var onBreakDurationChange: (@MainActor (TimeInterval) -> Void)?

    var settings: AppSettings {
        AppSettings(
            workIntervalSeconds: workIntervalSeconds,
            breakDurationSeconds: breakDurationSeconds
        )
    }

    var persistedPauseUntil: Date? {
        get {
            guard let seconds = Self.validEpochSeconds(
                defaults.object(forKey: Keys.persistedPauseUntil)
            ) else {
                defaults.removeObject(forKey: Keys.persistedPauseUntil)
                return nil
            }

            return Date(timeIntervalSince1970: seconds)
        }
        set {
            guard let newValue else {
                defaults.removeObject(forKey: Keys.persistedPauseUntil)
                return
            }

            let seconds = newValue.timeIntervalSince1970
            guard Self.isValidEpochSeconds(seconds) else {
                defaults.removeObject(forKey: Keys.persistedPauseUntil)
                return
            }

            defaults.set(seconds, forKey: Keys.persistedPauseUntil)
        }
    }

    private let defaults: UserDefaults
    private let logger = Logger(
        subsystem: Bundle.main.bundleIdentifier ?? "com.fynxiu.BlinkRest",
        category: "settings"
    )

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults

        let workInterval = Self.validAllowedValue(
            defaults.object(forKey: Keys.workIntervalSeconds),
            allowedValues: AppSettings.allowedWorkIntervalSeconds
        ) ?? AppSettings.defaultWorkIntervalSeconds
        let breakDuration = Self.validAllowedValue(
            defaults.object(forKey: Keys.breakDurationSeconds),
            allowedValues: AppSettings.allowedBreakDurationSeconds
        ) ?? AppSettings.defaultBreakDurationSeconds

        workIntervalSeconds = workInterval
        breakDurationSeconds = breakDuration

        migrateAndCanonicalizeDefaults()
    }

    func setWorkIntervalSeconds(_ value: TimeInterval) {
        let validated = AppSettings(workIntervalSeconds: value).workIntervalSeconds
        defaults.set(validated, forKey: Keys.workIntervalSeconds)

        guard workIntervalSeconds != validated else { return }
        workIntervalSeconds = validated
        onWorkIntervalChange?(validated)
    }

    func setBreakDurationSeconds(_ value: TimeInterval) {
        let validated = AppSettings(breakDurationSeconds: value).breakDurationSeconds
        defaults.set(validated, forKey: Keys.breakDurationSeconds)

        guard breakDurationSeconds != validated else { return }
        breakDurationSeconds = validated
        onBreakDurationChange?(validated)
    }

    private func migrateAndCanonicalizeDefaults() {
        let storedSchemaVersion = Self.validWholeNumber(
            defaults.object(forKey: Keys.schemaVersion)
        )

        if storedSchemaVersion != AppSettings.schemaVersion {
            logger.info("Canonicalizing settings schema")
        }

        defaults.set(AppSettings.schemaVersion, forKey: Keys.schemaVersion)
        defaults.set(workIntervalSeconds, forKey: Keys.workIntervalSeconds)
        defaults.set(breakDurationSeconds, forKey: Keys.breakDurationSeconds)

        // ServiceManagement is the only source of truth for this setting.
        defaults.removeObject(forKey: Keys.legacyLaunchAtLogin)

        if defaults.object(forKey: Keys.persistedPauseUntil) != nil,
           Self.validEpochSeconds(defaults.object(forKey: Keys.persistedPauseUntil)) == nil {
            defaults.removeObject(forKey: Keys.persistedPauseUntil)
        }
    }

    private static func validAllowedValue(
        _ object: Any?,
        allowedValues: [TimeInterval]
    ) -> TimeInterval? {
        guard let number = validNumber(object) else { return nil }
        let value = number.doubleValue
        guard value.isFinite, value.rounded(.towardZero) == value else { return nil }
        return allowedValues.first { $0 == value }
    }

    private static func validWholeNumber(_ object: Any?) -> Int? {
        guard let number = validNumber(object) else { return nil }
        let value = number.doubleValue
        guard value.isFinite, value.rounded(.towardZero) == value else { return nil }
        return Int(exactly: value)
    }

    private static func validEpochSeconds(_ object: Any?) -> TimeInterval? {
        guard let number = validNumber(object) else { return nil }
        let value = number.doubleValue
        return isValidEpochSeconds(value) ? value : nil
    }

    private static func validNumber(_ object: Any?) -> NSNumber? {
        guard let number = object as? NSNumber else { return nil }
        guard CFGetTypeID(number) != CFBooleanGetTypeID() else { return nil }
        return number
    }

    private static func isValidEpochSeconds(_ value: TimeInterval) -> Bool {
        value.isFinite
            && value >= Date.distantPast.timeIntervalSince1970
            && value <= Date.distantFuture.timeIntervalSince1970
    }
}

extension SettingsStore: SessionScheduleProviding {
    var sessionSchedule: SessionSchedule {
        SessionSchedule(
            workInterval: workIntervalSeconds,
            breakDuration: breakDurationSeconds
        )
    }
}

extension SettingsStore: PauseUntilPersisting {}
