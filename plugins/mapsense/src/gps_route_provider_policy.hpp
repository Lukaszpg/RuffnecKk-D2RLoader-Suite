#pragma once

#include "gps_route_artifact.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace RuffnecKk::MapSense {

// The helper protocol authenticates the route endpoints and effective data;
// these DLL-side fields bind that artifact to one live resolver publication.
struct GpsRoutePublicationIdentity final {
    std::uint64_t sessionGeneration{};
    std::uint64_t destinationRevision{};
    std::uint64_t policyRevision{};
    std::uint64_t terrainRevision{};
    std::uint64_t destinationId{};
    std::uint8_t destinationKind{};
    GpsRouteRequestIdentity artifact{};

    [[nodiscard]] constexpr auto operator==(
        const GpsRoutePublicationIdentity&) const noexcept -> bool = default;
};

[[nodiscard]] constexpr auto IsGpsRoutePublicationCurrent(
        const GpsRoutePublicationIdentity& expected,
        const GpsRoutePublicationIdentity& candidate) noexcept -> bool {
    return expected == candidate
        && expected.sessionGeneration != 0U
        && expected.destinationRevision != 0U
        && expected.policyRevision != 0U
        && expected.terrainRevision != 0U
        && expected.destinationId != 0U
        && expected.destinationKind < 4U
        && expected.artifact.levelId > 0
        && expected.artifact.fromSubtileX >= 0
        && expected.artifact.fromSubtileY >= 0
        && expected.artifact.toSubtileX >= 0
        && expected.artifact.toSubtileY >= 0;
}

[[nodiscard]] constexpr auto SameGpsRoutePublicationScope(
        GpsRoutePublicationIdentity left,
        GpsRoutePublicationIdentity right) noexcept -> bool {
    left.artifact.dataFingerprint = 0U;
    right.artifact.dataFingerprint = 0U;
    return left == right;
}

// Present can enqueue a session reset while the helper worker owns its mutex.
// The worker consumes the latest request under that mutex; callers never wait
// or drop the reset merely because the worker is currently busy.
class GpsRouteResetMailbox final {
public:
    void Request(std::uint64_t sessionGeneration) noexcept {
        pendingSessionGeneration_.store(sessionGeneration, std::memory_order_release);
        (void)pendingEpoch_.fetch_add(1U, std::memory_order_acq_rel);
    }

    [[nodiscard]] auto Consume(std::uint64_t& sessionGeneration) noexcept -> bool {
        const auto pendingEpoch = pendingEpoch_.load(std::memory_order_acquire);
        if (pendingEpoch == appliedEpoch_) return false;
        sessionGeneration = pendingSessionGeneration_.load(std::memory_order_acquire);
        appliedEpoch_ = pendingEpoch;
        return true;
    }

    void Reset() noexcept {
        pendingSessionGeneration_.store(0U, std::memory_order_release);
        pendingEpoch_.store(0U, std::memory_order_release);
        appliedEpoch_ = 0U;
    }

    [[nodiscard]] auto Pending() const noexcept -> bool {
        return pendingEpoch_.load(std::memory_order_acquire) != appliedEpoch_;
    }

private:
    std::atomic<std::uint64_t> pendingSessionGeneration_{};
    std::atomic<std::uint64_t> pendingEpoch_{};
    std::uint64_t appliedEpoch_{}; // protected by the provider state mutex
};

class GpsRouteSessionGate final {
public:
    void Reset() noexcept { activeSessionGeneration_ = 0U; }

    [[nodiscard]] auto ConsumeReset(GpsRouteResetMailbox& mailbox) noexcept
        -> bool {
        std::uint64_t sessionGeneration{};
        if (!mailbox.Consume(sessionGeneration)) return false;
        activeSessionGeneration_ = sessionGeneration;
        return true;
    }

    [[nodiscard]] auto Accepts(std::uint64_t sessionGeneration) const noexcept
        -> bool {
        return sessionGeneration != 0U
            && sessionGeneration == activeSessionGeneration_;
    }

private:
    std::uint64_t activeSessionGeneration_{}; // provider state mutex owner
};

[[nodiscard]] constexpr auto IsValidGpsRouteRequestCount(
        std::size_t count, std::size_t maximum) noexcept -> bool {
    return count != 0U && count <= maximum;
}

[[nodiscard]] constexpr auto CanAccumulateGpsRoutePoints(
        std::size_t current,
        std::size_t incoming,
        std::size_t maximum) noexcept -> bool {
    return incoming <= maximum && current <= maximum - incoming;
}

} // namespace RuffnecKk::MapSense
