#include "navigation_gps_follow.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace RuffnecKk::MapSense;

namespace {
std::size_t ProjectCalls{};
void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
auto Grid() -> std::shared_ptr<GpsRouteWalkGrid> {
    auto grid = std::make_shared<GpsRouteWalkGrid>();
    grid->width = grid->height = 128U;
    grid->bits.assign(128U * 128U / 8U, 0xffU);
    return grid;
}
void Block(GpsRouteWalkGrid& grid, std::uint32_t x, std::uint32_t y) {
    const auto bit = y * grid.width + x;
    grid.bits[bit / 8U] &= static_cast<std::uint8_t>(~(1U << (bit % 8U)));
}
auto Project(void*, std::int32_t x, std::int32_t y,
    NavigationNativePoint& result) noexcept -> bool {
    ++ProjectCalls;
    result = {x, y}; return true;
}
auto Pass(std::int32_t x, std::int32_t y) -> NavigationAutomapPass {
    NavigationNativePoint client{};
    Require(ConvertNavigationSubtileToClientCoordinates(x, y, client), "client conversion");
    return {.currentLevelId = 4, .playerClientX = client.x,
        .playerClientY = client.y, .hasPlayerSubtile = true,
        .playerSubtile = {x, y}, .nativeWidth = 4000, .nativeHeight = 4000,
        .clipLeft = -2000, .clipTop = -2000, .clipWidth = 4000,
        .clipHeight = 4000, .projectClient = &Project,
        .borrowedAutomapContext = &ProjectCalls};
}

void TestBoundedFollowing() {
    auto grid = Grid();
    {
        auto cornerGrid = Grid();
        Block(*cornerGrid, 16U, 11U);
        NavigationGpsRoutePath straight{};
        straight.points = {{10, 10}, {30, 10}};
        straight.walkGrid = cornerGrid;
        NavigationGpsFollowState progress{};
        std::size_t work{};
        FollowNavigationGpsRoute(straight, {16, 12}, progress, work);
        Require(SameGpsPoint(progress.anchor, {16, 12}),
            "reconnection must find a checked turn when the direct route join is blocked");
    }
    {
        NavigationGpsRoutePath straight{};
        straight.points = {{10, 10}, {30, 10}};
        straight.walkGrid = grid;
        NavigationGpsFollowState progress{};
        std::size_t work{};
        FollowNavigationGpsRoute(straight, {15, 10}, progress, work);
        Require(SameGpsPoint(progress.anchor, {15, 10}), "initial straight-route anchor");
        FollowNavigationGpsRoute(straight, {16, 11}, progress, work);
        Require(SameGpsPoint(progress.anchor, {16, 11})
            && SameGpsPoint(progress.join, {16, 10}),
            "sideways step must reconnect to the retained segment, not freeze at the prior anchor");
        FollowNavigationGpsRoute(straight, {17, 12}, progress, work);
        Require(SameGpsPoint(progress.anchor, {17, 12})
            && SameGpsPoint(progress.join, {17, 10}), "repeated sideways movement must continue following");
    }
    NavigationGpsRoutePath route{};
    route.points = {{10, 10}, {30, 10}, {30, 30}, {50, 30}};
    route.walkGrid = grid;
    NavigationGpsFollowState state{};
    std::size_t checks{};
    FollowNavigationGpsRoute(route, {15, 10}, state, checks);
    Require(SameGpsPoint(state.anchor, {15, 10}), "walking must anchor during the current pass");
    Require(checks <= GpsFollowMaximumCellChecks, "bounded first pass");
    checks = 0U;
    FollowNavigationGpsRoute(route, {15, 10}, state, checks);
    Require(checks == 0U, "stationary pass must not repeat collision checks");
    FollowNavigationGpsRoute(route, {16, 11}, state, checks);
    Require(SameGpsPoint(state.anchor, {16, 11}), "nearby off-route player must attach through cached open cells");
    Require(checks <= GpsFollowMaximumCellChecks, "bounded moving pass");

    checks = 0U;
    Block(*grid, 20U, 10U);
    Require(!CheckGpsWalkConnector(*grid, {15, 10}, {25, 10}, checks), "wall must reject connector");
    Require(!CheckGpsWalkConnector(*grid, {1, 1}, {4, 3}, checks), "arbitrary Bresenham shortcut forbidden");
    Require(!CheckGpsWalkConnector(*grid, {1, 1}, {41, 1}, checks), "40-cell command forbidden");
    Require(CheckGpsWalkConnector(*grid, {1, 1}, {40, 1}, checks), "39-cell command accepted");
    // The exported map already includes the player footprint. A* checks the
    // diagonal destination, not an invented extra orthogonal-corner rule.
    Block(*grid, 2U, 1U);
    Require(CheckGpsWalkConnector(*grid, {1, 1}, {2, 2}, checks), "helper diagonal semantics must be preserved");
    Require(!CheckGpsWalkConnector(*grid, {-1, 0}, {1, 0}, checks), "outside grid rejected");

    route.walkGrid.reset();
    route.points = {{10, 10}, {30, 10}, {30, 30}};
    state = {};
    checks = 0U;
    FollowNavigationGpsRoute(route, {15, 10}, state, checks);
    Require(SameGpsPoint(state.anchor, {15, 10}), "legacy sparse run interior must trim");
    FollowNavigationGpsRoute(route, {15, 11}, state, checks);
    Require(SameGpsPoint(state.anchor, {15, 10}), "legacy data must not invent off-route connections");
    FollowNavigationGpsRoute(route, {12, 10}, state, checks);
    Require(SameGpsPoint(state.anchor, {15, 10}), "legacy progress must not rewind");

    route.mode = NavigationGpsRouteMode::Teleport;
    route.walkGrid = grid;
    state = {};
    FollowNavigationGpsRoute(route, {15, 10}, state, checks);
    Require(SameGpsPoint(state.anchor, {10, 10}), "teleport must not reuse walking connector rules");
    FollowNavigationGpsRoute(route, {30, 10}, state, checks);
    Require(state.next == 2U && SameGpsPoint(state.anchor, {30, 10}), "confirmed teleport landing trims immediately");
}

void RequireCertifiedPrefix(const NavigationGpsFollowState& state,
        const GpsRouteWalkGrid& grid) {
    Require(state.prefixCount > 0U && state.prefixCount <= state.prefix.size(),
        "connector prefix must have bounded verified points");
    Require(SameGpsPoint(state.prefix[0], state.anchor)
        && SameGpsPoint(state.prefix[state.prefixCount - 1U], state.join),
        "prefix endpoints must match the player anchor and route join");
    for (std::size_t index = 1U; index < state.prefixCount; ++index) {
        std::size_t checks{};
        Require(CheckGpsWalkConnector(grid, state.prefix[index - 1U],
            state.prefix[index], checks), "every retained connector edge must be collision-certified");
    }
}

void TestTurnAndMotionContinuity() {
    auto grid = Grid();
    for (std::uint32_t y = 0U; y <= 20U; ++y) Block(*grid, 20U, y);
    NavigationGpsRoutePath route{};
    route.points = {{15, 10}, {19, 10}};
    route.walkGrid = grid;
    NavigationGpsFollowState state{};
    const NavigationGpsRoutePoint history[]{{15, 10}, {15, 25}, {25, 25}, {25, 15}};
    for (const auto player : history) {
        std::size_t checks{};
        FollowNavigationGpsRoute(route, player, state, checks);
        Require(SameGpsPoint(state.anchor, player),
            "verified moving prefix must keep following around a multi-turn wall");
        Require(checks <= GpsFollowMaximumCellChecks, "moving-prefix work bound");
        RequireCertifiedPrefix(state, *grid);
    }
    std::size_t stationaryChecks{};
    FollowNavigationGpsRoute(route, history[3], state, stationaryChecks);
    Require(stationaryChecks == 0U, "retained continuity must be free while stationary");
    NavigationGpsFollowState replacement{};
    std::size_t checks{};
    FollowNavigationGpsRoute(route, history[3], replacement, checks, history);
    Require(SameGpsPoint(replacement.anchor, history[3]),
        "late helper origin must reconnect through verified recent movement");
    RequireCertifiedPrefix(replacement, *grid);

    // Player history alone is not a walkability certificate: break both
    // access corridors so neither history nor a bend may fabricate a path.
    auto blocked = Grid();
    for (std::uint32_t y = 0U; y < 128U; ++y) Block(*blocked, 20U, y);
    route.walkGrid = blocked;
    replacement = {};
    checks = 0U;
    FollowNavigationGpsRoute(route, history[3], replacement, checks, history);
    Require(!SameGpsPoint(replacement.anchor, history[3]),
        "history must not bridge an impassable wall");
    Require(checks <= GpsFollowMaximumCellChecks, "failed history work bound");

    replacement = {};
    checks = 0U;
    FollowNavigationGpsRoute(route, history[3], replacement, checks, history, 1U);
    Require(checks <= 1U, "per-pass remaining work budget must be honored");
}

void TestNativePassAndReplacement() {
    InitializeNavigationEngine();
    ResetNavigationSession(91U);
    ResetNavigationLevel(91U, 4);
    auto policy = AcquireNavigationLinePolicySnapshot();
    policy.families[NavigationLineKindIndex(NavigationLineKind::Waypoint)] =
        {true, NavigationLineMode::GpsWalk};
    Require(PublishNavigationLinePolicy(policy.families), "publish GPS policy");
    policy = AcquireNavigationLinePolicySnapshot();
    const NavigationSubtileDestination destination{.destinationId = 42U,
        .subtileX = 30, .subtileY = 10, .kind = NavigationLineKind::Waypoint};
    Require(PublishNavigationDestinations(91U, 4, &destination, 1U), "publish destination");
    static_cast<void>(ObserveNavigationAutomapPass(Pass(10, 10)));
    NavigationGpsRouteSourceSnapshot source{};
    Require(AcquireNavigationGpsRouteSourceSnapshot(source), "capture source");
    NavigationGpsRoutePath route{.destinationId = 42U,
        .policyRevision = policy.revision, .kind = NavigationLineKind::Waypoint,
        .points = {{10, 10}, {30, 10}}, .walkGrid = Grid()};
    std::uint64_t accepted{};
    Require(PublishNavigationGpsRoutes(91U, source.destinationRevision,
        policy.revision, 4, &route, 1U, &accepted), "publish route");
    Require(accepted == GetNavigationGpsRouteContentEpoch(), "exact accepted content epoch");
    ProjectCalls = 0U;
    static_cast<void>(ObserveNavigationAutomapPass(Pass(15, 10)));
    Require(ProjectCalls == 2U, "reuse native player projection and project only route endpoint");
    std::vector<NavigationGpsRouteSegmentSnapshot> segments;
    Require(AcquireNavigationGpsRouteSegmentSnapshots(segments) == 1U, "single followed segment");
    Require(segments.front().startX == 80 && segments.front().startY == 200,
        "same native pass must use current player, without helper completion");
    auto fractionalPass = Pass(15, 10);
    fractionalPass.playerClientX += 1;
    static_cast<void>(ObserveNavigationAutomapPass(fractionalPass));
    Require(AcquireNavigationGpsRouteSegmentSnapshots(segments) == 1U
        && segments.front().startX == 81,
        "within-subtile motion must follow the native player drawing immediately");
    static_cast<void>(ObserveNavigationAutomapPass(Pass(16, 11)));
    Require(AcquireNavigationGpsRouteSegmentSnapshots(segments) == 2U
        && segments.front().startX == 80 && segments.front().startY == 216,
        "sideways step must update the visible native route in the same pass");
    route.points = {{15, 10}, {25, 10}, {30, 10}};
    Require(PublishNavigationGpsRoutes(91U, source.destinationRevision,
        policy.revision, 4, &route, 1U), "replace compatible route");
    Require(AcquireNavigationGpsRouteSegmentSnapshots(segments) == 2U,
        "compatible replacement must retain the preceding frame");
    static_cast<void>(ObserveNavigationAutomapPass(Pass(16, 10)));
    Require(AcquireNavigationGpsRouteSegmentSnapshots(segments) > 0U,
        "replacement follows next position");
    static_cast<void>(ObserveNavigationAutomapPass(Pass(29, 10)));
    Require(AcquireNavigationGpsRouteSegmentSnapshots(segments) == 1U
        && segments.front().startX == 304 && segments.front().startY == 312,
        "approach to final endpoint must continue following on each pass");
    static_cast<void>(ObserveNavigationAutomapPass(Pass(30, 10)));
    Require(AcquireNavigationGpsRouteSegmentSnapshots(segments) == 0U,
        "arrival removes travelled route instead of retaining stale segments");
    InvalidateNavigationGpsRoutes();
    Require(GetNavigationGpsRouteContentEpoch() != accepted, "invalidation revokes content epoch");
    ResetNavigationLevel(91U, 5);
    Require(!PublishNavigationGpsRoutes(91U, source.destinationRevision,
        policy.revision, 4, &route, 1U), "old-level route cannot revive");
    ShutdownNavigationEngine();
}

void TestDisconnectedRouteDoesNotLeaveAnOldAnchor() {
    InitializeNavigationEngine();
    ResetNavigationSession(92U);
    ResetNavigationLevel(92U, 4);
    auto policy = AcquireNavigationLinePolicySnapshot();
    policy.families[NavigationLineKindIndex(NavigationLineKind::Waypoint)] =
        {true, NavigationLineMode::GpsWalk};
    Require(PublishNavigationLinePolicy(policy.families), "disconnection policy");
    policy = AcquireNavigationLinePolicySnapshot();
    const NavigationSubtileDestination destinations[]{
        {.destinationId = 1U, .subtileX = 19, .subtileY = 10},
        {.destinationId = 2U, .subtileX = 60, .subtileY = 10}};
    Require(PublishNavigationDestinations(92U, 4, destinations, 2U), "disconnection destinations");
    static_cast<void>(ObserveNavigationAutomapPass(Pass(15, 10)));
    NavigationGpsRouteSourceSnapshot source{};
    Require(AcquireNavigationGpsRouteSourceSnapshot(source), "disconnection source");
    auto grid = Grid();
    for (std::uint32_t y = 0U; y <= 40U; ++y) Block(*grid, 20U, y);
    NavigationGpsRoutePath routes[]{
        {.destinationId = 1U, .policyRevision = policy.revision,
            .points = {{15, 10}, {19, 10}}, .walkGrid = grid},
        {.destinationId = 2U, .policyRevision = policy.revision,
            .points = {{15, 10}, {15, 45}, {40, 45}, {40, 10}, {60, 10}},
            .walkGrid = grid}};
    Require(PublishNavigationGpsRoutes(92U, source.destinationRevision,
        policy.revision, 4, routes, 2U), "disconnection routes");
    static_cast<void>(ObserveNavigationAutomapPass(Pass(15, 10)));
    std::vector<NavigationGpsRouteSegmentSnapshot> segments;
    Require(AcquireNavigationGpsRouteSegmentSnapshots(segments) > 0U,
        "initial connected routes draw");
    static_cast<void>(ObserveNavigationAutomapPass(Pass(40, 10)));
    Require(AcquireNavigationGpsRouteSegmentSnapshots(segments) == 1U
        && segments.front().destinationId == 2U
        && segments.front().startX == 480 && segments.front().startY == 400,
        "disconnected route must not leave its starting point behind; connected route stays visible");
    static_cast<void>(ObserveNavigationAutomapPass(Pass(100, 100)));
    Require(AcquireNavigationGpsRouteSegmentSnapshots(segments) == 0U,
        "all disconnected routes must clear the old frame immediately");
    static_cast<void>(ObserveNavigationAutomapPass(Pass(100, 100)));
    Require(AcquireNavigationGpsRouteSegmentSnapshots(segments) == 0U,
        "stationary observation must not revive a stale anchor");
    routes[0].points = {{15, 10}, {18, 10}, {19, 10}};
    Require(PublishNavigationGpsRoutes(92U, source.destinationRevision,
        policy.revision, 4, routes, 2U), "late stale-origin replacement");
    static_cast<void>(ObserveNavigationAutomapPass(Pass(100, 100)));
    Require(AcquireNavigationGpsRouteSegmentSnapshots(segments) == 0U
        && !WantsNavigationGpsRouteFrame(),
        "late helper replacement must not revive its old request origin");
    static_cast<void>(ObserveNavigationAutomapPass(Pass(40, 10)));
    Require(AcquireNavigationGpsRouteSegmentSnapshots(segments) == 1U
        && segments.front().destinationId == 2U,
        "safe reconnection must restore the route without a new helper batch");
    ShutdownNavigationEngine();
}

void TestLateHelperKeepsMovementContinuity() {
    InitializeNavigationEngine();
    ResetNavigationSession(93U);
    ResetNavigationLevel(93U, 4);
    auto policy = AcquireNavigationLinePolicySnapshot();
    policy.families[NavigationLineKindIndex(NavigationLineKind::Waypoint)] =
        {true, NavigationLineMode::GpsWalk};
    Require(PublishNavigationLinePolicy(policy.families), "history policy");
    policy = AcquireNavigationLinePolicySnapshot();
    const NavigationSubtileDestination destination{.destinationId = 1U,
        .subtileX = 19, .subtileY = 10};
    Require(PublishNavigationDestinations(93U, 4, &destination, 1U), "history destination");
    for (const auto point : {NavigationGpsRoutePoint{15, 10}, {15, 25}, {25, 25}, {25, 15}}) {
        static_cast<void>(ObserveNavigationAutomapPass(Pass(point.subtileX, point.subtileY)));
    }
    NavigationGpsRouteSourceSnapshot source{};
    Require(AcquireNavigationGpsRouteSourceSnapshot(source), "history source");
    auto grid = Grid();
    for (std::uint32_t y = 0U; y <= 20U; ++y) Block(*grid, 20U, y);
    NavigationGpsRoutePath route{.destinationId = 1U,
        .policyRevision = policy.revision, .points = {{15, 10}, {19, 10}}, .walkGrid = grid};
    Require(PublishNavigationGpsRoutes(93U, source.destinationRevision,
        policy.revision, 4, &route, 1U), "late helper publication");
    static_cast<void>(ObserveNavigationAutomapPass(Pass(25, 15)));
    std::vector<NavigationGpsRouteSegmentSnapshot> segments;
    Require(AcquireNavigationGpsRouteSegmentSnapshots(segments) > 0U
        && segments.front().startX == 160 && segments.front().startY == 320,
        "late helper route must stay connected through observed, grid-verified movement");
    route.points = {{15, 10}, {17, 10}, {19, 10}};
    Require(PublishNavigationGpsRoutes(93U, source.destinationRevision,
        policy.revision, 4, &route, 1U), "second late helper publication");
    static_cast<void>(ObserveNavigationAutomapPass(Pass(25, 15)));
    Require(AcquireNavigationGpsRouteSegmentSnapshots(segments) > 0U
        && segments.front().startX == 160, "compatible replacement retains movement history");

    ResetNavigationLevel(93U, 5);
    Require(PublishNavigationDestinations(93U, 5, &destination, 1U), "new-level history destination");
    auto newLevelPass = Pass(25, 15);
    newLevelPass.currentLevelId = 5;
    static_cast<void>(ObserveNavigationAutomapPass(newLevelPass));
    Require(AcquireNavigationGpsRouteSourceSnapshot(source), "new-level source");
    Require(PublishNavigationGpsRoutes(93U, source.destinationRevision,
        policy.revision, 5, &route, 1U), "new-level route");
    static_cast<void>(ObserveNavigationAutomapPass(newLevelPass));
    Require(AcquireNavigationGpsRouteSegmentSnapshots(segments) == 0U,
        "movement history from the previous level must never reconnect a new-level route");
    ShutdownNavigationEngine();
}

void TestLongHistoryAndBudgetRetry() {
    auto grid = Grid();
    std::array<NavigationGpsRoutePoint, 128U> history{};
    for (std::size_t i = 0; i < history.size(); ++i) {
        history[i] = {static_cast<std::int32_t>(i), 10};
    }
    for (const auto count : {65U, 128U}) {
        NavigationGpsFollowState state{};
        std::size_t checks{};
        Require(RestoreGpsFollowPrefixFromHistory(*grid, history[count - 1U],
            history.front(), std::span<const NavigationGpsRoutePoint>(history.data(), count),
            state, checks, GpsFollowMaximumCellChecks),
            "long straight movement history must compact into a certified connection");
        RequireCertifiedPrefix(state, *grid);
    }
    NavigationGpsRoutePath route{.points = {{10, 10}, {30, 10}}, .walkGrid = grid};
    NavigationGpsFollowState state{};
    std::size_t checks{};
    FollowNavigationGpsRoute(route, {15, 12}, state, checks, {}, 0U);
    Require(checks == 0U && !state.observedOnce, "zero budget must permit later retry");
    FollowNavigationGpsRoute(route, {15, 12}, state, checks, {}, 1U);
    Require(checks <= 1U && !state.observedOnce, "exhausted incomplete connection must permit retry");
    checks = 0U;
    FollowNavigationGpsRoute(route, {15, 12}, state, checks);
    Require(SameGpsPoint(state.anchor, {15, 12}), "stationary retry must attach after receiving budget");
    RequireCertifiedPrefix(state, *grid);
}

void Benchmark() {
    NavigationGpsRoutePath route{};
    route.walkGrid = Grid();
    for (std::int32_t y = 10; y < 100; y += 10) {
        route.points.push_back({10, y}); route.points.push_back({40, y});
    }
    constexpr std::size_t iterations = 100000U;
    std::size_t totalChecks{};
    std::size_t worstChecks{};
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        NavigationGpsFollowState state{};
        std::size_t checks{};
        FollowNavigationGpsRoute(route, {11 + static_cast<std::int32_t>(iteration % 20U), 11}, state, checks);
        totalChecks += checks;
        worstChecks = (std::max)(worstChecks, checks);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
    Require(worstChecks <= GpsFollowMaximumCellChecks, "deterministic collision-work ceiling");
    std::printf("follow benchmark: %zu moving route observations, %.3f us/route, max %zu checks, total %zu\n",
        iterations, static_cast<double>(elapsed) / static_cast<double>(iterations) / 1000.0,
        worstChecks, totalChecks);

    auto adverse = Grid();
    Block(*adverse, 25U, 64U);
    Block(*adverse, 64U, 25U);
    route.walkGrid = adverse;
    route.points.clear();
    for (std::size_t index = 0U; index <= GpsFollowSegmentBudget; ++index) {
        route.points.push_back(index % 2U == 0U
            ? NavigationGpsRoutePoint{25, 64} : NavigationGpsRoutePoint{64, 25});
    }
    constexpr std::size_t passes = 1000U;
    std::size_t aggregateChecks{};
    const auto adverseStart = std::chrono::steady_clock::now();
    for (std::size_t pass = 0U; pass < passes; ++pass) {
        std::size_t passChecks{};
        for (std::size_t index = 0U; index < MaximumNavigationGpsRoutePaths; ++index) {
            NavigationGpsFollowState state{};
            std::size_t checks{};
            const auto budget = (std::min)(GpsFollowMaximumCellChecks,
                GpsFollowMaximumPassCellChecks - passChecks);
            FollowNavigationGpsRoute(route, {64, 64}, state, checks, {}, budget);
            Require(checks <= budget, "adverse fixture must honor the shared remaining budget");
            passChecks += checks;
        }
        Require(passChecks == GpsFollowMaximumPassCellChecks, "64-route observation work ceiling");
        aggregateChecks += passChecks;
    }
    const auto adverseElapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - adverseStart).count();
    std::printf("adverse follow benchmark: %zu observations x 64 routes, %.3f us/observation, %zu checks/observation, total %zu\n",
        passes, static_cast<double>(adverseElapsed) / static_cast<double>(passes) / 1000.0,
        GpsFollowMaximumPassCellChecks, aggregateChecks);
}
}

int main() {
    TestLongHistoryAndBudgetRetry();
    TestBoundedFollowing();
    TestTurnAndMotionContinuity();
    TestNativePassAndReplacement();
    TestDisconnectedRouteDoesNotLeaveAnOldAnchor();
    TestLateHelperKeepsMovementContinuity();
    Benchmark();
    std::puts("GPS following tests passed");
}
