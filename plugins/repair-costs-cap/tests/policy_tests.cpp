#include "policy.hpp"

#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

namespace {
auto Require(bool value, const char* expression, int line) -> bool {
    if (value) return true;
    std::cerr << "line " << line << ": failed: " << expression << '\n';
    return false;
}
}

#define REQUIRE(value) do { if (!Require((value), #value, __LINE__)) return 1; } while (false)

int main(int argc, char** argv) {
    using namespace RuffnecKk::RepairCostsCap;

    REQUIRE(IsValidMaximumGold(0));
    REQUIRE(IsValidMaximumGold(MaximumGoldLimit));
    REQUIRE(!IsValidMaximumGold(-1));
    REQUIRE(!IsValidMaximumGold(MaximumGoldLimit + 1));
    REQUIRE(IsValidChance(0.0));
    REQUIRE(IsValidChance(0.10));
    REQUIRE(IsValidChance(1.0));
    REQUIRE(!IsValidChance(-0.01));
    REQUIRE(!IsValidChance(1.01));
    REQUIRE(ChanceToBasisPoints(0.10) == 1'000);

    RepairPolicy policy{
        .enabled = true,
        .maximumGoldPerItem = 5'000,
        .maximumGoldRepairAll = 12'000,
        .durabilityWearEnabled = true,
        .durabilityWearChance = 0.10,
    };
    REQUIRE(IsValidPolicy(policy));
    REQUIRE(ApplyRepairCostCap(50'000, RepairTransactionType, policy) == 5'000);
    REQUIRE(ApplyRepairCostCap(10'000, 0, policy) == 10'000);
    const auto adjustedRepairAllTotal =
        ApplyRepairCostCap(50'000, RepairTransactionType, policy)
        + ApplyRepairCostCap(3'000, RepairTransactionType, policy);
    REQUIRE(adjustedRepairAllTotal == 8'000);
    REQUIRE(ApplyRepairAllCap(adjustedRepairAllTotal, policy) == 8'000);
    REQUIRE(ApplyRepairAllCap(10'000, policy) == 10'000);
    REQUIRE(ApplyRepairAllCap(30'000, policy) == 12'000);
    policy.maximumGoldPerItem = 0;
    policy.maximumGoldRepairAll = MaximumGoldLimit;
    REQUIRE(ApplyRepairCostCap(50'000, RepairTransactionType, policy) == 0);
    REQUIRE(ApplyRepairCostCap(
        MaximumGoldLimit, RepairTransactionType, policy) == MaximumGoldLimit);
    REQUIRE(ApplyRepairAllCap(50'000, policy) == 50'000);
    policy.maximumGoldPerItem = MaximumGoldLimit;
    policy.maximumGoldRepairAll = 0;
    REQUIRE(ApplyRepairCostCap(50'000, RepairTransactionType, policy) == 50'000);
    REQUIRE(ApplyRepairAllCap(50'000, policy) == 0);
    REQUIRE(ApplyRepairAllCap(MaximumGoldLimit, policy) == MaximumGoldLimit);
    REQUIRE(GoldReduction(50'000, 5'000) == 45'000);
    REQUIRE(DidPhysicalRepairSucceed(10, 20, 20));
    REQUIRE(!DidPhysicalRepairSucceed(10, 20, 19));
    REQUIRE(ShouldLoseMaximumDurability(true, 0.10, 999));
    REQUIRE(!ShouldLoseMaximumDurability(true, 0.10, 1'000));
    REQUIRE(ReducedMaximumDurability(2) == 1);
    REQUIRE(ReducedMaximumDurability(1) == 1);

    REQUIRE(argc == 2);
    std::ifstream file(argv[1], std::ios::binary);
    REQUIRE(file.good());
    std::ostringstream stream;
    stream << file.rdbuf();
    std::string error;
    REQUIRE(ParseConfig(stream.str(), policy, error));
    REQUIRE(policy.pluginEnabled);
    REQUIRE(!policy.enabled);
    REQUIRE(policy.maximumGoldPerItem == std::numeric_limits<std::int32_t>::max());
    REQUIRE(policy.maximumGoldRepairAll == std::numeric_limits<std::int32_t>::max());
    REQUIRE(!policy.durabilityWearEnabled);
    REQUIRE(policy.durabilityWearChance == 0.0);
    REQUIRE(!policy.diagnosticsEnabled);

    const auto configured = R"toml(
[repair_costs]
enabled = true
maximum_gold_per_item = 5000
maximum_gold_repair_all = 12000

[durability_wear]
enabled = true
chance = 0.10
)toml";
    REQUIRE(ParseConfig(configured, policy, error));
    REQUIRE(policy.pluginEnabled);
    REQUIRE(policy.enabled);
    REQUIRE(policy.maximumGoldPerItem == 5'000);
    REQUIRE(policy.maximumGoldRepairAll == 12'000);
    REQUIRE(policy.durabilityWearEnabled);
    REQUIRE(policy.durabilityWearChance == 0.10);
    REQUIRE(!policy.diagnosticsEnabled);

    const auto disabled = R"toml(
[plugin]
enabled = false
[repair_costs]
enabled = true
maximum_gold = 5000
[durability_wear]
enabled = true
chance = 0.10
[diagnostics]
enabled = true
)toml";
    REQUIRE(ParseConfig(disabled, policy, error));
    REQUIRE(!policy.pluginEnabled);
    REQUIRE(policy.enabled);
    REQUIRE(policy.diagnosticsEnabled);
    REQUIRE(policy.maximumGoldPerItem == 5'000);
    REQUIRE(policy.maximumGoldRepairAll == 5'000);

    REQUIRE(!ParseConfig(
        "[repair_costs]\nenabled=true\nmaximum_gold_per_item=-1\n"
        "maximum_gold_repair_all=5000\n"
        "[durability_wear]\nenabled=false\nchance=0.0\n",
        policy,
        error));
    REQUIRE(!ParseConfig(
        "[repair_costs]\nenabled=true\nmaximum_gold_per_item=5000\n"
        "maximum_gold_repair_all=2147483648\n"
        "[durability_wear]\nenabled=true\nchance=0.10\n",
        policy,
        error));
    REQUIRE(!ParseConfig(
        "[repair_costs]\nenabled=true\nmaximum_gold_per_item=5000\n"
        "maximum_gold_repair_all=12000\n"
        "[durability_wear]\nenabled=true\nchance=1.01\n",
        policy,
        error));
    REQUIRE(!ParseConfig(
        "[repair_costs]\nenabled=true\nmaximum_gold_per_item=5000\n"
        "maximum_gold_repair_all=12000\nextra=1\n"
        "[durability_wear]\nenabled=false\nchance=0.0\n",
        policy,
        error));
    REQUIRE(!ParseConfig(
        "[repair_costs]\nenabled=true\nmaximum_gold_per_item=5000\n"
        "[durability_wear]\nenabled=false\nchance=0.0\n",
        policy,
        error));
    REQUIRE(error == "maximum_gold_per_item and maximum_gold_repair_all must be set together");
    REQUIRE(!ParseConfig(
        "[repair_costs]\nenabled=true\nmaximum_gold=5000\n"
        "maximum_gold_per_item=5000\nmaximum_gold_repair_all=12000\n"
        "[durability_wear]\nenabled=false\nchance=0.0\n",
        policy,
        error));
    REQUIRE(error == "maximum_gold cannot be combined with maximum_gold_per_item or maximum_gold_repair_all");
    REQUIRE(!ParseConfig(
        "[repair_costs]\nenabled=true\nmaximum_gold_per_item=5000\n"
        "maximum_gold_per_item=12000\nmaximum_gold_repair_all=12000\n"
        "[durability_wear]\nenabled=false\nchance=0.0\n",
        policy,
        error));
    return 0;
}
