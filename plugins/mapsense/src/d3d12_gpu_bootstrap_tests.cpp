#include "d3d12_gpu_diagnostics.hpp"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {
std::vector<std::string> Messages;
unsigned Checks{};
unsigned Failures{};

void Log(const char* message) noexcept {
    Messages.emplace_back(message);
    std::puts(message);
}

auto Contains(std::string_view text) -> bool {
    for (const auto& message : Messages)
        if (message.find(text) != std::string::npos) return true;
    return false;
}

void Check(bool passed, const char* description) {
    ++Checks;
    if (!passed) {
        ++Failures;
        std::printf("FAIL: %s\n", description);
    }
}
} // namespace

// Optional Windows integration test, deliberately not linked to d3d12/dxgi:
// its first graphics operation is the same dynamic bootstrap as the plugin.
// Also run beside the locally installed legacy d3d12.dll in a disposable
// evidence directory to reproduce the game's application-directory lookup.
int main() {
    using namespace RuffnecKk::MapSense::GpuDiagnostics;
    Check(GetModuleHandleW(L"d3d12.dll") == nullptr,
        "D3D12 is absent before diagnostic bootstrap");
    ConfigureBeforeDeviceCreation(Log, Log);
    Check(Contains("system D3D12 module="), "system module path is recorded");
    Check(Contains("DRED breadcrumbs/page faults requested"),
        "DRED settings available before any device creation");
    Check(!Contains("unavailable") && !Contains("load failed"),
        "bootstrap has no hidden loader or interface failure");
    const auto count = Messages.size();
    ConfigureBeforeDeviceCreation(Log, Log);
    Check(Messages.size() == count, "bootstrap runs once per process");
    std::printf("MapSense GPU bootstrap checks: %u, failures: %u\n", Checks, Failures);
    return Failures == 0 ? 0 : 1;
}
