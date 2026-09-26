/*
 * blackhole-aodv.h
 * ----------------
 * ACTIVE blackhole: a self-contained malicious routing agent that lives
 * OUTSIDE src/aodv and does not copy the AODV protocol. It reuses only the
 * PUBLIC AODV packet headers (ns3/aodv-packet.h) to speak the AODV wire
 * protocol, so it interoperates with the unmodified AODV of honest nodes.
 *
 * Behaviour:
 *   - The malicious node runs THIS Ipv4RoutingProtocol instead of AODV.
 *   - It listens on UDP port 654. For every RREQ it receives, it unicasts a
 *     forged RREP back to the previous hop, claiming a fresh one-hop route
 *     to the requested destination (destination sequence number set very
 *     high, hop count = 1). Honest AODV prefers this (higher seqno / fewer
 *     hops), so routes are attracted through the attacker.
 *   - Every transit data packet it is then asked to forward is dropped.
 *
 * This is the genuine "route-attracting" blackhole (forged RREP), which the
 * data-plane-only MaliciousAodv wrapper deliberately did NOT do. It is
 * achieved WITHOUT touching or duplicating src/aodv: the AODV protocol
 * logic of honest nodes is untouched; the attacker simply emits crafted
 * control packets and drops data.
 *
 * HONEST LIMITATIONS (state in any write-up):
 *   - The agent answers RREQs it hears directly; it does not maintain a
 *     full routing table, RERR handling, HELLO, or expanding-ring logic.
 *     It is a purpose-built attacker, not a general router. That is
 *     sufficient for a blackhole (it only needs to win route discovery and
 *     then drop).
 *   - It was reviewed statically only; ns-3.48 was not built by the author.
 *     Spots that depend on the exact aodv-packet.h API are marked
 *     NS3-VERSION (notably TypeHeader::Get()).
 */
#ifndef BLACKHOLE_AODV_H
#define BLACKHOLE_AODV_H

#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4-routing-helper.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/object-factory.h"
#include "ns3/socket.h"

#include <map>

namespace ns3
{

class BlackholeAodv : public Ipv4RoutingProtocol
{
  public:
    static TypeId GetTypeId();
    BlackholeAodv();
    ~BlackholeAodv() override;

    void SetStartTime(Time t);
    uint64_t GetForgedRreps() const;
    uint64_t GetDroppedPackets() const;
    uint64_t GetDroppedBytes() const;
    // Diagnostics for why forged_rreps may be zero:
    uint64_t GetRreqSeen() const;      ///< RREQs actually received on a socket
    uint64_t GetBindFailures() const;  ///< sockets that failed to bind

    // ── Ipv4RoutingProtocol ───────────────────────────────────────
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

  private:
    void Start();
    void RecvAodv(Ptr<Socket> socket);
    void SendForgedReply(Ptr<Socket> socket,
                         Ipv4Address requestedDst,
                         uint32_t requestedDstSeqno,
                         Ipv4Address origin,
                         Ipv4Address prevHop,
                         Ipv4Address ifaceAddr);
    bool IsLocalOrBroadcast(Ipv4Address dst) const;
    Ptr<Ipv4Route> LoopbackRoute(const Ipv4Header& header, Ptr<NetDevice> oif) const;

    Ptr<Ipv4> m_ipv4;
    // Unicast recv/send sockets (bound to the interface address) and the
    // subnet-broadcast recv sockets, mirroring aodv::RoutingProtocol. AODV
    // sends RREQ to the subnet broadcast, so without the broadcast socket
    // the attacker never hears any RREQ and forges no reply.
    std::map<Ptr<Socket>, Ipv4InterfaceAddress> m_socketAddresses;
    std::map<Ptr<Socket>, Ipv4InterfaceAddress> m_socketBroadcastAddresses;
    /// Unicast socket bound to a given interface address (for sending RREPs).
    Ptr<Socket> UnicastSocketFor(Ipv4Address ifaceAddr) const;
    Time m_startTime;
    uint64_t m_forgedRreps;
    uint64_t m_droppedPackets;
    uint64_t m_droppedBytes;
    uint64_t m_rreqSeen;
    uint64_t m_bindFailures;

    static const uint32_t AODV_PORT = 654;
    static const uint32_t FORGED_SEQNO = 0x7FFFFFFF; // very fresh
    static const uint32_t FORGED_LIFETIME_MS = 100000;
};

/**
 * Ipv4RoutingHelper for BlackholeAodv, so the active blackhole can be
 * installed as the node's routing protocol from the start (via
 * InternetStackHelper::SetRoutingHelper). This avoids ever installing AODV
 * on the attacker, which otherwise binds UDP/654 first and makes the
 * attacker's own sockets fail to bind (observed: bind_fail>0, forged=0).
 */
class BlackholeAodvHelper : public Ipv4RoutingHelper
{
  public:
    BlackholeAodvHelper();
    BlackholeAodvHelper* Copy() const override;
    Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const override;
    void Set(std::string name, const AttributeValue& value);

  private:
    ObjectFactory m_factory;
};

} // namespace ns3

#endif // BLACKHOLE_AODV_H
