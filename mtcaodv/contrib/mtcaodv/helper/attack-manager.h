/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef MTC_AODV_ATTACK_MANAGER_H
#define MTC_AODV_ATTACK_MANAGER_H

#include "ns3/node-container.h"
#include "ns3/random-variable-stream.h"

#include <cstdint>
#include <set>
#include <vector>

namespace ns3
{
namespace mtcaodv
{

/**
 * \ingroup mtcaodv
 * \brief Convertit un ratio d'attaquants en nombre entier reproductible (A2.1, Éq. 2).
 *
 * \f$N_A(r_a)=\lfloor r_a N + 0.5 \rfloor\f$ : arrondi au plus proche, moitié vers le
 * haut. Ce n'est pas une troncature — pour \f$N=50\f$ et \f$r_a=0{,}05\f$, le résultat
 * est 3 et non 2.
 *
 * \param nodeCount population totale \f$N\f$, endpoints exclus compris (§5.2)
 * \param attackerRatio ratio demandé \f$r_a \in [0,1]\f$
 * \return le nombre entier d'attaquants \f$N_A\f$
 * \throw std::invalid_argument si le ratio n'est pas fini ou sort de [0,1] (D-01)
 * \throw std::overflow_error si \f$N_A > N\f$
 *
 * Aucune réduction silencieuse n'est pratiquée : une configuration impossible est une
 * erreur, pas un ratio discrètement modifié (§13.3, §21).
 */
uint32_t ComputeAttackerCount(uint32_t nodeCount, double attackerRatio);

/**
 * \ingroup mtcaodv
 * \brief Résultat canonique et auto-validant d'une sélection d'attaquants (A2.2).
 *
 * L'objet est immuable après construction. Il constitue la seule représentation de la
 * vérité terrain \f$\mathcal{A}\f$ et ne doit être consommé que par la couche scénario
 * et par l'évaluation hors ligne (invariant 20.2.8).
 */
class AttackSelectionResult
{
  public:
    AttackSelectionResult() = default;
    AttackSelectionResult(std::vector<uint32_t> attackerIds,
                          uint32_t nodeCount,
                          uint32_t eligibleCount,
                          double requestedRatio);

    const std::vector<uint32_t>& GetAttackerIds() const;
    uint32_t GetAttackerCount() const;
    uint32_t GetNodeCount() const;
    uint32_t GetEligibleCount() const;

    /// Ratio demandé \f$r_a\f$, tel que fourni par la configuration.
    double GetRequestedRatio() const;

    /**
     * \brief Ratio effectif parmi les nœuds *éligibles*.
     *
     * Avec exclusion des endpoints, ce ratio est supérieur au ratio demandé, puisque
     * \f$N_A\f$ est calculé sur \f$N\f$ mais tiré parmi les éligibles (§5.2). Les deux
     * grandeurs sont exportées séparément dans le manifest afin qu'aucun lecteur ne
     * surestime la marge de sûreté des comités (voir DIVERGENCES.md § D-I7).
     */
    double GetRatioAmongEligible() const;

    bool Contains(uint32_t nodeId) const;

    /**
     * \brief Vérifie tous les invariants de A2.2 ligne 13.
     * \throw std::logic_error si le compte, l'ordre, l'unicité ou la population sont
     *        incohérents
     */
    void Validate() const;

  private:
    std::vector<uint32_t> m_attackerIds; //!< Identifiants triés, uniques.
    uint32_t m_nodeCount{0};             //!< \f$N\f$, endpoints exclus compris.
    uint32_t m_eligibleCount{0};         //!< Taille de l'ensemble admissible au tirage.
    double m_requestedRatio{0.0};        //!< \f$r_a\f$ demandé.
};

/**
 * \ingroup mtcaodv
 * \brief Sélection déterministe et sans remise des nœuds Blackhole (A2, A2.2).
 *
 * Le tirage utilise exclusivement un flux ns-3 explicitement assigné : `std::rand()`
 * est proscrit (A7.1, préconditions). Le flux doit être assigné avant tout appel à
 * SelectAttackers(), faute de quoi l'appariement expérimental ne serait pas
 * reproductible (§21, « Flux RNG non assigné → erreur »).
 */
class AttackManager
{
  public:
    AttackManager();

    /**
     * \brief Assigne le flux RNG utilisé pour la sélection.
     * \param stream index de flux, doit être non négatif
     * \return le nombre de flux consommés (toujours 1)
     * \throw std::invalid_argument si l'index est négatif
     */
    int64_t AssignStream(int64_t stream);

    bool IsStreamAssigned() const;

    /**
     * \brief Tire \f$N_A\f$ attaquants sans remise (A2.2).
     * \param nodes conteneur des nœuds du scénario
     * \param attackerRatio ratio demandé
     * \param excludedIds identifiants exclus du tirage (typiquement les endpoints de trafic)
     * \return un AttackSelectionResult validé
     * \throw std::logic_error si le flux n'a pas été assigné
     * \throw std::invalid_argument sur identifiant dupliqué ou ensemble admissible trop
     *        petit (D-02) ; le ratio n'est jamais réduit silencieusement
     */
    AttackSelectionResult SelectAttackers(const NodeContainer& nodes,
                                          double attackerRatio,
                                          const std::set<uint32_t>& excludedIds);

  private:
    Ptr<UniformRandomVariable> m_selectionVariable; //!< Source du tirage, flux assigné.
    bool m_streamAssigned;                          //!< Garde-fou de reproductibilité.
};

} // namespace mtcaodv
} // namespace ns3

#endif /* MTC_AODV_ATTACK_MANAGER_H */
