#include <RuffnecKk/native_stat_compat.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <type_traits>

namespace RuffnecKk::NativeStatCompat {
namespace {

[[nodiscard]] auto HexNibble(char value) noexcept -> std::uint8_t {
    if (value >= '0' && value <= '9') return static_cast<std::uint8_t>(value - '0');
    if (value >= 'a' && value <= 'f') return static_cast<std::uint8_t>(value - 'a' + 10);
    if (value >= 'A' && value <= 'F') return static_cast<std::uint8_t>(value - 'A' + 10);
    return 0xFF;
}

[[nodiscard]] auto HexSize(std::string_view hex) noexcept -> std::size_t {
    return hex.size() % 2 == 0 ? hex.size() / 2 : 0;
}

[[nodiscard]] auto Read(const MemoryReader& reader, const AddressRange& range,
        std::uintptr_t address, std::byte* output, std::size_t size) noexcept -> bool {
    return reader.read != nullptr && range.Contains(address, size)
        && reader.read(reader.userData, address, output, size);
}

[[nodiscard]] auto MatchesHex(const MemoryReader& reader, const AddressRange& range,
        std::uintptr_t address, std::string_view expected) noexcept -> bool {
    const auto size = HexSize(expected);
    if (size == 0 || size > 4096) return false;
    std::array<std::byte, 4096> actual{};
    if (!Read(reader, range, address, actual.data(), size)) return false;
    for (std::size_t index = 0; index < size; ++index) {
        const auto high = HexNibble(expected[index * 2]);
        const auto low = HexNibble(expected[index * 2 + 1]);
        if (high == 0xFF || low == 0xFF
            || std::to_integer<std::uint8_t>(actual[index]) != static_cast<std::uint8_t>((high << 4) | low)) return false;
    }
    return true;
}

template <typename Value>
[[nodiscard]] auto ReadValue(const MemoryReader& reader, const AddressRange& range,
        std::uintptr_t address, Value& output) noexcept -> bool {
    static_assert(std::is_trivially_copyable_v<Value>);
    return Read(reader, range, address, reinterpret_cast<std::byte*>(&output), sizeof(output));
}

[[nodiscard]] auto At(std::uintptr_t base, std::uint32_t rva, std::uintptr_t& output) noexcept -> bool {
    if (base > (std::numeric_limits<std::uintptr_t>::max)() - rva) return false;
    output = base + rva;
    return true;
}

[[nodiscard]] auto AddOffset(std::uintptr_t address, std::size_t offset, std::uintptr_t& output) noexcept -> bool {
    if (address > (std::numeric_limits<std::uintptr_t>::max)() - offset) return false;
    output = address + offset;
    return true;
}

[[nodiscard]] auto RelativeTarget(std::uintptr_t afterInstruction, std::int32_t displacement,
        std::uintptr_t& output) noexcept -> bool {
    if (displacement >= 0) return AddOffset(afterInstruction, static_cast<std::uint32_t>(displacement), output);
    const auto magnitude = static_cast<std::uint32_t>(-(static_cast<std::int64_t>(displacement)));
    if (afterInstruction < magnitude) return false;
    output = afterInstruction - magnitude;
    return true;
}

[[nodiscard]] auto MatchesDirectCall(const MemoryReader& reader, const AddressRange& core,
        std::uint32_t siteRva, std::uint32_t targetRva) noexcept -> bool {
    std::uintptr_t site{};
    std::array<std::byte, 5> bytes{};
    if (!At(core.base, siteRva, site) || !Read(reader, core, site, bytes.data(), 1)) return false;
    const auto opcode = std::to_integer<std::uint8_t>(bytes[0]);
    std::size_t instructionSize{};
    std::int32_t displacement{};
    if (opcode == 0xE8 || opcode == 0xE9) {
        instructionSize = 5;
        if (!Read(reader, core, site, bytes.data(), instructionSize)) return false;
        std::memcpy(&displacement, bytes.data() + 1, sizeof(displacement));
    } else if (opcode == 0xEB) {
        instructionSize = 2;
        if (!Read(reader, core, site, bytes.data(), instructionSize)) return false;
        displacement = static_cast<std::int8_t>(std::to_integer<std::uint8_t>(bytes[1]));
    } else {
        return false;
    }
    std::uintptr_t after{};
    std::uintptr_t resolved{};
    std::uintptr_t expected{};
    return AddOffset(site, instructionSize, after) && RelativeTarget(after, displacement, resolved)
        && At(core.base, targetRva, expected) && resolved == expected;
}

[[nodiscard]] auto MatchesIndirectCall(const MemoryReader& reader, const AddressRange& core,
        std::uint32_t siteRva, std::uint32_t slotRva) noexcept -> bool {
    std::uintptr_t site{};
    std::array<std::byte, 6> bytes{};
    if (!At(core.base, siteRva, site) || !Read(reader, core, site, bytes.data(), bytes.size())
        || std::to_integer<std::uint8_t>(bytes[0]) != 0xFF || std::to_integer<std::uint8_t>(bytes[1]) != 0x15) return false;
    std::int32_t displacement{};
    std::memcpy(&displacement, bytes.data() + 2, sizeof(displacement));
    std::uintptr_t after{};
    std::uintptr_t resolved{};
    std::uintptr_t expected{};
    return AddOffset(site, bytes.size(), after) && RelativeTarget(after, displacement, resolved)
        && At(core.base, slotRva, expected) && resolved == expected;
}

[[nodiscard]] auto MatchesProvider(const MemoryReader& reader, const AddressRange& main,
        const AddressRange& core, const HelperWitness& helper) noexcept -> bool {
    std::uintptr_t entry{};
    std::uintptr_t slot{};
    std::uintptr_t expectedExport{};
    std::array<std::byte, 6> prefix{};
    if (!At(main.base, helper.nativeRva, entry) || helper.patchSpan < prefix.size()
        || !Read(reader, main, entry, prefix.data(), prefix.size())
        || std::to_integer<std::uint8_t>(prefix[0]) != 0xFF || std::to_integer<std::uint8_t>(prefix[1]) != 0x25) return false;
    std::int32_t displacement{};
    std::memcpy(&displacement, prefix.data() + 2, sizeof(displacement));
    std::uintptr_t after{};
    if (!AddOffset(entry, prefix.size(), after) || !RelativeTarget(after, displacement, slot) || !At(core.base, helper.exportRva, expectedExport)) return false;
    std::uintptr_t destination{};
    std::uintptr_t padding{};
    std::uintptr_t tail{};
    if (!ReadValue(reader, main, slot, destination) || destination != expectedExport
        || !AddOffset(entry, prefix.size(), padding) || !AddOffset(entry, helper.patchSpan, tail)) return false;
    if (!helper.paddingExpectedHex.empty() && !MatchesHex(reader, main, padding, helper.paddingExpectedHex)) return false;
    if (!MatchesHex(reader, main, tail, helper.unchangedNativeTailExpectedHex)) return false;

    for (const auto& function : helper.functions) {
        std::uintptr_t functionAddress{};
        std::uintptr_t unwindAddress{};
        const auto functionSize = HexSize(function.expectedHex);
        const auto hasUnwind = !function.unwind.expectedHex.empty();
        if (functionSize == 0 || reader.validateUnwind == nullptr || !At(core.base, function.rva, functionAddress)
            || !MatchesHex(reader, core, functionAddress, function.expectedHex)
            || !reader.validateUnwind(reader.userData, core.base, function.rva, functionSize,
                function.unwind.rva, hasUnwind)) return false;
        if (hasUnwind && (!At(core.base, function.unwind.rva, unwindAddress)
            || !MatchesHex(reader, core, unwindAddress, function.unwind.expectedHex))) return false;
        for (const auto& call : function.directCalls) if (!MatchesDirectCall(reader, core, call.siteRva, call.targetRva)) return false;
        for (const auto& call : function.indirectCalls) if (!MatchesIndirectCall(reader, core, call.siteRva, call.slotRva)) return false;
        for (const auto& data : function.readOnlyData) {
            std::uintptr_t dataAddress{};
            if (!At(core.base, data.dataRva, dataAddress) || !MatchesHex(reader, core, dataAddress, data.expectedHex)) return false;
        }
    }

    std::array<std::uint64_t, 3> descriptor{};
    std::uintptr_t descriptorAddress{};
    std::uintptr_t expectedNative{};
    if (!At(core.base, helper.descriptorRva, descriptorAddress) || !At(main.base, helper.nativeRva, expectedNative)
        || !Read(reader, core, descriptorAddress,
            reinterpret_cast<std::byte*>(descriptor.data()), sizeof(descriptor))
        || descriptor[0] != expectedNative
        || descriptor[1] != helper.nativeRva
        || descriptor[2] != expectedNative) return false;
    for (const auto& imported : helper.imports) {
        std::array<std::uint64_t, 3> importSlot{};
        std::uintptr_t importAddress{};
        std::uintptr_t targetAddress{};
        if (!At(core.base, imported.slotRva, importAddress) || !At(main.base, imported.targetRva, targetAddress)
            || !Read(reader, core, importAddress,
                reinterpret_cast<std::byte*>(importSlot.data()), sizeof(importSlot))
            || importSlot[0] != targetAddress
            || importSlot[1] != imported.targetRva
            || importSlot[2] != targetAddress
            || !MatchesHex(reader, main, targetAddress, imported.targetExpectedHex)) return false;
    }
    return true;
}

[[nodiscard]] auto ReadCurrentProcess(void*, std::uintptr_t address,
        std::byte* output, std::size_t size) noexcept -> bool {
    if (output == nullptr || size == 0 || address > (std::numeric_limits<std::uintptr_t>::max)() - size) return false;
    auto cursor = address;
    const auto end = address + size;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION information{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &information, sizeof(information)) != sizeof(information)
            || information.State != MEM_COMMIT || information.RegionSize == 0
            || (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
        const auto protection = information.Protect & 0xFF;
        if (protection != PAGE_READONLY && protection != PAGE_READWRITE && protection != PAGE_WRITECOPY
            && protection != PAGE_EXECUTE_READ && protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) return false;
        const auto region = reinterpret_cast<std::uintptr_t>(information.BaseAddress);
        if (cursor < region || region > (std::numeric_limits<std::uintptr_t>::max)() - information.RegionSize) return false;
        cursor = (std::min)(end, region + information.RegionSize);
    }
    __try {
        std::memcpy(output, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

[[nodiscard]] auto ImageRange(std::uintptr_t base, AddressRange& range) noexcept -> bool {
    if (base == 0) return false;
    IMAGE_DOS_HEADER dos{};
    if (!ReadCurrentProcess(nullptr, base, reinterpret_cast<std::byte*>(&dos), sizeof(dos))
        || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0) return false;
    std::uintptr_t ntAddress{};
    IMAGE_NT_HEADERS64 nt{};
    if (!At(base, static_cast<std::uint32_t>(dos.e_lfanew), ntAddress)
        || !ReadCurrentProcess(nullptr, ntAddress, reinterpret_cast<std::byte*>(&nt), sizeof(nt))
        || nt.Signature != IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
        || nt.OptionalHeader.SizeOfImage == 0) return false;
    range = {base, nt.OptionalHeader.SizeOfImage};
    return true;
}

[[nodiscard]] auto ValidateCurrentUnwind(void*, std::uintptr_t imageBase, std::uint32_t functionRva,
        std::size_t functionSize, std::uint32_t unwindRva, bool hasUnwind) noexcept -> bool {
    if (functionSize == 0 || functionSize > (std::numeric_limits<std::uint32_t>::max)() - functionRva) return false;
    std::uintptr_t functionAddress{};
    if (!At(imageBase, functionRva, functionAddress)) return false;
    __try {
        DWORD64 resolvedBase{};
        const auto* entry = RtlLookupFunctionEntry(static_cast<DWORD64>(functionAddress), &resolvedBase, nullptr);
        if (!hasUnwind) return entry == nullptr;
        return entry != nullptr && resolvedBase == imageBase && entry->BeginAddress == functionRva
            && entry->EndAddress == functionRva + static_cast<std::uint32_t>(functionSize)
            && entry->UnwindData == unwindRva;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

#include "native_stat_compat_contract.inc"

} // namespace

auto AddressRange::Contains(std::uintptr_t address, std::size_t length) const noexcept -> bool {
    return address >= base && length <= size && address - base <= size - length;
}

void Adapter::Reset() noexcept { entries_ = {}; routes_ = {}; admitted_ = 0; required_ = 0; failure_ = Failure::InvalidArguments; }

auto Adapter::Bind(const MemoryReader& reader, AddressRange mainImage, AddressRange coreImage,
        const AdmissionContract& contract, HelperMask required) noexcept -> bool {
    Reset();
    if (reader.read == nullptr || mainImage.size == 0
        || contract.helpers.size() != entries_.size() || required == 0 || (required & ~ToMask(Helper::All)) != 0) return false;
    required_ = required;
    for (std::size_t index = 0; index < contract.helpers.size(); ++index) {
        if ((required & (1U << index)) == 0) continue;
        const auto& helper = contract.helpers[index];
        std::uintptr_t entry{};
        if (!At(mainImage.base, helper.nativeRva, entry)) { Reset(); failure_ = Failure::InvalidArguments; return false; }
        if (MatchesHex(reader, mainImage, entry, helper.canonicalExpectedHex)) {
            entries_[index] = entry;
            routes_[index] = Route::Canonical;
            admitted_ |= 1U << index;
            continue;
        }
        if (MatchesProvider(reader, mainImage, coreImage, helper)) {
            entries_[index] = entry;
            routes_[index] = Route::ProviderWide;
            admitted_ |= 1U << index;
            continue;
        }
        Reset();
        failure_ = Failure::ProviderWitness;
        return false;
    }
    failure_ = Failure::None;
    return true;
}

auto Adapter::BindCurrentProcess(std::uintptr_t mainImageBase, HelperMask required,
        const AdmissionContract& contract) noexcept -> bool {
    Reset();
    AddressRange main{};
    AddressRange core{};
    if (required == 0 || (required & ~ToMask(Helper::All)) != 0 || contract.helpers.size() != entries_.size()
        || !ImageRange(mainImageBase, main)) return false;
    bool needsCore = false;
    for (std::size_t index = 0; index < contract.helpers.size(); ++index) {
        if ((required & (1U << index)) == 0) continue;
        const auto& helper = contract.helpers[index];
        std::uintptr_t entry{};
        std::array<std::byte, 2> opcode{};
        if (!At(main.base, helper.nativeRva, entry)) return false;
        if (MatchesHex({nullptr, ReadCurrentProcess, ValidateCurrentUnwind}, main, entry, helper.canonicalExpectedHex)) continue;
        if (!ReadCurrentProcess(nullptr, entry, opcode.data(), opcode.size())
            || std::to_integer<std::uint8_t>(opcode[0]) != 0xFF || std::to_integer<std::uint8_t>(opcode[1]) != 0x25) return false;
        needsCore = true;
    }
    if (needsCore) {
        const auto coreModule = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"D2RCore.dll"));
        if (!ImageRange(coreModule, core)) return false;
    }
    return Bind({nullptr, ReadCurrentProcess, ValidateCurrentUnwind}, main, core, contract, required);
}

auto Adapter::GetUnitStat(void* unit, std::int32_t stat, std::uint16_t layer) const noexcept -> std::int32_t { if (!IsAdmitted(Helper::GetUnitStat)) return 0; return routes_[0] == Route::ProviderWide ? reinterpret_cast<ReadWideFn>(entries_[0])(unit, stat, static_cast<std::uint32_t>(layer)) : reinterpret_cast<ReadLegacyFn>(entries_[0])(unit, stat, layer); }
void Adapter::AddUnitStat(void* unit, std::int32_t stat, std::int32_t delta, std::uint16_t layer) const noexcept { if (!IsAdmitted(Helper::AddUnitStat)) return; if (routes_[1] == Route::ProviderWide) reinterpret_cast<AddWideFn>(entries_[1])(unit, stat, delta, static_cast<std::uint32_t>(layer)); else reinterpret_cast<AddLegacyFn>(entries_[1])(unit, stat, delta, layer); }
void Adapter::MergeStatLists(void* target, void* source, std::int32_t mode) const noexcept { if (IsAdmitted(Helper::MergeStatLists)) reinterpret_cast<MergeWideFn>(entries_[2])(target, source, mode); }
auto Adapter::ReadWeaponMastery(void* unit, void* weapon) const noexcept -> std::int32_t { return IsAdmitted(Helper::WeaponMastery) ? reinterpret_cast<CriticalFn>(entries_[3])(unit, weapon, nullptr, 2) : 0; }
auto Adapter::GetUnitBaseStat(void* unit, std::int32_t stat, std::uint16_t layer) const noexcept -> std::int32_t { if (!IsAdmitted(Helper::GetUnitBaseStat)) return 0; return routes_[4] == Route::ProviderWide ? reinterpret_cast<ReadWideFn>(entries_[4])(unit, stat, static_cast<std::uint32_t>(layer)) : reinterpret_cast<ReadLegacyFn>(entries_[4])(unit, stat, layer); }
auto Adapter::GetUnitAlignment(void* unit) const noexcept -> std::int32_t { return IsAdmitted(Helper::GetUnitAlignment) ? reinterpret_cast<AlignmentFn>(entries_[6])(unit) : 0; }
void Adapter::SetUnitStat(void* unit, std::int32_t stat, std::int32_t value, std::uint16_t layer) const noexcept { if (!IsAdmitted(Helper::SetUnitStat)) return; if (routes_[5] == Route::ProviderWide) (void)reinterpret_cast<SetWideFn>(entries_[5])(unit, stat, value, static_cast<std::uint32_t>(layer)); else reinterpret_cast<SetLegacyFn>(entries_[5])(unit, stat, value, layer); }
void Adapter::SetUnitStatWide(void* unit, std::int32_t stat, std::int32_t value, std::uint32_t layer) const noexcept {
    if (!IsAdmitted(Helper::SetUnitStat)) return;
    if (routes_[5] == Route::ProviderWide) {
        (void)reinterpret_cast<SetWideFn>(entries_[5])(unit, stat, value, layer);
    } else if (routes_[5] == Route::Canonical) {
        reinterpret_cast<SetLegacyFn>(entries_[5])(unit, stat, value, static_cast<std::uint16_t>(layer));
    }
}

auto Loader130StatAdmissionContract() noexcept -> const AdmissionContract& { return kAdmissionContract; }

} // namespace RuffnecKk::NativeStatCompat
