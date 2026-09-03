/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This file is part of the MTC-AODV research prototype.
 */

#include "attack-manager.h"

#include "ns3/object.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ns3
{
namespace mtcaodv
{

void
AttackSelectionResult::Validate() const
{
    // A record outside the probability domain cannot describe a scientifically
    // meaningful attacker fraction and must not reach a result manifest.
    if (!std::isfinite(attackerRatio) || attackerRatio < 0.0 || attackerRatio > 1.0)
    {
        throw std::logic_error("AttackSelectionResult contains an invalid attackerRatio");
    }

    // The declared count and stored identifiers must match exactly; otherwise
    // downstream scripts could report a different attack intensity from the
    // one actually simulated.
    if (attackerNodeIds.size() != attackerCount)
    {
        throw std::logic_error("AttackSelectionResult count does not match its node-ID list");
    }

    // A selected set cannot exceed the total population represented by the
    // record, independently of any endpoint exclusions.
    if (attackerCount > nodeCount)
    {
        throw std::logic_error("AttackSelectionResult contains more attackers than nodes");
    }

    // Recomputing the declared equation prevents a record with a plausible list
    // size but an incorrect ratio label from passing manifest validation.
    const uint32_t expectedAttackerCount =
        AttackManager::ComputeAttackerCount(nodeCount, attackerRatio);
    if (attackerCount != expectedAttackerCount)
    {
        throw std::logic_error("AttackSelectionResult count differs from its ratio equation");
    }

    // Sorted output produces stable manifests and makes duplicate detection
    // linear.  At each iteration all previously visited identifiers are known
    // to be strictly increasing.
    for (std::size_t index = 1; index < attackerNodeIds.size(); ++index)
    {
        if (attackerNodeIds[index - 1] >= attackerNodeIds[index])
        {
            throw std::logic_error("Attacker identifiers must be sorted and unique");
        }
    }
}

AttackManager::AttackManager()
    : m_selectionRandomVariable(CreateObject<UniformRandomVariable>()),
      m_hasAssignedStream(false),
      m_assignedStream(-1)
{
}

AttackManager::~AttackManager() = default;

uint32_t
AttackManager::ComputeAttackerCount(uint32_t nodeCount, double attackerRatio)
{
    // Non-finite values and probabilities outside [0,1] would invalidate both
    // the experiment label and the count equation.
    if (!std::isfinite(attackerRatio) || attackerRatio < 0.0 || attackerRatio > 1.0)
    {
        throw std::invalid_argument("attackerRatio must be finite and within [0, 1]");
    }

    // Mathematical mapping: N_a = floor(r_a * N + 0.5).  The intermediate is
    // computed in double precision, which exactly represents all practical
    // ns-3 node counts and the four required decimal ratios.
    const double roundedCount = std::floor(attackerRatio * static_cast<double>(nodeCount) + 0.5);

    // The probability-domain check guarantees this upper bound theoretically;
    // retaining the guard makes numerical assumptions explicit.
    if (roundedCount > static_cast<double>(nodeCount))
    {
        throw std::overflow_error("computed attacker count exceeds the node population");
    }

    return static_cast<uint32_t>(roundedCount);
}

void
AttackManager::AssignStream(int64_t stream)
{
    // ns-3 reserves non-negative indices for deterministic stream assignment.
    // Rejecting an implicit or negative stream prevents accidental coupling to
    // the order in which unrelated random variables are created.
    if (stream < 0)
    {
        throw std::invalid_argument("attacker-selection stream must be non-negative");
    }

    m_selectionRandomVariable->SetStream(stream);
    m_hasAssignedStream = true;
    m_assignedStream = stream;
}

int64_t
AttackManager::GetAssignedStream() const
{
    return m_assignedStream;
}

AttackSelectionResult
AttackManager::SelectAttackers(const NodeContainer& nodes,
                               double attackerRatio,
                               const std::set<uint32_t>& excludedNodeIds)
{
    // Explicit stream assignment is a reproducibility invariant.  Letting ns-3
    // assign a stream implicitly could change attacker identities when another
    // component adds or removes a random variable.
    if (!m_hasAssignedStream)
    {
        throw std::logic_error("AssignStream() must be called before SelectAttackers()");
    }

    const uint32_t nodeCount = nodes.GetN();

    // A MANET scenario with no nodes is a configuration error rather than a
    // valid zero-attacker experiment.
    if (nodeCount == 0)
    {
        throw std::invalid_argument("attacker selection requires at least one node");
    }

    const uint32_t attackerCount = ComputeAttackerCount(nodeCount, attackerRatio);
    std::vector<uint32_t> eligibleNodeIds;
    eligibleNodeIds.reserve(nodeCount);
    std::set<uint32_t> observedNodeIds;

    // Build the sampling population from actual ns-3 identifiers.  The loop
    // visits every scenario node once; its invariant is that each appended ID
    // is eligible and appears only once because NodeContainer holds nodes, not
    // identifier aliases.  Complexity is O(N log E) due to set lookup.
    for (auto nodeIterator = nodes.Begin(); nodeIterator != nodes.End(); ++nodeIterator)
    {
        const uint32_t nodeId = (*nodeIterator)->GetId();

        // Repeated Node pointers would cause a nominal population N to contain
        // fewer than N identities.  Rejecting that container preserves both the
        // count equation and sampling-without-replacement semantics.
        const bool insertedNewIdentifier = observedNodeIds.insert(nodeId).second;
        if (!insertedNewIdentifier)
        {
            throw std::invalid_argument("NodeContainer contains a duplicate node identifier");
        }

        // Endpoints or other protected roles are omitted from the eligible
        // population, but they remain part of N in the attacker-count formula.
        if (excludedNodeIds.find(nodeId) != excludedNodeIds.end())
        {
            continue;
        }

        eligibleNodeIds.push_back(nodeId);
    }

    // Failing here is safer than silently lowering the attack ratio, which
    // would mislabel the scenario and compromise comparisons across variants.
    if (attackerCount > eligibleNodeIds.size())
    {
        throw std::invalid_argument("too few eligible nodes for the requested attacker ratio");
    }

    // The partial Fisher-Yates shuffle samples without replacement.  Before
    // iteration i, positions [0,i) contain final unique selections and [i,E)
    // contain exactly the remaining eligible IDs.  Only N_a iterations are
    // required, giving O(N_a) sampling time after the O(N) population build.
    for (uint32_t selectionIndex = 0; selectionIndex < attackerCount; ++selectionIndex)
    {
        const uint32_t lastEligibleIndex =
            static_cast<uint32_t>(eligibleNodeIds.size() - 1);
        const uint32_t sampledIndex =
            m_selectionRandomVariable->GetInteger(selectionIndex, lastEligibleIndex);
        std::swap(eligibleNodeIds[selectionIndex], eligibleNodeIds[sampledIndex]);
    }

    AttackSelectionResult result;
    result.nodeCount = nodeCount;
    result.attackerRatio = attackerRatio;
    result.attackerCount = attackerCount;
    result.attackerNodeIds.assign(eligibleNodeIds.begin(),
                                  eligibleNodeIds.begin() + attackerCount);

    // Sorting is not part of the random selection; it canonicalizes output so
    // manifests, tests, and paired experiment checks can compare lists exactly.
    std::sort(result.attackerNodeIds.begin(), result.attackerNodeIds.end());
    result.Validate();
    return result;
}

void
AttackManager::PartitionByAttackers(const NodeContainer& nodes,
                                    const AttackSelectionResult& selection,
                                    NodeContainer& honestNodes,
                                    NodeContainer& attackerNodes)
{
    // Revalidating is cheap and prevents a hand-edited or partially copied
    // record from silently producing a different attack intensity than the
    // one the manifest reports.
    selection.Validate();

    if (nodes.GetN() != selection.nodeCount)
    {
        throw std::invalid_argument("selection does not describe this node population");
    }

    const std::set<uint32_t> attackerIds(selection.attackerNodeIds.begin(),
                                         selection.attackerNodeIds.end());
    std::size_t matchedAttackers = 0;

    // One pass over the population. The invariant is that every visited node
    // has been placed in exactly one of the two containers, so the two sizes
    // always sum to the number of nodes visited so far.
    for (auto nodeIterator = nodes.Begin(); nodeIterator != nodes.End(); ++nodeIterator)
    {
        Ptr<Node> node = *nodeIterator;

        if (attackerIds.find(node->GetId()) != attackerIds.end())
        {
            attackerNodes.Add(node);
            ++matchedAttackers;
        }
        else
        {
            honestNodes.Add(node);
        }
    }

    // A selection naming an identifier this population does not contain would
    // otherwise yield a silently smaller attacker set.
    if (matchedAttackers != selection.attackerNodeIds.size())
    {
        throw std::invalid_argument("selection names node identifiers absent from the population");
    }
}

} // namespace mtcaodv
} // namespace ns3
