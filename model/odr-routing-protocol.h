#ifndef ODR_ROUTING_PROTOCOL_H
#define ODR_ROUTING_PROTOCOL_H

#include "odr-packet-queue.h"
#include "odr-packet.h"
#include "odr-request-cache.h"
#include "odr-routing-table.h"

#include "ns3/arp-cache.h"
#include "ns3/event-id.h"
#include "ns3/ipv4-interface-address.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/random-variable-stream.h"
#include "ns3/wifi-mac.h"
#include "ns3/wifi-mpdu.h"

#include <map>
#include <set>

namespace ns3
{
namespace odr
{

/**
 * @ingroup odr
 * @brief Per-node counters exported after the run.
 *
 * Message counters count transmissions, one per interface for broadcasts,
 * because that is what occupies the channel. Byte counters cover the ODR
 * payload only (type byte plus message body); add 28 bytes per packet for the
 * IPv4 and UDP headers when comparing with an IP-level overhead figure.
 */
struct Statistics
{
    uint64_t rreqOriginated{0};          //!< RREQ floods started, retries included
    uint64_t rreqForwarded{0};           //!< RREQ rebroadcasts on behalf of others
    uint64_t rrepOriginated{0};          //!< RREP sent as destination or from a cached route
    uint64_t rrepForwarded{0};           //!< RREP relayed towards an originator
    uint64_t rerrSent{0};                //!< RERR transmissions
    uint64_t rerrSuppressed{0};          //!< RERR withheld by the rate limiter
    uint64_t controlTxPackets{0};        //!< All control transmissions
    uint64_t controlTxBytes{0};          //!< ODR payload bytes transmitted
    uint64_t controlRxPackets{0};        //!< Control messages received
    uint64_t controlRxBytes{0};          //!< ODR payload bytes received
    uint64_t discoveriesStarted{0};      //!< Route discoveries initiated
    uint64_t discoveriesSucceeded{0};    //!< Discoveries that produced a route
    uint64_t discoveriesFailed{0};       //!< Discoveries abandoned after all retries
    Time discoveryLatencySum;            //!< Sum of latencies of successful discoveries
    uint64_t linkBreaks{0};              //!< MAC-detected losses that invalidated routes
    uint64_t dataDroppedNoRoute{0};      //!< Parked packets dropped when discovery failed
    uint64_t dataDroppedQueueFull{0};    //!< Parked packets evicted by newer ones
    uint64_t dataDroppedQueueTimeout{0}; //!< Parked packets that waited too long
    uint64_t dataDroppedForwarding{0};   //!< Transit packets without a usable route
};

/**
 * @ingroup odr
 * @brief ODR, a reactive hop-count routing protocol for IPv4 MANETs.
 *
 * Version 1 deliberately implements the RFC 3561 core with hop count as the
 * only metric, so that it can be validated against ns-3's AODV before any new
 * route selection criterion is introduced:
 * - expanding-ring RREQ flooding with jittered rebroadcast and duplicate
 *   suppression;
 * - destination sequence numbers for loop freedom;
 * - replies from the destination or from an intermediate node holding a
 *   fresh enough route;
 * - link-layer break detection (802.11 retry exhaustion) and RERR
 *   propagation along precursor lists.
 *
 * Not implemented, by design for v1: HELLO beacons (they add a constant
 * overhead that would blur the reactive overhead measurement), local repair,
 * gratuitous RREP and RREP-ACK blacklisting of unidirectional links. The last
 * one matters only with asymmetric propagation, and should be revisited if
 * scenarios use heterogeneous transmit powers.
 *
 * The protocol must be installed through OdrHelper, which aggregates it to
 * the node so that DoInitialize() runs; in an Ipv4ListRouting it needs the
 * highest priority, because it answers every RouteOutput() with either a
 * route or the deferred loopback route.
 */
class RoutingProtocol : public Ipv4RoutingProtocol
{
  public:
    /// UDP port of ODR control traffic. Distinct from AODV's 654 so that both
    /// can be captured and filtered side by side.
    static constexpr uint16_t ODR_PORT = 5654;

    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    RoutingProtocol();
    ~RoutingProtocol() override;

    Ptr<Ipv4Route> RouteOutput(Ptr<Packet> p,
                               const Ipv4Header& header,
                               Ptr<NetDevice> oif,
                               Socket::SocketErrno& sockerr) override;
    bool RouteInput(Ptr<const Packet> p,
                    const Ipv4Header& header,
                    Ptr<const NetDevice> idev,
                    const UnicastForwardCallback& ucb,
                    const MulticastForwardCallback& mcb,
                    const LocalDeliverCallback& lcb,
                    const ErrorCallback& ecb) override;
    void NotifyInterfaceUp(uint32_t interface) override;
    void NotifyInterfaceDown(uint32_t interface) override;
    void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address) override;
    void NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address) override;
    void SetIpv4(Ptr<Ipv4> ipv4) override;
    void PrintRoutingTable(Ptr<OutputStreamWrapper> stream,
                           Time::Unit unit = Time::S) const override;

    /**
     * @brief Fix the random stream used for rebroadcast jitter.
     * @param stream first stream index to use
     * @return the number of streams consumed (1)
     */
    int64_t AssignStreams(int64_t stream);

    /**
     * @brief Get the counters accumulated since the start of the run.
     * @return the statistics; read them before Simulator::Destroy()
     */
    const Statistics& GetStatistics() const;

    /**
     * @brief Get the number of data packets still waiting for a route.
     *
     * Needed to close the packet balance at the end of a run: a packet parked
     * at Simulator::Stop() is neither delivered nor dropped.
     *
     * @return the queue length
     */
    uint32_t GetQueueLength();

    /**
     * @brief Get a copy of the routing table entry for a destination.
     * @param dst destination address
     * @param entry receives the entry when found
     * @return true if an entry exists, whatever its state
     */
    bool LookupRoute(Ipv4Address dst, RoutingTableEntry& entry) const;

  protected:
    void DoInitialize() override;
    void DoDispose() override;

  private:
    /// Sockets and link-layer hooks attached to one ODR-enabled interface.
    struct InterfaceState
    {
        Ipv4InterfaceAddress address; //!< The single address ODR runs on
        Ptr<NetDevice> device;        //!< Interface device
        Ptr<Socket> unicastSocket;    //!< Bound to the local address; also used to send
        Ptr<Socket> broadcastSocket;  //!< Bound to the subnet-directed broadcast address
        Ptr<ArpCache> arpCache;       //!< Maps MAC failures back to IPv4 neighbors
        Ptr<WifiMac> mac;             //!< Source of retry-exhaustion notifications
    };

    /// State of an ongoing route discovery.
    struct Discovery
    {
        uint32_t ttl{0};     //!< TTL of the last RREQ flood
        uint32_t retries{0}; //!< Floods sent at network-diameter TTL beyond the first
        Time started;        //!< Time the first flood was sent
        EventId timer;       //!< Pending timeout or rate-limited retransmission
    };

    /**
     * @brief Open the ODR sockets and link-layer hooks on an interface.
     * @param interface interface index
     */
    void StartInterface(uint32_t interface);

    /**
     * @brief Close the ODR sockets and hooks of an interface and drop its routes.
     * @param interface interface index
     */
    void StopInterface(uint32_t interface);

    /**
     * @brief Tell whether an address belongs to an ODR-enabled interface.
     * @param address the address to test
     * @return true for one of this node's addresses
     */
    bool IsMyOwnAddress(Ipv4Address address) const;

    /**
     * @brief Tell whether a destination is a broadcast address on some interface.
     * @param dst the destination
     * @return true for 255.255.255.255 or a subnet-directed broadcast
     */
    bool IsBroadcast(Ipv4Address dst) const;

    /**
     * @brief Build the route that sends a packet through the loopback device.
     * @param header IP header of the packet
     * @param oif output device requested by the caller, if any
     * @return the loopback route
     */
    Ptr<Ipv4Route> LoopbackRoute(const Ipv4Header& header, Ptr<NetDevice> oif) const;

    /**
     * @brief Park a looped-back packet and trigger a discovery if needed.
     * @param p the packet
     * @param header its IP header
     * @param ucb forwarding callback
     * @param ecb error callback
     */
    void DeferRouteOutput(Ptr<const Packet> p,
                          const Ipv4Header& header,
                          const UnicastForwardCallback& ucb,
                          const ErrorCallback& ecb);

    /**
     * @brief Forward a transit packet or report the missing route.
     * @param p the packet
     * @param header its IP header
     * @param ucb forwarding callback
     * @return false if no usable route exists
     */
    bool Forward(Ptr<const Packet> p, const Ipv4Header& header, const UnicastForwardCallback& ucb);

    /**
     * @brief Extend the lifetime of the routes a data packet is using.
     * @param dst the packet destination
     * @param origin the packet source, whose reverse route is kept alive too
     */
    void RefreshActiveRoutes(Ipv4Address dst, Ipv4Address origin);

    /**
     * @brief React to a route becoming usable: close discovery, flush the queue.
     * @param dst the destination now reachable
     */
    void OnRouteAvailable(Ipv4Address dst);

    /**
     * @brief Transmit every packet parked for a destination.
     * @param dst the destination
     */
    void SendQueuedPackets(Ipv4Address dst);

    /**
     * @brief Account for a packet dropped by the queue.
     * @param entry the packet
     * @param reason why it was dropped
     */
    void OnQueueDrop(const QueueEntry& entry, QueueDropReason reason);

    /**
     * @brief Begin a discovery for a destination unless one is running.
     * @param dst the destination
     */
    void StartDiscovery(Ipv4Address dst);

    /**
     * @brief Flood a RREQ for an ongoing discovery, subject to rate limiting.
     * @param dst the destination
     */
    void SendRequest(Ipv4Address dst);

    /**
     * @brief Widen the ring, retry or give up after a RREQ went unanswered.
     * @param dst the destination
     */
    void OnDiscoveryTimeout(Ipv4Address dst);

    /**
     * @brief Dispatch a received control message.
     * @param socket the socket that received it
     */
    void RecvControl(Ptr<Socket> socket);

    /**
     * @brief Process a RREQ.
     * @param packet message body
     * @param itf receiving interface
     * @param sender IP source of the message
     * @param ttl IP TTL on arrival, bounding the remaining flood radius
     */
    void RecvRequest(Ptr<Packet> packet,
                     const InterfaceState& itf,
                     Ipv4Address sender,
                     uint8_t ttl);

    /**
     * @brief Process a RREP.
     * @param packet message body
     * @param itf receiving interface
     * @param sender IP source of the message
     */
    void RecvReply(Ptr<Packet> packet, const InterfaceState& itf, Ipv4Address sender);

    /**
     * @brief Process a RERR.
     * @param packet message body
     * @param sender IP source of the message
     */
    void RecvError(Ptr<Packet> packet, Ipv4Address sender);

    /**
     * @brief Answer a RREQ addressed to this node.
     * @param rreq the request
     * @param sender neighbor the reply is sent to
     */
    void ReplyAsDestination(const RreqHeader& rreq, Ipv4Address sender);

    /**
     * @brief Answer a RREQ from a cached route.
     * @param rreq the request
     * @param toDst the cached route
     * @param sender neighbor the reply is sent to
     */
    void ReplyFromCache(const RreqHeader& rreq, const RoutingTableEntry& toDst, Ipv4Address sender);

    /**
     * @brief Invalidate routes through a lost neighbor and warn upstream nodes.
     * @param nextHop the neighbor that stopped acknowledging
     */
    void HandleLinkFailure(Ipv4Address nextHop);

    /**
     * @brief Emit RERRs for a set of unreachable destinations.
     * @param unreachable the destinations
     * @param precursors upstream neighbors relying on them; empty means broadcast
     */
    void SendError(const std::vector<UnreachableDestination>& unreachable,
                   std::set<Ipv4Address> precursors);

    /**
     * @brief Receive 802.11 MPDU drops and keep only link failures.
     * @param reason drop reason reported by the MAC
     * @param mpdu the dropped MPDU
     */
    void NotifyTxError(WifiMacDropReason reason, Ptr<const WifiMpdu> mpdu);

    /**
     * @brief Send a control message to every neighbor on every interface.
     * @param packet message, type header included
     * @param ttl IP TTL, i.e. flood radius
     * @param counter per-message counter incremented for each transmission
     */
    void BroadcastControl(Ptr<Packet> packet, uint8_t ttl, uint64_t Statistics::* counter);

    /**
     * @brief Send a control message to one neighbor.
     * @param packet message, type header included
     * @param neighbor destination neighbor, which must have a usable route
     * @return false if the message could not be sent
     */
    bool UnicastControl(Ptr<Packet> packet, Ipv4Address neighbor);

    /**
     * @brief Hand a control message to a socket and account for it.
     * @param itf interface to send on
     * @param packet message, type header included
     * @param to IP destination
     * @param ttl IP TTL
     * @return false if the socket refused the message
     */
    bool SendControl(const InterfaceState& itf, Ptr<Packet> packet, Ipv4Address to, uint8_t ttl);

    /**
     * @brief Take a token from a one-second rate-limiting window.
     * @param windowStart start of the current window, updated in place
     * @param count messages sent in the current window, updated in place
     * @param limit maximum messages per window
     * @return false if the window is exhausted
     */
    static bool ConsumeToken(Time& windowStart, uint32_t& count, uint32_t limit);

    /**
     * @brief Get the interface state owning a socket.
     * @param socket the socket
     * @return the interface state, or nullptr
     */
    const InterfaceState* FindInterface(Ptr<Socket> socket) const;

    /// @return RFC 3561 NET_TRAVERSAL_TIME, derived from the attributes
    Time NetTraversalTime() const;
    /// @return RFC 3561 PATH_DISCOVERY_TIME, derived from the attributes
    Time PathDiscoveryTime() const;
    /// @return RFC 3561 DELETE_PERIOD for link-layer feedback (K = 5)
    Time DeletePeriod() const;
    /// @param ttl flood radius
    /// @return RFC 3561 RING_TRAVERSAL_TIME for that radius
    Time RingTraversalTime(uint32_t ttl) const;

    // Attributes. Timing values follow RFC 3561 section 10; derived constants
    // are recomputed from these on use instead of being attributes of their
    // own, so that changing NodeTraversalTime cannot leave NetTraversalTime
    // inconsistent with it.
    Time m_activeRouteTimeout; //!< Lifetime granted to routes in use
    Time m_nodeTraversalTime;  //!< Conservative per-hop traversal estimate
    uint32_t m_netDiameter;    //!< Maximum path length, in hops
    uint32_t m_rreqRetries;    //!< Retries at full TTL before giving up
    uint32_t m_rreqRateLimit;  //!< RREQ originated per second
    uint32_t m_rerrRateLimit;  //!< RERR sent per second
    uint32_t m_ttlStart;       //!< First ring radius
    uint32_t m_ttlIncrement;   //!< Ring growth per attempt
    uint32_t m_ttlThreshold;   //!< Largest ring before jumping to the diameter
    uint32_t m_timeoutBuffer;  //!< Extra hops of slack in ring timeouts
    uint32_t m_maxQueueLength; //!< Parked packets, all destinations
    Time m_maxQueueTime;       //!< Maximum parking delay
    bool m_destinationOnly;    //!< Forbid replies from cached routes
    Time m_maxJitter;          //!< Upper bound of the rebroadcast jitter

    Ptr<Ipv4> m_ipv4;                                //!< IPv4 stack of the node
    Ptr<NetDevice> m_lo;                             //!< Loopback device
    std::map<uint32_t, InterfaceState> m_interfaces; //!< ODR interfaces by index
    RoutingTable m_routingTable;                     //!< Routes
    RequestCache m_requestCache;                     //!< Processed floods
    PacketQueue m_queue;                             //!< Packets awaiting a route
    std::map<Ipv4Address, Discovery> m_discoveries;  //!< Ongoing discoveries
    Ptr<UniformRandomVariable> m_jitter;             //!< Rebroadcast jitter source
    uint32_t m_seqNo;                                //!< Own sequence number
    uint32_t m_requestId;                            //!< Last RREQ identifier used
    Time m_rreqWindowStart;                          //!< RREQ rate-limiting window start
    uint32_t m_rreqWindowCount;                      //!< RREQ sent in the window
    Time m_rerrWindowStart;                          //!< RERR rate-limiting window start
    uint32_t m_rerrWindowCount;                      //!< RERR sent in the window
    Statistics m_stats;                              //!< Exported counters
};

} // namespace odr
} // namespace ns3

#endif /* ODR_ROUTING_PROTOCOL_H */
