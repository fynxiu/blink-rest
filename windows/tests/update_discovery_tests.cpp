#include <blinkrest/update_discovery.hpp>

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

}  // namespace

int main() {
    using blinkrest::updates::Version;
    using blinkrest::updates::discover_latest_compatible_release;

    require(Version::parse("v1.10.0") > Version::parse("1.9.0"), "numeric version comparison");
    require(!Version::parse("1.2"), "reject short version");
    require(!Version::parse("1.2.beta"), "reject nonnumeric version");
    require(!Version::parse("1.2.3.4"), "reject long version");

    const char* releases = R"JSON([
      {"tag_name":"v1.2.1","html_url":"https://example/v1.2.1","body":"ignored","draft":false,"prerelease":false,
       "assets":[{"name":"BlinkRest-v1.2.1-windows-x64.zip","size":12}]},
      {"tag_name":"v1.3.0","html_url":"https://example/v1.3.0","draft":false,"prerelease":true,
       "assets":[{"name":"BlinkRest-v1.3.0-macos-arm64.zip"}]},
      {"tag_name":"v1.2.0","html_url":"https://example/v1.2.0","draft":false,"prerelease":false,
       "assets":[{"name":"BlinkRest-v1.2.0-macos-arm64.zip"}]},
      {"tag_name":"v1.1.0","html_url":"https://example/v1.1.0","draft":false,"prerelease":false,
       "assets":[{"name":"BlinkRest-v1.1.0-macos-arm64.zip"}]}
    ])JSON";

    const auto mac = discover_latest_compatible_release(releases, "-macos-arm64.zip");
    require(mac.valid && mac.latest.has_value(), "find mac release");
    require(mac.latest->version == *Version::parse("1.2.0"), "skip windows-only and prerelease");

    const auto windows = discover_latest_compatible_release(releases, "-windows-x64.zip");
    require(windows.valid && windows.latest.has_value(), "find windows release");
    require(windows.latest->version == *Version::parse("1.2.1"), "select latest windows release");

    const auto wrong_name = discover_latest_compatible_release(
        R"JSON([{"tag_name":"v2.0.0","html_url":"https://example/v2","draft":false,"prerelease":false,"assets":[{"name":"Other-v2.0.0-windows-x64.zip"}]}])JSON",
        "-windows-x64.zip"
    );
    require(wrong_name.valid && !wrong_name.latest, "asset name contract is exact");

    require(!discover_latest_compatible_release("{bad", "-windows-x64.zip").valid, "reject malformed json");
    std::cout << "update_discovery_tests: PASS\n";
    return 0;
}
