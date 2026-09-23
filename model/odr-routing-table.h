#ifndef ODR_ROUTING_TABLE_H
#define ODR_ROUTING_TABLE_H

#include "odr-packet.h"

#include "ns3/ipv4-interface-address.h"
#include "ns3/ipv4-route.h"
#include "ns3/net-device.h"
#include "ns3/nstime.h"
#include "ns3/output-stream-wrapper.h"

#include <map>
#include <set>
#include <vector>

namespace ns3
{
namespace odr
{

/**
 * @ingroup odr
 * @brief Usability state of a routing table entry.
 */
enum class RouteState : uint8_t
{
    VALID,   //!< Usable for forwarding until its expiry time
    INVALID, //!< Kept only to remember the destination sequence number
};

/**
 * @ingroup odr
 * @brief One destination entry of the ODR routing table.
 *
 * Entries are value types: the table hands out copies for read access and a
 * pointer only for in-place maintenance. The Ipv4Route handed to the IP layer
 * is therefore built on demand instead of being cached inside the entry, since
 * a cached Ptr would be shared between copies and a next-hop change on one copy
 * would silently rewrite the route of the other.
 */
class RoutingTableEntry
{
  public:
    RoutingTableEntry();

    /**
     * @brief Build a usable entry.
     * @param dev output device towards the next hop
     * @param iface local interface address used as the packet source
     * @param dst destination address
     * @param nextHop neighbor to hand packets to
     * @param hopCount distance to the destination, in hops
     * @param expiry absolute simulation time at which the route stops being usable
     */
    RoutingTableEntry(Ptr<NetDevice> dev,
                      Ipv4InterfaceAddress iface,
                      Ipv4Address dst,
                      Ipv4Address nextHop,
                      uint8_t hopCount,
                      Time expiry);

    /**
     * @brief Get the destination address.
     * @return the destination
     */
    Ipv4Address GetDestination() const;

    /**
     * @brief Get the neighbor packets are handed to.
     * @return the next hop
     */
    Ipv4Address GetNextHop() const;

    /**
     * @brief Get the distance to the destination.
     * @return the hop count
     */
    uint8_t GetHopCount() const;

    /**
     * @brief Get the destination sequence number.
     * @return the sequence number; meaningful only if HasValidSeqNo()
     */
    uint32_t GetSeqNo() const;

    /**
     * @brief Record a destination sequence number and mark it as known.
     * @param seqNo the destination sequence number
     */
    void SetSeqNo(uint32_t seqNo);

    /**
     * @brief Tell whether the entry carries a trustworthy sequence number.
     *
     * Routes to neighbors learned from the IP source of a control message have
     * no sequence number; they must never be used to answer a RREQ.
     *
     * @return true if the sequence number is known
     */
    bool HasValidSeqNo() const;

    /**
     * @brief Get the state flag, regardless of the expiry time.
     * @return the route state
     */
    RouteState GetState() const;

    /**
     * @brief Tell whether the route may forward packets right now.
     * @return true if the entry is VALID and not yet expired
     */
    bool IsUsable() const;

    /**
     * @brief Get the absolute expiry time.
     *
     * For a VALID entry this is the end of its usability; for an INVALID entry
     * it is the time at which the entry is deleted.
     *
     * @return the expiry time
     */
    Time GetExpiry() const;

    /**
     * @brief Push the expiry time forward, never backward.
     * @param lifetime minimum remaining validity from now
     */
    void Refresh(Time lifetime);

    /**
     * @brief Mark the route unusable and schedule its deletion.
     * @param deletePeriod how long the sequence number must be remembered
     */
    void Invalidate(Time deletePeriod);

    /**
     * @brief Get the local interface address used to reach the next hop.
     * @return the interface address
     */
    Ipv4InterfaceAddress GetInterface() const;

    /**
     * @brief Get the output device used to reach the next hop.
     * @return the output device
     */
    Ptr<NetDevice> GetOutputDevice() const;

    /**
     * @brief Register a neighbor that forwards traffic through this route.
     * @param precursor the upstream neighbor to notify on breakage
     */
    void AddPrecursor(Ipv4Address precursor);

    /**
     * @brief Get the upstream neighbors that rely on this route.
     * @return the precursor set, in address order
     */
    const std::set<Ipv4Address>& GetPrecursors() const;

    /**
     * @brief Build the route object expected by the IP layer.
     * @return a freshly allocated Ipv4Route
     */
    Ptr<Ipv4Route> GetRoute() const;

    /**
     * @brief Print the entry on one line.
     * @param os output stream
     * @param unit time unit for the expiry column
     */
    void Print(std::ostream& os, Time::Unit unit) const;

  private:
    friend class RoutingTable;

    Ptr<NetDevice> m_device;            //!< Output device
    Ipv4InterfaceAddress m_iface;       //!< Local interface address
    Ipv4Address m_dst;                  //!< Destination
    Ipv4Address m_nextHop;              //!< Next hop neighbor
    uint8_t m_hopCount;                 //!< Distance in hops
    uint32_t m_seqNo;                   //!< Destination sequence number
    bool m_validSeqNo;                  //!< Whether m_seqNo is known
    RouteState m_state;                 //!< Usability flag
    Time m_expiry;                      //!< Absolute expiry or deletion time
    std::set<Ipv4Address> m_precursors; //!< Upstream neighbors using this route
};

/**
 * @ingroup odr
 * @brief Destination-indexed routing table with lazy expiry.
 *
 * Expiry is evaluated on access rather than with one timer per entry: a
 * reactive protocol with N destinations would otherwise keep O(N) pending
 * events in the scheduler for routes that are mostly never looked up again.
 *
 * The container is an ordered map on purpose. Invalidation walks the table
 * and emits RERRs in iteration order; a hash map would make that order, and
 * hence the event sequence, depend on the standard library implementation and
 * break run-to-run reproducibility for a given RngRun.
 */
class RoutingTable
{
  public:
    /**
     * @brief Build an empty table.
     * @param deletePeriod retention of invalidated entries (RFC 3561 DELETE_PERIOD)
     */
    explicit RoutingTable(Time deletePeriod = Seconds(15));

    /**
     * @brief Change the retention time of invalidated entries.
     * @param deletePeriod the new retention time
     */
    void SetDeletePeriod(Time deletePeriod);

    /**
     * @brief Find the entry for a destination, whatever its state.
     * @param dst destination address
     * @param entry receives a copy of the entry when found
     * @return true if an entry exists
     */
    bool Lookup(Ipv4Address dst, RoutingTableEntry& entry) const;

    /**
     * @brief Find a route that may forward packets right now.
     * @param dst destination address
     * @param entry receives a copy of the entry when found
     * @return true if a usable route exists
     */
    bool LookupUsable(Ipv4Address dst, RoutingTableEntry& entry) const;

    /**
     * @brief Get mutable access to an entry for in-place maintenance.
     * @param dst destination address
     * @return the entry, or nullptr; dangling once the entry is deleted (Purge(),
     *         DeleteRoutesOnInterface(), Clear()), stable across insertions
     */
    RoutingTableEntry* Find(Ipv4Address dst);

    /**
     * @brief Offer a route carrying a destination sequence number.
     *
     * Applies the RFC 3561 section 6.2 replacement rule: the candidate wins if
     * the current entry has no known sequence number, if the candidate's number
     * is fresher, or if both are equal and the current entry is unusable or
     * longer. Refusing an equal-number longer route, and any older number, is
     * what guarantees loop freedom.
     *
     * @param candidate the proposed route; must have a valid sequence number
     * @return true if the table now holds the candidate
     */
    bool Update(const RoutingTableEntry& candidate);

    /**
     * @brief Install or refresh a one-hop route to a neighbor heard directly.
     *
     * The neighbor's sequence number is unknown at this point, so a previously
     * learned number is preserved rather than overwritten.
     *
     * @param dev output device towards the neighbor
     * @param iface local interface on which the neighbor was heard
     * @param neighbor neighbor address
     * @param lifetime minimum remaining validity
     * @return true if the neighbor was not reachable through a usable route before
     */
    bool RefreshNeighbor(Ptr<NetDevice> dev,
                         Ipv4InterfaceAddress iface,
                         Ipv4Address neighbor,
                         Time lifetime);

    /**
     * @brief Invalidate every VALID route whose next hop just became unreachable.
     *
     * Implements RFC 3561 section 6.11 case (i): each affected destination
     * sequence number is incremented so that stale replies advertising the
     * broken path are rejected by Update().
     *
     * @param nextHop the neighbor that stopped acknowledging frames
     * @param precursors receives the upstream neighbors to warn
     * @return the destinations to list in the RERR
     */
    std::vector<UnreachableDestination> InvalidateByNextHop(Ipv4Address nextHop,
                                                            std::set<Ipv4Address>& precursors);

    /**
     * @brief Apply a received RERR.
     *
     * Only routes whose next hop is the RERR transmitter are affected: a RERR
     * describes the sender's view, and a route through another neighbor may
     * still be intact.
     *
     * @param sender IP source of the RERR
     * @param unreachable destinations listed in the RERR
     * @param precursors receives the upstream neighbors to warn
     * @return the subset of destinations to propagate further
     */
    std::vector<UnreachableDestination> InvalidateReported(
        Ipv4Address sender,
        const std::vector<UnreachableDestination>& unreachable,
        std::set<Ipv4Address>& precursors);

    /**
     * @brief Expire VALID routes past their lifetime and delete stale INVALID ones.
     */
    void Purge();

    /**
     * @brief Drop every route that leaves through the given interface.
     * @param iface the interface address being removed
     */
    void DeleteRoutesOnInterface(Ipv4InterfaceAddress iface);

    /**
     * @brief Remove every entry.
     */
    void Clear();

    /**
     * @brief Get the number of entries, whatever their state.
     * @return the table size
     */
    std::size_t GetSize() const;

    /**
     * @brief Print the whole table.
     * @param stream output stream
     * @param unit time unit for the expiry column
     */
    void Print(Ptr<OutputStreamWrapper> stream, Time::Unit unit) const;

  private:
    std::map<Ipv4Address, RoutingTableEntry> m_entries; //!< Entries by destination
    Time m_deletePeriod;                                //!< Retention of INVALID entries
};

} // namespace odr
} // namespace ns3

#endif /* ODR_ROUTING_TABLE_H */
