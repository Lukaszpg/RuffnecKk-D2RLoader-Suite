#include "d3d12_gpu_diagnostics.hpp"

#if RUFFNECKK_MAPSENSE_GPU_DIAGNOSTICS
#include <dxgi.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cwchar>

namespace RuffnecKk::MapSense::GpuDiagnostics {
namespace {
using Microsoft::WRL::ComPtr;
constexpr UINT MaximumNodes = 64;
constexpr UINT MaximumAllocations = 32;
constexpr UINT MaximumHistory = 65'536;
constexpr UINT MaximumOperations = 16;
constexpr UINT MaximumContexts = 8;
// All calls from the host are serialized by HostMutex. No telemetry owns COM
// objects or allocates memory while processing a lost device.
bool Configured{};
bool FailureCaptured{};
std::uint64_t SubmissionCount{};
std::uint64_t LastSubmissionFence{};
std::uint64_t LastSubmissionTick{};
const char* LastSubmissionPhase = "none";

template <typename... Args>
void Emit(LogSink log, const char* format, Args... args) noexcept {
    if (log == nullptr) return;
    std::array<char, 768> message{};
    if (std::snprintf(message.data(), message.size(), format, args...) > 0)
        log(message.data());
}

auto SafeName(const char* narrow, const wchar_t* wide) noexcept -> std::array<char, 97> {
    std::array<char, 97> out{};
    if (narrow == nullptr && wide == nullptr) {
        out[0] = '?';
        return out;
    }
    for (std::size_t index = 0; index + 1 < out.size(); ++index) {
        const auto value = narrow != nullptr
            ? static_cast<unsigned>(static_cast<unsigned char>(narrow[index]))
            : static_cast<unsigned>(wide[index]);
        if (value == 0) break;
        out[index] = value >= 32 && value < 127 && value != '"'
            ? static_cast<char>(value) : '?';
    }
    return out;
}

auto OperationName(D3D12_AUTO_BREADCRUMB_OP operation) noexcept -> const char* {
    switch (operation) {
        case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED: return "DrawInstanced";
        case D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED: return "DrawIndexedInstanced";
        case D3D12_AUTO_BREADCRUMB_OP_DISPATCH: return "Dispatch";
        case D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION: return "CopyBufferRegion";
        case D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION: return "CopyTextureRegion";
        case D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE: return "CopyResource";
        case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER: return "ResourceBarrier";
        case D3D12_AUTO_BREADCRUMB_OP_EXECUTEINDIRECT: return "ExecuteIndirect";
        case D3D12_AUTO_BREADCRUMB_OP_EXECUTEBUNDLE: return "ExecuteBundle";
        case D3D12_AUTO_BREADCRUMB_OP_PRESENT: return "Present";
        case D3D12_AUTO_BREADCRUMB_OP_BEGINEVENT: return "BeginEvent";
        case D3D12_AUTO_BREADCRUMB_OP_ENDEVENT: return "EndEvent";
        case D3D12_AUTO_BREADCRUMB_OP_SETMARKER: return "SetMarker";
        default: return "Other";
    }
}
} // namespace

void ConfigureBeforeDeviceCreation(LogSink info, LogSink warning) noexcept {
    if (Configured) return;
    Configured = true;
    Emit(info, "MapSense GPU diagnostic candidate: gpu-diag.2; DRED enabled by build option; local diagnostic build.");
    // The game directory can contain an older D3D12 redistributable that fails
    // before graphics initialization. The observed game device uses Windows'
    // runtime. Resolve that exact system module before any device is created;
    // retain this one module reference for the diagnostic process lifetime.
    std::array<wchar_t, MAX_PATH> modulePath{};
    const UINT directoryLength = GetSystemDirectoryW(
        modulePath.data(), static_cast<UINT>(modulePath.size()));
    constexpr wchar_t suffix[] = L"\\d3d12.dll";
    if (directoryLength == 0 || directoryLength + std::size(suffix) > modulePath.size()) {
        const DWORD error = directoryLength == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;
        Emit(warning, "MapSense GPU diagnostic: system D3D12 path unavailable (win32=%lu); DRED not configured.", error);
        return;
    }
    std::copy(std::begin(suffix), std::end(suffix), modulePath.begin() + directoryLength);
    HMODULE module = LoadLibraryExW(modulePath.data(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module == nullptr) {
        const DWORD error = GetLastError();
        Emit(warning, "MapSense GPU diagnostic: system D3D12 load failed (win32=%lu); DRED not configured.", error);
        return;
    }
    std::array<wchar_t, MAX_PATH> loadedPath{};
    if (GetModuleFileNameW(module, loadedPath.data(), static_cast<DWORD>(loadedPath.size())) == 0) {
        Emit(warning, "MapSense GPU diagnostic: loaded D3D12 module path unavailable (win32=%lu).", GetLastError());
    } else {
        const auto name = SafeName(nullptr, loadedPath.data());
        Emit(info, "MapSense GPU diagnostic: system D3D12 module=\"%s\".", name.data());
    }
    using GetDebugInterface = HRESULT(WINAPI*)(REFIID, void**);
    const auto getDebug = reinterpret_cast<GetDebugInterface>(
        GetProcAddress(module, "D3D12GetDebugInterface"));
    if (getDebug == nullptr) {
        Emit(warning, "MapSense GPU diagnostic: D3D12GetDebugInterface export unavailable (win32=%lu); DRED not configured.", GetLastError());
        return;
    }
    ComPtr<ID3D12DeviceRemovedExtendedDataSettings> settings;
    const HRESULT result = getDebug(IID_PPV_ARGS(&settings));
    if (FAILED(result)) {
        Emit(warning, "MapSense GPU diagnostic: DRED settings unavailable (hr=0x%08X); this run cannot prove DRED collection.", static_cast<unsigned>(result));
        return;
    }
    settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> contexts;
    const bool contextEnabled = SUCCEEDED(settings.As(&contexts));
    if (contextEnabled)
        contexts->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    Emit(info, "MapSense GPU diagnostic: DRED breadcrumbs/page faults requested before the first MapSense D3D12 device probe; contexts=%u. Already-created game devices cannot be retrofitted.", contextEnabled ? 1U : 0U);
}

void NameObject(ID3D12Object* object, const wchar_t* name) noexcept {
    if (object != nullptr) static_cast<void>(object->SetName(name));
}

void RecordSubmission(std::uint64_t fence, const char* phase) noexcept {
    ++SubmissionCount;
    LastSubmissionFence = fence;
    LastSubmissionTick = GetTickCount64();
    LastSubmissionPhase = phase;
}

void WriteBreadcrumbs(const D3D12_AUTO_BREADCRUMB_NODE1* first, LogSink log) noexcept {
    auto* node = first;
    UINT nodes{};
    for (; node != nullptr && nodes < MaximumNodes; node = node->pNext, ++nodes) {
        const auto listName = SafeName(node->pCommandListDebugNameA, node->pCommandListDebugNameW);
        const auto queueName = SafeName(node->pCommandQueueDebugNameA, node->pCommandQueueDebugNameW);
        const bool hasCompleted = node->pLastBreadcrumbValue != nullptr;
        const UINT completed = hasCompleted ? *node->pLastBreadcrumbValue : 0;
        Emit(log, "MapSense DRED node[%u]: list=%p name=\"%s\" queue=%p name=\"%s\" count=%u completed=%u completed-known=%u.",
            nodes, static_cast<void*>(node->pCommandList), listName.data(),
            static_cast<void*>(node->pCommandQueue), queueName.data(),
            node->BreadcrumbCount, completed, hasCompleted ? 1U : 0U);
        if (node->pCommandHistory != nullptr && hasCompleted
            && completed <= node->BreadcrumbCount && node->BreadcrumbCount <= MaximumHistory) {
            const UINT begin = completed > 4 ? completed - 4 : 0;
            const UINT end = (std::min)(node->BreadcrumbCount, begin + MaximumOperations);
            for (UINT index = begin; index < end; ++index) {
                const auto operation = node->pCommandHistory[index];
                Emit(log, "MapSense DRED op: node=%u index=%u code=%u name=%s progress=%s.",
                    nodes, index, static_cast<unsigned>(operation), OperationName(operation),
                    index < completed ? "reported-completed" : "not-reported-completed");
            }
        } else if (node->BreadcrumbCount != 0) {
            Emit(log, "MapSense DRED history omitted: missing data, invalid progress, or count above the bounded 65536-operation layout.");
        }
        const UINT contexts = (std::min)(node->BreadcrumbContextsCount, MaximumContexts);
        for (UINT index = 0; node->pBreadcrumbContexts != nullptr && index < contexts; ++index) {
            const auto& context = node->pBreadcrumbContexts[index];
            const auto name = SafeName(nullptr, context.pContextString);
            Emit(log, "MapSense DRED context: node=%u index=%u name=\"%s\".", nodes, context.BreadcrumbIndex, name.data());
        }
    }
    Emit(log, "MapSense DRED breadcrumbs end: nodes=%u truncated=%u. Progress is evidence, not an exact fault attribution.", nodes, node != nullptr ? 1U : 0U);
}

void WriteAllocations(const D3D12_DRED_ALLOCATION_NODE1* first, const char* kind, LogSink log) noexcept {
    auto* node = first;
    UINT count{};
    for (; node != nullptr && count < MaximumAllocations; node = node->pNext, ++count) {
        const auto name = SafeName(node->ObjectNameA, node->ObjectNameW);
        Emit(log, "MapSense DRED allocation: kind=%s index=%u type=%u object=%p name=\"%s\".",
            kind, count, static_cast<unsigned>(node->AllocationType), static_cast<const void*>(node->pObject), name.data());
    }
    Emit(log, "MapSense DRED allocations end: kind=%s count=%u truncated=%u.", kind, count, node != nullptr ? 1U : 0U);
}

void RecordFailure(ID3D12Device* device, const char* operation, HRESULT result, LogSink warning) noexcept {
    if (FailureCaptured) return;
    const HRESULT reason = device != nullptr ? device->GetDeviceRemovedReason() : E_POINTER;
    const auto age = SubmissionCount != 0 ? GetTickCount64() - LastSubmissionTick : 0;
    Emit(warning, "MapSense GPU diagnostic failure: operation=%s hr=0x%08X reason=0x%08X host-list-submissions=%llu last-fence=%llu last-phase=%s last-age-ms=%llu. Backend font uploads are not counted as host-list submissions.",
        operation, static_cast<unsigned>(result), static_cast<unsigned>(reason),
        static_cast<unsigned long long>(SubmissionCount), static_cast<unsigned long long>(LastSubmissionFence),
        LastSubmissionPhase, static_cast<unsigned long long>(age));
    if (device == nullptr || SUCCEEDED(reason)) return;
    FailureCaptured = true;
    ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
    const HRESULT query = device->QueryInterface(IID_PPV_ARGS(&dred));
    if (FAILED(query)) {
        Emit(warning, "MapSense DRED unavailable: QueryInterface=0x%08X.", static_cast<unsigned>(query));
        return;
    }
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs{};
    const HRESULT breadcrumbResult = dred->GetAutoBreadcrumbsOutput1(&breadcrumbs);
    Emit(warning, "MapSense DRED breadcrumbs result=0x%08X.", static_cast<unsigned>(breadcrumbResult));
    if (SUCCEEDED(breadcrumbResult)) WriteBreadcrumbs(breadcrumbs.pHeadAutoBreadcrumbNode, warning);
    D3D12_DRED_PAGE_FAULT_OUTPUT1 fault{};
    const HRESULT faultResult = dred->GetPageFaultAllocationOutput1(&fault);
    Emit(warning, "MapSense DRED page-fault result=0x%08X address=0x%016llX.",
        static_cast<unsigned>(faultResult), static_cast<unsigned long long>(fault.PageFaultVA));
    if (SUCCEEDED(faultResult)) {
        WriteAllocations(fault.pHeadExistingAllocationNode, "existing", warning);
        WriteAllocations(fault.pHeadRecentFreedAllocationNode, "recently-freed", warning);
    }
    Emit(warning, "MapSense DRED capture end; the original graphics failure remains unchanged.");
}
} // namespace RuffnecKk::MapSense::GpuDiagnostics
#else
namespace RuffnecKk::MapSense::GpuDiagnostics {
void ConfigureBeforeDeviceCreation(LogSink, LogSink) noexcept {}
void NameObject(ID3D12Object*, const wchar_t*) noexcept {}
void RecordSubmission(std::uint64_t, const char*) noexcept {}
void RecordFailure(ID3D12Device*, const char*, HRESULT, LogSink) noexcept {}
void WriteBreadcrumbs(const D3D12_AUTO_BREADCRUMB_NODE1*, LogSink) noexcept {}
void WriteAllocations(const D3D12_DRED_ALLOCATION_NODE1*, const char*, LogSink) noexcept {}
} // namespace RuffnecKk::MapSense::GpuDiagnostics
#endif
