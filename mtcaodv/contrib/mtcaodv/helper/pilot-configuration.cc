/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "pilot-configuration.h"

#include "ns3/hash.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace ns3
{
namespace mtcaodv
{

namespace
{

/**
 * Nombre maximal d'hôtes adressables dans un /24.
 *
 * L'adressage normatif du projet pilote est 10.1.0.0 avec le masque 255.255.255.0. Le
 * sous-réseau offre 254 adresses d'hôte utilisables (.1 à .254). Au-delà,
 * `Ipv4AddressHelper` déborderait ; la configuration est refusée plutôt que de produire
 * un plan d'adressage silencieusement faux.
 */
constexpr uint32_t MAXIMUM_HOSTS_IN_SLASH_24 = 254;

/// Taille de l'en-tête applicatif de mesure, en octets (voir MtcTrafficHeader).
constexpr uint32_t TRAFFIC_HEADER_BYTES = 14;

} // namespace

// ---------------------------------------------------------------------------------
// Conversions d'énumérations
// ---------------------------------------------------------------------------------

std::string
ToString(PilotProtocol protocol)
{
    switch (protocol)
    {
    case PilotProtocol::STOCK_AODV:
        return "aodv";
    case PilotProtocol::MTC_AODV_FORK:
        return "mtcaodv";
    }
    throw std::invalid_argument("PilotProtocol inconnu");
}

PilotProtocol
ParsePilotProtocol(const std::string& label)
{
    if (label == "aodv")
    {
        return PilotProtocol::STOCK_AODV;
    }
    if (label == "mtcaodv")
    {
        return PilotProtocol::MTC_AODV_FORK;
    }
    throw std::invalid_argument("protocole inconnu « " + label + " » ; attendu aodv ou mtcaodv");
}

std::string
ToString(PilotMobility mobility)
{
    switch (mobility)
    {
    case PilotMobility::RANDOM_WAYPOINT:
        return "rwp";
    case PilotMobility::STEADY_STATE_RWP:
        return "ssrwp";
    }
    throw std::invalid_argument("PilotMobility inconnu");
}

PilotMobility
ParsePilotMobility(const std::string& label)
{
    if (label == "rwp")
    {
        return PilotMobility::RANDOM_WAYPOINT;
    }
    if (label == "ssrwp")
    {
        return PilotMobility::STEADY_STATE_RWP;
    }
    throw std::invalid_argument("mobilité inconnue « " + label + " » ; attendu rwp ou ssrwp");
}

std::string
ToString(PilotPropagation propagation)
{
    switch (propagation)
    {
    case PilotPropagation::LOG_DISTANCE:
        return "logdistance";
    case PilotPropagation::RANGE_DISC:
        return "range";
    }
    throw std::invalid_argument("PilotPropagation inconnu");
}

PilotPropagation
ParsePilotPropagation(const std::string& label)
{
    if (label == "logdistance")
    {
        return PilotPropagation::LOG_DISTANCE;
    }
    if (label == "range")
    {
        return PilotPropagation::RANGE_DISC;
    }
    throw std::invalid_argument("propagation inconnue « " + label +
                                " » ; attendu logdistance ou range");
}

// ---------------------------------------------------------------------------------
// Repères temporels
// ---------------------------------------------------------------------------------

Time
PilotConfiguration::GetSimulationEnd() const
{
    return Seconds(simulationTime);
}

Time
PilotConfiguration::GetTrafficStart() const
{
    return Seconds(warmupTime);
}

Time
PilotConfiguration::GetTrafficStop() const
{
    return Seconds(simulationTime - drainTime);
}

Time
PilotConfiguration::GetEvaluationStart() const
{
    return GetTrafficStart();
}

Time
PilotConfiguration::GetEvaluationEnd() const
{
    return GetTrafficStop();
}

double
PilotConfiguration::GetEvaluationWindowSeconds() const
{
    // T_eval du §17.1 : la fenêtre pendant laquelle des paquets peuvent être *émis*.
    // C'est le dénominateur des débits de l'Éq. (25) et de la fréquence de découverte de
    // l'Éq. (28). Utiliser la durée totale simulée à la place gonflerait artificiellement
    // le dénominateur du warm-up et de la vidange.
    return simulationTime - drainTime - warmupTime;
}

// ---------------------------------------------------------------------------------
// Ligne de commande
// ---------------------------------------------------------------------------------

void
PilotConfiguration::RegisterCommandLine(CommandLine& commandLine,
                                        std::string& protocolLabel,
                                        std::string& mobilityLabel,
                                        std::string& propagationLabel)
{
    // --- Options normatives de l'étape 0 -------------------------------------------
    commandLine.AddValue("nodes", "Nombre de nœuds N", nodeCount);
    commandLine.AddValue("simTime", "Durée totale simulée (s)", simulationTime);
    commandLine.AddValue("minSpeed", "Vitesse minimale des nœuds (m/s)", minSpeed);
    commandLine.AddValue("maxSpeed", "Vitesse maximale des nœuds (m/s)", maxSpeed);
    commandLine.AddValue("attackerRatio", "Ratio d'attaquants r_a dans [0,1]", attackerRatio);
    commandLine.AddValue("attackStart", "Instant d'activation de l'attaque t_attack (s)",
                         attackStartTime);
    commandLine.AddValue("seed", "Seed RNG ns-3", seed);
    commandLine.AddValue("run", "Numéro de run RNG ns-3", run);

    // --- Protocole et sorties -------------------------------------------------------
    commandLine.AddValue("protocol", "Protocole de routage : aodv (stock) ou mtcaodv (fork)",
                         protocolLabel);
    commandLine.AddValue("outputDir", "Répertoire des fichiers de sortie", outputDirectory);
    commandLine.AddValue("label", "Préfixe des fichiers de sortie", runLabel);

    // --- Zone et mobilité (calibrables, C-28) ---------------------------------------
    commandLine.AddValue("areaWidth", "Largeur de la zone (m)", areaWidth);
    commandLine.AddValue("areaHeight", "Hauteur de la zone (m)", areaHeight);
    commandLine.AddValue("pause", "Pause aux points de passage (s)", pauseTime);
    commandLine.AddValue("mobility", "Modèle de mobilité : rwp ou ssrwp", mobilityLabel);

    // --- Radio (calibrables, C-28) --------------------------------------------------
    commandLine.AddValue("propagation", "Propagation : logdistance ou range", propagationLabel);
    commandLine.AddValue("pathLossExponent", "Exposant de perte de parcours", pathLossExponent);
    commandLine.AddValue("txPowerDbm", "Puissance d'émission (dBm)", txPowerDbm);
    commandLine.AddValue("radioRange", "Portée du modèle range (m)", radioRange);
    commandLine.AddValue("connectivityRadius", "Rayon du diagnostic topologique (m)",
                         connectivityRadius);
    commandLine.AddValue("dataRate", "Débit Wi-Fi des données", dataRate);
    commandLine.AddValue("controlRate", "Débit Wi-Fi du contrôle 802.11", controlRate);
    commandLine.AddValue("nonUnicastRate", "Débit des trames de diffusion", nonUnicastRate);

    // --- AODV -----------------------------------------------------------------------
    commandLine.AddValue("enableHello", "Activer les HELLO d'AODV", enableHello);
    commandLine.AddValue("helloInterval", "Période des HELLO d'AODV (s)", helloInterval);

    // --- Trafic (calibrables, C-28) -------------------------------------------------
    commandLine.AddValue("flows", "Nombre de flux CBR UDP", flowCount);
    commandLine.AddValue("packetSize", "Charge utile applicative (octets)", packetSize);
    commandLine.AddValue("packetRate", "Débit par flux (paquets/s)", packetRate);

    // --- Temps ----------------------------------------------------------------------
    commandLine.AddValue("warmup", "Warm-up exclu des métriques (s)", warmupTime);
    commandLine.AddValue("drain", "Vidange après l'arrêt du trafic (s)", drainTime);

    // --- Attaque et reproductibilité ------------------------------------------------
    commandLine.AddValue("excludeTrafficEndpoints",
                         "Exclure sources et puits du tirage d'attaquants",
                         excludeTrafficEndpoints);
    commandLine.AddValue("seqOffset", "Surcroît de séquence forgé Delta_seq (Éq. 23)",
                         sequenceNumberOffset);
    commandLine.AddValue("advertisedHops", "Sauts annoncés h_fake dans le RREP forgé",
                         advertisedHopCount);
    commandLine.AddValue("forgedLifetime", "Durée de route forgée T_fake (s)",
                         forgedRouteLifetime);
    commandLine.AddValue("dropTransitData", "Abandonner les données en transit (A2.4)",
                         dropTransitData);
    commandLine.AddValue("preserveControlPlane", "Exempter le plan de contrôle de l'abandon",
                         preserveControlPlane);
    commandLine.AddValue("positionStream", "Index RNG du placement initial", positionStream);
    commandLine.AddValue("mobilityStream", "Index RNG de la mobilité", mobilityStream);
    commandLine.AddValue("wifiStream", "Index RNG de la couche Wi-Fi", wifiStream);
    commandLine.AddValue("attackerSelectionStream", "Index RNG du tirage d'attaquants",
                         attackerSelectionStream);
    commandLine.AddValue("trafficStream", "Index RNG du trafic", trafficStream);
    commandLine.AddValue("routingStream", "Index RNG du routage", routingStream);
}

// ---------------------------------------------------------------------------------
// Validation fail-closed
// ---------------------------------------------------------------------------------

void
PilotConfiguration::Validate() const
{
    auto require = [](bool condition, const std::string& message) {
        if (!condition)
        {
            throw std::invalid_argument("PilotConfiguration : " + message);
        }
    };

    // --- Population et adressage ----------------------------------------------------
    require(nodeCount >= 2, "nodeCount doit valoir au moins 2");
    require(nodeCount <= MAXIMUM_HOSTS_IN_SLASH_24,
            "nodeCount dépasse les 254 adresses d'hôte du réseau normatif 10.1.0.0/24 ; "
            "élargir le masque serait une modification du plan d'adressage, pas un détail");

    // --- Zone et mobilité -----------------------------------------------------------
    require(areaWidth > 0.0 && areaHeight > 0.0, "dimensions de zone non positives");
    // Les deux modèles de mobilité exigent une vitesse strictement positive : le modèle
    // stationnaire l'impose par assertion interne (m_minSpeed >= 1e-6), et une vitesse
    // minimale nulle rendrait de toute façon une partie des nœuds définitivement
    // immobiles, ce qui n'est pas le régime demandé (1 à 20 m/s).
    require(minSpeed > 0.0, "minSpeed doit être strictement positif");
    require(maxSpeed >= minSpeed, "maxSpeed inférieur à minSpeed");
    require(pauseTime >= 0.0, "pauseTime négatif");

    // --- Radio ----------------------------------------------------------------------
    require(std::isfinite(pathLossExponent) && pathLossExponent > 0.0,
            "pathLossExponent doit être fini et strictement positif");
    require(std::isfinite(txPowerDbm), "txPowerDbm non fini");
    if (propagation == PilotPropagation::RANGE_DISC)
    {
        require(radioRange > 0.0, "radioRange doit être strictement positif");
    }
    require(connectivityRadius > 0.0, "connectivityRadius doit être strictement positif");

    // --- AODV -----------------------------------------------------------------------
    require(helloInterval > 0.0, "helloInterval doit être strictement positif");

    // --- Trafic ---------------------------------------------------------------------
    require(flowCount >= 1, "flowCount doit valoir au moins 1");
    // Chaque flux consomme deux nœuds distincts : une source et un puits.
    require(2 * flowCount <= nodeCount, "trop de flux pour le nombre de nœuds disponibles");
    require(packetSize >= TRAFFIC_HEADER_BYTES,
            "packetSize doit contenir l'en-tête de mesure (14 octets)");
    require(std::isfinite(packetRate) && packetRate > 0.0,
            "packetRate doit être fini et strictement positif");

    // --- Temps ----------------------------------------------------------------------
    require(std::isfinite(simulationTime) && simulationTime > 0.0,
            "simTime doit être fini et strictement positif");
    require(warmupTime >= 0.0, "warmup négatif");
    require(drainTime >= 0.0, "drain négatif");
    require(warmupTime + drainTime < simulationTime,
            "warm-up et vidange couvrent toute la simulation : la fenêtre d'évaluation "
            "serait vide ou négative");

    // --- Attaque --------------------------------------------------------------------
    require(std::isfinite(attackerRatio) && attackerRatio >= 0.0 && attackerRatio <= 1.0,
            "attackerRatio hors de [0,1] ou non fini (décision D-01)");
    require(attackStartTime >= 0.0, "attackStart négatif");
    if (attackerRatio > 0.0)
    {
        require(attackStartTime < simulationTime - drainTime,
                "l'attaque commencerait après la dernière émission : elle serait inobservable");
    }
    // h_fake est sérialisé dans le champ hop count 8 bits du RrepHeader AODV : au-delà de
    // 255 il déborderait silencieusement. On refuse plutôt que de tronquer.
    require(advertisedHopCount <= 255, "advertisedHops dépasse le champ 8 bits du RREP");
    require(forgedRouteLifetime >= 0.0, "forgedLifetime négatif");

    // --- Flux RNG -------------------------------------------------------------------
    require(positionStream >= 0 && mobilityStream >= 0 && wifiStream >= 0 &&
                attackerSelectionStream >= 0 && trafficStream >= 0 && routingStream >= 0,
            "index de flux RNG négatif : la reproductibilité ne serait plus garantie");
}

// ---------------------------------------------------------------------------------
// Description canonique et empreinte
// ---------------------------------------------------------------------------------

std::map<std::string, std::string>
PilotConfiguration::Describe() const
{
    std::ostringstream stream;
    stream << std::setprecision(10);

    auto asString = [&stream](auto value) {
        stream.str("");
        stream.clear();
        stream << value;
        return stream.str();
    };

    // Le protocole est volontairement absent : c'est la seule grandeur qui doit
    // différer entre deux exécutions appariées (invariant 20.4.4).
    return {
        {"nodeCount", asString(nodeCount)},
        {"areaWidth", asString(areaWidth)},
        {"areaHeight", asString(areaHeight)},
        {"minSpeed", asString(minSpeed)},
        {"maxSpeed", asString(maxSpeed)},
        {"pauseTime", asString(pauseTime)},
        {"mobility", ToString(mobility)},
        {"propagation", ToString(propagation)},
        {"pathLossExponent", asString(pathLossExponent)},
        {"txPowerDbm", asString(txPowerDbm)},
        {"radioRange", asString(radioRange)},
        {"dataRate", dataRate},
        {"controlRate", controlRate},
        {"nonUnicastRate", nonUnicastRate},
        {"enableHello", enableHello ? "1" : "0"},
        {"helloInterval", asString(helloInterval)},
        {"flowCount", asString(flowCount)},
        {"packetSize", asString(packetSize)},
        {"packetRate", asString(packetRate)},
        {"applicationPort", asString(applicationPort)},
        {"simulationTime", asString(simulationTime)},
        {"warmupTime", asString(warmupTime)},
        {"drainTime", asString(drainTime)},
        {"attackerRatio", asString(attackerRatio)},
        {"attackStartTime", asString(attackStartTime)},
        {"excludeTrafficEndpoints", excludeTrafficEndpoints ? "1" : "0"},
        {"sequenceNumberOffset", asString(sequenceNumberOffset)},
        {"advertisedHopCount", asString(advertisedHopCount)},
        {"forgedRouteLifetime", asString(forgedRouteLifetime)},
        {"dropTransitData", dropTransitData ? "1" : "0"},
        {"preserveControlPlane", preserveControlPlane ? "1" : "0"},
        {"seed", asString(seed)},
        {"run", asString(run)},
        {"positionStream", asString(positionStream)},
        {"mobilityStream", asString(mobilityStream)},
        {"wifiStream", asString(wifiStream)},
        {"attackerSelectionStream", asString(attackerSelectionStream)},
        {"trafficStream", asString(trafficStream)},
        {"routingStream", asString(routingStream)},
    };
}

std::string
PilotConfiguration::ComputeScenarioHash() const
{
    // std::map garantit un parcours ordonné par clé : la sérialisation est canonique et
    // deux exécutions appariées produisent nécessairement la même empreinte.
    std::ostringstream canonical;
    for (const auto& [key, value] : Describe())
    {
        canonical << key << '=' << value << ';';
    }

    std::ostringstream hex;
    hex << std::hex << std::setw(8) << std::setfill('0')
        << Hash32(canonical.str().c_str(), canonical.str().size());
    return hex.str();
}

} // namespace mtcaodv
} // namespace ns3
