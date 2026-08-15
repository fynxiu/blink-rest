import Combine
import Foundation
import OSLog

struct AppVersion: Comparable, Equatable, Sendable, CustomStringConvertible {
    let major: Int
    let minor: Int
    let patch: Int

    init?(_ rawValue: String) {
        let value = rawValue.hasPrefix("v") ? String(rawValue.dropFirst()) : rawValue
        let components = value.split(separator: ".", omittingEmptySubsequences: false)
        guard components.count == 3 else { return nil }

        let numbers = components.compactMap { component -> Int? in
            guard !component.isEmpty,
                  component.allSatisfy({ $0 >= "0" && $0 <= "9" }) else {
                return nil
            }
            return Int(component)
        }
        guard numbers.count == 3 else { return nil }

        major = numbers[0]
        minor = numbers[1]
        patch = numbers[2]
    }

    static func < (lhs: AppVersion, rhs: AppVersion) -> Bool {
        if lhs.major != rhs.major { return lhs.major < rhs.major }
        if lhs.minor != rhs.minor { return lhs.minor < rhs.minor }
        return lhs.patch < rhs.patch
    }

    var description: String {
        "\(major).\(minor).\(patch)"
    }
}

struct UpdateRelease: Equatable, Sendable {
    let version: AppVersion
    let url: URL
    let notes: String
}

enum UpdateCheckState: Equatable, Sendable {
    case idle
    case checking
    case upToDate
    case updateAvailable(UpdateRelease)
    case failed
}

@MainActor
final class UpdateChecker: ObservableObject {
    enum Keys {
        static let lastAutomaticCheckAt = "updates.lastAutomaticCheckAt"
        static let lastPromptedVersion = "updates.lastPromptedVersion"
    }

    typealias Fetcher = (URLRequest) async throws -> (Data, URLResponse)

    static let endpoint = URL(
        string: "https://api.github.com/repos/fynxiu/blink-rest/releases?per_page=100"
    )!
    static let automaticCheckInterval: TimeInterval = 24 * 60 * 60

    static var currentAssetSuffix: String {
#if arch(arm64)
        "-macos-arm64.zip"
#elseif arch(x86_64)
        "-macos-x86_64.zip"
#else
        "-macos-unsupported.zip"
#endif
    }

    @Published private(set) var state: UpdateCheckState = .idle

    let currentVersion: AppVersion
    var onAutomaticUpdateAvailable: (@MainActor (UpdateRelease) -> Void)?

    private let defaults: UserDefaults
    private let fetcher: Fetcher
    private let compatibleAssetSuffix: String
    private let logger = Logger(
        subsystem: Bundle.main.bundleIdentifier ?? "com.fynxiu.BlinkRest",
        category: "updates"
    )

    init(
        defaults: UserDefaults = .standard,
        currentVersionString: String = Bundle.main.object(
            forInfoDictionaryKey: "CFBundleShortVersionString"
        ) as? String ?? "1.0.0",
        compatibleAssetSuffix: String = UpdateChecker.currentAssetSuffix,
        fetcher: @escaping Fetcher = { request in
            try await URLSession.shared.data(for: request)
        }
    ) {
        self.defaults = defaults
        self.currentVersion = AppVersion(currentVersionString) ?? AppVersion("0.0.0")!
        self.compatibleAssetSuffix = compatibleAssetSuffix
        self.fetcher = fetcher
    }

    var isChecking: Bool {
        if case .checking = state { return true }
        return false
    }

    func checkAutomatically(now: Date = Date()) async {
        guard shouldPerformAutomaticCheck(now: now) else { return }
        defaults.set(now.timeIntervalSince1970, forKey: Keys.lastAutomaticCheckAt)
        await performCheck(isAutomatic: true)
    }

    func checkManually() async {
        await performCheck(isAutomatic: false)
    }

    private func shouldPerformAutomaticCheck(now: Date) -> Bool {
        guard defaults.object(forKey: Keys.lastAutomaticCheckAt) != nil else { return true }

        let lastCheck = Date(
            timeIntervalSince1970: defaults.double(forKey: Keys.lastAutomaticCheckAt)
        )
        let elapsed = now.timeIntervalSince(lastCheck)
        return elapsed < 0 || elapsed >= Self.automaticCheckInterval
    }

    private func performCheck(isAutomatic: Bool) async {
        guard !isChecking else { return }
        state = .checking

        do {
            var request = URLRequest(
                url: Self.endpoint,
                cachePolicy: .reloadIgnoringLocalCacheData,
                timeoutInterval: 10
            )
            request.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
            request.setValue("2022-11-28", forHTTPHeaderField: "X-GitHub-Api-Version")
            request.setValue(
                "BlinkRest/\(currentVersion.description)",
                forHTTPHeaderField: "User-Agent"
            )

            let (data, response) = try await fetcher(request)
            guard let httpResponse = response as? HTTPURLResponse,
                  (200..<300).contains(httpResponse.statusCode) else {
                throw UpdateCheckFailure.invalidResponse
            }

            let payloads = try JSONDecoder().decode([GitHubReleasePayload].self, from: data)
            let compatible = payloads.compactMap { payload -> (AppVersion, GitHubReleasePayload)? in
                let expectedAssetName = "BlinkRest-\(payload.tagName)\(compatibleAssetSuffix)"
                guard !payload.draft,
                      !payload.prerelease,
                      payload.assets.contains(where: { $0.name == expectedAssetName }),
                      let version = AppVersion(payload.tagName) else {
                    return nil
                }
                return (version, payload)
            }.max { $0.0 < $1.0 }

            guard let (latestVersion, payload) = compatible else {
                state = .upToDate
                logger.info("Update check completed; no compatible release is newer")
                return
            }

            guard latestVersion > currentVersion else {
                state = .upToDate
                logger.info("Update check completed; current version is up to date")
                return
            }

            let release = UpdateRelease(
                version: latestVersion,
                url: payload.htmlURL,
                notes: payload.body ?? ""
            )
            state = .updateAvailable(release)
            logger.info("Update available: \(latestVersion.description, privacy: .public)")

            if isAutomatic,
               defaults.string(forKey: Keys.lastPromptedVersion) != latestVersion.description {
                defaults.set(latestVersion.description, forKey: Keys.lastPromptedVersion)
                onAutomaticUpdateAvailable?(release)
            }
        } catch {
            state = .failed
            logger.error("Update check failed: \(error.localizedDescription, privacy: .public)")
        }
    }
}

private struct GitHubReleasePayload: Decodable {
    let tagName: String
    let htmlURL: URL
    let body: String?
    let draft: Bool
    let prerelease: Bool
    let assets: [GitHubReleaseAsset]

    enum CodingKeys: String, CodingKey {
        case tagName = "tag_name"
        case htmlURL = "html_url"
        case body
        case draft
        case prerelease
        case assets
    }
}

private struct GitHubReleaseAsset: Decodable {
    let name: String
}

private enum UpdateCheckFailure: Error {
    case invalidResponse
}
