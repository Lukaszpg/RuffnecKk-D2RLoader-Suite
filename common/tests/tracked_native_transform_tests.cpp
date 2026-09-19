#include <RuffnecKk/tracked_native_transform.hpp>

#include <type_traits>

namespace Transform = RuffnecKk::TrackedNativeTransform;

constexpr auto Expect(Transform::Admission actual, Transform::Admission expected) noexcept -> bool {
    return actual == expected;
}
int main() {
    static_assert(std::is_same_v<
        decltype(Transform::Observation::ownerPluginId),
        Transform::OwnerPluginId>);
    constexpr auto Owner = "celestialrayone.max-life-one";
    constexpr Transform::Observation pristine{
        Transform::State::Unchanged, Transform::Kind::Unknown, 0, "", true, true};
    if (!Expect(Transform::Evaluate(pristine, true, Owner, Transform::Kind::InlineHook),
            Transform::Admission::Pristine)) return 1;
    if (!Expect(Transform::Evaluate(pristine, false, Owner, Transform::Kind::InlineHook),
            Transform::Admission::Rejected)) return 2;

    constexpr Transform::Observation compatible{
        Transform::State::Tracked, Transform::Kind::InlineHook, 1, Owner, true, true};
    constexpr auto copiedCompatible = compatible;
    static_assert(copiedCompatible.ownerPluginId == Owner);
    if (!Expect(Transform::Evaluate(compatible, false, Owner, Transform::Kind::InlineHook),
            Transform::Admission::TrackedCompatible)) return 3;

    constexpr Transform::Observation untracked{
        Transform::State::Untracked, Transform::Kind::InlineHook, 0, "", true, true};
    if (!Expect(Transform::Evaluate(untracked, false, Owner, Transform::Kind::InlineHook),
            Transform::Admission::Rejected)) return 4;
    constexpr Transform::Observation wrongOwner{
        Transform::State::Tracked, Transform::Kind::InlineHook, 1, "different-owner", true, true};
    if (!Expect(Transform::Evaluate(wrongOwner, false, Owner, Transform::Kind::InlineHook),
            Transform::Admission::Rejected)) return 5;
    constexpr Transform::Observation multipleOwners{
        Transform::State::Tracked, Transform::Kind::InlineHook, 2, Owner, true, true};
    if (!Expect(Transform::Evaluate(multipleOwners, false, Owner, Transform::Kind::InlineHook),
            Transform::Admission::Rejected)) return 6;
    constexpr Transform::Observation bytePatch{
        Transform::State::Tracked, Transform::Kind::BytePatch, 1, Owner, true, true};
    if (!Expect(Transform::Evaluate(bytePatch, false, Owner, Transform::Kind::InlineHook),
            Transform::Admission::Rejected)) return 7;
    constexpr Transform::Observation notExecutable{
        Transform::State::Tracked, Transform::Kind::InlineHook, 1, Owner, false, true};
    if (!Expect(Transform::Evaluate(notExecutable, false, Owner, Transform::Kind::InlineHook),
            Transform::Admission::Rejected)) return 8;
    constexpr Transform::Observation brokenWitness{
        Transform::State::Tracked, Transform::Kind::InlineHook, 1, Owner, true, false};
    return Expect(Transform::Evaluate(brokenWitness, false, Owner, Transform::Kind::InlineHook),
        Transform::Admission::Rejected) ? 0 : 9;
}
