#pragma once

#include "navigation_engine.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace RuffnecKk::MapSense {

// The direct eight-segment fast path is unchanged. Reconnection can also
// validate a fixed motion prefix and at most four one-bend candidates per
// target. There is no search, allocation, or native collision access here.
inline constexpr std::size_t GpsFollowSegmentBudget = 8U;
inline constexpr std::size_t GpsFollowMaximumPrefixPoints = 64U;
inline constexpr std::size_t GpsFollowMaximumCellChecks = 2'048U;
inline constexpr std::size_t GpsFollowMaximumPassCellChecks =
    640U * MaximumNavigationGpsRoutePaths;

struct NavigationGpsFollowState final {
    std::size_t next{1U};
    NavigationGpsRoutePoint anchor{};
    NavigationGpsRoutePoint join{};
    NavigationGpsRoutePoint observed{};
    std::array<NavigationGpsRoutePoint, GpsFollowMaximumPrefixPoints> prefix{};
    std::size_t prefixCount{};
    bool initialized{};
    bool observedOnce{};
};

inline auto SameGpsPoint(NavigationGpsRoutePoint a,
        NavigationGpsRoutePoint b) noexcept -> bool {
    return a.subtileX == b.subtileX && a.subtileY == b.subtileY;
}

inline auto GpsDirectionSpan(NavigationGpsRoutePoint a,
        NavigationGpsRoutePoint b) noexcept -> std::int64_t {
    const auto dx = std::int64_t{b.subtileX} - a.subtileX;
    const auto dy = std::int64_t{b.subtileY} - a.subtileY;
    const auto ax = dx < 0 ? -dx : dx;
    const auto ay = dy < 0 ? -dy : dy;
    return dx == 0 || dy == 0 || ax == ay ? (std::max)(ax, ay) : -1;
}

inline auto OnGpsWalkSegment(NavigationGpsRoutePoint p,
        NavigationGpsRoutePoint a, NavigationGpsRoutePoint b) noexcept -> bool {
    const auto span = GpsDirectionSpan(a, b);
    return span >= 0 && span < 40
        && GpsDirectionSpan(a, p) >= 0 && GpsDirectionSpan(p, b) >= 0
        && GpsDirectionSpan(a, p) + GpsDirectionSpan(p, b) == span;
}

inline auto CheckGpsWalkConnector(const GpsRouteWalkGrid& grid,
        NavigationGpsRoutePoint from, NavigationGpsRoutePoint to,
        std::size_t& cellChecks,
        std::size_t cellBudget = GpsFollowMaximumCellChecks) noexcept -> bool {
    cellBudget = (std::min)(cellBudget, GpsFollowMaximumCellChecks);
    const auto span = GpsDirectionSpan(from, to);
    if (span < 0 || span >= 40) return false;
    const auto stepX = (to.subtileX > from.subtileX) - (to.subtileX < from.subtileX);
    const auto stepY = (to.subtileY > from.subtileY) - (to.subtileY < from.subtileY);
    for (std::int64_t step = 0; step <= span; ++step) {
        if (cellChecks >= cellBudget) return false;
        ++cellChecks;
        if (!grid.Passable(from.subtileX, from.subtileY)) return false;
        if (step != span) {
            from.subtileX += stepX;
            from.subtileY += stepY;
        }
    }
    return true;
}

// A point on the certified segment reachable on an axis from the player when
// its projection lies inside that segment. Diagonal runs use the player's X.
inline auto GpsSegmentJoin(NavigationGpsRoutePoint player,
        NavigationGpsRoutePoint a, NavigationGpsRoutePoint b) noexcept
        -> NavigationGpsRoutePoint {
    if (a.subtileX == b.subtileX) {
        return {a.subtileX, std::clamp(player.subtileY,
            (std::min)(a.subtileY, b.subtileY), (std::max)(a.subtileY, b.subtileY))};
    }
    const auto x = std::clamp(player.subtileX,
        (std::min)(a.subtileX, b.subtileX), (std::max)(a.subtileX, b.subtileX));
    if (a.subtileY == b.subtileY) return {x, a.subtileY};
    const auto direction = (b.subtileX > a.subtileX) == (b.subtileY > a.subtileY)
        ? 1 : -1;
    return {x, a.subtileY + direction * (x - a.subtileX)};
}

struct GpsFollowConnector final {
    NavigationGpsRoutePoint bend{};
    bool hasBend{};
};

struct GpsFollowBendCandidate final {
    NavigationGpsRoutePoint point{};
    std::int64_t firstSpan{};
    std::int64_t secondSpan{};
    std::size_t order{};
    bool inBoxAxisBend{};
};

inline auto TryGpsOneBendConnector(const GpsRouteWalkGrid& grid,
        NavigationGpsRoutePoint from, NavigationGpsRoutePoint to,
        std::size_t& cellChecks, std::size_t cellBudget,
        GpsFollowConnector& connector) noexcept -> bool {
    if (cellChecks >= cellBudget) return false;
    const auto dx = std::int64_t{to.subtileX} - from.subtileX;
    const auto dy = std::int64_t{to.subtileY} - from.subtileY;
    const auto absoluteX = dx < 0 ? -dx : dx;
    const auto absoluteY = dy < 0 ? -dy : dy;
    if ((std::max)(absoluteX, absoluteY) >= 40) return false;

    struct Direction final { std::int32_t x; std::int32_t y; };
    constexpr std::array<Direction, 8U> directions{{
        {-1, -1}, {-1, 0}, {-1, 1}, {0, -1},
        {0, 1}, {1, -1}, {1, 0}, {1, 1}}};
    std::array<GpsFollowBendCandidate, 64U> candidates{};
    std::size_t candidateCount{};
    std::size_t order{};
    for (const auto first : directions) {
        for (const auto second : directions) {
            const auto determinant = std::int64_t{first.x} * second.y
                - std::int64_t{first.y} * second.x;
            if (determinant == 0) {
                ++order;
                continue;
            }
            const auto firstNumerator = dx * second.y - dy * second.x;
            const auto secondNumerator = std::int64_t{first.x} * dy
                - std::int64_t{first.y} * dx;
            if (firstNumerator % determinant != 0
                || secondNumerator % determinant != 0) {
                ++order;
                continue;
            }
            const auto firstSpan = firstNumerator / determinant;
            const auto secondSpan = secondNumerator / determinant;
            if (firstSpan <= 0 || firstSpan >= 40
                || secondSpan <= 0 || secondSpan >= 40) {
                ++order;
                continue;
            }
            const NavigationGpsRoutePoint bend{
                static_cast<std::int32_t>(
                    std::int64_t{from.subtileX} + firstSpan * first.x),
                static_cast<std::int32_t>(
                    std::int64_t{from.subtileY} + firstSpan * first.y)};
            bool duplicate{};
            for (std::size_t index = 0U; index < candidateCount; ++index) {
                if (SameGpsPoint(candidates[index].point, bend)) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate && candidateCount < candidates.size()) {
                const bool horizontalFirst = bend.subtileY == from.subtileY
                    && bend.subtileX == to.subtileX;
                const bool verticalFirst = bend.subtileX == from.subtileX
                    && bend.subtileY == to.subtileY;
                candidates[candidateCount++] = {
                    bend, firstSpan, secondSpan, order,
                    horizontalFirst || verticalFirst};
            }
            ++order;
        }
    }
    std::sort(candidates.begin(),
        candidates.begin() + static_cast<std::ptrdiff_t>(candidateCount),
        [](const GpsFollowBendCandidate& left,
                const GpsFollowBendCandidate& right) noexcept {
            if (left.inBoxAxisBend != right.inBoxAxisBend) {
                return left.inBoxAxisBend;
            }
            const auto leftTotal = left.firstSpan + left.secondSpan;
            const auto rightTotal = right.firstSpan + right.secondSpan;
            if (leftTotal != rightTotal) return leftTotal < rightTotal;
            const auto leftMaximum = (std::max)(left.firstSpan, left.secondSpan);
            const auto rightMaximum = (std::max)(right.firstSpan, right.secondSpan);
            if (leftMaximum != rightMaximum) return leftMaximum < rightMaximum;
            return left.order < right.order;
        });
    const auto attempts = (std::min)(candidateCount, std::size_t{4U});
    for (std::size_t index = 0U; index < attempts; ++index) {
        if (cellChecks >= cellBudget) break;
        const auto bend = candidates[index].point;
        if (CheckGpsWalkConnector(grid, from, bend, cellChecks, cellBudget)
            && CheckGpsWalkConnector(grid, bend, to, cellChecks, cellBudget)) {
            connector = {bend, true};
            return true;
        }
    }
    return false;
}

inline auto TryGpsConnector(const GpsRouteWalkGrid& grid,
        NavigationGpsRoutePoint from, NavigationGpsRoutePoint to,
        std::size_t& cellChecks, std::size_t cellBudget,
        GpsFollowConnector& connector) noexcept -> bool {
    if (cellChecks >= cellBudget) return false;
    if (CheckGpsWalkConnector(grid, from, to, cellChecks, cellBudget)) {
        connector = {};
        return true;
    }
    return TryGpsOneBendConnector(
        grid, from, to, cellChecks, cellBudget, connector);
}

inline auto AppendGpsPrefixPoint(
        std::array<NavigationGpsRoutePoint, GpsFollowMaximumPrefixPoints>& prefix,
        std::size_t& count, NavigationGpsRoutePoint point) noexcept -> bool {
    if (count != 0U && SameGpsPoint(prefix[count - 1U], point)) return true;
    if (count >= 2U) {
        const auto firstSpan = GpsDirectionSpan(prefix[count - 2U], prefix[count - 1U]);
        const auto secondSpan = GpsDirectionSpan(prefix[count - 1U], point);
        const auto combinedSpan = GpsDirectionSpan(prefix[count - 2U], point);
        // Both constituent edges were already grid-certified. When they run
        // in the same direction, replacing their shared point preserves that
        // exact certificate while keeping long straight motion histories small.
        if (firstSpan >= 0 && secondSpan >= 0 && combinedSpan >= 0
            && combinedSpan < 40 && firstSpan + secondSpan == combinedSpan) {
            prefix[count - 1U] = point;
            return true;
        }
    }
    if (count >= prefix.size()) return false;
    prefix[count++] = point;
    return true;
}

inline auto AssignGpsFollowPrefix(NavigationGpsFollowState& state,
        NavigationGpsRoutePoint player, NavigationGpsRoutePoint join,
        const GpsFollowConnector& connector) noexcept -> bool {
    state.anchor = player;
    state.join = join;
    state.prefix[0] = player;
    state.prefixCount = 1U;
    if (connector.hasBend && !SameGpsPoint(state.prefix[0], connector.bend)) {
        state.prefix[state.prefixCount++] = connector.bend;
    }
    if (!SameGpsPoint(state.prefix[state.prefixCount - 1U], join)) {
        state.prefix[state.prefixCount++] = join;
    }
    return true;
}

inline auto PreserveGpsFollowPrefix(const GpsRouteWalkGrid& grid,
        NavigationGpsRoutePoint player, NavigationGpsFollowState& state,
        std::size_t& cellChecks, std::size_t cellBudget) noexcept -> bool {
    if (state.prefixCount == 0U || state.prefixCount > state.prefix.size()
        || !SameGpsPoint(state.prefix.front(), state.anchor)
        || !SameGpsPoint(state.prefix[state.prefixCount - 1U], state.join)) {
        return false;
    }
    for (std::size_t index = state.prefixCount; index-- > 0U;) {
        if (!SameGpsPoint(state.prefix[index], player)) continue;
        std::array<NavigationGpsRoutePoint, GpsFollowMaximumPrefixPoints> prefix{};
        const auto count = state.prefixCount - index;
        std::copy_n(state.prefix.begin() + static_cast<std::ptrdiff_t>(index),
            count, prefix.begin());
        state.anchor = player;
        state.prefix = prefix;
        state.prefixCount = count;
        return true;
    }

    GpsFollowConnector connector{};
    if (!TryGpsConnector(
            grid, player, state.anchor, cellChecks, cellBudget, connector)) {
        return false;
    }
    std::size_t suffix{};
    if (connector.hasBend) {
        for (std::size_t index = state.prefixCount; index-- > 0U;) {
            if (SameGpsPoint(state.prefix[index], connector.bend)) {
                suffix = index;
                break;
            }
        }
    }
    std::array<NavigationGpsRoutePoint, GpsFollowMaximumPrefixPoints> prefix{};
    std::size_t count{};
    if (!AppendGpsPrefixPoint(prefix, count, player)
        || (connector.hasBend
            && !AppendGpsPrefixPoint(prefix, count, connector.bend))) {
        return false;
    }
    for (std::size_t index = suffix; index < state.prefixCount; ++index) {
        if (!AppendGpsPrefixPoint(prefix, count, state.prefix[index])) return false;
    }
    state.anchor = player;
    state.prefix = prefix;
    state.prefixCount = count;
    return true;
}

inline auto RestoreGpsFollowPrefixFromHistory(const GpsRouteWalkGrid& grid,
        NavigationGpsRoutePoint player, NavigationGpsRoutePoint routeOrigin,
        std::span<const NavigationGpsRoutePoint> motionHistory,
        NavigationGpsFollowState& state, std::size_t& cellChecks,
        std::size_t cellBudget) noexcept -> bool {
    std::size_t originIndex = motionHistory.size();
    for (std::size_t index = motionHistory.size(); index-- > 0U;) {
        if (SameGpsPoint(motionHistory[index], routeOrigin)) {
            originIndex = index;
            break;
        }
    }
    if (originIndex == motionHistory.size()) return false;

    std::array<NavigationGpsRoutePoint, GpsFollowMaximumPrefixPoints> prefix{};
    std::size_t count{};
    if (!AppendGpsPrefixPoint(prefix, count, player)) return false;
    auto tail = player;
    for (std::size_t index = motionHistory.size(); index-- > originIndex;) {
        if (cellChecks >= cellBudget) return false;
        const auto target = motionHistory[index];
        if (SameGpsPoint(tail, target)) continue;
        bool erasedLoop{};
        for (std::size_t existing = count; existing-- > 0U;) {
            if (!SameGpsPoint(prefix[existing], target)) continue;
            count = existing + 1U;
            tail = target;
            erasedLoop = true;
            break;
        }
        if (erasedLoop) continue;
        GpsFollowConnector connector{};
        if (!TryGpsConnector(
                grid, tail, target, cellChecks, cellBudget, connector)) {
            return false;
        }
        if (connector.hasBend) {
            std::size_t existingBend = count;
            for (std::size_t existing = count; existing-- > 0U;) {
                if (SameGpsPoint(prefix[existing], connector.bend)) {
                    existingBend = existing;
                    break;
                }
            }
            if (existingBend != count) count = existingBend + 1U;
            else if (!AppendGpsPrefixPoint(prefix, count, connector.bend)) return false;
        }
        if (!AppendGpsPrefixPoint(prefix, count, target)) return false;
        tail = target;
    }
    // Arbitrarily winding histories that still require more than 64 certified
    // points remain a bounded failure; a later pass or helper route can retry.
    if (!SameGpsPoint(tail, routeOrigin)) return false;
    state.anchor = player;
    state.join = routeOrigin;
    state.prefix = prefix;
    state.prefixCount = count;
    return true;
}

inline void FollowNavigationGpsRoute(const NavigationGpsRoutePath& route,
        NavigationGpsRoutePoint player, NavigationGpsFollowState& state,
        std::size_t& cellChecks,
        std::span<const NavigationGpsRoutePoint> motionHistory = {},
        std::size_t cellBudget = GpsFollowMaximumCellChecks) noexcept {
    if (route.points.empty()) return;
    if (!state.initialized) {
        state.anchor = state.join = route.points.front();
        state.initialized = true;
    }
    cellBudget = (std::min)(cellBudget, GpsFollowMaximumCellChecks);
    if (cellBudget == 0U) return;
    if (state.observedOnce && SameGpsPoint(state.observed, player)) return;
    state.observed = player;
    state.observedOnce = true;
    const auto first = state.next;
    const auto end = (std::min)(route.points.size(), first + GpsFollowSegmentBudget);

    if (route.mode == NavigationGpsRouteMode::Teleport) {
        for (auto index = first; index < end; ++index) {
            const auto b = route.points[index];
            if (SameGpsPoint(player, b)) {
                state.anchor = state.join = player;
                state.next = index + 1U;
            }
        }
        return;
    }
    if (route.walkGrid == nullptr) {
        for (auto index = first; index < end; ++index) {
            const auto a = index == first ? state.join : route.points[index - 1U];
            const auto b = route.points[index];
            if (OnGpsWalkSegment(player, a, b)) {
                state.anchor = state.join = player;
                state.next = SameGpsPoint(player, b) ? index + 1U : index;
            }
        }
        return;
    }

    bool directConnected{};
    for (auto index = first; index < end; ++index) {
        if (cellChecks >= cellBudget) break;
        const auto a = route.points[index - 1U];
        const auto b = route.points[index];
        const auto span = GpsDirectionSpan(a, b);
        if (span < 0 || span >= 40) continue;
        auto join = b;
        bool connected = CheckGpsWalkConnector(
            *route.walkGrid, player, join, cellChecks, cellBudget);
        if (!connected) {
            join = GpsSegmentJoin(player, a, b);
            connected = CheckGpsWalkConnector(
                *route.walkGrid, player, join, cellChecks, cellBudget);
        }
        if (connected) {
            const GpsFollowConnector connector{};
            if (AssignGpsFollowPrefix(state, player, join, connector)) {
                state.next = SameGpsPoint(player, b) ? index + 1U : index;
                directConnected = true;
            }
        }
    }
    if (directConnected) return;

    const bool retainedPrefix = PreserveGpsFollowPrefix(
        *route.walkGrid, player, state, cellChecks, cellBudget);
    if (!retainedPrefix && state.prefixCount == 0U) {
        static_cast<void>(RestoreGpsFollowPrefixFromHistory(
            *route.walkGrid, player, route.points.front(), motionHistory,
            state, cellChecks, cellBudget));
    }

    const auto bendEnd = (std::min)(route.points.size(), first + 2U);
    for (auto index = first; index < bendEnd; ++index) {
        if (cellChecks >= cellBudget) break;
        const auto a = route.points[index - 1U];
        const auto b = route.points[index];
        const auto span = GpsDirectionSpan(a, b);
        if (span < 0 || span >= 40) continue;
        const std::array<NavigationGpsRoutePoint, 2U> joins{{
            b, GpsSegmentJoin(player, a, b)}};
        for (const auto join : joins) {
            if (cellChecks >= cellBudget) break;
            GpsFollowConnector connector{};
            if (!TryGpsOneBendConnector(*route.walkGrid, player, join,
                    cellChecks, cellBudget, connector)) {
                continue;
            }
            if (AssignGpsFollowPrefix(state, player, join, connector)) {
                state.next = SameGpsPoint(player, b) ? index + 1U : index;
                return;
            }
        }
    }
    if (cellChecks >= cellBudget
        && (state.prefixCount == 0U || !SameGpsPoint(state.anchor, player))) {
        state.observedOnce = false;
    }
}

} // namespace RuffnecKk::MapSense
