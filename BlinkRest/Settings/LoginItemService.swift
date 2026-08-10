import Combine
import Foundation
import OSLog
import ServiceManagement

enum LoginItemStatus: Equatable, Sendable {
    case disabled
    case enabled
    case requiresApproval

    var isRegistered: Bool {
        self != .disabled
    }
}

@MainActor
protocol LoginItemManaging: AnyObject {
    var status: LoginItemStatus { get }

    func register() throws
    func unregister() throws
    func openSystemSettings()
}

@MainActor
final class SystemLoginItemManager: LoginItemManaging {
    private let service: SMAppService

    init(service: SMAppService = .mainApp) {
        self.service = service
    }

    var status: LoginItemStatus {
        switch service.status {
        case .enabled:
            .enabled
        case .requiresApproval:
            .requiresApproval
        case .notRegistered, .notFound:
            .disabled
        @unknown default:
            .disabled
        }
    }

    func register() throws {
        try service.register()
    }

    func unregister() throws {
        try service.unregister()
    }

    func openSystemSettings() {
        SMAppService.openSystemSettingsLoginItems()
    }
}

@MainActor
final class LoginItemService: ObservableObject {
    @Published private(set) var status: LoginItemStatus
    @Published private(set) var errorMessage: String?

    var isRegistered: Bool {
        status.isRegistered
    }

    private let manager: any LoginItemManaging
    private let logger = Logger(
        subsystem: Bundle.main.bundleIdentifier ?? "com.fynxiu.BlinkRest",
        category: "settings"
    )

    convenience init() {
        self.init(manager: SystemLoginItemManager())
    }

    init(manager: any LoginItemManaging) {
        self.manager = manager
        status = manager.status
    }

    func refresh() {
        status = manager.status
    }

    func setRegistered(_ shouldRegister: Bool) {
        errorMessage = nil
        refresh()

        do {
            switch (shouldRegister, status) {
            case (true, .disabled):
                try manager.register()
            case (false, .enabled), (false, .requiresApproval):
                try manager.unregister()
            case (true, .enabled), (true, .requiresApproval), (false, .disabled):
                break
            }
        } catch {
            let error = error as NSError
            logger.error(
                "Login item update failed, domain=\(error.domain, privacy: .public), code=\(error.code)"
            )
            errorMessage = String(
                localized: "settings.login.error",
                defaultValue: "Blink Rest couldn't update Launch at Login. Check Login Items in System Settings and try again."
            )
        }

        refresh()
    }

    func openSystemSettings() {
        manager.openSystemSettings()
    }

    func clearError() {
        errorMessage = nil
    }
}
