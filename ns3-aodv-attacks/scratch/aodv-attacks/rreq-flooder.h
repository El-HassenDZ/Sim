/*
 * rreq-flooder.h
 * --------------
 * RREQ-flooding attack, implemented WITHOUT modifying src/aodv.
 *
 * The attacker sends small UDP datagrams to a stream of DISTINCT
 * destination addresses that have no active route. Each new destination
 * makes the node's own AODV issue a fresh route-discovery RREQ, which is
 * broadcast network-wide. At a high enough rate this floods the control
 * plane and inflates routing overhead — the effect of an RREQ flood —
 * while the attack code itself only sends application traffic.
 *
 * HONEST NOTE: this induces genuine RREQs through the standard AODV state
 * machine (it does not fabricate RREQ packets on the wire). The flood
 * intensity is bounded by AODV's own rate limiting (RreqRateLimit,
 * default 10 RREQ/s) unless that attribute is raised on the attacker.
 * The scenario raises it on flooder nodes so the attack is observable;
 * this is documented in the README.
 */
#ifndef RREQ_FLOODER_H
#define RREQ_FLOODER_H

#include "ns3/application.h"
#include "ns3/ipv4-address.h"
#include "ns3/ptr.h"
#include "ns3/random-variable-stream.h"
#include "ns3/socket.h"

namespace ns3
{

class RreqFlooder : public Application
{
  public:
    static TypeId GetTypeId();
    RreqFlooder();
    ~RreqFlooder() override;

    uint64_t GetSentProbes() const;

  private:
    void StartApplication() override;
    void StopApplication() override;
    void SendProbe();

    Ptr<Socket> m_socket;
    Ipv4Address m_base;        ///< base network for fabricated destinations
    uint32_t m_firstHost;      ///< first (likely unassigned) host id
    uint32_t m_hostRange;      ///< number of distinct destinations to cycle
    uint32_t m_next;           ///< running host-id offset
    Time m_interval;           ///< time between probes
    uint16_t m_port;
    uint32_t m_pktSize;
    uint64_t m_sent;
    EventId m_sendEvent;
    bool m_running;
};

} // namespace ns3

#endif // RREQ_FLOODER_H
