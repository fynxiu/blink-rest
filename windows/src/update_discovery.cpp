#include <blinkrest/update_discovery.hpp>

#include <array>
#include <charconv>
#include <cctype>
#include <system_error>
#include <vector>

namespace blinkrest::updates {
namespace {

class JsonCursor {
public:
    explicit JsonCursor(std::string_view input) : input_(input) {}

    bool parse_releases(std::string_view suffix, DiscoveryResult& result) {
        skip_ws();
        if (!consume('[')) return false;
        skip_ws();
        if (consume(']')) {
            result.valid = at_end();
            return result.valid;
        }

        for (;;) {
            ReleaseFields release;
            if (!parse_release(release)) return false;
            consider_release(release, suffix, result);
            skip_ws();
            if (consume(']')) break;
            if (!consume(',')) return false;
        }
        result.valid = at_end();
        return result.valid;
    }

private:
    struct ReleaseFields {
        std::optional<std::string> tag;
        std::optional<std::string> html_url;
        bool draft = false;
        bool prerelease = false;
        bool draft_seen = false;
        bool prerelease_seen = false;
        std::vector<std::string> asset_names;
    };

    bool parse_release(ReleaseFields& release) {
        skip_ws();
        if (!consume('{')) return false;
        skip_ws();
        if (consume('}')) return true;

        for (;;) {
            std::string key;
            if (!parse_string(key)) return false;
            skip_ws();
            if (!consume(':')) return false;
            skip_ws();

            if (key == "tag_name") {
                std::string value;
                if (!parse_string(value)) return false;
                release.tag = std::move(value);
            } else if (key == "html_url") {
                std::string value;
                if (!parse_string(value)) return false;
                release.html_url = std::move(value);
            } else if (key == "draft") {
                if (!parse_bool(release.draft)) return false;
                release.draft_seen = true;
            } else if (key == "prerelease") {
                if (!parse_bool(release.prerelease)) return false;
                release.prerelease_seen = true;
            } else if (key == "assets") {
                if (!parse_assets(release.asset_names)) return false;
            } else if (!skip_value()) {
                return false;
            }

            skip_ws();
            if (consume('}')) return true;
            if (!consume(',')) return false;
            skip_ws();
        }
    }

    bool parse_assets(std::vector<std::string>& names) {
        skip_ws();
        if (!consume('[')) return false;
        skip_ws();
        if (consume(']')) return true;

        for (;;) {
            skip_ws();
            if (!consume('{')) return false;
            skip_ws();
            if (!consume('}')) {
                for (;;) {
                    std::string key;
                    if (!parse_string(key)) return false;
                    skip_ws();
                    if (!consume(':')) return false;
                    skip_ws();
                    if (key == "name") {
                        std::string value;
                        if (!parse_string(value)) return false;
                        names.push_back(std::move(value));
                    } else if (!skip_value()) {
                        return false;
                    }
                    skip_ws();
                    if (consume('}')) break;
                    if (!consume(',')) return false;
                    skip_ws();
                }
            }
            skip_ws();
            if (consume(']')) return true;
            if (!consume(',')) return false;
            skip_ws();
        }
    }

    bool skip_value() {
        skip_ws();
        if (pos_ >= input_.size()) return false;
        const char ch = input_[pos_];
        if (ch == '"') {
            std::string ignored;
            return parse_string(ignored);
        }
        if (ch == '{') {
            ++pos_;
            skip_ws();
            if (consume('}')) return true;
            for (;;) {
                std::string key;
                if (!parse_string(key)) return false;
                skip_ws();
                if (!consume(':')) return false;
                if (!skip_value()) return false;
                skip_ws();
                if (consume('}')) return true;
                if (!consume(',')) return false;
                skip_ws();
            }
        }
        if (ch == '[') {
            ++pos_;
            skip_ws();
            if (consume(']')) return true;
            for (;;) {
                if (!skip_value()) return false;
                skip_ws();
                if (consume(']')) return true;
                if (!consume(',')) return false;
                skip_ws();
            }
        }
        if (match_literal("true") || match_literal("false") || match_literal("null")) {
            return true;
        }
        return skip_number();
    }

    bool parse_bool(bool& value) {
        if (match_literal("true")) {
            value = true;
            return true;
        }
        if (match_literal("false")) {
            value = false;
            return true;
        }
        return false;
    }

    bool parse_string(std::string& output) {
        skip_ws();
        if (!consume('"')) return false;
        output.clear();
        while (pos_ < input_.size()) {
            const unsigned char ch = static_cast<unsigned char>(input_[pos_++]);
            if (ch == '"') return true;
            if (ch < 0x20) return false;
            if (ch != '\\') {
                output.push_back(static_cast<char>(ch));
                continue;
            }
            if (pos_ >= input_.size()) return false;
            const char escaped = input_[pos_++];
            switch (escaped) {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'u': {
                unsigned value = 0;
                for (int i = 0; i < 4; ++i) {
                    if (pos_ >= input_.size()) return false;
                    const char hex = input_[pos_++];
                    value <<= 4;
                    if (hex >= '0' && hex <= '9') value += static_cast<unsigned>(hex - '0');
                    else if (hex >= 'a' && hex <= 'f') value += static_cast<unsigned>(hex - 'a' + 10);
                    else if (hex >= 'A' && hex <= 'F') value += static_cast<unsigned>(hex - 'A' + 10);
                    else return false;
                }
                output.push_back(value <= 0x7f ? static_cast<char>(value) : '?');
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    bool skip_number() {
        const std::size_t start = pos_;
        if (pos_ < input_.size() && input_[pos_] == '-') ++pos_;
        if (pos_ >= input_.size()) return false;
        if (input_[pos_] == '0') {
            ++pos_;
        } else if (std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
        } else {
            return false;
        }
        if (pos_ < input_.size() && input_[pos_] == '.') {
            ++pos_;
            const std::size_t digits = pos_;
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
            if (digits == pos_) return false;
        }
        if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) ++pos_;
            const std::size_t digits = pos_;
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
            if (digits == pos_) return false;
        }
        return pos_ > start;
    }

    bool match_literal(std::string_view literal) {
        if (input_.substr(pos_, literal.size()) != literal) return false;
        pos_ += literal.size();
        return true;
    }

    void consider_release(
        const ReleaseFields& release,
        std::string_view suffix,
        DiscoveryResult& result
    ) const {
        if (!release.tag || !release.html_url || !release.draft_seen || !release.prerelease_seen) return;
        if (release.draft || release.prerelease) return;
        const auto version = Version::parse(*release.tag);
        if (!version) return;
        const std::string expected = "BlinkRest-" + *release.tag + std::string(suffix);
        bool compatible = false;
        for (const auto& asset : release.asset_names) {
            if (asset == expected) {
                compatible = true;
                break;
            }
        }
        if (!compatible) return;
        if (!result.latest || result.latest->version < *version) {
            result.latest = CompatibleRelease{*version, *release.tag, *release.html_url};
        }
    }

    void skip_ws() {
        while (pos_ < input_.size()) {
            const unsigned char ch = static_cast<unsigned char>(input_[pos_]);
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') break;
            ++pos_;
        }
    }

    bool consume(char expected) {
        if (pos_ >= input_.size() || input_[pos_] != expected) return false;
        ++pos_;
        return true;
    }

    bool at_end() {
        skip_ws();
        return pos_ == input_.size();
    }

    std::string_view input_;
    std::size_t pos_ = 0;
};

}  // namespace

std::optional<Version> Version::parse(std::string_view value) {
    if (!value.empty() && value.front() == 'v') value.remove_prefix(1);
    std::array<int, 3> parts{};
    for (std::size_t index = 0; index < parts.size(); ++index) {
        const std::size_t dot = value.find('.');
        const std::string_view part = index == parts.size() - 1 ? value : value.substr(0, dot);
        if (part.empty() || (index != parts.size() - 1 && dot == std::string_view::npos)) return std::nullopt;
        int parsed = 0;
        const auto conversion = std::from_chars(part.data(), part.data() + part.size(), parsed);
        if (conversion.ec != std::errc{} || conversion.ptr != part.data() + part.size() || parsed < 0) {
            return std::nullopt;
        }
        parts[index] = parsed;
        if (index != parts.size() - 1) value.remove_prefix(dot + 1);
    }
    return Version{parts[0], parts[1], parts[2]};
}

std::string Version::description() const {
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

DiscoveryResult discover_latest_compatible_release(
    std::string_view releases_json,
    std::string_view asset_suffix
) {
    DiscoveryResult result;
    JsonCursor cursor(releases_json);
    if (!cursor.parse_releases(asset_suffix, result)) return {};
    return result;
}

}  // namespace blinkrest::updates
