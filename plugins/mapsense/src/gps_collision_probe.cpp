#include "gps_collision_probe.hpp"

#include <D2RLPlugin/api.h>

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace RuffnecKk::MapSense {
namespace {

constexpr std::uintptr_t CollisionFreeGridRva = 0x363A10U;
constexpr std::uintptr_t CollisionAllocateGridRva = 0x363A90U;
constexpr std::uintptr_t CollisionGridGetterRva = 0x2EFB30U;
constexpr std::uintptr_t CollisionGridSetterRva = 0x2F0540U;
constexpr std::uintptr_t ActiveRoomDrlgRoomAccessorRva = 0x192B20U;
constexpr std::uintptr_t DrlgRoomActiveRoomWitnessRva = 0x3289EEU;
constexpr std::uintptr_t ActiveRoomCoordinatesRva = 0x2EFB40U;
constexpr std::uintptr_t ActiveRoomNeighboursRva = 0x2EFDE0U;
constexpr std::uintptr_t ActiveRoomCloneSizeWitnessRva = 0x2EF36EU;
constexpr std::uintptr_t CollisionGridLayoutWitnessRva = 0x36697BU;
constexpr std::uintptr_t CollisionGridHeightWitnessRva = 0x3669E6U;
constexpr std::size_t CollisionFreeGridBytes = 121U;
constexpr std::size_t CollisionAllocateGridBytes = 467U;
constexpr std::uintptr_t FloorTilesAccessorRva = 0x2EFB70U;
constexpr std::uintptr_t WallTilesAccessorRva = 0x2EFDF0U;
constexpr std::uintptr_t RoofTilesAccessorRva = 0x2EFD30U;
constexpr std::uintptr_t CollisionStampTilesRva = 0x365520U;
constexpr std::size_t FloorTilesAccessorBytes = 116U;
constexpr std::size_t WallTilesAccessorBytes = 81U;
constexpr std::size_t RoofTilesAccessorBytes = 82U;
constexpr std::size_t CollisionStampTilesBytes = 589U;

#ifndef RUFFNECKK_MAPSENSE_ENABLE_GPS_COLLISION_PROBE
#define RUFFNECKK_MAPSENSE_ENABLE_GPS_COLLISION_PROBE 0
#endif

using NativeGridFn = void(__fastcall*)(void*) noexcept;

struct alignas(16) PrivateActiveRoomClone final {
    std::array<std::byte, GpsCollisionProbeCloneBytes> descriptor{};
    std::vector<void*> neighbours;
};

static_assert(offsetof(PrivateActiveRoomClone, descriptor) == 0U);
static_assert(GpsCollisionProbeCloneBytes
    >= GpsCollisionProbeCoordinatesOffset + 0x20U);
static_assert(GpsCollisionProbeGridOffset + sizeof(void*)
    <= GpsCollisionProbeCloneBytes);
static_assert(GpsCollisionProbeNeighbourCountOffset + sizeof(std::uint32_t)
    <= GpsCollisionProbeCloneBytes);

std::atomic_bool Active{};
std::atomic<NativeGridFn> AllocateGrid{};
std::atomic<NativeGridFn> FreeGrid{};

[[nodiscard]] auto HashMatches(
        const void* bytes,
        std::size_t size,
        const std::array<std::uint8_t, 32U>& expected) noexcept -> bool {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    std::array<std::uint8_t, 32U> digest{};
    const auto opened = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0U);
    if (opened < 0) return false;
    const auto created = BCryptCreateHash(
        algorithm, &hash, nullptr, 0U, nullptr, 0U, 0U);
    const auto updated = created >= 0 && BCryptHashData(
        hash,
        const_cast<PUCHAR>(static_cast<const UCHAR*>(bytes)),
        static_cast<ULONG>(size),
        0U) >= 0;
    const auto finished = updated && BCryptFinishHash(
        hash, digest.data(), static_cast<ULONG>(digest.size()), 0U) >= 0;
    if (hash != nullptr) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0U);
    return finished && digest == expected;
}

// Keep structured exception handling in leaf functions. This keeps native
// access faults from crossing C++ objects that require stack unwinding.
[[nodiscard]] auto CopyNativeMemory(
        void* destination, const void* source, std::size_t size) noexcept -> bool {
    __try {
        std::memcpy(destination, source, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

[[nodiscard]] auto InvokeNativeGrid(
        GpsCollisionProbeGridFn function, void* room) noexcept -> bool {
    __try {
        function(room);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

struct NativeCollisionGridView final {
    std::array<std::int32_t, 4U> dimensions{};
    void* cells{};
};

[[nodiscard]] auto ReadNativeCollisionGrid(
        const void* grid, NativeCollisionGridView& output) noexcept -> bool {
    return grid != nullptr
        && CopyNativeMemory(output.dimensions.data(), grid, output.dimensions.size()
            * sizeof(output.dimensions.front()))
        && CopyNativeMemory(&output.cells,
            static_cast<const std::byte*>(grid) + GpsCollisionProbeGridCellsOffset,
            sizeof(output.cells));
}

template <typename T>
[[nodiscard]] auto ReadField(
        const std::array<std::byte, GpsCollisionProbeCloneBytes>& bytes,
        std::size_t offset,
        T& output) noexcept -> bool {
    static_assert(std::is_trivially_copyable_v<T>);
    if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) return false;
    std::memcpy(&output, bytes.data() + offset, sizeof(T));
    return true;
}

template <typename T>
auto WriteField(
        std::array<std::byte, GpsCollisionProbeCloneBytes>& bytes,
        std::size_t offset,
        const T& value) noexcept -> bool {
    static_assert(std::is_trivially_copyable_v<T>);
    if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) return false;
    std::memcpy(bytes.data() + offset, &value, sizeof(T));
    return true;
}

[[nodiscard]] auto IsElapsed(
        std::chrono::steady_clock::time_point started,
        const GpsCollisionProbeLimits& limits) noexcept -> bool {
    return limits.maximumDuration.count() <= 0
        || std::chrono::steady_clock::now() - started > limits.maximumDuration;
}

[[nodiscard]] auto ReadLiveClone(
        void* liveRoom,
        std::span<void* const> activeRooms,
        const GpsCollisionProbeLimits& limits,
        const GpsCollisionProbeCallbacks& callbacks,
        const GpsCollisionProbeIdentity& expected,
        PrivateActiveRoomClone& clone,
        std::vector<std::uint32_t>& relationIndices,
        std::uint64_t& rawNeighbourEntries,
        std::uint32_t& relationStart,
        std::uint32_t& relationCount) noexcept -> GpsCollisionProbeStatus {
    if (liveRoom == nullptr) return GpsCollisionProbeStatus::MissingRoom;
    if (!CopyNativeMemory(clone.descriptor.data(), liveRoom,
            clone.descriptor.size())) {
        return GpsCollisionProbeStatus::NativeFailure;
    }

    void* liveNeighbours{};
    std::uint32_t neighbourCount{};
    if (!ReadField(clone.descriptor, GpsCollisionProbeNeighboursOffset,
            liveNeighbours)
        || !ReadField(clone.descriptor, GpsCollisionProbeNeighbourCountOffset,
            neighbourCount)
        || (neighbourCount != 0U && liveNeighbours == nullptr)) {
        return GpsCollisionProbeStatus::MissingRoom;
    }
    if (neighbourCount > limits.maximumRawNeighbours
        || rawNeighbourEntries > limits.maximumRawNeighbours - neighbourCount) {
        return GpsCollisionProbeStatus::BudgetExceeded;
    }
    rawNeighbourEntries += neighbourCount;

    try {
        clone.neighbours.resize(neighbourCount);
    } catch (...) {
        return GpsCollisionProbeStatus::NativeFailure;
    }
    if (neighbourCount != 0U) {
        if (!CopyNativeMemory(clone.neighbours.data(), liveNeighbours,
                static_cast<std::size_t>(neighbourCount) * sizeof(void*))) {
            return GpsCollisionProbeStatus::NativeFailure;
        }
    }
    const auto cloneAddress = static_cast<void*>(clone.descriptor.data());
    relationStart = static_cast<std::uint32_t>(relationIndices.size());
    relationCount = 0U;
    try {
        std::uint32_t selfCount{};
        for (std::size_t index = 0U; index < clone.neighbours.size(); ++index) {
            auto& neighbour = clone.neighbours[index];
            const auto liveNeighbour = neighbour;
            if (liveNeighbour == nullptr) {
                return GpsCollisionProbeStatus::MissingRoom;
            }
            if (std::find(clone.neighbours.begin(),
                    clone.neighbours.begin() + static_cast<std::ptrdiff_t>(index),
                    liveNeighbour) != clone.neighbours.begin()
                        + static_cast<std::ptrdiff_t>(index)) {
                return GpsCollisionProbeStatus::InvalidInput;
            }
            const auto found = std::find(activeRooms.begin(), activeRooms.end(),
                liveNeighbour);
            if (found == activeRooms.end()) {
                if (callbacks.validateForeignNeighbour == nullptr
                    || !callbacks.validateForeignNeighbour(
                        callbacks.userData, liveNeighbour, expected)) {
                    return GpsCollisionProbeStatus::MissingRoom;
                }
                // The native allocator needs boundary rooms to reconstruct its
                // private CollMap. MSNC v1 cannot represent cross-level links.
                continue;
            }
            const auto relationIndex = static_cast<std::uint32_t>(
                std::distance(activeRooms.begin(), found));
            if (relationIndices.size() >= limits.maximumRelations) {
                return GpsCollisionProbeStatus::BudgetExceeded;
            }
            relationIndices.push_back(relationIndex);
            ++relationCount;
            if (neighbour == liveRoom) {
                neighbour = cloneAddress;
                ++selfCount;
            }
        }
        if (selfCount != 1U) return GpsCollisionProbeStatus::InvalidInput;
    } catch (...) {
        return GpsCollisionProbeStatus::NativeFailure;
    }
    void* clonedNeighbours = clone.neighbours.empty()
        ? nullptr : static_cast<void*>(clone.neighbours.data());
    if (!WriteField(clone.descriptor, GpsCollisionProbeNeighboursOffset,
            clonedNeighbours)
        || !WriteField(clone.descriptor, GpsCollisionProbeNeighbourCountOffset,
            neighbourCount)) {
        return GpsCollisionProbeStatus::NativeFailure;
    }
    return GpsCollisionProbeStatus::Complete;
}

[[nodiscard]] auto CopyCloneGrid(
        const PrivateActiveRoomClone& clone,
        const GpsCollisionProbeLimits& limits,
        std::uint64_t& cellsTotal,
        GpsCollisionProbeRoom& output) noexcept -> GpsCollisionProbeStatus {
    void* grid{};
    if (!ReadField(clone.descriptor, GpsCollisionProbeGridOffset, grid)
        || grid == nullptr) {
        return GpsCollisionProbeStatus::NativeFailure;
    }
    NativeCollisionGridView nativeGrid;
    if (!ReadNativeCollisionGrid(grid, nativeGrid)
        || nativeGrid.dimensions[0] < 0 || nativeGrid.dimensions[1] < 0
        || nativeGrid.dimensions[2] <= 0 || nativeGrid.dimensions[3] <= 0) {
        return GpsCollisionProbeStatus::NativeFailure;
    }
    const auto width = static_cast<std::uint64_t>(nativeGrid.dimensions[2]);
    const auto height = static_cast<std::uint64_t>(nativeGrid.dimensions[3]);
    if (width > limits.maximumCells || height > limits.maximumCells
        || width > limits.maximumCells / height) {
        return GpsCollisionProbeStatus::BudgetExceeded;
    }
    const auto count = width * height;
    if (cellsTotal > limits.maximumCells - count) {
        return GpsCollisionProbeStatus::BudgetExceeded;
    }
    if (nativeGrid.cells == nullptr) return GpsCollisionProbeStatus::NativeFailure;

    try {
        output.subtileX = nativeGrid.dimensions[0];
        output.subtileY = nativeGrid.dimensions[1];
        output.width = nativeGrid.dimensions[2];
        output.height = nativeGrid.dimensions[3];
        output.cells.resize(static_cast<std::size_t>(count));
        if (!CopyNativeMemory(output.cells.data(), nativeGrid.cells,
                static_cast<std::size_t>(count) * sizeof(std::uint16_t))) {
            output = {};
            return GpsCollisionProbeStatus::NativeFailure;
        }
    } catch (...) {
        output = {};
        return GpsCollisionProbeStatus::NativeFailure;
    }
    cellsTotal += count;
    return GpsCollisionProbeStatus::Complete;
}

[[nodiscard]] auto PreflightCloneGridBudget(
        const PrivateActiveRoomClone& clone,
        const GpsCollisionProbeLimits& limits,
        std::uint64_t& plannedCells) noexcept -> GpsCollisionProbeStatus {
    std::int32_t width{};
    std::int32_t height{};
    if (!ReadField(clone.descriptor,
            GpsCollisionProbeCoordinatesOffset + 0x08U, width)
        || !ReadField(clone.descriptor,
            GpsCollisionProbeCoordinatesOffset + 0x0CU, height)
        || width <= 0 || height <= 0) {
        return GpsCollisionProbeStatus::NativeFailure;
    }
    const auto width64 = static_cast<std::uint64_t>(width);
    const auto height64 = static_cast<std::uint64_t>(height);
    if (width64 > limits.maximumCells
        || height64 > limits.maximumCells
        || width64 > limits.maximumCells / height64) {
        return GpsCollisionProbeStatus::BudgetExceeded;
    }
    const auto count = width64 * height64;
    if (plannedCells > limits.maximumCells - count) {
        return GpsCollisionProbeStatus::BudgetExceeded;
    }
    plannedCells += count;
    return GpsCollisionProbeStatus::Complete;
}

} // namespace

auto RunGpsCollisionProbeWithCallbacks(
        std::span<void* const> activeRooms,
        const GpsCollisionProbeLimits& limits,
        const GpsCollisionProbeCallbacks& callbacks,
        GpsCollisionProbeResult& output) noexcept -> bool {
    output = {};
    if (callbacks.readIdentity == nullptr || callbacks.allocateGrid == nullptr
        || callbacks.freeGrid == nullptr || activeRooms.empty()
        || activeRooms.size() > limits.maximumRooms
        || limits.maximumRooms == 0U || limits.maximumRelations == 0U
        || limits.maximumRawNeighbours == 0U
        || limits.maximumCells == 0U || limits.maximumDuration.count() <= 0) {
        output.status = GpsCollisionProbeStatus::InvalidInput;
        return false;
    }
    for (std::size_t index = 0U; index < activeRooms.size(); ++index) {
        if (activeRooms[index] == nullptr) {
            output.status = GpsCollisionProbeStatus::MissingRoom;
            return false;
        }
        if (std::find(activeRooms.begin(), activeRooms.begin() + index,
                activeRooms[index]) != activeRooms.begin() + index) {
            output.status = GpsCollisionProbeStatus::InvalidInput;
            return false;
        }
    }

    const auto started = std::chrono::steady_clock::now();
    GpsCollisionProbeIdentity expected{};
    if (!callbacks.readIdentity(callbacks.userData, expected)) {
        output.status = GpsCollisionProbeStatus::SessionDrift;
        return false;
    }

    GpsCollisionProbeArtifact artifact;
    artifact.identity = expected;
    try {
        artifact.rooms.reserve(activeRooms.size());
    } catch (...) {
        output.status = GpsCollisionProbeStatus::NativeFailure;
        return false;
    }

    std::uint64_t cells{};
    std::uint64_t plannedCells{};
    std::uint64_t rawNeighbourEntries{};
    for (void* const liveRoom : activeRooms) {
        if (IsElapsed(started, limits)) {
            output.status = GpsCollisionProbeStatus::BudgetExceeded;
            return false;
        }
        GpsCollisionProbeIdentity before{};
        if (!callbacks.readIdentity(callbacks.userData, before) || before != expected) {
            output.status = GpsCollisionProbeStatus::SessionDrift;
            return false;
        }

        PrivateActiveRoomClone clone;
        GpsCollisionProbeRoom room;
        const auto cloneState = ReadLiveClone(liveRoom, activeRooms, limits,
            callbacks, expected, clone, artifact.relations, rawNeighbourEntries,
            room.relationStart, room.relationCount);
        if (cloneState != GpsCollisionProbeStatus::Complete) {
            output.status = cloneState;
            return false;
        }

        const auto preflightState = PreflightCloneGridBudget(
            clone, limits, plannedCells);
        if (preflightState != GpsCollisionProbeStatus::Complete) {
            output.status = preflightState;
            return false;
        }

        void* nullGrid{};
        if (!WriteField(clone.descriptor, GpsCollisionProbeGridOffset, nullGrid)) {
            output.status = GpsCollisionProbeStatus::NativeFailure;
            return false;
        }
        auto* const cloneRoom = static_cast<void*>(clone.descriptor.data());
        if (!InvokeNativeGrid(callbacks.allocateGrid, cloneRoom)) {
            void* partialGrid{};
            if (ReadField(clone.descriptor, GpsCollisionProbeGridOffset, partialGrid)
                && partialGrid != nullptr) {
                (void)InvokeNativeGrid(callbacks.freeGrid, cloneRoom);
            }
            output.status = GpsCollisionProbeStatus::NativeFailure;
            return false;
        }
        void* grid{};
        (void)ReadField(clone.descriptor, GpsCollisionProbeGridOffset, grid);
        const auto copyState = grid != nullptr
            ? CopyCloneGrid(clone, limits, cells, room)
            : GpsCollisionProbeStatus::NativeFailure;
        const auto freed = InvokeNativeGrid(callbacks.freeGrid, cloneRoom);
        if (!freed) {
            output.status = GpsCollisionProbeStatus::NativeFailure;
            return false;
        }
        if (copyState != GpsCollisionProbeStatus::Complete) {
            output.status = copyState;
            return false;
        }
        room.levelId = expected.levelId;

        GpsCollisionProbeIdentity after{};
        if (!callbacks.readIdentity(callbacks.userData, after) || after != expected) {
            output.status = GpsCollisionProbeStatus::SessionDrift;
            return false;
        }
        try {
            artifact.rooms.push_back(std::move(room));
        } catch (...) {
            output.status = GpsCollisionProbeStatus::NativeFailure;
            return false;
        }
    }
    if (IsElapsed(started, limits)) {
        output.status = GpsCollisionProbeStatus::BudgetExceeded;
        return false;
    }
    GpsCollisionProbeIdentity finalIdentity{};
    if (!callbacks.readIdentity(callbacks.userData, finalIdentity)
        || finalIdentity != expected) {
        output.status = GpsCollisionProbeStatus::SessionDrift;
        return false;
    }
    artifact.allRoomsActive = true;
    artifact.shadowCollisionsRebuilt = true;
    artifact.sessionStable = true;
    output.status = GpsCollisionProbeStatus::Complete;
    output.artifact = std::move(artifact);
    return true;
}

auto InitializeGpsCollisionProbe(const D2RL::PluginContext* context) noexcept -> bool {
#if !RUFFNECKK_MAPSENSE_ENABLE_GPS_COLLISION_PROBE
    (void)context;
    return true;
#else
    ShutdownGpsCollisionProbe();
    if (!D2RL::HasContext(context) || context->exeBase == 0U) return false;
    constexpr std::array<std::uint8_t, 5U> gridGetterExpected{
        0x48, 0x8B, 0x41, 0x38, 0xC3};
    constexpr std::array<std::uint8_t, 5U> gridSetterExpected{
        0x48, 0x89, 0x51, 0x38, 0xC3};
    constexpr std::array<std::uint8_t, 16U> activeRoomDrlgExpected{
        0x48, 0x8B, 0x41, 0x18, 0xC3, 0xCC, 0xCC, 0xCC,
        0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    constexpr std::array<std::uint8_t, 24U> drlgRoomActiveRoomExpected{
        0x48, 0x8B, 0x43, 0x58, 0x48, 0x8B, 0x5C, 0x24,
        0x30, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3, 0xCC,
        0xCC, 0xCC, 0x40, 0x53, 0x56, 0x57, 0x41, 0x54};
    constexpr std::array<std::uint8_t, 38U> coordinatesExpected{
        0x48, 0x85, 0xC9, 0x75, 0x0B, 0x0F, 0x57, 0xC0,
        0x0F, 0x11, 0x02, 0x0F, 0x11, 0x42, 0x10, 0xC3,
        0x0F, 0x10, 0x81, 0x80, 0x00, 0x00, 0x00, 0x0F,
        0x11, 0x02, 0x0F, 0x10, 0x89, 0x90, 0x00, 0x00,
        0x00, 0x0F, 0x11, 0x4A, 0x10, 0xC3};
    constexpr std::array<std::uint8_t, 13U> neighboursExpected{
        0x8B, 0x41, 0x40, 0x41, 0x89, 0x00, 0x48,
        0x8B, 0x01, 0x48, 0x89, 0x02, 0xC3};
    constexpr std::array<std::uint8_t, 41U> cloneSizeExpected{
        0xBA, 0xB8, 0x00, 0x00, 0x00, 0x41, 0xB8, 0x10,
        0x00, 0x00, 0x00, 0x48, 0x8B, 0xC8, 0x4C, 0x8B,
        0x10, 0x41, 0xFF, 0x52, 0x08, 0x33, 0xD2, 0x41,
        0xB8, 0xB8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xC8,
        0x4C, 0x8B, 0xF0, 0xE8, 0x7A, 0x49, 0xFE, 0x00,
        0x8B};
    constexpr std::array<std::uint8_t, 43U> gridLayoutExpected{
        0x4C, 0x8B, 0x53, 0x20, 0x4D, 0x85, 0xD2, 0x0F,
        0x84, 0x6A, 0x03, 0x00, 0x00, 0x44, 0x8B, 0x03,
        0x45, 0x8D, 0x5F, 0x01, 0x44, 0x8B, 0x4B, 0x04,
        0x33, 0xC0, 0x45, 0x3B, 0xC5, 0x48, 0x89, 0x74,
        0x24, 0x50, 0x48, 0x63, 0x73, 0x08, 0x0F, 0x9D,
        0xC0, 0x48, 0x89};
    constexpr std::array<std::uint8_t, 15U> gridHeightExpected{
        0x8B, 0x4B, 0x0C, 0x41, 0x03, 0xC9, 0x41, 0x3B,
        0xCB, 0x0F, 0x4F, 0xC2, 0x83, 0xF8, 0x0C};
    constexpr std::array<std::uint8_t, 32U> freeHash{
        0x5B, 0x15, 0x2D, 0x13, 0x81, 0xE4, 0x0E, 0x51,
        0x88, 0xF3, 0x15, 0x90, 0x19, 0x47, 0x86, 0x0B,
        0xD4, 0x40, 0x6B, 0x7A, 0xBF, 0x1C, 0xB0, 0xCB,
        0x67, 0x0A, 0xF7, 0xA7, 0x9D, 0xFC, 0x45, 0x71};
    constexpr std::array<std::uint8_t, 32U> allocateHash{
        0x3C, 0x4B, 0x03, 0x8A, 0xC5, 0xC4, 0x19, 0x19,
        0xF3, 0xBD, 0x11, 0x36, 0x2A, 0x6B, 0x9C, 0x57,
        0xB1, 0xEF, 0xA4, 0xD5, 0x73, 0x92, 0x1C, 0xCA,
        0xBB, 0x02, 0x4C, 0x13, 0x80, 0xC8, 0x74, 0x81};
    constexpr std::array<std::uint8_t, 32U> floorTilesHash{
        0xBF, 0xA9, 0xE1, 0x00, 0x3C, 0xFC, 0xCE, 0xAF,
        0x3D, 0x8A, 0xEB, 0xEA, 0x92, 0x0F, 0x8B, 0xA9,
        0x29, 0x7A, 0x79, 0x85, 0x07, 0x11, 0xC4, 0xEC,
        0x5E, 0xE0, 0xB4, 0x01, 0xDB, 0xEA, 0x61, 0xA1};
    constexpr std::array<std::uint8_t, 32U> wallTilesHash{
        0xE0, 0x6C, 0x04, 0x60, 0x45, 0x3A, 0xC9, 0x75,
        0x71, 0x5B, 0xB2, 0xCC, 0xBC, 0x88, 0x83, 0xC1,
        0x49, 0xE5, 0xE5, 0x57, 0xA5, 0x68, 0x75, 0xEB,
        0x9D, 0x49, 0xA9, 0x2D, 0x59, 0xC0, 0xFA, 0xAE};
    constexpr std::array<std::uint8_t, 32U> roofTilesHash{
        0xE4, 0x1F, 0xE8, 0x44, 0xF0, 0xE1, 0x0C, 0x24,
        0x88, 0xD9, 0x73, 0x2C, 0x8E, 0x8D, 0x6E, 0xF2,
        0x02, 0x5C, 0xA2, 0xDB, 0x9E, 0x5B, 0x36, 0xD8,
        0x98, 0x85, 0xB1, 0x55, 0x45, 0xBF, 0x4A, 0xFA};
    constexpr std::array<std::uint8_t, 32U> stampTilesHash{
        0xC2, 0x55, 0x61, 0xF2, 0xB8, 0xBD, 0x27, 0x10,
        0xF5, 0x62, 0xBB, 0x4D, 0x38, 0x35, 0xC5, 0xBC,
        0x32, 0x7C, 0x5C, 0x19, 0xC0, 0xF6, 0x50, 0x17,
        0x3E, 0x16, 0x4B, 0x75, 0xFB, 0x86, 0x44, 0xE1};
    if (!context->CheckExpectedBytes(CollisionGridGetterRva,
            gridGetterExpected.data(), static_cast<std::uint32_t>(gridGetterExpected.size()))
        || !context->CheckExpectedBytes(CollisionGridSetterRva,
            gridSetterExpected.data(), static_cast<std::uint32_t>(gridSetterExpected.size()))
        || !context->CheckExpectedBytes(ActiveRoomDrlgRoomAccessorRva,
            activeRoomDrlgExpected.data(), static_cast<std::uint32_t>(activeRoomDrlgExpected.size()))
        || !context->CheckExpectedBytes(DrlgRoomActiveRoomWitnessRva,
            drlgRoomActiveRoomExpected.data(), static_cast<std::uint32_t>(drlgRoomActiveRoomExpected.size()))
        || !context->CheckExpectedBytes(ActiveRoomCoordinatesRva,
            coordinatesExpected.data(), static_cast<std::uint32_t>(coordinatesExpected.size()))
        || !context->CheckExpectedBytes(ActiveRoomNeighboursRva,
            neighboursExpected.data(), static_cast<std::uint32_t>(neighboursExpected.size()))
        || !context->CheckExpectedBytes(ActiveRoomCloneSizeWitnessRva,
            cloneSizeExpected.data(), static_cast<std::uint32_t>(cloneSizeExpected.size()))
        || !context->CheckExpectedBytes(CollisionGridLayoutWitnessRva,
            gridLayoutExpected.data(), static_cast<std::uint32_t>(gridLayoutExpected.size()))
        || !context->CheckExpectedBytes(CollisionGridHeightWitnessRva,
            gridHeightExpected.data(), static_cast<std::uint32_t>(gridHeightExpected.size()))
        || !HashMatches(reinterpret_cast<const void*>(
                context->exeBase + CollisionFreeGridRva),
            CollisionFreeGridBytes, freeHash)
        || !HashMatches(reinterpret_cast<const void*>(
            context->exeBase + CollisionAllocateGridRva),
            CollisionAllocateGridBytes, allocateHash)
        || !HashMatches(reinterpret_cast<const void*>(
                context->exeBase + FloorTilesAccessorRva),
            FloorTilesAccessorBytes, floorTilesHash)
        || !HashMatches(reinterpret_cast<const void*>(
                context->exeBase + WallTilesAccessorRva),
            WallTilesAccessorBytes, wallTilesHash)
        || !HashMatches(reinterpret_cast<const void*>(
                context->exeBase + RoofTilesAccessorRva),
            RoofTilesAccessorBytes, roofTilesHash)
        || !HashMatches(reinterpret_cast<const void*>(
                context->exeBase + CollisionStampTilesRva),
            CollisionStampTilesBytes, stampTilesHash)) {
        context->LogError("MapSense: GPS collision probe fingerprint mismatch; diagnostic disabled.");
        return false;
    }
    AllocateGrid.store(reinterpret_cast<NativeGridFn>(
        context->exeBase + CollisionAllocateGridRva), std::memory_order_release);
    FreeGrid.store(reinterpret_cast<NativeGridFn>(
        context->exeBase + CollisionFreeGridRva), std::memory_order_release);
    Active.store(true, std::memory_order_release);
    return true;
#endif
}

void ShutdownGpsCollisionProbe() noexcept {
    Active.store(false, std::memory_order_release);
    AllocateGrid.store(nullptr, std::memory_order_release);
    FreeGrid.store(nullptr, std::memory_order_release);
}

void ResetGpsCollisionProbe() noexcept {
    // The probe retains only process-lifetime native function pointers.
    // Per-session capture state is owned by RevealEngine.
}

auto IsGpsCollisionProbeActive() noexcept -> bool {
    return Active.load(std::memory_order_acquire)
        && AllocateGrid.load(std::memory_order_acquire) != nullptr
        && FreeGrid.load(std::memory_order_acquire) != nullptr;
}

auto RunGpsCollisionProbe(
        std::span<void* const> activeRooms,
        const GpsCollisionProbeLimits& limits,
        GpsCollisionProbeReadIdentityFn readIdentity,
        GpsCollisionProbeValidateForeignNeighbourFn validateForeignNeighbour,
        void* userData,
        GpsCollisionProbeResult& output) noexcept -> bool {
    if (!IsGpsCollisionProbeActive()) {
        output = {.status = GpsCollisionProbeStatus::Inactive};
        return false;
    }
    const auto allocateGrid = AllocateGrid.load(std::memory_order_acquire);
    const auto freeGrid = FreeGrid.load(std::memory_order_acquire);
    if (allocateGrid == nullptr || freeGrid == nullptr) {
        output = {.status = GpsCollisionProbeStatus::Inactive};
        return false;
    }
    return RunGpsCollisionProbeWithCallbacks(activeRooms, limits, {
        .readIdentity = readIdentity,
        .validateForeignNeighbour = validateForeignNeighbour,
        .allocateGrid = allocateGrid,
        .freeGrid = freeGrid,
        .userData = userData,
    }, output);
}

} // namespace RuffnecKk::MapSense
