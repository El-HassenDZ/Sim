/*
 * rreq-flooder.cc  — see rreq-flooder.h for the design and honest note.
 */
#include "rreq-flooder.h"

#include "ns3/inet-socket-address.h"
#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RreqFlooder");
NS_OBJECT_ENSURE_REGISTERED(RreqFlooder);

TypeId
RreqFlooder::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::RreqFlooder")
            .SetParent<Application>()
            .SetGroupName("Applications")
            .AddConstructor<RreqFlooder>()
            .AddAttribute("Base",
                          "Base network address for fabricated destinations.",
                          Ipv4AddressValue(Ipv4Address("10.0.0.0")),
                          MakeIpv4AddressAccessor(&RreqFlooder::m_base),
                          MakeIpv4AddressChecker())
            .AddAttribute("FirstHost",
                          "First (likely unassigned) host id to target.",
                          UintegerValue(150),
                          MakeUintegerAccessor(&RreqFlooder::m_firstHost),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("HostRange",
                          "Number of distinct destinations to cycle through.",
                          UintegerValue(90),
                          MakeUintegerAccessor(&RreqFlooder::m_hostRange),
                          MakeUintegerChecker<uint32_t>(1))
            .AddAttribute("Interval",
                          "Time between probe packets.",
                          TimeValue(MilliSeconds(50)),
                          MakeTimeAccessor(&RreqFlooder::m_interval),
                          MakeTimeChecker())
            .AddAttribute("Port",
                          "Destination UDP port.",
                          UintegerValue(9999),
                          MakeUintegerAccessor(&RreqFlooder::m_port),
                          MakeUintegerChecker<uint16_t>())
            .AddAttribute("PacketSize",
                          "Probe packet size (bytes).",
                          UintegerValue(64),
                          MakeUintegerAccessor(&RreqFlooder::m_pktSize),
                          MakeUintegerChecker<uint32_t>());
    return tid;
}

RreqFlooder::RreqFlooder()
    : m_socket(nullptr),
      m_next(0),
      m_sent(0),
      m_running(false)
{
}

RreqFlooder::~RreqFlooder()
{
}

uint64_t
RreqFlooder::GetSentProbes() const
{
    return m_sent;
}

void
RreqFlooder::StartApplication()
{
    m_running = true;
    if (!m_socket)
    {
        m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
        m_socket->Bind();
    }
    m_next = 0;
    m_sendEvent = Simulator::ScheduleNow(&RreqFlooder::SendProbe, this);
}

void
RreqFlooder::StopApplication()
{
    m_running = false;
    Simulator::Cancel(m_sendEvent);
    if (m_socket)
    {
        m_socket->Close();
    }
}

void
RreqFlooder::SendProbe()
{
    if (!m_running)
    {
        return;
    }
    // Fabricate the next destination inside m_base with an unassigned host id.
    uint32_t host = m_firstHost + (m_next % m_hostRange);
    Ipv4Address dst(m_base.Get() + host);
    m_next++;

    Ptr<Packet> pkt = Create<Packet>(m_pktSize);
    m_socket->SendTo(pkt, 0, InetSocketAddress(dst, m_port));
    m_sent++;

    m_sendEvent = Simulator::Schedule(m_interval, &RreqFlooder::SendProbe, this);
}

} // namespace ns3
