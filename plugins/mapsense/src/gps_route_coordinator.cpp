#include "gps_route_coordinator.hpp"
#include "gps_route_coordinator_policy.hpp"

#include <algorithm>
#include <array>
#include <mutex>
#include <span>
#include <utility>

namespace RuffnecKk::MapSense {
namespace {

std::mutex CoordinatorMutex;
std::vector<GpsRouteProviderRequest> ActiveRequests;
std::uint64_t LastRequestTick{};
std::uint64_t AcceptedNavigationContentEpoch{};
bool HasAcceptedNavigationContent{};

void ResetAcceptedNavigationContent() noexcept {
    AcceptedNavigationContentEpoch = 0U;
    HasAcceptedNavigationContent = false;
}

[[nodiscard]] auto BuildRequests(
        const GpsRouteCoordinatorInput& input,
        std::vector<GpsRouteProviderRequest>& requests) -> std::size_t {
    std::array<NavigationGpsRouteDestination, MaximumNavigationGpsRoutePaths>
        selected{};
    const auto count = SelectNavigationGpsRouteDestinations(
        input.source.destinations,
        input.source.player,
        input.source.policy,
        selected);
    requests.clear();
    requests.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const auto& destination = selected[index].destination;
        requests.push_back({
            .identity = {
                .sessionGeneration = input.source.sessionGeneration,
                .destinationRevision = input.source.destinationRevision,
                .policyRevision = input.source.policy.revision,
                .terrainRevision = input.terrainRevision,
                .destinationId = destination.destinationId,
                .destinationKind = static_cast<std::uint8_t>(destination.kind),
                .artifact = {
                    .seed = input.seed,
                    .difficulty = input.difficulty,
                    .levelId = input.source.levelId,
                    .fromSubtileX = input.source.player.subtileX,
                    .fromSubtileY = input.source.player.subtileY,
                    .toSubtileX = destination.subtileX,
                    .toSubtileY = destination.subtileY,
                    .dataFingerprint = 0U,
                    .mode = selected[index].mode == NavigationGpsRouteMode::Walk
                        ? GpsRouteMode::Walk : GpsRouteMode::Teleport,
                },
            },
            .excelRoots = input.excelRoots,
            .tileRoots = input.tileRoots,
        });
    }
    return count;
}

[[nodiscard]] auto PublishCompleteBatch(
        std::span<const GpsRouteProviderRequest> requests,
        std::span<const GpsRouteProviderPath> paths,
        std::size_t& publishedCount,
        std::uint64_t& acceptedContentEpoch) -> bool {
    publishedCount = 0U;
    acceptedContentEpoch = 0U;
    std::vector<NavigationGpsRoutePath> navigationPaths;
    navigationPaths.reserve(paths.size());
    std::size_t pointBudget{};
    for (const auto& request : requests) {
        const auto found = std::find_if(paths.begin(), paths.end(),
            [&request](const GpsRouteProviderPath& path) noexcept {
                return SameGpsRoutePublicationScope(request.identity, path.identity)
                    && path.identity.artifact.dataFingerprint != 0U;
            });
        if (found == paths.end()) return false;
        if (found->moves.size() > MaximumNavigationGpsRoutePoints
            || pointBudget > MaximumNavigationGpsRoutePoints
                - found->moves.size()) return false;
        pointBudget += found->moves.size();
        NavigationGpsRoutePath path{};
        path.destinationId = request.identity.destinationId;
        path.policyRevision = request.identity.policyRevision;
        path.kind = static_cast<NavigationLineKind>(
            request.identity.destinationKind);
        path.mode = request.identity.artifact.mode == GpsRouteMode::Walk
            ? NavigationGpsRouteMode::Walk : NavigationGpsRouteMode::Teleport;
        path.points.reserve(found->moves.size());
        for (const auto& move : found->moves) {
            path.points.push_back({move.subtileX, move.subtileY});
        }
        path.walkGrid = found->walkGrid;
        navigationPaths.push_back(std::move(path));
    }
    const auto& identity = requests.front().identity;
    if (!PublishNavigationGpsRoutes(
            identity.sessionGeneration,
            identity.destinationRevision,
            identity.policyRevision,
            identity.artifact.levelId,
            navigationPaths.data(),
            navigationPaths.size(),
            &acceptedContentEpoch)) return false;
    publishedCount = navigationPaths.size();
    return true;
}

} // namespace

auto InitializeGpsRouteCoordinator(
        const D2RL::PluginContext* context) noexcept -> bool {
    std::scoped_lock lock(CoordinatorMutex);
    ActiveRequests.clear();
    LastRequestTick = 0U;
    ResetAcceptedNavigationContent();
    return InitializeGpsRouteProvider(context);
}

void ShutdownGpsRouteCoordinator() noexcept {
    ShutdownGpsRouteProvider();
    std::scoped_lock lock(CoordinatorMutex);
    ActiveRequests.clear();
    LastRequestTick = 0U;
    ResetAcceptedNavigationContent();
}

void ResetGpsRouteCoordinator(
        std::uint64_t sessionGeneration) noexcept {
    ResetGpsRouteProviderSession(sessionGeneration);
    std::scoped_lock lock(CoordinatorMutex);
    ActiveRequests.clear();
    LastRequestTick = 0U;
    ResetAcceptedNavigationContent();
    InvalidateNavigationGpsRoutes();
}

auto TickGpsRouteCoordinator(
        const GpsRouteCoordinatorInput& input,
        GpsRouteCoordinatorResult& result) noexcept -> bool {
    result = {};
    result.statuses.fill(GpsRouteProviderStatus::Calculating);
    if (input.source.sessionGeneration == 0U
        || input.source.destinationRevision == 0U
        || input.source.policy.revision == 0U
        || input.source.levelId <= 0
        || input.difficulty > 2U
        || input.terrainRevision == 0U) return false;
    try {
        std::vector<GpsRouteProviderRequest> candidates;
        result.selectedCount = BuildRequests(input, candidates);
        if (candidates.empty()) return true;

        std::scoped_lock lock(CoordinatorMutex);
        const auto hardChange = !ActiveRequests.empty()
            && !SameGpsRouteCoordinatorHardBatch(ActiveRequests, candidates);
        if (hardChange) {
            ActiveRequests.clear();
            ResetAcceptedNavigationContent();
            InvalidateNavigationGpsRoutes();
        }
        if (ActiveRequests.empty()) {
            if (!input.allowRequestSubmission) return true;
            result.submitted = SubmitGpsRouteProviderRequests(candidates);
            if (!result.submitted) return true;
            ActiveRequests = candidates;
            LastRequestTick = input.nowMilliseconds;
            ResetAcceptedNavigationContent();
        } else {
            result.submitted = true;
        }

        (void)AcquireGpsRouteProviderStatuses(result.statuses);
        const auto navigationContentCurrent = HasAcceptedNavigationContent
            && GetNavigationGpsRouteContentEpoch()
                == AcceptedNavigationContentEpoch;
        if (!navigationContentCurrent) {
            std::vector<GpsRouteProviderPath> paths;
            result.acquired = AcquireGpsRouteProviderPaths(paths);
            result.providerPathCount = paths.size();
            if (result.acquired
                && IsGpsRouteCoordinatorBatchComplete(ActiveRequests, paths)) {
                std::uint64_t acceptedContentEpoch{};
                result.published = PublishCompleteBatch(
                    ActiveRequests,
                    paths,
                    result.publishedPathCount,
                    acceptedContentEpoch);
                if (result.published) {
                    AcceptedNavigationContentEpoch = acceptedContentEpoch;
                    HasAcceptedNavigationContent = true;
                }
            }
        }

        if (!input.allowRequestSubmission) return true;
        const auto providerTerminal = GetGpsRouteProviderStatus()
            != GpsRouteProviderStatus::Calculating;
        const auto replanDue = ShouldGpsRouteCoordinatorReplan(
            ActiveRequests.front(), candidates.front(), LastRequestTick,
            input.nowMilliseconds, providerTerminal);
        if (replanDue && SubmitGpsRouteProviderRequests(candidates)) {
            ActiveRequests = std::move(candidates);
            LastRequestTick = input.nowMilliseconds;
            ResetAcceptedNavigationContent();
            result.statuses.fill(GpsRouteProviderStatus::Calculating);
        }
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace RuffnecKk::MapSense
