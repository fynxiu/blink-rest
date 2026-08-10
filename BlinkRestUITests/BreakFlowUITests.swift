import XCTest

@MainActor
final class BreakFlowUITests: XCTestCase {
    func testScheduledBreakShowsEveryStageAndDismisses() {
        continueAfterFailure = false

        let app = XCUIApplication()
        app.launchArguments = [
            "--ui-testing",
            "--reset-defaults",
            "--work-interval-seconds", "2",
            "--break-duration-seconds", "10",
            "--warning-seconds", "1"
        ]
        app.launch()

        let overlay = app.descendants(matching: .any)["break-overlay"].firstMatch
        let lookFar = app.descendants(matching: .any)["break-stage-look-far"].firstMatch
        let blink = app.descendants(matching: .any)["break-stage-blink"].firstMatch
        let closeEyes = app.descendants(matching: .any)["break-stage-close-eyes"].firstMatch

        XCTAssertTrue(lookFar.waitForExistence(timeout: 6))
        XCTAssertTrue(overlay.exists)
        XCTAssertTrue(blink.waitForExistence(timeout: 4))
        XCTAssertTrue(closeEyes.waitForExistence(timeout: 7))

        let dismissed = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "exists == false"),
            object: overlay
        )
        XCTAssertEqual(XCTWaiter.wait(for: [dismissed], timeout: 5), .completed)
    }
}
