import AppKit
import SwiftUI

enum DesignTokens {
    static let grid: CGFloat = 8

    static let popoverWidth: CGFloat = 304
    static let popoverHorizontalPadding: CGFloat = 20
    static let popoverVerticalPadding: CGFloat = 18

    static let standardCornerRadius: CGFloat = 14
    static let smallCornerRadius: CGFloat = 10
    static let warningCornerRadius: CGFloat = 16

    static let overlayBackground = Color(
        red: 14.0 / 255.0,
        green: 16.0 / 255.0,
        blue: 19.0 / 255.0
    )
    static let overlaySecondarySurface = Color(
        red: 23.0 / 255.0,
        green: 26.0 / 255.0,
        blue: 32.0 / 255.0
    )
    static let overlayPrimaryText = Color(
        red: 245.0 / 255.0,
        green: 247.0 / 255.0,
        blue: 250.0 / 255.0
    )
    static let overlaySecondaryText = Color(
        red: 166.0 / 255.0,
        green: 175.0 / 255.0,
        blue: 188.0 / 255.0
    )
    static let overlayAccent = Color(
        red: 119.0 / 255.0,
        green: 169.0 / 255.0,
        blue: 1.0
    )
    static let overlayTrack = Color.white.opacity(0.12)

    static let overlayBackgroundColor = NSColor(
        srgbRed: 14.0 / 255.0,
        green: 16.0 / 255.0,
        blue: 19.0 / 255.0,
        alpha: 1
    )
}
