#ifndef ODR_PACKET_QUEUE_H
#define ODR_PACKET_QUEUE_H

#include "ns3/callback.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/nstime.h"
#include "ns3/packet.h"

#include <deque>
#include <vector>

namespace ns3
{
namespace odr
{

/**
 * @ingroup odr
 * @brief A data packet parked while a route to its destination is searched.
 *
 * The forwarding and error callbacks are the ones the IP layer passed to
 * RouteInput(); keeping them with the packet is the only way to hand it back
 * to Ipv4L3Protocol later, once RouteInput() has already returned.
 */
class QueueEntry
{
  public:
    /// Forwarding callback type of the IP layer.
    using UnicastForwardCallback = Ipv4RoutingProtocol::UnicastForwardCallback;
    /// Error callback type of the IP layer.
    using ErrorCallback = Ipv4RoutingProtocol::ErrorCallback;

    QueueEntry() = default;

    /**
     * @brief Park a packet.
     * @param packet the data packet, IP header removed
     * @param header its IP header
     * @param ucb callback that transmits the packet once a route is known
     * @param ecb callback that reports the packet as dropped
     * @param expiry absolute time after which the packet is discarded
     */
    QueueEntry(Ptr<const Packet> packet,
               const Ipv4Header& header,
               UnicastForwardCallback ucb,
               ErrorCallback ecb,
               Time expiry);

    /**
     * @brief Get the parked packet.
     * @return the packet
     */
    Ptr<const Packet> GetPacket() const;

    /**
     * @brief Get the IP header of the parked packet.
     * @return the header
     */
    const Ipv4Header& GetIpv4Header() const;

    /**
     * @brief Get the transmission callback.
     * @return the forwarding callback
     */
    UnicastForwardCallback GetUnicastForwardCallback() const;

    /**
     * @brief Get the drop-reporting callback.
     * @return the error callback
     */
    ErrorCallback GetErrorCallback() const;

    /**
     * @brief Get the discard time.
     * @return the absolute expiry time
     */
    Time GetExpiry() const;

  private:
    Ptr<const Packet> m_packet;   //!< Parked packet
    Ipv4Header m_header;          //!< Its IP header
    UnicastForwardCallback m_ucb; //!< Transmission callback
    ErrorCallback m_ecb;          //!< Drop callback
    Time m_expiry;                //!< Discard time
};

/**
 * @ingroup odr
 * @brief Why a parked packet left the queue without being transmitted.
 */
enum class QueueDropReason : uint8_t
{
    QUEUE_FULL, //!< Evicted to make room for a newer packet
    EXPIRED,    //!< Waited longer than the maximum queueing delay
    NO_ROUTE,   //!< Route discovery gave up
    SHUTDOWN,   //!< Interface or protocol went down
};

/**
 * @ingroup odr
 * @brief Bounded FIFO of packets awaiting route discovery.
 *
 * Under a network partition every packet to the unreachable side lands here
 * for the whole discovery back-off, so both the length and the waiting time
 * are bounded. On overflow the oldest packet is evicted: it has the least
 * remaining lifetime and its discovery is the most likely to be failing,
 * whereas tail drop would starve the fresh traffic a repaired route could
 * still deliver.
 */
class PacketQueue
{
  public:
    /// Signature of the observer notified for each dropped packet.
    using DropCallback = Callback<void, const QueueEntry&, QueueDropReason>;

    /**
     * @brief Build an empty queue.
     * @param maxLength maximum number of parked packets, all destinations included
     * @param maxDelay maximum time a packet may stay parked
     */
    PacketQueue(uint32_t maxLength = 64, Time maxDelay = Seconds(30));

    /**
     * @brief Change the capacity; excess packets are evicted on the next insertion.
     * @param maxLength the new capacity
     */
    void SetMaxLength(uint32_t maxLength);

    /**
     * @brief Change the maximum waiting time for packets parked from now on.
     * @param maxDelay the new maximum waiting time
     */
    void SetMaxDelay(Time maxDelay);

    /**
     * @brief Get the maximum waiting time.
     * @return the maximum waiting time
     */
    Time GetMaxDelay() const;

    /**
     * @brief Install the observer of dropped packets.
     *
     * The observer runs before the entry's own error callback, and is the
     * single place where drop statistics are accounted.
     *
     * @param callback the observer
     */
    void SetDropCallback(DropCallback callback);

    /**
     * @brief Park a packet, evicting the oldest one if the queue is full.
     *
     * A packet already parked for the same destination is refused: it means
     * the IP layer looped it back twice, and parking it again would transmit
     * it twice once the route is found.
     *
     * @param entry the packet to park
     * @return false if the packet was a duplicate and was not parked
     */
    bool Enqueue(const QueueEntry& entry);

    /**
     * @brief Remove every packet for a destination, oldest first.
     * @param dst the destination whose route just became available
     * @return the packets, in arrival order
     */
    std::vector<QueueEntry> Dequeue(Ipv4Address dst);

    /**
     * @brief Discard every packet for a destination.
     * @param dst the destination
     * @param reason the drop reason reported to the observer
     */
    void Drop(Ipv4Address dst, QueueDropReason reason);

    /**
     * @brief Discard every packet.
     * @param reason the drop reason reported to the observer
     */
    void DropAll(QueueDropReason reason);

    /**
     * @brief Release every packet without reporting any drop.
     *
     * Reserved for teardown, when the IP layer the error callbacks point to
     * may already be disposed.
     */
    void Clear();

    /**
     * @brief Tell whether packets are waiting for a destination.
     * @param dst the destination
     * @return true if at least one packet is parked for it
     */
    bool Contains(Ipv4Address dst);

    /**
     * @brief Get the number of parked packets.
     * @return the queue length after discarding expired packets
     */
    uint32_t GetSize();

  private:
    /**
     * @brief Discard the packets that waited too long.
     */
    void Purge();

    /**
     * @brief Report a drop to the observer and to the IP layer.
     * @param entry the dropped packet
     * @param reason the drop reason
     */
    void NotifyDrop(const QueueEntry& entry, QueueDropReason reason);

    std::deque<QueueEntry> m_queue; //!< Parked packets, oldest first
    uint32_t m_maxLength;           //!< Capacity
    Time m_maxDelay;                //!< Maximum waiting time
    DropCallback m_dropCallback;    //!< Drop observer
};

} // namespace odr
} // namespace ns3

#endif /* ODR_PACKET_QUEUE_H */
