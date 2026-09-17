#pragma once

#include "gps_route_provider.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace RuffnecKk::MapSense {

inline constexpr std::int32_t GpsRouteReplanDistanceSubtiles = 8;
inline constexpr std::uint64_t GpsRouteReplanIntervalMilliseconds = 750U;

[[nodiscard]] inline auto SameGpsRouteCoordinatorHardScope(
        const GpsRouteProviderRequest& left,
        const GpsRouteProviderRequest& right) noexcept -> bool {
    return left.identity.sessionGeneration == right.identity.sessionGeneration
        && left.identity.destinationRevision == right.identity.destinationRevision
        && left.identity.policyRevision == right.identity.policyRevision
        && left.identity.terrainRevision == right.identity.terrainRevision
        && left.identity.destinationId == right.identity.destinationId
        && left.identity.destinationKind == right.identity.destinationKind
        && left.identity.artifact.seed == right.identity.artifact.seed
        && left.identity.artifact.difficulty == right.identity.artifact.difficulty
        && left.identity.artifact.levelId == right.identity.artifact.levelId
        && left.identity.artifact.toSubtileX == right.identity.artifact.toSubtileX
        && left.identity.artifact.toSubtileY == right.identity.artifact.toSubtileY
        && left.identity.artifact.mode == right.identity.artifact.mode;
}

[[nodiscard]] inline auto SameGpsRouteCoordinatorHardBatch(
        std::span<const GpsRouteProviderRequest> left,
        std::span<const GpsRouteProviderRequest> right) noexcept -> bool {
    return left.size() == right.size()
        && std::equal(left.begin(), left.end(), right.begin(),
            SameGpsRouteCoordinatorHardScope);
}

[[nodiscard]] inline auto IsGpsRouteCoordinatorBatchComplete(
        std::span<const GpsRouteProviderRequest> requests,
        std::span<const GpsRouteProviderPath> paths) noexcept -> bool {
    if (requests.empty() || paths.size() != requests.size()) return false;
    return std::all_of(requests.begin(), requests.end(),
        [paths](const GpsRouteProviderRequest& request) noexcept {
            return std::any_of(paths.begin(), paths.end(),
                [&request](const GpsRouteProviderPath& path) noexcept {
                    return SameGpsRoutePublicationScope(
                        request.identity, path.identity)
                        && path.identity.artifact.dataFingerprint != 0U;
                });
        });
}

[[nodiscard]] inline auto ShouldGpsRouteCoordinatorReplan(
        const GpsRouteProviderRequest& active,
        const GpsRouteProviderRequest& candidate,
        std::uint64_t lastRequestMilliseconds,
        std::uint64_t nowMilliseconds,
        bool providerTerminal) noexcept -> bool {
    const auto deltaX = std::abs(
        active.identity.artifact.fromSubtileX
        - candidate.identity.artifact.fromSubtileX);
    const auto deltaY = std::abs(
        active.identity.artifact.fromSubtileY
        - candidate.identity.artifact.fromSubtileY);
    return providerTerminal
        && nowMilliseconds - lastRequestMilliseconds
            >= GpsRouteReplanIntervalMilliseconds
        && std::max(deltaX, deltaY) >= GpsRouteReplanDistanceSubtiles;
}

} // namespace RuffnecKk::MapSense
