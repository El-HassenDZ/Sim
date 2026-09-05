/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file
 * \ingroup mtcaodv
 * \brief Scénario pilote MTC-AODV — étape 0 : baseline AODV mesurée et reproductible.
 *
 * **Objectif scientifique de l'étape 0.** Établir une baseline saine *avant* toute
 * défense. La règle de méthode est explicite : si le PDR sans attaque est faible, le
 * défaut doit être diagnostiqué dans la connectivité, la portée radio, la mobilité ou la
 * charge — jamais compensé par de la logique de sécurité. Ce programme produit donc les
 * grandeurs nécessaires à ce diagnostic (degré du graphe, connectivité par flux, nombre
 * de sauts) à côté des métriques de performance.
 *
 * **Ce que le programme fait.**
 * 1. Crée \f$N\f$ nœuds (20 par défaut) mobiles selon Random Waypoint, 1 à 20 m/s.
 * 2. Installe une radio IEEE 802.11 en mode ad hoc (`ns3::AdhocWifiMac`).
 * 3. Adresse le réseau en 10.1.0.0/24, exactement comme le prescrit le plan de
 *    développement.
 * 4. Installe AODV — le module standard `src/aodv/` par défaut, le fork
 *    `contrib/mtcaodv/` sur demande — et n'y touche pas.
 * 5. Génère des flux CBR UDP instrumentés entre les couples de nœuds les plus éloignés.
 * 6. Mesure \f$N_{app}^{tx}\f$, \f$N_{app}^{rx}\f$, les octets applicatifs, puis dérive
 *    PDR, PLR, throughput, goodput et délai moyen sur une fenêtre d'évaluation excluant
 *    le warm-up.
 * 7. Écrit un CSV au schéma normatif et un manifest JSON permettant de rejouer
 *    l'exécution à l'identique.
 *
 * **Ce que le programme ne fait pas, volontairement.** Aucune attaque n'est installée :
 * le câblage de A2.3 et A2.4 dans le protocole appartient à l'étape 1. Aucun détecteur,
 * aucune confiance, aucun registre. Les options `--attackerRatio` et `--attackStart`
 * sont exposées dès maintenant, mais un ratio non nul est **refusé** : produire une
 * ligne de résultats étiquetée « attaquée » alors qu'aucune attaque ne s'exécute
 * fabriquerait une donnée fausse, ce que la règle fail-closed interdit.
 *
 * **Exemple d'exécution.**
 * \code
 *   ./ns3 run "mtc-aodv-pilot --nodes=20 --simTime=60 --minSpeed=1 --maxSpeed=20 \
 *              --attackerRatio=0 --seed=12345 --run=1"
 * \endcode
 *
 * **Flux RNG utilisés** (indices fixes, voir `PilotConfiguration`) : placement initial
 * 70000, mobilité 71000, Wi-Fi 72000, tirage d'attaquants 73001, trafic 74000, routage
 * 75000. Deux exécutions appariées consomment les mêmes coordonnées aléatoires exogènes
 * et doivent afficher la même empreinte de scénario.
 */

#include "ns3/aodv-module.h"
#include "ns3/attack-manager.h"
#include "ns3/blackhole-behavior.h"
#include "ns3/cbr-traffic-applications.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/metrics-collector.h"
#include "ns3/mobility-module.h"
#include "ns3/mtc-aodv-helper.h"
#include "ns3/network-module.h"
#include "ns3/pilot-configuration.h"
#include "ns3/run-record.h"
#include "ns3/topology-probe.h"
#include "ns3/wifi-module.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

using namespace ns3;
using namespace ns3::mtcaodv;

NS_LOG_COMPONENT_DEFINE("MtcAodvPilot");

namespace
{

/**
 * \brief Installe la mobilité et le placement initial.
 *
 * Le placement initial et les trajectoires consomment des flux RNG distincts et fixes,
 * de sorte que deux variantes appariées partent des mêmes positions et suivent les mêmes
 * trajectoires (invariant 20.4.4).
 *
 * Pour `RandomWaypointMobilityModel`, ns-3 exige que l'allocateur de position soit passé
 * *deux fois* : à l'assistant, pour tirer la position initiale de chaque nœud, et au
 * modèle lui-même via l'attribut `PositionAllocator`, pour tirer les destinations
 * successives. Omettre le second provoque un arrêt fatal à l'initialisation
 * (« No position allocator added before using this model »). C'est l'idiome utilisé par
 * `examples/routing/manet-routing-compare.cc` dans ns-3.48, repris ici tel quel.
 *
 * \param nodes nœuds à équiper
 * \param config configuration validée
 */
void
InstallMobility(NodeContainer& nodes, const PilotConfiguration& config)
{
    if (config.mobility == PilotMobility::STEADY_STATE_RWP)
    {
        // Le modèle stationnaire calcule lui-même sa position initiale à
        // l'initialisation, à partir de la distribution stationnaire du RWP : il ne
        // demande donc pas d'allocateur externe.
        MobilityHelper mobility;
        mobility.SetMobilityModel("ns3::SteadyStateRandomWaypointMobilityModel",
                                  "MinX", DoubleValue(0.0),
                                  "MaxX", DoubleValue(config.areaWidth),
                                  "MinY", DoubleValue(0.0),
                                  "MaxY", DoubleValue(config.areaHeight),
                                  "MinSpeed", DoubleValue(config.minSpeed),
                                  "MaxSpeed", DoubleValue(config.maxSpeed),
                                  "MinPause", DoubleValue(0.0),
                                  "MaxPause", DoubleValue(config.pauseTime));
        mobility.Install(nodes);
        mobility.AssignStreams(nodes, config.mobilityStream);
        return;
    }

    // Random Waypoint classique. L'allocateur uniforme sur le rectangle sert à la fois
    // au placement initial et au tirage des destinations.
    std::ostringstream xRange;
    xRange << "ns3::UniformRandomVariable[Min=0.0|Max=" << config.areaWidth << "]";
    std::ostringstream yRange;
    yRange << "ns3::UniformRandomVariable[Min=0.0|Max=" << config.areaHeight << "]";

    ObjectFactory positionFactory;
    positionFactory.SetTypeId("ns3::RandomRectanglePositionAllocator");
    positionFactory.Set("X", StringValue(xRange.str()));
    positionFactory.Set("Y", StringValue(yRange.str()));
    Ptr<PositionAllocator> positionAllocator =
        positionFactory.Create()->GetObject<PositionAllocator>();
    // Deux sous-flux consommés (X et Y) : le placement initial est donc reproductible
    // indépendamment du reste.
    positionAllocator->AssignStreams(config.positionStream);

    std::ostringstream speedRange;
    speedRange << "ns3::UniformRandomVariable[Min=" << config.minSpeed
               << "|Max=" << config.maxSpeed << "]";
    std::ostringstream pauseRange;
    pauseRange << "ns3::ConstantRandomVariable[Constant=" << config.pauseTime << "]";

    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::RandomWaypointMobilityModel",
                              "Speed", StringValue(speedRange.str()),
                              "Pause", StringValue(pauseRange.str()),
                              "PositionAllocator", PointerValue(positionAllocator));
    mobility.SetPositionAllocator(positionAllocator);
    mobility.Install(nodes);

    // Chaque modèle consomme deux sous-flux (vitesse, pause) puis réaffecte le flux de
    // l'allocateur partagé ; c'est le comportement d'AssignStreams en amont et il reste
    // parfaitement déterministe pour un couple (seed, run) donné.
    mobility.AssignStreams(nodes, config.mobilityStream);
}

/**
 * \brief Construit la couche physique et MAC IEEE 802.11 en mode ad hoc.
 *
 * Le MAC est `ns3::AdhocWifiMac`, comme l'impose le plan de développement : aucun point
 * d'accès, aucune association, chaque nœud émet directement.
 *
 * \param nodes nœuds à équiper
 * \param config configuration validée
 * \return les interfaces radio créées
 */
NetDeviceContainer
InstallWifi(NodeContainer& nodes, const PilotConfiguration& config)
{
    YansWifiChannelHelper channel;
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    if (config.propagation == PilotPropagation::RANGE_DISC)
    {
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
    WifiHelper::AssignStreams(devices, config.wifiStream);
    return devices;
}

/**
 * \brief Installe la pile IPv4 et le protocole de routage demandé.
 *
 * La baseline utilise `ns3::aodv::RoutingProtocol`, c'est-à-dire le module standard
 * `src/aodv/` de ns-3.48, jamais modifié (invariant 20.3.1). Le fork
 * `ns3::mtcaodv::RoutingProtocol` est sélectionnable pour vérifier qu'il n'a pas dérivé
 * de son amont : à l'étape 0, les deux doivent produire des résultats indiscernables.
 *
 * \param nodes nœuds à équiper
 * \param config configuration validée
 */
void
InstallInternetStack(NodeContainer& nodes, const PilotConfiguration& config)
{
    InternetStackHelper internet;

    if (config.protocol == PilotProtocol::STOCK_AODV)
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
 * \brief Choisit les couples source/puits, les plus éloignés d'abord.
 *
 * Un appariement par indice de nœud donnerait des couples dont la distance est
 * arbitraire : une bonne partie des flux se retrouverait à un seul saut. Or un chemin
 * sans nœud intermédiaire ne peut être ni intercepté par un Blackhole, ni observé par le
 * mécanisme de forwarding des étapes suivantes : la mesure perdrait son objet.
 *
 * La sélection est déterministe et ne dépend que des positions initiales, elles-mêmes
 * issues d'un flux RNG fixe : deux variantes appariées obtiennent donc exactement les
 * mêmes couples.
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
        Ptr<MobilityModel> model = nodes.Get(i)->GetObject<MobilityModel>();
        NS_ABORT_MSG_IF(!model, "mobilité absente sur le nœud " << i);
        positions[i] = model->GetPosition();
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

    // Tri décroissant par distance ; les indices départagent les égalités pour rendre
    // l'ordre total, donc la sélection strictement reproductible.
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
        // Un nœud ne porte qu'un seul rôle : sinon un même nœud pourrait être source de
        // plusieurs flux et concentrer artificiellement la charge sur sa file.
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

/**
 * \brief Écrit le manifest JSON de l'exécution (A7.2).
 *
 * Le manifest contient tout ce qui est nécessaire pour rejouer l'exécution et pour
 * vérifier l'appariement : version ns-3, protocole, empreinte de scénario, paramètres
 * exogènes complets, attaquants tirés, compteurs observés et diagnostic topologique.
 *
 * \param path chemin du fichier
 * \param config configuration validée
 * \param attackers résultat du tirage d'attaquants
 * \param report métriques calculées
 * \param topology sonde topologique, déjà arrêtée
 * \param flowCounters compteurs par flux
 */
void
WriteManifest(const std::string& path,
              const PilotConfiguration& config,
              const AttackSelectionResult& attackers,
              const MetricsReport& report,
              const Ptr<TopologyProbe>& topology,
              const std::map<uint16_t, FlowCounters>& flowCounters,
              bool attackInstalled,
              uint64_t forgedReplies,
              uint64_t blackholeDrops)
{
    std::ofstream file(path);
    if (!file)
    {
        throw std::runtime_error("impossible d'écrire le manifest : " + path);
    }

    file << std::setprecision(10);
    file << "{\n";
    file << "  \"ns3Version\": \"3.48\",\n";
    file << "  \"step\": 1,\n";
    file << "  \"protocol\": \"" << ToString(config.protocol) << "\",\n";
    file << "  \"scenarioHash\": \"" << config.ComputeScenarioHash() << "\",\n";
    file << "  \"ipv4Network\": \"10.1.0.0\",\n";
    file << "  \"ipv4Mask\": \"255.255.255.0\",\n";
    file << "  \"evaluationWindowSeconds\": "
         << FormatJsonNumber(report.evaluationWindowSeconds) << ",\n";

    file << "  \"parameters\": {\n";
    bool first = true;
    for (const auto& [key, value] : config.Describe())
    {
        file << (first ? "" : ",\n") << "    \"" << key << "\": \"" << value << "\"";
        first = false;
    }
    file << "\n  },\n";

    // Les deux ratios sont exportés séparément : avec exclusion des endpoints, le ratio
    // parmi les nœuds admissibles diffère du ratio demandé.
    file << "  \"attack\": {\n";
    file << "    \"installed\": " << (attackInstalled ? "true" : "false") << ",\n";
    file << "    \"forgedRrepCount\": " << forgedReplies << ",\n";
    file << "    \"blackholeDropCount\": " << blackholeDrops << ",\n";
    file << "    \"attackerRatioRequested\": "
         << FormatJsonNumber(attackers.GetRequestedRatio()) << ",\n";
    file << "    \"attackerRatioAmongEligible\": "
         << FormatJsonNumber(attackers.GetRatioAmongEligible()) << ",\n";
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
    file << "    \"appTxBytes\": " << report.applicationTxPayloadBytes << ",\n";
    file << "    \"appRxBytes\": " << report.applicationRxPayloadBytes << ",\n";
    file << "    \"deliveredNetworkBytes\": " << report.deliveredNetworkBytes << ",\n";
    file << "    \"aodvControlTransmissions\": " << report.aodvControlTransmissions << ",\n";
    file << "    \"routeDiscoveries\": " << report.routeDiscoveries << "\n";
    file << "  },\n";

    file << "  \"perFlow\": [";
    bool firstFlow = true;
    for (const auto& [flowId, counters] : flowCounters)
    {
        file << (firstFlow ? "\n" : ",\n") << "    {\"flowId\": " << flowId
             << ", \"txPackets\": " << counters.txPackets
             << ", \"rxPackets\": " << counters.rxPackets << "}";
        firstFlow = false;
    }
    file << (firstFlow ? "" : "\n  ") << "],\n";

    // Diagnostic topologique : il permet de distinguer « le protocole perd des paquets »
    // de « aucun chemin n'existait ». Sans lui, un PDR faible n'est pas interprétable.
    // Toute grandeur susceptible d'être non applicable passe par FormatJsonNumber() :
    // un flux dont les extrémités n'ont jamais été connectées n'a pas de nombre de sauts
    // moyen, et écrire « nan » produirait un manifest que tout analyseur JSON conforme
    // rejette — invalidant l'exécution entière pour une grandeur pourtant légitimement
    // absente. La convention est « null » en JSON, « NaN » en CSV.
    file << "  \"topology\": {\n";
    file << "    \"connectivityRadius\": " << FormatJsonNumber(config.connectivityRadius) << ",\n";
    file << "    \"meanDegree\": " << FormatJsonNumber(topology->GetMeanDegree()) << ",\n";
    file << "    \"connectedGraphFraction\": "
         << FormatJsonNumber(topology->GetConnectedGraphFraction()) << ",\n";
    file << "    \"flows\": [";
    const std::vector<FlowConnectivity>& connectivity = topology->GetFlowConnectivity();
    for (size_t flow = 0; flow < connectivity.size(); ++flow)
    {
        file << (flow ? ",\n" : "\n") << "      {\"flowId\": " << flow << ", \"connectedFraction\": "
             << FormatJsonNumber(connectivity[flow].GetConnectedFraction())
             << ", \"meanHopCount\": " << FormatJsonNumber(connectivity[flow].GetMeanHopCount())
             << "}";
    }
    file << (connectivity.empty() ? "" : "\n    ") << "]\n";
    file << "  }\n";
    file << "}\n";
}

/**
 * \brief Corps du programme, séparé de main() pour que toute erreur de configuration
 *        remonte comme une exception rattrapée et non comme un std::terminate.
 *
 * \param argc nombre d'arguments
 * \param argv arguments
 * \return 0 si l'exécution est valide, un code non nul sinon
 */
int
RunPilot(int argc, char* argv[])
{
    PilotConfiguration config;

    std::string protocolLabel = ToString(config.protocol);
    std::string mobilityLabel = ToString(config.mobility);
    std::string propagationLabel = ToString(config.propagation);

    CommandLine commandLine(__FILE__);
    config.RegisterCommandLine(commandLine, protocolLabel, mobilityLabel, propagationLabel);
    commandLine.Parse(argc, argv);

    config.protocol = ParsePilotProtocol(protocolLabel);
    config.mobility = ParsePilotMobility(mobilityLabel);
    config.propagation = ParsePilotPropagation(propagationLabel);

    // Le diagnostic topologique doit raisonner avec la portée réellement en vigueur.
    if (config.propagation == PilotPropagation::RANGE_DISC)
    {
        config.connectivityRadius = config.radioRange;
    }

    config.Validate();

    // -------------------------------------------------------------------------------
    // Garde-fou d'étape : l'attaque n'est pas encore câblée au protocole.
    //
    // À l'étape 1, le comportement Blackhole (A2.3, A2.4) est câblé dans le *fork*
    // ns3::mtcaodv::RoutingProtocol. Le module standard src/aodv reste intact
    // (invariant 20.3.1) et ne porte donc aucun hook d'attaque.
    //
    // Conséquence fail-closed : demander une attaque avec le protocole stock produirait
    // une ligne étiquetée « attaquée » où aucun RREP ne serait forgé et aucun paquet
    // détourné — une donnée fausse. On la refuse, en dirigeant vers le fork. La variante
    // A du §16.2 (stock AODV honnête + attaquants forgeurs, piles mixtes) relève de
    // l'étape 10 (admission sécurisée) et n'est pas couverte par ce pilote homogène.
    // -------------------------------------------------------------------------------
    if (config.attackerRatio > 0.0 && config.protocol == PilotProtocol::STOCK_AODV)
    {
        std::cerr << "mtc-aodv-pilot : --attackerRatio=" << config.attackerRatio
                  << " incompatible avec --protocol=aodv.\n"
                  << "  Le module standard src/aodv n'est jamais modifié (invariant "
                     "20.3.1) et ne porte aucun hook d'attaque ; une exécution « attaquée » "
                     "sur ce protocole ne forgerait ni n'abandonnerait rien.\n"
                  << "  Utiliser --protocol=mtcaodv pour une attaque câblée au fork.\n";
        return 1;
    }

    // Coordonnées aléatoires. La réinitialisation explicite du prochain index de flux
    // (§13.1, point 2) permet de relancer plusieurs exécutions dans un même processus
    // sans hériter de l'état de la précédente.
    RngSeedManager::SetSeed(config.seed);
    RngSeedManager::SetRun(config.run);
    RngSeedManager::ResetNextStreamIndex();

    NodeContainer nodes;
    nodes.Create(config.nodeCount);

    InstallMobility(nodes, config);
    NetDeviceContainer devices = InstallWifi(nodes, config);
    InstallInternetStack(nodes, config);

    // Adressage normatif du projet pilote : 10.1.0.0 / 255.255.255.0, soit un /24.
    // La validation de PilotConfiguration garantit N <= 254, donc l'absence de
    // débordement du sous-réseau.
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.0.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);

    // --- Extrémités de flux et tirage d'attaquants ---------------------------------
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

    // Le tirage a lieu même à ratio nul. Il produit alors un ensemble vide, mais il
    // consomme la même trajectoire de code et remplit le manifest de la même façon :
    // l'étape 1 n'aura donc rien à changer au chemin de sélection, seulement à installer
    // les comportements sur les identifiants retournés.
    AttackManager attackManager;
    attackManager.AssignStream(config.attackerSelectionStream);
    const AttackSelectionResult attackers =
        attackManager.SelectAttackers(nodes, config.attackerRatio, excludedIds);

    // --- Installation des comportements Blackhole (A2, étape 1) ---------------------
    //
    // Une instance *distincte* est agrégée sur chaque attaquant (§8.2, invariant
    // 20.4.3) : aucun compteur ni état mutable n'est partagé entre attaquants. Le fork
    // la retrouvera par GetObject<AttackBehavior>() sur le Node, sans que la liste des
    // attaquants ne transite jamais par un composant de détection (invariant 20.2.8).
    //
    // Les pointeurs sont conservés uniquement pour relire les compteurs *après* la
    // simulation, côté évaluation hors ligne — jamais pour piloter la défense.
    std::vector<Ptr<BlackholeBehavior>> behaviors;
    for (uint32_t attackerId : attackers.GetAttackerIds())
    {
        Ptr<BlackholeBehavior> behavior = CreateObject<BlackholeBehavior>();
        behavior->SetAttribute("AttackStartTime", TimeValue(Seconds(config.attackStartTime)));
        behavior->SetAttribute("SequenceNumberOffset",
                               UintegerValue(config.sequenceNumberOffset));
        behavior->SetAttribute("AdvertisedHopCount",
                               UintegerValue(config.advertisedHopCount));
        behavior->SetAttribute("ForgedRouteLifetime",
                               TimeValue(Seconds(config.forgedRouteLifetime)));
        behavior->SetAttribute("DropTransitData", BooleanValue(config.dropTransitData));
        behavior->SetAttribute("PreserveControlPlane",
                               BooleanValue(config.preserveControlPlane));
        NodeList::GetNode(attackerId)->AggregateObject(behavior);
        behaviors.push_back(behavior);
    }

    // --- Trafic CBR UDP -------------------------------------------------------------
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
        source->SetAttribute("DestinationAddress",
                             Ipv4AddressValue(interfaces.GetAddress(sinkIndex)));
        source->SetAttribute("DestinationPort", UintegerValue(port));
        source->SetAttribute("FlowId", UintegerValue(flow));
        source->SetAttribute("PacketSize", UintegerValue(config.packetSize));
        source->SetAttribute("PacketRate", DoubleValue(config.packetRate));
        source->AssignStreams(trafficStream++);
        nodes.Get(sourceIndex)->AddApplication(source);
        sources.Add(source);
    }

    // Les puits démarrent à l'instant 0 : un paquet arrivant sur une socket non encore
    // ouverte serait perdu pour une raison sans rapport avec le protocole évalué.
    sinks.Start(Seconds(0.0));
    sinks.Stop(config.GetSimulationEnd());
    sources.Start(config.GetTrafficStart());
    sources.Stop(config.GetTrafficStop());

    // --- Mesure ---------------------------------------------------------------------
    Ptr<MetricsCollector> metrics = CreateObject<MetricsCollector>();
    metrics->SetEvaluationWindow(config.GetEvaluationStart(), config.GetEvaluationEnd());
    metrics->ConnectTrafficSources(sources);
    metrics->ConnectTrafficSinks(sinks);
    metrics->ConnectRoutingOverhead(nodes);

    Ptr<TopologyProbe> topology = CreateObject<TopologyProbe>();
    topology->Start(nodes,
                    flowEndpoints,
                    config.connectivityRadius,
                    Seconds(1.0),
                    config.GetEvaluationStart(),
                    config.GetEvaluationEnd());

    Simulator::Stop(config.GetSimulationEnd());
    Simulator::Run();

    // Relecture des compteurs d'attaque, côté évaluation hors ligne uniquement (A2,
    // ligne finale ; invariant 20.2.8 : cette lecture ne nourrit aucune défense).
    uint64_t totalForgedReplies = 0;
    uint64_t totalBlackholeDrops = 0;
    for (const Ptr<BlackholeBehavior>& behavior : behaviors)
    {
        totalForgedReplies += behavior->GetForgedReplyCount();
        totalBlackholeDrops += behavior->GetTransitDropCount();
    }

    const MetricsReport report = metrics->ComputeReport();

    // Contrôle d'invariants avant écriture : un PDR hors de [0,1] ou une identité
    // PDR + PLR = 1 violée signale un défaut de comptage, pas un résultat.
    DerivedNetworkMetrics derived;
    derived.packetDeliveryRatio = report.packetDeliveryRatio;
    derived.packetLossRatio = report.packetLossRatio;
    derived.throughputBitsPerSecond = report.throughputBitsPerSecond;
    derived.goodputBitsPerSecond = report.goodputBitsPerSecond;
    derived.meanEndToEndDelay = report.meanEndToEndDelay;
    derived.medianEndToEndDelay = report.medianEndToEndDelay;
    derived.jitter = report.jitter;
    derived.normalizedRoutingOverhead = report.normalizedRoutingOverhead;
    derived.routeDiscoveryFrequency = report.routeDiscoveryFrequency;

    std::string violation;
    if (!CheckNetworkMetricInvariants(derived, &violation))
    {
        std::cerr << "mtc-aodv-pilot : invariant de métrique violé — " << violation << '\n';
        Simulator::Destroy();
        return 2;
    }

    // --- Écriture des sorties -------------------------------------------------------
    std::filesystem::create_directories(config.outputDirectory);
    const std::string prefix = config.outputDirectory + "/" + config.runLabel;

    RunRecord record;
    // Colonnes normatives, dans l'ordre imposé par le schéma.
    record.SetString("protocol", ToString(config.protocol));
    record.SetUnsigned("nodes", config.nodeCount);
    record.SetDouble("simTime", config.simulationTime);
    record.SetDouble("minSpeed", config.minSpeed);
    record.SetDouble("maxSpeed", config.maxSpeed);
    record.SetUnsigned("seed", config.seed);
    record.SetUnsigned("run", config.run);
    record.SetDouble("attackerRatio", config.attackerRatio);
    record.SetUnsigned("attackerCount", attackers.GetAttackerCount());
    record.SetDouble("attackStart", config.attackStartTime);
    record.SetUnsigned("appTxPackets", report.applicationTxPackets);
    record.SetUnsigned("appRxPackets", report.applicationRxPackets);
    record.SetUnsigned("appTxBytes", report.applicationTxPayloadBytes);
    record.SetUnsigned("appRxBytes", report.applicationRxPayloadBytes);
    record.SetMetric("pdr", report.packetDeliveryRatio);
    record.SetMetric("plr", report.packetLossRatio);
    record.SetMetric("throughput_bps", report.throughputBitsPerSecond);
    record.SetMetric("goodput_bps", report.goodputBitsPerSecond);
    record.SetMetric("meanDelay_s", report.meanEndToEndDelay);

    // Colonnes additionnelles de l'étape 0. Elles sont ajoutées *après* les colonnes
    // normatives et ne les déplacent pas : les étapes suivantes procéderont de même.
    record.SetMetric("medianDelay_s", report.medianEndToEndDelay);
    record.SetMetric("jitter_s", report.jitter);
    record.SetMetric("nro", report.normalizedRoutingOverhead);
    record.SetMetric("rdf_per_s", report.routeDiscoveryFrequency);

    // Colonnes de l'étape 1, ajoutées après celles de l'étape 0 (continuité du schéma).
    // Ce sont des compteurs observés à l'exécution, jamais déduits : à ratio nul ils
    // valent légitimement 0 (aucune attaque installée), ce qui n'est pas un « faux zéro »
    // mais l'absence réelle d'événement d'attaque.
    record.SetUnsigned("forgedRrepCount", totalForgedReplies);
    record.SetUnsigned("blackholeDropCount", totalBlackholeDrops);

    record.SetUnsigned("aodvControlTx", report.aodvControlTransmissions);
    record.SetUnsigned("routeDiscoveries", report.routeDiscoveries);
    record.SetUnsigned("deliveredNetworkBytes", report.deliveredNetworkBytes);
    record.SetDouble("evalWindow_s", report.evaluationWindowSeconds);
    record.SetUnsigned("flows", config.flowCount);
    record.SetDouble("areaWidth_m", config.areaWidth);
    record.SetDouble("areaHeight_m", config.areaHeight);
    record.SetDouble("meanDegree", topology->GetMeanDegree());
    record.SetDouble("connectedGraphFraction", topology->GetConnectedGraphFraction());
    record.SetString("mobility", ToString(config.mobility));
    record.SetString("propagation", ToString(config.propagation));
    record.SetString("scenarioHash", config.ComputeScenarioHash());

    const bool attackInstalled = !behaviors.empty();

    record.WriteCsv(prefix + "_metrics.csv");
    WriteManifest(prefix + "_manifest.json",
                  config,
                  attackers,
                  report,
                  topology,
                  metrics->GetFlowCounters(),
                  attackInstalled,
                  totalForgedReplies,
                  totalBlackholeDrops);

    // --- Résumé console -------------------------------------------------------------
    std::cout << "protocole=" << ToString(config.protocol) << " N=" << config.nodeCount
              << " simTime=" << config.simulationTime << "s"
              << " vitesse=[" << config.minSpeed << ',' << config.maxSpeed << "] m/s"
              << " seed=" << config.seed << " run=" << config.run << '\n'
              << "  appTx=" << report.applicationTxPackets
              << " appRx=" << report.applicationRxPackets
              << " PDR=" << FormatMetric(report.packetDeliveryRatio)
              << " PLR=" << FormatMetric(report.packetLossRatio) << '\n'
              << "  throughput=" << FormatMetric(report.throughputBitsPerSecond) << " bit/s"
              << " goodput=" << FormatMetric(report.goodputBitsPerSecond) << " bit/s"
              << " délai moyen=" << FormatMetric(report.meanEndToEndDelay) << " s" << '\n'
              << "  NRO=" << FormatMetric(report.normalizedRoutingOverhead)
              << " découvertes=" << report.routeDiscoveries
              << " fenêtre=" << report.evaluationWindowSeconds << " s" << '\n'
              << "  attaque : N_A=" << attackers.GetAttackerCount()
              << " installée=" << (attackInstalled ? "oui" : "non")
              << " RREP forgés=" << totalForgedReplies
              << " drops Blackhole=" << totalBlackholeDrops << '\n'
              << "  scenarioHash=" << config.ComputeScenarioHash() << '\n';

    std::cout << "  par flux :";
    for (const auto& [flowId, counters] : metrics->GetFlowCounters())
    {
        std::cout << ' ' << flowId << ':' << counters.rxPackets << '/' << counters.txPackets;
    }
    std::cout << '\n';

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
    std::cout << "  sorties : " << prefix << "_metrics.csv, " << prefix << "_manifest.json\n";

    Simulator::Destroy();
    return 0;
}

} // namespace

int
main(int argc, char* argv[])
{
    // Une configuration impossible doit produire un message lisible et un code de retour
    // non nul, que l'orchestrateur Python compte comme un échec (§13.3, §21). Laisser
    // l'exception se propager donnerait un std::terminate, dont le message est tronqué et
    // le code de retour ambigu.
    try
    {
        return RunPilot(argc, argv);
    }
    catch (const std::exception& error)
    {
        std::cerr << "mtc-aodv-pilot : exécution rejetée — " << error.what() << '\n';
        return 1;
    }
}
