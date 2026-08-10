import Foundation
import XCTest

final class LocalizationCatalogTests: XCTestCase {
    func testEveryCatalogEntryHasEnglishAndSimplifiedChineseTranslations() throws {
        let catalogURL = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .appending(path: "BlinkRest/Resources/Localizable.xcstrings")
        let data = try Data(contentsOf: catalogURL)
        let root = try XCTUnwrap(
            JSONSerialization.jsonObject(with: data) as? [String: Any]
        )
        let strings = try XCTUnwrap(root["strings"] as? [String: Any])

        XCTAssertFalse(strings.isEmpty)
        for key in strings.keys.sorted() {
            let entry = try XCTUnwrap(strings[key] as? [String: Any], key)
            let localizations = try XCTUnwrap(
                entry["localizations"] as? [String: Any],
                key
            )

            for language in ["en", "zh-Hans"] {
                let localization = try XCTUnwrap(
                    localizations[language] as? [String: Any],
                    "\(key) is missing \(language)"
                )
                let stringUnit = try XCTUnwrap(
                    localization["stringUnit"] as? [String: Any],
                    "\(key) has no \(language) string unit"
                )
                let value = try XCTUnwrap(
                    stringUnit["value"] as? String,
                    "\(key) has no \(language) value"
                )
                XCTAssertFalse(
                    value.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty,
                    "\(key) has an empty \(language) translation"
                )
            }
        }
    }
}
