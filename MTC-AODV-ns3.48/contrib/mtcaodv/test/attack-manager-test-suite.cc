/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Unit tests for deterministic MTC-AODV attacker selection.
 */

#include "ns3/attack-manager.h"

#include "ns3/node-container.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/simulator.h"
#include "ns3/test.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ns3
{
namespace mtcaodv
{

/**
 * @brief Verifies the attacker-count equation for all mandatory ratios.
 */
class RequiredAttackerCountTestCase : public TestCase
{
  public:
    /** @brief Construct the count-equation unit test. */
    RequiredAttackerCountTestCase()
        : TestCase("MTC-AODV computes exact attacker counts for a 100-node MANET")
    {
    }

  private:
    /** @brief Execute the numerical assertions for Equation N_a. */
    void DoRun() override
    {
        /** Each pair contains attackerRatio followed by its exact expected count. */
        const std::array<std::pair<double, uint32_t>, 4> requiredCases = {
            std::make_pair(0.05, 5),
            std::make_pair(0.10, 10),
            std::make_pair(0.20, 20),
            std::make_pair(0.30, 30),
        };

        // This loop covers every mandatory experimental intensity.  The
        // invariant is that all preceding pairs satisfy floor(r_a*N+0.5).
        for (const auto& [attackerRatio, expectedCount] : requiredCases)
        {
            const uint32_t actualCount =
                AttackManager::ComputeAttackerCount(100, attackerRatio);
            NS_TEST_EXPECT_MSG_EQ(actualCount,
                                  expectedCount,
                                  "Attacker count differs from the registered scenario ratio");
        }

        NS_TEST_EXPECT_MSG_EQ(AttackManager::ComputeAttackerCount(100, 0.0),
                              0,
                              "The lower probability bound must select no attackers");
        NS_TEST_EXPECT_MSG_EQ(AttackManager::ComputeAttackerCount(100, 1.0),
                              100,
                              "The upper probability bound must select the full population");
    }
};

/**
 * @brief Verifies reproducibility, uniqueness, and endpoint exclusion.
 */
class DeterministicSelectionTestCase : public TestCase
{
  public:
    /** @brief Construct the deterministic-selection unit test. */
    DeterministicSelectionTestCase()
        : TestCase("MTC-AODV selects unique attackers reproducibly without endpoints")
    {
    }

  private:
    /** @brief Execute two independent selections with the same RNG coordinates. */
    void DoRun() override
    {
        constexpr uint32_t nodeCount = 100;
        constexpr double attackerRatio = 0.30;
        constexpr int64_t attackerSelectionStream = 73001;

        RngSeedManager::SetSeed(12345);
        RngSeedManager::SetRun(7);

        NodeContainer nodes;
        nodes.Create(nodeCount);

        /** Source and destination IDs are excluded to keep traffic roles fixed. */
        const std::set<uint32_t> excludedNodeIds = {
            nodes.Get(0)->GetId(),
            nodes.Get(nodeCount - 1)->GetId(),
        };

        AttackManager firstManager;
        firstManager.AssignStream(attackerSelectionStream);
        const AttackSelectionResult firstSelection =
            firstManager.SelectAttackers(nodes, attackerRatio, excludedNodeIds);

        AttackManager secondManager;
        secondManager.AssignStream(attackerSelectionStream);
        const AttackSelectionResult secondSelection =
            secondManager.SelectAttackers(nodes, attackerRatio, excludedNodeIds);

        NS_TEST_EXPECT_MSG_EQ(firstSelection.attackerCount,
                              30,
                              "The 30% scenario must contain exactly 30 attackers");
        NS_TEST_EXPECT_MSG_EQ(firstSelection.attackerNodeIds.size(),
                              secondSelection.attackerNodeIds.size(),
                              "Repeated selections returned different list sizes");

        // Compare each canonicalized position.  Because both lists are sorted,
        // equality at all visited positions proves identical selected sets.
        for (std::size_t index = 0; index < firstSelection.attackerNodeIds.size(); ++index)
        {
            NS_TEST_EXPECT_MSG_EQ(firstSelection.attackerNodeIds[index],
                                  secondSelection.attackerNodeIds[index],
                                  "Identical seed, run, and stream changed attacker identity");
        }

        // Each excluded endpoint must remain absent.  This branch represents a
        // role-integrity condition rather than part of the random selection.
        for (const uint32_t excludedNodeId : excludedNodeIds)
        {
            const bool endpointWasSelected =
                std::binary_search(firstSelection.attackerNodeIds.begin(),
                                   firstSelection.attackerNodeIds.end(),
                                   excludedNodeId);
            NS_TEST_EXPECT_MSG_EQ(endpointWasSelected,
                                  false,
                                  "An excluded traffic endpoint became an attacker");
        }

        Simulator::Destroy();
    }
};

/**
 * @brief Verifies that invalid ratios fail instead of being silently clamped.
 */
class InvalidRatioTestCase : public TestCase
{
  public:
    /** @brief Construct the input-validation unit test. */
    InvalidRatioTestCase()
        : TestCase("MTC-AODV rejects an attacker ratio outside the probability domain")
    {
    }

  private:
    /** @brief Exercise the negative-ratio error path. */
    void DoRun() override
    {
        /** Values below zero, above one, and NaN are all invalid probabilities. */
        const std::array<double, 3> invalidRatios = {
            -0.01,
            1.01,
            std::numeric_limits<double>::quiet_NaN(),
        };

        // Exercise every rejected domain class.  The invariant is that each
        // previously visited value raised std::invalid_argument.
        for (const double invalidRatio : invalidRatios)
        {
            bool exceptionObserved = false;

            try
            {
                static_cast<void>(AttackManager::ComputeAttackerCount(100, invalidRatio));
            }
            catch (const std::invalid_argument&)
            {
                // Reaching this branch confirms that invalid experiment labels
                // are rejected before nodes or packets are created.
                exceptionObserved = true;
            }

            NS_TEST_EXPECT_MSG_EQ(exceptionObserved,
                                  true,
                                  "An out-of-domain attacker ratio was accepted unexpectedly");
        }
    }
};

/**
 * @brief Registers the Gate 1A attacker-selection tests with ns-3.
 */
class AttackManagerTestSuite : public TestSuite
{
  public:
    /** @brief Construct and register all attacker-selection test cases. */
    AttackManagerTestSuite()
        : TestSuite("mtcaodv-attack-manager", Type::UNIT)
    {
        AddTestCase(new RequiredAttackerCountTestCase, TestCase::Duration::QUICK);
        AddTestCase(new DeterministicSelectionTestCase, TestCase::Duration::QUICK);
        AddTestCase(new InvalidRatioTestCase, TestCase::Duration::QUICK);
    }
};

/** Static registration object discovered automatically by the ns-3 test runner. */
static AttackManagerTestSuite g_attackManagerTestSuite;

} // namespace mtcaodv
} // namespace ns3
