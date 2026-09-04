/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Scénario MANET principal (§16.1).
 *
 * Une exécution = une condition expérimentale (variante, ratio d'attaquants, seed).
 * Tout ce qui est exogène — positions, mobilité, radio, trafic, sélection d'attaquants —
 * est dérivé de flux RNG disjoints et reproductibles, de sorte que deux variantes
 * appariées partagent exactement le même scénario (A7.1, invariant 20.4.4).
 *
 * Exemple :
 *   ./ns3 run "mtcaodv-manet-scenario --variant=A --seed=1001 --attackerRatio=0.0"
 */

#include "ns3/applications-module.h"
#include "ns3/aodv-module.h"
#include "ns3/attack-manager.h"
#include "ns3/basic-energy-source-helper.h"
#include "ns3/blackhole-behavior.h"
#include "ns3/cbr-traffic-applications.h"
#include "ns3/core-module.h"
#include "ns3/energy-source-container.h"
#include "ns3/experiment-configuration.h"
#include "ns3/internet-module.h"
#include "ns3/metrics-collector.h"
#include "ns3/mobility-module.h"
#include "ns3/mtc-aodv-helper.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"
#include "ns3/wifi-radio-energy-model-helper.h"

#include <filesystem>
#include <iomanip>
#include <fstream>
#include <iostream>

using namespace ns3;
using namespace ns3::mtcaodv;

NS_LOG_COMPONENT_DEFINE("MtcAodvManetScenario");

namespace
{

/**
 * \brief Installe la mobilité Random Waypoint en régime stationnaire.
 *
 * Le modèle stationnaire est retenu par le §16.1 pour éliminer le biais de vitesse et de
 * densité des premières secondes du RWP classique : sans lui, la comparaison entre
 * variantes serait polluée par un transitoire commun mais non représentatif
 * (réf. 22 de la spécification, Yoon et al.).
 */
void
InstallMobility(NodeContainer& nodes, const ExperimentConfiguration& config)
{
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::SteadyStateRandomWaypointMobilityModel",
                              "MinX", DoubleValue(0.0),
                              "MaxX", DoubleValue(config.areaWidth),
                              "MinY", DoubleValue(0.0),
                              "MaxY", DoubleValue(config.areaHeight),
                              "MinSpeed", DoubleValue(config.speedMin),
                              "MaxSpeed", DoubleValue(config.speedMax),
                              "MinPause", DoubleValue(0.0),
                              "MaxPause", DoubleValue(config.pauseTime));
    mobility.Install(nodes);
    mobility.AssignStreams(nodes, config.mobilityStream);
}

/**
 * \brief Construit la couche radio 802.11 ad hoc.
 *
 * Le modèle de propagation est LogDistance : il produit une portée effective finie et
 * dépendante de la distance, contrairement à un modèle de portée fixe qui masquerait les
 * pertes de lien que le mécanisme OCEA doit précisément savoir distinguer d'une
 * malveillance (§9.2).
 */
NetDeviceContainer
InstallWifi(NodeContainer& nodes, const ExperimentConfiguration& config)
{
    YansWifiChannelHelper channel;
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                               "Exponent", DoubleValue(config.pathLossExponent));

    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    phy.Set("TxPowerStart", DoubleValue(config.txPowerDbm));
    phy.Set("TxPowerEnd", DoubleValue(config.txPowerDbm));

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue(config.dataRate),
                                 "ControlMode", StringValue(config.controlRate),
                                 "NonUnicastMode", StringValue(config.nonUnicastRate));

    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");

    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);
    wifi.AssignStreams(devices, config.wifiStream);
    return devices;
}

/**
 * \brief Installe le protocole de routage correspondant à la variante.
 *
 * La variante A utilise le module standard `src/aodv`, laissé strictement intact
 * (invariant 20.3.1). Toutes les autres variantes utilisent le fork `contrib/mtcaodv`.
 * Les deux protocoles cohabitent dans le même binaire, ce qui permet des exécutions
 * appariées sans recompilation.
 */
void
InstallInternetStack(NodeContainer& nodes, const ExperimentConfiguration& config)
{
    InternetStackHelper internet;

    if (config.variant == ProtocolVariant::A)
    {
        AodvHelper aodv;
        aodv.Set("EnableHello", BooleanValue(config.enableHello));
        internet.SetRoutingHelper(aodv);
        internet.Install(nodes);
        aodv.AssignStreams(nodes, config.routingStream);
    }
    else
    {
        MtcAodvHelper mtcAodv;
        mtcAodv.Set("EnableHello", BooleanValue(config.enableHello));
        internet.SetRoutingHelper(mtcAodv);
        internet.Install(nodes);
        mtcAodv.AssignStreams(nodes, config.routingStream);
    }
}

/**
 * \brief Choisit les couples source/puits des flux CBR.
 *
 * Les endpoints sont pris aux deux extrémités de la liste de nœuds : les sources sont les
 * `flowCount` premiers indices, les puits les `flowCount` derniers. Ce choix est
 * déterministe et identique entre variantes, condition nécessaire de l'appariement. Il
 * n'introduit pas de biais géographique, les positions étant tirées indépendamment de
 * l'indice par le modèle de mobilité stationnaire.
 */
std::vector<std::pair<uint32_t, uint32_t>>
SelectFlowEndpoints(const ExperimentConfiguration& config)
{
    std::vector<std::pair<uint32_t, uint32_t>> endpoints;
    endpoints.reserve(config.flowCount);
    for (uint32_t flow = 0; flow < config.flowCount; ++flow)
    {
        endpoints.emplace_back(flow, config.nodeCount - 1 - flow);
    }
    return endpoints;
}

/// Écrit le manifest de l'exécution (A7.2) : paramètres, hash de scénario et attaquants.
void
WriteManifest(const std::string& path,
              const ExperimentConfiguration& config,
              const AttackSelectionResult& attackers,
              const MetricsReport& report)
{
    std::ofstream file(path);
    if (!file)
    {
        throw std::runtime_error("impossible d'écrire le manifest : " + path);
    }

    file << "{\n";
    file << "  \"ns3Version\": \"3.48\",\n";
    file << "  \"variant\": \"" << ToString(config.variant) << "\",\n";
    file << "  \"scenarioHash\": \"" << config.ComputeScenarioHash() << "\",\n";
    file << "  \"parameters\": {\n";
    bool first = true;
    for (const auto& [key, value] : config.Describe())
    {
        file << (first ? "" : ",\n") << "    \"" << key << "\": \"" << value << "\"";
        first = false;
    }
    file << "\n  },\n";

    // Les deux ratios sont exportés séparément : avec exclusion des endpoints, le ratio
    // parmi candidats dépasse le ratio demandé (voir docs/DIVERGENCES.md § D-I7).
    file << "  \"attack\": {\n";
    file << "    \"attackerRatioRequested\": " << attackers.GetRequestedRatio() << ",\n";
    file << "    \"attackerRatioAmongEligible\": " << attackers.GetRatioAmongEligible() << ",\n";
    file << "    \"attackerCount\": " << attackers.GetAttackerCount() << ",\n";
    file << "    \"eligibleCount\": " << attackers.GetEligibleCount() << ",\n";
    file << "    \"attackerNodeIds\": [";
    for (size_t i = 0; i < attackers.GetAttackerIds().size(); ++i)
    {
        file << (i ? ", " : "") << attackers.GetAttackerIds()[i];
    }
    file << "]\n  },\n";

    file << "  \"observedCounters\": {\n";
    file << "    \"appTxPackets\": " << report.applicationTxPackets << ",\n";
    file << "    \"appRxPackets\": " << report.applicationRxPackets << ",\n";
    file << "    \"aodvControlTransmissions\": " << report.aodvControlTransmissions << ",\n";
    file << "    \"routeDiscoveries\": " << report.routeDiscoveries << ",\n";
    file << "    \"forgedReplies\": " << report.forgedReplies << ",\n";
    file << "    \"blackholeTransitDrops\": " << report.blackholeTransitDrops << "\n";
    file << "  }\n}\n";
}

} // namespace

int
main(int argc, char* argv[])
{
    ExperimentConfiguration config;

    std::string variantLabel = "A";
    CommandLine commandLine(__FILE__);
    config.RegisterCommandLine(commandLine);
    commandLine.AddValue("protocolVariant", "Variante : A, B, C0, C ou D", variantLabel);
    commandLine.Parse(argc, argv);
    config.variant = ParseProtocolVariant(variantLabel);
    config.Validate();

    // Coordonnées aléatoires : réinitialisées explicitement pour qu'une exécution répétée
    // dans le même processus reparte du même état (§13.1 point 2).
    RngSeedManager::SetSeed(config.seed);
    RngSeedManager::SetRun(config.run);

    NodeContainer nodes;
    nodes.Create(config.nodeCount);

    InstallMobility(nodes, config);
    NetDeviceContainer devices = InstallWifi(nodes, config);
    InstallInternetStack(nodes, config);

    Ipv4AddressHelper addresses;
    addresses.SetBase("10.1.0.0", "255.255.0.0");
    Ipv4InterfaceContainer interfaces = addresses.Assign(devices);

    // --- Énergie (Éq. 30) ----------------------------------------------------------
    BasicEnergySourceHelper energySourceHelper;
    energySourceHelper.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(config.initialEnergy));
    energy::EnergySourceContainer energySources = energySourceHelper.Install(nodes);

    WifiRadioEnergyModelHelper radioEnergyHelper;
    radioEnergyHelper.Install(devices, energySources);

    // --- Sélection des attaquants (A2.1, A2.2) --------------------------------------
    // Le tirage a lieu même à ratio nul : le manifest doit toujours porter la trace de la
    // décision, et l'appariement exige que la consommation de flux RNG soit identique
    // entre variantes.
    const std::vector<std::pair<uint32_t, uint32_t>> flowEndpoints = SelectFlowEndpoints(config);
    std::set<uint32_t> excludedIds;
    if (config.excludeTrafficEndpoints)
    {
        for (const auto& [sourceIndex, sinkIndex] : flowEndpoints)
        {
            excludedIds.insert(nodes.Get(sourceIndex)->GetId());
            excludedIds.insert(nodes.Get(sinkIndex)->GetId());
        }
    }

    AttackManager attackManager;
    attackManager.AssignStream(config.attackerSelectionStream);
    const AttackSelectionResult attackers =
        attackManager.SelectAttackers(nodes, config.attackerRatio, excludedIds);

    // Une instance distincte par attaquant (invariant 20.4.3). Le câblage au protocole
    // (A2.3, A2.4) est introduit à l'étape 4 ; à ce stade la politique est installée mais
    // le fork ne l'interroge pas encore.
    std::vector<Ptr<BlackholeBehavior>> attackBehaviors;
    for (uint32_t attackerId : attackers.GetAttackerIds())
    {
        Ptr<BlackholeBehavior> behavior = CreateObject<BlackholeBehavior>();
        behavior->SetAttribute("AttackStartTime", TimeValue(Seconds(config.attackStartTime)));
        NodeList::GetNode(attackerId)->AggregateObject(behavior);
        attackBehaviors.push_back(behavior);
    }

    // --- Trafic ---------------------------------------------------------------------
    ApplicationContainer sources;
    ApplicationContainer sinks;
    int64_t trafficStream = config.trafficStream;

    for (uint32_t flow = 0; flow < config.flowCount; ++flow)
    {
        const auto [sourceIndex, sinkIndex] = flowEndpoints[flow];
        const uint16_t port = config.applicationPort + static_cast<uint16_t>(flow);

        Ptr<CbrTrafficSink> sink = CreateObject<CbrTrafficSink>();
        sink->SetAttribute("ListenPort", UintegerValue(port));
        nodes.Get(sinkIndex)->AddApplication(sink);
        sinks.Add(sink);

        Ptr<CbrTrafficSource> source = CreateObject<CbrTrafficSource>();
        source->SetAttribute("DestinationAddress", Ipv4AddressValue(interfaces.GetAddress(sinkIndex)));
        source->SetAttribute("DestinationPort", UintegerValue(port));
        source->SetAttribute("FlowId", UintegerValue(flow));
        source->SetAttribute("PacketSize", UintegerValue(config.packetSize));
        source->SetAttribute("PacketRate", DoubleValue(config.packetRate));
        source->AssignStreams(trafficStream++);
        nodes.Get(sourceIndex)->AddApplication(source);
        sources.Add(source);
    }

    // Les puits démarrent avant les sources : un paquet arrivant sur une socket non
    // encore ouverte serait perdu pour une raison sans rapport avec le protocole.
    sinks.Start(Seconds(0.0));
    sinks.Stop(config.GetSimulationEnd());
    sources.Start(Seconds(config.trafficStart));
    sources.Stop(Seconds(config.trafficStop));

    // --- Mesure ---------------------------------------------------------------------
    Ptr<MetricsCollector> metrics = CreateObject<MetricsCollector>();
    metrics->SetEvaluationWindow(config.GetEvaluationStart(), config.GetEvaluationEnd());
    metrics->ConnectTrafficSources(sources);
    metrics->ConnectTrafficSinks(sinks);
    metrics->ConnectRoutingOverhead(nodes);
    metrics->ConnectEnergySources(energySources);

    Simulator::Stop(config.GetSimulationEnd());
    Simulator::Run();

    for (const Ptr<BlackholeBehavior>& behavior : attackBehaviors)
    {
        metrics->AddAttackCounters(behavior->GetForgedReplyCount(), behavior->GetTransitDropCount());
    }

    const MetricsReport report = metrics->ComputeReport();

    std::filesystem::create_directories(config.outputDirectory);
    const std::string prefix = config.outputDirectory + "/" + config.runLabel;
    metrics->ExportCsv(prefix + "_metrics.csv",
                       {{"variant", ToString(config.variant)},
                        {"seed", std::to_string(config.seed)},
                        {"run", std::to_string(config.run)},
                        {"attackerRatio", std::to_string(config.attackerRatio)},
                        {"attackerCount", std::to_string(attackers.GetAttackerCount())},
                        {"scenarioHash", config.ComputeScenarioHash()}});
    WriteManifest(prefix + "_manifest.json", config, attackers, report);

    std::cout << "variante=" << ToString(config.variant) << " seed=" << config.seed
              << " r_a=" << config.attackerRatio << " N_A=" << attackers.GetAttackerCount() << '\n'
              << "  appTx=" << report.applicationTxPackets << " appRx=" << report.applicationRxPackets
              << " PDR=" << (report.packetDeliveryRatio ? *report.packetDeliveryRatio : NAN) << '\n'
              << "  délai moyen=" << (report.meanEndToEndDelay ? *report.meanEndToEndDelay : NAN) << " s"
              << "  NRO=" << (report.normalizedRoutingOverhead ? *report.normalizedRoutingOverhead : NAN)
              << "  découvertes=" << report.routeDiscoveries << '\n'
              << "  scenarioHash=" << config.ComputeScenarioHash() << '\n';

    // Détail par flux : indispensable pour distinguer une perte diffuse d'un flux
    // entièrement coupé, deux situations qui appellent des diagnostics opposés.
    std::cout << "  par flux :";
    for (const auto& [flowId, counters] : metrics->GetFlowCounters())
    {
        const double ratio =
            counters.txPackets ? static_cast<double>(counters.rxPackets) / counters.txPackets : NAN;
        std::cout << ' ' << flowId << ':' << counters.rxPackets << '/' << counters.txPackets << '('
                  << std::fixed << std::setprecision(2) << ratio << ')';
    }
    std::cout << std::defaultfloat << '\n';

    Simulator::Destroy();
    return 0;
}
