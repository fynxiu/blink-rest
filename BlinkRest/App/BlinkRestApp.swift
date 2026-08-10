import AppKit
import SwiftUI

@main
struct BlinkRestApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @StateObject private var model = AppModel()

    var body: some Scene {
        MenuBarExtra {
            BlinkRestMenuRoot(model: model)
        } label: {
            let presentation = model.menuPresentation
            Image(systemName: presentation.symbolName)
                .symbolRenderingMode(.monochrome)
                .accessibilityLabel(presentation.accessibilityLabel)
        }
        .menuBarExtraStyle(.window)

        Settings {
            SettingsView(
                settingsStore: model.settingsStore,
                loginItemService: model.loginItemService
            )
        }
        .defaultSize(width: 500, height: 340)
    }
}

private struct BlinkRestMenuRoot: View {
    @ObservedObject var model: AppModel
    @Environment(\.openSettings) private var openSettings

    var body: some View {
        MenuBarContentView(
            presentation: model.menuPresentation,
            onTakeBreakNow: model.sessionController.takeBreakNow,
            onPause: model.sessionController.pause,
            onResume: model.sessionController.resume,
            onOpenSettings: showSettings,
            onQuit: { NSApp.terminate(nil) }
        )
    }

    private func showSettings() {
        NSApp.activate()
        openSettings()
    }
}
