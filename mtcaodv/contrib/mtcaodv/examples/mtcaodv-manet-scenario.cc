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
#include "ns3/topology-probe.h"
#include "ns3/wifi-module.h"
#include "ns3/wifi-radio-energy-model-helper.h"

#include <filesystem>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <tuple>
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
    if (config.mobilityProfile == MobilityProfile::STATIC_GRID)
    {
        // Grille régulière immobile. Aucune route ne casse : c'est une fixture causale,
        // au même titre que le diamant du §16.3, et non un résultat de mobilité.
        const uint32_t count = nodes.GetN();
        const uint32_t width = static_cast<uint32_t>(std::ceil(std::sqrt(count)));
        const double spacing = width > 1 ? config.areaWidth / (width - 1) : 0.0;

        Ptr<ListPositionAllocator> positions = CreateObject<ListPositionAllocator>();
        for (uint32_t index = 0; index < count; ++index)
        {
            positions->Add(Vector((index % width) * spacing, (index / width) * spacing, 0.0));
        }

        MobilityHelper gridMobility;
        gridMobility.SetPositionAllocator(positions);
        gridMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        gridMobility.Install(nodes);
        return;
    }

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
    if (config.propagationModel == PropagationModel::RANGE_DISC)
    {
        // Disque dur : réception parfaite en deçà de la portée, nulle au-delà. Aucun
        // lien marginal, donc aucune rupture ambiguë à interpréter.
        channel.AddPropagationLoss("ns3::RangePropagationLossModel",
                                   "MaxRange", DoubleValue(config.radioRange));
    }
    else
    {
        channel.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                                   "Exponent", DoubleValue(config.pathLossExponent));
    }

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
        aodv.Set("HelloInterval", TimeValue(Seconds(config.helloInterval)));
        internet.SetRoutingHelper(aodv);
        internet.Install(nodes);
        aodv.AssignStreams(nodes, config.routingStream);
    }
    else
    {
        MtcAodvHelper mtcAodv;
        mtcAodv.Set("EnableHello", BooleanValue(config.enableHello));
        mtcAodv.Set("HelloInterval", TimeValue(Seconds(config.helloInterval)));
        internet.SetRoutingHelper(mtcAodv);
        internet.Install(nodes);
        mtcAodv.AssignStreams(nodes, config.routingStream);
    }
}

/**
 * \brief Choisit les couples source/puits des flux CBR, les plus éloignés d'abord.
 *
 * Un choix par indice de nœud donnerait des couples dont les positions sont
 * arbitraires : mesuré sur ce scénario, la plupart des flux se retrouvent alors à un
 * seul saut. Or un chemin sans nœud intermédiaire ne peut être ni intercepté par un
 * Blackhole, ni observé par le mécanisme de forwarding : l'expérience perdrait son objet.
 *
 * La sélection retient donc les `flowCount` couples disjoints les plus éloignés à
 * l'instant initial. Elle est déterministe et ne dépend que des positions, elles-mêmes
 * issues du flux RNG de mobilité : deux variantes appariées obtiennent exactement les
 * mêmes couples (invariant 20.4.4).
 *
 * \param nodes nœuds du scénario, mobilité déjà installée
 * \param flowCount nombre de flux souhaité
 * \return les couples (indice source, indice destination)
 * \throw std::invalid_argument si le réseau ne compte pas assez de nœuds
 */
std::vector<std::pair<uint32_t, uint32_t>>
SelectFlowEndpoints(const NodeContainer& nodes, uint32_t flowCount)
{
    const uint32_t count = nodes.GetN();
    if (2 * flowCount > count)
    {
        throw std::invalid_argument("pas assez de nœuds pour le nombre de flux demandé");
    }

    std::vector<Vector> positions(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        positions[i] = nodes.Get(i)->GetObject<MobilityModel>()->GetPosition();
    }

    struct Candidate
    {
        double squaredDistance;
        uint32_t source;
        uint32_t destination;
    };

    std::vector<Candidate> candidates;
    candidates.reserve(static_cast<size_t>(count) * (count - 1) / 2);
    for (uint32_t i = 0; i < count; ++i)
    {
        for (uint32_t j = i + 1; j < count; ++j)
        {
            const double dx = positions[i].x - positions[j].x;
            const double dy = positions[i].y - positions[j].y;
            candidates.push_back({dx * dx + dy * dy, i, j});
        }
    }

    // Tri décroissant par distance. Les indices départagent les égalités, ce qui rend
    // l'ordre total et donc la sélection reproductible à l'identique.
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.squaredDistance != b.squaredDistance)
        {
            return a.squaredDistance > b.squaredDistance;
        }
        return std::tie(a.source, a.destination) < std::tie(b.source, b.destination);
    });

    std::vector<std::pair<uint32_t, uint32_t>> endpoints;
    std::vector<bool> used(count, false);
    for (const Candidate& candidate : candidates)
    {
        if (endpoints.size() == flowCount)
        {
            break;
        }
        // Un nœud ne porte qu'un seul rôle : sans cette contrainte, un même nœud
        // pourrait être source de plusieurs flux et concentrer artificiellement la charge.
        if (used[candidate.source] || used[candidate.destination])
        {
            continue;
        }
        used[candidate.source] = true;
        used[candidate.destination] = true;
        endpoints.emplace_back(candidate.source, candidate.destination);
    }

    NS_ABORT_MSG_IF(endpoints.size() != flowCount, "sélection des extrémités de flux incomplète");
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
    std::string propagationLabel = ToString(config.propagationModel);
    std::string mobilityLabel = ToString(config.mobilityProfile);

    CommandLine commandLine(__FILE__);
    config.RegisterCommandLine(commandLine);
    commandLine.AddValue("protocolVariant", "Variante : A, B, C0, C ou D", variantLabel);
    commandLine.AddValue("propagationModel", "Propagation : logdistance ou range", propagationLabel);
    commandLine.AddValue("mobilityProfile", "Mobilité : rwp ou grid", mobilityLabel);
    commandLine.Parse(argc, argv);

    config.variant = ParseProtocolVariant(variantLabel);
    config.propagationModel = ParsePropagationModel(propagationLabel);
    config.mobilityProfile = ParseMobilityProfile(mobilityLabel);

    // Le diagnostic topologique doit raisonner avec la portée réellement en vigueur.
    if (config.propagationModel == PropagationModel::RANGE_DISC)
    {
        config.connectivityRadius = config.radioRange;
    }
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
    const std::vector<std::pair<uint32_t, uint32_t>> flowEndpoints =
        SelectFlowEndpoints(nodes, config.flowCount);
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

    // Diagnostic topologique : mesure hors ligne de la connectivité géométrique, pour
    // distinguer une perte imputable au protocole d'une absence pure et simple de chemin.
    Ptr<TopologyProbe> topology = CreateObject<TopologyProbe>();
    topology->Start(nodes,
                    flowEndpoints,
                    config.connectivityRadius,
                    Seconds(1.0),
                    config.GetEvaluationStart(),
                    config.GetEvaluationEnd());

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

    std::cout << "  topologie : degré moyen=" << std::fixed << std::setprecision(2)
              << topology->GetMeanDegree()
              << " graphe connexe=" << topology->GetConnectedGraphFraction() << '\n'
              << "  connectivité par flux :";
    const std::vector<FlowConnectivity>& connectivity = topology->GetFlowConnectivity();
    for (size_t flow = 0; flow < connectivity.size(); ++flow)
    {
        std::cout << ' ' << flow << ':' << connectivity[flow].GetConnectedFraction() << '/'
                  << std::setprecision(1) << connectivity[flow].GetMeanHopCount() << "sauts"
                  << std::setprecision(2);
    }
    std::cout << std::defaultfloat << '\n';

    Simulator::Destroy();
    return 0;
}
