#include "d3d12_renderer_discovery_policy.hpp"

#include <cstdio>
#include <iterator>

namespace {

unsigned Checks{};
unsigned Failures{};

void Check(bool condition, const char* description) {
    ++Checks;
    if (!condition) {
        ++Failures;
        std::printf("FAIL: %s\n", description);
    }
}

} // namespace

int main() {
    using namespace RuffnecKk::MapSense::Detail;

    int presentTarget{};
    int resizeTarget{};
    int conflictingPresentTarget{};
    int conflictingResizeTarget{};
    const RendererMethodPair pair{
        .present = &presentTarget,
        .resizeBuffers = &resizeTarget,
    };
    const RendererMethodPair conflictingPair{
        .present = &conflictingPresentTarget,
        .resizeBuffers = &conflictingResizeTarget,
    };
    const RendererCandidateLifecycle staleCandidate{
        .live = false,
        .foreground = true,
        .generation = 9U,
        .methods = pair,
    };
    const RendererCandidateLifecycle backgroundCandidate{
        .live = true,
        .foreground = false,
        .generation = 10U,
        .methods = conflictingPair,
    };
    const RendererCandidateLifecycle olderForegroundCandidate{
        .live = true,
        .foreground = true,
        .generation = 11U,
        .methods = pair,
    };
    const RendererCandidateLifecycle newestForegroundCandidate{
        .live = true,
        .foreground = true,
        .generation = 12U,
        .methods = conflictingPair,
    };
    const RendererCandidateLifecycle candidates[]{
        staleCandidate,
        backgroundCandidate,
        olderForegroundCandidate,
        newestForegroundCandidate,
    };

    Check(CaptureRendererMethodPair({}, pair)
            == RendererMethodPairCapture::Accepted,
        "the first complete real pair is accepted");
    Check(CaptureRendererMethodPair(pair, pair)
            == RendererMethodPairCapture::Duplicate,
        "an identical real pair stays idempotent");
    Check(CaptureRendererMethodPair(pair, conflictingPair)
            == RendererMethodPairCapture::Conflict,
        "a conflicting non-null real pair is rejected");
    Check(CaptureRendererMethodPair({}, {.present = &presentTarget})
            == RendererMethodPairCapture::Invalid,
        "an incomplete real pair is rejected");
    Check(SelectRendererMethodDiscoveryPath(false, {}, pair, false)
            == RendererMethodDiscoveryPath::CapturedRealSwapChain,
        "composition unavailable selects the consistent real-pair fallback");
    Check(SelectRendererMethodDiscoveryPath(false, {}, pair, true)
            == RendererMethodDiscoveryPath::Unavailable,
        "a conflicting real pair prevents the fallback");
    Check(SelectRendererMethodDiscoveryPath(true, pair, pair, false)
            == RendererMethodDiscoveryPath::CapturedRealSwapChain,
        "a matching exact real pair is authoritative before installation");
    Check(SelectRendererMethodDiscoveryPath(true, pair, conflictingPair, false)
            == RendererMethodDiscoveryPath::ProbeRealMismatch,
        "a probe-versus-real mismatch is rejected before installation");
    Check(SelectRendererMethodDiscoveryPath(true, pair, {}, false)
            == RendererMethodDiscoveryPath::CompositionProbe,
        "the Windows-first composition probe remains selected without a real pair");
    Check(SelectRendererMethodDiscoveryPath(true, pair, pair, true)
            == RendererMethodDiscoveryPath::Unavailable,
        "a conflict fail-closes even when composition discovery is available");
    Check(SelectNewestQualifiedRendererCandidate(
              candidates, std::size(candidates)) == 3U,
        "retry qualification prefers the newest live foreground candidate without electing identity");
    Check(SelectNewestQualifiedRendererCandidate(nullptr, 0U)
            == NoRendererCandidate,
        "no lifecycle candidate is selected when the bounded registry is empty");
    Check(ReconcileRendererMethodPair(pair, pair)
            == RendererMethodReconciliation::Matches,
        "late real evidence accepts an installed matching pair");
    Check(ReconcileRendererMethodPair(pair, conflictingPair)
            == RendererMethodReconciliation::Mismatch,
        "late real evidence rejects an installed mismatching pair");
    Check(ReconcileRendererMethodPair({}, pair)
            == RendererMethodReconciliation::NotComparable,
        "uninstalled targets do not manufacture a reconciliation result");
    Check(ReconcileReplacementRendererMethodPair(true, pair, conflictingPair)
            == RendererMethodReconciliation::Mismatch,
        "a marked same-window replacement reconciles immediately against installed targets");
    Check(ReconcileReplacementRendererMethodPair(false, pair, conflictingPair)
            == RendererMethodReconciliation::NotComparable,
        "an ordinary candidate does not inherit replacement reconciliation");
    Check(SelectRendererDevice(
              true, true, true, true)
            == RendererDeviceSelection::ExactIdentity,
        "an exact creation queue with the swap-chain device identity is accepted");
    Check(SelectRendererDevice(
              false, true, true, true)
            == RendererDeviceSelection::Rejected,
        "a canonical device identity without an exact creation association is rejected");
    Check(SelectRendererDevice(
              true, true, true, false)
            == RendererDeviceSelection::ExactCreationQueue,
        "an exact creation queue remains authoritative across distinct COM wrappers");
    Check(SelectRendererDevice(
              false, true, true, false)
            == RendererDeviceSelection::Rejected,
        "distinct wrappers without an exact creation association are rejected");
    Check(SelectRendererDevice(
              true, false, true, false)
            == RendererDeviceSelection::Rejected,
        "a missing exact-queue device is rejected");
    Check(SelectRendererDevice(
              true, true, false, false)
            == RendererDeviceSelection::Rejected,
        "a missing swap-chain-reported device is rejected");

    std::printf("MapSense renderer discovery policy checks: %u, failures: %u\n",
        Checks, Failures);
    return Failures == 0U ? 0 : 1;
}
