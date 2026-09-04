/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "attack-manager.h"

#include "ns3/log.h"
#include "ns3/node.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("MtcAodvAttackManager");

namespace mtcaodv
{

uint32_t
ComputeAttackerCount(uint32_t nodeCount, double attackerRatio)
{
    // A2.1 ligne 1 : NaN, infini et hors [0,1] sont des configurations invalides. Les
    // rejeter ici évite qu'une campagne entière tourne sur un ratio absurde (D-01).
    if (!std::isfinite(attackerRatio) || attackerRatio < 0.0 || attackerRatio > 1.0)
    {
        std::ostringstream message;
        message << "attackerRatio invalide : " << attackerRatio << " (attendu un réel fini dans [0,1])";
        throw std::invalid_argument(message.str());
    }

    // A2.1 ligne 2, Éq. (2) : arrondi au plus proche, moitié vers le haut. Le calcul est
    // conduit en long double pour que la partie fractionnaire exacte de r_a * N ne soit
    // pas perdue avant l'ajout de 0,5 ; en double, 0.05 * 50 vaut 2.5000000000000004,
    // ce qui donne le même résultat ici mais ne le garantit pas pour tout couple.
    const long double scaled = static_cast<long double>(attackerRatio) * static_cast<long double>(nodeCount);
    const long double rounded = std::floor(scaled + 0.5L);

    if (rounded > static_cast<long double>(nodeCount))
    {
        std::ostringstream message;
        message << "N_A=" << static_cast<uint64_t>(rounded) << " dépasse N=" << nodeCount;
        throw std::overflow_error(message.str());
    }

    return static_cast<uint32_t>(rounded);
}

// --------------------------------------------------------------------------------
// AttackSelectionResult
// --------------------------------------------------------------------------------

AttackSelectionResult::AttackSelectionResult(std::vector<uint32_t> attackerIds,
                                             uint32_t nodeCount,
                                             uint32_t eligibleCount,
                                             double requestedRatio)
    : m_attackerIds(std::move(attackerIds)),
      m_nodeCount(nodeCount),
      m_eligibleCount(eligibleCount),
      m_requestedRatio(requestedRatio)
{
}

const std::vector<uint32_t>&
AttackSelectionResult::GetAttackerIds() const
{
    return m_attackerIds;
}

uint32_t
AttackSelectionResult::GetAttackerCount() const
{
    return static_cast<uint32_t>(m_attackerIds.size());
}

uint32_t
AttackSelectionResult::GetNodeCount() const
{
    return m_nodeCount;
}

uint32_t
AttackSelectionResult::GetEligibleCount() const
{
    return m_eligibleCount;
}

double
AttackSelectionResult::GetRequestedRatio() const
{
    return m_requestedRatio;
}

double
AttackSelectionResult::GetRatioAmongEligible() const
{
    if (m_eligibleCount == 0)
    {
        // Aucun éligible : le ratio n'est pas défini. On ne renvoie pas zéro, qui serait
        // un faux « aucun attaquant » (invariant 20.4.6).
        return std::nan("");
    }
    return static_cast<double>(m_attackerIds.size()) / static_cast<double>(m_eligibleCount);
}

bool
AttackSelectionResult::Contains(uint32_t nodeId) const
{
    // La liste est triée par construction : recherche dichotomique.
    return std::binary_search(m_attackerIds.begin(), m_attackerIds.end(), nodeId);
}

void
AttackSelectionResult::Validate() const
{
    if (m_attackerIds.size() > m_eligibleCount)
    {
        throw std::logic_error("AttackSelectionResult : plus d'attaquants que d'éligibles");
    }
    if (m_eligibleCount > m_nodeCount)
    {
        throw std::logic_error("AttackSelectionResult : plus d'éligibles que de nœuds");
    }
    if (!std::is_sorted(m_attackerIds.begin(), m_attackerIds.end()))
    {
        throw std::logic_error("AttackSelectionResult : identifiants non triés");
    }
    if (std::adjacent_find(m_attackerIds.begin(), m_attackerIds.end()) != m_attackerIds.end())
    {
        throw std::logic_error("AttackSelectionResult : identifiants dupliqués");
    }

    // Cohérence avec l'Éq. (2) : la liste doit contenir exactement N_A(r_a, N) éléments.
    const uint32_t expected = ComputeAttackerCount(m_nodeCount, m_requestedRatio);
    if (m_attackerIds.size() != expected)
    {
        std::ostringstream message;
        message << "AttackSelectionResult : " << m_attackerIds.size() << " attaquants pour un N_A attendu de "
                << expected;
        throw std::logic_error(message.str());
    }
}

// --------------------------------------------------------------------------------
// AttackManager
// --------------------------------------------------------------------------------

AttackManager::AttackManager()
    : m_selectionVariable(CreateObject<UniformRandomVariable>()),
      m_streamAssigned(false)
{
}

int64_t
AttackManager::AssignStream(int64_t stream)
{
    if (stream < 0)
    {
        throw std::invalid_argument("AttackManager::AssignStream : index de flux négatif");
    }
    m_selectionVariable->SetStream(stream);
    m_streamAssigned = true;
    return 1; // un seul flux est consommé : cf. le champ 'span' de la configuration
}

bool
AttackManager::IsStreamAssigned() const
{
    return m_streamAssigned;
}

AttackSelectionResult
AttackManager::SelectAttackers(const NodeContainer& nodes,
                               double attackerRatio,
                               const std::set<uint32_t>& excludedIds)
{
    // Sans flux explicitement assigné, deux variantes appariées pourraient tirer des
    // attaquants différents et le bloc expérimental serait invalide (invariant 20.4.4).
    if (!m_streamAssigned)
    {
        throw std::logic_error("AttackManager : AssignStream() doit être appelé avant SelectAttackers()");
    }

    const uint32_t nodeCount = nodes.GetN();
    if (nodeCount == 0)
    {
        throw std::invalid_argument("AttackManager : conteneur de nœuds vide");
    }

    // A2.1 : le compte est calculé sur la population totale, endpoints exclus compris
    // (§5.2). Le tirage, lui, ne portera que sur les éligibles.
    const uint32_t attackerCount = ComputeAttackerCount(nodeCount, attackerRatio);

    // A2.2 lignes 1-6 : construire l'ensemble admissible en détectant tout identifiant
    // dupliqué. Un doublon signalerait un conteneur mal construit, cas dans lequel la
    // sélection ne serait pas reproductible.
    std::vector<uint32_t> eligible;
    eligible.reserve(nodeCount);
    std::set<uint32_t> observed;

    for (uint32_t index = 0; index < nodeCount; ++index)
    {
        const uint32_t nodeId = nodes.Get(index)->GetId();
        if (!observed.insert(nodeId).second)
        {
            std::ostringstream message;
            message << "AttackManager : identifiant de nœud dupliqué (" << nodeId << ")";
            throw std::invalid_argument(message.str());
        }
        if (excludedIds.find(nodeId) == excludedIds.end())
        {
            eligible.push_back(nodeId);
        }
    }

    // A2.2 ligne 7 / D-02 : refuser plutôt que réduire le ratio en silence.
    if (attackerCount > eligible.size())
    {
        std::ostringstream message;
        message << "AttackManager : N_A=" << attackerCount << " dépasse le nombre de nœuds admissibles ("
                << eligible.size() << ") ; le ratio n'est pas réduit silencieusement";
        throw std::invalid_argument(message.str());
    }

    // A2.2 lignes 8-11 : mélange de Fisher-Yates partiel. Seules les attackerCount
    // premières positions sont mélangées, ce qui suffit pour un tirage sans remise
    // uniforme et coûte O(N_A) au lieu de O(N).
    for (uint32_t selectionIndex = 0; selectionIndex < attackerCount; ++selectionIndex)
    {
        const uint32_t sampledIndex =
            m_selectionVariable->GetInteger(selectionIndex, static_cast<uint32_t>(eligible.size()) - 1);
        std::swap(eligible[selectionIndex], eligible[sampledIndex]);
    }

    // A2.2 ligne 12 : forme canonique triée, pour que deux exécutions appariées
    // produisent des manifests comparables octet à octet.
    std::vector<uint32_t> attackerIds(eligible.begin(), eligible.begin() + attackerCount);
    std::sort(attackerIds.begin(), attackerIds.end());

    AttackSelectionResult result(std::move(attackerIds),
                                 nodeCount,
                                 static_cast<uint32_t>(eligible.size()),
                                 attackerRatio);
    result.Validate(); // A2.2 ligne 13

    NS_LOG_INFO("sélection : N=" << nodeCount << ", éligibles=" << result.GetEligibleCount() << ", r_a="
                                 << attackerRatio << ", N_A=" << result.GetAttackerCount());
    return result;
}

} // namespace mtcaodv
} // namespace ns3
