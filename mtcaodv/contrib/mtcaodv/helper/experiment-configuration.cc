/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "experiment-configuration.h"

#include "ns3/hash.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace ns3
{
namespace mtcaodv
{

std::string
ToString(ProtocolVariant variant)
{
    switch (variant)
    {
    case ProtocolVariant::A:
        return "A";
    case ProtocolVariant::B:
        return "B";
    case ProtocolVariant::C0:
        return "C0";
    case ProtocolVariant::C:
        return "C";
    case ProtocolVariant::D:
        return "D";
    }
    return "?";
}

ProtocolVariant
ParseProtocolVariant(const std::string& label)
{
    if (label == "A")
    {
        return ProtocolVariant::A;
    }
    if (label == "B")
    {
        return ProtocolVariant::B;
    }
    if (label == "C0")
    {
        return ProtocolVariant::C0;
    }
    if (label == "C")
    {
        return ProtocolVariant::C;
    }
    if (label == "D")
    {
        return ProtocolVariant::D;
    }
    throw std::invalid_argument("variante inconnue : '" + label + "' (attendu A, B, C0, C ou D)");
}

Time
ExperimentConfiguration::GetSimulationEnd() const
{
    return Seconds(trafficStop + drainTime);
}

Time
ExperimentConfiguration::GetEvaluationStart() const
{
    return Seconds(warmupEnd);
}

Time
ExperimentConfiguration::GetEvaluationEnd() const
{
    // La fenêtre s'arrête à la dernière émission possible : les paquets encore en vol
    // disposent de `drainTime` pour arriver et restent comptés, car le filtrage se fait
    // sur l'instant d'émission (voir MetricsCollector::RecordApplicationRx).
    return Seconds(trafficStop);
}

void
ExperimentConfiguration::RegisterCommandLine(CommandLine& commandLine)
{
    static std::string variantLabel = ToString(variant);

    commandLine.AddValue("variant", "Variante évaluée : A, B, C0, C ou D", variantLabel);
    commandLine.AddValue("seed", "Seed RNG ns-3", seed);
    commandLine.AddValue("run", "Numéro de run RNG ns-3", run);
    commandLine.AddValue("outputDir", "Répertoire de sortie", outputDirectory);
    commandLine.AddValue("runLabel", "Préfixe des fichiers de sortie", runLabel);

    commandLine.AddValue("nodeCount", "Nombre de nœuds N", nodeCount);
    commandLine.AddValue("areaWidth", "Largeur de la zone (m)", areaWidth);
    commandLine.AddValue("areaHeight", "Hauteur de la zone (m)", areaHeight);
    commandLine.AddValue("speedMin", "Vitesse minimale (m/s)", speedMin);
    commandLine.AddValue("speedMax", "Vitesse maximale (m/s)", speedMax);
    commandLine.AddValue("pauseTime", "Temps de pause RWP (s)", pauseTime);

    commandLine.AddValue("dataRate", "Débit Wi-Fi des données", dataRate);
    commandLine.AddValue("controlRate", "Débit Wi-Fi du contrôle", controlRate);
    commandLine.AddValue("nonUnicastRate", "Débit des trames de diffusion", nonUnicastRate);
    commandLine.AddValue("txPowerDbm", "Puissance d'émission (dBm)", txPowerDbm);
    commandLine.AddValue("pathLossExponent", "Exposant de perte de parcours", pathLossExponent);

    commandLine.AddValue("enableHello", "Activer les messages HELLO d'AODV", enableHello);
    commandLine.AddValue("flowCount", "Nombre de flux CBR", flowCount);
    commandLine.AddValue("packetSize", "Charge utile applicative (octets)", packetSize);
    commandLine.AddValue("packetRate", "Débit par flux (paquets/s)", packetRate);

    commandLine.AddValue("warmupEnd", "Fin du warm-up (s)", warmupEnd);
    commandLine.AddValue("trafficStart", "Début du trafic (s)", trafficStart);
    commandLine.AddValue("trafficStop", "Fin du trafic (s)", trafficStop);
    commandLine.AddValue("drainTime", "Durée de vidange après le trafic (s)", drainTime);

    commandLine.AddValue("attackerRatio", "Ratio d'attaquants r_a", attackerRatio);
    commandLine.AddValue("attackStartTime", "Début de l'attaque t_attack (s)", attackStartTime);
    commandLine.AddValue("excludeTrafficEndpoints",
                         "Exclure les endpoints du tirage d'attaquants",
                         excludeTrafficEndpoints);

    commandLine.AddValue("initialEnergy", "Énergie initiale par nœud (J)", initialEnergy);

    // La variante est relue après le parsing par le programme appelant, via
    // ParseProtocolVariant(variantLabel) : CommandLine ne sait pas remplir un enum.
}

void
ExperimentConfiguration::Validate() const
{
    auto require = [](bool condition, const std::string& message) {
        if (!condition)
        {
            throw std::invalid_argument("ExperimentConfiguration : " + message);
        }
    };

    require(nodeCount >= 2, "nodeCount doit valoir au moins 2");
    require(areaWidth > 0.0 && areaHeight > 0.0, "dimensions de zone non positives");
    require(speedMin > 0.0, "speedMin doit être strictement positif (RWP en régime stationnaire)");
    require(speedMax >= speedMin, "speedMax inférieur à speedMin");
    require(pauseTime >= 0.0, "pauseTime négatif");

    require(flowCount >= 1, "flowCount doit valoir au moins 1");
    // Chaque flux consomme deux nœuds distincts (source et puits).
    require(2 * flowCount <= nodeCount, "trop de flux pour le nombre de nœuds disponibles");
    require(packetSize >= 14, "packetSize doit contenir l'en-tête de mesure (14 octets)");
    require(packetRate > 0.0, "packetRate doit être strictement positif");

    require(warmupEnd >= 0.0, "warmupEnd négatif");
    require(trafficStart >= 0.0, "trafficStart négatif");
    require(trafficStop > trafficStart, "trafficStop doit suivre trafficStart");
    require(warmupEnd < trafficStop, "le warm-up couvre toute la fenêtre d'évaluation");
    require(drainTime >= 0.0, "drainTime négatif");

    require(std::isfinite(attackerRatio) && attackerRatio >= 0.0 && attackerRatio <= 1.0,
            "attackerRatio hors de [0,1]");
    require(attackStartTime >= 0.0, "attackStartTime négatif");
    if (attackerRatio > 0.0)
    {
        require(attackStartTime < trafficStop,
                "l'attaque démarrerait après la fin du trafic : elle serait inobservable");
    }

    require(initialEnergy > 0.0, "initialEnergy doit être strictement positive");
    require(mobilityStream >= 0 && wifiStream >= 0 && attackerSelectionStream >= 0 &&
                trafficStream >= 0 && routingStream >= 0,
            "index de flux RNG négatif");
}

std::map<std::string, std::string>
ExperimentConfiguration::Describe() const
{
    std::ostringstream stream;
    stream << std::setprecision(10);

    auto asString = [&stream](auto value) {
        stream.str("");
        stream.clear();
        stream << value;
        return stream.str();
    };

    return {
        {"nodeCount", asString(nodeCount)},
        {"areaWidth", asString(areaWidth)},
        {"areaHeight", asString(areaHeight)},
        {"speedMin", asString(speedMin)},
        {"speedMax", asString(speedMax)},
        {"pauseTime", asString(pauseTime)},
        {"dataRate", dataRate},
        {"enableHello", enableHello ? "1" : "0"},
        {"controlRate", controlRate},
        {"nonUnicastRate", nonUnicastRate},
        {"txPowerDbm", asString(txPowerDbm)},
        {"pathLossExponent", asString(pathLossExponent)},
        {"flowCount", asString(flowCount)},
        {"packetSize", asString(packetSize)},
        {"packetRate", asString(packetRate)},
        {"warmupEnd", asString(warmupEnd)},
        {"trafficStart", asString(trafficStart)},
        {"trafficStop", asString(trafficStop)},
        {"drainTime", asString(drainTime)},
        {"attackerRatio", asString(attackerRatio)},
        {"attackStartTime", asString(attackStartTime)},
        {"excludeTrafficEndpoints", excludeTrafficEndpoints ? "1" : "0"},
        {"initialEnergy", asString(initialEnergy)},
        {"seed", asString(seed)},
        {"run", asString(run)},
        {"mobilityStream", asString(mobilityStream)},
        {"wifiStream", asString(wifiStream)},
        {"attackerSelectionStream", asString(attackerSelectionStream)},
        {"trafficStream", asString(trafficStream)},
        {"routingStream", asString(routingStream)},
    };
}

std::string
ExperimentConfiguration::ComputeScenarioHash() const
{
    // std::map garantit un ordre de parcours déterministe : la sérialisation est donc
    // canonique et deux exécutions appariées produisent la même empreinte.
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
