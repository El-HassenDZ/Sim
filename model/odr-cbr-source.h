#ifndef ODR_CBR_SOURCE_H
#define ODR_CBR_SOURCE_H

#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/nstime.h"
#include "ns3/socket.h"

namespace ns3
{
namespace odr
{

/**
 * @ingroup odr
 * @brief Constant-bit-rate UDP source that accounts for every scheduled packet.
 *
 * OnOffApplication and UdpClient count only the packets the socket accepts.
 * A proactive protocol without a route makes RouteOutput() fail, so the
 * socket refuses the packet before it reaches IP: those packets are invisible
 * to FlowMonitor, and the packet delivery ratio of proactive protocols is
 * overestimated by exactly the traffic they could not route. Reactive
 * protocols never refuse (they park the packet), so the bias also distorts
 * every reactive/proactive comparison.
 *
 * This source emits one packet per interval from its start time, whether or
 * not the previous one was accepted, and keeps three counters:
 * attempted = accepted + refused. The attempted count is the offered load to
 * use as the delivery ratio denominator.
 */
class CbrSource : public Application
{
  public:
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    CbrSource();
    ~CbrSource() override;

    /**
     * @brief Get the number of packets scheduled since the start.
     * @return accepted plus refused packets
     */
    uint64_t GetAttempted() const;

    /**
     * @brief Get the number of packets accepted by the UDP socket.
     * @return packets handed to IP; should match FlowMonitor's txPackets
     */
    uint64_t GetAccepted() const;

    /**
     * @brief Get the number of packets the socket refused.
     * @return packets lost at the source, typically for lack of a route
     */
    uint64_t GetRefused() const;

  protected:
    void DoDispose() override;

  private:
    void StartApplication() override;
    void StopApplication() override;

    /**
     * @brief Emit one packet and schedule the next one.
     */
    void SendPacket();

    Address m_remote;      //!< Destination address and port
    uint32_t m_packetSize; //!< UDP payload size
    Time m_interval;       //!< Time between two packets
    Ptr<Socket> m_socket;  //!< Connected UDP socket
    EventId m_sendEvent;   //!< Next emission
    uint64_t m_attempted;  //!< Packets scheduled
    uint64_t m_accepted;   //!< Packets accepted by the socket
    uint64_t m_refused;    //!< Packets refused by the socket
};

} // namespace odr
} // namespace ns3

#endif /* ODR_CBR_SOURCE_H */
