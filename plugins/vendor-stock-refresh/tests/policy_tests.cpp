#include "native_contract.hpp"
#include "policy.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

namespace {

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (false)

bool ReadUint16(const std::string& bytes, std::size_t offset, std::uint16_t& value) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(value)) return false;
    value = static_cast<std::uint16_t>(
        static_cast<unsigned char>(bytes[offset])
        | static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[offset + 1])) << 8);
    return true;
}

bool ReadUint32(const std::string& bytes, std::size_t offset, std::uint32_t& value) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(value)) return false;
    value = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset]))
        | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 1])) << 8
        | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 2])) << 16
        | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 3])) << 24;
    return true;
}

void WriteUint16(std::string& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<char>(value & 0xFFU);
    bytes[offset + 1] = static_cast<char>((value >> 8) & 0xFFU);
}

void WriteUint32(std::string& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<char>(value & 0xFFU);
    bytes[offset + 1] = static_cast<char>((value >> 8) & 0xFFU);
    bytes[offset + 2] = static_cast<char>((value >> 16) & 0xFFU);
    bytes[offset + 3] = static_cast<char>((value >> 24) & 0xFFU);
}

bool PeFileOffsetForRva(
    const std::string& bytes,
    std::uint32_t rva,
    std::size_t& fileOffset
) {
    std::uint32_t peOffset{};
    std::uint16_t sectionCount{};
    std::uint16_t optionalHeaderSize{};
    if (bytes.size() < 0x40
        || bytes[0] != 'M'
        || bytes[1] != 'Z'
        || !ReadUint32(bytes, 0x3C, peOffset)
        || peOffset > bytes.size()
        || bytes.size() - peOffset < 24
        || bytes.compare(peOffset, 4, "PE\0\0", 4) != 0
        || !ReadUint16(bytes, peOffset + 6, sectionCount)
        || !ReadUint16(bytes, peOffset + 20, optionalHeaderSize)) {
        return false;
    }
    const auto sectionTable = static_cast<std::size_t>(peOffset) + 24 + optionalHeaderSize;
    if (sectionTable > bytes.size()
        || sectionCount > (bytes.size() - sectionTable) / 40) {
        return false;
    }
    for (std::uint16_t index{}; index < sectionCount; ++index) {
        const auto section = sectionTable + static_cast<std::size_t>(index) * 40;
        std::uint32_t virtualSize{};
        std::uint32_t virtualAddress{};
        std::uint32_t rawSize{};
        std::uint32_t rawOffset{};
        if (!ReadUint32(bytes, section + 8, virtualSize)
            || !ReadUint32(bytes, section + 12, virtualAddress)
            || !ReadUint32(bytes, section + 16, rawSize)
            || !ReadUint32(bytes, section + 20, rawOffset)
            || rva < virtualAddress) {
            continue;
        }
        const auto delta = rva - virtualAddress;
        if (delta >= rawSize
            || delta > bytes.size()
            || rawOffset > bytes.size() - delta) {
            continue;
        }
        fileOffset = static_cast<std::size_t>(rawOffset) + delta;
        return true;
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    using namespace RuffnecKk::VendorStockRefresh;

    CHECK(argc == 2 || argc == 3);
    std::string truncatedPe(512, '\0');
    truncatedPe[0] = 'M';
    truncatedPe[1] = 'Z';
    WriteUint32(truncatedPe, 0x3C, 0x80);
    std::memcpy(truncatedPe.data() + 0x80, "PE\0\0", 4);
    WriteUint16(truncatedPe, 0x86, 1);
    WriteUint16(truncatedPe, 0x94, 0);
    WriteUint32(truncatedPe, 0x98 + 8, 0x01000000);
    WriteUint32(truncatedPe, 0x98 + 12, 0);
    WriteUint32(truncatedPe, 0x98 + 16, 0x01000000);
    WriteUint32(truncatedPe, 0x98 + 20, 0);
    std::size_t truncatedOffset{};
    CHECK(!PeFileOffsetForRva(truncatedPe, 0x634A98, truncatedOffset));
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
        "[plugin]\nenabled = true\n[diagnostics]\nenabled = TRUE\n",
        config,
        error));
    CHECK(!ParseConfig(
        "[plugin]\nenabled = true\n[diagnostics]\nenabled = false\n"
        "extra = false\n",
        config,
        error));
    CHECK(!ParseConfig("[plugin]\nenabled = true\n", config, error));

    CHECK(RefreshActionForPanel(false) == NormalRefreshAction);
    CHECK(RefreshActionForPanel(true) == VanillaGambleRefreshAction);
    CHECK(ShouldShowNormalRefresh(false));
    CHECK(!ShouldShowNormalRefresh(true));

    constexpr WidgetRect vanillaGold{421, 1305, 313, 58};
    constexpr WidgetRect vanillaRefresh{877, 1277, 112, 112};
    constexpr auto vanillaPlacement = CenterBelow(vanillaGold, vanillaRefresh);
    static_assert(vanillaPlacement.valid);
    static_assert(vanillaPlacement.x == 521);
    static_assert(vanillaPlacement.y == 1382);

    constexpr WidgetRect moddedGold{600, 1500, 500, 80};
    constexpr WidgetRect moddedRefresh{1100, 1400, 160, 160};
    constexpr auto moddedPlacement = CenterBelow(moddedGold, moddedRefresh);
    static_assert(moddedPlacement.valid);
    static_assert(moddedPlacement.x == 770);
    static_assert(moddedPlacement.y == 1607);

    constexpr auto fallbackGold = UnionRect(
        WidgetRect{427, 1304, 57, 57},
        WidgetRect{487, 1309, 249, 48});
    static_assert(fallbackGold.x == 427);
    static_assert(fallbackGold.y == 1304);
    static_assert(fallbackGold.width == 309);
    static_assert(fallbackGold.height == 57);
    static_assert(!CenterBelow(WidgetRect{}, vanillaRefresh).valid);
    static_assert(!CenterBelow(vanillaGold, WidgetRect{}).valid);

    CHECK(ShouldArmNormalRefresh(true, NormalVendorMode, true, true));
    CHECK(!ShouldArmNormalRefresh(false, NormalVendorMode, true, true));
    CHECK(!ShouldArmNormalRefresh(true, GambleVendorMode, true, true));
    CHECK(!ShouldArmNormalRefresh(true, NormalVendorMode, false, true));
    CHECK(!ShouldArmNormalRefresh(true, NormalVendorMode, true, false));

    using namespace NativeContract;
    CHECK(Matches(VanillaBuilder.data(), VanillaBuilder));
    CHECK(MatchesRelayBuilder(VanillaBuilder.data()));
    auto relayedBuilder = VanillaBuilder;
    relayedBuilder[BuilderCallDisplacementOffset + 0] = 0xA7;
    relayedBuilder[BuilderCallDisplacementOffset + 1] = 0xE0;
    relayedBuilder[BuilderCallDisplacementOffset + 2] = 0xD3;
    relayedBuilder[BuilderCallDisplacementOffset + 3] = 0x03;
    CHECK(!Matches(relayedBuilder.data(), VanillaBuilder));
    CHECK(MatchesRelayBuilder(relayedBuilder.data()));
    auto mutatedBuilder = relayedBuilder;
    mutatedBuilder[0x19] ^= 0x01;
    CHECK(!MatchesRelayBuilder(mutatedBuilder.data()));

    CHECK(Matches(RelayStubOpcode.data(), RelayStubOpcode));
    auto invalidRelay = RelayStubOpcode;
    invalidRelay[1] = 0x15;
    CHECK(!Matches(invalidRelay.data(), RelayStubOpcode));
    CHECK(Matches(D2RCoreProviderEntry12.data(), D2RCoreProviderEntry12));
    auto invalidProvider = D2RCoreProviderEntry12;
    invalidProvider[0x20] ^= 0x01;
    CHECK(!Matches(invalidProvider.data(), D2RCoreProviderEntry12));
    CHECK(Matches(
        D2RCoreForwardingWitness12.data(),
        D2RCoreForwardingWitness12));
    auto invalidForwarding = D2RCoreForwardingWitness12;
    invalidForwarding[0x0B] ^= 0x01;
    CHECK(!Matches(invalidForwarding.data(), D2RCoreForwardingWitness12));
    CHECK(Matches(D2RCoreProviderEntry121.data(), D2RCoreProviderEntry121));
    CHECK(Matches(
        D2RCoreForwardingWitness121.data(),
        D2RCoreForwardingWitness121));
    CHECK(Matches(
        D2RCoreProviderEntry121Release.data(),
        D2RCoreProviderEntry121Release));
    CHECK(Matches(
        D2RCoreForwardingWitness121Release.data(),
        D2RCoreForwardingWitness121Release));
    CHECK(Matches(
        D2RCoreProviderEntryPublicPacket.data(),
        D2RCoreProviderEntryPublicPacket));
    CHECK(Matches(
        D2RCoreForwardingWitnessPublicPacket.data(),
        D2RCoreForwardingWitnessPublicPacket));
    CHECK(Matches(
        D2RCoreProviderEntryEligibilityCheckedPacket.data(),
        D2RCoreProviderEntryEligibilityCheckedPacket));
    CHECK(Matches(
        D2RCoreForwardingWitnessEligibilityCheckedPacket.data(),
        D2RCoreForwardingWitnessEligibilityCheckedPacket));
    CHECK(IdentifyD2RCoreProviderProfile(
        D2RCoreProviderEntry12.data(),
        D2RCoreForwardingWitness12.data())
        == D2RCoreProviderProfile::D2RLoader12);
    CHECK(IdentifyD2RCoreProviderProfile(
        D2RCoreProviderEntry121.data(),
        D2RCoreForwardingWitness121.data())
        == D2RCoreProviderProfile::D2RLoader121);
    CHECK(IdentifyD2RCoreProviderProfile(
        D2RCoreProviderEntry121Release.data(),
        D2RCoreForwardingWitness121Release.data())
        == D2RCoreProviderProfile::D2RLoader121Release);
    // Runtime build identity is diagnostic only: a complete fingerprint admits
    // the public packet provider even when no identity is available.
    CHECK(IdentifyD2RCoreProviderProfile(
        D2RCoreProviderEntryPublicPacket.data(),
        D2RCoreForwardingWitnessPublicPacket.data())
        == D2RCoreProviderProfile::PublicPacketProvider);
    CHECK(IdentifyD2RCoreProviderProfile(
        D2RCoreProviderEntryEligibilityCheckedPacket.data(),
        D2RCoreForwardingWitnessEligibilityCheckedPacket.data())
        == D2RCoreProviderProfile::EligibilityCheckedPacketProvider);
    CHECK(IdentifyD2RCoreProviderProfile(
        D2RCoreProviderEntry12.data(),
        D2RCoreForwardingWitness121.data())
        == D2RCoreProviderProfile::Invalid);
    CHECK(IdentifyD2RCoreProviderProfile(
        invalidProvider.data(),
        D2RCoreForwardingWitness12.data())
        == D2RCoreProviderProfile::Invalid);
    // A known runtime must still refuse a changed, absent, or partial witness.
    auto invalidPublicProvider = D2RCoreProviderEntryPublicPacket;
    invalidPublicProvider[0x3F] ^= 0x01;
    CHECK(IdentifyD2RCoreProviderProfile(
        invalidPublicProvider.data(),
        D2RCoreForwardingWitnessPublicPacket.data())
        == D2RCoreProviderProfile::Invalid);
    CHECK(IdentifyD2RCoreProviderProfile(
        nullptr,
        D2RCoreForwardingWitnessPublicPacket.data())
        == D2RCoreProviderProfile::Invalid);
    CHECK(IdentifyD2RCoreProviderProfile(
        D2RCoreProviderEntryPublicPacket.data(),
        nullptr)
        == D2RCoreProviderProfile::Invalid);
    auto invalidEligibilityCheckedProvider =
        D2RCoreProviderEntryEligibilityCheckedPacket;
    invalidEligibilityCheckedProvider[0x3F] ^= 0x01;
    CHECK(IdentifyD2RCoreProviderProfile(
        invalidEligibilityCheckedProvider.data(),
        D2RCoreForwardingWitnessEligibilityCheckedPacket.data())
        == D2RCoreProviderProfile::Invalid);
    CHECK(SelectUniqueD2RCoreProviderProfile(
        false, true, true, false, false)
        == D2RCoreProviderProfile::Invalid);
    static_assert(D2RCoreProviderSize12 == 0x19A);
    static_assert(D2RCoreProviderSize121 == 0x170);
    static_assert(D2RCoreProviderSize121Release == 0x170);
    static_assert(D2RCoreProviderSizePublicPacket == 0x170);
    static_assert(D2RCoreProviderSizeEligibilityCheckedPacket == 0x1A4);
    static_assert(D2RCoreProviderHash12.size() == 32);
    static_assert(D2RCoreProviderHash121.size() == 32);
    static_assert(D2RCoreProviderHash121Release.size() == 32);
    static_assert(D2RCoreProviderHashPublicPacket.size() == 32);
    static_assert(D2RCoreProviderHashEligibilityCheckedPacket.size() == 32);
    const auto publicProviderForwardingSlot = ResolveRelativeTarget(
        D2RCoreProviderRvaPublicPacket
            + ProviderForwardingOffset
            + ProviderForwardingCallOffset,
        6,
        -0x106A9C);
    CHECK(publicProviderForwardingSlot
        && *publicProviderForwardingSlot
            == D2RCoreProviderForwardingSlotRvaPublicPacket);
    const auto eligibilityCheckedProviderForwardingSlot = ResolveRelativeTarget(
        D2RCoreProviderRvaEligibilityCheckedPacket
            + ProviderForwardingOffset
            + ProviderForwardingCallOffset,
        6,
        -0x11AB4C);
    CHECK(eligibilityCheckedProviderForwardingSlot
        && *eligibilityCheckedProviderForwardingSlot
            == D2RCoreProviderForwardingSlotRvaEligibilityCheckedPacket);
    if (argc == 3) {
        std::ifstream coreInput(argv[2], std::ios::binary);
        CHECK(coreInput.good());
        const std::string coreBytes{
            std::istreambuf_iterator<char>(coreInput), std::istreambuf_iterator<char>()};
        std::size_t providerOffset{};
        std::size_t forwardingOffset{};
        std::size_t unwindOffset{};
        std::size_t funcInfoSlotOffset{};
        std::size_t funcInfoOffset{};
        CHECK(PeFileOffsetForRva(
            coreBytes,
            D2RCoreProviderRvaEligibilityCheckedPacket,
            providerOffset));
        CHECK(PeFileOffsetForRva(
            coreBytes,
            D2RCoreProviderRvaEligibilityCheckedPacket
                + ProviderForwardingOffset,
            forwardingOffset));
        CHECK(PeFileOffsetForRva(
            coreBytes,
            D2RCoreProviderUnwindRvaEligibilityCheckedPacket,
            unwindOffset));
        CHECK(PeFileOffsetForRva(
            coreBytes,
            D2RCoreProviderUnwindRvaEligibilityCheckedPacket + 28,
            funcInfoSlotOffset));
        std::uint32_t funcInfoRva{};
        CHECK(ReadUint32(coreBytes, funcInfoSlotOffset, funcInfoRva));
        CHECK(funcInfoSlotOffset == unwindOffset + 28);
        CHECK(funcInfoRva == D2RCoreProviderFuncInfoRvaEligibilityCheckedPacket);
        CHECK(funcInfoRva
            != D2RCoreProviderUnwindRvaEligibilityCheckedPacket + 28);
        CHECK(PeFileOffsetForRva(coreBytes, funcInfoRva, funcInfoOffset));
        CHECK(providerOffset <= coreBytes.size());
        CHECK(coreBytes.size() - providerOffset
            >= D2RCoreProviderSizeEligibilityCheckedPacket);
        CHECK(std::memcmp(
            coreBytes.data() + providerOffset,
            D2RCoreProviderEntryEligibilityCheckedPacket.data(),
            D2RCoreProviderEntryEligibilityCheckedPacket.size()) == 0);
        CHECK(forwardingOffset <= coreBytes.size());
        CHECK(coreBytes.size() - forwardingOffset
            >= D2RCoreForwardingWitnessEligibilityCheckedPacket.size());
        CHECK(std::memcmp(
            coreBytes.data() + forwardingOffset,
            D2RCoreForwardingWitnessEligibilityCheckedPacket.data(),
            D2RCoreForwardingWitnessEligibilityCheckedPacket.size()) == 0);
        CHECK(unwindOffset <= coreBytes.size());
        CHECK(coreBytes.size() - unwindOffset
            >= D2RCoreProviderUnwindEligibilityCheckedPacket.size());
        CHECK(std::memcmp(
            coreBytes.data() + unwindOffset,
            D2RCoreProviderUnwindEligibilityCheckedPacket.data(),
            D2RCoreProviderUnwindEligibilityCheckedPacket.size()) == 0);
        CHECK(funcInfoOffset <= coreBytes.size());
        CHECK(coreBytes.size() - funcInfoOffset
            >= D2RCoreProviderFuncInfoEligibilityCheckedPacket.size());
        CHECK(std::memcmp(
            coreBytes.data() + funcInfoOffset,
            D2RCoreProviderFuncInfoEligibilityCheckedPacket.data(),
            D2RCoreProviderFuncInfoEligibilityCheckedPacket.size()) == 0);
        const std::string pdataBytes{
            reinterpret_cast<const char*>(
                D2RCoreProviderPdataEligibilityCheckedPacket.data()),
            D2RCoreProviderPdataEligibilityCheckedPacket.size()};
        const auto pdataOffset = coreBytes.find(pdataBytes);
        CHECK(pdataOffset != std::string::npos);
        CHECK(coreBytes.find(pdataBytes, pdataOffset + 1) == std::string::npos);
    }
    CHECK(Matches(DownstreamQueueEntry.data(), DownstreamQueueEntry));
    auto invalidDownstream = DownstreamQueueEntry;
    invalidDownstream[0x14] ^= 0x01;
    CHECK(!Matches(invalidDownstream.data(), DownstreamQueueEntry));

    const auto forwardTarget = ResolveRelativeTarget(0x1000, 5, 0x200);
    CHECK(forwardTarget && *forwardTarget == 0x1205);
    const auto backwardTarget = ResolveRelativeTarget(0x1000, 6, -0x206);
    CHECK(backwardTarget && *backwardTarget == 0x0E00);
    CHECK(!AddSignedDisplacement(0, -1));
    CHECK(!AddSignedDisplacement(
        std::numeric_limits<std::uintptr_t>::max(), 1));
    return EXIT_SUCCESS;
}
