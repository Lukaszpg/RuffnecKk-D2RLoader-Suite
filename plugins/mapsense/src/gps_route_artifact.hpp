#pragma once

#include "gps_route_walk_grid.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace RuffnecKk::MapSense {

inline constexpr std::size_t GpsRouteArtifactHeaderSize = 52U;
inline constexpr std::size_t GpsRouteArtifactMoveSize = 12U;
inline constexpr std::size_t GpsRouteArtifactWalkGridHeaderSize = 20U;
inline constexpr std::size_t MaximumGpsRouteMoves = 65'536U;
inline constexpr std::int32_t MaximumGpsRouteGoalSnapDistance = 48;
inline constexpr std::size_t MaximumGpsRouteWalkGridBytes =
    (MaximumGpsRouteWalkGridCells + 7U) / 8U;
inline constexpr std::size_t MaximumGpsRouteArtifactBytes =
    GpsRouteArtifactHeaderSize + MaximumGpsRouteMoves * GpsRouteArtifactMoveSize
    + GpsRouteArtifactWalkGridHeaderSize + MaximumGpsRouteWalkGridBytes;

enum class GpsRouteMode : std::uint8_t {
    Walk,
    Teleport,
};

enum class GpsRouteMoveKind : std::uint8_t {
    Walk,
    Teleport,
};

struct GpsRouteRequestIdentity final {
    std::uint32_t seed{};
    std::uint8_t difficulty{};
    std::int32_t levelId{-1};
    std::int32_t fromSubtileX{};
    std::int32_t fromSubtileY{};
    std::int32_t toSubtileX{};
    std::int32_t toSubtileY{};
    std::uint64_t dataFingerprint{};
    GpsRouteMode mode{GpsRouteMode::Walk};

    [[nodiscard]] constexpr auto operator==(
        const GpsRouteRequestIdentity&) const noexcept -> bool = default;
};

struct GpsRouteMove final {
    std::int32_t subtileX{};
    std::int32_t subtileY{};
    GpsRouteMoveKind kind{GpsRouteMoveKind::Walk};
};

struct GpsRouteArtifact final {
    GpsRouteRequestIdentity identity{};
    std::vector<GpsRouteMove> moves;
    std::shared_ptr<const GpsRouteWalkGrid> walkGrid;
};

// Parses helper-owned MSR1 or MSR2 artifacts and validates every identity
// field. MSR1 has no walk grid; MSR2 requires one for walk and forbids one for
// teleport. Failure leaves output empty and never accepts pad transitions.
[[nodiscard]] auto ParseGpsRouteArtifact(
    std::span<const std::uint8_t> bytes,
    const GpsRouteRequestIdentity& expected,
    GpsRouteArtifact& output) noexcept -> bool;

} // namespace RuffnecKk::MapSense
