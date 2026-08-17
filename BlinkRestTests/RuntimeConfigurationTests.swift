import Foundation
import XCTest
@testable import BlinkRest

final class RuntimeConfigurationTests: XCTestCase {
    func testUITestingUsesSupportedFastDefaults() {
        let options = DebugLaunchOptions.parse(["BlinkRest", "--ui-testing"])

        XCTAssertTrue(options.isUITesting)
        XCTAssertEqual(options.workIntervalOverride, 2)
        XCTAssertEqual(options.breakDurationOverride, 10)
        XCTAssertEqual(options.warningDurationOverride, 1)
    }

    func testExplicitValuesOverrideUITestDefaults() {
        let options = DebugLaunchOptions.parse([
            "BlinkRest",
            "--ui-testing",
            "--work-interval-seconds", "3",
            "--break-duration-seconds", "30",
            "--warning-seconds", "2"
        ])

        XCTAssertEqual(options.workIntervalOverride, 3)
        XCTAssertEqual(options.breakDurationOverride, 30)
        XCTAssertEqual(options.warningDurationOverride, 2)
    }

    func testInvalidOverridesAreIgnored() {
        let options = DebugLaunchOptions.parse([
            "BlinkRest",
            "--work-interval-seconds", "nope",
            "--break-duration-seconds", "-1"
        ])

        XCTAssertNil(options.workIntervalOverride)
        XCTAssertNil(options.breakDurationOverride)
    }

    func testUnitTestRuntimeIsDetectedFromXCTestEnvironment() {
        XCTAssertTrue(AppModel.isRunningUnitTests(environment: [
            "XCTestConfigurationFilePath": "/tmp/BlinkRest.xctestconfiguration"
        ]))
        XCTAssertTrue(AppModel.isRunningUnitTests(environment: [
            "XCInjectBundleInto": "/tmp/BlinkRest.app/Contents/MacOS/BlinkRest"
        ]))
        XCTAssertFalse(AppModel.isRunningUnitTests(environment: [:]))
    }
}
