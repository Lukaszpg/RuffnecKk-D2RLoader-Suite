#include "compact_layout.hpp"
#include "ui_scale_contract.hpp"

#include <cmath>
#include <limits>

using namespace RuffnecKk::VendorStockRefresh;
#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (false)

int main() {
    const WidgetRect gold{421, 1305, 313, 58};
    const WidgetGeometry original{{877, 1277, 112, 112}, 1.0F};
    const auto compact = CompactBelow(gold, original);
    CHECK(compact.valid);
    CHECK(compact.geometry.rect.x == 549);
    CHECK(compact.geometry.rect.y == 1391);
    CHECK(compact.geometry.scale == 0.5F);
    CHECK(compact.geometry.rect.width == original.rect.width);
    CHECK(compact.geometry.rect.height == original.rect.height);
    // Visual center differs from the gold center by at most integer rounding.
    CHECK(std::abs((compact.geometry.rect.x + 28.0) - (gold.x + gold.width / 2.0)) <= 0.5);
    CHECK(compact.geometry.rect.y + 56 <= 1450); // Reference layout's lower trim.

    RefreshLayoutState state;
    state.Observe(original);
    for (int i = 0; i != 100; ++i) {
        const auto placed = CompactBelow(gold, state.original);
        CHECK(SameGeometry(placed.geometry, compact.geometry));
        state.Applied(placed.geometry);
        state.Observe(placed.geometry);
        CHECK(SameGeometry(state.original, original));
    }
    // On a failed native restore, leave state intact for another attempt.
    CHECK(state.hasApplied);
    CHECK(SameGeometry(state.original, original));
    state.Restored();
    state.Observe(original);
    CHECK(!state.hasApplied);
    CHECK(SameGeometry(state.original, original));

    const WidgetGeometry modded{{1100, 1400, 160, 160}, 0.75F};
    state.Applied(compact.geometry);
    state.Observe(modded); // A layout rebuild replaces both baseline and scale.
    CHECK(!state.hasApplied);
    CHECK(SameGeometry(state.original, modded));
    const auto modPlacement = CompactBelow({600, 1500, 500, 80}, state.original);
    CHECK(modPlacement.valid);
    CHECK(modPlacement.geometry.scale == 0.375F);
    CHECK(modPlacement.geometry.rect.x == 820);
    CHECK(modPlacement.geometry.rect.y == 1610);
    state.Applied(modPlacement.geometry);
    state.Observe(modPlacement.geometry);
    CHECK(SameGeometry(state.original, modded));

    CHECK(!CompactBelow({}, original).valid);
    CHECK(!CompactBelow(gold, {{0, 0, 0, 112}, 1.0F}).valid);
    CHECK(!CompactBelow({0, 0, 20, 50}, original).valid);
    CHECK(!CompactBelow(gold, {{0, 0, 112, 112}, 0.0F}).valid);
    CHECK(!CompactBelow(gold, {{0, 0, 112, 112}, -1.0F}).valid);
    CHECK(!CompactBelow(gold, {{0, 0, 112, 112}, std::numeric_limits<float>::infinity()}).valid);
    CHECK(!CompactBelow(gold, {{0, 0, 112, 112}, std::numeric_limits<float>::quiet_NaN()}).valid);
    CHECK(!CompactBelow({0, (std::numeric_limits<int>::max)(), 313, 58}, original).valid);
    CHECK(!CompactBelow({(std::numeric_limits<int>::max)(), 0, 313, 58}, original).valid);

    // Each added witness is mandatory; reject before writes when any one fails.
    unsigned count{};
    CHECK(UiScaleContract::Matches([&](auto, auto, auto) { ++count; return true; }));
    CHECK(count == 8);
    for (unsigned rejected = 0; rejected < count; ++rejected) {
        unsigned index{};
        CHECK(!UiScaleContract::Matches([&](auto, auto, auto) { return index++ != rejected; }));
    }
    return 0;
}
