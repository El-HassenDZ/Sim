#ifndef ODR_CONTROL_TRAFFIC_MONITOR_H
#define ODR_CONTROL_TRAFFIC_MONITOR_H

#include "ns3/ipv4.h"
#include "ns3/node-container.h"
#include "ns3/object.h"
#include "ns3/packet.h"

#include <map>
#include <set>
#include <string>

namespace ns3
{
namespace odr
{

/**
 * @ingroup odr
 * @brief Counts routing control transmissions of any UDP-based protocol.
 *
 * FlowMonitor only follows unicast packets (Ipv4FlowProbe discards anything
 * else), so the RREQ floods, HELLO beacons and broadcast RERRs that make up
 * most of a MANET routing overhead are invisible to it. This monitor hooks
 * the Ipv4L3Protocol "Tx" trace instead, which fires once per packet and per
 * outgoing interface, broadcasts and relayed packets included, and counts the
 * UDP packets whose source or destination port belongs to the configured
 * control ports. The same counting rule therefore applies to ODR, AODV, OLSR
 * and DSDV, which is what a normalized routing load comparison requires.
 *
 * Counted bytes are IPv4 packet sizes (IP and UDP headers included); MAC
 * headers, acknowledgements and retransmissions are not counted.
 */
class ControlTrafficMonitor : public Object
{
  public:
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    ControlTrafficMonitor();

    /**
     * @brief Declare a UDP port used by routing control messages.
     * @param port the port, matched against both source and destination
     */
    void AddPort(uint16_t port);

    /**
     * @brief Start counting on the given nodes.
     *
     * The traces keep a reference to the monitor, so it stays alive for as
     * long as the nodes do, even if the caller drops its own reference.
     *
     * @param nodes nodes with an IPv4 stack installed
     */
    void Install(NodeContainer nodes);

    /**
     * @brief Get the number of control packets transmitted by all nodes.
     * @return the packet count
     */
    uint64_t GetTotalPackets() const;

    /**
     * @brief Get the number of control bytes transmitted by all nodes.
     * @return the byte count, IPv4 and UDP headers included
     */
    uint64_t GetTotalBytes() const;

    /**
     * @brief Write one CSV row per installed node: node, packets, bytes.
     * @param filename output path; the file is overwritten
     */
    void WriteCsv(const std::string& filename) const;

  private:
    /// Counters of one node.
    struct Counters
    {
        uint64_t packets{0}; //!< Control packets transmitted
        uint64_t bytes{0};   //!< Control bytes transmitted
    };

    /**
     * @brief Classify one transmitted IPv4 packet.
     * @param nodeId node the trace belongs to
     * @param packet the packet, IPv4 header included
     * @param ipv4 the IPv4 stack
     * @param interface outgoing interface index
     */
    void NotifyTx(uint32_t nodeId, Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface);

    std::set<uint16_t> m_ports;             //!< Control ports
    std::map<uint32_t, Counters> m_perNode; //!< Counters by node id
};

} // namespace odr
} // namespace ns3

#endif /* ODR_CONTROL_TRAFFIC_MONITOR_H */
