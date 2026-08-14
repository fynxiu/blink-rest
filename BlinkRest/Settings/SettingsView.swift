import AppKit
import SwiftUI

struct SettingsView: View {
    @ObservedObject private var settingsStore: SettingsStore
    @ObservedObject private var loginItemService: LoginItemService
    @ObservedObject private var updateChecker: UpdateChecker

    init(
        settingsStore: SettingsStore,
        loginItemService: LoginItemService,
        updateChecker: UpdateChecker
    ) {
        self.settingsStore = settingsStore
        self.loginItemService = loginItemService
        self.updateChecker = updateChecker
    }

    var body: some View {
        Form {
            Section {
                Picker(
                    String(localized: "settings.workInterval", defaultValue: "Work interval"),
                    selection: workIntervalBinding
                ) {
                    ForEach(AppSettings.allowedWorkIntervalSeconds, id: \.self) { seconds in
                        Text(minutesLabel(for: seconds)).tag(seconds)
                    }
                }
                .pickerStyle(.segmented)

                Picker(
                    String(localized: "settings.breakDuration", defaultValue: "Break duration"),
                    selection: breakDurationBinding
                ) {
                    ForEach(AppSettings.allowedBreakDurationSeconds, id: \.self) { seconds in
                        Text(secondsLabel(for: seconds)).tag(seconds)
                    }
                }
                .pickerStyle(.segmented)
            } header: {
                Text(String(localized: "settings.schedule", defaultValue: "Schedule"))
            } footer: {
                Text(
                    String(
                        localized: "settings.schedule.footer",
                        defaultValue: "Work interval changes restart the current cycle. Break duration changes apply to the next break."
                    )
                )
            }

            Section {
                HStack {
                    Text(versionLabel)

                    Spacer()

                    if updateChecker.isChecking {
                        ProgressView()
                            .controlSize(.small)
                    }

                    Button {
                        Task {
                            await checkForUpdatesManually()
                        }
                    } label: {
                        Label(
                            String(
                                localized: "settings.update.check",
                                defaultValue: "Check for Updates..."
                            ),
                            systemImage: "arrow.clockwise"
                        )
                    }
                    .disabled(updateChecker.isChecking)
                }
            } header: {
                Text(String(localized: "settings.update", defaultValue: "Updates"))
            }

            Section {
                Toggle(
                    String(localized: "settings.launchAtLogin", defaultValue: "Launch at login"),
                    isOn: launchAtLoginBinding
                )

                if loginItemService.status == .requiresApproval {
                    VStack(alignment: .leading, spacing: 8) {
                        Text(
                            String(
                                localized: "settings.login.requiresApproval",
                                defaultValue: "Approval is required in System Settings before Blink Rest can launch at login."
                            )
                        )
                        .foregroundStyle(.secondary)

                        Button {
                            loginItemService.openSystemSettings()
                        } label: {
                            Label(
                                String(
                                    localized: "settings.login.open",
                                    defaultValue: "Open Login Items Settings"
                                ),
                                systemImage: "arrow.up.right.square"
                            )
                        }
                    }
                    .font(.callout)
                }

                if let errorMessage = loginItemService.errorMessage {
                    Text(errorMessage)
                        .font(.callout)
                        .foregroundStyle(.red)
                        .accessibilityIdentifier("login-item-error")
                }
            } header: {
                Text(String(localized: "settings.system", defaultValue: "System"))
            } footer: {
                VStack(alignment: .leading, spacing: 4) {
                    Text(
                        String(
                            localized: "settings.privacy",
                            defaultValue: "No analytics. No account. Blink Rest contacts GitHub only to check for updates."
                        )
                    )
                }
            }
        }
        .formStyle(.grouped)
        .frame(
            minWidth: 480,
            idealWidth: 480,
            maxWidth: 560,
            minHeight: 360,
            idealHeight: 400
        )
        .onAppear {
            loginItemService.refresh()
        }
        .onReceive(
            NotificationCenter.default.publisher(for: NSApplication.didBecomeActiveNotification)
        ) { _ in
            loginItemService.refresh()
        }
    }

    private var workIntervalBinding: Binding<TimeInterval> {
        Binding(
            get: { settingsStore.workIntervalSeconds },
            set: { settingsStore.setWorkIntervalSeconds($0) }
        )
    }

    private var breakDurationBinding: Binding<TimeInterval> {
        Binding(
            get: { settingsStore.breakDurationSeconds },
            set: { settingsStore.setBreakDurationSeconds($0) }
        )
    }

    private var launchAtLoginBinding: Binding<Bool> {
        Binding(
            get: { loginItemService.isRegistered },
            set: { loginItemService.setRegistered($0) }
        )
    }

    private var versionLabel: String {
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String
            ?? "1.0.0"
        let format = String(localized: "settings.version.format", defaultValue: "Version %@")
        return String(format: format, locale: Locale.current, version)
    }

    private func updateAvailableLabel(for version: AppVersion) -> String {
        let format = String(
            localized: "settings.update.available.format",
            defaultValue: "Version %@ is available."
        )
        return String(format: format, locale: Locale.current, version.description)
    }

    private func checkForUpdatesManually() async {
        await updateChecker.checkManually()

        switch updateChecker.state {
        case .upToDate:
            presentInformationalAlert(
                title: String(
                    localized: "settings.update.current",
                    defaultValue: "Blink Rest is up to date."
                )
            )
        case let .updateAvailable(release):
            let alert = NSAlert()
            alert.messageText = String(
                localized: "update.alert.title",
                defaultValue: "Blink Rest Update Available"
            )
            alert.informativeText = updateAvailableLabel(for: release.version)
            alert.addButton(withTitle: String(
                localized: "settings.update.view",
                defaultValue: "View Update"
            ))
            alert.addButton(withTitle: String(
                localized: "update.alert.later",
                defaultValue: "Later"
            ))

            NSApp.activate()
            if alert.runModal() == .alertFirstButtonReturn {
                NSWorkspace.shared.open(release.url)
            }
        case .failed:
            presentInformationalAlert(
                title: String(
                    localized: "settings.update.failed",
                    defaultValue: "Could not check for updates."
                ),
                style: .warning
            )
        case .idle, .checking:
            break
        }
    }

    private func presentInformationalAlert(
        title: String,
        style: NSAlert.Style = .informational
    ) {
        let alert = NSAlert()
        alert.alertStyle = style
        alert.messageText = title
        alert.addButton(withTitle: String(localized: "common.ok", defaultValue: "OK"))
        NSApp.activate()
        alert.runModal()
    }

    private func minutesLabel(for seconds: TimeInterval) -> String {
        let format = String(localized: "settings.minutes.format", defaultValue: "%lld min")
        return String(format: format, locale: Locale.current, Int64(seconds / 60))
    }

    private func secondsLabel(for seconds: TimeInterval) -> String {
        let format = String(localized: "settings.seconds.format", defaultValue: "%lld sec")
        return String(format: format, locale: Locale.current, Int64(seconds))
    }
}
