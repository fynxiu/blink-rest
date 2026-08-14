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
        string: "https://api.github.com/repos/fynxiu/blink-rest/releases/latest"
    )!
    static let automaticCheckInterval: TimeInterval = 24 * 60 * 60

    @Published private(set) var state: UpdateCheckState = .idle

    let currentVersion: AppVersion
    var onAutomaticUpdateAvailable: (@MainActor (UpdateRelease) -> Void)?

    private let defaults: UserDefaults
    private let fetcher: Fetcher
    private let logger = Logger(
        subsystem: Bundle.main.bundleIdentifier ?? "com.fynxiu.BlinkRest",
        category: "updates"
    )

    init(
        defaults: UserDefaults = .standard,
        currentVersionString: String = Bundle.main.object(
            forInfoDictionaryKey: "CFBundleShortVersionString"
        ) as? String ?? "1.0.0",
        fetcher: @escaping Fetcher = { request in
            try await URLSession.shared.data(for: request)
        }
    ) {
        self.defaults = defaults
        self.currentVersion = AppVersion(currentVersionString) ?? AppVersion("0.0.0")!
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

            let payload = try JSONDecoder().decode(GitHubReleasePayload.self, from: data)
            guard let latestVersion = AppVersion(payload.tagName) else {
                throw UpdateCheckFailure.invalidVersion
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

    enum CodingKeys: String, CodingKey {
        case tagName = "tag_name"
        case htmlURL = "html_url"
        case body
    }
}

private enum UpdateCheckFailure: Error {
    case invalidResponse
    case invalidVersion
}
