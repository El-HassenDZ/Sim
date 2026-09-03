/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * This file is part of the MTC-AODV research prototype.
 */

#include "attack-behavior.h"

namespace ns3
{
namespace mtcaodv
{

NS_OBJECT_ENSURE_REGISTERED(AttackBehavior);

TypeId
AttackBehavior::GetTypeId()
{
    static TypeId typeId = TypeId("ns3::mtcaodv::AttackBehavior")
                               .SetParent<Object>()
                               .SetGroupName("MtcAodv");
    return typeId;
}

AttackBehavior::AttackBehavior() = default;

AttackBehavior::~AttackBehavior() = default;

} // namespace mtcaodv
} // namespace ns3

