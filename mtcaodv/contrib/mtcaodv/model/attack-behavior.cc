/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
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
    // Classe abstraite : pas de constructeur enregistré. Elle n'existe que pour donner
    // au protocole de routage un point d'attache indépendant du profil d'attaque
    // (§8.1, « L'interface abstraite AttackBehavior permet d'ajouter ultérieurement un
    // autre profil sans exposer la vérité terrain au détecteur »).
    static TypeId tid = TypeId("ns3::mtcaodv::AttackBehavior")
                            .SetParent<Object>()
                            .SetGroupName("MtcAodv");
    return tid;
}

void
AttackBehavior::NotifyForgedReplySent(uint32_t)
{
    // Corps par défaut vide : un profil sans RREP forgé n'a rien à notifier.
}

void
AttackBehavior::NotifyTransitPacketDropped(uint64_t, uint32_t, uint32_t)
{
    // Corps par défaut vide.
}

} // namespace mtcaodv
} // namespace ns3
