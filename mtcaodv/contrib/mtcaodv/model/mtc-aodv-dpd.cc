/*
 * Copyright (c) 2009 IITP RAS
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 *
 * Authors: Elena Buchatskaia <borovkovaes@iitp.ru>
 *          Pavel Boyko <boyko@iitp.ru>
 */

#include "mtc-aodv-dpd.h"

namespace ns3
{
namespace mtcaodv
{

bool
DuplicatePacketDetection::IsDuplicate(Ptr<const Packet> p, const Ipv4Header& header)
{
    return m_idCache.IsDuplicate(header.GetSource(), p->GetUid());
}

void
DuplicatePacketDetection::SetLifetime(Time lifetime)
{
    m_idCache.SetLifetime(lifetime);
}

Time
DuplicatePacketDetection::GetLifetime() const
{
    return m_idCache.GetLifeTime();
}

} // namespace mtcaodv
} // namespace ns3
