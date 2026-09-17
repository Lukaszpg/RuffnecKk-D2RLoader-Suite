#pragma once

#include "gps_collision_probe.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace RuffnecKk::MapSense {

inline constexpr std::uint32_t GpsCollisionArtifactAllRoomsActive = 1U << 0U;
inline constexpr std::uint32_t GpsCollisionArtifactShadowRebuilt = 1U << 1U;
inline constexpr std::uint32_t GpsCollisionArtifactSessionStable = 1U << 2U;

[[nodiscard]] auto SerializeGpsCollisionArtifact(
    const GpsCollisionProbeArtifact& artifact,
    std::vector<std::uint8_t>& output) noexcept -> bool;

// Creates a new immutable diagnostic artifact. Existing files are never
// replaced, and output contains the created path only after every byte lands.
[[nodiscard]] auto WriteGpsCollisionArtifact(
    const GpsCollisionProbeArtifact& artifact,
    std::filesystem::path& output) noexcept -> bool;

} // namespace RuffnecKk::MapSense
