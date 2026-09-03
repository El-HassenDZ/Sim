/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This file is part of the MTC-AODV research prototype.
 */

#ifndef MTC_AODV_ATTACK_MANAGER_H
#define MTC_AODV_ATTACK_MANAGER_H

#include "ns3/node-container.h"
#include "ns3/node.h"
#include "ns3/ptr.h"
#include "ns3/random-variable-stream.h"

#include <cstdint>
#include <set>
#include <vector>

namespace ns3
{
namespace mtcaodv
{

/**
 * @ingroup mtcaodv
 *
 * @brief Immutable description of one deterministic attacker selection.
 *
 * The ratio is always interpreted against the total MANET population, not
 * against the smaller set of eligible nodes.  This convention ensures that
 * 5%, 10%, 20%, and 30% correspond to exactly 5, 10, 20, and 30 attackers for
 * a 100-node experiment, even when traffic endpoints are excluded.
 */
struct AttackSelectionResult
{
    /** @brief Total MANET population, in nodes. */
    uint32_t nodeCount{0};

    /** @brief Requested attacker proportion. Domain: [0, 1]. Unit: dimensionless. */
    double attackerRatio{0.0};

    /** @brief Required number of unique malicious nodes. Unit: nodes. */
    uint32_t attackerCount{0};

    /** @brief Sorted ns-3 node identifiers selected as attackers. */
    std::vector<uint32_t> attackerNodeIds;

    /**
     * @brief Verify the internal invariants of the selection record.
     *
     * @throws std::logic_error if the count, ordering, uniqueness, or ratio is
     *         inconsistent with the record.
     */
    void Validate() const;
};

/**
 * @ingroup mtcaodv
 *
 * @brief Selects attacker nodes without replacement using an assigned ns-3
 *        random-number stream.
 *
 * The manager is part of experiment configuration only.  Its output must
 * never be exposed to the runtime detector, trust model, or PTMB components.
 * This separation prevents an oracle-like detector from learning ground-truth
 * attacker identities.
 */
class AttackManager
{
  public:
    /**
     * @brief Construct a selector with its own UniformRandomVariable.
     *
     * Call AssignStream() before SelectAttackers() so experiment manifests can
     * reproduce the exact node set independently of other random processes.
     */
    AttackManager();

    /** @brief Destroy the selector and release its reference-counted RNG. */
    ~AttackManager();

    /**
     * @brief Compute the exact attacker count from a population and ratio.
     *
     * The implemented equation is
     * @f$N_a = \lfloor r_a N + 0.5 \rfloor@f$,
     * which is nearest-integer rounding for non-negative inputs.
     *
     * @param nodeCount Total MANET population, in nodes.
     * @param attackerRatio Attacker proportion @f$r_a@f$ in [0, 1].
     * @return Required attacker count @f$N_a@f$, in nodes.
     * @throws std::invalid_argument for a non-finite or out-of-range ratio.
     */
    static uint32_t ComputeAttackerCount(uint32_t nodeCount, double attackerRatio);

    /**
     * @brief Bind attacker selection to one deterministic ns-3 RNG stream.
     * @param stream Non-negative stream index reserved in the experiment manifest.
     * @throws std::invalid_argument if stream is negative.
     */
    void AssignStream(int64_t stream);

    /**
     * @brief Return the stream currently reserved for attacker selection.
     * @return Non-negative stream index after AssignStream(), or -1 before it.
     */
    int64_t GetAssignedStream() const;

    /**
     * @brief Select unique malicious nodes from a MANET population.
     *
     * @param nodes All MANET nodes participating in the scenario.
     * @param attackerRatio Proportion applied to the total node count.
     * @param excludedNodeIds Node identifiers that cannot be attackers, such as
     *        fixed source and destination endpoints.
     * @return Validated and sorted attacker selection.
     * @throws std::invalid_argument if configuration is invalid or too few
     *         eligible nodes remain.
     * @throws std::logic_error if AssignStream() was not called first.
     */
    AttackSelectionResult SelectAttackers(const NodeContainer& nodes,
                                          double attackerRatio,
                                          const std::set<uint32_t>& excludedNodeIds);

    /**
     * @brief Split a population into honest and malicious containers.
     *
     * The two containers are what a scenario hands to `InternetStackHelper`:
     * the honest set receives the stock `AodvHelper`, the malicious set
     * receives `BlackholeAodvHelper`. Performing the split here keeps the
     * mapping from a selection record to installed behaviour in one place, so
     * a scenario cannot install an attacker the manifest does not declare.
     *
     * @param nodes All MANET nodes, in the order used for selection.
     * @param selection A validated selection over that same population.
     * @param honestNodes Receives every node absent from the selection.
     * @param attackerNodes Receives every node named by the selection.
     * @throws std::invalid_argument if the selection does not describe this
     *         population, or names an identifier the population lacks.
     */
    static void PartitionByAttackers(const NodeContainer& nodes,
                                     const AttackSelectionResult& selection,
                                     NodeContainer& honestNodes,
                                     NodeContainer& attackerNodes);

  private:
    /**
     * @brief RNG used solely for sampling attacker identities without replacement.
     */
    Ptr<UniformRandomVariable> m_selectionRandomVariable;

    /**
     * @brief Indicates whether a reproducible RNG stream has been explicitly assigned.
     */
    bool m_hasAssignedStream;

    /**
     * @brief Stream index recorded for diagnostics and reproducibility manifests.
     */
    int64_t m_assignedStream;
};

} // namespace mtcaodv
} // namespace ns3

#endif // MTC_AODV_ATTACK_MANAGER_H
