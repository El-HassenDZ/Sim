/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This file is part of the MTC-AODV research prototype.
 */

#include "blackhole-behavior.h"

#include "ns3/boolean.h"
#include "ns3/log.h"
#include "ns3/uinteger.h"

namespace ns3
{
namespace mtcaodv
{

NS_LOG_COMPONENT_DEFINE("MtcAodvBlackholeBehavior");
NS_OBJECT_ENSURE_REGISTERED(BlackholeBehavior);

TypeId
BlackholeBehavior::GetTypeId()
{
    static TypeId typeId =
        TypeId("ns3::mtcaodv::BlackholeBehavior")
            .SetParent<AttackBehavior>()
            .SetGroupName("MtcAodv")
            .AddConstructor<BlackholeBehavior>()
            .AddAttribute("AttackStartTime",
                          "Simulation time at which malicious actions become active.",
                          TimeValue(Seconds(50)),
                          MakeTimeAccessor(&BlackholeBehavior::m_attackStartTime),
                          MakeTimeChecker(Seconds(0)))
            .AddAttribute("SequenceNumberOffset",
                          "Positive value added to the destination sequence number observed "
                          "in a route request.",
                          UintegerValue(1000),
                          MakeUintegerAccessor(&BlackholeBehavior::m_sequenceNumberOffset),
                          MakeUintegerChecker<uint32_t>(1))
            .AddAttribute("AdvertisedHopCount",
                          "Hop count inserted into a forged route reply.",
                          UintegerValue(1),
                          MakeUintegerAccessor(&BlackholeBehavior::m_advertisedHopCount),
                          MakeUintegerChecker<uint8_t>())
            .AddAttribute("ForgedRouteLifetime",
                          "Validity period claimed by the forged route reply.",
                          TimeValue(Seconds(60)),
                          MakeTimeAccessor(&BlackholeBehavior::m_forgedRouteLifetime),
                          MakeTimeChecker(Seconds(0)))
            .AddAttribute("DropTransitData",
                          "Whether active attackers discard transit data packets.",
                          BooleanValue(true),
                          MakeBooleanAccessor(&BlackholeBehavior::m_dropTransitData),
                          MakeBooleanChecker())
            .AddAttribute("PreserveControlPlane",
                          "Whether routing and security control packets are exempt from drops.",
                          BooleanValue(true),
                          MakeBooleanAccessor(&BlackholeBehavior::m_preserveControlPlane),
                          MakeBooleanChecker());
    return typeId;
}

BlackholeBehavior::BlackholeBehavior()
    : m_attackStartTime(Seconds(50)),
      m_sequenceNumberOffset(1000),
      m_advertisedHopCount(1),
      m_forgedRouteLifetime(Seconds(60)),
      m_dropTransitData(true),
      m_preserveControlPlane(true)
{
    NS_LOG_FUNCTION(this);
}

BlackholeBehavior::~BlackholeBehavior()
{
    NS_LOG_FUNCTION(this);
}

std::string
BlackholeBehavior::GetBehaviorName() const
{
    return "full-blackhole";
}

bool
BlackholeBehavior::IsActive(Time now) const
{
    // The inclusive comparison makes the activation instant reproducible at
    // an exact scheduled event boundary.
    return now >= m_attackStartTime;
}

bool
BlackholeBehavior::ShouldForgeRouteReply(Time now, bool hasUsableReverseRoute) const
{
    // A forged RREP is useful only after activation and only when AODV has a
    // reverse path over which the malicious reply can reach the RREQ source.
    if (!IsActive(now))
    {
        return false;
    }

    // Refusing to forge without a reverse route avoids creating attack events
    // that the network could never deliver and therefore never observe.
    if (!hasUsableReverseRoute)
    {
        return false;
    }

    return true;
}

ForgedReplyProfile
BlackholeBehavior::CreateForgedReplyProfile(uint32_t observedDestinationSequenceNumber) const
{
    ForgedReplyProfile profile;

    // Mathematical mapping:
    //   seq_fake = seq_observed + Delta_seq  (mod 2^32).
    // Unsigned wrap-around is intentional because AODV sequence numbers occupy
    // a 32-bit serial-number space.  The routing adapter remains responsible
    // for applying the exact ns-3 AODV freshness comparison.
    profile.destinationSequenceNumber = observedDestinationSequenceNumber + m_sequenceNumberOffset;
    profile.hopCount = m_advertisedHopCount;
    profile.routeLifetime = m_forgedRouteLifetime;
    return profile;
}

bool
BlackholeBehavior::ShouldDropTransitPacket(Time now,
                                           bool isRoutingControl,
                                           bool isSecurityControl) const
{
    // Before activation the configured node behaves honestly, preserving a
    // clean warm-up interval for routing-table formation.
    if (!IsActive(now) || !m_dropTransitData)
    {
        return false;
    }

    // Control-plane preservation lets a Blackhole advertise and maintain
    // routes.  Dropping these packets would model a disconnected node rather
    // than an adversary that attracts and then discards transit data.
    if (m_preserveControlPlane && (isRoutingControl || isSecurityControl))
    {
        return false;
    }

    return true;
}

} // namespace mtcaodv
} // namespace ns3

