#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace RuffnecKk::MapSense::Detail {

// This policy is deliberately free of DXGI, MinHook, and process state. The
// host owns synchronization and lifetime; this file only keeps the fallback
// decision auditable and regression-testable.
struct RendererMethodPair {
    void* present{};
    void* resizeBuffers{};
};

enum class RendererMethodPairCapture {
    Accepted,
    Duplicate,
    Conflict,
    Invalid,
};

enum class RendererMethodDiscoveryPath {
    CompositionProbe,
    CapturedRealSwapChain,
    ProbeRealMismatch,
    Unavailable,
};

// A candidate may supply method addresses during discovery before a particular
// COM identity is elected by Present. Keeping that lifecycle policy pure makes
// the host's bounded-window rule independently testable.
struct RendererCandidateLifecycle {
    bool live{};
    bool foreground{};
    bool conflicted{};
    std::uint64_t generation{};
    RendererMethodPair methods{};
};

enum class RendererMethodReconciliation {
    NotComparable,
    Matches,
    Mismatch,
};

enum class RendererDeviceSelection {
    ExactIdentity,
    ExactCreationQueue,
    Rejected,
};

constexpr std::size_t NoRendererCandidate =
    std::numeric_limits<std::size_t>::max();

[[nodiscard]] constexpr auto IsCompleteRendererMethodPair(
    RendererMethodPair pair) noexcept -> bool {
    return pair.present != nullptr && pair.resizeBuffers != nullptr;
}

[[nodiscard]] constexpr auto SameRendererMethodPair(
    RendererMethodPair left,
    RendererMethodPair right) noexcept -> bool {
    return left.present == right.present
        && left.resizeBuffers == right.resizeBuffers;
}

[[nodiscard]] constexpr auto CaptureRendererMethodPair(
    RendererMethodPair existing,
    RendererMethodPair incoming) noexcept -> RendererMethodPairCapture {
    if (!IsCompleteRendererMethodPair(incoming))
        return RendererMethodPairCapture::Invalid;
    if (!IsCompleteRendererMethodPair(existing))
        return RendererMethodPairCapture::Accepted;
    return SameRendererMethodPair(existing, incoming)
        ? RendererMethodPairCapture::Duplicate
        : RendererMethodPairCapture::Conflict;
}

[[nodiscard]] constexpr auto IsQualifiedForegroundRendererCandidate(
    RendererCandidateLifecycle candidate) noexcept -> bool {
    return candidate.live && candidate.foreground && !candidate.conflicted
        && IsCompleteRendererMethodPair(candidate.methods);
}

[[nodiscard]] constexpr auto SelectNewestQualifiedRendererCandidate(
    const RendererCandidateLifecycle* candidates,
    std::size_t candidateCount) noexcept -> std::size_t {
    std::size_t selected = NoRendererCandidate;
    for (std::size_t index = 0U; index < candidateCount; ++index) {
        const auto& candidate = candidates[index];
        if (!IsQualifiedForegroundRendererCandidate(candidate)) continue;
        if (selected == NoRendererCandidate
            || candidate.generation > candidates[selected].generation) {
            selected = index;
        }
    }
    return selected;
}

[[nodiscard]] constexpr auto ReconcileRendererMethodPair(
    RendererMethodPair installed,
    RendererMethodPair qualified) noexcept -> RendererMethodReconciliation {
    if (!IsCompleteRendererMethodPair(installed)
        || !IsCompleteRendererMethodPair(qualified)) {
        return RendererMethodReconciliation::NotComparable;
    }
    return SameRendererMethodPair(installed, qualified)
        ? RendererMethodReconciliation::Matches
        : RendererMethodReconciliation::Mismatch;
}

[[nodiscard]] constexpr auto ReconcileReplacementRendererMethodPair(
    bool replacesElectedIdentity,
    RendererMethodPair installed,
    RendererMethodPair replacement) noexcept -> RendererMethodReconciliation {
    return replacesElectedIdentity
        ? ReconcileRendererMethodPair(installed, replacement)
        : RendererMethodReconciliation::NotComparable;
}

// DXGI receives a direct command queue, not an ID3D12Device, when a D3D12
// swap chain is created. A successful intercepted CreateSwapChain* call
// therefore gives the host a stronger ownership witness than a later COM
// wrapper identity comparison: the recorded queue is the presentation queue
// for that exact returned swap-chain identity. Native Windows normally exposes
// the same canonical device identity through both objects. Compatibility
// layers may expose distinct wrappers, so the exact creation-time association
// is accepted after both device lookups succeed, without adapter probing.
[[nodiscard]] constexpr auto SelectRendererDevice(
    bool exactCreationQueueBinding,
    bool queueDeviceAvailable,
    bool swapChainDeviceAvailable,
    bool sameComIdentity) noexcept -> RendererDeviceSelection {
    if (!exactCreationQueueBinding || !queueDeviceAvailable
        || !swapChainDeviceAvailable) {
        return RendererDeviceSelection::Rejected;
    }
    if (sameComIdentity) return RendererDeviceSelection::ExactIdentity;
    return RendererDeviceSelection::ExactCreationQueue;
}

[[nodiscard]] constexpr auto SelectRendererMethodDiscoveryPath(
    bool compositionProbeReady,
    RendererMethodPair compositionProbePair,
    RendererMethodPair capturedRealPair,
    bool realPairConflicted) noexcept -> RendererMethodDiscoveryPath {
    if (realPairConflicted)
        return RendererMethodDiscoveryPath::Unavailable;
    if (IsCompleteRendererMethodPair(capturedRealPair)) {
        if (compositionProbeReady
            && !SameRendererMethodPair(
                compositionProbePair, capturedRealPair)) {
            return RendererMethodDiscoveryPath::ProbeRealMismatch;
        }
        return RendererMethodDiscoveryPath::CapturedRealSwapChain;
    }
    if (compositionProbeReady)
        return RendererMethodDiscoveryPath::CompositionProbe;
    return RendererMethodDiscoveryPath::Unavailable;
}

} // namespace RuffnecKk::MapSense::Detail
