#include "gps_collision_probe.hpp"
#include "gps_collision_attempt_tracker.hpp"
#include "gps_collision_foreign_neighbour_policy.hpp"

#include <Windows.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <cstddef>
#include <vector>

namespace {

using namespace RuffnecKk::MapSense;

int Failures{};

void Check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::cerr << "FAIL line " << line << ": " << expression << '\n';
    ++Failures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

struct FakeGrid final {
    std::int32_t x{10};
    std::int32_t y{20};
    std::int32_t width{2};
    std::int32_t height{2};
    std::array<std::byte, 16U> reserved{};
    std::uint16_t* cells{};
    std::array<std::uint16_t, 4U> storage{1U, 2U, 3U, 4U};

    FakeGrid() noexcept : cells(storage.data()) {}
};

static_assert(offsetof(FakeGrid, cells) == GpsCollisionProbeGridCellsOffset);

struct FakeRoom final {
    alignas(16) std::array<std::byte, GpsCollisionProbeCloneBytes> bytes{};
    std::vector<void*> neighbours;
};

struct FakeState final {
    GpsCollisionProbeIdentity identity{
        .sessionGeneration = 7U,
        .mapSeed = 9U,
        .difficulty = 2U,
        .levelId = 4,
    };
    FakeGrid grid{};
    std::uint64_t liveGridSentinel{};
    bool sawSelfRemap{};
    bool partialAllocation{};
    bool raiseAllocation{};
    bool raiseFree{};
    bool freed{};
    bool freedLiveGrid{};
    bool sawNullGridOnEntry{};
    bool sawCopiedNeighbourList{};
    void* expectedLiveNeighbour{};
    void* expectedForeignNeighbour{};
    void* liveNeighbourList{};
    std::uint32_t foreignValidationCalls{};
    bool driftAfterTransaction{};
    std::uint32_t identityReads{};
    std::uint32_t allocationCalls{};
};

template <typename T>
void Put(std::array<std::byte, GpsCollisionProbeCloneBytes>& bytes,
        std::size_t offset, const T& value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

template <typename T>
auto Get(const std::array<std::byte, GpsCollisionProbeCloneBytes>& bytes,
        std::size_t offset) -> T {
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

auto ReadIdentity(void* userData, GpsCollisionProbeIdentity& output) noexcept -> bool {
    auto& state = *static_cast<FakeState*>(userData);
    ++state.identityReads;
    output = state.identity;
    if (state.driftAfterTransaction && state.identityReads >= 3U) {
        ++output.mapSeed;
    }
    return true;
}

auto ValidateForeignNeighbour(
        void* userData,
        void* activeRoom,
        const GpsCollisionProbeIdentity& expected) noexcept -> bool {
    auto& state = *static_cast<FakeState*>(userData);
    ++state.foreignValidationCalls;
    return expected == state.identity
        && activeRoom == state.expectedForeignNeighbour;
}

// The fake state is carried through a test-only global because the native ABI
// accepts only ActiveRoom*. Production state is never global to the artifact.
FakeState* Current{};

void __fastcall FakeAllocate(void* room) noexcept {
    auto& bytes = *static_cast<std::array<std::byte, GpsCollisionProbeCloneBytes>*>(room);
    ++Current->allocationCalls;
    if (Current->raiseAllocation) {
        RaiseException(0xE0424242U, 0U, 0U, nullptr);
        return;
    }
    const auto count = Get<std::uint32_t>(bytes, GpsCollisionProbeNeighbourCountOffset);
    const auto list = Get<void*>(bytes, GpsCollisionProbeNeighboursOffset);
    Current->sawNullGridOnEntry = Current->sawNullGridOnEntry
        || Get<void*>(bytes, GpsCollisionProbeGridOffset) == nullptr;
    Current->sawCopiedNeighbourList = Current->sawCopiedNeighbourList
        || (list != nullptr && list != Current->liveNeighbourList);
    if (count != 0U && list != nullptr) {
        auto neighbours = static_cast<void**>(list);
        for (std::uint32_t index = 0U; index < count; ++index) {
            if (neighbours[index] == room) Current->sawSelfRemap = true;
            if (neighbours[index] == Current->expectedLiveNeighbour) {
                Current->expectedLiveNeighbour = nullptr;
            }
            if (neighbours[index] == Current->expectedForeignNeighbour) {
                Current->expectedForeignNeighbour = nullptr;
            }
        }
    }
    if (Current->partialAllocation) {
        void* grid = &Current->grid;
        Put(bytes, GpsCollisionProbeGridOffset, grid);
        void* nullCells{};
        std::memcpy(static_cast<std::byte*>(grid) + GpsCollisionProbeGridCellsOffset,
            &nullCells, sizeof(nullCells));
        return;
    }
    void* grid = &Current->grid;
    Put(bytes, GpsCollisionProbeGridOffset, grid);
}

void __fastcall FakeFree(void* room) noexcept {
    if (Current->raiseFree) {
        RaiseException(0xE0424243U, 0U, 0U, nullptr);
        return;
    }
    auto& bytes = *static_cast<std::array<std::byte, GpsCollisionProbeCloneBytes>*>(room);
    const auto grid = Get<void*>(bytes, GpsCollisionProbeGridOffset);
    Current->freedLiveGrid = grid == &Current->liveGridSentinel;
    void* nullGrid{};
    Put(bytes, GpsCollisionProbeGridOffset, nullGrid);
    Current->freed = true;
}

void InitializeRoom(FakeRoom& room, void* liveGrid) {
    room.neighbours.push_back(room.bytes.data());
    void* list = room.neighbours.data();
    const std::uint32_t count = 1U;
    Put(room.bytes, GpsCollisionProbeNeighboursOffset, list);
    Put(room.bytes, GpsCollisionProbeNeighbourCountOffset, count);
    Put(room.bytes, GpsCollisionProbeGridOffset, liveGrid);
    const std::int32_t width = 2;
    const std::int32_t height = 2;
    Put(room.bytes, GpsCollisionProbeCoordinatesOffset + 0x08U, width);
    Put(room.bytes, GpsCollisionProbeCoordinatesOffset + 0x0CU, height);
}

auto Callbacks(FakeState& state) -> GpsCollisionProbeCallbacks {
    Current = &state;
    return {
        .readIdentity = ReadIdentity,
        .validateForeignNeighbour = ValidateForeignNeighbour,
        .allocateGrid = FakeAllocate,
        .freeGrid = FakeFree,
        .userData = &state,
    };
}

} // namespace

int main() {
    alignas(16) std::array<std::byte, 16U> foreignActive{};
    alignas(16) std::array<std::byte, 16U> foreignDrlgRoom{};
    alignas(16) std::array<std::byte, 16U> foreignLevel{};
    alignas(16) std::array<std::byte, 16U> currentLevel{};
    alignas(16) std::array<std::byte, 16U> clientDrlg{};
    alignas(16) std::array<std::byte, 16U> wrongDrlg{};
    alignas(16) std::array<std::byte, 32U> foreignGrid{};
    std::array<std::uint16_t, 100U> foreignCells{};
    const GpsCollisionProbeForeignNeighbourContext foreignContext{
        .clientDrlg = clientDrlg.data(),
        .currentLevel = currentLevel.data(),
        .currentLevelId = 4,
    };
    const GpsCollisionProbeForeignNeighbourView validForeign{
        .activeRoom = foreignActive.data(),
        .drlgRoom = foreignDrlgRoom.data(),
        .drlgRoomActiveRoom = foreignActive.data(),
        .level = foreignLevel.data(),
        .owningDrlg = clientDrlg.data(),
        .levelId = 5,
        .roomTileX = 100,
        .roomTileY = 200,
        .roomWidth = 2,
        .roomHeight = 2,
        .collisionGrid = foreignGrid.data(),
        .gridX = 500,
        .gridY = 1000,
        .gridWidth = 10,
        .gridHeight = 10,
        .gridCells = foreignCells.data(),
    };
    CHECK(IsGpsCollisionProbeForeignNeighbourValid(foreignContext, validForeign));
    auto wrongBacklink = validForeign;
    wrongBacklink.drlgRoomActiveRoom = foreignDrlgRoom.data();
    CHECK(!IsGpsCollisionProbeForeignNeighbourValid(foreignContext, wrongBacklink));
    auto wrongOwner = validForeign;
    wrongOwner.owningDrlg = wrongDrlg.data();
    CHECK(!IsGpsCollisionProbeForeignNeighbourValid(foreignContext, wrongOwner));
    auto sameLevel = validForeign;
    sameLevel.level = currentLevel.data();
    sameLevel.levelId = foreignContext.currentLevelId;
    CHECK(!IsGpsCollisionProbeForeignNeighbourValid(foreignContext, sameLevel));
    auto invalidGrid = validForeign;
    invalidGrid.gridWidth = 9;
    CHECK(!IsGpsCollisionProbeForeignNeighbourValid(foreignContext, invalidGrid));

    GpsCollisionProbeAttemptTracker attempts;
    attempts.BeginSession(7U);
    std::uint32_t nativeWorkReservations{};
    const auto reserveBeforeNativeWork = [&attempts, &nativeWorkReservations](
            std::uint64_t generation,
            std::int32_t levelId) {
        if (attempts.ReserveFirstAttempt(generation, levelId)) {
            ++nativeWorkReservations;
            return true;
        }
        return false;
    };
    CHECK(reserveBeforeNativeWork(7U, 4));
    CHECK(reserveBeforeNativeWork(7U, 5));
    CHECK(!reserveBeforeNativeWork(7U, 4));
    CHECK(nativeWorkReservations == 2U);
    CHECK(!attempts.ReserveFirstAttempt(8U, 4));
    attempts.BeginSession(8U);
    CHECK(attempts.ReserveFirstAttempt(8U, 4));
    CHECK(!attempts.ReserveFirstAttempt(8U, 0));
    attempts.BeginSession(9U);
    for (std::int32_t levelId = 1;
         levelId <= static_cast<std::int32_t>(GpsCollisionProbeAttemptLevelCapacity);
         ++levelId) {
        CHECK(attempts.ReserveFirstAttempt(9U, levelId));
    }
    CHECK(!attempts.ReserveFirstAttempt(
        9U,
        static_cast<std::int32_t>(GpsCollisionProbeAttemptLevelCapacity) + 1));

    FakeState state;
    FakeRoom room;
    InitializeRoom(room, &state.liveGridSentinel);
    const auto before = room.bytes;
    std::array<void*, 1U> rooms{room.bytes.data()};
    GpsCollisionProbeResult result;
    const GpsCollisionProbeLimits limits{};

    CHECK(RunGpsCollisionProbeWithCallbacks(rooms, limits, Callbacks(state), result));
    CHECK(result.status == GpsCollisionProbeStatus::Complete);
    CHECK(state.sawSelfRemap);
    CHECK(state.sawNullGridOnEntry);
    CHECK(state.sawCopiedNeighbourList);
    CHECK(state.freed);
    CHECK(!state.freedLiveGrid);
    CHECK(room.bytes == before);
    CHECK(Get<void*>(room.bytes, GpsCollisionProbeGridOffset)
        == &state.liveGridSentinel);
    CHECK(result.artifact.rooms.size() == 1U);
    CHECK(result.artifact.rooms[0].levelId == state.identity.levelId);
    CHECK(result.artifact.rooms[0].relationStart == 0U);
    CHECK(result.artifact.rooms[0].relationCount == 1U);
    CHECK(result.artifact.relations == std::vector<std::uint32_t>({0U}));
    CHECK(result.artifact.rooms[0].cells == std::vector<std::uint16_t>({1U, 2U, 3U, 4U}));

    std::array<void*, 1U> missing{nullptr};
    CHECK(!RunGpsCollisionProbeWithCallbacks(missing, limits, Callbacks(state), result));
    CHECK(result.status == GpsCollisionProbeStatus::MissingRoom);
    CHECK(result.artifact.rooms.empty());

    state.identityReads = 0U;
    state.driftAfterTransaction = true;
    state.freed = false;
    CHECK(!RunGpsCollisionProbeWithCallbacks(rooms, limits, Callbacks(state), result));
    CHECK(result.status == GpsCollisionProbeStatus::SessionDrift);
    CHECK(state.freed);
    CHECK(result.artifact.rooms.empty());
    state.driftAfterTransaction = false;

    auto bounded = limits;
    bounded.maximumCells = 3U;
    state.freed = false;
    const auto allocationsBeforeBudgetRefusal = state.allocationCalls;
    CHECK(!RunGpsCollisionProbeWithCallbacks(rooms, bounded, Callbacks(state), result));
    CHECK(result.status == GpsCollisionProbeStatus::BudgetExceeded);
    CHECK(state.allocationCalls == allocationsBeforeBudgetRefusal);
    CHECK(!state.freed);

    state.partialAllocation = true;
    state.freed = false;
    CHECK(!RunGpsCollisionProbeWithCallbacks(rooms, limits, Callbacks(state), result));
    CHECK(result.status == GpsCollisionProbeStatus::NativeFailure);
    CHECK(state.freed);

    FakeState allocatingFault;
    FakeRoom allocatingFaultRoom;
    InitializeRoom(allocatingFaultRoom, &allocatingFault.liveGridSentinel);
    allocatingFault.raiseAllocation = true;
    std::array<void*, 1U> allocatingFaultRooms{
        allocatingFaultRoom.bytes.data()};
    CHECK(!RunGpsCollisionProbeWithCallbacks(allocatingFaultRooms, limits,
        Callbacks(allocatingFault), result));
    CHECK(result.status == GpsCollisionProbeStatus::NativeFailure);
    CHECK(!allocatingFault.freedLiveGrid);

    FakeState freeingFault;
    FakeRoom freeingFaultRoom;
    InitializeRoom(freeingFaultRoom, &freeingFault.liveGridSentinel);
    freeingFault.raiseFree = true;
    std::array<void*, 1U> freeingFaultRooms{freeingFaultRoom.bytes.data()};
    CHECK(!RunGpsCollisionProbeWithCallbacks(freeingFaultRooms, limits,
        Callbacks(freeingFault), result));
    CHECK(result.status == GpsCollisionProbeStatus::NativeFailure);
    CHECK(!freeingFault.freedLiveGrid);

    FakeState twoRoomState;
    FakeRoom first;
    FakeRoom second;
    InitializeRoom(first, &twoRoomState.liveGridSentinel);
    InitializeRoom(second, &twoRoomState.liveGridSentinel);
    first.neighbours = {first.bytes.data(), second.bytes.data()};
    void* firstList = first.neighbours.data();
    const std::uint32_t firstCount = 2U;
    Put(first.bytes, GpsCollisionProbeNeighboursOffset, firstList);
    Put(first.bytes, GpsCollisionProbeNeighbourCountOffset, firstCount);
    twoRoomState.expectedLiveNeighbour = second.bytes.data();
    twoRoomState.liveNeighbourList = firstList;
    std::array<void*, 2U> twoRooms{first.bytes.data(), second.bytes.data()};
    CHECK(RunGpsCollisionProbeWithCallbacks(
        twoRooms, limits, Callbacks(twoRoomState), result));
    CHECK(twoRoomState.expectedLiveNeighbour == nullptr);
    CHECK(twoRoomState.sawCopiedNeighbourList);
    CHECK(result.artifact.rooms.size() == 2U);
    CHECK(result.artifact.rooms[0].relationCount == 2U);
    CHECK(result.artifact.relations
        == std::vector<std::uint32_t>({0U, 1U, 1U}));

    FakeState boundaryState;
    FakeRoom boundaryLocal;
    FakeRoom boundaryForeign;
    InitializeRoom(boundaryLocal, &boundaryState.liveGridSentinel);
    InitializeRoom(boundaryForeign, &boundaryState.liveGridSentinel);
    boundaryLocal.neighbours = {
        boundaryLocal.bytes.data(), boundaryForeign.bytes.data()};
    void* boundaryList = boundaryLocal.neighbours.data();
    const std::uint32_t boundaryCount = 2U;
    Put(boundaryLocal.bytes, GpsCollisionProbeNeighboursOffset, boundaryList);
    Put(boundaryLocal.bytes, GpsCollisionProbeNeighbourCountOffset, boundaryCount);
    boundaryState.expectedForeignNeighbour = boundaryForeign.bytes.data();
    boundaryState.liveNeighbourList = boundaryList;
    std::array<void*, 1U> boundaryRooms{boundaryLocal.bytes.data()};
    CHECK(RunGpsCollisionProbeWithCallbacks(
        boundaryRooms, limits, Callbacks(boundaryState), result));
    CHECK(boundaryState.foreignValidationCalls == 1U);
    CHECK(boundaryState.expectedForeignNeighbour == nullptr);
    CHECK(boundaryState.sawSelfRemap);
    CHECK(boundaryState.sawCopiedNeighbourList);
    CHECK(result.artifact.rooms.size() == 1U);
    CHECK(result.artifact.rooms[0].relationCount == 1U);
    CHECK(result.artifact.relations == std::vector<std::uint32_t>({0U}));

    FakeState invalidBoundaryState;
    FakeRoom invalidBoundaryLocal;
    FakeRoom invalidBoundaryForeign;
    InitializeRoom(invalidBoundaryLocal, &invalidBoundaryState.liveGridSentinel);
    InitializeRoom(invalidBoundaryForeign, &invalidBoundaryState.liveGridSentinel);
    invalidBoundaryLocal.neighbours = {
        invalidBoundaryLocal.bytes.data(), invalidBoundaryForeign.bytes.data()};
    void* invalidBoundaryList = invalidBoundaryLocal.neighbours.data();
    Put(invalidBoundaryLocal.bytes, GpsCollisionProbeNeighboursOffset,
        invalidBoundaryList);
    Put(invalidBoundaryLocal.bytes, GpsCollisionProbeNeighbourCountOffset,
        boundaryCount);
    std::array<void*, 1U> invalidBoundaryRooms{
        invalidBoundaryLocal.bytes.data()};
    CHECK(!RunGpsCollisionProbeWithCallbacks(invalidBoundaryRooms, limits,
        Callbacks(invalidBoundaryState), result));
    CHECK(result.status == GpsCollisionProbeStatus::MissingRoom);
    CHECK(invalidBoundaryState.foreignValidationCalls == 1U);
    CHECK(invalidBoundaryState.allocationCalls == 0U);

    FakeState missingSameLevelState;
    FakeRoom missingSameLevelLocal;
    FakeRoom missingSameLevelRoom;
    InitializeRoom(missingSameLevelLocal, &missingSameLevelState.liveGridSentinel);
    InitializeRoom(missingSameLevelRoom, &missingSameLevelState.liveGridSentinel);
    missingSameLevelLocal.neighbours = {
        missingSameLevelLocal.bytes.data(), missingSameLevelRoom.bytes.data()};
    void* missingSameLevelList = missingSameLevelLocal.neighbours.data();
    Put(missingSameLevelLocal.bytes, GpsCollisionProbeNeighboursOffset,
        missingSameLevelList);
    Put(missingSameLevelLocal.bytes, GpsCollisionProbeNeighbourCountOffset,
        boundaryCount);
    std::array<void*, 1U> missingSameLevelRooms{
        missingSameLevelLocal.bytes.data()};
    // The validator refuses this absent same-level room before allocation.
    CHECK(!RunGpsCollisionProbeWithCallbacks(missingSameLevelRooms, limits,
        Callbacks(missingSameLevelState), result));
    CHECK(result.status == GpsCollisionProbeStatus::MissingRoom);
    CHECK(missingSameLevelState.allocationCalls == 0U);

    FakeState duplicateForeignState;
    FakeRoom duplicateForeignLocal;
    FakeRoom duplicateForeignRoom;
    InitializeRoom(duplicateForeignLocal, &duplicateForeignState.liveGridSentinel);
    InitializeRoom(duplicateForeignRoom, &duplicateForeignState.liveGridSentinel);
    duplicateForeignLocal.neighbours = {duplicateForeignLocal.bytes.data(),
        duplicateForeignRoom.bytes.data(), duplicateForeignRoom.bytes.data()};
    void* duplicateForeignList = duplicateForeignLocal.neighbours.data();
    const std::uint32_t duplicateForeignCount = 3U;
    Put(duplicateForeignLocal.bytes, GpsCollisionProbeNeighboursOffset,
        duplicateForeignList);
    Put(duplicateForeignLocal.bytes, GpsCollisionProbeNeighbourCountOffset,
        duplicateForeignCount);
    duplicateForeignState.expectedForeignNeighbour = duplicateForeignRoom.bytes.data();
    std::array<void*, 1U> duplicateForeignRooms{
        duplicateForeignLocal.bytes.data()};
    CHECK(!RunGpsCollisionProbeWithCallbacks(duplicateForeignRooms, limits,
        Callbacks(duplicateForeignState), result));
    CHECK(result.status == GpsCollisionProbeStatus::InvalidInput);
    CHECK(duplicateForeignState.allocationCalls == 0U);

    auto rawBounded = limits;
    rawBounded.maximumRawNeighbours = 1U;
    FakeState rawBudgetState;
    FakeRoom rawBudgetRoom;
    InitializeRoom(rawBudgetRoom, &rawBudgetState.liveGridSentinel);
    rawBudgetRoom.neighbours = {rawBudgetRoom.bytes.data(),
        boundaryForeign.bytes.data()};
    void* rawBudgetList = rawBudgetRoom.neighbours.data();
    Put(rawBudgetRoom.bytes, GpsCollisionProbeNeighboursOffset, rawBudgetList);
    Put(rawBudgetRoom.bytes, GpsCollisionProbeNeighbourCountOffset, boundaryCount);
    rawBudgetState.expectedForeignNeighbour = boundaryForeign.bytes.data();
    std::array<void*, 1U> rawBudgetRooms{rawBudgetRoom.bytes.data()};
    CHECK(!RunGpsCollisionProbeWithCallbacks(rawBudgetRooms, rawBounded,
        Callbacks(rawBudgetState), result));
    CHECK(result.status == GpsCollisionProbeStatus::BudgetExceeded);
    CHECK(rawBudgetState.allocationCalls == 0U);

    FakeState cumulativeRawBudgetState;
    FakeRoom cumulativeFirst;
    FakeRoom cumulativeSecond;
    FakeRoom cumulativeForeign;
    InitializeRoom(cumulativeFirst, &cumulativeRawBudgetState.liveGridSentinel);
    InitializeRoom(cumulativeSecond, &cumulativeRawBudgetState.liveGridSentinel);
    InitializeRoom(cumulativeForeign, &cumulativeRawBudgetState.liveGridSentinel);
    cumulativeFirst.neighbours = {cumulativeFirst.bytes.data(),
        cumulativeForeign.bytes.data()};
    cumulativeSecond.neighbours = {cumulativeSecond.bytes.data(),
        cumulativeForeign.bytes.data()};
    void* cumulativeFirstList = cumulativeFirst.neighbours.data();
    void* cumulativeSecondList = cumulativeSecond.neighbours.data();
    Put(cumulativeFirst.bytes, GpsCollisionProbeNeighboursOffset,
        cumulativeFirstList);
    Put(cumulativeSecond.bytes, GpsCollisionProbeNeighboursOffset,
        cumulativeSecondList);
    Put(cumulativeFirst.bytes, GpsCollisionProbeNeighbourCountOffset,
        boundaryCount);
    Put(cumulativeSecond.bytes, GpsCollisionProbeNeighbourCountOffset,
        boundaryCount);
    cumulativeRawBudgetState.expectedForeignNeighbour = cumulativeForeign.bytes.data();
    std::array<void*, 2U> cumulativeRawBudgetRooms{
        cumulativeFirst.bytes.data(), cumulativeSecond.bytes.data()};
    rawBounded.maximumRawNeighbours = 3U;
    CHECK(!RunGpsCollisionProbeWithCallbacks(cumulativeRawBudgetRooms,
        rawBounded, Callbacks(cumulativeRawBudgetState), result));
    CHECK(result.status == GpsCollisionProbeStatus::BudgetExceeded);
    CHECK(cumulativeRawBudgetState.allocationCalls == 1U);

    FakeState missingSelfState;
    FakeRoom missingSelf;
    InitializeRoom(missingSelf, &missingSelfState.liveGridSentinel);
    missingSelf.neighbours = {second.bytes.data()};
    void* missingSelfList = missingSelf.neighbours.data();
    Put(missingSelf.bytes, GpsCollisionProbeNeighboursOffset, missingSelfList);
    std::array<void*, 2U> missingSelfRooms{
        missingSelf.bytes.data(), second.bytes.data()};
    CHECK(!RunGpsCollisionProbeWithCallbacks(missingSelfRooms, limits,
        Callbacks(missingSelfState), result));
    CHECK(result.status == GpsCollisionProbeStatus::InvalidInput);
    CHECK(missingSelfState.allocationCalls == 0U);

    FakeState duplicateState;
    FakeRoom duplicate;
    InitializeRoom(duplicate, &duplicateState.liveGridSentinel);
    duplicate.neighbours = {duplicate.bytes.data(), duplicate.bytes.data()};
    void* duplicateList = duplicate.neighbours.data();
    const std::uint32_t duplicateCount = 2U;
    Put(duplicate.bytes, GpsCollisionProbeNeighboursOffset, duplicateList);
    Put(duplicate.bytes, GpsCollisionProbeNeighbourCountOffset, duplicateCount);
    std::array<void*, 1U> duplicateRooms{duplicate.bytes.data()};
    CHECK(!RunGpsCollisionProbeWithCallbacks(duplicateRooms, limits,
        Callbacks(duplicateState), result));
    CHECK(result.status == GpsCollisionProbeStatus::InvalidInput);
    CHECK(duplicateState.allocationCalls == 0U);

    return Failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
