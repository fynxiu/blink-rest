import Foundation
import XCTest
@testable import BlinkRest

@MainActor
final class UpdateCheckerTests: XCTestCase {
    func testVersionComparisonUsesNumericSemanticComponents() throws {
        XCTAssertLessThan(try XCTUnwrap(AppVersion("1.9.0")), try XCTUnwrap(AppVersion("1.10.0")))
        XCTAssertLessThan(try XCTUnwrap(AppVersion("1.10.0")), try XCTUnwrap(AppVersion("2.0.0")))
        XCTAssertEqual(AppVersion("v1.2.3"), AppVersion("1.2.3"))
        XCTAssertNil(AppVersion("1.2"))
        XCTAssertNil(AppVersion("1.2.beta"))
    }

    func testAutomaticCheckFindsNewReleaseAndPromptsOnlyOncePerVersion() async throws {
        try await withDefaults { defaults in
            var fetchCount = 0
            let checker = UpdateChecker(
                defaults: defaults,
                currentVersionString: "1.0.0",
                compatibleAssetSuffix: "-macos-arm64.zip",
                fetcher: { _ in
                    fetchCount += 1
                    return try Self.releaseResponse(releases: [
                        ("v1.1.0", ["BlinkRest-v1.1.0-macos-arm64.zip"])
                    ])
                }
            )
            var promptedVersions: [AppVersion] = []
            checker.onAutomaticUpdateAvailable = { release in
                promptedVersions.append(release.version)
            }

            let firstCheck = Date(timeIntervalSince1970: 2_000_000_000)
            await checker.checkAutomatically(now: firstCheck)

            guard case let .updateAvailable(release) = checker.state else {
                return XCTFail("Expected updateAvailable state")
            }
            XCTAssertEqual(release.version, AppVersion("1.1.0"))
            XCTAssertEqual(promptedVersions, [AppVersion("1.1.0")!])

            await checker.checkAutomatically(
                now: firstCheck.addingTimeInterval(UpdateChecker.automaticCheckInterval + 1)
            )
            XCTAssertEqual(fetchCount, 2)
            XCTAssertEqual(promptedVersions, [AppVersion("1.1.0")!])
        }
    }

    func testAutomaticCheckIsThrottledForTwentyFourHours() async throws {
        try await withDefaults { defaults in
            let now = Date(timeIntervalSince1970: 2_000_000_000)
            defaults.set(now.timeIntervalSince1970, forKey: UpdateChecker.Keys.lastAutomaticCheckAt)
            var fetchCount = 0
            let checker = UpdateChecker(
                defaults: defaults,
                currentVersionString: "1.0.0",
                compatibleAssetSuffix: "-macos-arm64.zip",
                fetcher: { _ in
                    fetchCount += 1
                    return try Self.releaseResponse(releases: [
                        ("v1.1.0", ["BlinkRest-v1.1.0-macos-arm64.zip"])
                    ])
                }
            )

            await checker.checkAutomatically(now: now.addingTimeInterval(60 * 60))

            XCTAssertEqual(fetchCount, 0)
            XCTAssertEqual(checker.state, .idle)
        }
    }

    func testManualCheckBypassesAutomaticThrottle() async throws {
        try await withDefaults { defaults in
            defaults.set(2_000_000_000.0, forKey: UpdateChecker.Keys.lastAutomaticCheckAt)
            var fetchCount = 0
            let checker = UpdateChecker(
                defaults: defaults,
                currentVersionString: "1.0.0",
                compatibleAssetSuffix: "-macos-arm64.zip",
                fetcher: { _ in
                    fetchCount += 1
                    return try Self.releaseResponse(releases: [
                        ("v1.0.0", ["BlinkRest-v1.0.0-macos-arm64.zip"])
                    ])
                }
            )

            await checker.checkManually()

            XCTAssertEqual(fetchCount, 1)
            XCTAssertEqual(checker.state, .upToDate)
        }
    }

    func testInvalidReleaseResponseProducesFailureState() async throws {
        try await withDefaults { defaults in
            let checker = UpdateChecker(
                defaults: defaults,
                currentVersionString: "1.0.0",
                fetcher: { _ in
                    let response = HTTPURLResponse(
                        url: UpdateChecker.endpoint,
                        statusCode: 500,
                        httpVersion: nil,
                        headerFields: nil
                    )!
                    return (Data(), response)
                }
            )

            await checker.checkManually()

            XCTAssertEqual(checker.state, .failed)
        }
    }

    func testCheckSkipsNewerReleaseWithoutCompatibleAsset() async throws {
        try await withDefaults { defaults in
            let checker = UpdateChecker(
                defaults: defaults,
                currentVersionString: "1.0.0",
                compatibleAssetSuffix: "-macos-arm64.zip",
                fetcher: { _ in
                    try Self.releaseResponse(releases: [
                        ("v1.2.1", ["BlinkRest-v1.2.1-windows-x64.zip"]),
                        ("v1.2.0", ["BlinkRest-v1.2.0-macos-arm64.zip"]),
                        ("v1.1.0", ["BlinkRest-v1.1.0-macos-arm64.zip"])
                    ])
                }
            )

            await checker.checkManually()

            guard case let .updateAvailable(release) = checker.state else {
                return XCTFail("Expected updateAvailable state")
            }
            XCTAssertEqual(release.version, AppVersion("1.2.0"))
        }
    }

    func testCheckTreatsPlatformWithoutNewerAssetAsCurrent() async throws {
        try await withDefaults { defaults in
            let checker = UpdateChecker(
                defaults: defaults,
                currentVersionString: "1.1.0",
                compatibleAssetSuffix: "-macos-arm64.zip",
                fetcher: { _ in
                    try Self.releaseResponse(releases: [
                        ("v1.2.0", ["BlinkRest-v1.2.0-windows-x64.zip"]),
                        ("v1.1.0", ["BlinkRest-v1.1.0-macos-arm64.zip"])
                    ])
                }
            )

            await checker.checkManually()

            XCTAssertEqual(checker.state, .upToDate)
        }
    }

    private static func releaseResponse(
        releases: [(tag: String, assets: [String])]
    ) throws -> (Data, URLResponse) {
        let objects = releases.map { release in
            let assets = release.assets.map { "{\"name\":\"\($0)\"}" }.joined(separator: ",")
            return """
            {
              "tag_name": "\(release.tag)",
              "html_url": "https://github.com/fynxiu/blink-rest/releases/tag/\(release.tag)",
              "body": "Release notes",
              "draft": false,
              "prerelease": false,
              "assets": [\(assets)]
            }
            """
        }.joined(separator: ",")
        let json = "[\(objects)]"
        let response = HTTPURLResponse(
            url: UpdateChecker.endpoint,
            statusCode: 200,
            httpVersion: nil,
            headerFields: nil
        )!
        return (Data(json.utf8), response)
    }

    private func withDefaults(
        _ body: @MainActor (UserDefaults) async throws -> Void
    ) async throws {
        let suiteName = "UpdateCheckerTests.\(UUID().uuidString)"
        let defaults = try XCTUnwrap(UserDefaults(suiteName: suiteName))
        defaults.removePersistentDomain(forName: suiteName)
        defer { defaults.removePersistentDomain(forName: suiteName) }
        try await body(defaults)
    }
}
