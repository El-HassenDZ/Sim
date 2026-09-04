/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef MTC_AODV_METRICS_COLLECTOR_H
#define MTC_AODV_METRICS_COLLECTOR_H

#include "network-metrics.h"

#include "ns3/application-container.h"
#include "ns3/energy-source-container.h"
#include "ns3/ipv4.h"
#include "ns3/node-container.h"
#include "ns3/packet.h"
#include "ns3/nstime.h"
#include "ns3/object.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ns3
{
namespace mtcaodv
{

// Le type MetricValue, les compteurs observés, les équations (20), (24)-(28) et la
// règle d'export « NaN » sont définis dans network-metrics.h. Ce collecteur ne
// réimplémente aucune de ces formules : il les alimente en compteurs observés et leur
// délègue le calcul, de sorte qu'il n'existe qu'une seule définition du PDR dans le
// dépôt (voir l'en-tête de network-metrics.h pour la justification).

/**
 * \ingroup mtcaodv
 * \brief Jeu complet de métriques d'une exécution.
 *
 * Chaque champ renvoie à son équation dans la spécification. Les compteurs bruts sont
 * conservés à côté des métriques dérivées : un relecteur doit pouvoir recalculer
 * lui-même toute métrique sans refaire la simulation.
 */
struct MetricsReport
{
    // --- Compteurs observés ---------------------------------------------------------
    uint64_t applicationTxPackets{0};   //!< \f$N_{app}^{tx}\f$, observés à la source.
    uint64_t applicationRxPackets{0};   //!< \f$N_{app}^{rx}\f$, observés au puits.
    uint64_t applicationTxPayloadBytes{0};
    uint64_t applicationRxPayloadBytes{0}; //!< \f$B_{app,payload}^{rx}\f$.
    uint64_t deliveredNetworkBytes{0};     //!< \f$B_{network}^{rx}\f$, périmètre documenté.
    uint64_t aodvControlTransmissions{0};  //!< \f$N_{AODV,hop}^{control}\f$, hop par hop.
    uint64_t routeDiscoveries{0};          //!< \f$N_{routeDiscovery}\f$, RREQ originés.
    uint64_t forgedReplies{0};             //!< RREP forgés réellement émis (A2.3).
    uint64_t blackholeTransitDrops{0};     //!< Paquets consommés par les attaquants (A2.4).

    // --- Métriques dérivées ---------------------------------------------------------
    MetricValue packetDeliveryRatio;   //!< Éq. (20).
    MetricValue packetLossRatio;       //!< Éq. (24).
    MetricValue throughputBitsPerSecond; //!< Éq. (25).
    MetricValue goodputBitsPerSecond;    //!< Éq. (25).
    MetricValue meanEndToEndDelay;     //!< Éq. (26), en secondes.
    MetricValue medianEndToEndDelay;   //!< Quantile empirique, §17.1.
    MetricValue jitter;                //!< Éq. (27), en secondes.
    MetricValue normalizedRoutingOverhead; //!< Éq. (28).
    MetricValue routeDiscoveryFrequency;   //!< Éq. (28), en s^-1.
    MetricValue routeUnavailabilityDuration; //!< Éq. (29) — non instrumentée à ce stade.
    MetricValue totalEnergyJoules;     //!< Éq. (30), première égalité.

    double evaluationWindowSeconds{0.0};
};

/**
 * \ingroup mtcaodv
 * \brief Compteurs d'un flux applicatif.
 *
 * Le PDR agrégé masque la différence entre « toutes les routes perdent un peu » et
 * « un flux est entièrement mort ». Les deux situations appellent des diagnostics
 * opposés, d'où le détail par flux.
 */
struct FlowCounters
{
    uint64_t txPackets{0};
    uint64_t rxPackets{0};
};

/**
 * \ingroup mtcaodv
 * \brief Collecte et export des métriques d'une exécution (§17, A7.2).
 *
 * Le collecteur est la seule classe autorisée à connaître la vérité terrain des
 * attaquants, et uniquement pour l'évaluation hors ligne (invariant 20.2.8). Aucun
 * composant de détection, de confiance ou de certification ne doit y accéder.
 *
 * Toutes les grandeurs sont accumulées par événement pendant la fenêtre d'évaluation.
 * Aucune n'est déduite de la configuration : un paquet configuré mais jamais remis à la
 * socket n'est pas compté comme émis (invariant 20.4.5).
 */
class MetricsCollector : public Object
{
  public:
    static TypeId GetTypeId();

    MetricsCollector();
    ~MetricsCollector() override;

    /**
     * \brief Définit la fenêtre d'évaluation \f$[start, end]\f$.
     *
     * Le warm-up est exclu afin que la convergence initiale du routage ne soit pas
     * comptée comme une perte. Les événements hors fenêtre sont ignorés, pas pondérés.
     */
    void SetEvaluationWindow(Time start, Time end);

    /// Connecte les traces Tx de toutes les sources CBR.
    void ConnectTrafficSources(const ApplicationContainer& sources);
    /// Connecte les traces Rx de tous les puits CBR.
    void ConnectTrafficSinks(const ApplicationContainer& sinks);
    /// Connecte le comptage de contrôle AODV sur la couche IPv4 de chaque nœud.
    void ConnectRoutingOverhead(const NodeContainer& nodes);
    /// Enregistre les sources d'énergie pour l'Éq. (30).
    void ConnectEnergySources(const energy::EnergySourceContainer& sources);

    /// Ajoute les compteurs d'un attaquant (lecture hors ligne uniquement).
    void AddAttackCounters(uint64_t forgedReplies, uint64_t transitDrops);

    /// Calcule le rapport final. Ne modifie aucun compteur.
    MetricsReport ComputeReport() const;

    /// Compteurs par flux, indexés par identifiant de flux.
    const std::map<uint16_t, FlowCounters>& GetFlowCounters() const;

    /**
     * \brief Écrit le rapport au format CSV à une seule ligne de données.
     * \param path chemin du fichier
     * \param extraColumns colonnes de contexte (variante, seed, ratio…) écrites en tête
     */
    void ExportCsv(const std::string& path,
                   const std::map<std::string, std::string>& extraColumns) const;

  private:
    // Callbacks de trace.
    void RecordApplicationTx(uint16_t flowId, uint32_t sequenceNumber, uint32_t payloadBytes);
    void RecordApplicationRx(uint16_t flowId, uint32_t sequenceNumber, Time delay, uint32_t payloadBytes);
    void RecordIpv4Transmission(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface);

    bool InsideWindow(Time instant) const;

    Time m_windowStart;
    Time m_windowEnd;

    uint64_t m_applicationTxPackets;
    uint64_t m_applicationRxPackets;
    uint64_t m_applicationTxPayloadBytes;
    uint64_t m_applicationRxPayloadBytes;
    uint64_t m_aodvControlTransmissions;
    uint64_t m_routeDiscoveries;
    uint64_t m_forgedReplies;
    uint64_t m_blackholeTransitDrops;

    /// Délais des paquets livrés, dans l'ordre de réception (nécessaire à l'Éq. 27).
    std::vector<double> m_delaysSeconds;

    /// Détail par flux, pour distinguer perte diffuse et flux entièrement coupé.
    std::map<uint16_t, FlowCounters> m_flowCounters;

    energy::EnergySourceContainer m_energySources;
    bool m_energyConnected;
};

} // namespace mtcaodv
} // namespace ns3

#endif /* MTC_AODV_METRICS_COLLECTOR_H */
