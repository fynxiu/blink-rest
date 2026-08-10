import AppKit
import SwiftUI

struct SettingsView: View {
    @ObservedObject private var settingsStore: SettingsStore
    @ObservedObject private var loginItemService: LoginItemService

    init(
        settingsStore: SettingsStore,
        loginItemService: LoginItemService
    ) {
        self.settingsStore = settingsStore
        self.loginItemService = loginItemService
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
                            defaultValue: "No analytics. No account. Blink Rest does not transmit your data."
                        )
                    )
                    Text(versionLabel)
                }
            }
        }
        .formStyle(.grouped)
        .frame(
            minWidth: 480,
            idealWidth: 480,
            maxWidth: 560,
            minHeight: 300,
            idealHeight: 340
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

    private func minutesLabel(for seconds: TimeInterval) -> String {
        let format = String(localized: "settings.minutes.format", defaultValue: "%lld min")
        return String(format: format, locale: Locale.current, Int64(seconds / 60))
    }

    private func secondsLabel(for seconds: TimeInterval) -> String {
        let format = String(localized: "settings.seconds.format", defaultValue: "%lld sec")
        return String(format: format, locale: Locale.current, Int64(seconds))
    }
}
