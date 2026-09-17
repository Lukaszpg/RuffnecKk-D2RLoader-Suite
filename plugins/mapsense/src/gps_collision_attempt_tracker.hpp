#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace RuffnecKk::MapSense {

// D2R 3.3's governed level ids occupy a much smaller range. Keep a fixed
// diagnostic-only set so a future level expansion fails closed rather than
// retriggering a CollMap transaction for a level already seen this session.
inline constexpr std::size_t GpsCollisionProbeAttemptLevelCapacity = 256U;

class GpsCollisionProbeAttemptTracker final {
public:
    void BeginSession(std::uint64_t generation) noexcept {
        generation_ = generation;
        levelCount_ = 0U;
    }

    // Reservations are made before a caller starts its native work. A failed
    // reservation must therefore leave the caller's rooms and automap alone.
    [[nodiscard]] auto ReserveFirstAttempt(
            std::uint64_t generation,
            std::int32_t levelId) noexcept -> bool {
        if (generation == 0U || generation != generation_ || levelId <= 0) {
            return false;
        }
        for (std::size_t index = 0U; index < levelCount_; ++index) {
            if (levels_[index] == levelId) return false;
        }
        if (levelCount_ >= levels_.size()) return false;
        levels_[levelCount_++] = levelId;
        return true;
    }

private:
    std::uint64_t generation_{};
    std::array<std::int32_t, GpsCollisionProbeAttemptLevelCapacity> levels_{};
    std::size_t levelCount_{};
};

} // namespace RuffnecKk::MapSense
