/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef MTC_AODV_BLACKHOLE_BEHAVIOR_H
#define MTC_AODV_BLACKHOLE_BEHAVIOR_H

#include "attack-behavior.h"

#include "ns3/traced-callback.h"

namespace ns3
{
namespace mtcaodv
{

/**
 * \ingroup mtcaodv
 * \brief Politique full Blackhole interne et authentifiée (§8.1, A2.3, A2.4).
 *
 * Après \f$t_{attack}\f$, le nœud répond à tout RREQ par un RREP de format AODV
 * standard portant une destination correcte, un numéro de séquence artificiellement
 * augmenté, un faible nombre de sauts et une durée de validité attractive ; il consomme
 * ensuite silencieusement les données en transit. Le plan de contrôle est préservé par
 * défaut, ce qui maintient l'attaquant joignable et participant (§8.1).
 *
 * Cette classe est une *politique pure* : elle ne touche ni au simulateur, ni aux
 * paquets, ni aux tables de routage. Elle est donc testable sans exécuter ns-3, et le
 * code qui sérialise ou transfère les paquets reste indépendant d'elle (§8.1).
 *
 * Une instance distincte est attachée à chaque attaquant (§8.2, invariant 20.4.3) : les
 * compteurs ci-dessous sont par nœud et ne doivent jamais être partagés.
 */
class BlackholeBehavior : public AttackBehavior
{
  public:
    /// Signature de la trace ForgedReply : numéro de séquence annoncé.
    typedef void (*ForgedReplyTracedCallback)(uint32_t destinationSequenceNumber);
    /// Signature de la trace BlackholeDrop : (uid du paquet, source, destination).
    typedef void (*DropTracedCallback)(uint64_t packetUid,
                                       uint32_t sourceAddress,
                                       uint32_t destinationAddress);

    static TypeId GetTypeId();

    BlackholeBehavior();
    ~BlackholeBehavior() override = default;

    // Interface AttackBehavior.
    bool IsActive(Time now) const override;
    RouteReplyDecision ShouldForgeRouteReply(Time now, bool hasValidReverseRoute) const override;
    ForgedReplyProfile CreateForgedReplyProfile(uint32_t observedDestinationSequence) const override;
    TransitPacketDecision ShouldDropTransitPacket(Time now,
                                                   const TransitPacketContext& context) const override;

    /**
     * \brief Enregistrer l'émission effective d'un RREP forgé.
     *
     * Appelé par le protocole *après* sérialisation réussie, jamais au moment de la
     * décision : le compteur mesure des RREP réellement émis, pas des intentions
     * (§8.1, « observé seulement à l'exécution »).
     */
    void NotifyForgedReplySent(uint32_t destinationSequenceNumber);

    /**
     * \brief Enregistrer l'abandon effectif d'un paquet en transit.
     */
    void NotifyTransitPacketDropped(uint64_t packetUid, uint32_t sourceAddress, uint32_t destinationAddress);

    uint32_t GetForgedReplyCount() const;
    uint32_t GetTransitDropCount() const;

  private:
    // --- Paramètres de l'attaque (§8.1, Annexe C) ---------------------------------
    Time m_attackStartTime;          //!< \f$t_{attack}\f$, début inclusif de l'attaque.
    uint32_t m_sequenceNumberOffset; //!< \f$\Delta_{seq}\f$, Éq. (23).
    uint8_t m_advertisedHopCount;    //!< \f$h_{fake}\f$.
    Time m_forgedRouteLifetime;      //!< \f$T_{fake}\f$.
    bool m_dropTransitData;          //!< Active l'abandon des données en transit.
    bool m_preserveControlPlane;     //!< Exempte le contrôle routage et sécurité.

    // --- Compteurs d'exécution ------------------------------------------------------
    uint32_t m_forgedReplyCount;          //!< RREP forgés réellement sérialisés.
    uint32_t m_blackholeTransitDropCount; //!< Paquets transit consommés.

    /// Trace émise à chaque RREP forgé : (numéro de séquence annoncé).
    TracedCallback<uint32_t> m_forgedReplyTrace;
    /// Trace émise à chaque abandon : (uid, source, destination), cf. A2.4 ligne 6.
    TracedCallback<uint64_t, uint32_t, uint32_t> m_blackholeDropTrace;
};

} // namespace mtcaodv
} // namespace ns3

#endif /* MTC_AODV_BLACKHOLE_BEHAVIOR_H */
