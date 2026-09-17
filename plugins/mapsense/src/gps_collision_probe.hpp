#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace D2RL {
struct PluginContext;
}

namespace RuffnecKk::MapSense {

// MSNC v1 is a copied collision diagnostic. It deliberately contains no
// ActiveRoom, CollMap, neighbour-list, or allocator pointer.
inline constexpr std::uint16_t GpsCollisionProbeArtifactVersion = 1U;
inline constexpr std::size_t GpsCollisionProbeCloneBytes = 0xB8U;
inline constexpr std::size_t GpsCollisionProbeNeighboursOffset = 0x00U;
inline constexpr std::size_t GpsCollisionProbeGridOffset = 0x38U;
inline constexpr std::size_t GpsCollisionProbeNeighbourCountOffset = 0x40U;
inline constexpr std::size_t GpsCollisionProbeCoordinatesOffset = 0x80U;
inline constexpr std::size_t GpsCollisionProbeGridCellsOffset = 0x20U;
inline constexpr char GpsCollisionProbeNativeProfileId[] =
    "CFD75A5A1453B2DA0647B59EE3AEE664B707E7E744DDF9490FD58A51C43F7FF8";

struct GpsCollisionProbeIdentity final {
    std::uint64_t sessionGeneration{};
    std::uint32_t mapSeed{};
    std::uint8_t difficulty{};
    std::int32_t levelId{-1};

    [[nodiscard]] constexpr auto operator==(
            const GpsCollisionProbeIdentity&) const noexcept -> bool = default;
};

struct GpsCollisionProbeLimits final {
    std::uint32_t maximumRooms{4'096U};
    std::uint32_t maximumRelations{65'536U};
    // Bounds every raw native neighbour slot, including valid boundary rooms
    // which are deliberately absent from the serialized same-level relation set.
    std::uint32_t maximumRawNeighbours{65'536U};
    std::uint32_t maximumCells{1'048'576U};
    std::chrono::milliseconds maximumDuration{250};
};

struct GpsCollisionProbeRoom final {
    std::int32_t levelId{-1};
    std::int32_t subtileX{};
    std::int32_t subtileY{};
    std::int32_t width{};
    std::int32_t height{};
    std::uint32_t relationStart{};
    std::uint32_t relationCount{};
    std::vector<std::uint16_t> cells;
};

struct GpsCollisionProbeArtifact final {
    // These fields are consumed by the MSNC v1 serializer owned elsewhere.
    // No local wire writer is defined here.
    std::uint16_t version{GpsCollisionProbeArtifactVersion};
    std::string producer{"MapSense"};
    std::string profile{"shadow-CollMap"};
    std::string session;
    std::string nativeFingerprint{GpsCollisionProbeNativeProfileId};
    GpsCollisionProbeIdentity identity{};
    bool allRoomsActive{};
    bool shadowCollisionsRebuilt{};
    bool sessionStable{};
    std::vector<GpsCollisionProbeRoom> rooms;
    std::vector<std::uint32_t> relations;
};

enum class GpsCollisionProbeStatus : std::uint8_t {
    Complete,
    Inactive,
    InvalidInput,
    MissingRoom,
    SessionDrift,
    BudgetExceeded,
    NativeFailure,
};

struct GpsCollisionProbeResult final {
    GpsCollisionProbeStatus status{GpsCollisionProbeStatus::Inactive};
    GpsCollisionProbeArtifact artifact{};
};

using GpsCollisionProbeReadIdentityFn = bool(*) (
    void* userData,
    GpsCollisionProbeIdentity& output) noexcept;
// RevealEngine owns the governed ActiveRoom -> DrlgRoom -> Level -> DRLG
// ownership check. It accepts only a positive-level room from the same client
// DRLG and a level other than `expected.levelId`.
using GpsCollisionProbeValidateForeignNeighbourFn = bool(*) (
    void* userData,
    void* activeRoom,
    const GpsCollisionProbeIdentity& expected) noexcept;
using GpsCollisionProbeGridFn = void(__fastcall*)(void* activeRoom) noexcept;

// Callbacks make the transaction testable without D2R. The allocator and
// releaser receive only a private descriptor clone, never the live room.
struct GpsCollisionProbeCallbacks final {
    GpsCollisionProbeReadIdentityFn readIdentity{};
    GpsCollisionProbeValidateForeignNeighbourFn validateForeignNeighbour{};
    GpsCollisionProbeGridFn allocateGrid{};
    GpsCollisionProbeGridFn freeGrid{};
    void* userData{};
};

// Runs one complete clone-only capture. `activeRooms` must have been fully
// materialized by the caller. Failure clears the output artifact.
[[nodiscard]] auto RunGpsCollisionProbeWithCallbacks(
    std::span<void* const> activeRooms,
    const GpsCollisionProbeLimits& limits,
    const GpsCollisionProbeCallbacks& callbacks,
    GpsCollisionProbeResult& output) noexcept -> bool;

// Production wiring is intentionally compile-gated. RevealEngine owns the
// activation point and passes its copied ActiveRoom list to RunGpsCollisionProbe.
[[nodiscard]] auto InitializeGpsCollisionProbe(
    const D2RL::PluginContext* context) noexcept -> bool;
void ShutdownGpsCollisionProbe() noexcept;
void ResetGpsCollisionProbe() noexcept;
[[nodiscard]] auto IsGpsCollisionProbeActive() noexcept -> bool;
[[nodiscard]] auto RunGpsCollisionProbe(
    std::span<void* const> activeRooms,
    const GpsCollisionProbeLimits& limits,
    GpsCollisionProbeReadIdentityFn readIdentity,
    GpsCollisionProbeValidateForeignNeighbourFn validateForeignNeighbour,
    void* userData,
    GpsCollisionProbeResult& output) noexcept -> bool;

} // namespace RuffnecKk::MapSense
