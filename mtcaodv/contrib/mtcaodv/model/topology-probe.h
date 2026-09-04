/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef MTC_AODV_TOPOLOGY_PROBE_H
#define MTC_AODV_TOPOLOGY_PROBE_H

#include "ns3/node-container.h"
#include "ns3/nstime.h"
#include "ns3/object.h"

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace ns3
{
namespace mtcaodv
{

/**
 * \ingroup mtcaodv
 * \brief Connectivité observée d'un couple source/destination.
 */
struct FlowConnectivity
{
    uint32_t samples{0};          //!< Nombre d'échantillons pris.
    uint32_t connectedSamples{0}; //!< Échantillons où un chemin existait.
    uint64_t hopSum{0};           //!< Somme des longueurs de chemin, pour la moyenne.
    uint32_t maximumHops{0};

    double GetConnectedFraction() const;
    double GetMeanHopCount() const;
};

/**
 * \ingroup mtcaodv
 * \brief Instrument de diagnostic : connectivité géométrique du scénario.
 *
 * ATTENTION — ce composant est un **instrument de simulation**, pas un mécanisme du
 * framework. Il lit les positions de tous les nœuds via le simulateur, ce qu'aucun nœud
 * réel ne pourrait faire. Il n'est jamais consulté par le routage, la détection, la
 * confiance ni la certification : il sert uniquement à répondre hors ligne à la question
 * « une perte de paquets vient-elle du protocole ou de l'absence de chemin ? ».
 *
 * Sans cette distinction, un PDR médiocre serait attribué à AODV alors qu'il peut
 * simplement traduire un réseau partitionné, et le dimensionnement du scénario resterait
 * arbitraire.
 *
 * Le rayon de connectivité doit être celui mesuré par `mtcaodv-range-probe`, pas une
 * valeur supposée.
 */
class TopologyProbe : public Object
{
  public:
    static TypeId GetTypeId();

    TopologyProbe();
    ~TopologyProbe() override;

    /**
     * \brief Arme l'échantillonnage périodique.
     * \param nodes nœuds du scénario
     * \param flows couples (indice source, indice destination) à suivre
     * \param connectivityRadius portée utile mesurée, en mètres
     * \param interval période d'échantillonnage
     * \param start début de l'échantillonnage
     * \param end fin de l'échantillonnage
     */
    void Start(const NodeContainer& nodes,
               const std::vector<std::pair<uint32_t, uint32_t>>& flows,
               double connectivityRadius,
               Time interval,
               Time start,
               Time end);

    const std::vector<FlowConnectivity>& GetFlowConnectivity() const;

    /// Degré moyen du graphe, moyenné sur tous les échantillons.
    double GetMeanDegree() const;
    /// Fraction d'échantillons où le graphe entier est connexe.
    double GetConnectedGraphFraction() const;

  private:
    /// Prend un échantillon et replanifie le suivant.
    void Sample();
    /// Construit les listes d'adjacence à partir des positions courantes.
    std::vector<std::vector<uint32_t>> BuildAdjacency() const;
    /// Longueur du plus court chemin en sauts, ou -1 si aucun chemin n'existe.
    int BreadthFirstHopCount(const std::vector<std::vector<uint32_t>>& adjacency,
                             uint32_t source,
                             uint32_t destination) const;

    NodeContainer m_nodes;
    std::vector<std::pair<uint32_t, uint32_t>> m_flows;
    std::vector<FlowConnectivity> m_flowConnectivity;

    double m_connectivityRadius;
    Time m_interval;
    Time m_end;

    uint64_t m_degreeSum;
    uint32_t m_graphSamples;
    uint32_t m_connectedGraphSamples;
};

} // namespace mtcaodv
} // namespace ns3

#endif /* MTC_AODV_TOPOLOGY_PROBE_H */
