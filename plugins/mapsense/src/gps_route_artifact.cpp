#include "gps_route_artifact.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <type_traits>

namespace RuffnecKk::MapSense {
namespace {

template <typename T>
[[nodiscard]] auto ReadLittle(
        std::span<const std::uint8_t> bytes,
        std::size_t& offset,
        T& output) noexcept -> bool {
    static_assert(std::is_integral_v<T>);
    if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) return false;
    using Unsigned = std::make_unsigned_t<T>;
    Unsigned value{};
    for (std::size_t index = 0U; index < sizeof(T); ++index) {
        value |= static_cast<Unsigned>(bytes[offset + index]) << (index * 8U);
    }
    std::memcpy(&output, &value, sizeof(T));
    offset += sizeof(T);
    return true;
}

[[nodiscard]] constexpr auto ValidMode(std::uint8_t value) noexcept -> bool {
    return value <= static_cast<std::uint8_t>(GpsRouteMode::Teleport);
}

[[nodiscard]] constexpr auto ValidMoveKind(
        std::uint8_t value, GpsRouteMode mode) noexcept -> bool {
    if (value > static_cast<std::uint8_t>(GpsRouteMoveKind::Teleport)) {
        return false;
    }
    return mode == GpsRouteMode::Teleport
        || value == static_cast<std::uint8_t>(GpsRouteMoveKind::Walk);
}

[[nodiscard]] constexpr auto WithinGoalSnapDistance(
        std::int32_t actual, std::int32_t requested) noexcept -> bool {
    const auto difference = static_cast<std::int64_t>(actual)
        - static_cast<std::int64_t>(requested);
    return difference >= -MaximumGpsRouteGoalSnapDistance
        && difference <= MaximumGpsRouteGoalSnapDistance;
}

[[nodiscard]] auto ReadWalkGrid(
        std::span<const std::uint8_t> bytes,
        std::size_t& offset,
        GpsRouteMode mode,
        std::shared_ptr<const GpsRouteWalkGrid>& output) noexcept -> bool {
    output.reset();
    std::int32_t originX{};
    std::int32_t originY{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t byteCount{};
    if (!ReadLittle(bytes, offset, originX)
        || !ReadLittle(bytes, offset, originY)
        || !ReadLittle(bytes, offset, width)
        || !ReadLittle(bytes, offset, height)
        || !ReadLittle(bytes, offset, byteCount)) {
        return false;
    }
    if (mode == GpsRouteMode::Teleport) {
        return originX == 0 && originY == 0 && width == 0U && height == 0U
            && byteCount == 0U && offset == bytes.size();
    }
    if (originX < 0 || originY < 0 || width == 0U || height == 0U
        || width > MaximumGpsRouteWalkGridExtent
        || height > MaximumGpsRouteWalkGridExtent) {
        return false;
    }
    const auto cellCount = static_cast<std::uint64_t>(width) * height;
    if (cellCount == 0U || cellCount > MaximumGpsRouteWalkGridCells
        || static_cast<std::uint64_t>(originX) + width
                > MaximumGpsRouteWalkGridExtent
        || static_cast<std::uint64_t>(originY) + height
                > MaximumGpsRouteWalkGridExtent) {
        return false;
    }
    const auto expectedByteCount = (cellCount + 7U) / 8U;
    if (byteCount != expectedByteCount || byteCount > MaximumGpsRouteWalkGridBytes
        || offset > bytes.size() || bytes.size() - offset != byteCount) {
        return false;
    }
    if (cellCount % 8U != 0U) {
        const auto validBits = static_cast<std::uint8_t>(cellCount % 8U);
        const auto paddingMask = static_cast<std::uint8_t>(
            ~((std::uint32_t{1U} << validBits) - 1U));
        if ((bytes.back() & paddingMask) != 0U) return false;
    }
    try {
        auto grid = std::make_shared<GpsRouteWalkGrid>();
        grid->originX = originX;
        grid->originY = originY;
        grid->width = width;
        grid->height = height;
        grid->bits.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.end());
        output = std::move(grid);
    } catch (...) {
        output.reset();
        return false;
    }
    offset = bytes.size();
    return true;
}

} // namespace

auto ParseGpsRouteArtifact(
        std::span<const std::uint8_t> bytes,
        const GpsRouteRequestIdentity& expected,
        GpsRouteArtifact& output) noexcept -> bool {
    output = {};
    if (bytes.size() < GpsRouteArtifactHeaderSize) {
        return false;
    }
    const bool msr1 = std::equal(bytes.begin(), bytes.begin() + 4, "MSR1");
    const bool msr2 = std::equal(bytes.begin(), bytes.begin() + 4, "MSR2");
    if (!msr1 && !msr2) return false;

    std::size_t offset = 4U;
    std::uint16_t version{};
    std::uint8_t modeValue{};
    std::uint8_t reservedByte{};
    GpsRouteRequestIdentity parsed{};
    std::array<std::uint8_t, 3U> reservedDifficulty{};
    std::uint32_t moveCount{};
    std::uint32_t reservedHeader{};
    if (!ReadLittle(bytes, offset, version)
        || !ReadLittle(bytes, offset, modeValue)
        || !ReadLittle(bytes, offset, reservedByte)
        || !ReadLittle(bytes, offset, parsed.seed)
        || !ReadLittle(bytes, offset, parsed.difficulty)) {
        return false;
    }
    for (auto& value : reservedDifficulty) {
        if (!ReadLittle(bytes, offset, value)) return false;
    }
    if (!ReadLittle(bytes, offset, parsed.levelId)
        || !ReadLittle(bytes, offset, parsed.fromSubtileX)
        || !ReadLittle(bytes, offset, parsed.fromSubtileY)
        || !ReadLittle(bytes, offset, parsed.toSubtileX)
        || !ReadLittle(bytes, offset, parsed.toSubtileY)
        || !ReadLittle(bytes, offset, parsed.dataFingerprint)
        || !ReadLittle(bytes, offset, moveCount)
        || !ReadLittle(bytes, offset, reservedHeader)
        || version != (msr1 ? 1U : 2U) || !ValidMode(modeValue)
        || reservedByte != 0U || reservedHeader != 0U
        || reservedDifficulty != std::array<std::uint8_t, 3U>{}
        || moveCount < 2U || moveCount > MaximumGpsRouteMoves) {
        return false;
    }
    parsed.mode = static_cast<GpsRouteMode>(modeValue);
    if (parsed != expected) return false;
    if (moveCount > ((std::numeric_limits<std::size_t>::max)()
            - GpsRouteArtifactHeaderSize) / GpsRouteArtifactMoveSize) {
        return false;
    }
    const auto moveBytes = static_cast<std::size_t>(moveCount)
        * GpsRouteArtifactMoveSize;
    const auto moveEnd = GpsRouteArtifactHeaderSize + moveBytes;
    if ((msr1 && bytes.size() != moveEnd)
        || (!msr1 && (moveEnd > bytes.size()
            || bytes.size() - moveEnd < GpsRouteArtifactWalkGridHeaderSize))) {
        return false;
    }

    try {
        output.identity = parsed;
        output.moves.reserve(moveCount);
        for (std::uint32_t index = 0U; index < moveCount; ++index) {
            GpsRouteMove move{};
            std::uint8_t kind{};
            std::array<std::uint8_t, 3U> reservedMove{};
            if (!ReadLittle(bytes, offset, move.subtileX)
                || !ReadLittle(bytes, offset, move.subtileY)
                || !ReadLittle(bytes, offset, kind)) {
                output = {};
                return false;
            }
            for (auto& value : reservedMove) {
                if (!ReadLittle(bytes, offset, value)) {
                    output = {};
                    return false;
                }
            }
            if (move.subtileX < 0 || move.subtileY < 0
                || reservedMove != std::array<std::uint8_t, 3U>{}
                || !ValidMoveKind(kind, parsed.mode)) {
                output = {};
                return false;
            }
            move.kind = static_cast<GpsRouteMoveKind>(kind);
            output.moves.push_back(move);
        }
        if (msr2 && !ReadWalkGrid(bytes, offset, parsed.mode, output.walkGrid)) {
            output = {};
            return false;
        }
        if (msr2 && parsed.mode == GpsRouteMode::Walk
            && (!output.walkGrid || std::any_of(
                output.moves.begin(), output.moves.end(),
                [&output](const GpsRouteMove& move) noexcept {
                    return !output.walkGrid->Passable(
                        move.subtileX, move.subtileY);
                }))) {
            output = {};
            return false;
        }
    } catch (...) {
        output = {};
        return false;
    }
    if (offset != bytes.size()
        || output.moves.front().subtileX != expected.fromSubtileX
        || output.moves.front().subtileY != expected.fromSubtileY
        || !WithinGoalSnapDistance(
            output.moves.back().subtileX, expected.toSubtileX)
        || !WithinGoalSnapDistance(
            output.moves.back().subtileY, expected.toSubtileY)) {
        output = {};
        return false;
    }
    return true;
}

} // namespace RuffnecKk::MapSense
