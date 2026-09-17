#pragma once

#include "gps_route_provider.hpp"
#include "navigation_engine.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace D2RL {
struct PluginContext;
}

namespace RuffnecKk::MapSense {

struct GpsRouteCoordinatorInput final {
    NavigationGpsRouteSourceSnapshot source{};
    std::uint32_t seed{};
    std::uint8_t difficulty{};
    std::uint64_t terrainRevision{};
    std::vector<std::filesystem::path> excelRoots;
    std::vector<std::filesystem::path> tileRoots;
    std::uint64_t nowMilliseconds{};
    // The pre-projection phase may publish, but only the post-observation
    // phase has the newest player position and may submit initial/replan work.
    bool allowRequestSubmission{true};
};

struct GpsRouteCoordinatorResult final {
    std::size_t selectedCount{};
    std::size_t providerPathCount{};
    std::size_t publishedPathCount{};
    bool submitted{};
    bool acquired{};
    bool published{};
    std::array<GpsRouteProviderStatus, NavigationLineKindCount> statuses{};
};

[[nodiscard]] auto InitializeGpsRouteCoordinator(
    const D2RL::PluginContext* context) noexcept -> bool;
void ShutdownGpsRouteCoordinator() noexcept;
void ResetGpsRouteCoordinator(std::uint64_t sessionGeneration) noexcept;

// Called before native automap projection to publish completed work and again
// after observation to use the latest player subtile. It owns batching and bounded
// replanning; the renderer only consumes already-published route snapshots.
[[nodiscard]] auto TickGpsRouteCoordinator(
    const GpsRouteCoordinatorInput& input,
    GpsRouteCoordinatorResult& result) noexcept -> bool;

} // namespace RuffnecKk::MapSense
