#include "gps_collision_artifact.hpp"

#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <string>
#include <system_error>
#include <type_traits>
#include <set>
#include <unordered_set>

namespace RuffnecKk::MapSense {
namespace {

constexpr std::size_t HeaderBytes = 104U;
constexpr std::size_t RoomRecordBytes = 40U;
constexpr std::size_t MaximumArtifactBytes = 80U * 1024U * 1024U;

template <typename T>
void AppendLe(std::vector<std::uint8_t>& bytes, T value) {
    using Unsigned = std::make_unsigned_t<T>;
    const auto raw = static_cast<Unsigned>(value);
    for (std::size_t index = 0U; index < sizeof(T); ++index) {
        bytes.push_back(static_cast<std::uint8_t>(raw >> (index * 8U)));
    }
}

void AppendText(std::vector<std::uint8_t>& bytes, const std::string& value) {
    bytes.insert(bytes.end(), value.begin(), value.end());
}

[[nodiscard]] auto Sha256(
        std::span<const std::uint8_t> bytes,
        std::array<std::uint8_t, 32U>& output) noexcept -> bool {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(
            &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0U) < 0) {
        return false;
    }
    const auto created = BCryptCreateHash(
        algorithm, &hash, nullptr, 0U, nullptr, 0U, 0U);
    const auto updated = created >= 0 && BCryptHashData(
        hash,
        const_cast<PUCHAR>(bytes.data()),
        static_cast<ULONG>(bytes.size()),
        0U) >= 0;
    const auto finished = updated && BCryptFinishHash(
        hash, output.data(), static_cast<ULONG>(output.size()), 0U) >= 0;
    if (hash != nullptr) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0U);
    return finished;
}

[[nodiscard]] auto HasValidText(
        const std::string& value, std::size_t maximum) noexcept -> bool {
    return !value.empty() && value.size() <= maximum
        && value.find('\0') == std::string::npos;
}

[[nodiscard]] auto DiagnosticDirectory(std::filesystem::path& output) -> bool {
    wchar_t* localAppData{};
    std::size_t length{};
    if (_wdupenv_s(&localAppData, &length, L"LOCALAPPDATA") != 0
        || localAppData == nullptr || length <= 1U) {
        std::free(localAppData);
        return false;
    }
    output = std::filesystem::path(localAppData)
        / L"RuffnecKk" / L"MapSense" / L"gps-diagnostics";
    std::free(localAppData);
    std::error_code error;
    std::filesystem::create_directories(output, error);
    return !error;
}

} // namespace

auto SerializeGpsCollisionArtifact(
        const GpsCollisionProbeArtifact& artifact,
        std::vector<std::uint8_t>& output) noexcept -> bool {
    output.clear();
    try {
        if (artifact.version != GpsCollisionProbeArtifactVersion
            || artifact.producer != "MapSense"
            || artifact.profile != "shadow-CollMap"
            || artifact.nativeFingerprint != GpsCollisionProbeNativeProfileId
            || !HasValidText(artifact.producer, 128U)
            || !HasValidText(artifact.profile, 128U)
            || !HasValidText(artifact.session, 256U)
            || artifact.nativeFingerprint.size() != 64U
            || artifact.identity.levelId <= 0
            || artifact.identity.difficulty > 2U
            || artifact.rooms.empty() || artifact.rooms.size() > 4'096U
            || artifact.relations.size() > 262'144U) {
            return false;
        }
        std::uint64_t cellCount{};
        std::uint32_t relationCursor{};
        std::set<std::array<std::int32_t, 5U>> rectangles;
        for (const auto& room : artifact.rooms) {
            if (room.levelId != artifact.identity.levelId
                || room.subtileX < 0 || room.subtileY < 0
                || room.width <= 0 || room.height <= 0
                || static_cast<std::uint64_t>(room.width)
                    * static_cast<std::uint64_t>(room.height) != room.cells.size()
                || room.relationStart != relationCursor
                || room.relationCount > artifact.relations.size() - relationCursor) {
                return false;
            }
            if (!rectangles.insert({room.levelId, room.subtileX, room.subtileY,
                    room.width, room.height}).second) {
                return false;
            }
            std::unordered_set<std::uint32_t> relationTargets;
            for (std::uint32_t index = 0U; index < room.relationCount; ++index) {
                const auto target = artifact.relations[room.relationStart + index];
                if (target >= artifact.rooms.size()
                    || !relationTargets.insert(target).second) {
                    return false;
                }
            }
            relationCursor += room.relationCount;
            cellCount += room.cells.size();
            if (cellCount > 16'777'216U) return false;
        }
        if (relationCursor != artifact.relations.size()) {
            return false;
        }

        std::vector<std::uint8_t> body;
        const auto metadataBytes = artifact.producer.size() + artifact.profile.size()
            + artifact.session.size() + artifact.nativeFingerprint.size();
        const auto bodyBytes = metadataBytes + artifact.rooms.size() * RoomRecordBytes
            + artifact.relations.size() * sizeof(std::uint32_t)
            + static_cast<std::size_t>(cellCount) * sizeof(std::uint16_t);
        if (HeaderBytes + bodyBytes > MaximumArtifactBytes
            || bodyBytes > std::numeric_limits<std::uint32_t>::max()) return false;
        body.reserve(bodyBytes);
        AppendText(body, artifact.producer);
        AppendText(body, artifact.profile);
        AppendText(body, artifact.session);
        AppendText(body, artifact.nativeFingerprint);
        std::uint32_t cellCursor{};
        for (const auto& room : artifact.rooms) {
            AppendLe(body, room.levelId);
            AppendLe(body, room.subtileX);
            AppendLe(body, room.subtileY);
            AppendLe(body, room.width);
            AppendLe(body, room.height);
            AppendLe(body, room.relationStart);
            AppendLe(body, room.relationCount);
            AppendLe(body, cellCursor);
            AppendLe(body, static_cast<std::uint32_t>(room.cells.size()));
            AppendLe(body, std::uint32_t{0U});
            cellCursor += static_cast<std::uint32_t>(room.cells.size());
        }
        for (const auto relation : artifact.relations) AppendLe(body, relation);
        for (const auto& room : artifact.rooms) {
            for (const auto cell : room.cells) AppendLe(body, cell);
        }
        if (body.size() != bodyBytes) return false;
        std::array<std::uint8_t, 32U> bodyHash{};
        if (!Sha256(body, bodyHash)) return false;

        output.reserve(HeaderBytes + body.size());
        output.insert(output.end(), {'M', 'S', 'N', 'C'});
        AppendLe(output, std::uint16_t{1U});
        AppendLe(output, static_cast<std::uint16_t>(HeaderBytes));
        std::uint32_t flags{};
        if (artifact.allRoomsActive) flags |= GpsCollisionArtifactAllRoomsActive;
        if (artifact.shadowCollisionsRebuilt) flags |= GpsCollisionArtifactShadowRebuilt;
        if (artifact.sessionStable) flags |= GpsCollisionArtifactSessionStable;
        AppendLe(output, flags);
        AppendLe(output, std::uint32_t{0U});
        AppendLe(output, static_cast<std::uint16_t>(artifact.producer.size()));
        AppendLe(output, static_cast<std::uint16_t>(artifact.profile.size()));
        AppendLe(output, static_cast<std::uint16_t>(artifact.session.size()));
        AppendLe(output, static_cast<std::uint16_t>(artifact.nativeFingerprint.size()));
        AppendLe(output, artifact.identity.mapSeed);
        AppendLe(output, static_cast<std::uint32_t>(artifact.identity.difficulty));
        AppendLe(output, artifact.identity.levelId);
        AppendLe(output, static_cast<std::uint32_t>(artifact.rooms.size()));
        AppendLe(output, static_cast<std::uint32_t>(artifact.relations.size()));
        AppendLe(output, static_cast<std::uint32_t>(cellCount));
        AppendLe(output, static_cast<std::uint32_t>(RoomRecordBytes));
        AppendLe(output, std::uint32_t{4U});
        AppendLe(output, std::uint32_t{2U});
        AppendLe(output, static_cast<std::uint32_t>(metadataBytes));
        AppendLe(output, static_cast<std::uint32_t>(body.size()));
        output.insert(output.end(), bodyHash.begin(), bodyHash.end());
        AppendLe(output, std::uint32_t{0U});
        output.insert(output.end(), body.begin(), body.end());
        return output.size() == HeaderBytes + body.size();
    } catch (...) {
        output.clear();
        return false;
    }
}

auto WriteGpsCollisionArtifact(
        const GpsCollisionProbeArtifact& artifact,
        std::filesystem::path& output) noexcept -> bool {
    output.clear();
    try {
        std::vector<std::uint8_t> bytes;
        std::filesystem::path directory;
        if (!SerializeGpsCollisionArtifact(artifact, bytes)
            || !DiagnosticDirectory(directory)) return false;
        const auto name = L"mapsense-gps-" + std::to_wstring(GetCurrentProcessId())
            + L"-" + std::to_wstring(artifact.identity.sessionGeneration)
            + L"-" + std::to_wstring(artifact.identity.mapSeed)
            + L"-" + std::to_wstring(artifact.identity.difficulty)
            + L"-" + std::to_wstring(artifact.identity.levelId) + L".msnc";
        const auto path = directory / name;
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0U, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        std::size_t offset{};
        bool written = true;
        while (offset < bytes.size()) {
            DWORD count{};
            const auto remaining = static_cast<DWORD>(std::min<std::size_t>(
                bytes.size() - offset, std::numeric_limits<DWORD>::max()));
            if (!WriteFile(file, bytes.data() + offset, remaining, &count, nullptr)
                || count != remaining) {
                written = false;
                break;
            }
            offset += count;
        }
        written = written && FlushFileBuffers(file) != FALSE;
        CloseHandle(file);
        if (!written) {
            DeleteFileW(path.c_str());
            return false;
        }
        output = path;
        return true;
    } catch (...) {
        output.clear();
        return false;
    }
}

} // namespace RuffnecKk::MapSense
