#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace RuffnecKk::MapSense {

inline constexpr std::int32_t GpsCollisionProbeSubtilesPerGameTile = 5;
inline constexpr std::uint64_t GpsCollisionProbeMaximumCellsPerRoom =
    16'777'216U;

struct GpsCollisionProbeForeignNeighbourContext final {
    void* clientDrlg{};
    void* currentLevel{};
    std::int32_t currentLevelId{};
};

// This is a copied, fully-read view. The production leaf function performs the
// guarded native reads before it calls this policy, so unit tests exercise the
// exact accept/refuse rules without a D2R process.
struct GpsCollisionProbeForeignNeighbourView final {
    void* activeRoom{};
    void* drlgRoom{};
    void* drlgRoomActiveRoom{};
    void* level{};
    void* owningDrlg{};
    std::int32_t levelId{};
    std::int32_t roomTileX{};
    std::int32_t roomTileY{};
    std::int32_t roomWidth{};
    std::int32_t roomHeight{};
    void* collisionGrid{};
    std::int32_t gridX{};
    std::int32_t gridY{};
    std::int32_t gridWidth{};
    std::int32_t gridHeight{};
    const std::uint16_t* gridCells{};
};

[[nodiscard]] constexpr auto IsGpsCollisionProbeAligned(
        const void* pointer) noexcept -> bool {
    return pointer != nullptr
        && (reinterpret_cast<std::uintptr_t>(pointer)
            & (alignof(void*) - 1U)) == 0U;
}

[[nodiscard]] constexpr auto IsGpsCollisionProbeForeignNeighbourValid(
        const GpsCollisionProbeForeignNeighbourContext& expected,
        const GpsCollisionProbeForeignNeighbourView& foreign) noexcept -> bool {
    if (!IsGpsCollisionProbeAligned(expected.clientDrlg)
        || !IsGpsCollisionProbeAligned(expected.currentLevel)
        || expected.currentLevelId <= 0
        || !IsGpsCollisionProbeAligned(foreign.activeRoom)
        || !IsGpsCollisionProbeAligned(foreign.drlgRoom)
        || foreign.drlgRoomActiveRoom != foreign.activeRoom
        || !IsGpsCollisionProbeAligned(foreign.level)
        || foreign.level == expected.currentLevel
        || foreign.owningDrlg != expected.clientDrlg
        || foreign.levelId <= 0 || foreign.levelId == expected.currentLevelId
        || foreign.roomTileX < 0 || foreign.roomTileY < 0
        || foreign.roomWidth <= 0 || foreign.roomHeight <= 0
        || !IsGpsCollisionProbeAligned(foreign.collisionGrid)
        || foreign.gridX < 0 || foreign.gridY < 0
        || foreign.gridWidth < 2 || foreign.gridHeight < 2
        || foreign.gridCells == nullptr
        || (reinterpret_cast<std::uintptr_t>(foreign.gridCells)
            & (alignof(std::uint16_t) - 1U)) != 0U) {
        return false;
    }
    const auto cellCount = static_cast<std::uint64_t>(foreign.gridWidth)
        * static_cast<std::uint64_t>(foreign.gridHeight);
    if (cellCount == 0U || cellCount > GpsCollisionProbeMaximumCellsPerRoom) {
        return false;
    }
    const auto expectedX = static_cast<std::int64_t>(foreign.roomTileX)
        * GpsCollisionProbeSubtilesPerGameTile;
    const auto expectedY = static_cast<std::int64_t>(foreign.roomTileY)
        * GpsCollisionProbeSubtilesPerGameTile;
    const auto expectedWidth = static_cast<std::int64_t>(foreign.roomWidth)
        * GpsCollisionProbeSubtilesPerGameTile;
    const auto expectedHeight = static_cast<std::int64_t>(foreign.roomHeight)
        * GpsCollisionProbeSubtilesPerGameTile;
    const auto right = static_cast<std::int64_t>(foreign.gridX)
        + foreign.gridWidth;
    const auto bottom = static_cast<std::int64_t>(foreign.gridY)
        + foreign.gridHeight;
    return expectedX == foreign.gridX && expectedY == foreign.gridY
        && expectedWidth == foreign.gridWidth && expectedHeight == foreign.gridHeight
        && right <= (std::numeric_limits<std::int32_t>::max)()
        && bottom <= (std::numeric_limits<std::int32_t>::max)();
}

} // namespace RuffnecKk::MapSense
