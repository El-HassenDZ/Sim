/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "cbr-traffic-applications.h"

#include "mtc-traffic-header.h"

#include "ns3/double.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4-address.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/random-variable-stream.h"
#include "ns3/simulator.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("MtcAodvCbrTraffic");

namespace mtcaodv
{

// --------------------------------------------------------------------------------
// CbrTrafficSource
// --------------------------------------------------------------------------------

NS_OBJECT_ENSURE_REGISTERED(CbrTrafficSource);

TypeId
CbrTrafficSource::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::mtcaodv::CbrTrafficSource")
            .SetParent<Application>()
            .SetGroupName("MtcAodv")
            .AddConstructor<CbrTrafficSource>()
            .AddAttribute("DestinationAddress",
                          "Adresse IPv4 du puits.",
                          Ipv4AddressValue(),
                          MakeIpv4AddressAccessor(&CbrTrafficSource::m_destinationAddress),
                          MakeIpv4AddressChecker())
            .AddAttribute("DestinationPort",
                          "Port UDP du puits.",
                          UintegerValue(9000),
                          MakeUintegerAccessor(&CbrTrafficSource::m_destinationPort),
                          MakeUintegerChecker<uint16_t>())
            .AddAttribute("FlowId",
                          "Identifiant du flux applicatif.",
                          UintegerValue(0),
                          MakeUintegerAccessor(&CbrTrafficSource::m_flowId),
                          MakeUintegerChecker<uint16_t>())
            .AddAttribute("PacketSize",
                          "Taille de la charge utile applicative, en octets, en-tête de "
                          "mesure compris.",
                          UintegerValue(512),
                          MakeUintegerAccessor(&CbrTrafficSource::m_packetSize),
                          MakeUintegerChecker<uint32_t>(14, 1400))
            .AddAttribute("PacketRate",
                          "Débit constant en paquets par seconde.",
                          DoubleValue(4.0),
                          MakeDoubleAccessor(&CbrTrafficSource::m_packetRate),
                          MakeDoubleChecker<double>(0.0))
            .AddAttribute("StartJitter",
                          "Borne supérieure du décalage aléatoire du premier envoi.",
                          TimeValue(MilliSeconds(100)),
                          MakeTimeAccessor(&CbrTrafficSource::m_startJitter),
                          MakeTimeChecker())
            .AddTraceSource("Tx",
                            "Un paquet applicatif a été remis à la socket.",
                            MakeTraceSourceAccessor(&CbrTrafficSource::m_txTrace),
                            "ns3::mtcaodv::CbrTrafficSource::TxTracedCallback");
    return tid;
}

CbrTrafficSource::CbrTrafficSource()
    : m_destinationPort(9000),
      m_flowId(0),
      m_packetSize(512),
      m_packetRate(4.0),
      m_startJitter(MilliSeconds(100)),
      m_sequenceNumber(0),
      m_transmittedPacketCount(0),
      m_transmittedPayloadBytes(0),
      m_jitterVariable(CreateObject<UniformRandomVariable>())
{
}

CbrTrafficSource::~CbrTrafficSource()
{
}

uint32_t
CbrTrafficSource::GetTransmittedPacketCount() const
{
    return m_transmittedPacketCount;
}

uint64_t
CbrTrafficSource::GetTransmittedPayloadBytes() const
{
    return m_transmittedPayloadBytes;
}

int64_t
CbrTrafficSource::AssignStreams(int64_t stream)
{
    m_jitterVariable->SetStream(stream);
    return 1;
}

void
CbrTrafficSource::StartApplication()
{
    if (!m_socket)
    {
        m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
        m_socket->Bind();
        m_socket->Connect(InetSocketAddress(m_destinationAddress, m_destinationPort));
        // La source n'attend aucune réponse ; ignorer les réceptions évite d'ajouter
        // du trafic parasite au bilan de surcharge.
        m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
    }

    const Time jitter = MicroSeconds(m_jitterVariable->GetValue(0.0, m_startJitter.GetMicroSeconds()));
    m_sendEvent = Simulator::Schedule(jitter, &CbrTrafficSource::SendPacket, this);
}

void
CbrTrafficSource::StopApplication()
{
    Simulator::Cancel(m_sendEvent);
    if (m_socket)
    {
        m_socket->Close();
    }
}

void
CbrTrafficSource::SendPacket()
{
    MtcTrafficHeader header;
    header.SetFlowId(m_flowId);
    header.SetSequenceNumber(m_sequenceNumber);
    header.SetSendTime(Simulator::Now());

    // La taille configurée est la taille totale de la charge utile applicative :
    // l'en-tête de mesure en fait partie et n'est pas un surcoût caché.
    const uint32_t fillerBytes = m_packetSize - header.GetSerializedSize();
    Ptr<Packet> packet = Create<Packet>(fillerBytes);
    packet->AddHeader(header);

    const int sent = m_socket->Send(packet);
    if (sent >= 0)
    {
        // Seul un envoi accepté par la socket est compté. Un refus (file pleine, pas de
        // route bufferisable) doit apparaître comme une non-émission, pas comme une
        // perte réseau : confondre les deux fausserait le dénominateur de l'Éq. (20).
        ++m_transmittedPacketCount;
        m_transmittedPayloadBytes += m_packetSize;
        m_txTrace(m_flowId, m_sequenceNumber, m_packetSize);
    }
    else
    {
        NS_LOG_DEBUG("flux " << m_flowId << " : envoi refusé par la socket, seq="
                             << m_sequenceNumber);
    }

    ++m_sequenceNumber;
    m_sendEvent = Simulator::Schedule(Seconds(1.0 / m_packetRate), &CbrTrafficSource::SendPacket, this);
}

// --------------------------------------------------------------------------------
// CbrTrafficSink
// --------------------------------------------------------------------------------

NS_OBJECT_ENSURE_REGISTERED(CbrTrafficSink);

TypeId
CbrTrafficSink::GetTypeId()
{
    static TypeId tid = TypeId("ns3::mtcaodv::CbrTrafficSink")
                            .SetParent<Application>()
                            .SetGroupName("MtcAodv")
                            .AddConstructor<CbrTrafficSink>()
                            .AddAttribute("ListenPort",
                                          "Port UDP d'écoute.",
                                          UintegerValue(9000),
                                          MakeUintegerAccessor(&CbrTrafficSink::m_listenPort),
                                          MakeUintegerChecker<uint16_t>())
                            .AddTraceSource("Rx",
                                            "Un paquet applicatif a été livré.",
                                            MakeTraceSourceAccessor(&CbrTrafficSink::m_rxTrace),
                                            "ns3::mtcaodv::CbrTrafficSink::RxTracedCallback");
    return tid;
}

CbrTrafficSink::CbrTrafficSink()
    : m_listenPort(9000),
      m_receivedPacketCount(0),
      m_receivedPayloadBytes(0)
{
}

CbrTrafficSink::~CbrTrafficSink()
{
}

uint32_t
CbrTrafficSink::GetReceivedPacketCount() const
{
    return m_receivedPacketCount;
}

uint64_t
CbrTrafficSink::GetReceivedPayloadBytes() const
{
    return m_receivedPayloadBytes;
}

void
CbrTrafficSink::StartApplication()
{
    if (!m_socket)
    {
        m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
        m_socket->Bind(InetSocketAddress(Ipv4Address::GetAny(), m_listenPort));
        m_socket->SetRecvCallback(MakeCallback(&CbrTrafficSink::HandleRead, this));
    }
}

void
CbrTrafficSink::StopApplication()
{
    if (m_socket)
    {
        m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
        m_socket->Close();
    }
}

void
CbrTrafficSink::HandleRead(Ptr<Socket> socket)
{
    Ptr<Packet> packet;
    Address from;
    while ((packet = socket->RecvFrom(from)))
    {
        if (packet->GetSize() == 0)
        {
            break; // socket fermée
        }

        MtcTrafficHeader header;
        if (packet->RemoveHeader(header) == 0)
        {
            NS_LOG_WARN("paquet reçu sans en-tête de mesure : ignoré");
            continue;
        }

        const Time delay = Simulator::Now() - header.GetSendTime();
        const uint32_t payloadBytes = packet->GetSize() + header.GetSerializedSize();

        ++m_receivedPacketCount;
        m_receivedPayloadBytes += payloadBytes;
        m_rxTrace(header.GetFlowId(), header.GetSequenceNumber(), delay, payloadBytes);
    }
}

} // namespace mtcaodv
} // namespace ns3
