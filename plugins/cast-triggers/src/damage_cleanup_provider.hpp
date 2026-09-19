#pragma once

#include "damage_cleanup_api.hpp"
#include <Windows.h>
#include <array>
#include <cstring>
#include <mutex>

namespace RuffnecKk::DamageCleanup {
using CleanupFn = void(__cdecl*)(void*) noexcept;

inline auto ReadBytes(const void* source, void* target, std::size_t size)
        noexcept -> bool {
    __try {
        std::memcpy(target, source, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

inline auto ExecuteProtected(OperationFn operation, void* userData,
        void* damage, CleanupFn cleanup) noexcept -> Result {
    __try {
        bool success{};
        __try {
            success = operation(userData, damage);
        } __finally {
            // Also attempt cleanup on an operation SEH fault. Never retry.
            cleanup(damage);
        }
        return success ? Result::Completed : Result::OperationFailed;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return Result::Fault; }
}

// No borrowed trampoline escapes this provider. Its live hook bytes are
// captured only after a successful exclusive InstallInlineHook. Revalidate
// them for every request. Recursive locking permits nested Doll deaths in AoE;
// Stop waits for all admitted operations before the loader removes hooks.
class Provider {
public:
    auto Publish(const void* entry, const std::array<std::uint8_t, 32>& native,
            CleanupFn cleanup) noexcept -> bool {
        std::scoped_lock lock(mutex_);
        ready_ = false;
        if (!entry || !cleanup || !ReadBytes(entry, live_.data(), live_.size())
                || live_ == native) return false;
        entry_ = entry;
        native_ = native;
        cleanup_ = cleanup;
        ready_ = true;
        return true;
    }
    void Stop() noexcept {
        std::scoped_lock lock(mutex_);
        ready_ = false;
        cleanup_ = nullptr;
        entry_ = nullptr;
    }
    auto Accepts(const RequestV1* request) noexcept -> bool {
        std::scoped_lock lock(mutex_);
        return AcceptsLocked(request);
    }
    auto Run(const RequestV1* request, void* damage, OperationFn operation,
            void* userData) noexcept -> Result {
        std::scoped_lock lock(mutex_);
        if (!damage || !operation
                || reinterpret_cast<std::uintptr_t>(damage) % 16 != 0
                || !AcceptsLocked(request)) return Result::Rejected;
        return ExecuteProtected(operation, userData, damage, cleanup_);
    }
private:
    auto AcceptsLocked(const RequestV1* request) const noexcept -> bool {
        RequestV1 copy{};
        std::array<std::uint8_t, 32> expected{}, live{};
        return ready_ && cleanup_ && request
            && ReadBytes(request, &copy, sizeof(copy)) && ValidRequest(&copy)
            && copy.nativeDestructor == entry_
            && ReadBytes(copy.nativeExpected, expected.data(), expected.size())
            && expected == native_
            && ReadBytes(entry_, live.data(), live.size()) && live == live_;
    }
    std::recursive_mutex mutex_;
    const void* entry_{};
    std::array<std::uint8_t, 32> native_{}, live_{};
    CleanupFn cleanup_{};
    bool ready_{};
};
} // namespace RuffnecKk::DamageCleanup
