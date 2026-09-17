#include "d3d12_gpu_diagnostics.hpp"
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace RuffnecKk::MapSense::GpuDiagnostics;
using Microsoft::WRL::ComPtr;
unsigned Checks{};
unsigned Failures{};
std::vector<std::string> Messages;
void Check(bool ok, const char* name) {
    ++Checks;
    if (!ok) { ++Failures; std::fprintf(stderr, "FAIL: %s\n", name); }
}
void Log(const char* message) noexcept { Messages.emplace_back(message); }
auto Contains(std::string_view part) -> bool {
    for (const auto& message : Messages)
        if (message.find(part) != std::string::npos) return true;
    return false;
}

void TestFences() {
    struct Case {
        std::uint64_t target, before, after;
        bool arm, wait;
        FenceResult expected;
        unsigned reads, arms, waits;
        const char* name;
    };
    const std::array cases{
        Case{7, 7, 0, false, false, FenceResult::Complete, 1, 0, 0, "already completed"},
        Case{7, 9, 0, false, false, FenceResult::Complete, 1, 0, 0, "later work completed"},
        Case{7, 4, 7, true, true, FenceResult::Complete, 2, 1, 1, "completion after wake"},
        Case{7, RemovedFenceValue, 0, true, true, FenceResult::DeviceRemoved, 1, 0, 0, "lost before wait"},
        Case{0, RemovedFenceValue, 0, true, true, FenceResult::DeviceRemoved, 1, 0, 0, "lost before first submission"},
        Case{7, 4, RemovedFenceValue, true, true, FenceResult::DeviceRemoved, 2, 1, 1, "device loss wakes event"},
        Case{7, 4, 6, true, true, FenceResult::Incomplete, 2, 1, 1, "wake is not completion"},
        Case{7, 4, 7, false, true, FenceResult::ArmFailed, 1, 1, 0, "event registration fails"},
        Case{7, 4, 7, true, false, FenceResult::WaitFailed, 1, 1, 1, "wait fails or times out"},
        Case{RemovedFenceValue, 0, 0, true, true, FenceResult::InvalidTarget, 0, 0, 0, "reserved fence target"},
        Case{0, 0, 0, false, false, FenceResult::Complete, 1, 0, 0, "healthy first submission"},
    };
    for (const auto& c : cases) {
        unsigned reads{}, arms{}, waits{};
        const auto result = AwaitFence(c.target,
            [&]() noexcept { return reads++ == 0 ? c.before : c.after; },
            [&](std::uint64_t value) noexcept { ++arms; return value == c.target && c.arm; },
            [&]() noexcept { ++waits; return c.wait; });
        Check(result == c.expected, c.name);
        Check(reads == c.reads && arms == c.arms && waits == c.waits, "expected synchronization operations only");
        // These are the failure modes the previous >= check accepted incorrectly.
        if (c.before == RemovedFenceValue || (c.wait && c.after == RemovedFenceValue))
            Check(result != FenceResult::Complete, "lost device cannot admit buffer reuse");
    }
}

void TestDredReaders() {
    Messages.clear();
    WriteBreadcrumbs(nullptr, Log);
    Check(Contains("nodes=0 truncated=0"), "empty DRED is explicit");
    const std::array operations{
        D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER,
        D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED,
        D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION,
        D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER,
    };
    UINT completed = 2;
    D3D12_AUTO_BREADCRUMB_NODE1 node{};
    node.pCommandListDebugNameA = "RuffnecKk MapSense\ncommand list";
    node.pCommandQueueDebugNameW = L"D2R queue";
    node.BreadcrumbCount = static_cast<UINT>(operations.size());
    node.pLastBreadcrumbValue = &completed;
    node.pCommandHistory = operations.data();
    Messages.clear();
    WriteBreadcrumbs(&node, Log);
    Check(Contains("RuffnecKk MapSense?command list"), "diagnostic names cannot inject log lines");
    Check(Contains("index=1 code=4 name=DrawIndexedInstanced progress=reported-completed"), "completed breadcrumb");
    Check(Contains("index=2 code=8 name=CopyTextureRegion progress=not-reported-completed"), "pending breadcrumb");
    Check(Contains("nodes=1 truncated=0"), "single DRED node");
    node.pNext = &node;
    Messages.clear();
    WriteBreadcrumbs(&node, Log);
    Check(Contains("nodes=64 truncated=1"), "cyclic DRED list is bounded");
    node.pNext = nullptr;
    node.pLastBreadcrumbValue = nullptr;
    Messages.clear();
    WriteBreadcrumbs(&node, Log);
    Check(Contains("completed-known=0") && Contains("history omitted"), "missing progress is not zero completion");
    node.pLastBreadcrumbValue = &completed;
    completed = 5;
    Messages.clear();
    WriteBreadcrumbs(&node, Log);
    Check(Contains("history omitted") && !Contains("MapSense DRED op:"), "out-of-range progress refused");
    completed = 0;
    node.BreadcrumbCount = 65'537;
    Messages.clear();
    WriteBreadcrumbs(&node, Log);
    Check(Contains("history omitted"), "oversized history never indexes the short supplied array");
    const std::vector<D3D12_AUTO_BREADCRUMB_OP> large(65'536, D3D12_AUTO_BREADCRUMB_OP_DISPATCH);
    node.pCommandHistory = large.data();
    node.BreadcrumbCount = static_cast<UINT>(large.size());
    completed = 65'535;
    Messages.clear();
    WriteBreadcrumbs(&node, Log);
    Check(Contains("index=65535") && !Contains("index=65536"), "maximum history boundary respected");
    completed = 0;
    Messages.clear();
    WriteBreadcrumbs(&node, Log);
    Check(Contains("index=15") && !Contains("index=16"), "per-node operation output bounded");
    D3D12_DRED_ALLOCATION_NODE1 allocation{};
    allocation.ObjectNameW = L"MapSense texture";
    allocation.AllocationType = D3D12_DRED_ALLOCATION_TYPE_RESOURCE;
    allocation.pNext = &allocation;
    Messages.clear();
    WriteAllocations(&allocation, "recently-freed", Log);
    Check(Contains("name=\"MapSense texture\""), "allocation name retained");
    Check(Contains("count=32 truncated=1"), "cyclic allocation list bounded");
    Messages.clear();
    WriteAllocations(nullptr, "existing", Log);
    Check(Contains("count=0 truncated=0"), "empty allocation list explicit");
}

// Optional integration probe: a new software-only WARP device, no window, no
// game process and no workload submitted to the user's physical GPU. Explicit
// RemoveDevice tests the loss path without intentionally hanging any hardware.
void TestWarpDeviceLoss() {
    Messages.clear();
    ConfigureBeforeDeviceCreation(Log, Log);
    Check(Contains("DRED breadcrumbs/page faults requested"), "DRED configuration available");
    ComPtr<IDXGIFactory4> factory;
    HRESULT result = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    Check(SUCCEEDED(result), "WARP DXGI factory");
    if (FAILED(result)) return;
    ComPtr<IDXGIAdapter> adapter;
    result = factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
    Check(SUCCEEDED(result), "software WARP adapter");
    if (FAILED(result)) return;
    ComPtr<ID3D12Device> device;
    result = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
    Check(SUCCEEDED(result), "software D3D12 device");
    if (FAILED(result)) return;
    ComPtr<ID3D12Fence> fence;
    result = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    Check(SUCCEEDED(result), "software fence");
    if (FAILED(result)) return;
    NameObject(fence.Get(), L"RuffnecKk MapSense WARP smoke fence");
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Check(event != nullptr, "software fence event");
    if (event == nullptr) return;
    result = fence->SetEventOnCompletion(1, event);
    Check(SUCCEEDED(result), "pending software fence event");
    ComPtr<ID3D12Device5> removable;
    result = device.As(&removable);
    Check(SUCCEEDED(result), "explicit software device removal supported");
    if (FAILED(result)) { CloseHandle(event); return; }
    removable->RemoveDevice();
    const DWORD wait = WaitForSingleObject(event, 2'000);
    Check(wait == WAIT_OBJECT_0, "device loss wakes the pending fence event");
    Check(fence->GetCompletedValue() == RemovedFenceValue, "real API publishes removed sentinel");
    const auto state = AwaitFence(1,
        [&]() noexcept { return fence->GetCompletedValue(); },
        [&](std::uint64_t target) noexcept { return SUCCEEDED(fence->SetEventOnCompletion(target, event)); },
        [&]() noexcept { return WaitForSingleObject(event, 2'000) == WAIT_OBJECT_0; });
    Check(state == FenceResult::DeviceRemoved, "real software loss refuses reuse");
    RecordFailure(device.Get(), "WARP explicit device removal", DXGI_ERROR_DEVICE_REMOVED, Log);
    Check(Contains("MapSense DRED breadcrumbs result="), "DRED API queried after real device loss");
    Check(Contains("MapSense DRED capture end"), "DRED collector finishes on removed software device");
    const auto messageCount = Messages.size();
    RecordFailure(device.Get(), "duplicate failure", DXGI_ERROR_DEVICE_REMOVED, Log);
    Check(Messages.size() == messageCount, "device-loss capture emitted once");
    CloseHandle(event);
    for (const auto& message : Messages) std::puts(message.c_str());
}
} // namespace

int main(int argc, char** argv) {
    TestFences();
    TestDredReaders();
    if (argc == 2 && std::string_view(argv[1]) == "--warp-smoke") TestWarpDeviceLoss();
    std::printf("MapSense GPU checks: %u, failures: %u\n", Checks, Failures);
    return Failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
