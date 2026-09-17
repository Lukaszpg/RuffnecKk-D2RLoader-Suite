#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace RuffnecKk::MapSense {

inline constexpr std::uint32_t MaximumGpsRouteWalkGridCells = 1'048'576U;
inline constexpr std::uint32_t MaximumGpsRouteWalkGridExtent = 65'536U;

// Immutable helper-owned walkability corridor. Bits are row-major and use the
// least significant bit first within each byte. A grid received from an
// untrusted artifact is validated before it is shared with route consumers.
struct GpsRouteWalkGrid final {
    std::int32_t originX{};
    std::int32_t originY{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> bits;

    [[nodiscard]] auto Passable(std::int32_t x, std::int32_t y) const noexcept
            -> bool {
        const auto localX = static_cast<std::int64_t>(x) - originX;
        const auto localY = static_cast<std::int64_t>(y) - originY;
        if (localX < 0 || localY < 0
            || static_cast<std::uint64_t>(localX) >= width
            || static_cast<std::uint64_t>(localY) >= height) {
            return false;
        }
        const auto cell = static_cast<std::uint64_t>(localY) * width
            + static_cast<std::uint64_t>(localX);
        const auto byteIndex = cell / 8U;
        if (byteIndex >= bits.size()) return false;
        return (bits[static_cast<std::size_t>(byteIndex)]
            & static_cast<std::uint8_t>(1U << (cell % 8U))) != 0U;
    }
};

} // namespace RuffnecKk::MapSense
