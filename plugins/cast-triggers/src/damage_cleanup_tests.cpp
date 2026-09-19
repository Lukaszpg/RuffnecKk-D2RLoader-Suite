#define NOMINMAX
#include "damage_cleanup_provider.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <vector>

using namespace RuffnecKk::DamageCleanup;
namespace {
int failures{};
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; ++failures; } } while (false)
std::vector<int> calls;
std::atomic_int cleanups{};
void __cdecl Cleanup(void*) noexcept { ++cleanups; }
bool __cdecl Succeed(void*, void*) noexcept { calls.push_back(1); return true; }
bool __cdecl Fail(void*, void*) noexcept { calls.push_back(1); return false; }
bool __cdecl Fault(void*, void*) noexcept {
    RaiseException(0xE0420001, 0, 0, nullptr);
    return false;
}
void __cdecl CleanupFault(void*) noexcept {
    ++cleanups;
    RaiseException(0xE0420002, 0, 0, nullptr);
}
struct Nested { Provider* provider; RequestV1* request; };
bool __cdecl Reenter(void* context, void*) noexcept {
    auto& nested = *static_cast<Nested*>(context);
    alignas(16) std::array<std::uint8_t, DamageBytes> other{};
    return nested.provider->Run(nested.request, other.data(), Succeed, nullptr)
        == Result::Completed;
}
struct Blocking {
    HANDLE entered;
    HANDLE release;
};
bool __cdecl Block(void* context, void*) noexcept {
    auto& block = *static_cast<Blocking*>(context);
    SetEvent(block.entered);
    return WaitForSingleObject(block.release, 5000) == WAIT_OBJECT_0;
}
}

int main(int argc, char** argv) {
    Provider provider;
    std::array<std::uint8_t, 32> native{};
    auto live = native;
    live[0] = 0xE9;
    RequestV1 request{sizeof(RequestV1), Version, DamageLayout,
        live.data(), native.data(), static_cast<std::uint32_t>(native.size()), 0};
    alignas(16) std::array<std::uint8_t, DamageBytes> damage{};
    CHECK(!provider.Accepts(&request));
    CHECK(provider.Run(&request, damage.data(), Succeed, nullptr) == Result::Rejected);
    CHECK(cleanups == 0 && calls.empty());
    CHECK(!provider.Publish(native.data(), native, Cleanup));
    CHECK(provider.Publish(live.data(), native, Cleanup));
    CHECK(provider.Accepts(&request));
    CHECK(!provider.Accepts(nullptr));

    // ABI/layout/entry/witness failures cannot invoke the operation or cleanup.
    auto invalid = request;
    invalid.version = 2; CHECK(!provider.Accepts(&invalid));
    invalid = request; --invalid.structSize; CHECK(!provider.Accepts(&invalid));
    invalid = request; ++invalid.damageLayout; CHECK(!provider.Accepts(&invalid));
    invalid = request; invalid.reserved = 1; CHECK(!provider.Accepts(&invalid));
    invalid = request; invalid.nativeDestructor = native.data(); CHECK(!provider.Accepts(&invalid));
    invalid = request; --invalid.nativeExpectedSize; CHECK(!provider.Accepts(&invalid));
    invalid = request; invalid.nativeExpected = nullptr; CHECK(!provider.Accepts(&invalid));
    CHECK(provider.Run(&invalid, damage.data(), Succeed, nullptr) == Result::Rejected);
    CHECK(provider.Run(&request, nullptr, Succeed, nullptr) == Result::Rejected);
    CHECK(provider.Run(&request, damage.data() + 1, Succeed, nullptr) == Result::Rejected);
    CHECK(provider.Run(&request, damage.data(), nullptr, nullptr) == Result::Rejected);
    for (std::size_t i = 0; i < live.size(); ++i) {
        live[i] ^= 1;
        CHECK(!provider.Accepts(&request));
        CHECK(provider.Run(&request, damage.data(), Succeed, nullptr) == Result::Rejected);
        live[i] ^= 1;
        native[i] ^= 1; CHECK(!provider.Accepts(&request)); native[i] ^= 1;
    }
    CHECK(cleanups == 0 && calls.empty());
    CHECK(provider.Run(&request, damage.data(), Succeed, nullptr) == Result::Completed);
    CHECK(cleanups == 1 && calls.size() == 1);
    CHECK(provider.Run(&request, damage.data(), Fail, nullptr) == Result::OperationFailed);
    CHECK(cleanups == 2 && calls.size() == 2);
    CHECK(provider.Run(&request, damage.data(), Fault, nullptr) == Result::Fault);
    CHECK(cleanups == 3);
    Nested nested{&provider, &request};
    CHECK(provider.Run(&request, damage.data(), Reenter, &nested) == Result::Completed);
    CHECK(cleanups == 5); // Inner and outer records, once each; no deadlock.

    // Shutdown must wait until the in-flight callback AND cleanup complete.
    Blocking block{CreateEventW(nullptr, TRUE, FALSE, nullptr),
        CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    CHECK(block.entered && block.release);
    auto operation = std::async(std::launch::async, [&] {
        return provider.Run(&request, damage.data(), Block, &block);
    });
    CHECK(WaitForSingleObject(block.entered, 5000) == WAIT_OBJECT_0);
    std::promise<void> stopping;
    auto stopped = std::async(std::launch::async, [&] {
        stopping.set_value();
        provider.Stop();
        return cleanups.load();
    });
    stopping.get_future().wait();
    CHECK(stopped.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout);
    SetEvent(block.release);
    CHECK(operation.get() == Result::Completed);
    CHECK(stopped.get() == 6);
    CloseHandle(block.entered);
    CloseHandle(block.release);
    CHECK(!provider.Accepts(&request));
    CHECK(provider.Run(&request, damage.data(), Succeed, nullptr) == Result::Rejected);
    CHECK(cleanups == 6);
    CHECK(provider.Publish(live.data(), native, CleanupFault));
    CHECK(provider.Run(&request, damage.data(), Succeed, nullptr) == Result::Fault);
    CHECK(cleanups == 7); // A cleanup fault is never retried.
    provider.Stop();

    ApiV1 api{sizeof(ApiV1), Version, DamageLayout,
        +[](const RequestV1*) noexcept { return true; },
        +[](const RequestV1*, void*, OperationFn, void*) noexcept { return Result::Rejected; }};
    CHECK(ValidApi(&api));
    CHECK(!ValidApi(nullptr));
    ++api.version; CHECK(!ValidApi(&api)); --api.version;
    --api.structSize; CHECK(!ValidApi(&api)); ++api.structSize;
    ++api.damageLayout; CHECK(!ValidApi(&api)); --api.damageLayout;
    api.run = nullptr; CHECK(!ValidApi(&api));
    if (argc == 2) {
        // Load the actual candidate without ever calling D2RLoaderLoadPlugin.
        // Its public ABI must negotiate safely while no game/hook is present.
        const auto module = LoadLibraryExA(argv[1], nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        CHECK(module != nullptr);
        if (module) {
            const auto symbol = GetProcAddress(module, ExportName);
            CHECK(symbol != nullptr);
            if (symbol) {
                GetApiFn getApi{};
                static_assert(sizeof(symbol) == sizeof(getApi));
                std::memcpy(&getApi, &symbol, sizeof(symbol));
                CHECK(getApi(Version + 1, sizeof(ApiV1)) == nullptr);
                CHECK(getApi(Version, sizeof(ApiV1) - 1) == nullptr);
                const auto* actual = getApi(Version, sizeof(ApiV1));
                CHECK(ValidApi(actual));
                if (ValidApi(actual)) {
                    CHECK(!actual->accepts(&request));
                    CHECK(actual->run(&request, damage.data(), Succeed, nullptr)
                        == Result::Rejected);
                    CHECK(cleanups == 7);
                }
            }
            FreeLibrary(module);
        }
    }
    std::cout << "damage cleanup ABI/lifetime tests: " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
