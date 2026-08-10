import AppKit

@MainActor
protocol WarningScreenProviding: AnyObject {
    func targetVisibleFrame() -> NSRect?
}

@MainActor
final class SystemWarningScreenProvider: WarningScreenProviding {
    func targetVisibleFrame() -> NSRect? {
        let mouseLocation = NSEvent.mouseLocation
        let target = NSScreen.screens.first {
            NSMouseInRect(mouseLocation, $0.frame, false)
        } ?? NSScreen.main

        return target?.visibleFrame
    }
}

@MainActor
protocol WarningPanelMaking: AnyObject {
    func makePanel(secondsRemaining: Int) -> any WarningPanelManaging
}

@MainActor
final class SystemWarningPanelFactory: WarningPanelMaking {
    func makePanel(secondsRemaining: Int) -> any WarningPanelManaging {
        WarningPanel(secondsRemaining: secondsRemaining)
    }
}

@MainActor
final class WarningPanelCoordinator: WarningPresenting {
    static let panelSize = NSSize(width: 248, height: 48)

    private let screenProvider: any WarningScreenProviding
    private let panelFactory: any WarningPanelMaking
    private var panel: (any WarningPanelManaging)?

    convenience init() {
        self.init(
            screenProvider: SystemWarningScreenProvider(),
            panelFactory: SystemWarningPanelFactory()
        )
    }

    init(
        screenProvider: any WarningScreenProviding,
        panelFactory: any WarningPanelMaking
    ) {
        self.screenProvider = screenProvider
        self.panelFactory = panelFactory
    }

    func show(secondsRemaining: Int) {
        let seconds = max(0, secondsRemaining)
        if panel == nil {
            panel = panelFactory.makePanel(secondsRemaining: seconds)
        }

        panel?.update(secondsRemaining: seconds)
        repositionPanel()
        panel?.show()
    }

    func hide() {
        panel?.hide()
    }

    private func repositionPanel() {
        guard let visibleFrame = screenProvider.targetVisibleFrame() else { return }

        let origin = NSPoint(
            x: visibleFrame.midX - (Self.panelSize.width / 2),
            y: visibleFrame.maxY - Self.panelSize.height - 24
        )
        panel?.setFrame(NSRect(origin: origin, size: Self.panelSize))
    }
}
