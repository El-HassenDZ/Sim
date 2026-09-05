/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file
 * \ingroup mtcaodv
 * \brief Test d'intégration niveau 3 de l'étape 1 : chaîne causale du full Blackhole.
 *
 * Ce test exécute réellement ns-3 sur une topologie déterministe minimale, et vérifie
 * que le câblage de la politique Blackhole dans le fork produit l'effet attendu de bout
 * en bout. Il correspond au test T-08 de la matrice du §18.2 (« chaîne causale »).
 *
 * **Topologie (fixture causale, pas un résultat de mobilité — §16.3).**
 *
 * \verbatim
 *     S(0,0) ---- 60 m ---- R(60,0) ---- 60 m ---- D(120,0)
 *        \___________________ 120 m ___________________/  (hors portée : 120 > 70)
 * \endverbatim
 *
 * Portée radio fixée à 70 m par `RangePropagationLossModel` : S et D ne s'entendent pas
 * directement, donc **tout** le trafic S→D doit transiter par R. R est le point de
 * passage unique ; en faire un Blackhole coupe le seul chemin. C'est ce qui rend la
 * relation de cause à effet non ambiguë : contrairement au scénario pilote mobile, il
 * n'existe ici aucun chemin alternatif dont la présence ou l'absence brouillerait
 * l'interprétation.
 *
 * **Trois exécutions comparées, à graine et topologie identiques :**
 *
 * | Cas | R porte une politique ? | Attendu |
 * |---|---|---|
 * | témoin sain | non | R relaie ; D reçoit la quasi-totalité des paquets |
 * | Blackhole   | oui, actif à t=0 | R forge ≥1 RREP et abandonne ≥1 paquet ; D reçoit strictement moins |
 * | attaque tardive | oui, active après la fin du trafic | aucun RREP forgé, aucun abandon ; livraison ≈ témoin |
 *
 * Le troisième cas isole l'effet du *déclenchement temporel* (D-04, T-05) : la seule
 * présence d'une politique ne doit rien changer tant que \f$t < t_{attack}\f$.
 *
 * **Ce que le test ne fait pas.** Il ne mesure pas un PDR de référence ni une
 * performance : les compteurs bruts et leurs inégalités sont les seuls oracles, et ils
 * sont exacts (pas de tolérance statistique). Les valeurs absolues dépendent de la
 * pile ns-3 et ne sont pas des résultats scientifiques.
 */

#include "ns3/aodv-module.h"
#include "ns3/blackhole-behavior.h"
#include "ns3/cbr-traffic-applications.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/mtc-aodv-helper.h"
#include "ns3/node-container.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/simulator.h"
#include "ns3/test.h"
#include "ns3/uinteger.h"
#include "ns3/boolean.h"
#include "ns3/mobility-helper.h"
#include "ns3/position-allocator.h"
#include "ns3/yans-wifi-helper.h"

using namespace ns3;
using namespace ns3::mtcaodv;

namespace
{

/// Portée radio de la fixture, en mètres. 70 m couvre un saut de 60 m, pas deux.
constexpr double FIXTURE_RANGE_METERS = 70.0;
/// Nombre de paquets de données émis par le témoin S.
constexpr uint32_t FIXTURE_PACKET_COUNT = 40;
/// Port UDP applicatif de la fixture (distinct du port de contrôle AODV 654).
constexpr uint16_t FIXTURE_PORT = 9100;

/**
 * \brief Résultat d'une exécution de la fixture : compteurs observés.
 */
struct FixtureOutcome
{
    uint32_t delivered{0};    //!< Paquets applicatifs reçus par D.
    uint32_t transmitted{0};  //!< Paquets applicatifs émis par S.
    uint32_t forgedReplies{0};//!< RREP forgés émis par R (0 si R est honnête).
    uint32_t drops{0};        //!< Paquets consommés par R (0 si R est honnête).
};

/**
 * \brief Exécute la fixture S–R–D une fois.
 *
 * \param installBlackhole R porte-t-il une politique Blackhole ?
 * \param attackStart instant d'activation de l'attaque (ignoré si installBlackhole=false)
 * \return les compteurs observés
 *
 * La graine et les flux RNG sont fixés à l'intérieur, de sorte que les trois cas du test
 * partagent exactement le même scénario exogène : seule la présence et le déclenchement
 * de l'attaque varient.
 */
FixtureOutcome
RunFixture(bool installBlackhole, Time attackStart)
{
    RngSeedManager::SetSeed(20260905);
    RngSeedManager::SetRun(1);
    RngSeedManager::ResetNextStreamIndex();

    NodeContainer nodes;
    nodes.Create(3); // 0 = S (source), 1 = R (relais unique), 2 = D (destination)

    // Positions statiques : la fixture est causale, pas un scénario de mobilité (§16.3).
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positions = CreateObject<ListPositionAllocator>();
    positions->Add(Vector(0.0, 0.0, 0.0));
    positions->Add(Vector(60.0, 0.0, 0.0));
    positions->Add(Vector(120.0, 0.0, 0.0));
    mobility.SetPositionAllocator(positions);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    // Radio 802.11b avec disque de portée dur : réception parfaite en deçà de 70 m,
    // nulle au-delà. Aucun lien marginal, donc aucune perte ambiguë à interpréter.
    YansWifiChannelHelper channel;
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::RangePropagationLossModel",
                               "MaxRange", DoubleValue(FIXTURE_RANGE_METERS));
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("DsssRate11Mbps"),
                                 "ControlMode", StringValue("DsssRate1Mbps"),
                                 "NonUnicastMode", StringValue("DsssRate11Mbps"));
    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);
    WifiHelper::AssignStreams(devices, 40100);

    // Tous les nœuds exécutent le fork : c'est là que vit le hook d'attaque. Le témoin
    // sain n'installe simplement aucune politique, et le fork se comporte alors comme
    // l'AODV d'origine (propriété vérifiée à l'étape 0).
    MtcAodvHelper routing;
    InternetStackHelper internet;
    internet.SetRoutingHelper(routing);
    internet.Install(nodes);
    routing.AssignStreams(nodes, 40200);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.0.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);

    // Politique Blackhole sur R (nœud 1) uniquement, si demandé.
    Ptr<BlackholeBehavior> behavior;
    if (installBlackhole)
    {
        behavior = CreateObject<BlackholeBehavior>();
        behavior->SetAttribute("AttackStartTime", TimeValue(attackStart));
        behavior->SetAttribute("DropTransitData", BooleanValue(true));
        behavior->SetAttribute("PreserveControlPlane", BooleanValue(true));
        nodes.Get(1)->AggregateObject(behavior);
    }

    // Trafic S(0) -> D(2). Un paquet toutes les 200 ms à partir de t=1 s.
    Ptr<CbrTrafficSink> sink = CreateObject<CbrTrafficSink>();
    sink->SetAttribute("ListenPort", UintegerValue(FIXTURE_PORT));
    nodes.Get(2)->AddApplication(sink);
    sink->SetStartTime(Seconds(0.0));
    sink->SetStopTime(Seconds(15.0));

    Ptr<CbrTrafficSource> source = CreateObject<CbrTrafficSource>();
    source->SetAttribute("DestinationAddress", Ipv4AddressValue(interfaces.GetAddress(2)));
    source->SetAttribute("DestinationPort", UintegerValue(FIXTURE_PORT));
    source->SetAttribute("FlowId", UintegerValue(0));
    source->SetAttribute("PacketSize", UintegerValue(128));
    source->SetAttribute("PacketRate", DoubleValue(5.0)); // 5 paquets/s -> 40 paquets en 8 s
    source->AssignStreams(40300);
    nodes.Get(0)->AddApplication(source);
    source->SetStartTime(Seconds(1.0));
    source->SetStopTime(Seconds(1.0 + FIXTURE_PACKET_COUNT / 5.0));

    Simulator::Stop(Seconds(15.0));
    Simulator::Run();

    FixtureOutcome outcome;
    outcome.transmitted = source->GetTransmittedPacketCount();
    outcome.delivered = sink->GetReceivedPacketCount();
    if (behavior)
    {
        outcome.forgedReplies = behavior->GetForgedReplyCount();
        outcome.drops = behavior->GetTransitDropCount();
    }

    Simulator::Destroy();
    return outcome;
}

} // namespace

/**
 * \ingroup mtcaodv
 * T-08 — La chaîne causale du full Blackhole produit l'effet attendu de bout en bout.
 */
class BlackholeCausalChainTestCase : public TestCase
{
  public:
    BlackholeCausalChainTestCase()
        : TestCase("T-08 : chaîne causale S-R-D, témoin sain vs Blackhole vs attaque tardive")
    {
    }

  private:
    void DoRun() override
    {
        // 1. Témoin sain : R relaie normalement.
        const FixtureOutcome healthy = RunFixture(/*installBlackhole=*/false, Seconds(0.0));
        NS_TEST_ASSERT_MSG_GT(healthy.transmitted, 0u, "le témoin doit émettre des paquets");
        NS_TEST_ASSERT_MSG_GT(healthy.delivered, 0u,
                              "sans attaque, R doit relayer et D recevoir des paquets");
        NS_TEST_ASSERT_MSG_EQ(healthy.forgedReplies, 0u,
                              "un nœud sans politique ne forge aucun RREP");
        NS_TEST_ASSERT_MSG_EQ(healthy.drops, 0u,
                              "un nœud sans politique n'abandonne aucun paquet");

        // 2. Blackhole actif dès le départ.
        const FixtureOutcome attacked = RunFixture(/*installBlackhole=*/true, Seconds(0.0));
        NS_TEST_ASSERT_MSG_EQ(attacked.transmitted, healthy.transmitted,
                              "même charge émise : seule l'attaque diffère (appariement)");
        NS_TEST_ASSERT_MSG_GT(attacked.forgedReplies, 0u,
                              "A2.3 : R doit forger au moins un RREP");
        NS_TEST_ASSERT_MSG_GT(attacked.drops, 0u,
                              "A2.4 : R doit abandonner au moins un paquet en transit");
        NS_TEST_ASSERT_MSG_LT(attacked.delivered, healthy.delivered,
                              "l'attaque doit réduire la livraison par rapport au témoin");

        // 3. Attaque programmée après la fin du trafic : la seule présence de la
        //    politique ne doit rien changer (D-04, T-05).
        const FixtureOutcome late = RunFixture(/*installBlackhole=*/true, Seconds(60.0));
        NS_TEST_ASSERT_MSG_EQ(late.forgedReplies, 0u,
                              "avant t_attack, aucun RREP ne doit être forgé");
        NS_TEST_ASSERT_MSG_EQ(late.drops, 0u,
                              "avant t_attack, aucun paquet ne doit être abandonné");
        NS_TEST_ASSERT_MSG_EQ(late.delivered, healthy.delivered,
                              "une attaque inactive doit livrer autant que le témoin sain");
    }
};

/**
 * \ingroup mtcaodv
 * Suite d'intégration de l'étape 1.
 */
class MtcAodvBlackholeIntegrationTestSuite : public TestSuite
{
  public:
    MtcAodvBlackholeIntegrationTestSuite()
        : TestSuite("mtcaodv-blackhole-integration", Type::SYSTEM)
    {
        AddTestCase(new BlackholeCausalChainTestCase, TestCase::Duration::QUICK);
    }
};

static MtcAodvBlackholeIntegrationTestSuite g_mtcAodvBlackholeIntegrationTestSuite;
