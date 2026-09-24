#include "odr-cbr-source.h"

#include "ns3/abort.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OdrCbrSource");

namespace odr
{

NS_OBJECT_ENSURE_REGISTERED(CbrSource);

TypeId
CbrSource::GetTypeId()
{
    static TypeId tid = TypeId("ns3::odr::CbrSource")
                            .SetParent<Application>()
                            .SetGroupName("Odr")
                            .AddConstructor<CbrSource>()
                            .AddAttribute("Remote",
                                          "Destination address and port (InetSocketAddress).",
                                          AddressValue(),
                                          MakeAddressAccessor(&CbrSource::m_remote),
                                          MakeAddressChecker())
                            .AddAttribute("PacketSize",
                                          "UDP payload size in bytes.",
                                          UintegerValue(512),
                                          MakeUintegerAccessor(&CbrSource::m_packetSize),
                                          MakeUintegerChecker<uint32_t>(1, 65507))
                            .AddAttribute("Interval",
                                          "Time between two packets.",
                                          TimeValue(MilliSeconds(250)),
                                          MakeTimeAccessor(&CbrSource::m_interval),
                                          MakeTimeChecker(NanoSeconds(1)));
    return tid;
}

CbrSource::CbrSource()
    : m_packetSize(512),
      m_attempted(0),
      m_accepted(0),
      m_refused(0)
{
}

CbrSource::~CbrSource() = default;

uint64_t
CbrSource::GetAttempted() const
{
    return m_attempted;
}

uint64_t
CbrSource::GetAccepted() const
{
    return m_accepted;
}

uint64_t
CbrSource::GetRefused() const
{
    return m_refused;
}

void
CbrSource::DoDispose()
{
    m_sendEvent.Cancel();
    m_socket = nullptr;
    Application::DoDispose();
}

void
CbrSource::StartApplication()
{
    NS_LOG_FUNCTION(this);
    NS_ABORT_MSG_IF(m_remote.IsInvalid(), "CbrSource started without a Remote address");
    m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
    NS_ABORT_MSG_IF(m_socket->Bind() != 0, "CbrSource could not bind its socket");
    // Connecting a UDP socket only records the default destination; no route
    // is looked up here, so a missing route shows up per packet in Send().
    m_socket->Connect(m_remote);
    m_sendEvent = Simulator::ScheduleNow(&CbrSource::SendPacket, this);
}

void
CbrSource::StopApplication()
{
    NS_LOG_FUNCTION(this);
    m_sendEvent.Cancel();
    if (m_socket)
    {
        m_socket->Close();
    }
}

void
CbrSource::SendPacket()
{
    ++m_attempted;
    if (m_socket->Send(Create<Packet>(m_packetSize)) >= 0)
    {
        ++m_accepted;
    }
    else
    {
        // The packet is dropped rather than retried: a retry would shift the
        // offered load in time and make it depend on the routing protocol.
        NS_LOG_DEBUG("Packet refused by the socket, errno " << m_socket->GetErrno());
        ++m_refused;
    }
    m_sendEvent = Simulator::Schedule(m_interval, &CbrSource::SendPacket, this);
}

} // namespace odr
} // namespace ns3
