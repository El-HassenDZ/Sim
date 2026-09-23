#include "odr-routing-table.h"

#include "ns3/log.h"
#include "ns3/simulator.h"

#include <iomanip>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("OdrRoutingTable");

namespace odr
{

RoutingTableEntry::RoutingTableEntry()
    : m_hopCount(0),
      m_seqNo(0),
      m_validSeqNo(false),
      m_state(RouteState::INVALID)
{
}

RoutingTableEntry::RoutingTableEntry(Ptr<NetDevice> dev,
                                     Ipv4InterfaceAddress iface,
                                     Ipv4Address dst,
                                     Ipv4Address nextHop,
                                     uint8_t hopCount,
                                     Time expiry)
    : m_device(dev),
      m_iface(iface),
      m_dst(dst),
      m_nextHop(nextHop),
      m_hopCount(hopCount),
      m_seqNo(0),
      m_validSeqNo(false),
      m_state(RouteState::VALID),
      m_expiry(expiry)
{
}

Ipv4Address
RoutingTableEntry::GetDestination() const
{
    return m_dst;
}

Ipv4Address
RoutingTableEntry::GetNextHop() const
{
    return m_nextHop;
}

uint8_t
RoutingTableEntry::GetHopCount() const
{
    return m_hopCount;
}

uint32_t
RoutingTableEntry::GetSeqNo() const
{
    return m_seqNo;
}

void
RoutingTableEntry::SetSeqNo(uint32_t seqNo)
{
    m_seqNo = seqNo;
    m_validSeqNo = true;
}

bool
RoutingTableEntry::HasValidSeqNo() const
{
    return m_validSeqNo;
}

RouteState
RoutingTableEntry::GetState() const
{
    return m_state;
}

bool
RoutingTableEntry::IsUsable() const
{
    return m_state == RouteState::VALID && m_expiry > Simulator::Now();
}

Time
RoutingTableEntry::GetExpiry() const
{
    return m_expiry;
}

void
RoutingTableEntry::Refresh(Time lifetime)
{
    NS_ASSERT_MSG(m_state == RouteState::VALID, "Refreshing an invalidated route to " << m_dst);
    m_expiry = std::max(m_expiry, Simulator::Now() + lifetime);
}

void
RoutingTableEntry::Invalidate(Time deletePeriod)
{
    m_state = RouteState::INVALID;
    m_expiry = Simulator::Now() + deletePeriod;
}

Ipv4InterfaceAddress
RoutingTableEntry::GetInterface() const
{
    return m_iface;
}

Ptr<NetDevice>
RoutingTableEntry::GetOutputDevice() const
{
    return m_device;
}

void
RoutingTableEntry::AddPrecursor(Ipv4Address precursor)
{
    m_precursors.insert(precursor);
}

const std::set<Ipv4Address>&
RoutingTableEntry::GetPrecursors() const
{
    return m_precursors;
}

Ptr<Ipv4Route>
RoutingTableEntry::GetRoute() const
{
    Ptr<Ipv4Route> route = Create<Ipv4Route>();
    route->SetDestination(m_dst);
    route->SetGateway(m_nextHop);
    route->SetSource(m_iface.GetLocal());
    route->SetOutputDevice(m_device);
    return route;
}

void
RoutingTableEntry::Print(std::ostream& os, Time::Unit unit) const
{
    std::ostringstream expiry;
    expiry << std::setprecision(3) << (m_expiry - Simulator::Now()).As(unit);
    os << std::left << std::setw(16) << m_dst << std::setw(16) << m_nextHop << std::setw(16)
       << m_iface.GetLocal() << std::setw(9) << (m_state == RouteState::VALID ? "VALID" : "INVALID")
       << std::setw(6) << static_cast<uint32_t>(m_hopCount) << std::setw(11)
       << (m_validSeqNo ? std::to_string(m_seqNo) : std::string("-")) << expiry.str() << "\n";
}

RoutingTable::RoutingTable(Time deletePeriod)
    : m_deletePeriod(deletePeriod)
{
}

void
RoutingTable::SetDeletePeriod(Time deletePeriod)
{
    m_deletePeriod = deletePeriod;
}

bool
RoutingTable::Lookup(Ipv4Address dst, RoutingTableEntry& entry) const
{
    auto it = m_entries.find(dst);
    if (it == m_entries.end())
    {
        return false;
    }
    entry = it->second;
    return true;
}

bool
RoutingTable::LookupUsable(Ipv4Address dst, RoutingTableEntry& entry) const
{
    auto it = m_entries.find(dst);
    if (it == m_entries.end() || !it->second.IsUsable())
    {
        return false;
    }
    entry = it->second;
    return true;
}

RoutingTableEntry*
RoutingTable::Find(Ipv4Address dst)
{
    auto it = m_entries.find(dst);
    return it == m_entries.end() ? nullptr : &it->second;
}

bool
RoutingTable::Update(const RoutingTableEntry& candidate)
{
    NS_ASSERT_MSG(candidate.HasValidSeqNo(),
                  "Only routes backed by a sequence number may compete for " << candidate.m_dst);
    auto it = m_entries.find(candidate.m_dst);
    if (it == m_entries.end())
    {
        m_entries.emplace(candidate.m_dst, candidate);
        return true;
    }

    RoutingTableEntry& current = it->second;
    bool accept = !current.m_validSeqNo || IsFresher(candidate.m_seqNo, current.m_seqNo) ||
                  (candidate.m_seqNo == current.m_seqNo &&
                   (!current.IsUsable() || candidate.m_hopCount < current.m_hopCount));
    if (!accept)
    {
        NS_LOG_LOGIC("Keep route to " << current.m_dst << " seq " << current.m_seqNo << " hops "
                                      << +current.m_hopCount << ", reject seq " << candidate.m_seqNo
                                      << " hops " << +candidate.m_hopCount);
        return false;
    }

    // An INVALID entry's expiry is its deletion time, not a validity bound, so
    // it must not leak into the lifetime of the route replacing it.
    Time expiry =
        current.IsUsable() ? std::max(current.m_expiry, candidate.m_expiry) : candidate.m_expiry;
    // Precursors are kept across a next-hop change: upstream neighbors still
    // forward through this node and must hear about a later breakage.
    std::set<Ipv4Address> precursors = std::move(current.m_precursors);
    current = candidate;
    current.m_expiry = expiry;
    current.m_precursors.insert(precursors.begin(), precursors.end());
    return true;
}

bool
RoutingTable::RefreshNeighbor(Ptr<NetDevice> dev,
                              Ipv4InterfaceAddress iface,
                              Ipv4Address neighbor,
                              Time lifetime)
{
    auto it = m_entries.find(neighbor);
    if (it == m_entries.end())
    {
        m_entries.emplace(
            neighbor,
            RoutingTableEntry(dev, iface, neighbor, neighbor, 1, Simulator::Now() + lifetime));
        return true;
    }

    RoutingTableEntry& entry = it->second;
    bool wasUsable = entry.IsUsable();
    if (wasUsable && entry.m_nextHop == neighbor && entry.m_hopCount == 1)
    {
        entry.Refresh(lifetime);
        return false;
    }
    // Switching a multi-hop route to the direct link cannot create a loop: the
    // destination itself is the next hop. The sequence number is left as is,
    // since hearing a neighbor says nothing about its freshness.
    entry.m_device = dev;
    entry.m_iface = iface;
    entry.m_nextHop = neighbor;
    entry.m_hopCount = 1;
    entry.m_expiry = wasUsable ? std::max(entry.m_expiry, Simulator::Now() + lifetime)
                               : Simulator::Now() + lifetime;
    entry.m_state = RouteState::VALID;
    return !wasUsable;
}

std::vector<UnreachableDestination>
RoutingTable::InvalidateByNextHop(Ipv4Address nextHop, std::set<Ipv4Address>& precursors)
{
    std::vector<UnreachableDestination> unreachable;
    for (auto& [dst, entry] : m_entries)
    {
        if (entry.m_state != RouteState::VALID || entry.m_nextHop != nextHop)
        {
            continue;
        }
        if (entry.m_validSeqNo)
        {
            ++entry.m_seqNo;
        }
        entry.Invalidate(m_deletePeriod);
        unreachable.push_back({dst, entry.m_seqNo});
        precursors.insert(entry.m_precursors.begin(), entry.m_precursors.end());
        entry.m_precursors.clear();
    }
    return unreachable;
}

std::vector<UnreachableDestination>
RoutingTable::InvalidateReported(Ipv4Address sender,
                                 const std::vector<UnreachableDestination>& unreachable,
                                 std::set<Ipv4Address>& precursors)
{
    std::vector<UnreachableDestination> propagated;
    for (const auto& reported : unreachable)
    {
        auto it = m_entries.find(reported.address);
        if (it == m_entries.end())
        {
            continue;
        }
        RoutingTableEntry& entry = it->second;
        if (entry.m_state != RouteState::VALID || entry.m_nextHop != sender)
        {
            continue;
        }
        // RFC 3561 copies the reported number verbatim. Taking the fresher of
        // the two instead keeps the per-destination number monotonic even when
        // the reporter never knew one and sends 0, which a verbatim copy would
        // turn into a sequence number regression.
        if (!entry.m_validSeqNo || IsFresher(reported.seqNo, entry.m_seqNo))
        {
            entry.SetSeqNo(reported.seqNo);
        }
        entry.Invalidate(m_deletePeriod);
        propagated.push_back({reported.address, entry.m_seqNo});
        precursors.insert(entry.m_precursors.begin(), entry.m_precursors.end());
        entry.m_precursors.clear();
    }
    return propagated;
}

void
RoutingTable::Purge()
{
    Time now = Simulator::Now();
    for (auto it = m_entries.begin(); it != m_entries.end();)
    {
        RoutingTableEntry& entry = it->second;
        if (entry.m_expiry > now)
        {
            ++it;
            continue;
        }
        if (entry.m_state == RouteState::VALID)
        {
            // Retention is anchored on the actual expiry instant so that the
            // deletion time does not depend on when Purge() happens to run.
            entry.m_state = RouteState::INVALID;
            entry.m_expiry += m_deletePeriod;
            entry.m_precursors.clear();
            if (entry.m_expiry > now)
            {
                ++it;
                continue;
            }
        }
        NS_LOG_LOGIC("Delete stale entry for " << it->first);
        it = m_entries.erase(it);
    }
}

void
RoutingTable::DeleteRoutesOnInterface(Ipv4InterfaceAddress iface)
{
    for (auto it = m_entries.begin(); it != m_entries.end();)
    {
        if (it->second.m_iface == iface)
        {
            it = m_entries.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void
RoutingTable::Clear()
{
    m_entries.clear();
}

std::size_t
RoutingTable::GetSize() const
{
    return m_entries.size();
}

void
RoutingTable::Print(Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
    std::ostream& os = *stream->GetStream();
    os << std::left << std::setw(16) << "Destination" << std::setw(16) << "Gateway" << std::setw(16)
       << "Interface" << std::setw(9) << "State" << std::setw(6) << "Hops" << std::setw(11)
       << "SeqNo"
       << "Expiry\n";
    for (const auto& [dst, entry] : m_entries)
    {
        entry.Print(os, unit);
    }
}

} // namespace odr
} // namespace ns3
