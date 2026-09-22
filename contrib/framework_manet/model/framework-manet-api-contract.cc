/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * framework_manet — STEP 0.
 *
 * Implémentation du contrat d'API de la baseline AODV (voir le fichier
 * d'en-tête pour la justification générale).
 *
 * Toutes les chaînes listées dans GetBaselineApiRequirements() ont été
 * relevées dans les sources officielles de ns-3.48 (src/aodv, src/wifi,
 * src/mobility, src/propagation, src/applications, src/internet,
 * src/energy). Le test unitaire du module et l'exemple
 * framework-manet-env-check vérifient qu'elles existent réellement dans
 * l'installation compilée.
 */

#include "framework-manet-api-contract.h"

// En-têtes nécessaires uniquement pour AnchorRequiredModules() : un symbole
// par module requis, plus les signatures vérifiées à la compilation.
#include "ns3/adhoc-wifi-mac.h"
#include "ns3/animation-interface.h"
#include "ns3/aodv-helper.h"
#include "ns3/aodv-routing-protocol.h"
#include "ns3/basic-energy-source.h"
#include "ns3/flow-monitor.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/ipv4-routing-helper.h"
#include "ns3/mobility-helper.h"
#include "ns3/node.h"
#include "ns3/onoff-application.h"
#include "ns3/output-stream-wrapper.h"
#include "ns3/packet-sink.h"
#include "ns3/position-allocator.h"
#include "ns3/propagation-loss-model.h"
#include "ns3/random-waypoint-mobility-model.h"
#include "ns3/type-id.h"
#include "ns3/wifi-helper.h"
#include "ns3/wifi-radio-energy-model.h"

#include <type_traits>

namespace ns3
{
namespace fmanet
{

const char* const TARGET_NS3_VERSION = "3.48";

// ---------------------------------------------------------------------------
// Vérifications de signatures à la compilation.
//
// Ces fonctions/méthodes ne sont pas des TypeId : leur existence et leur
// signature ne peuvent pas être vérifiées par nom à l'exécution. Un
// static_assert produit un message explicite si ns-3 les modifie.
// ---------------------------------------------------------------------------

// STEP 4 : export des tables AODV à des instants choisis.
static_assert(std::is_same_v<decltype(&Ipv4RoutingHelper::PrintRoutingTableAllAt),
                             void (*)(Time, Ptr<OutputStreamWrapper>, Time::Unit)>,
              "Ipv4RoutingHelper::PrintRoutingTableAllAt a changé de signature");

// STEP 4 : NetAnim (le module netanim doit être compilé et lié).
static_assert(std::is_same_v<decltype(&AnimationInterface::IsInitialized), bool (*)()>,
              "AnimationInterface::IsInitialized a changé de signature");

// STEP 1 : affectation explicite des streams RNG (reproductibilité et
// expériences appariées). Chaque helper retourne le nombre de streams
// consommés, ce qui permet de vérifier l'absence de chevauchement des blocs.
static_assert(std::is_same_v<decltype(&AodvHelper::AssignStreams),
                             int64_t (AodvHelper::*)(NodeContainer, int64_t)>,
              "AodvHelper::AssignStreams a changé de signature");
static_assert(std::is_same_v<decltype(&MobilityHelper::AssignStreams),
                             int64_t (MobilityHelper::*)(NodeContainer, int64_t)>,
              "MobilityHelper::AssignStreams a changé de signature");
static_assert(
    std::is_same_v<decltype(&WifiHelper::AssignStreams), int64_t (*)(NetDeviceContainer, int64_t)>,
    "WifiHelper::AssignStreams a changé de signature");
static_assert(std::is_same_v<decltype(&InternetStackHelper::AssignStreams),
                             int64_t (InternetStackHelper::*)(NodeContainer, int64_t)>,
              "InternetStackHelper::AssignStreams a changé de signature");
// MobilityHelper::AssignStreams NE couvre PAS l'allocateur de positions
// initiales (documenté dans mobility-helper.h) : STEP 1 devra appeler
// PositionAllocator::AssignStreams explicitement, avant Install().
static_assert(std::is_same_v<decltype(&PositionAllocator::AssignStreams),
                             int64_t (PositionAllocator::*)(int64_t)>,
              "PositionAllocator::AssignStreams a changé de signature");

// ---------------------------------------------------------------------------
// Environnement de compilation
// ---------------------------------------------------------------------------

BuildEnvironment
GetBuildEnvironment()
{
    BuildEnvironment env;

    // FMANET_NS3_VERSION est injecté par contrib/framework_manet/CMakeLists.txt
    // à partir de la variable CMake NS3_VER (fichier VERSION de ns-3).
#ifdef FMANET_NS3_VERSION
    env.ns3Version = FMANET_NS3_VERSION;
#else
    env.ns3Version = "unknown";
#endif

    // Macros définies par build-support/macros-and-definitions.cmake.
    // Remarque : le profil ns-3 "default" définit NS3_BUILD_PROFILE_DEBUG
    // (asserts et logs actifs, optimisation -Os) ; il est donc rapporté
    // comme "debug", ce qui est le comportement réel de ns-3.48.
#if defined(NS3_BUILD_PROFILE_DEBUG)
    env.buildProfile = "debug";
#elif defined(NS3_BUILD_PROFILE_OPTIMIZED)
    env.buildProfile = "optimized";
#elif defined(NS3_BUILD_PROFILE_RELEASE)
    env.buildProfile = "release";
#else
    env.buildProfile = "unknown";
#endif

#ifdef NS3_ASSERT_ENABLE
    env.assertsEnabled = true;
#else
    env.assertsEnabled = false;
#endif

#ifdef NS3_LOG_ENABLE
    env.logsEnabled = true;
#else
    env.logsEnabled = false;
#endif

    // HAVE_GSL est ajouté globalement par add_definitions() lorsque GSL est
    // trouvé au configure : la macro vue ici est celle vue par src/wifi.
#ifdef HAVE_GSL
    env.gslEnabled = true;
#else
    env.gslEnabled = false;
#endif

    // ns3/core-config.h (généré) choisit l'implémentation de int64x64_t,
    // qui porte l'arithmétique de ns3::Time.
#if defined(INT64X64_USE_128)
    env.int64x64Impl = "int128";
#elif defined(INT64X64_USE_CAIRO)
    env.int64x64Impl = "cairo";
#elif defined(INT64X64_USE_DOUBLE)
    env.int64x64Impl = "long double";
#else
    env.int64x64Impl = "unknown";
#endif

#if defined(__clang__)
    env.compiler = std::string("clang ") + __clang_version__;
#elif defined(__GNUC__)
    env.compiler = std::string("gcc ") + __VERSION__;
#else
    env.compiler = "unknown";
#endif

    env.cxxStandard = __cplusplus;
    return env;
}

// ---------------------------------------------------------------------------
// Ancrage des modules
// ---------------------------------------------------------------------------

uint32_t
AnchorRequiredModules()
{
    // Chaque appel référence un symbole d'une bibliothèque différente, ce qui
    // oblige l'éditeur de liens à la conserver (cf. --as-needed) et donc à
    // exécuter ses enregistrements de TypeId au chargement.
    const TypeId anchors[] = {
        aodv::RoutingProtocol::GetTypeId(),           // aodv
        AdhocWifiMac::GetTypeId(),                    // wifi
        WifiRadioEnergyModel::GetTypeId(),            // wifi (modèle radio énergie)
        RandomWaypointMobilityModel::GetTypeId(),     // mobility
        LogDistancePropagationLossModel::GetTypeId(), // propagation
        Ipv4L3Protocol::GetTypeId(),                  // internet
        OnOffApplication::GetTypeId(),                // applications
        PacketSink::GetTypeId(),                      // applications
        energy::BasicEnergySource::GetTypeId(),       // energy
        FlowMonitor::GetTypeId(),                     // flow-monitor
        Node::GetTypeId(),                            // network
    };

    uint32_t anchored = 0;
    for (const auto& tid : anchors)
    {
        // Un uid nul signifierait un TypeId invalide ; en pratique GetTypeId()
        // échouerait avant. Le test sert surtout à consommer la valeur.
        anchored += (tid.GetUid() != 0) ? 1 : 0;
    }

    // netanim ne définit pas de TypeId pour AnimationInterface : on référence
    // une fonction statique. Aucune instance n'est créée à STEP 0 (le
    // constructeur ouvrirait un fichier XML).
    anchored += AnimationInterface::IsInitialized() ? 0 : 1;
    return anchored;
}

// ---------------------------------------------------------------------------
// Liste des exigences
// ---------------------------------------------------------------------------

const std::vector<ApiRequirement>&
GetBaselineApiRequirements()
{
    using K = ApiItemKind;

    // Liste construite une seule fois (initialisation statique locale,
    // thread-safe en C++11) et conservée dans l'ordre des étapes.
    static const std::vector<ApiRequirement> requirements = {
        // ------------------------------------------------------------ STEP 1
        // Routage : AODV officiel, sans modification (src/aodv inchangé).
        {K::TYPE_ID, "ns3::aodv::RoutingProtocol", "", "STEP 1", "routage baseline"},
        {K::ATTRIBUTE,
         "ns3::aodv::RoutingProtocol",
         "EnableHello",
         "STEP 1",
         "journaliser la configuration HELLO (source d'overhead)"},
        {K::ATTRIBUTE,
         "ns3::aodv::RoutingProtocol",
         "HelloInterval",
         "STEP 1",
         "journaliser la période HELLO"},

        // Wi-Fi 802.11b ad hoc.
        {K::TYPE_ID, "ns3::AdhocWifiMac", "", "STEP 1", "MAC ad hoc (pas d'AP/STA)"},
        {K::TYPE_ID, "ns3::YansWifiPhy", "", "STEP 1", "PHY Yans"},
        {K::TYPE_ID, "ns3::YansWifiChannel", "", "STEP 1", "canal Yans"},
        {K::ATTRIBUTE, "ns3::YansWifiPhy", "TxPowerStart", "STEP 1", "txPower = 16 dBm"},
        {K::ATTRIBUTE, "ns3::YansWifiPhy", "TxPowerEnd", "STEP 1", "txPower = 16 dBm"},
        {K::ATTRIBUTE, "ns3::YansWifiPhy", "TxPowerLevels", "STEP 1", "un seul niveau"},
        {K::ATTRIBUTE,
         "ns3::YansWifiPhy",
         "RxSensitivity",
         "STEP 1",
         "seuil de détection (bilan de liaison)"},
        {K::ATTRIBUTE,
         "ns3::YansWifiPhy",
         "CcaEdThreshold",
         "STEP 1",
         "seuil CCA (bilan de liaison)"},
        {K::ATTRIBUTE,
         "ns3::YansWifiPhy",
         "RxNoiseFigure",
         "STEP 1",
         "bruit récepteur (bilan de liaison)"},
        {K::TYPE_ID, "ns3::ConstantRateWifiManager", "", "STEP 1", "débit fixe DSSS"},
        {K::ATTRIBUTE,
         "ns3::ConstantRateWifiManager",
         "DataMode",
         "STEP 1",
         "DsssRate11Mbps (unicast)"},
        {K::ATTRIBUTE,
         "ns3::ConstantRateWifiManager",
         "ControlMode",
         "STEP 1",
         "débit des trames de contrôle (RTS/ACK)"},
        {K::ATTRIBUTE,
         "ns3::ConstantRateWifiManager",
         "NonUnicastMode",
         "STEP 1",
         "débit des broadcasts AODV (RREQ/HELLO/RERR) : décision D-06"},
        {K::TRACE_SOURCE,
         "ns3::WifiPhy",
         "MonitorSnifferTx",
         "STEP 1",
         "diagnostic : débit PHY réellement utilisé par type de trame"},

        // Propagation.
        {K::TYPE_ID,
         "ns3::LogDistancePropagationLossModel",
         "",
         "STEP 1",
         "profil radio principal"},
        {K::ATTRIBUTE,
         "ns3::LogDistancePropagationLossModel",
         "Exponent",
         "STEP 1",
         "pathLossExponent = 3.0"},
        {K::ATTRIBUTE,
         "ns3::LogDistancePropagationLossModel",
         "ReferenceDistance",
         "STEP 1",
         "distance de référence 1 m"},
        {K::ATTRIBUTE,
         "ns3::LogDistancePropagationLossModel",
         "ReferenceLoss",
         "STEP 1",
         "referenceLoss = 40.05 dB (défaut ns-3 différent)"},
        {K::TYPE_ID, "ns3::RangePropagationLossModel", "", "STEP 1", "contrôle positif"},
        {K::ATTRIBUTE,
         "ns3::RangePropagationLossModel",
         "MaxRange",
         "STEP 1",
         "maxRange du contrôle positif"},
        {K::TYPE_ID,
         "ns3::ConstantSpeedPropagationDelayModel",
         "",
         "STEP 1",
         "délai de propagation"},

        // Mobilité.
        {K::TYPE_ID, "ns3::RandomWaypointMobilityModel", "", "STEP 1", "mobilité RWP"},
        {K::ATTRIBUTE,
         "ns3::RandomWaypointMobilityModel",
         "Speed",
         "STEP 1",
         "U(minSpeed, maxSpeed)"},
        {K::ATTRIBUTE, "ns3::RandomWaypointMobilityModel", "Pause", "STEP 1", "pauseTime"},
        {K::ATTRIBUTE,
         "ns3::RandomWaypointMobilityModel",
         "PositionAllocator",
         "STEP 1",
         "allocateur des waypoints (obligatoire)"},
        {K::TYPE_ID,
         "ns3::RandomRectanglePositionAllocator",
         "",
         "STEP 1",
         "positions initiales et waypoints"},
        {K::ATTRIBUTE, "ns3::RandomRectanglePositionAllocator", "X", "STEP 1", "U(0, areaSize)"},
        {K::ATTRIBUTE, "ns3::RandomRectanglePositionAllocator", "Y", "STEP 1", "U(0, areaSize)"},
        {K::TYPE_ID,
         "ns3::UniformRandomVariable",
         "",
         "STEP 1",
         "tirage des flux et du start jitter (stream dédié)"},

        // Trafic UDP.
        {K::TYPE_ID, "ns3::OnOffApplication", "", "STEP 1", "source CBR UDP"},
        {K::ATTRIBUTE, "ns3::OnOffApplication", "DataRate", "STEP 1", "64 kbit/s"},
        {K::ATTRIBUTE, "ns3::OnOffApplication", "PacketSize", "STEP 1", "512 octets"},
        {K::ATTRIBUTE, "ns3::OnOffApplication", "OnTime", "STEP 1", "source toujours ON"},
        {K::ATTRIBUTE, "ns3::OnOffApplication", "OffTime", "STEP 1", "source toujours ON"},
        {K::ATTRIBUTE, "ns3::OnOffApplication", "Remote", "STEP 1", "adresse destination"},
        {K::ATTRIBUTE,
         "ns3::OnOffApplication",
         "EnableSeqTsSizeHeader",
         "STEP 2",
         "horodatage applicatif (délai, jitter)"},
        {K::TYPE_ID, "ns3::PacketSink", "", "STEP 1", "récepteur UDP"},
        {K::ATTRIBUTE, "ns3::PacketSink", "Local", "STEP 1", "adresse d'écoute"},
        {K::ATTRIBUTE,
         "ns3::PacketSink",
         "EnableSeqTsSizeHeader",
         "STEP 2",
         "horodatage applicatif (délai, jitter)"},

        // ------------------------------------------------------------ STEP 2
        {K::TRACE_SOURCE, "ns3::OnOffApplication", "Tx", "STEP 2", "appTxPackets, appTxBytes"},
        {K::TRACE_SOURCE,
         "ns3::OnOffApplication",
         "TxWithSeqTsSize",
         "STEP 2",
         "numéro de séquence et horodatage à l'émission"},
        {K::TRACE_SOURCE, "ns3::PacketSink", "Rx", "STEP 2", "appRxPackets, appRxBytes"},
        {K::TRACE_SOURCE,
         "ns3::PacketSink",
         "RxWithSeqTsSize",
         "STEP 2",
         "délai et jitter applicatifs par flux"},
        {K::TRACE_SOURCE,
         "ns3::Ipv4L3Protocol",
         "Tx",
         "STEP 2",
         "routingTxPackets/Bytes : émissions IP des paquets UDP/654 (AODV)"},
        {K::TRACE_SOURCE,
         "ns3::Ipv4L3Protocol",
         "Drop",
         "STEP 2",
         "diagnostic des pertes au niveau IP"},
        {K::TYPE_ID,
         "ns3::FlowMonitor",
         "",
         "STEP 2",
         "validation croisée des métriques applicatives"},

        // ------------------------------------------------------------ STEP 3
        {K::TYPE_ID, "ns3::energy::BasicEnergySource", "", "STEP 3", "source d'énergie"},
        {K::ATTRIBUTE,
         "ns3::energy::BasicEnergySource",
         "BasicEnergySourceInitialEnergyJ",
         "STEP 3",
         "initialEnergy_J (défaut ns-3 : voir D-09)"},
        {K::ATTRIBUTE,
         "ns3::energy::BasicEnergySource",
         "BasicEnergySupplyVoltageV",
         "STEP 3",
         "tension d'alimentation (E = V·I·t)"},
        {K::TRACE_SOURCE,
         "ns3::energy::BasicEnergySource",
         "RemainingEnergy",
         "STEP 3",
         "remainingEnergy_J"},
        {K::TYPE_ID, "ns3::WifiRadioEnergyModel", "", "STEP 3", "consommation radio Wi-Fi"},
        {K::ATTRIBUTE, "ns3::WifiRadioEnergyModel", "TxCurrentA", "STEP 3", "courant TX"},
        {K::ATTRIBUTE, "ns3::WifiRadioEnergyModel", "RxCurrentA", "STEP 3", "courant RX"},
        {K::ATTRIBUTE, "ns3::WifiRadioEnergyModel", "IdleCurrentA", "STEP 3", "courant IDLE"},
        {K::ATTRIBUTE,
         "ns3::WifiRadioEnergyModel",
         "CcaBusyCurrentA",
         "STEP 3",
         "courant CCA_BUSY"},
        {K::ATTRIBUTE,
         "ns3::WifiRadioEnergyModel",
         "SwitchingCurrentA",
         "STEP 3",
         "courant SWITCHING"},
        {K::ATTRIBUTE, "ns3::WifiRadioEnergyModel", "SleepCurrentA", "STEP 3", "courant SLEEP"},
        {K::TRACE_SOURCE,
         "ns3::WifiRadioEnergyModel",
         "TotalEnergyConsumption",
         "STEP 3",
         "consumedEnergy_J (contrôle croisé)"},
        {K::TRACE_SOURCE,
         "ns3::WifiPhyStateHelper",
         "State",
         "STEP 3",
         "durées par état radio -> décomposition tx/rx/idle/ccaBusy/..."},
    };
    return requirements;
}

// ---------------------------------------------------------------------------
// Vérification
// ---------------------------------------------------------------------------

namespace
{

/**
 * Convertit le niveau de support ns-3 en verdict du contrat.
 */
ApiStatus
FromSupportLevel(TypeId::SupportLevel level)
{
    switch (level)
    {
    case TypeId::SupportLevel::SUPPORTED:
        return ApiStatus::SUPPORTED;
    case TypeId::SupportLevel::DEPRECATED:
        return ApiStatus::DEPRECATED;
    case TypeId::SupportLevel::OBSOLETE:
        return ApiStatus::OBSOLETE;
    }
    return ApiStatus::MISSING; // inatteignable ; évite un avertissement du compilateur
}

/**
 * Recherche un attribut dans le TypeId et ses parents.
 *
 * La recherche remonte la hiérarchie car de nombreux attributs sont définis
 * dans une classe de base (ex. TxPowerStart dans ns3::WifiPhy pour
 * ns3::YansWifiPhy, Remote dans ns3::SourceApplication pour
 * ns3::OnOffApplication). C'est aussi le comportement de Config::Set.
 */
ApiCheckResult
CheckAttribute(TypeId tid, const ApiRequirement& requirement)
{
    ApiCheckResult result{requirement, ApiStatus::MISSING, "attribut introuvable"};
    TypeId current = tid;
    while (true)
    {
        for (std::size_t i = 0; i < current.GetAttributeN(); ++i)
        {
            const TypeId::AttributeInformation info = current.GetAttribute(i);
            if (info.name != requirement.member)
            {
                continue;
            }
            result.status = FromSupportLevel(info.supportLevel);
            // originalInitialValue = défaut codé dans ns-3, indépendant de tout
            // Config::SetDefault ou argument --ns3::... de la ligne de commande.
            std::string value = "<none>";
            if (info.originalInitialValue && info.checker)
            {
                value = info.originalInitialValue->SerializeToString(info.checker);
            }
            result.detail = "défaut=" + value + " (déclaré dans " + current.GetName() + ")";
            if (result.status != ApiStatus::SUPPORTED)
            {
                result.detail += " ; " + info.supportMsg;
            }
            return result;
        }
        const TypeId parent = current.GetParent();
        if (parent == current)
        {
            break; // racine de la hiérarchie atteinte
        }
        current = parent;
    }
    return result;
}

/**
 * Recherche une TraceSource dans le TypeId et ses parents, sans effet de
 * bord (TypeId::LookupTraceSourceByName écrirait sur std::cerr pour une
 * source dépréciée et appellerait NS_FATAL_ERROR pour une source obsolète).
 */
ApiCheckResult
CheckTraceSource(TypeId tid, const ApiRequirement& requirement)
{
    ApiCheckResult result{requirement, ApiStatus::MISSING, "TraceSource introuvable"};
    TypeId current = tid;
    while (true)
    {
        for (std::size_t i = 0; i < current.GetTraceSourceN(); ++i)
        {
            const TypeId::TraceSourceInformation info = current.GetTraceSource(i);
            if (info.name != requirement.member)
            {
                continue;
            }
            result.status = FromSupportLevel(info.supportLevel);
            // Nom de la signature déclarée : c'est lui qui fixe le prototype
            // du callback à écrire aux étapes suivantes.
            result.detail =
                "signature=" + info.callback + " (déclarée dans " + current.GetName() + ")";
            if (result.status != ApiStatus::SUPPORTED)
            {
                result.detail += " ; " + info.supportMsg;
            }
            return result;
        }
        const TypeId parent = current.GetParent();
        if (parent == current)
        {
            break;
        }
        current = parent;
    }
    return result;
}

} // namespace

ApiCheckResult
CheckApiRequirement(const ApiRequirement& requirement)
{
    TypeId tid;
    if (!TypeId::LookupByNameFailSafe(requirement.typeName, &tid))
    {
        return {requirement,
                ApiStatus::MISSING,
                "TypeId introuvable (nom erroné, module non compilé ou non chargé)"};
    }

    // Si le nom demandé n'est qu'un alias déprécié (AddDeprecatedName), ns-3
    // le résout vers le nom canonique : la baseline doit utiliser ce dernier.
    if (tid.GetName() != requirement.typeName)
    {
        return {requirement,
                ApiStatus::DEPRECATED,
                "alias déprécié ; nom canonique : " + tid.GetName()};
    }

    switch (requirement.kind)
    {
    case ApiItemKind::TYPE_ID:
        return {requirement, ApiStatus::SUPPORTED, "enregistré : " + tid.GetName()};
    case ApiItemKind::ATTRIBUTE:
        return CheckAttribute(tid, requirement);
    case ApiItemKind::TRACE_SOURCE:
        return CheckTraceSource(tid, requirement);
    }
    return {requirement, ApiStatus::MISSING, "nature d'exigence inconnue"};
}

std::vector<ApiCheckResult>
CheckBaselineApiContract()
{
    // L'ancrage doit précéder toute recherche par nom (voir l'en-tête).
    AnchorRequiredModules();

    std::vector<ApiCheckResult> results;
    const auto& requirements = GetBaselineApiRequirements();
    results.reserve(requirements.size());
    for (const auto& requirement : requirements)
    {
        results.push_back(CheckApiRequirement(requirement));
    }
    return results;
}

uint32_t
CountContractViolations(const std::vector<ApiCheckResult>& results)
{
    uint32_t violations = 0;
    for (const auto& result : results)
    {
        // Politique volontairement stricte : une API dépréciée est une
        // violation, car elle menace la reproductibilité à la version suivante.
        if (result.status != ApiStatus::SUPPORTED)
        {
            ++violations;
        }
    }
    return violations;
}

std::string
ToString(ApiItemKind kind)
{
    switch (kind)
    {
    case ApiItemKind::TYPE_ID:
        return "TYPE_ID";
    case ApiItemKind::ATTRIBUTE:
        return "ATTRIBUTE";
    case ApiItemKind::TRACE_SOURCE:
        return "TRACE_SOURCE";
    }
    return "UNKNOWN";
}

std::string
ToString(ApiStatus status)
{
    switch (status)
    {
    case ApiStatus::SUPPORTED:
        return "SUPPORTED";
    case ApiStatus::DEPRECATED:
        return "DEPRECATED";
    case ApiStatus::OBSOLETE:
        return "OBSOLETE";
    case ApiStatus::MISSING:
        return "MISSING";
    }
    return "UNKNOWN";
}

} // namespace fmanet
} // namespace ns3
