/*
 * malicious-aodv.h
 * ----------------
 * Data-plane blackhole / grayhole attack for ns-3, implemented WITHOUT
 * modifying src/aodv.
 *
 * MaliciousAodv is an Ipv4RoutingProtocol that WRAPS the node's real
 * aodv::RoutingProtocol. Every control-plane and routing decision is
 * delegated to the inner AODV object, so the malicious node participates
 * fully in route discovery (it broadcasts/forwards RREQ, answers RREP,
 * keeps a normal routing table). Only transit UNICAST DATA packets that
 * AODV would forward are intercepted:
 *   - blackhole : every forwarded data packet is dropped;
 *   - grayhole  : forwarded data packets are dropped with probability p.
 *
 * Local delivery, broadcast and multicast (this covers AODV control
 * traffic on UDP port 654) are always delegated untouched, so the node
 * stays on the topology and attracts routes the normal AODV way.
 *
 * HONEST LIMITATION: this does NOT forge RREPs with an inflated sequence
 * number to *actively* attract traffic. That behaviour requires editing
 * src/aodv, which this project deliberately does not do. What is modelled
 * here is the widely used "packet-dropping" blackhole/grayhole on the
 * forwarding path. State this in any write-up.
 */
#ifndef MALICIOUS_AODV_H
#define MALICIOUS_AODV_H

#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4.h"
#include "ns3/random-variable-stream.h"

namespace ns3
{

class MaliciousAodv : public Ipv4RoutingProtocol
{
  public:
    enum Mode
    {
        BLACKHOLE,
        GRAYHOLE
    };

    static TypeId GetTypeId();
    MaliciousAodv();
    ~MaliciousAodv() override;

    /// The real aodv::RoutingProtocol already installed on the node.
    void SetInner(Ptr<Ipv4RoutingProtocol> inner);
    void SetMode(Mode mode);
    void SetGrayholeProb(double p);      ///< drop probability for GRAYHOLE
    void SetStartTime(Time t);           ///< attack is inert before this time

    uint64_t GetDroppedPackets() const;
    uint64_t GetDroppedBytes() const;

    // ── Ipv4RoutingProtocol interface ─────────────────────────────
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
    /// True if dst is delivered locally / is broadcast / multicast, i.e.
    /// must never be dropped (covers AODV control traffic).
    bool IsLocalOrBroadcast(Ipv4Address dst) const;

    Ptr<Ipv4RoutingProtocol> m_inner;
    Ptr<Ipv4> m_ipv4;
    Mode m_mode;
    double m_grayholeProb;
    Time m_startTime;
    Ptr<UniformRandomVariable> m_rand;
    uint64_t m_droppedPackets;
    uint64_t m_droppedBytes;
};

} // namespace ns3

#endif // MALICIOUS_AODV_H
