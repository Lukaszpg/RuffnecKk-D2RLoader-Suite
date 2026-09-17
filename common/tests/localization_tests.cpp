#include <RuffnecKk/localization.hpp>
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)

namespace {
using QueryResult = D2RL::ServiceQueryResult;
using Result = D2RL::Localization::Result;
std::array<std::uint32_t, 4> Queries{};
std::size_t QueryCount{};
QueryResult V2Result{QueryResult::Success};
QueryResult V1Result{QueryResult::Success};
const void* V2Table{};
const void* V1Table{};
const char* LastKey{};
const D2RL::PluginContext* LastContext{};

auto __cdecl ById(const D2RL::PluginContext*, std::uint32_t, char*,
        std::uint32_t, std::uint32_t*) noexcept -> Result { return Result::NotFound; }
auto __cdecl ByKey(const D2RL::PluginContext* context, const char* key,
        char* output, std::uint32_t size, std::uint32_t* required) noexcept -> Result {
    LastContext = context;
    LastKey = key;
    if (required != nullptr) *required = 3;
    if (output == nullptr || size < 3) return Result::BufferTooSmall;
    std::memcpy(output, "ok", 3);
    return Result::Success;
}
auto __cdecl Query(const D2RL::PluginContext*, D2RL::ServiceId id,
        std::uint32_t version, const void** output) noexcept -> QueryResult {
    CHECK(id == D2RL::ServiceId::Localization);
    CHECK(QueryCount < Queries.size());
    Queries[QueryCount++] = version;
    *output = version == 2 ? V2Table : V1Table;
    return version == 2 ? V2Result : V1Result;
}
void ResetQueries() { QueryCount = 0; Queries.fill(0); }
}

int main() {
    RuffnecKk::Localization::ServiceV2 v2{24, 2, ById, ByKey};
    D2RL::LocalizationServiceV1 v1{24, 1, ById, ByKey};
    V2Table = &v2;
    V1Table = &v1;
    D2RL::PluginApi api{};
    api.apiSize = sizeof(api);
    api.queryService = Query;
    D2RL::PluginContext context{};
    context.contextSize = sizeof(context);
    context.apiVersion = 3;
    context.api = &api;
    context.pluginId = "ruffneckk-test";
    context.buildName = "unknown-future-channel-and-build";
    context.buildVersion = "unpublished";
    RuffnecKk::Localization::Service service;
    CHECK(service.Bind(&context));
    CHECK(service.Version() == 2 && QueryCount == 1 && Queries[0] == 2);
    std::uint32_t required{};
    CHECK(service.GetStringByKey(&context, "ItemStats1h", "d2r:ItemStats1h",
        nullptr, 0, &required) == Result::BufferTooSmall);
    CHECK(required == 3 && std::strcmp(LastKey, "d2r:ItemStats1h") == 0);
    CHECK(LastContext == &context);
    std::array<char, 3> output{};
    CHECK(service.GetStringByKey(&context, "legacy-private", "other-plugin:custom",
        output.data(), 3, &required) == Result::Success);
    CHECK(std::strcmp(LastKey, "other-plugin:custom") == 0 && std::strcmp(output.data(), "ok") == 0);
    CHECK(service.GetStringByKey(&context, "custom", "custom", nullptr, 0, nullptr) == Result::BufferTooSmall);
    CHECK(std::strcmp(LastKey, "custom") == 0); // No invented namespace.

    V2Result = QueryResult::UnsupportedVersion;
    ResetQueries();
    CHECK(service.Bind(&context));
    CHECK(service.Version() == 1 && QueryCount == 2 && Queries[0] == 2 && Queries[1] == 1);
    CHECK(service.GetStringByKey(&context, "ItemStats1h", "d2r:ItemStats1h", nullptr, 0, nullptr) == Result::BufferTooSmall);
    CHECK(std::strcmp(LastKey, "ItemStats1h") == 0);

    for (auto failure : {QueryResult::InvalidArgument, QueryResult::UnknownService,
            QueryResult::Unavailable, QueryResult::OwnerInactive}) {
        V2Result = failure;
        ResetQueries();
        CHECK(!service.Bind(&context) && !service && service.Version() == 0);
        CHECK(QueryCount == 1);
    }
    V2Result = QueryResult::Success;
    for (int malformed = 0; malformed < 5; ++malformed) {
        v2 = {24, 2, ById, ByKey};
        V2Table = &v2;
        if (malformed == 0) V2Table = nullptr;
        if (malformed == 1) v2.serviceSize = 16;
        if (malformed == 2) v2.serviceVersion = 1;
        if (malformed == 3) v2.getStringById = nullptr;
        if (malformed == 4) v2.getStringByKey = nullptr;
        ResetQueries();
        CHECK(!service.Bind(&context) && QueryCount == 1 && !service);
    }
    V2Result = QueryResult::UnsupportedVersion;
    for (int malformed = 0; malformed < 6; ++malformed) {
        v1 = {24, 1, ById, ByKey};
        V1Table = &v1;
        V1Result = QueryResult::Success;
        if (malformed == 0) V1Table = nullptr;
        if (malformed == 1) v1.serviceSize = 16;
        if (malformed == 2) v1.serviceVersion = 2;
        if (malformed == 3) v1.getStringById = nullptr;
        if (malformed == 4) v1.getStringByKey = nullptr;
        if (malformed == 5) V1Result = QueryResult::OwnerInactive;
        ResetQueries();
        CHECK(!service.Bind(&context) && QueryCount == 2 && !service);
    }
    CHECK(!service.Bind(nullptr));
    CHECK(service.GetStringByKey(&context, "key", "d2r:key", nullptr, 0, nullptr) == Result::Unavailable);
    V2Result = QueryResult::Success;
    v2 = {24, 2, ById, ByKey};
    V2Table = &v2;
    for (const auto* identity : {"unknown", "Steam 93787", "Battle.net 93847"}) {
        context.buildName = identity;
        ResetQueries();
        CHECK(service.Bind(&context) && service.Version() == 2);
        v2.serviceSize = 8;
        ResetQueries();
        CHECK(!service.Bind(&context) && QueryCount == 1);
        v2.serviceSize = 24;
    }
    std::puts("PASS: Localization capabilities, malformed tables, explicit keys and identity independence");
}
