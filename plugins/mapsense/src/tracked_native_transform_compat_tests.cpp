#include "tracked_native_transform_compat.hpp"

#include <cstdlib>
#include <iostream>

namespace {

int Failures{};

void Check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::cerr << "FAIL line " << line << ": " << expression << '\n';
    ++Failures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

} // namespace

int main() {
    using namespace RuffnecKk::TrackedNativeTransform;
    using RuffnecKk::MapSense::Detail::EvaluateClientUnitHashLookup;

    CHECK(EvaluateClientUnitHashLookup(
        {State::Unchanged, Kind::Unknown, 0U, {}, true, true}, true, true)
        == Admission::Pristine);
    CHECK(EvaluateClientUnitHashLookup(
        {State::Tracked, Kind::InlineHook, 1U,
            "celestialrayone.engine-stability", true, true}, false, true)
        == Admission::TrackedCompatible);
    CHECK(EvaluateClientUnitHashLookup(
        {State::Untracked, Kind::Unknown, 0U, {}, true, true}, false, true)
        == Admission::Rejected);
    CHECK(EvaluateClientUnitHashLookup(
        {State::Tracked, Kind::InlineHook, 1U,
            "another-plugin", true, true}, false, true)
        == Admission::Rejected);
    CHECK(EvaluateClientUnitHashLookup(
        {State::Tracked, Kind::InlineHook, 2U,
            "celestialrayone.engine-stability", true, true}, false, true)
        == Admission::Rejected);
    CHECK(EvaluateClientUnitHashLookup(
        {State::Tracked, Kind::BytePatch, 1U,
            "celestialrayone.engine-stability", true, true}, false, true)
        == Admission::Rejected);
    CHECK(EvaluateClientUnitHashLookup(
        {State::Tracked, Kind::InlineHook, 1U,
            "celestialrayone.engine-stability", false, true}, false, true)
        == Admission::Rejected);
    CHECK(EvaluateClientUnitHashLookup(
        {State::Tracked, Kind::InlineHook, 1U,
            "celestialrayone.engine-stability", true, false}, false, true)
        == Admission::Rejected);
    CHECK(EvaluateClientUnitHashLookup(
        {State::Tracked, Kind::InlineHook, 1U,
            "celestialrayone.engine-stability", true, true}, false, false)
        == Admission::Rejected);
    return Failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
