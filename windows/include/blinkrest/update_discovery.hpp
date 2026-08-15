#pragma once

#include <compare>
#include <optional>
#include <string>
#include <string_view>

namespace blinkrest::updates {

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;

    static std::optional<Version> parse(std::string_view value);
    std::string description() const;
    auto operator<=>(const Version&) const = default;
};

struct CompatibleRelease {
    Version version{};
    std::string tag;
    std::string html_url;
};

struct DiscoveryResult {
    bool valid = false;
    std::optional<CompatibleRelease> latest;
};

DiscoveryResult discover_latest_compatible_release(
    std::string_view releases_json,
    std::string_view asset_suffix
);

}  // namespace blinkrest::updates
