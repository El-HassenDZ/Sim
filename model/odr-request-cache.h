#ifndef ODR_REQUEST_CACHE_H
#define ODR_REQUEST_CACHE_H

#include "ns3/ipv4-address.h"
#include "ns3/nstime.h"

#include <deque>
#include <set>
#include <utility>

namespace ns3
{
namespace odr
{

/**
 * @ingroup odr
 * @brief Memory of recently processed RREQ floods.
 *
 * Every node rebroadcasts a given flood at most once; without this cache a
 * single RREQ would circulate until its TTL expired on every path of the
 * network, i.e. exponentially many transmissions in the network diameter.
 *
 * All records share the same lifetime, so insertion order equals expiry order.
 * A FIFO of records plus an ordered set for membership gives amortized O(1)
 * expiry and O(log n) lookup without scanning the whole cache on each RREQ.
 */
class RequestCache
{
  public:
    /**
     * @brief Build an empty cache.
     * @param lifetime how long a flood is remembered (RFC 3561 PATH_DISCOVERY_TIME)
     */
    explicit RequestCache(Time lifetime = Seconds(5.6));

    /**
     * @brief Change the lifetime of records inserted from now on.
     *
     * Records already present keep their expiry. A shorter lifetime can then
     * leave a few records alive slightly longer than requested, which only
     * errs on the side of suppressing more duplicates.
     *
     * @param lifetime the new lifetime
     */
    void SetLifetime(Time lifetime);

    /**
     * @brief Test a flood identifier and remember it when first seen.
     * @param origin RREQ originator
     * @param requestId RREQ identifier
     * @return true if this flood was already processed and must be dropped
     */
    bool IsDuplicate(Ipv4Address origin, uint32_t requestId);

    /**
     * @brief Get the number of live records.
     * @return the cache size after expiring old records
     */
    std::size_t GetSize();

    /**
     * @brief Forget every flood.
     */
    void Clear();

  private:
    /// Flood identifier: originator address and request id.
    using Key = std::pair<Ipv4Address, uint32_t>;

    /**
     * @brief Drop the records whose lifetime has elapsed.
     */
    void Purge();

    std::deque<std::pair<Key, Time>> m_expiryQueue; //!< Records in expiry order
    std::set<Key> m_members;                        //!< Live records, for lookup
    Time m_lifetime;                                //!< Lifetime of new records
};

} // namespace odr
} // namespace ns3

#endif /* ODR_REQUEST_CACHE_H */
