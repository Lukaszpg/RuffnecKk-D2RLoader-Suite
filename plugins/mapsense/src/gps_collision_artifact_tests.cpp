#include "gps_collision_artifact.hpp"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

using namespace RuffnecKk::MapSense;

int Failures{};

void Check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::cerr << "FAIL line " << line << ": " << expression << '\n';
    ++Failures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

template <typename T>
auto Read(const std::vector<std::uint8_t>& bytes, std::size_t offset) -> T {
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

auto Fixture() -> GpsCollisionProbeArtifact {
    GpsCollisionProbeArtifact artifact;
    artifact.session = "pid:42;generation:7";
    artifact.identity = {
        .sessionGeneration = 7U,
        .mapSeed = 1337U,
        .difficulty = 2U,
        .levelId = 4,
    };
    artifact.allRoomsActive = true;
    artifact.shadowCollisionsRebuilt = true;
    artifact.sessionStable = true;
    artifact.rooms = {{
        .levelId = 4,
        .subtileX = 10,
        .subtileY = 20,
        .width = 2,
        .height = 2,
        .relationStart = 0U,
        .relationCount = 1U,
        .cells = {1U, 2U, 3U, 4U},
    }};
    artifact.relations = {0U};
    return artifact;
}

} // namespace

int main(int argc, char** argv) {
    auto artifact = Fixture();
    std::vector<std::uint8_t> bytes;
    CHECK(SerializeGpsCollisionArtifact(artifact, bytes));
    CHECK(bytes.size() == 104U + 8U + 14U + 19U + 64U + 40U + 4U + 8U);
    CHECK(std::memcmp(bytes.data(), "MSNC", 4U) == 0);
    CHECK(Read<std::uint16_t>(bytes, 4U) == 1U);
    CHECK(Read<std::uint16_t>(bytes, 6U) == 104U);
    CHECK(Read<std::uint32_t>(bytes, 8U) == 7U);
    CHECK(Read<std::uint32_t>(bytes, 12U) == 0U);
    CHECK(Read<std::uint16_t>(bytes, 16U) == 8U);
    CHECK(Read<std::uint16_t>(bytes, 18U) == 14U);
    CHECK(Read<std::uint16_t>(bytes, 20U) == 19U);
    CHECK(Read<std::uint16_t>(bytes, 22U) == 64U);
    CHECK(Read<std::uint32_t>(bytes, 24U) == 1337U);
    CHECK(Read<std::uint32_t>(bytes, 28U) == 2U);
    CHECK(Read<std::int32_t>(bytes, 32U) == 4);
    CHECK(Read<std::uint32_t>(bytes, 36U) == 1U);
    CHECK(Read<std::uint32_t>(bytes, 40U) == 1U);
    CHECK(Read<std::uint32_t>(bytes, 44U) == 4U);
    CHECK(Read<std::uint32_t>(bytes, 48U) == 40U);
    CHECK(Read<std::uint32_t>(bytes, 52U) == 4U);
    CHECK(Read<std::uint32_t>(bytes, 56U) == 2U);
    CHECK(Read<std::uint32_t>(bytes, 100U) == 0U);
    if (argc == 2) {
        std::ofstream output(argv[1], std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        CHECK(output.good());
    }

    artifact.nativeFingerprint[0] = '0';
    CHECK(!SerializeGpsCollisionArtifact(artifact, bytes));
    CHECK(bytes.empty());
    artifact = Fixture();
    artifact.rooms[0].relationCount = 2U;
    CHECK(!SerializeGpsCollisionArtifact(artifact, bytes));
    artifact = Fixture();
    artifact.rooms[0].cells.pop_back();
    CHECK(!SerializeGpsCollisionArtifact(artifact, bytes));
    artifact = Fixture();
    artifact.rooms.push_back(artifact.rooms[0]);
    artifact.rooms[1].relationStart = 1U;
    artifact.relations.push_back(1U);
    CHECK(!SerializeGpsCollisionArtifact(artifact, bytes));
    artifact = Fixture();
    artifact.rooms[0].relationCount = 2U;
    artifact.relations = {0U, 0U};
    CHECK(!SerializeGpsCollisionArtifact(artifact, bytes));
    return Failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
