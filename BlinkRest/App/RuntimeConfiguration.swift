import Foundation

struct DebugLaunchOptions: Equatable {
    var resetsDefaults = false
    var isUITesting = false
    var workIntervalOverride: TimeInterval?
    var breakDurationOverride: TimeInterval?
    var warningDurationOverride: TimeInterval?

    static func parse(_ arguments: [String]) -> DebugLaunchOptions {
#if DEBUG
        var result = DebugLaunchOptions()
        var index = 1

        while index < arguments.count {
            let argument = arguments[index]
            switch argument {
            case "--reset-defaults":
                result.resetsDefaults = true
            case "--ui-testing":
                result.isUITesting = true
            case "--work-interval-seconds":
                result.workIntervalOverride = value(after: index, in: arguments)
                index += 1
            case "--break-duration-seconds":
                result.breakDurationOverride = value(after: index, in: arguments)
                index += 1
            case "--warning-seconds":
                result.warningDurationOverride = value(after: index, in: arguments)
                index += 1
            default:
                break
            }
            index += 1
        }

        if result.isUITesting {
            result.workIntervalOverride = result.workIntervalOverride ?? 2
            result.breakDurationOverride = result.breakDurationOverride ?? 10
            result.warningDurationOverride = result.warningDurationOverride ?? 1
        }
        return result
#else
        return DebugLaunchOptions()
#endif
    }

    private static func value(after index: Int, in arguments: [String]) -> TimeInterval? {
        let valueIndex = index + 1
        guard arguments.indices.contains(valueIndex),
              let value = TimeInterval(arguments[valueIndex]),
              value.isFinite,
              value > 0 else {
            return nil
        }
        return value
    }
}

@MainActor
final class RuntimeScheduleProvider: SessionScheduleProviding {
    private let settingsStore: SettingsStore
    private let launchOptions: DebugLaunchOptions

    init(settingsStore: SettingsStore, launchOptions: DebugLaunchOptions) {
        self.settingsStore = settingsStore
        self.launchOptions = launchOptions
    }

    var sessionSchedule: SessionSchedule {
        SessionSchedule(
            workInterval: launchOptions.workIntervalOverride
                ?? settingsStore.workIntervalSeconds,
            breakDuration: launchOptions.breakDurationOverride
                ?? settingsStore.breakDurationSeconds,
            warningDuration: launchOptions.warningDurationOverride ?? 5
        )
    }
}
