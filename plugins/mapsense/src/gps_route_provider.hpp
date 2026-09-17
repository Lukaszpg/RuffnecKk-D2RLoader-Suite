#pragma once

#include "gps_route_provider_policy.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace D2RL {
struct PluginContext;
}

namespace RuffnecKk::MapSense {

enum class GpsRouteProviderStatus : std::uint8_t {
    Unavailable,
    Calculating,
    NoRoute,
    RouteReady,
};

struct GpsRouteProviderRequest final {
    GpsRoutePublicationIdentity identity{};
    // Provider-owned monotonic epoch, deliberately outside the artifact and
    // publication identity so A→B→A cannot accept an old A completion.
    std::uint64_t requestEpoch{};
    std::vector<std::filesystem::path> excelRoots;
    std::vector<std::filesystem::path> tileRoots;
};

struct GpsRouteProviderPath final {
    GpsRoutePublicationIdentity identity{};
    std::vector<GpsRouteMove> moves;
    std::shared_ptr<const GpsRouteWalkGrid> walkGrid;
};

// A batch is terminal only when every desired request either published a path
// or reached a recorded failure. A partial publication must remain Calculating
// so callers do not replace still-running helper work.
template <typename PublishedPredicate, typename FailedPredicate>
[[nodiscard]] auto EvaluateGpsRouteProviderAggregateStatus(
        std::span<const GpsRouteProviderRequest> desired,
        PublishedPredicate&& isPublished,
        FailedPredicate&& isFailed) -> GpsRouteProviderStatus {
    if (desired.empty()) return GpsRouteProviderStatus::Calculating;
    bool anyPublished{};
    for (const auto& request : desired) {
        if (isPublished(request)) {
            anyPublished = true;
        } else if (!isFailed(request)) {
            return GpsRouteProviderStatus::Calculating;
        }
    }
    return anyPublished ? GpsRouteProviderStatus::RouteReady
                        : GpsRouteProviderStatus::NoRoute;
}

// Mirrors Mapgen LoadedInputs exactly: ordered lower-case table names followed
// by the first readable nonempty file from each ordered excel root, or one
// zero byte when Mapgen uses its embedded fallback. Tile roots affect routing
// but are not part of the route input fingerprint contract.
[[nodiscard]] auto ComputeGpsRouteInputFingerprint(
    std::span<const std::filesystem::path> excelRoots,
    std::uint64_t& fingerprint) noexcept -> bool;

[[nodiscard]] auto InitializeGpsRouteProvider(
    const D2RL::PluginContext* context) noexcept -> bool;
void ShutdownGpsRouteProvider() noexcept;
void ResetGpsRouteProviderSession(std::uint64_t sessionGeneration) noexcept;

// Replaces the desired route set. The private worker serializes helper calls,
// cancels stale work, and publishes only complete MSR1/MSR2 artifacts matching this
// exact request identity.
[[nodiscard]] auto SubmitGpsRouteProviderRequests(
    std::span<const GpsRouteProviderRequest> requests) noexcept -> bool;

[[nodiscard]] auto AcquireGpsRouteProviderPaths(
    std::vector<GpsRouteProviderPath>& paths) noexcept -> bool;

// A nonblocking summary of the entire current desired set. It remains
// Calculating while any desired request is still in flight.
[[nodiscard]] auto GetGpsRouteProviderStatus() noexcept
    -> GpsRouteProviderStatus;

// Returns one nonblocking aggregate per destination kind. A family remains
// Calculating while any of its desired routes is pending, becomes RouteReady
// once all work is terminal and at least one path exists, and becomes NoRoute
// only when every desired route for that family failed.
[[nodiscard]] auto AcquireGpsRouteProviderStatuses(
    std::span<GpsRouteProviderStatus> statuses) noexcept -> bool;

} // namespace RuffnecKk::MapSense
