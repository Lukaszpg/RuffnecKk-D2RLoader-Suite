#pragma once

#include <d3d12.h>
#include <cstdint>
#include <limits>

#ifndef RUFFNECKK_MAPSENSE_GPU_DIAGNOSTICS
#define RUFFNECKK_MAPSENSE_GPU_DIAGNOSTICS 0
#endif

namespace RuffnecKk::MapSense::GpuDiagnostics {

inline constexpr bool Enabled = RUFFNECKK_MAPSENSE_GPU_DIAGNOSTICS != 0;
inline constexpr auto RemovedFenceValue = (std::numeric_limits<std::uint64_t>::max)();

enum class FenceResult { Complete, DeviceRemoved, InvalidTarget, ArmFailed, WaitFailed, Incomplete };

// The same control flow is used with real D3D12/Win32 operations and deterministic
// failure-injection tests. A signaled event alone is not proof of GPU completion:
// device removal also wakes fence events and publishes UINT64_MAX.
template <typename ReadCompleted, typename ArmEvent, typename WaitEvent>
[[nodiscard]] auto AwaitFence(
    std::uint64_t target, ReadCompleted read, ArmEvent arm, WaitEvent wait) noexcept
    -> FenceResult {
    if (target == RemovedFenceValue) return FenceResult::InvalidTarget;
    const auto before = read();
    if (before == RemovedFenceValue) return FenceResult::DeviceRemoved;
    if (before >= target) return FenceResult::Complete;
    if (!arm(target)) return FenceResult::ArmFailed;
    if (!wait()) return FenceResult::WaitFailed;
    const auto after = read();
    if (after == RemovedFenceValue) return FenceResult::DeviceRemoved;
    return after >= target ? FenceResult::Complete : FenceResult::Incomplete;
}

using LogSink = void (*)(const char*) noexcept;
void ConfigureBeforeDeviceCreation(LogSink info, LogSink warning) noexcept;
void NameObject(ID3D12Object* object, const wchar_t* name) noexcept;
void RecordSubmission(std::uint64_t fence, const char* phase) noexcept;
void RecordFailure(
    ID3D12Device* device, const char* operation, HRESULT result, LogSink warning) noexcept;

// Bounded readers of DRED-owned records, also exercised with synthetic records.
// They never dereference or call the command-list/queue/allocation object pointers.
void WriteBreadcrumbs(const D3D12_AUTO_BREADCRUMB_NODE1* first, LogSink log) noexcept;
void WriteAllocations(
    const D3D12_DRED_ALLOCATION_NODE1* first, const char* kind, LogSink log) noexcept;

} // namespace RuffnecKk::MapSense::GpuDiagnostics
