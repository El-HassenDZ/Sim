#ifndef ODR_PACKET_H
#define ODR_PACKET_H

#include "ns3/header.h"
#include "ns3/ipv4-address.h"
#include "ns3/nstime.h"

#include <vector>

namespace ns3
{
namespace odr
{

/**
 * @ingroup odr
 * @brief Discriminator placed in front of every ODR control message.
 */
enum MessageType : uint8_t
{
    ODR_RREQ = 1, //!< Route request, flooded with an expanding TTL
    ODR_RREP = 2, //!< Route reply, unicast hop by hop towards the originator
    ODR_RERR = 3, //!< Route error, sent to the precursors of broken routes
};

/**
 * @ingroup odr
 * @brief One-byte header identifying the ODR message body that follows.
 *
 * The type travels in its own header so that the receive path can reject a
 * foreign or corrupted datagram on the ODR port before committing to a body
 * layout. The 1 + body byte split reproduces the on-air sizes of RFC 3561,
 * which keeps byte-level routing overhead directly comparable with AODV.
 */
class TypeHeader : public Header
{
  public:
    /**
     * @brief Build a header announcing the given message type.
     * @param type the ODR message type
     */
    explicit TypeHeader(MessageType type = ODR_RREQ);

    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
    void Print(std::ostream& os) const override;

    /**
     * @brief Get the announced message type.
     * @return the message type; meaningful only if IsValid() is true
     */
    MessageType GetType() const;

    /**
     * @brief Tell whether the deserialized byte matched a known message type.
     * @return false if the datagram must be discarded
     */
    bool IsValid() const;

  private:
    MessageType m_type; //!< Message type
    bool m_valid;       //!< False after deserializing an unknown type byte
};

/**
 * @ingroup odr
 * @brief Route request (RREQ) body, 23 bytes on air.
 *
 * Layout: flags (1), reserved (1), hop count (1), request id (4),
 * destination (4), destination sequence number (4), originator (4),
 * originator sequence number (4). The (originator, request id) pair is the
 * flood identifier used for duplicate suppression.
 */
class RreqHeader : public Header
{
  public:
    RreqHeader();

    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
    void Print(std::ostream& os) const override;

    /**
     * @brief Set the number of hops travelled from the originator.
     * @param hopCount hops travelled so far
     */
    void SetHopCount(uint8_t hopCount);

    /**
     * @brief Get the number of hops travelled from the originator.
     * @return the hop count
     */
    uint8_t GetHopCount() const;

    /**
     * @brief Set the per-originator request identifier.
     * @param id the request identifier
     */
    void SetRequestId(uint32_t id);

    /**
     * @brief Get the per-originator request identifier.
     * @return the request identifier
     */
    uint32_t GetRequestId() const;

    /**
     * @brief Set the address being searched for.
     * @param dst the destination address
     */
    void SetDestination(Ipv4Address dst);

    /**
     * @brief Get the address being searched for.
     * @return the destination address
     */
    Ipv4Address GetDestination() const;

    /**
     * @brief Set the freshest destination sequence number known on the path.
     * @param seqNo the destination sequence number
     */
    void SetDestinationSeqNo(uint32_t seqNo);

    /**
     * @brief Get the freshest destination sequence number known on the path.
     * @return the destination sequence number; ignore it if IsUnknownSeqNo()
     */
    uint32_t GetDestinationSeqNo() const;

    /**
     * @brief Set the node that started the discovery.
     * @param origin the originator address
     */
    void SetOrigin(Ipv4Address origin);

    /**
     * @brief Get the node that started the discovery.
     * @return the originator address
     */
    Ipv4Address GetOrigin() const;

    /**
     * @brief Set the originator's own sequence number at flood time.
     * @param seqNo the originator sequence number
     */
    void SetOriginSeqNo(uint32_t seqNo);

    /**
     * @brief Get the originator's own sequence number at flood time.
     * @return the originator sequence number
     */
    uint32_t GetOriginSeqNo() const;

    /**
     * @brief Forbid intermediate nodes from answering from their cache.
     * @param destinationOnly true to require the destination itself to reply
     */
    void SetDestinationOnly(bool destinationOnly);

    /**
     * @brief Tell whether only the destination may answer.
     * @return true if the D flag is set
     */
    bool IsDestinationOnly() const;

    /**
     * @brief Declare that the originator holds no sequence number for the
     *        destination.
     * @param unknown true to set the U flag
     */
    void SetUnknownSeqNo(bool unknown);

    /**
     * @brief Tell whether the destination sequence number field is void.
     * @return true if the U flag is set
     */
    bool IsUnknownSeqNo() const;

  private:
    uint8_t m_flags;        //!< D and U flags, RFC 3561 bit positions
    uint8_t m_hopCount;     //!< Hops travelled from the originator
    uint32_t m_requestId;   //!< Flood identifier, unique per originator
    Ipv4Address m_dst;      //!< Destination searched for
    uint32_t m_dstSeqNo;    //!< Freshest known destination sequence number
    Ipv4Address m_origin;   //!< Discovery originator
    uint32_t m_originSeqNo; //!< Originator sequence number
};

/**
 * @ingroup odr
 * @brief Route reply (RREP) body, 19 bytes on air.
 *
 * Layout: reserved (2), hop count (1), destination (4), destination sequence
 * number (4), originator (4), lifetime in milliseconds (4). The lifetime is
 * the validity granted by the replying node and is propagated unchanged, so
 * every node on the path expires the route at roughly the same instant.
 */
class RrepHeader : public Header
{
  public:
    RrepHeader();

    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
    void Print(std::ostream& os) const override;

    /**
     * @brief Set the distance, in hops, between the sender and the destination.
     * @param hopCount the hop count
     */
    void SetHopCount(uint8_t hopCount);

    /**
     * @brief Get the distance, in hops, between the sender and the destination.
     * @return the hop count
     */
    uint8_t GetHopCount() const;

    /**
     * @brief Set the destination the reply advertises a route to.
     * @param dst the destination address
     */
    void SetDestination(Ipv4Address dst);

    /**
     * @brief Get the destination the reply advertises a route to.
     * @return the destination address
     */
    Ipv4Address GetDestination() const;

    /**
     * @brief Set the destination sequence number backing the route.
     * @param seqNo the destination sequence number
     */
    void SetDestinationSeqNo(uint32_t seqNo);

    /**
     * @brief Get the destination sequence number backing the route.
     * @return the destination sequence number
     */
    uint32_t GetDestinationSeqNo() const;

    /**
     * @brief Set the originator of the discovery this reply answers.
     * @param origin the originator address
     */
    void SetOrigin(Ipv4Address origin);

    /**
     * @brief Get the originator of the discovery this reply answers.
     * @return the originator address
     */
    Ipv4Address GetOrigin() const;

    /**
     * @brief Set the route validity granted by the replying node.
     * @param lifetime the lifetime, truncated to whole milliseconds on air
     */
    void SetLifetime(Time lifetime);

    /**
     * @brief Get the route validity granted by the replying node.
     * @return the lifetime
     */
    Time GetLifetime() const;

  private:
    uint8_t m_hopCount;    //!< Hops between the sender and the destination
    Ipv4Address m_dst;     //!< Advertised destination
    uint32_t m_dstSeqNo;   //!< Destination sequence number
    Ipv4Address m_origin;  //!< Discovery originator
    uint32_t m_lifetimeMs; //!< Granted validity, in milliseconds
};

/**
 * @ingroup odr
 * @brief A destination that became unreachable, as listed in a RERR.
 */
struct UnreachableDestination
{
    Ipv4Address address; //!< Destination that can no longer be reached
    uint32_t seqNo;      //!< Destination sequence number after invalidation
};

/**
 * @ingroup odr
 * @brief Route error (RERR) body, 3 + 8n bytes on air.
 *
 * Layout: flags (1), reserved (1), destination count (1), then n pairs
 * (address, sequence number). The one-byte count caps a single message at
 * 255 destinations; the sender splits larger invalidations.
 */
class RerrHeader : public Header
{
  public:
    /// Largest number of destinations a single RERR can carry.
    static constexpr uint8_t MAX_DESTINATIONS = 255;

    RerrHeader();

    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
    void Print(std::ostream& os) const override;

    /**
     * @brief Append an unreachable destination.
     * @param dst the destination address
     * @param seqNo the destination sequence number after invalidation
     * @return false, leaving the header unchanged, if the message is full
     */
    bool AddUnreachable(Ipv4Address dst, uint32_t seqNo);

    /**
     * @brief Get the unreachable destinations in transmission order.
     * @return the destination list
     */
    const std::vector<UnreachableDestination>& GetUnreachable() const;

    /**
     * @brief Get the number of listed destinations.
     * @return the destination count
     */
    uint8_t GetDestinationCount() const;

  private:
    std::vector<UnreachableDestination> m_unreachable; //!< Listed destinations
};

/**
 * @ingroup odr
 * @brief Compare two sequence numbers under 32-bit wrap-around.
 *
 * RFC 3561 section 6.1 mandates signed modular comparison: a plain unsigned
 * comparison would treat the value following 0xFFFFFFFF as the stalest
 * possible one and make nodes reject every route after a rollover.
 *
 * @param a candidate sequence number
 * @param b reference sequence number
 * @return true if a is strictly fresher than b
 */
inline bool
IsFresher(uint32_t a, uint32_t b)
{
    return static_cast<int32_t>(a - b) > 0;
}

} // namespace odr
} // namespace ns3

#endif /* ODR_PACKET_H */
