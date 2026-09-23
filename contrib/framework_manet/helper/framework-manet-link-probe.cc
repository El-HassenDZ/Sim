/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * framework_manet — STEP 1a. Implémentation de la sonde de liaison
 * (voir l'en-tête pour le rôle et les choix de conception).
 */

#include "framework-manet-link-probe.h"

#include "ns3/abort.h"
#include "ns3/constant-position-mobility-model.h"
#include "ns3/node-container.h"
#include "ns3/packet-socket-address.h"
#include "ns3/packet-socket-client.h"
#include "ns3/packet-socket-helper.h"
#include "ns3/packet-socket-server.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"
#include "ns3/wifi-mac-header.h"
#include "ns3/wifi-net-device.h"
#include "ns3/wifi-phy.h"

#include <limits>
#include <set>
#include <sstream>

namespace ns3
{
namespace fmanet
{

namespace
{

/// EtherType des trames de sonde : 0x88B5, « IEEE 802 Local Experimental
/// EtherType 1 ». Il ne peut être confondu ni avec IPv4 (0x0800) ni avec
/// ARP (0x0806), et aucune pile IP n'est installée de toute façon.
constexpr uint16_t PROBE_PROTOCOL = 0x88B5;

/// Instant de démarrage de l'émetteur [s]. Le récepteur démarre à 0 s, donc
/// avant la première trame.
constexpr double CLIENT_START_S = 0.1;

/// Marge après la dernière trame, pour laisser se terminer les
/// retransmissions en cours (≫ 36 ms, pire cas en 802.11b) [s].
constexpr double DRAIN_S = 1.0;

/**
 * Compteurs d'une mesure. Ils sont alimentés par des callbacks de trace liés
 * par pointeur (MakeBoundCallback) : la structure doit vivre jusqu'à la fin
 * de Simulator::Run().
 */
struct ProbeCounters
{
    uint32_t offered = 0;            //!< PacketSocketClient/Tx
    uint32_t delivered = 0;          //!< PacketSocketServer/Rx
    uint32_t senderDataTx = 0;       //!< MonitorSnifferTx, émetteur, trames de données
    uint32_t receiverAckTx = 0;      //!< MonitorSnifferTx, récepteur, ACK
    std::set<std::string> dataModes; //!< débits observés des trames de données
    std::set<std::string> ackModes;  //!< débits observés des ACK
    double signalSumDbm = 0.0;       //!< Σ signal des données reçues (MonitorSnifferRx)
    double noiseSumDbm = 0.0;        //!< Σ bruit associé
    uint32_t sniffedDataRx = 0;      //!< nombre de termes des deux sommes
};

/// Trace "Tx" de PacketSocketClient (ns3::Packet::AddressTracedCallback).
void
OnClientTx(ProbeCounters* counters, Ptr<const Packet> /* packet */, const Address& /* to */)
{
    ++counters->offered;
}

/// Trace "Rx" de PacketSocketServer (ns3::Packet::AddressTracedCallback).
void
OnServerRx(ProbeCounters* counters, Ptr<const Packet> /* packet */, const Address& /* from */)
{
    ++counters->delivered;
}

/**
 * Trace "MonitorSnifferTx" du PHY émetteur. Le paquet est la MPDU complète
 * (en-tête MAC inclus) : l'en-tête permet de ne compter que les trames de
 * données, retransmissions comprises.
 */
void
OnSenderSnifferTx(ProbeCounters* counters,
                  Ptr<const Packet> packet,
                  uint16_t /* channelFreqMhz */,
                  WifiTxVector txVector,
                  MpduInfo /* aMpdu */,
                  uint16_t /* staId */)
{
    WifiMacHeader header;
    packet->PeekHeader(header);
    if (header.IsData())
    {
        ++counters->senderDataTx;
        counters->dataModes.insert(txVector.GetMode().GetUniqueName());
    }
}

/// Trace "MonitorSnifferTx" du PHY récepteur : seuls les ACK sont comptés.
void
OnReceiverSnifferTx(ProbeCounters* counters,
                    Ptr<const Packet> packet,
                    uint16_t /* channelFreqMhz */,
                    WifiTxVector txVector,
                    MpduInfo /* aMpdu */,
                    uint16_t /* staId */)
{
    WifiMacHeader header;
    packet->PeekHeader(header);
    if (header.IsAck())
    {
        ++counters->receiverAckTx;
        counters->ackModes.insert(txVector.GetMode().GetUniqueName());
    }
}

/**
 * Trace "MonitorSnifferRx" du PHY récepteur. ns-3 ne la déclenche que pour
 * une réception réussie (phy-entity.cc). Le signal sert de contrôle croisé
 * avec la puissance prédite par le modèle de propagation.
 *
 * La moyenne est faite en dBm : c'est licite ici parce que la propagation est
 * déterministe et que les deux nœuds sont immobiles (toutes les valeurs sont
 * égales). Avec de l'évanouissement, il faudrait moyenner en mW.
 */
void
OnReceiverSnifferRx(ProbeCounters* counters,
                    Ptr<const Packet> packet,
                    uint16_t /* channelFreqMhz */,
                    WifiTxVector /* txVector */,
                    MpduInfo /* aMpdu */,
                    SignalNoiseDbm signalNoise,
                    uint16_t /* staId */)
{
    WifiMacHeader header;
    packet->PeekHeader(header);
    if (header.IsData())
    {
        counters->signalSumDbm += signalNoise.signal;
        counters->noiseSumDbm += signalNoise.noise;
        ++counters->sniffedDataRx;
    }
}

/**
 * @param modes ensemble de noms de modes
 * @return les noms joints par "|", ou "-" si l'ensemble est vide
 */
std::string
JoinModes(const std::set<std::string>& modes)
{
    if (modes.empty())
    {
        return "-";
    }
    std::ostringstream os;
    bool first = true;
    for (const auto& mode : modes)
    {
        os << (first ? "" : "|") << mode;
        first = false;
    }
    return os.str();
}

} // namespace

std::string
ToString(FrameClass frameClass)
{
    switch (frameClass)
    {
    case FrameClass::BROADCAST:
        return "broadcast";
    case FrameClass::UNICAST:
        return "unicast";
    }
    return "unknown";
}

LinkProbeResult
RunLinkProbe(const RadioConfig& radioConfig, const LinkProbeConfig& probe)
{
    NS_ABORT_MSG_IF(!(probe.distanceM >= 0.0), "distance négative ou NaN");
    NS_ABORT_MSG_IF(probe.frames == 0, "frames doit être >= 1");
    NS_ABORT_MSG_IF(!probe.interval.IsStrictlyPositive(), "interval doit être > 0");

    // --- Nœuds et positions fixes : émetteur en (0,0,0), récepteur en (d,0,0).
    NodeContainer nodes;
    nodes.Create(2);
    Ptr<ConstantPositionMobilityModel> senderMobility =
        CreateObject<ConstantPositionMobilityModel>();
    Ptr<ConstantPositionMobilityModel> receiverMobility =
        CreateObject<ConstantPositionMobilityModel>();
    senderMobility->SetPosition(Vector(0.0, 0.0, 0.0));
    receiverMobility->SetPosition(Vector(probe.distanceM, 0.0, 0.0));
    nodes.Get(0)->AggregateObject(senderMobility);
    nodes.Get(1)->AggregateObject(receiverMobility);

    // --- Radio de la baseline (même helper que le scénario STEP 1b).
    RadioHelper radio(radioConfig);
    NetDeviceContainer devices = radio.Install(nodes);
    const int64_t streamsUsed = RadioHelper::AssignStreams(devices, WIFI_STREAM_BLOCK_START);
    NS_ABORT_MSG_IF(streamsUsed >= WIFI_STREAM_BLOCK_SIZE,
                    "le bloc de streams Wi-Fi (D-07) déborde : " << streamsUsed);

    // --- Applications de niveau 2.
    PacketSocketHelper packetSocket;
    packetSocket.Install(nodes);

    PacketSocketAddress remote;
    remote.SetSingleDevice(devices.Get(0)->GetIfIndex());
    remote.SetPhysicalAddress(probe.frameClass == FrameClass::UNICAST
                                  ? devices.Get(1)->GetAddress()
                                  : devices.Get(0)->GetBroadcast());
    remote.SetProtocol(PROBE_PROTOCOL);

    Ptr<PacketSocketClient> client = CreateObject<PacketSocketClient>();
    client->SetRemote(remote);
    client->SetAttribute("MaxPackets", UintegerValue(probe.frames));
    client->SetAttribute("Interval", TimeValue(probe.interval));
    client->SetAttribute("PacketSize", UintegerValue(probe.payloadBytes));
    nodes.Get(0)->AddApplication(client);

    PacketSocketAddress local;
    local.SetSingleDevice(devices.Get(1)->GetIfIndex());
    local.SetPhysicalAddress(devices.Get(1)->GetAddress());
    local.SetProtocol(PROBE_PROTOCOL);
    Ptr<PacketSocketServer> server = CreateObject<PacketSocketServer>();
    server->SetLocal(local);
    nodes.Get(1)->AddApplication(server);

    // --- Instrumentation.
    ProbeCounters counters;
    client->TraceConnectWithoutContext("Tx", MakeBoundCallback(&OnClientTx, &counters));
    server->TraceConnectWithoutContext("Rx", MakeBoundCallback(&OnServerRx, &counters));
    Ptr<WifiPhy> senderPhy = DynamicCast<WifiNetDevice>(devices.Get(0))->GetPhy();
    Ptr<WifiPhy> receiverPhy = DynamicCast<WifiNetDevice>(devices.Get(1))->GetPhy();
    senderPhy->TraceConnectWithoutContext("MonitorSnifferTx",
                                          MakeBoundCallback(&OnSenderSnifferTx, &counters));
    receiverPhy->TraceConnectWithoutContext("MonitorSnifferTx",
                                            MakeBoundCallback(&OnReceiverSnifferTx, &counters));
    receiverPhy->TraceConnectWithoutContext("MonitorSnifferRx",
                                            MakeBoundCallback(&OnReceiverSnifferRx, &counters));

    // --- Calendrier : dernière trame à start + (N−1)·interval, puis drain.
    const Time clientStart = Seconds(CLIENT_START_S);
    const Time stopTime =
        clientStart + probe.interval * static_cast<int64_t>(probe.frames) + Seconds(DRAIN_S);
    server->SetStartTime(Seconds(0));
    client->SetStartTime(clientStart);
    client->SetStopTime(stopTime);
    server->SetStopTime(stopTime);

    LinkProbeResult result;
    result.distanceM = probe.distanceM;
    result.predictedRxDbm = radio.PredictRxPowerDbm(senderMobility, receiverMobility);
    result.wifiStreamsUsed = streamsUsed;

    Simulator::Stop(stopTime);
    Simulator::Run();

    result.offered = counters.offered;
    result.delivered = counters.delivered;
    result.senderDataTx = counters.senderDataTx;
    result.receiverAckTx = counters.receiverAckTx;
    result.dataMode = JoinModes(counters.dataModes);
    result.ackMode = JoinModes(counters.ackModes);
    if (counters.sniffedDataRx > 0)
    {
        result.meanRxSignalDbm = counters.signalSumDbm / counters.sniffedDataRx;
        result.meanRxNoiseDbm = counters.noiseSumDbm / counters.sniffedDataRx;
    }
    else
    {
        // Aucune trame reçue : moyenne indéfinie, et non 0 dBm.
        result.meanRxSignalDbm = std::numeric_limits<double>::quiet_NaN();
        result.meanRxNoiseDbm = std::numeric_limits<double>::quiet_NaN();
    }

    Simulator::Destroy();
    return result;
}

} // namespace fmanet
} // namespace ns3
