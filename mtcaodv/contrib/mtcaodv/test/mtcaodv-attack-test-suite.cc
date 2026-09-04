/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Tests de la couche attaque : A2.1, A2.2, A2.3 et A2.4.
 * Couvre la matrice minimale T-01 à T-07 du §18.2 de la spécification.
 */

#include "ns3/attack-manager.h"
#include "ns3/blackhole-behavior.h"
#include "ns3/boolean.h"
#include "ns3/node-container.h"
#include "ns3/nstime.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/test.h"
#include "ns3/uinteger.h"

#include <limits>
#include <set>
#include <stdexcept>

using namespace ns3;
using namespace ns3::mtcaodv;

namespace
{

/// Index de flux de validation, imposé par l'Annexe C de la spécification.
constexpr int64_t ATTACK_SELECTION_STREAM = 73001;
/// Seed et run de validation Gate 1A (Annexe C).
constexpr uint32_t VALIDATION_SEED = 12345;
constexpr uint32_t VALIDATION_RUN = 1;

/// Construit un conteneur de n nœuds nus : la sélection ne dépend d'aucune pile réseau.
NodeContainer
MakeNodes(uint32_t count)
{
    NodeContainer nodes;
    nodes.Create(count);
    return nodes;
}

} // namespace

/**
 * \ingroup mtcaodv
 * T-01 — L'Éq. (2) produit les comptes obligatoires.
 */
class AttackerCountTestCase : public TestCase
{
  public:
    AttackerCountTestCase()
        : TestCase("T-01 : Eq.(2) donne les comptes obligatoires")
    {
    }

  private:
    void DoRun() override
    {
        // §16.1 : pour N = 100, les quatre ratios doivent donner exactement 5/10/20/30.
        NS_TEST_ASSERT_MSG_EQ(ComputeAttackerCount(100, 0.05), 5, "r_a=0,05 sur N=100");
        NS_TEST_ASSERT_MSG_EQ(ComputeAttackerCount(100, 0.10), 10, "r_a=0,10 sur N=100");
        NS_TEST_ASSERT_MSG_EQ(ComputeAttackerCount(100, 0.20), 20, "r_a=0,20 sur N=100");
        NS_TEST_ASSERT_MSG_EQ(ComputeAttackerCount(100, 0.30), 30, "r_a=0,30 sur N=100");

        // Cas de test explicitement exigé par le cahier des charges : 50 nœuds à 20 %.
        NS_TEST_ASSERT_MSG_EQ(ComputeAttackerCount(50, 0.20), 10, "50 nœuds à 20 %");

        // L'Éq. (2) est un arrondi au plus proche, pas une troncature. Ce cas surprend
        // et doit rester couvert : 0,05 * 50 = 2,5 donne 3, non 2.
        NS_TEST_ASSERT_MSG_EQ(ComputeAttackerCount(50, 0.05), 3, "arrondi half-up sur 2,5");
        NS_TEST_ASSERT_MSG_EQ(ComputeAttackerCount(15, 0.10), 2, "arrondi half-up sur 1,5");

        // Bornes.
        NS_TEST_ASSERT_MSG_EQ(ComputeAttackerCount(100, 0.0), 0, "ratio nul");
        NS_TEST_ASSERT_MSG_EQ(ComputeAttackerCount(100, 1.0), 100, "ratio unitaire");
    }
};

/**
 * \ingroup mtcaodv
 * T-04 — Un ratio invalide est rejeté avant toute simulation (D-01).
 */
class InvalidRatioTestCase : public TestCase
{
  public:
    InvalidRatioTestCase()
        : TestCase("T-04 : ratio invalide rejeté")
    {
    }

  private:
    void DoRun() override
    {
        const double invalidRatios[] = {std::numeric_limits<double>::quiet_NaN(),
                                        std::numeric_limits<double>::infinity(),
                                        -std::numeric_limits<double>::infinity(),
                                        -0.01,
                                        1.01};

        for (double ratio : invalidRatios)
        {
            bool threw = false;
            try
            {
                ComputeAttackerCount(100, ratio);
            }
            catch (const std::invalid_argument&)
            {
                threw = true;
            }
            NS_TEST_ASSERT_MSG_EQ(threw, true, "ratio " << ratio << " aurait dû être rejeté");
        }
    }
};

/**
 * \ingroup mtcaodv
 * T-02 — La sélection est reproductible et sans remise.
 */
class SelectionReproducibilityTestCase : public TestCase
{
  public:
    SelectionReproducibilityTestCase()
        : TestCase("T-02 : sélection reproductible et sans remise")
    {
    }

  private:
    /**
     * Renvoie la sélection sous forme d'indices relatifs au premier nœud du conteneur.
     *
     * Les identifiants ns-3 sont globaux et croissent d'une NodeContainer à l'autre dans
     * un même processus ; TestCase::Run() ne détruit pas le simulateur entre les cas.
     * Comparer des identifiants absolus testerait donc l'ordre d'allocation, pas la
     * reproductibilité du tirage. On normalise par l'identifiant de base.
     */
    std::vector<uint32_t> SelectOnce()
    {
        RngSeedManager::SetSeed(VALIDATION_SEED);
        RngSeedManager::SetRun(VALIDATION_RUN);

        NodeContainer nodes = MakeNodes(100);
        const uint32_t baseId = nodes.Get(0)->GetId();

        AttackManager manager;
        manager.AssignStream(ATTACK_SELECTION_STREAM);
        std::vector<uint32_t> selected = manager.SelectAttackers(nodes, 0.20, {}).GetAttackerIds();

        for (uint32_t& nodeId : selected)
        {
            nodeId -= baseId;
        }
        return selected;
    }

    void DoRun() override
    {
        const std::vector<uint32_t> first = SelectOnce();
        const std::vector<uint32_t> second = SelectOnce();

        NS_TEST_ASSERT_MSG_EQ(first.size(), static_cast<size_t>(20), "20 attaquants attendus");
        NS_TEST_ASSERT_MSG_EQ(second.size(), first.size(), "tailles de sélection différentes");
        for (size_t i = 0; i < first.size(); ++i)
        {
            NS_TEST_ASSERT_MSG_EQ(first[i], second[i],
                                  "mêmes seed, run et flux doivent donner la même sélection "
                                  "(divergence à l'indice " << i << ")");
        }

        // Tri et unicité (A2.2 lignes 12-13).
        for (size_t i = 1; i < first.size(); ++i)
        {
            NS_TEST_ASSERT_MSG_LT(first[i - 1], first[i], "identifiants non strictement croissants");
        }
    }
};

/**
 * \ingroup mtcaodv
 * T-03 — Les endpoints exclus ne changent pas N_A (§5.2).
 */
class EndpointExclusionTestCase : public TestCase
{
  public:
    EndpointExclusionTestCase()
        : TestCase("T-03 : endpoints exclus sans changer N_A")
    {
    }

  private:
    void DoRun() override
    {
        RngSeedManager::SetSeed(VALIDATION_SEED);
        RngSeedManager::SetRun(VALIDATION_RUN);

        NodeContainer nodes = MakeNodes(100);
        const std::set<uint32_t> excluded = {nodes.Get(0)->GetId(), nodes.Get(1)->GetId()};

        AttackManager manager;
        manager.AssignStream(ATTACK_SELECTION_STREAM);
        const AttackSelectionResult result = manager.SelectAttackers(nodes, 0.20, excluded);

        // N_A est calculé sur N = 100, pas sur les 98 éligibles.
        NS_TEST_ASSERT_MSG_EQ(result.GetAttackerCount(), 20, "N_A doit rester 20");
        NS_TEST_ASSERT_MSG_EQ(result.GetEligibleCount(), 98, "98 éligibles attendus");

        for (uint32_t excludedId : excluded)
        {
            NS_TEST_ASSERT_MSG_EQ(result.Contains(excludedId), false,
                                  "un endpoint exclu a été sélectionné");
        }

        // Le ratio parmi les éligibles est supérieur au ratio demandé : c'est attendu et
        // exporté séparément (DIVERGENCES.md § D-I7).
        NS_TEST_ASSERT_MSG_GT(result.GetRatioAmongEligible(), result.GetRequestedRatio(),
                              "le ratio parmi éligibles doit dépasser le ratio demandé");
    }
};

/**
 * \ingroup mtcaodv
 * D-02 — Un ensemble admissible trop petit est rejeté, jamais réduit (§21).
 */
class InsufficientEligibleTestCase : public TestCase
{
  public:
    InsufficientEligibleTestCase()
        : TestCase("D-02 : ensemble admissible insuffisant rejeté")
    {
    }

  private:
    void DoRun() override
    {
        NodeContainer nodes = MakeNodes(10);
        std::set<uint32_t> excluded;
        for (uint32_t i = 0; i < 8; ++i)
        {
            excluded.insert(nodes.Get(i)->GetId());
        }

        AttackManager manager;
        manager.AssignStream(ATTACK_SELECTION_STREAM);

        // N_A = round(0,50 * 10) = 5 > 2 éligibles.
        bool threw = false;
        try
        {
            manager.SelectAttackers(nodes, 0.50, excluded);
        }
        catch (const std::invalid_argument&)
        {
            threw = true;
        }
        NS_TEST_ASSERT_MSG_EQ(threw, true, "la configuration impossible aurait dû être rejetée");
    }
};

/**
 * \ingroup mtcaodv
 * §21 — Un flux RNG non assigné est une erreur, pas un défaut silencieux.
 */
class UnassignedStreamTestCase : public TestCase
{
  public:
    UnassignedStreamTestCase()
        : TestCase("§21 : flux RNG non assigné rejeté")
    {
    }

  private:
    void DoRun() override
    {
        NodeContainer nodes = MakeNodes(20);
        AttackManager manager; // AssignStream() volontairement omis

        bool threw = false;
        try
        {
            manager.SelectAttackers(nodes, 0.10, {});
        }
        catch (const std::logic_error&)
        {
            threw = true;
        }
        NS_TEST_ASSERT_MSG_EQ(threw, true, "un flux non assigné aurait dû être rejeté");
    }
};

/**
 * \ingroup mtcaodv
 * T-05 — Aucune action malveillante avant t_attack (D-04, D-06).
 */
class AttackInactiveTestCase : public TestCase
{
  public:
    AttackInactiveTestCase()
        : TestCase("T-05 : attaque inactive avant t_attack")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<BlackholeBehavior> behavior = CreateObject<BlackholeBehavior>();
        behavior->SetAttribute("AttackStartTime", TimeValue(Seconds(50.0)));

        NS_TEST_ASSERT_MSG_EQ(behavior->IsActive(Seconds(49.999)), false, "active trop tôt");
        NS_TEST_ASSERT_MSG_EQ(behavior->IsActive(Seconds(50.0)), true, "le début est inclusif");

        NS_TEST_ASSERT_MSG_EQ(behavior->ShouldForgeRouteReply(Seconds(49.999), true) ==
                                  RouteReplyDecision::CONTINUE_AODV,
                              true,
                              "aucune forge avant t_attack");

        TransitPacketContext dataPacket; // données en transit, aucun drapeau de contrôle
        NS_TEST_ASSERT_MSG_EQ(behavior->ShouldDropTransitPacket(Seconds(49.999), dataPacket) ==
                                  TransitPacketDecision::FORWARD_NORMALLY,
                              true,
                              "aucun drop avant t_attack");
        NS_TEST_ASSERT_MSG_EQ(behavior->ShouldDropTransitPacket(Seconds(50.0), dataPacket) ==
                                  TransitPacketDecision::DROP_SILENTLY,
                              true,
                              "drop attendu dès t_attack");
    }
};

/**
 * \ingroup mtcaodv
 * T-06 — Profil du RREP forgé et repliement sur 2^32 (Éq. 23).
 */
class ForgedReplyProfileTestCase : public TestCase
{
  public:
    ForgedReplyProfileTestCase()
        : TestCase("T-06 : profil RREP forgé et wrap-around 2^32")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<BlackholeBehavior> behavior = CreateObject<BlackholeBehavior>();
        behavior->SetAttribute("SequenceNumberOffset", UintegerValue(1000));
        behavior->SetAttribute("AdvertisedHopCount", UintegerValue(1));
        behavior->SetAttribute("ForgedRouteLifetime", TimeValue(Seconds(30.0)));

        // Cas nominal.
        ForgedReplyProfile profile = behavior->CreateForgedReplyProfile(42);
        NS_TEST_ASSERT_MSG_EQ(profile.destinationSequenceNumber, 1042, "seq_fake = seq + Delta_seq");
        NS_TEST_ASSERT_MSG_EQ(profile.advertisedHopCount, 1, "h_fake");
        NS_TEST_ASSERT_MSG_EQ(profile.routeLifetime, Seconds(30.0), "T_fake");

        // Repliement : la spécification impose le modulo 2^32 et non une saturation.
        const uint32_t nearMaximum = std::numeric_limits<uint32_t>::max() - 100; // 2^32 - 101
        profile = behavior->CreateForgedReplyProfile(nearMaximum);
        // (2^32 - 101 + 1000) mod 2^32 = 899 - 1 + 1 = 899.
        NS_TEST_ASSERT_MSG_EQ(profile.destinationSequenceNumber, 899u,
                              "le repliement modulo 2^32 doit être exact");

        // Le maximum exact plus 1 doit donner 0.
        profile = behavior->CreateForgedReplyProfile(std::numeric_limits<uint32_t>::max());
        NS_TEST_ASSERT_MSG_EQ(profile.destinationSequenceNumber, 999u, "wrap depuis 2^32 - 1");
    }
};

/**
 * \ingroup mtcaodv
 * T-07 — Le plan de contrôle est préservé (D-06).
 */
class ControlPlanePreservationTestCase : public TestCase
{
  public:
    ControlPlanePreservationTestCase()
        : TestCase("T-07 : contrôle préservé")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<BlackholeBehavior> behavior = CreateObject<BlackholeBehavior>();
        behavior->SetAttribute("AttackStartTime", TimeValue(Seconds(0.0)));
        const Time active = Seconds(10.0);

        TransitPacketContext routingControl;
        routingControl.isRoutingControl = true;
        NS_TEST_ASSERT_MSG_EQ(behavior->ShouldDropTransitPacket(active, routingControl) ==
                                  TransitPacketDecision::FORWARD_NORMALLY,
                              true,
                              "le contrôle AODV ne doit pas être abandonné");

        TransitPacketContext securityControl;
        securityControl.isSecurityControl = true;
        NS_TEST_ASSERT_MSG_EQ(behavior->ShouldDropTransitPacket(active, securityControl) ==
                                  TransitPacketDecision::FORWARD_NORMALLY,
                              true,
                              "le contrôle de sécurité ne doit pas être abandonné");

        TransitPacketContext localPacket;
        localPacket.isLocallyDestined = true;
        NS_TEST_ASSERT_MSG_EQ(behavior->ShouldDropTransitPacket(active, localPacket) ==
                                  TransitPacketDecision::FORWARD_NORMALLY,
                              true,
                              "un paquet local n'est pas en transit");

        // Avec PreserveControlPlane=false, le contrôle redevient une cible. Ce mode
        // n'est pas le scénario principal : il sert au stress test secondaire (I-6).
        behavior->SetAttribute("PreserveControlPlane", BooleanValue(false));
        NS_TEST_ASSERT_MSG_EQ(behavior->ShouldDropTransitPacket(active, routingControl) ==
                                  TransitPacketDecision::DROP_SILENTLY,
                              true,
                              "PreserveControlPlane=false doit lever l'exemption");
    }
};

/**
 * \ingroup mtcaodv
 * Invariant 20.4.3 — Chaque attaquant possède une instance de comportement indépendante.
 */
class IndependentBehaviorInstancesTestCase : public TestCase
{
  public:
    IndependentBehaviorInstancesTestCase()
        : TestCase("20.4.3 : instances de comportement indépendantes")
    {
    }

  private:
    void DoRun() override
    {
        Ptr<BlackholeBehavior> first = CreateObject<BlackholeBehavior>();
        Ptr<BlackholeBehavior> second = CreateObject<BlackholeBehavior>();

        first->NotifyForgedReplySent(1001);
        first->NotifyForgedReplySent(1002);
        second->NotifyTransitPacketDropped(7, 1, 2);

        NS_TEST_ASSERT_MSG_EQ(first->GetForgedReplyCount(), 2, "compteur du premier attaquant");
        NS_TEST_ASSERT_MSG_EQ(first->GetTransitDropCount(), 0, "compteurs non partagés");
        NS_TEST_ASSERT_MSG_EQ(second->GetForgedReplyCount(), 0, "compteurs non partagés");
        NS_TEST_ASSERT_MSG_EQ(second->GetTransitDropCount(), 1, "compteur du second attaquant");
    }
};

/**
 * \ingroup mtcaodv
 * Suite de tests de la couche attaque.
 */
class MtcAodvAttackTestSuite : public TestSuite
{
  public:
    MtcAodvAttackTestSuite()
        : TestSuite("mtcaodv-attack", Type::UNIT)
    {
        AddTestCase(new AttackerCountTestCase, TestCase::Duration::QUICK);
        AddTestCase(new InvalidRatioTestCase, TestCase::Duration::QUICK);
        AddTestCase(new SelectionReproducibilityTestCase, TestCase::Duration::QUICK);
        AddTestCase(new EndpointExclusionTestCase, TestCase::Duration::QUICK);
        AddTestCase(new InsufficientEligibleTestCase, TestCase::Duration::QUICK);
        AddTestCase(new UnassignedStreamTestCase, TestCase::Duration::QUICK);
        AddTestCase(new AttackInactiveTestCase, TestCase::Duration::QUICK);
        AddTestCase(new ForgedReplyProfileTestCase, TestCase::Duration::QUICK);
        AddTestCase(new ControlPlanePreservationTestCase, TestCase::Duration::QUICK);
        AddTestCase(new IndependentBehaviorInstancesTestCase, TestCase::Duration::QUICK);
    }
};

static MtcAodvAttackTestSuite g_mtcAodvAttackTestSuite; //!< Instance statique de la suite.
