#include "odr-packet.h"

#include "ns3/address-utils.h"
#include "ns3/log.h"

#include <limits>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OdrPacket");

namespace odr
{

namespace
{
// RFC 3561 flag positions in the first RREQ byte. Keeping the RFC layout lets
// Wireshark's AODV dissector decode ODR captures ("Decode As" on ODR_PORT).
constexpr uint8_t RREQ_FLAG_DESTINATION_ONLY = 0x10;
constexpr uint8_t RREQ_FLAG_UNKNOWN_SEQNO = 0x08;
} // namespace

NS_OBJECT_ENSURE_REGISTERED(TypeHeader);

TypeHeader::TypeHeader(MessageType type)
    : m_type(type),
      m_valid(true)
{
}

TypeId
TypeHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::odr::TypeHeader")
                            .SetParent<Header>()
                            .SetGroupName("Odr")
                            .AddConstructor<TypeHeader>();
    return tid;
}

TypeId
TypeHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
TypeHeader::GetSerializedSize() const
{
    return 1;
}

void
TypeHeader::Serialize(Buffer::Iterator start) const
{
    start.WriteU8(static_cast<uint8_t>(m_type));
}

uint32_t
TypeHeader::Deserialize(Buffer::Iterator start)
{
    uint8_t raw = start.ReadU8();
    switch (raw)
    {
    case ODR_RREQ:
    case ODR_RREP:
    case ODR_RERR:
        m_type = static_cast<MessageType>(raw);
        m_valid = true;
        break;
    default:
        NS_LOG_WARN("Unknown ODR message type " << static_cast<uint32_t>(raw));
        m_valid = false;
    }
    return GetSerializedSize();
}

void
TypeHeader::Print(std::ostream& os) const
{
    switch (m_type)
    {
    case ODR_RREQ:
        os << "RREQ";
        break;
    case ODR_RREP:
        os << "RREP";
        break;
    case ODR_RERR:
        os << "RERR";
        break;
    }
    if (!m_valid)
    {
        os << " (invalid)";
    }
}

MessageType
TypeHeader::GetType() const
{
    return m_type;
}

bool
TypeHeader::IsValid() const
{
    return m_valid;
}

NS_OBJECT_ENSURE_REGISTERED(RreqHeader);

RreqHeader::RreqHeader()
    : m_flags(0),
      m_hopCount(0),
      m_requestId(0),
      m_dstSeqNo(0),
      m_originSeqNo(0)
{
}

TypeId
RreqHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::odr::RreqHeader")
                            .SetParent<Header>()
                            .SetGroupName("Odr")
                            .AddConstructor<RreqHeader>();
    return tid;
}

TypeId
RreqHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
RreqHeader::GetSerializedSize() const
{
    return 23;
}

void
RreqHeader::Serialize(Buffer::Iterator i) const
{
    i.WriteU8(m_flags);
    i.WriteU8(0);
    i.WriteU8(m_hopCount);
    i.WriteHtonU32(m_requestId);
    WriteTo(i, m_dst);
    i.WriteHtonU32(m_dstSeqNo);
    WriteTo(i, m_origin);
    i.WriteHtonU32(m_originSeqNo);
}

uint32_t
RreqHeader::Deserialize(Buffer::Iterator start)
{
    Buffer::Iterator i = start;
    m_flags = i.ReadU8();
    i.ReadU8();
    m_hopCount = i.ReadU8();
    m_requestId = i.ReadNtohU32();
    ReadFrom(i, m_dst);
    m_dstSeqNo = i.ReadNtohU32();
    ReadFrom(i, m_origin);
    m_originSeqNo = i.ReadNtohU32();
    return i.GetDistanceFrom(start);
}

void
RreqHeader::Print(std::ostream& os) const
{
    os << "RREQ id " << m_requestId << " dst " << m_dst << " dstSeq " << m_dstSeqNo
       << (IsUnknownSeqNo() ? " (unknown)" : "") << " origin " << m_origin << " originSeq "
       << m_originSeqNo << " hops " << static_cast<uint32_t>(m_hopCount)
       << (IsDestinationOnly() ? " D" : "");
}

void
RreqHeader::SetHopCount(uint8_t hopCount)
{
    m_hopCount = hopCount;
}

uint8_t
RreqHeader::GetHopCount() const
{
    return m_hopCount;
}

void
RreqHeader::SetRequestId(uint32_t id)
{
    m_requestId = id;
}

uint32_t
RreqHeader::GetRequestId() const
{
    return m_requestId;
}

void
RreqHeader::SetDestination(Ipv4Address dst)
{
    m_dst = dst;
}

Ipv4Address
RreqHeader::GetDestination() const
{
    return m_dst;
}

void
RreqHeader::SetDestinationSeqNo(uint32_t seqNo)
{
    m_dstSeqNo = seqNo;
}

uint32_t
RreqHeader::GetDestinationSeqNo() const
{
    return m_dstSeqNo;
}

void
RreqHeader::SetOrigin(Ipv4Address origin)
{
    m_origin = origin;
}

Ipv4Address
RreqHeader::GetOrigin() const
{
    return m_origin;
}

void
RreqHeader::SetOriginSeqNo(uint32_t seqNo)
{
    m_originSeqNo = seqNo;
}

uint32_t
RreqHeader::GetOriginSeqNo() const
{
    return m_originSeqNo;
}

void
RreqHeader::SetDestinationOnly(bool destinationOnly)
{
    m_flags = destinationOnly ? (m_flags | RREQ_FLAG_DESTINATION_ONLY)
                              : (m_flags & ~RREQ_FLAG_DESTINATION_ONLY);
}

bool
RreqHeader::IsDestinationOnly() const
{
    return (m_flags & RREQ_FLAG_DESTINATION_ONLY) != 0;
}

void
RreqHeader::SetUnknownSeqNo(bool unknown)
{
    m_flags = unknown ? (m_flags | RREQ_FLAG_UNKNOWN_SEQNO) : (m_flags & ~RREQ_FLAG_UNKNOWN_SEQNO);
}

bool
RreqHeader::IsUnknownSeqNo() const
{
    return (m_flags & RREQ_FLAG_UNKNOWN_SEQNO) != 0;
}

NS_OBJECT_ENSURE_REGISTERED(RrepHeader);

RrepHeader::RrepHeader()
    : m_hopCount(0),
      m_dstSeqNo(0),
      m_lifetimeMs(0)
{
}

TypeId
RrepHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::odr::RrepHeader")
                            .SetParent<Header>()
                            .SetGroupName("Odr")
                            .AddConstructor<RrepHeader>();
    return tid;
}

TypeId
RrepHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
RrepHeader::GetSerializedSize() const
{
    return 19;
}

void
RrepHeader::Serialize(Buffer::Iterator i) const
{
    i.WriteU8(0);
    i.WriteU8(0);
    i.WriteU8(m_hopCount);
    WriteTo(i, m_dst);
    i.WriteHtonU32(m_dstSeqNo);
    WriteTo(i, m_origin);
    i.WriteHtonU32(m_lifetimeMs);
}

uint32_t
RrepHeader::Deserialize(Buffer::Iterator start)
{
    Buffer::Iterator i = start;
    i.ReadU8();
    i.ReadU8();
    m_hopCount = i.ReadU8();
    ReadFrom(i, m_dst);
    m_dstSeqNo = i.ReadNtohU32();
    ReadFrom(i, m_origin);
    m_lifetimeMs = i.ReadNtohU32();
    return i.GetDistanceFrom(start);
}

void
RrepHeader::Print(std::ostream& os) const
{
    os << "RREP dst " << m_dst << " dstSeq " << m_dstSeqNo << " origin " << m_origin << " hops "
       << static_cast<uint32_t>(m_hopCount) << " lifetime " << m_lifetimeMs << "ms";
}

void
RrepHeader::SetHopCount(uint8_t hopCount)
{
    m_hopCount = hopCount;
}

uint8_t
RrepHeader::GetHopCount() const
{
    return m_hopCount;
}

void
RrepHeader::SetDestination(Ipv4Address dst)
{
    m_dst = dst;
}

Ipv4Address
RrepHeader::GetDestination() const
{
    return m_dst;
}

void
RrepHeader::SetDestinationSeqNo(uint32_t seqNo)
{
    m_dstSeqNo = seqNo;
}

uint32_t
RrepHeader::GetDestinationSeqNo() const
{
    return m_dstSeqNo;
}

void
RrepHeader::SetOrigin(Ipv4Address origin)
{
    m_origin = origin;
}

Ipv4Address
RrepHeader::GetOrigin() const
{
    return m_origin;
}

void
RrepHeader::SetLifetime(Time lifetime)
{
    // A negative lifetime can reach this point when an intermediate node
    // answers from a route that expires within the same simulated instant;
    // advertising zero makes the requester treat the route as already stale
    // instead of wrapping to a 49-day validity.
    int64_t ms = lifetime.GetMilliSeconds();
    if (ms < 0)
    {
        ms = 0;
    }
    else if (ms > std::numeric_limits<uint32_t>::max())
    {
        ms = std::numeric_limits<uint32_t>::max();
    }
    m_lifetimeMs = static_cast<uint32_t>(ms);
}

Time
RrepHeader::GetLifetime() const
{
    return MilliSeconds(m_lifetimeMs);
}

NS_OBJECT_ENSURE_REGISTERED(RerrHeader);

RerrHeader::RerrHeader() = default;

TypeId
RerrHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::odr::RerrHeader")
                            .SetParent<Header>()
                            .SetGroupName("Odr")
                            .AddConstructor<RerrHeader>();
    return tid;
}

TypeId
RerrHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
RerrHeader::GetSerializedSize() const
{
    return 3 + 8 * static_cast<uint32_t>(m_unreachable.size());
}

void
RerrHeader::Serialize(Buffer::Iterator i) const
{
    i.WriteU8(0);
    i.WriteU8(0);
    i.WriteU8(GetDestinationCount());
    for (const auto& entry : m_unreachable)
    {
        WriteTo(i, entry.address);
        i.WriteHtonU32(entry.seqNo);
    }
}

uint32_t
RerrHeader::Deserialize(Buffer::Iterator start)
{
    Buffer::Iterator i = start;
    i.ReadU8();
    i.ReadU8();
    uint8_t count = i.ReadU8();
    m_unreachable.clear();
    m_unreachable.reserve(count);
    for (uint8_t k = 0; k < count; ++k)
    {
        UnreachableDestination entry;
        ReadFrom(i, entry.address);
        entry.seqNo = i.ReadNtohU32();
        m_unreachable.push_back(entry);
    }
    return i.GetDistanceFrom(start);
}

void
RerrHeader::Print(std::ostream& os) const
{
    os << "RERR";
    for (const auto& entry : m_unreachable)
    {
        os << " " << entry.address << "/" << entry.seqNo;
    }
}

bool
RerrHeader::AddUnreachable(Ipv4Address dst, uint32_t seqNo)
{
    if (m_unreachable.size() >= MAX_DESTINATIONS)
    {
        return false;
    }
    m_unreachable.push_back({dst, seqNo});
    return true;
}

const std::vector<UnreachableDestination>&
RerrHeader::GetUnreachable() const
{
    return m_unreachable;
}

uint8_t
RerrHeader::GetDestinationCount() const
{
    return static_cast<uint8_t>(m_unreachable.size());
}

} // namespace odr
} // namespace ns3
