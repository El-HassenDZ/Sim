/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "metrics-collector.h"

#include "cbr-traffic-applications.h"

#include "ns3/ipv4-header.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/udp-header.h"
#include "ns3/udp-l4-protocol.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <stdexcept>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("MtcAodvMetricsCollector");

namespace mtcaodv
{

namespace
{

/// Port UDP du plan de contrôle AODV (Annexe C, constante AODV_PORT).
constexpr uint16_t AODV_CONTROL_PORT = 654;
/// Valeur du champ « type » d'un RREQ dans le format AODV standard (RFC 3561).
constexpr uint8_t AODV_MESSAGE_TYPE_RREQ = 1;
/// Octets d'en-tête ajoutés à la charge utile applicative sur le chemin réseau.
constexpr uint32_t IPV4_HEADER_BYTES = 20;
constexpr uint32_t UDP_HEADER_BYTES = 8;

/// Écrit une métrique, ou « NaN » si elle n'est pas applicable (D-22, invariant 20.4.6).
std::string
FormatMetric(const MetricValue& value)
{
    if (!value.has_value() || !std::isfinite(*value))
    {
        return "NaN";
    }
    std::ostringstream stream;
    stream.precision(9);
    stream << std::defaultfloat << *value;
    return stream.str();
}

} // namespace

NS_OBJECT_ENSURE_REGISTERED(MetricsCollector);

TypeId
MetricsCollector::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::mtcaodv::MetricsCollector").SetParent<Object>().SetGroupName("MtcAodv");
    return tid;
}

MetricsCollector::MetricsCollector()
    : m_windowStart(Seconds(0)),
      m_windowEnd(Time::Max()),
      m_applicationTxPackets(0),
      m_applicationRxPackets(0),
      m_applicationTxPayloadBytes(0),
      m_applicationRxPayloadBytes(0),
      m_aodvControlTransmissions(0),
      m_routeDiscoveries(0),
      m_forgedReplies(0),
      m_blackholeTransitDrops(0),
      m_energyConnected(false)
{
}

MetricsCollector::~MetricsCollector()
{
}

void
MetricsCollector::SetEvaluationWindow(Time start, Time end)
{
    if (end <= start)
    {
        throw std::invalid_argument("MetricsCollector : fenêtre d'évaluation vide ou inversée");
    }
    m_windowStart = start;
    m_windowEnd = end;
}

bool
MetricsCollector::InsideWindow(Time instant) const
{
    return instant >= m_windowStart && instant <= m_windowEnd;
}

void
MetricsCollector::ConnectTrafficSources(const ApplicationContainer& sources)
{
    for (uint32_t i = 0; i < sources.GetN(); ++i)
    {
        Ptr<CbrTrafficSource> source = DynamicCast<CbrTrafficSource>(sources.Get(i));
        NS_ASSERT_MSG(source, "MetricsCollector attend des CbrTrafficSource");
        source->TraceConnectWithoutContext(
            "Tx",
            MakeCallback(&MetricsCollector::RecordApplicationTx, this));
    }
}

void
MetricsCollector::ConnectTrafficSinks(const ApplicationContainer& sinks)
{
    for (uint32_t i = 0; i < sinks.GetN(); ++i)
    {
        Ptr<CbrTrafficSink> sink = DynamicCast<CbrTrafficSink>(sinks.Get(i));
        NS_ASSERT_MSG(sink, "MetricsCollector attend des CbrTrafficSink");
        sink->TraceConnectWithoutContext("Rx",
                                         MakeCallback(&MetricsCollector::RecordApplicationRx, this));
    }
}

void
MetricsCollector::ConnectRoutingOverhead(const NodeContainer& nodes)
{
    // La trace Tx d'Ipv4L3Protocol se déclenche à chaque transmission sortante, y
    // compris lors d'un relais. C'est exactement le comptage « hop par hop » demandé par
    // l'Éq. (28) : un RREQ inondé sur k nœuds compte pour k transmissions.
    for (uint32_t i = 0; i < nodes.GetN(); ++i)
    {
        Ptr<Ipv4L3Protocol> layer = nodes.Get(i)->GetObject<Ipv4L3Protocol>();
        NS_ASSERT_MSG(layer, "pile IPv4 absente sur le nœud " << nodes.Get(i)->GetId());
        layer->TraceConnectWithoutContext(
            "Tx",
            MakeCallback(&MetricsCollector::RecordIpv4Transmission, this));
    }
}

void
MetricsCollector::ConnectEnergySources(const energy::EnergySourceContainer& sources)
{
    m_energySources = sources;
    m_energyConnected = true;
}

void
MetricsCollector::AddAttackCounters(uint64_t forgedReplies, uint64_t transitDrops)
{
    m_forgedReplies += forgedReplies;
    m_blackholeTransitDrops += transitDrops;
}

void
MetricsCollector::RecordApplicationTx(uint16_t flowId, uint32_t, uint32_t payloadBytes)
{
    if (!InsideWindow(Simulator::Now()))
    {
        return;
    }
    ++m_applicationTxPackets;
    m_applicationTxPayloadBytes += payloadBytes;
    ++m_flowCounters[flowId].txPackets;
}

void
MetricsCollector::RecordApplicationRx(uint16_t flowId, uint32_t, Time delay, uint32_t payloadBytes)
{
    // Un paquet est retenu si son *émission* appartient à la fenêtre : le numérateur de
    // l'Éq. (20) reste ainsi un sous-ensemble de son dénominateur, même pour un paquet
    // arrivé après la fin de la fenêtre. Le trafic s'arrête avant la fin de simulation
    // pour laisser les paquets en vol se vider, ce qui évite un biais systématique.
    const Time sendTime = Simulator::Now() - delay;
    if (!InsideWindow(sendTime))
    {
        return;
    }
    ++m_applicationRxPackets;
    m_applicationRxPayloadBytes += payloadBytes;
    m_delaysSeconds.push_back(delay.GetSeconds());
    ++m_flowCounters[flowId].rxPackets;
}

void
MetricsCollector::RecordIpv4Transmission(Ptr<const Packet> packet, Ptr<Ipv4>, uint32_t)
{
    if (!InsideWindow(Simulator::Now()))
    {
        return;
    }

    // Le paquet fourni par la trace porte son en-tête IPv4 (Ipv4L3Protocol::CallTxTrace).
    // On travaille sur une copie : la trace donne un const et le retrait des en-têtes est
    // destructif.
    Ptr<Packet> copy = packet->Copy();

    Ipv4Header ipHeader;
    if (copy->RemoveHeader(ipHeader) == 0 || ipHeader.GetProtocol() != UdpL4Protocol::PROT_NUMBER)
    {
        return;
    }

    UdpHeader udpHeader;
    if (copy->RemoveHeader(udpHeader) == 0 || udpHeader.GetDestinationPort() != AODV_CONTROL_PORT)
    {
        return;
    }

    ++m_aodvControlTransmissions;

    // Le premier octet de la charge utile AODV est le champ « type » (TypeHeader).
    // On identifie une découverte de route en comptant les RREQ dont l'adresse
    // d'origine est celle de l'émetteur : les relais d'inondation ne sont pas des
    // découvertes nouvelles. Le décodage est fait au niveau du format standard
    // (RFC 3561), et non via les classes d'un module particulier, afin que le comptage
    // vaille identiquement pour AODV stock et pour le fork.
    if (copy->GetSize() < 24)
    {
        return;
    }

    uint8_t buffer[24];
    copy->CopyData(buffer, sizeof(buffer));
    if (buffer[0] != AODV_MESSAGE_TYPE_RREQ)
    {
        return;
    }

    // Disposition RREQ : type(1) flags(1) reserved(1) hopCount(1) rreqId(4)
    //                    dst(4) dstSeq(4) origin(4) originSeq(4)
    const uint32_t originRaw = (static_cast<uint32_t>(buffer[16]) << 24) |
                               (static_cast<uint32_t>(buffer[17]) << 16) |
                               (static_cast<uint32_t>(buffer[18]) << 8) |
                               static_cast<uint32_t>(buffer[19]);
    if (Ipv4Address(originRaw) == ipHeader.GetSource())
    {
        ++m_routeDiscoveries;
    }
}

const std::map<uint16_t, FlowCounters>&
MetricsCollector::GetFlowCounters() const
{
    return m_flowCounters;
}

MetricsReport
MetricsCollector::ComputeReport() const
{
    MetricsReport report;

    report.applicationTxPackets = m_applicationTxPackets;
    report.applicationRxPackets = m_applicationRxPackets;
    report.applicationTxPayloadBytes = m_applicationTxPayloadBytes;
    report.applicationRxPayloadBytes = m_applicationRxPayloadBytes;
    report.aodvControlTransmissions = m_aodvControlTransmissions;
    report.routeDiscoveries = m_routeDiscoveries;
    report.forgedReplies = m_forgedReplies;
    report.blackholeTransitDrops = m_blackholeTransitDrops;

    // Périmètre de comptage préenregistré pour B_network^rx (Éq. 25) : octets des
    // datagrammes IPv4 porteurs de données applicatives effectivement livrés à leur
    // destination finale, en-têtes IPv4 et UDP compris. Ce périmètre est déclaré ici et
    // ne doit plus changer entre variantes, sans quoi les débits ne seraient pas
    // comparables.
    report.deliveredNetworkBytes =
        m_applicationRxPayloadBytes + m_applicationRxPackets * (IPV4_HEADER_BYTES + UDP_HEADER_BYTES);

    const double windowSeconds = (m_windowEnd - m_windowStart).GetSeconds();
    report.evaluationWindowSeconds = windowSeconds;

    // Éq. (20) et (24). Sans paquet émis, l'exécution est invalide pour le PDR (§21) :
    // on ne fabrique pas un ratio.
    if (m_applicationTxPackets > 0)
    {
        const double pdr =
            static_cast<double>(m_applicationRxPackets) / static_cast<double>(m_applicationTxPackets);
        report.packetDeliveryRatio = pdr;
        report.packetLossRatio = 1.0 - pdr;
    }

    // Éq. (25). La fenêtre est strictement positive par construction de
    // SetEvaluationWindow, mais on reste défensif.
    if (windowSeconds > 0.0)
    {
        report.throughputBitsPerSecond = 8.0 * static_cast<double>(report.deliveredNetworkBytes) / windowSeconds;
        report.goodputBitsPerSecond = 8.0 * static_cast<double>(m_applicationRxPayloadBytes) / windowSeconds;
        report.routeDiscoveryFrequency = static_cast<double>(m_routeDiscoveries) / windowSeconds;
    }

    // Éq. (26) et quantiles.
    if (!m_delaysSeconds.empty())
    {
        const double sum = std::accumulate(m_delaysSeconds.begin(), m_delaysSeconds.end(), 0.0);
        report.meanEndToEndDelay = sum / static_cast<double>(m_delaysSeconds.size());

        std::vector<double> sorted = m_delaysSeconds;
        std::sort(sorted.begin(), sorted.end());
        const size_t middle = sorted.size() / 2;
        report.medianEndToEndDelay =
            (sorted.size() % 2 == 0) ? 0.5 * (sorted[middle - 1] + sorted[middle]) : sorted[middle];
    }

    // Éq. (27) : variation absolue des délais successifs, dans l'ordre de réception.
    // Moins de deux paquets reçus rend le jitter indéfini, pas nul.
    if (m_delaysSeconds.size() >= 2)
    {
        double absoluteVariation = 0.0;
        for (size_t k = 1; k < m_delaysSeconds.size(); ++k)
        {
            absoluteVariation += std::fabs(m_delaysSeconds[k] - m_delaysSeconds[k - 1]);
        }
        report.jitter = absoluteVariation / static_cast<double>(m_delaysSeconds.size() - 1);
    }

    // Éq. (28) : NRO est indéfini si aucun paquet applicatif n'est livré.
    if (m_applicationRxPackets > 0)
    {
        report.normalizedRoutingOverhead =
            static_cast<double>(m_aodvControlTransmissions) / static_cast<double>(m_applicationRxPackets);
    }

    // Éq. (29) : l'instrumentation des épisodes sans route n'est pas encore en place.
    // Le champ reste explicitement non renseigné plutôt que faussement nul.
    report.routeUnavailabilityDuration = std::nullopt;

    // Éq. (30), première égalité : somme des énergies consommées par nœud.
    if (m_energyConnected && m_energySources.GetN() > 0)
    {
        double consumed = 0.0;
        for (uint32_t i = 0; i < m_energySources.GetN(); ++i)
        {
            Ptr<energy::EnergySource> source = m_energySources.Get(i);
            consumed += source->GetInitialEnergy() - source->GetRemainingEnergy();
        }
        report.totalEnergyJoules = consumed;
    }

    return report;
}

void
MetricsCollector::ExportCsv(const std::string& path,
                            const std::map<std::string, std::string>& extraColumns) const
{
    const MetricsReport report = ComputeReport();

    std::ofstream file(path);
    if (!file)
    {
        throw std::runtime_error("MetricsCollector : impossible d'écrire " + path);
    }

    // En-tête : d'abord les colonnes de contexte, puis les compteurs, puis les métriques.
    for (const auto& [name, value] : extraColumns)
    {
        file << name << ',';
    }
    file << "appTxPackets,appRxPackets,appTxPayloadBytes,appRxPayloadBytes,deliveredNetworkBytes,"
         << "aodvControlTransmissions,routeDiscoveries,forgedReplies,blackholeTransitDrops,"
         << "evaluationWindowSeconds,pdr,plr,throughputBps,goodputBps,meanDelaySeconds,"
         << "medianDelaySeconds,jitterSeconds,nro,rdfPerSecond,rudSeconds,totalEnergyJoules\n";

    for (const auto& [name, value] : extraColumns)
    {
        (void)name;
        file << value << ',';
    }
    file << report.applicationTxPackets << ',' << report.applicationRxPackets << ','
         << report.applicationTxPayloadBytes << ',' << report.applicationRxPayloadBytes << ','
         << report.deliveredNetworkBytes << ',' << report.aodvControlTransmissions << ','
         << report.routeDiscoveries << ',' << report.forgedReplies << ','
         << report.blackholeTransitDrops << ',' << report.evaluationWindowSeconds << ','
         << FormatMetric(report.packetDeliveryRatio) << ',' << FormatMetric(report.packetLossRatio) << ','
         << FormatMetric(report.throughputBitsPerSecond) << ','
         << FormatMetric(report.goodputBitsPerSecond) << ','
         << FormatMetric(report.meanEndToEndDelay) << ','
         << FormatMetric(report.medianEndToEndDelay) << ',' << FormatMetric(report.jitter) << ','
         << FormatMetric(report.normalizedRoutingOverhead) << ','
         << FormatMetric(report.routeDiscoveryFrequency) << ','
         << FormatMetric(report.routeUnavailabilityDuration) << ','
         << FormatMetric(report.totalEnergyJoules) << '\n';
}

} // namespace mtcaodv
} // namespace ns3
