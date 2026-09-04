/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "mtc-traffic-header.h"

namespace ns3
{
namespace mtcaodv
{

NS_OBJECT_ENSURE_REGISTERED(MtcTrafficHeader);

TypeId
MtcTrafficHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::mtcaodv::MtcTrafficHeader")
                            .SetParent<Header>()
                            .SetGroupName("MtcAodv")
                            .AddConstructor<MtcTrafficHeader>();
    return tid;
}

TypeId
MtcTrafficHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

MtcTrafficHeader::MtcTrafficHeader()
    : m_flowId(0),
      m_sequenceNumber(0),
      m_sendTimeNs(0)
{
}

void
MtcTrafficHeader::SetFlowId(uint16_t flowId)
{
    m_flowId = flowId;
}

uint16_t
MtcTrafficHeader::GetFlowId() const
{
    return m_flowId;
}

void
MtcTrafficHeader::SetSequenceNumber(uint32_t sequenceNumber)
{
    m_sequenceNumber = sequenceNumber;
}

uint32_t
MtcTrafficHeader::GetSequenceNumber() const
{
    return m_sequenceNumber;
}

void
MtcTrafficHeader::SetSendTime(Time sendTime)
{
    m_sendTimeNs = static_cast<uint64_t>(sendTime.GetNanoSeconds());
}

Time
MtcTrafficHeader::GetSendTime() const
{
    return NanoSeconds(m_sendTimeNs);
}

uint32_t
MtcTrafficHeader::GetSerializedSize() const
{
    return 2 + 4 + 8;
}

void
MtcTrafficHeader::Serialize(Buffer::Iterator start) const
{
    start.WriteHtonU16(m_flowId);
    start.WriteHtonU32(m_sequenceNumber);
    start.WriteHtonU64(m_sendTimeNs);
}

uint32_t
MtcTrafficHeader::Deserialize(Buffer::Iterator start)
{
    m_flowId = start.ReadNtohU16();
    m_sequenceNumber = start.ReadNtohU32();
    m_sendTimeNs = start.ReadNtohU64();
    return GetSerializedSize();
}

void
MtcTrafficHeader::Print(std::ostream& os) const
{
    os << "flow=" << m_flowId << " seq=" << m_sequenceNumber << " sent=" << GetSendTime().As(Time::S);
}

} // namespace mtcaodv
} // namespace ns3
