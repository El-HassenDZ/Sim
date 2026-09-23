#include "odr-packet-queue.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"

#include <algorithm>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OdrPacketQueue");

namespace odr
{

QueueEntry::QueueEntry(Ptr<const Packet> packet,
                       const Ipv4Header& header,
                       UnicastForwardCallback ucb,
                       ErrorCallback ecb,
                       Time expiry)
    : m_packet(packet),
      m_header(header),
      m_ucb(ucb),
      m_ecb(ecb),
      m_expiry(expiry)
{
}

Ptr<const Packet>
QueueEntry::GetPacket() const
{
    return m_packet;
}

const Ipv4Header&
QueueEntry::GetIpv4Header() const
{
    return m_header;
}

QueueEntry::UnicastForwardCallback
QueueEntry::GetUnicastForwardCallback() const
{
    return m_ucb;
}

QueueEntry::ErrorCallback
QueueEntry::GetErrorCallback() const
{
    return m_ecb;
}

Time
QueueEntry::GetExpiry() const
{
    return m_expiry;
}

PacketQueue::PacketQueue(uint32_t maxLength, Time maxDelay)
    : m_maxLength(maxLength),
      m_maxDelay(maxDelay)
{
    NS_ABORT_MSG_IF(maxLength == 0, "A zero-length queue would drop every packet before discovery");
}

void
PacketQueue::SetMaxLength(uint32_t maxLength)
{
    NS_ABORT_MSG_IF(maxLength == 0, "A zero-length queue would drop every packet before discovery");
    m_maxLength = maxLength;
}

void
PacketQueue::SetMaxDelay(Time maxDelay)
{
    m_maxDelay = maxDelay;
}

Time
PacketQueue::GetMaxDelay() const
{
    return m_maxDelay;
}

void
PacketQueue::SetDropCallback(DropCallback callback)
{
    m_dropCallback = callback;
}

bool
PacketQueue::Enqueue(const QueueEntry& entry)
{
    Purge();
    uint64_t uid = entry.GetPacket()->GetUid();
    Ipv4Address dst = entry.GetIpv4Header().GetDestination();
    bool duplicate = std::any_of(m_queue.begin(), m_queue.end(), [uid, dst](const QueueEntry& e) {
        return e.GetPacket()->GetUid() == uid && e.GetIpv4Header().GetDestination() == dst;
    });
    if (duplicate)
    {
        NS_LOG_LOGIC("Packet " << uid << " to " << dst << " already parked");
        return false;
    }
    while (m_queue.size() >= m_maxLength)
    {
        QueueEntry evicted = m_queue.front();
        m_queue.pop_front();
        NotifyDrop(evicted, QueueDropReason::QUEUE_FULL);
    }
    m_queue.push_back(entry);
    return true;
}

std::vector<QueueEntry>
PacketQueue::Dequeue(Ipv4Address dst)
{
    Purge();
    std::vector<QueueEntry> released;
    auto keep = std::stable_partition(m_queue.begin(), m_queue.end(), [dst](const QueueEntry& e) {
        return e.GetIpv4Header().GetDestination() != dst;
    });
    released.assign(keep, m_queue.end());
    m_queue.erase(keep, m_queue.end());
    return released;
}

void
PacketQueue::Drop(Ipv4Address dst, QueueDropReason reason)
{
    for (const QueueEntry& entry : Dequeue(dst))
    {
        NotifyDrop(entry, reason);
    }
}

void
PacketQueue::DropAll(QueueDropReason reason)
{
    // Swap first: the IP layer error callback may re-enter the routing
    // protocol, which must then see an empty queue rather than one being
    // iterated.
    std::deque<QueueEntry> dropped;
    dropped.swap(m_queue);
    for (const QueueEntry& entry : dropped)
    {
        NotifyDrop(entry, reason);
    }
}

void
PacketQueue::Clear()
{
    m_queue.clear();
}

bool
PacketQueue::Contains(Ipv4Address dst)
{
    Purge();
    return std::any_of(m_queue.begin(), m_queue.end(), [dst](const QueueEntry& e) {
        return e.GetIpv4Header().GetDestination() == dst;
    });
}

uint32_t
PacketQueue::GetSize()
{
    Purge();
    return static_cast<uint32_t>(m_queue.size());
}

void
PacketQueue::Purge()
{
    // Expiry is insertion time plus a common delay, so expired packets form a
    // prefix of the FIFO as long as the delay is not shortened mid-run. After
    // such a change, the remaining out-of-order packets expire at the next
    // purge that reaches them, which is conservative.
    Time now = Simulator::Now();
    while (!m_queue.empty() && m_queue.front().GetExpiry() <= now)
    {
        QueueEntry expired = m_queue.front();
        m_queue.pop_front();
        NotifyDrop(expired, QueueDropReason::EXPIRED);
    }
}

void
PacketQueue::NotifyDrop(const QueueEntry& entry, QueueDropReason reason)
{
    NS_LOG_DEBUG("Drop packet " << entry.GetPacket()->GetUid() << " to "
                                << entry.GetIpv4Header().GetDestination() << " reason "
                                << static_cast<uint32_t>(reason));
    if (!m_dropCallback.IsNull())
    {
        m_dropCallback(entry, reason);
    }
    if (!entry.GetErrorCallback().IsNull())
    {
        entry.GetErrorCallback()(entry.GetPacket(),
                                 entry.GetIpv4Header(),
                                 Socket::ERROR_NOROUTETOHOST);
    }
}

} // namespace odr
} // namespace ns3
