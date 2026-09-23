#include "odr-request-cache.h"

#include "ns3/simulator.h"

namespace ns3
{
namespace odr
{

RequestCache::RequestCache(Time lifetime)
    : m_lifetime(lifetime)
{
}

void
RequestCache::SetLifetime(Time lifetime)
{
    m_lifetime = lifetime;
}

bool
RequestCache::IsDuplicate(Ipv4Address origin, uint32_t requestId)
{
    Purge();
    Key key{origin, requestId};
    if (m_members.count(key) != 0)
    {
        return true;
    }
    m_members.insert(key);
    m_expiryQueue.emplace_back(key, Simulator::Now() + m_lifetime);
    return false;
}

std::size_t
RequestCache::GetSize()
{
    Purge();
    return m_members.size();
}

void
RequestCache::Clear()
{
    m_expiryQueue.clear();
    m_members.clear();
}

void
RequestCache::Purge()
{
    Time now = Simulator::Now();
    while (!m_expiryQueue.empty() && m_expiryQueue.front().second <= now)
    {
        m_members.erase(m_expiryQueue.front().first);
        m_expiryQueue.pop_front();
    }
}

} // namespace odr
} // namespace ns3
