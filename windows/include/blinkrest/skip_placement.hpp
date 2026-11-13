#pragma once

#include <cstdint>
#include <optional>

namespace blinkrest {

// Deliberately constrained to four corners: random across breaks, but never
// inside the central rest content or during an active break.
enum class SkipCorner : std::uint8_t {
    top_left = 0,
    top_right = 1,
    bottom_left = 2,
    bottom_right = 3,
};

constexpr bool is_left(SkipCorner corner) {
    return corner == SkipCorner::top_left || corner == SkipCorner::bottom_left;
}

constexpr bool is_top(SkipCorner corner) {
    return corner == SkipCorner::top_left || corner == SkipCorner::top_right;
}

// Caller provides an entropy sample so the decision is testable on all
// platforms. With a prior corner, choose uniformly among the other three.
constexpr SkipCorner next_skip_corner(
    std::optional<SkipCorner> previous,
    std::uint32_t random_sample
) {
    if (!previous) {
        return static_cast<SkipCorner>(random_sample % 4);
    }
    const auto prior_index = static_cast<std::uint32_t>(*previous);
    auto index = random_sample % 3;
    if (index >= prior_index) ++index;
    return static_cast<SkipCorner>(index);
}

} // namespace blinkrest
