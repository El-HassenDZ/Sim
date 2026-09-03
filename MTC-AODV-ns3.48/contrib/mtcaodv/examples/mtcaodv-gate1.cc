/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Gate 1A executable for validating deterministic attacker selection.
 */

#include "ns3/attack-manager.h"
#include "ns3/command-line.h"
#include "ns3/node-container.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/simulator.h"

#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <set>

using namespace ns3;

/**
 * @brief Print a machine-readable manifest for one attacker selection.
 * @param selection Validated attacker selection.
 * @param seed Global ns-3 seed used for the run.
 * @param run ns-3 run/substream number.
 * @param stream Dedicated attacker-selection stream index.
 * @param excludedNodeIds Node identifiers protected from malicious assignment.
 */
static void
PrintSelectionManifest(const mtcaodv::AttackSelectionResult& selection,
                       uint32_t seed,
                       uint64_t run,
                       int64_t stream,
                       const std::set<uint32_t>& excludedNodeIds)
{
    // JSON is emitted as a single line so the Python validator can distinguish
    // the manifest from optional ns-3 logging written before it.
    std::cout << std::fixed << std::setprecision(8)
              << "{\"schemaVersion\":1"
              << ",\"gate\":\"1A\""
              << ",\"nodeCount\":" << selection.nodeCount
              << ",\"attackerRatio\":" << selection.attackerRatio
              << ",\"attackerCount\":" << selection.attackerCount
              << ",\"seed\":" << seed
              << ",\"run\":" << run
              << ",\"attackerSelectionStream\":" << stream
              << ",\"excludedNodeIds\":[";

    bool isFirstIdentifier = true;

    // Serialize every excluded role exactly once.  The delimiter flag is the
    // loop invariant that keeps the resulting JSON valid without a trailing comma.
    for (const uint32_t nodeId : excludedNodeIds)
    {
        if (!isFirstIdentifier)
        {
            // A comma is required only after at least one prior identifier has
            // already been serialized.
            std::cout << ',';
        }
        std::cout << nodeId;
        isFirstIdentifier = false;
    }

    std::cout << "],\"attackerNodeIds\":[";
    isFirstIdentifier = true;

    // Serialize the sorted malicious-node set.  The stable order makes paired
    // scenario manifests byte-comparable across AODV and MTC-AODV variants.
    for (const uint32_t nodeId : selection.attackerNodeIds)
    {
        if (!isFirstIdentifier)
        {
            // The separator belongs between entries and is intentionally
            // absent before the first or after the final entry.
            std::cout << ',';
        }
        std::cout << nodeId;
        isFirstIdentifier = false;
    }

    std::cout << "]}" << std::endl;
}

/**
 * @brief Run the standalone Gate 1A attacker-selection validation.
 * @param argc Number of command-line arguments.
 * @param argv Command-line argument vector.
 * @return zero for a valid manifest; non-zero for a rejected configuration.
 */
int
main(int argc, char* argv[])
{
    /** Total MANET population. Unit: nodes. */
    uint32_t nodeCount = 100;

    /** Required fraction of malicious nodes. Domain: [0,1]. */
    double attackerRatio = 0.05;

    /** Global seed shared by paired experiment variants. */
    uint32_t seed = 12345;

    /** ns-3 run/substream index used as the replication identifier. */
    uint64_t run = 1;

    /** Dedicated RNG stream reserved only for attacker identities. */
    int64_t attackerSelectionStream = 73001;

    /** Protects the first and last scenario nodes as fixed traffic endpoints. */
    bool excludeTrafficEndpoints = true;

    CommandLine commandLine(__FILE__);
    commandLine.AddValue("nodeCount", "Total number of MANET nodes.", nodeCount);
    commandLine.AddValue("attackerRatio", "Attacker proportion in [0,1].", attackerRatio);
    commandLine.AddValue("seed", "Global ns-3 seed.", seed);
    commandLine.AddValue("run", "ns-3 replication run number.", run);
    commandLine.AddValue("attackStream",
                         "Dedicated RNG stream for attacker selection.",
                         attackerSelectionStream);
    commandLine.AddValue("excludeTrafficEndpoints",
                         "Exclude the first and last nodes from attacker selection.",
                         excludeTrafficEndpoints);
    commandLine.Parse(argc, argv);

    try
    {
        // Seed and run are assigned before any random variable is used, which
        // is necessary for exact experiment replay.
        RngSeedManager::SetSeed(seed);
        RngSeedManager::SetRun(run);

        NodeContainer nodes;
        nodes.Create(nodeCount);

        std::set<uint32_t> excludedNodeIds;

        // Fixed endpoint exclusion is enabled only when two distinct roles can
        // exist.  A one-node configuration remains valid for zero-ratio tests.
        if (excludeTrafficEndpoints && nodeCount >= 2)
        {
            excludedNodeIds.insert(nodes.Get(0)->GetId());
            excludedNodeIds.insert(nodes.Get(nodeCount - 1)->GetId());
        }

        mtcaodv::AttackManager attackManager;
        attackManager.AssignStream(attackerSelectionStream);
        const mtcaodv::AttackSelectionResult selection =
            attackManager.SelectAttackers(nodes, attackerRatio, excludedNodeIds);

        PrintSelectionManifest(selection,
                               seed,
                               run,
                               attackManager.GetAssignedStream(),
                               excludedNodeIds);
        Simulator::Destroy();
        return 0;
    }
    catch (const std::exception& exception)
    {
        // Configuration failures are surfaced explicitly rather than converted
        // into a smaller attacker set or a misleading successful manifest.
        std::cerr << "MTC-AODV Gate 1A configuration error: " << exception.what() << std::endl;
        Simulator::Destroy();
        return 2;
    }
}

