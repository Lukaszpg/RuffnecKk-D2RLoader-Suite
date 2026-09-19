#pragma once

#include <RuffnecKk/native_stat_compat.hpp>

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>

namespace RuffnecKk::TrackedNativeTransform {
namespace detail {

inline auto ToState(D2RL::Diagnostics::ModificationState state) noexcept -> State {
    switch (state) {
    case D2RL::Diagnostics::ModificationState::Unchanged: return State::Unchanged;
    case D2RL::Diagnostics::ModificationState::Tracked: return State::Tracked;
    case D2RL::Diagnostics::ModificationState::Untracked: return State::Untracked;
    default: return State::Untracked;
    }
}

inline auto ToKind(D2RL::Diagnostics::ModificationKind kind) noexcept -> Kind {
    switch (kind) {
    case D2RL::Diagnostics::ModificationKind::BytePatch: return Kind::BytePatch;
    case D2RL::Diagnostics::ModificationKind::InlineHook: return Kind::InlineHook;
    case D2RL::Diagnostics::ModificationKind::Multiple: return Kind::Multiple;
    default: return Kind::Unknown;
    }
}

inline auto ObserveD2RL(void* userData, std::uintptr_t mainImageBase,
        std::uintptr_t targetAddress, std::span<const std::byte> expected,
        Observation& observation) noexcept -> bool {
    const auto* context = static_cast<const D2RL::PluginContext*>(userData);
    if (context == nullptr || expected.empty() || expected.size() > UINT32_MAX
        || targetAddress < mainImageBase) return false;

    const D2RL::DiagnosticsServiceV1* diagnostics{};
    if (context->QueryService(
            D2RL::ServiceId::Diagnostics,
            D2RL::DiagnosticsServiceV1Version,
            &diagnostics) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasDiagnosticsServiceV1Field(
            diagnostics, D2RL::DiagnosticsServiceV1RequiredSize)
        || diagnostics->queryHookStatus == nullptr) return false;

    const auto rva = targetAddress - mainImageBase;
    D2RL::Diagnostics::HookQuery query{
        .structSize = D2RL::Diagnostics::HookQuerySize,
        .rva = rva,
        .expected = expected.data(),
        .expectedSize = static_cast<std::uint32_t>(expected.size()),
    };
    D2RL::Diagnostics::HookStatus status{
        .structSize = D2RL::Diagnostics::HookStatusSize,
    };
    if (diagnostics->queryHookStatus(context, &query, &status)
            != D2RL::Diagnostics::Result::Success
        || status.structSize < D2RL::Diagnostics::HookStatusRequiredSize
        || status.rva != rva || status.size != expected.size()) return false;

    std::size_t ownerLength{};
    while (ownerLength < sizeof(status.ownerPluginId)
        && status.ownerPluginId[ownerLength] != '\0') ++ownerLength;
    if (ownerLength == sizeof(status.ownerPluginId)) return false;

    observation = {
        .state = ToState(status.state),
        .kind = ToKind(status.kind),
        .ownerCount = status.ownerCount,
        .ownerPluginId = {},
        .targetExecutable = false,
        .structuralWitnessesMatch = false,
    };
    if (!observation.ownerPluginId.Assign(
            {status.ownerPluginId, ownerLength})) return false;
    return true;
}

} // namespace detail

inline auto ObserveD2RL(void* userData, std::uintptr_t mainImageBase,
        std::uintptr_t targetAddress, std::span<const std::byte> expected,
        Observation& observation) noexcept -> bool {
    return detail::ObserveD2RL(
        userData, mainImageBase, targetAddress, expected, observation);
}

inline auto D2RLDiagnosticsContext(const D2RL::PluginContext* context) noexcept
        -> NativeStatCompat::TransformDiagnostics {
    return {const_cast<D2RL::PluginContext*>(context), ObserveD2RL};
}

} // namespace RuffnecKk::TrackedNativeTransform
