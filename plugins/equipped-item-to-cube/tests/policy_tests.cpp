#include "policy.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

namespace {

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (false)

} // namespace

int main(int argc, char** argv) {
    using namespace RuffnecKk::EquippedItemToCube;

    CHECK(argc == 2);
    std::ifstream input(argv[1], std::ios::binary);
    CHECK(input.good());
    const std::string text{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    Config config{};
    std::string error;
    CHECK(ParseConfig(text, config, error));
    CHECK(config.enabled);
    CHECK(!config.diagnosticsEnabled);
    CHECK(ParseConfig(
        "[plugin]\nenabled = false\n[diagnostics]\nenabled = true\n",
        config,
        error));
    CHECK(!config.enabled && config.diagnosticsEnabled);
    CHECK(!ParseConfig(
        "[plugin]\nenabled = true\n[diagnostics]\n", config, error));
    CHECK(!ParseConfig(
        "[plugin]\nenabled = true\n[plugin]\nenabled = false\n"
        "[diagnostics]\nenabled = false\n",
        config,
        error));
    CHECK(!ParseConfig(
        "[plugin]\nunknown = true\n[diagnostics]\nenabled = false\n",
        config,
        error));

    static_assert(HasNativeControl(NativeControl));
    static_assert(HasNativeControl(NativeLeftControl));
    static_assert(HasNativeControl(NativeRightControl));
    static_assert(!HasNativeControl(Win32AsyncControl));
    static_assert(HasWin32Control(Win32AsyncControl));
    static_assert(HasWin32Control(Win32AsyncLeftControl));
    static_assert(HasWin32Control(Win32StateRightControl));
    static_assert(!HasWin32Control(NativeControl));
    static_assert(LegacyControlAccepted(Win32AsyncControl));
    static_assert(!LegacyControlAccepted(NativeControl));
    static_assert(!LegacyControlAccepted(Win32AsyncLeftControl));
    static_assert(!LegacyControlAccepted(Win32StateControl));

    constexpr TwentyOneByteCommand inventoryCommand{
        .opcode = InventoryTransferOpcode,
        .field1 = 37,
        .field2 = 91,
        .field3 = 92,
        .field4 = CubeInventoryPage,
        .field5 = 5u | (3u << 16),
    };

    static_assert(IsEquippedBodyLocation(1));
    static_assert(IsEquippedBodyLocation(10));
    static_assert(!IsEquippedBodyLocation(0));
    static_assert(!IsEquippedBodyLocation(11));
    static_assert(ShouldRewriteCubeTransfer(true, inventoryCommand, 4));
    static_assert(!ShouldRewriteCubeTransfer(false, inventoryCommand, 4));
    static_assert(!ShouldRewriteCubeTransfer(true, inventoryCommand, 0));
    static_assert(!ShouldRewriteCubeTransfer(
        true, inventoryCommand, BodyLocationCount));

    auto wrongOpcode = inventoryCommand;
    wrongOpcode.opcode = 0x55;
    CHECK(!ShouldRewriteCubeTransfer(true, wrongOpcode, 4));
    auto wrongPage = inventoryCommand;
    wrongPage.field4 = 0;
    CHECK(!ShouldRewriteCubeTransfer(true, wrongPage, 4));

    constexpr auto equippedCommand = RewriteAsEquippedTransfer(
        inventoryCommand, 4);
    static_assert(equippedCommand.opcode == EquippedTransferOpcode);
    static_assert(equippedCommand.field1 == 37);
    static_assert(equippedCommand.field2 == SelfTargetGuid);
    static_assert(equippedCommand.field3 == 4);
    static_assert(equippedCommand.field4 == CubeInventoryPage);
    static_assert(equippedCommand.field5 == (5u | (3u << 16)));
    return EXIT_SUCCESS;
}
