/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef MTC_AODV_CBR_TRAFFIC_APPLICATIONS_H
#define MTC_AODV_CBR_TRAFFIC_APPLICATIONS_H

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ipv4-address.h"
#include "ns3/nstime.h"
#include "ns3/random-variable-stream.h"
#include "ns3/socket.h"
#include "ns3/traced-callback.h"

#include <cstdint>

namespace ns3
{
namespace mtcaodv
{

/**
 * \ingroup mtcaodv
 * \brief Source CBR UDP instrumentée pour la mesure (§16.1, §17.1).
 *
 * ns-3 fournit OnOffApplication et UdpClient, mais aucune des deux ne porte à la fois un
 * identifiant de flux, un numéro de séquence et un horodatage d'émission dans un format
 * que la couche de mesure contrôle. Comme les métriques doivent être calculées sur des
 * compteurs observés et non sur la charge configurée (Éq. 20), la source est écrite ici :
 * le nombre de paquets réellement remis à la socket est compté à la source même, et non
 * déduit du produit débit × durée.
 *
 * Le premier envoi est décalé d'une gigue aléatoire bornée afin que les flux ne se
 * synchronisent pas artificiellement sur la même milliseconde, ce qui provoquerait des
 * collisions systématiques sans rapport avec le protocole évalué.
 */
class CbrTrafficSource : public Application
{
  public:
    /// Signature de la trace Tx : (flux, séquence, octets de charge utile).
    typedef void (*TxTracedCallback)(uint16_t flowId, uint32_t sequenceNumber, uint32_t payloadBytes);

    static TypeId GetTypeId();

    CbrTrafficSource();
    ~CbrTrafficSource() override;

    /// Nombre de paquets applicatifs effectivement remis à la socket : \f$N_{app}^{tx}\f$.
    uint32_t GetTransmittedPacketCount() const;
    /// Octets de charge utile effectivement émis.
    uint64_t GetTransmittedPayloadBytes() const;

    int64_t AssignStreams(int64_t stream);

  private:
    void StartApplication() override;
    void StopApplication() override;

    /// Émet un paquet et replanifie l'émission suivante.
    void SendPacket();

    Ipv4Address m_destinationAddress; //!< Destination du flux.
    uint16_t m_destinationPort;       //!< Port UDP applicatif.
    uint16_t m_flowId;                //!< Identifiant du flux, porté dans l'en-tête.
    uint32_t m_packetSize;            //!< Taille totale de la charge utile, en octets.
    double m_packetRate;              //!< Débit en paquets par seconde.
    Time m_startJitter;               //!< Borne supérieure de la gigue de démarrage.

    Ptr<Socket> m_socket;
    EventId m_sendEvent;
    uint32_t m_sequenceNumber;          //!< Prochain numéro de séquence à émettre.
    uint32_t m_transmittedPacketCount;  //!< Compteur observé, jamais déduit.
    uint64_t m_transmittedPayloadBytes; //!< Octets utiles observés.
    Ptr<UniformRandomVariable> m_jitterVariable;

    /// Trace : (flux, séquence, taille de charge utile).
    TracedCallback<uint16_t, uint32_t, uint32_t> m_txTrace;
};

/**
 * \ingroup mtcaodv
 * \brief Puits CBR UDP instrumenté.
 *
 * Reconstruit par paquet reçu le délai de bout en bout \f$t_p^{rx}-t_p^{tx}\f$ (Éq. 26)
 * et publie une trace exploitée par MetricsCollector. Les paquets dupliqués ou
 * réordonnés ne sont pas filtrés ici : ils sont comptés tels qu'observés, et le
 * traitement statistique se fait hors ligne.
 */
class CbrTrafficSink : public Application
{
  public:
    /// Signature de la trace Rx : (flux, séquence, délai de bout en bout, octets utiles).
    typedef void (*RxTracedCallback)(uint16_t flowId,
                                     uint32_t sequenceNumber,
                                     Time endToEndDelay,
                                     uint32_t payloadBytes);

    static TypeId GetTypeId();

    CbrTrafficSink();
    ~CbrTrafficSink() override;

    /// Nombre de paquets applicatifs effectivement reçus : \f$N_{app}^{rx}\f$.
    uint32_t GetReceivedPacketCount() const;
    /// Octets de charge utile applicative livrés : \f$B_{app,payload}^{rx}\f$.
    uint64_t GetReceivedPayloadBytes() const;

  private:
    void StartApplication() override;
    void StopApplication() override;

    /// Callback de réception : extrait l'en-tête de mesure et publie le délai.
    void HandleRead(Ptr<Socket> socket);

    uint16_t m_listenPort;
    Ptr<Socket> m_socket;
    uint32_t m_receivedPacketCount;
    uint64_t m_receivedPayloadBytes;

    /// Trace : (flux, séquence, délai de bout en bout, octets de charge utile).
    TracedCallback<uint16_t, uint32_t, Time, uint32_t> m_rxTrace;
};

} // namespace mtcaodv
} // namespace ns3

#endif /* MTC_AODV_CBR_TRAFFIC_APPLICATIONS_H */
