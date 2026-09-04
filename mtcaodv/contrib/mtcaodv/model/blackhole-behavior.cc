/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "blackhole-behavior.h"

#include "ns3/boolean.h"
#include "ns3/log.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/uinteger.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("MtcAodvBlackholeBehavior");

namespace mtcaodv
{

NS_OBJECT_ENSURE_REGISTERED(BlackholeBehavior);

TypeId
BlackholeBehavior::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::mtcaodv::BlackholeBehavior")
            .SetParent<AttackBehavior>()
            .SetGroupName("MtcAodv")
            .AddConstructor<BlackholeBehavior>()
            .AddAttribute("AttackStartTime",
                          "Instant à partir duquel l'attaque est active (t_attack, inclusif).",
                          TimeValue(Seconds(50.0)),
                          MakeTimeAccessor(&BlackholeBehavior::m_attackStartTime),
                          MakeTimeChecker())
            .AddAttribute("SequenceNumberOffset",
                          "Surcroît Delta_seq ajouté au numéro de séquence destination (Éq. 23).",
                          UintegerValue(1000),
                          MakeUintegerAccessor(&BlackholeBehavior::m_sequenceNumberOffset),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("AdvertisedHopCount",
                          "Nombre de sauts h_fake annoncé dans le RREP forgé.",
                          UintegerValue(1),
                          MakeUintegerAccessor(&BlackholeBehavior::m_advertisedHopCount),
                          MakeUintegerChecker<uint8_t>())
            .AddAttribute("ForgedRouteLifetime",
                          "Durée de validité T_fake annoncée dans le RREP forgé.",
                          TimeValue(Seconds(60.0)),
                          MakeTimeAccessor(&BlackholeBehavior::m_forgedRouteLifetime),
                          MakeTimeChecker())
            .AddAttribute("DropTransitData",
                          "Abandonner silencieusement les paquets de données en transit.",
                          BooleanValue(true),
                          MakeBooleanAccessor(&BlackholeBehavior::m_dropTransitData),
                          MakeBooleanChecker())
            .AddAttribute("PreserveControlPlane",
                          "Relayer normalement le contrôle de routage et de sécurité.",
                          BooleanValue(true),
                          MakeBooleanAccessor(&BlackholeBehavior::m_preserveControlPlane),
                          MakeBooleanChecker())
            .AddTraceSource("ForgedReply",
                            "Un RREP forgé a été effectivement sérialisé et émis.",
                            MakeTraceSourceAccessor(&BlackholeBehavior::m_forgedReplyTrace),
                            "ns3::mtcaodv::BlackholeBehavior::ForgedReplyTracedCallback")
            .AddTraceSource("BlackholeDrop",
                            "Un paquet de données en transit a été consommé silencieusement.",
                            MakeTraceSourceAccessor(&BlackholeBehavior::m_blackholeDropTrace),
                            "ns3::mtcaodv::BlackholeBehavior::DropTracedCallback");
    return tid;
}

BlackholeBehavior::BlackholeBehavior()
    : m_attackStartTime(Seconds(50.0)),
      m_sequenceNumberOffset(1000),
      m_advertisedHopCount(1),
      m_forgedRouteLifetime(Seconds(60.0)),
      m_dropTransitData(true),
      m_preserveControlPlane(true),
      m_forgedReplyCount(0),
      m_blackholeTransitDropCount(0)
{
    NS_LOG_FUNCTION(this);
}

bool
BlackholeBehavior::IsActive(Time now) const
{
    // Le début est inclusif : à t exactement égal à t_attack, l'attaque est active
    // (§8.1, « t >= t_attack »).
    return now >= m_attackStartTime;
}

RouteReplyDecision
BlackholeBehavior::ShouldForgeRouteReply(Time now, bool hasValidReverseRoute) const
{
    // A2.3 lignes 1-2 : deux conditions nécessaires, dans cet ordre. Sans route inverse,
    // le RREP n'aurait aucun destinataire ; l'attaquant se comporte alors normalement
    // (D-04) plutôt que de se trahir par un envoi impossible.
    if (!IsActive(now) || !hasValidReverseRoute)
    {
        return RouteReplyDecision::CONTINUE_AODV;
    }
    return RouteReplyDecision::FORGE_REPLY;
}

ForgedReplyProfile
BlackholeBehavior::CreateForgedReplyProfile(uint32_t observedDestinationSequence) const
{
    ForgedReplyProfile profile;

    // Éq. (23) : seq_fake = (seq_observed + Delta_seq) mod 2^32. Le repliement du type
    // non signé 32 bits réalise exactement ce modulo ; aucun test de débordement n'est
    // requis, et un débordement n'est pas une erreur mais le comportement spécifié
    // (§21, « wrap-around séquence »).
    profile.destinationSequenceNumber = observedDestinationSequence + m_sequenceNumberOffset;
    profile.advertisedHopCount = m_advertisedHopCount;
    profile.routeLifetime = m_forgedRouteLifetime;

    return profile;
}

TransitPacketDecision
BlackholeBehavior::ShouldDropTransitPacket(Time now, const TransitPacketContext& context) const
{
    // A2.4 ligne 1 : hors période d'attaque ou drop désactivé, comportement normal.
    if (!IsActive(now) || !m_dropTransitData)
    {
        return TransitPacketDecision::FORWARD_NORMALLY;
    }

    // Un paquet destiné localement n'est pas « en transit » : l'attaque ne le concerne
    // pas (A2.4, préconditions).
    if (context.isLocallyDestined)
    {
        return TransitPacketDecision::FORWARD_NORMALLY;
    }

    // A2.4 lignes 2-4 : le plan de contrôle est exempté par défaut, ce qui maintient
    // l'attaquant joignable et lui permet de soutenir l'attaque (§8.1).
    if (m_preserveControlPlane && (context.isRoutingControl || context.isSecurityControl))
    {
        return TransitPacketDecision::FORWARD_NORMALLY;
    }

    return TransitPacketDecision::DROP_SILENTLY;
}

void
BlackholeBehavior::NotifyForgedReplySent(uint32_t destinationSequenceNumber)
{
    ++m_forgedReplyCount;
    m_forgedReplyTrace(destinationSequenceNumber);
    NS_LOG_INFO("RREP forgé émis, seq=" << destinationSequenceNumber
                                        << ", total=" << m_forgedReplyCount);
}

void
BlackholeBehavior::NotifyTransitPacketDropped(uint64_t packetUid,
                                              uint32_t sourceAddress,
                                              uint32_t destinationAddress)
{
    ++m_blackholeTransitDropCount;
    m_blackholeDropTrace(packetUid, sourceAddress, destinationAddress);
    NS_LOG_DEBUG("paquet transit consommé, uid=" << packetUid
                                                 << ", total=" << m_blackholeTransitDropCount);
}

uint32_t
BlackholeBehavior::GetForgedReplyCount() const
{
    return m_forgedReplyCount;
}

uint32_t
BlackholeBehavior::GetTransitDropCount() const
{
    return m_blackholeTransitDropCount;
}

} // namespace mtcaodv
} // namespace ns3
