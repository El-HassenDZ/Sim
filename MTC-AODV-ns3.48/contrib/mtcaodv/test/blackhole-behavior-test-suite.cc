/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Unit tests for the Gate 1A full-Blackhole policy.
 */

#include "ns3/blackhole-behavior.h"

#include "ns3/boolean.h"
#include "ns3/nstime.h"
#include "ns3/simulator.h"
#include "ns3/test.h"
#include "ns3/uinteger.h"

#include <cstdint>
#include <limits>

namespace ns3
{
namespace mtcaodv
{

/**
 * @brief Verifies activation, forged RREP fields, and control-plane preservation.
 */
class FullBlackholePolicyTestCase : public TestCase
{
  public:
    /** @brief Construct the Blackhole policy unit test. */
    FullBlackholePolicyTestCase()
        : TestCase("MTC-AODV full-Blackhole policy follows its configured semantics")
    {
    }

  private:
    /** @brief Execute deterministic policy decisions without a radio scenario. */
    void DoRun() override
    {
        Ptr<BlackholeBehavior> behavior = CreateObject<BlackholeBehavior>();
        behavior->SetAttribute("AttackStartTime", TimeValue(Seconds(10)));
        behavior->SetAttribute("SequenceNumberOffset", UintegerValue(200));
        behavior->SetAttribute("AdvertisedHopCount", UintegerValue(1));
        behavior->SetAttribute("ForgedRouteLifetime", TimeValue(Seconds(45)));
        behavior->SetAttribute("DropTransitData", BooleanValue(true));
        behavior->SetAttribute("PreserveControlPlane", BooleanValue(true));

        // Before the configured activation instant, the malicious node must be
        // observationally benign so the experiment has an uncontaminated warm-up.
        NS_TEST_EXPECT_MSG_EQ(behavior->IsActive(Seconds(9.999)),
                              false,
                              "The attack activated before its scheduled time");
        NS_TEST_EXPECT_MSG_EQ(behavior->ShouldForgeRouteReply(Seconds(9), true),
                              false,
                              "The policy forged a reply during the warm-up period");
        PacketDropContext warmUpTransit;
        warmUpTransit.observationTime = Seconds(9);
        warmUpTransit.isTransit = true;
        NS_TEST_EXPECT_MSG_EQ(behavior->ShouldDropPacket(warmUpTransit),
                              false,
                              "The policy dropped data during the warm-up period");

        // At the exact boundary the attack becomes active.  A missing reverse
        // route still prevents an undeliverable forged reply.
        NS_TEST_EXPECT_MSG_EQ(behavior->IsActive(Seconds(10)),
                              true,
                              "The policy was inactive at its activation boundary");
        NS_TEST_EXPECT_MSG_EQ(behavior->ShouldForgeRouteReply(Seconds(10), false),
                              false,
                              "The policy forged a reply without a usable reverse route");
        NS_TEST_EXPECT_MSG_EQ(behavior->ShouldForgeRouteReply(Seconds(10), true),
                              true,
                              "The policy did not forge despite an active reverse route");

        constexpr uint32_t observedSequenceNumber = 500;
        const ForgedReplyProfile profile =
            behavior->CreateForgedReplyProfile(observedSequenceNumber);
        NS_TEST_EXPECT_MSG_EQ(profile.destinationSequenceNumber,
                              700,
                              "The configured sequence-number offset was not applied");
        NS_TEST_EXPECT_MSG_EQ(profile.hopCount,
                              1,
                              "The forged hop count differs from the configured value");
        NS_TEST_EXPECT_MSG_EQ(profile.routeLifetime,
                              Seconds(45),
                              "The forged lifetime differs from the configured value");

        // A boundary example verifies the declared modulo-2^32 equation rather
        // than relying only on a non-overflowing sequence-number input.
        const ForgedReplyProfile wrappedProfile = behavior->CreateForgedReplyProfile(
            std::numeric_limits<uint32_t>::max() - 99);
        NS_TEST_EXPECT_MSG_EQ(wrappedProfile.destinationSequenceNumber,
                              100,
                              "Forged sequence-number addition did not wrap modulo 2^32");

        // Transit data is the attack target, whereas control traffic is kept so
        // the node can continue attracting routes and exchanging evidence.
        PacketDropContext activeTransit;
        activeTransit.observationTime = Seconds(11);
        activeTransit.isTransit = true;
        NS_TEST_EXPECT_MSG_EQ(behavior->ShouldDropPacket(activeTransit),
                              true,
                              "An active full Blackhole forwarded transit data");

        PacketDropContext routingControl = activeTransit;
        routingControl.isRoutingControl = true;
        NS_TEST_EXPECT_MSG_EQ(behavior->ShouldDropPacket(routingControl),
                              false,
                              "The policy dropped protected AODV control traffic");

        PacketDropContext securityControl = activeTransit;
        securityControl.isSecurityControl = true;
        NS_TEST_EXPECT_MSG_EQ(behavior->ShouldDropPacket(securityControl),
                              false,
                              "The policy dropped protected security-control traffic");

        // The transit precondition is a safety property, not a policy option:
        // an attacker that discarded its own traffic would model a broken node
        // and the loss would be misattributed to the attack.
        PacketDropContext locallyOriginated = activeTransit;
        locallyOriginated.isTransit = false;
        NS_TEST_EXPECT_MSG_EQ(behavior->ShouldDropPacket(locallyOriginated),
                              false,
                              "The policy dropped a packet that was not in transit");

        // A default-constructed context must also be refused, so a caller that
        // forgets to state the transit property obtains the benign decision.
        NS_TEST_EXPECT_MSG_EQ(behavior->ShouldDropPacket(PacketDropContext()),
                              false,
                              "A context with no stated transit property caused a drop");
        Simulator::Destroy();
    }
};

/**
 * @brief Registers the Gate 1A Blackhole behavior tests with ns-3.
 */
class BlackholeBehaviorTestSuite : public TestSuite
{
  public:
    /** @brief Construct and register all Blackhole-policy test cases. */
    BlackholeBehaviorTestSuite()
        : TestSuite("mtcaodv-blackhole-behavior", Type::UNIT)
    {
        AddTestCase(new FullBlackholePolicyTestCase, TestCase::Duration::QUICK);
    }
};

/** Static registration object discovered automatically by the ns-3 test runner. */
static BlackholeBehaviorTestSuite g_blackholeBehaviorTestSuite;

} // namespace mtcaodv
} // namespace ns3
