/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * framework_manet — STEP 1a. Implémentation de RadioHelper (voir l'en-tête).
 */

#include "framework-manet-radio-helper.h"

#include "ns3/abort.h"
#include "ns3/constant-rate-wifi-manager.h"
#include "ns3/double.h"
#include "ns3/pointer.h"
#include "ns3/preamble-detection-model.h"
#include "ns3/propagation-delay-model.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
#include "ns3/wifi-helper.h"
#include "ns3/wifi-mac-helper.h"
#include "ns3/wifi-net-device.h"
#include "ns3/wifi-phy.h"
#include "ns3/wifi-remote-station-manager.h"
#include "ns3/yans-wifi-helper.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>

namespace ns3
{
namespace fmanet
{

const char* const NON_UNICAST_MODE_NS3_DEFAULT = "default";

namespace
{

/// Modes du PHY 802.11b (clauses 15 et 16), seuls admis par la baseline.
/// Tout autre nom (ex. OfdmRate6Mbps, défaut de ConstantRateWifiManager) est
/// refusé par ValidateRadioConfig plutôt que de provoquer une erreur fatale
/// de ns-3 à l'installation.
const std::array<const char*, 4> DSSS_MODES = {"DsssRate1Mbps",
                                               "DsssRate2Mbps",
                                               "DsssRate5_5Mbps",
                                               "DsssRate11Mbps"};

/**
 * @param name nom d'un WifiMode
 * @return true si c'est un mode DSSS/HR-DSSS de la liste DSSS_MODES
 */
bool
IsDsssMode(const std::string& name)
{
    return std::any_of(DSSS_MODES.begin(), DSSS_MODES.end(), [&name](const char* mode) {
        return name == mode;
    });
}

/**
 * Lit un attribut quelconque sous forme de texte. ObjectBase::GetAttribute
 * accepte un StringValue pour tout type d'attribut (sérialisation par le
 * checker), ce qui évite de connaître le type exact de chaque attribut.
 *
 * Certains attributs ns-3 n'ont qu'un accesseur d'écriture (ex. RxNoiseFigure
 * de WifiPhy en ns-3.48) : les lire provoquerait NS_FATAL. Ils sont donc
 * signalés comme non lisibles au lieu d'être lus.
 *
 * @param object objet ns-3
 * @param name nom de l'attribut
 * @return la valeur sérialisée, "<absent>" ou "<non lisible>"
 */
std::string
ReadAttribute(Ptr<const Object> object, const std::string& name)
{
    TypeId::AttributeInformation info;
    if (!object->GetInstanceTypeId().LookupAttributeByName(name, &info, true))
    {
        return "<absent>";
    }
    if ((info.flags & TypeId::ATTR_GET) == 0 || !info.accessor->HasGetter())
    {
        return "<non lisible>";
    }
    StringValue value;
    object->GetAttribute(name, value);
    return value.Get();
}

/**
 * Liste les attributs lisibles déclarés par le TypeId *effectif* d'un objet
 * (sans ses parents), sous la forme "Nom=valeur, ...".
 *
 * @param object objet ns-3 (modèle de propagation, détecteur de préambule)
 * @return la liste formatée
 */
std::string
DescribeOwnAttributes(Ptr<const Object> object)
{
    const TypeId tid = object->GetInstanceTypeId();
    std::ostringstream os;
    bool first = true;
    for (std::size_t i = 0; i < tid.GetAttributeN(); ++i)
    {
        const TypeId::AttributeInformation info = tid.GetAttribute(i);
        if ((info.flags & TypeId::ATTR_GET) == 0 || !info.accessor->HasGetter())
        {
            continue; // attribut non lisible : rien à rapporter
        }
        os << (first ? "" : ", ") << info.name << "=" << ReadAttribute(object, info.name);
        first = false;
    }
    return os.str();
}

} // namespace

std::string
ToString(PropagationProfile profile)
{
    switch (profile)
    {
    case PropagationProfile::LOG_DISTANCE:
        return "logdistance";
    case PropagationProfile::RANGE:
        return "range";
    }
    return "unknown";
}

bool
ParsePropagationProfile(const std::string& name, PropagationProfile* profile)
{
    if (name == "logdistance")
    {
        *profile = PropagationProfile::LOG_DISTANCE;
        return true;
    }
    if (name == "range")
    {
        *profile = PropagationProfile::RANGE;
        return true;
    }
    return false;
}

std::string
ValidateRadioConfig(const RadioConfig& c)
{
    std::ostringstream errors;
    // Accumule toutes les erreurs au lieu de s'arrêter à la première : un
    // seul essai suffit à l'utilisateur pour corriger sa ligne de commande.
    auto fail = [&errors](const std::string& message) {
        errors << (errors.tellp() > 0 ? "; " : "") << message;
    };

    if (!std::isfinite(c.pathLossExponent) || c.pathLossExponent <= 0.0)
    {
        fail("pathLossExponent doit être fini et > 0");
    }
    if (!std::isfinite(c.referenceLossDb))
    {
        fail("referenceLoss doit être fini");
    }
    if (!std::isfinite(c.referenceDistanceM) || c.referenceDistanceM <= 0.0)
    {
        fail("referenceDistance doit être fini et > 0");
    }
    if (!std::isfinite(c.maxRangeM) || c.maxRangeM <= 0.0)
    {
        fail("maxRange doit être fini et > 0");
    }
    if (!std::isfinite(c.txPowerDbm))
    {
        fail("txPower doit être fini");
    }
    if (!IsDsssMode(c.dataMode))
    {
        fail("dataMode '" + c.dataMode + "' n'est pas un mode 802.11b");
    }
    if (!IsDsssMode(c.controlMode))
    {
        fail("controlMode '" + c.controlMode + "' n'est pas un mode 802.11b");
    }
    if (c.nonUnicastMode != NON_UNICAST_MODE_NS3_DEFAULT && !IsDsssMode(c.nonUnicastMode))
    {
        fail("nonUnicastMode '" + c.nonUnicastMode + "' n'est ni 'default' ni un mode 802.11b");
    }
    if (!std::isfinite(c.preambleMinRssiDbm) || !std::isfinite(c.preambleThresholdDb))
    {
        fail("les seuils du détecteur de préambule doivent être finis");
    }
    return errors.str();
}

std::string
DescribeRadioConfig(const RadioConfig& c)
{
    std::ostringstream os;
    os << "profile=" << ToString(c.propagation);
    if (c.propagation == PropagationProfile::LOG_DISTANCE)
    {
        os << " exponent=" << c.pathLossExponent << " refLoss=" << c.referenceLossDb
           << "dB refDist=" << c.referenceDistanceM << "m";
    }
    else
    {
        os << " maxRange=" << c.maxRangeM << "m";
    }
    os << " txPower=" << c.txPowerDbm << "dBm data=" << c.dataMode << " control=" << c.controlMode
       << " nonUnicast=" << c.nonUnicastMode << " preambleDetection=";
    if (c.preambleDetection)
    {
        os << "on(minRssi=" << c.preambleMinRssiDbm << "dBm,threshold=" << c.preambleThresholdDb
           << "dB)";
    }
    else
    {
        os << "off";
    }
    return os.str();
}

std::string
DescribeInstalledRadio(Ptr<NetDevice> device, Ptr<PropagationLossModel> lossModel)
{
    Ptr<WifiNetDevice> wifiDevice = DynamicCast<WifiNetDevice>(device);
    NS_ABORT_MSG_IF(!wifiDevice, "DescribeInstalledRadio attend un WifiNetDevice");
    Ptr<WifiPhy> phy = wifiDevice->GetPhy();
    Ptr<WifiRemoteStationManager> manager = wifiDevice->GetRemoteStationManager();

    std::ostringstream os;
    os << "  standard           : " << phy->GetStandard() << "\n"
       << "  frequency_MHz      : " << phy->GetFrequency() << "\n"
       << "  channelWidth_MHz   : " << phy->GetChannelWidth() << "\n"
       << "  TxPowerStart_dBm   : " << ReadAttribute(phy, "TxPowerStart") << "\n"
       << "  TxPowerEnd_dBm     : " << ReadAttribute(phy, "TxPowerEnd") << "\n"
       << "  RxSensitivity_dBm  : " << ReadAttribute(phy, "RxSensitivity") << "\n"
       << "  CcaEdThreshold_dBm : " << ReadAttribute(phy, "CcaEdThreshold") << "\n"
       << "  RxNoiseFigure_dB   : " << ReadAttribute(phy, "RxNoiseFigure")
       << "  (attribut en écriture seule ; bruit effectif : meanRxNoise_dBm)\n";

    // Le détecteur de préambule est un attribut pointeur du PHY : nul si
    // DisablePreambleDetectionModel() a été appelé.
    PointerValue pointer;
    phy->GetAttribute("PreambleDetectionModel", pointer);
    Ptr<PreambleDetectionModel> preamble = pointer.Get<PreambleDetectionModel>();
    os << "  preambleDetection  : ";
    if (preamble)
    {
        os << preamble->GetInstanceTypeId().GetName() << " (" << DescribeOwnAttributes(preamble)
           << ")\n";
    }
    else
    {
        os << "none\n";
    }

    os << "  stationManager     : " << manager->GetInstanceTypeId().GetName() << "\n"
       << "  DataMode           : " << ReadAttribute(manager, "DataMode") << "\n"
       << "  ControlMode        : " << ReadAttribute(manager, "ControlMode") << "\n"
       << "  NonUnicastMode     : " << ReadAttribute(manager, "NonUnicastMode")
       << "  (Invalid-WifiMode = non fixé ; débit réel mesuré par la calibration)\n"
       << "  lossModel          : " << lossModel->GetInstanceTypeId().GetName() << " ("
       << DescribeOwnAttributes(lossModel) << ")\n";
    return os.str();
}

RadioHelper::RadioHelper(const RadioConfig& config)
    : m_config(config)
{
    const std::string errors = ValidateRadioConfig(config);
    NS_ABORT_MSG_IF(!errors.empty(), "RadioConfig invalide : " << errors);

    // Canal construit explicitement (et non par YansWifiChannelHelper) pour
    // conserver un pointeur sur le modèle de propagation : la calibration
    // compare la puissance prédite par ce modèle à la puissance réellement
    // observée par le PHY.
    m_channel = CreateObject<YansWifiChannel>();
    m_channel->SetPropagationDelayModel(CreateObject<ConstantSpeedPropagationDelayModel>());

    if (config.propagation == PropagationProfile::LOG_DISTANCE)
    {
        // L(d) = L0 + 10·n·log10(d/d0) ; les trois paramètres sont fixés
        // explicitement car le défaut ns-3 de L0 est 46.6777 dB (F-03, D-17).
        m_lossModel = CreateObject<LogDistancePropagationLossModel>();
        m_lossModel->SetAttribute("Exponent", DoubleValue(config.pathLossExponent));
        m_lossModel->SetAttribute("ReferenceDistance", DoubleValue(config.referenceDistanceM));
        m_lossModel->SetAttribute("ReferenceLoss", DoubleValue(config.referenceLossDb));
    }
    else
    {
        // Disque idéal : Prx = Ptx si d ≤ MaxRange, −1000 dBm sinon (F-04).
        m_lossModel = CreateObject<RangePropagationLossModel>();
        m_lossModel->SetAttribute("MaxRange", DoubleValue(config.maxRangeM));
    }
    m_channel->SetPropagationLossModel(m_lossModel);
}

NetDeviceContainer
RadioHelper::Install(const NodeContainer& nodes) const
{
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);

    // NonUnicastMode n'est fixé que sur demande explicite : la valeur
    // "default" reproduit exactement le comportement ns-3 (D-06 ouvert).
    if (m_config.nonUnicastMode == NON_UNICAST_MODE_NS3_DEFAULT)
    {
        wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                     "DataMode",
                                     StringValue(m_config.dataMode),
                                     "ControlMode",
                                     StringValue(m_config.controlMode));
    }
    else
    {
        wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                     "DataMode",
                                     StringValue(m_config.dataMode),
                                     "ControlMode",
                                     StringValue(m_config.controlMode),
                                     "NonUnicastMode",
                                     StringValue(m_config.nonUnicastMode));
    }

    YansWifiPhyHelper phy;
    phy.SetChannel(m_channel);
    // Un seul niveau de puissance, égal à txPower (défaut ns-3 : 16.0206 dBm).
    phy.Set("TxPowerStart", DoubleValue(m_config.txPowerDbm));
    phy.Set("TxPowerEnd", DoubleValue(m_config.txPowerDbm));
    phy.Set("TxPowerLevels", UintegerValue(1));

    if (m_config.preambleDetection)
    {
        // Mêmes type et paramètres que le défaut de WifiPhyHelper (F-01),
        // mais explicites, pour que D-05 option C ne soit qu'un changement de
        // valeur tracé dans la configuration.
        phy.SetPreambleDetectionModel("ns3::ThresholdPreambleDetectionModel",
                                      "MinimumRssi",
                                      DoubleValue(m_config.preambleMinRssiDbm),
                                      "Threshold",
                                      DoubleValue(m_config.preambleThresholdDb));
    }
    else
    {
        phy.DisablePreambleDetectionModel();
    }

    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    return wifi.Install(phy, mac, nodes);
}

int64_t
RadioHelper::AssignStreams(NetDeviceContainer devices, int64_t stream)
{
    return WifiHelper::AssignStreams(devices, stream);
}

Ptr<PropagationLossModel>
RadioHelper::GetLossModel() const
{
    return m_lossModel;
}

double
RadioHelper::PredictRxPowerDbm(Ptr<MobilityModel> a, Ptr<MobilityModel> b) const
{
    return m_lossModel->CalcRxPower(m_config.txPowerDbm, a, b);
}

const RadioConfig&
RadioHelper::GetConfig() const
{
    return m_config;
}

} // namespace fmanet
} // namespace ns3
