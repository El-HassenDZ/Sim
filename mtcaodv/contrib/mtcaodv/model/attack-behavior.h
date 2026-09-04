/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef MTC_AODV_ATTACK_BEHAVIOR_H
#define MTC_AODV_ATTACK_BEHAVIOR_H

#include "ns3/nstime.h"
#include "ns3/object.h"

#include <cstdint>

namespace ns3
{
namespace mtcaodv
{

/**
 * \ingroup mtcaodv
 * \brief Décision d'une politique d'attaque face à un RREQ reçu (A2.3).
 */
enum class RouteReplyDecision
{
    CONTINUE_AODV,     //!< Aucune action malveillante : traitement AODV normal (D-04).
    FORGE_REPLY        //!< Émettre un RREP forgé et arrêter le traitement du RREQ (D-03).
};

/**
 * \ingroup mtcaodv
 * \brief Décision d'une politique d'attaque face à un paquet IPv4 en transit (A2.4).
 */
enum class TransitPacketDecision
{
    FORWARD_NORMALLY,  //!< Relayer sans modification (D-06).
    DROP_SILENTLY      //!< Consommer sans callback ni RERR explicite (D-05).
};

/**
 * \ingroup mtcaodv
 * \brief Contexte d'un paquet en transit, tel que le voit la politique d'attaque.
 *
 * La politique ne reçoit que ce dont elle a besoin pour décider. Elle n'a accès ni au
 * paquet, ni au nœud, ni au simulateur : cela la rend testable hors ns-3 et empêche
 * qu'un comportement malveillant lise involontairement un état global.
 */
struct TransitPacketContext
{
    bool isRoutingControl{false};   //!< Paquet de contrôle AODV (port 654).
    bool isSecurityControl{false};  //!< Paquet de contrôle MTC-AODV (plan UDP séparé).
    bool isLocallyDestined{false};  //!< Destiné à ce nœud : hors périmètre de l'attaque.
};

/**
 * \ingroup mtcaodv
 * \brief Profil d'un RREP forgé (Éq. 23, A2.3).
 *
 * Les trois champs correspondent exactement aux paramètres \f$\Delta_{seq}\f$,
 * \f$h_{fake}\f$ et \f$T_{fake}\f$ de la spécification, une fois appliqués au RREQ reçu.
 */
struct ForgedReplyProfile
{
    uint32_t destinationSequenceNumber{0};  //!< \f$seq_{fake}\f$, Éq. (23).
    uint8_t advertisedHopCount{1};          //!< \f$h_{fake}\f$.
    Time routeLifetime{Seconds(0)};         //!< \f$T_{fake}\f$.
};

/**
 * \ingroup mtcaodv
 * \brief Interface d'un comportement malveillant attaché à un nœud.
 *
 * Le protocole de routage interroge cette interface ; il n'implémente aucune logique
 * d'attaque lui-même. Cette séparation permet d'ajouter un autre profil d'attaquant
 * sans toucher au fork AODV, et garantit que la vérité terrain (§2, invariant 20.2.8)
 * reste confinée à la couche scénario.
 *
 * Périmètre de cette version : full Blackhole uniquement (§1, §8.3). Aucun autre profil
 * n'est fourni.
 */
class AttackBehavior : public Object
{
  public:
    static TypeId GetTypeId();
    ~AttackBehavior() override = default;

    /**
     * \brief L'attaque est-elle active à cet instant ?
     * \param now instant courant du simulateur
     * \return true si \f$t \ge t_{attack}\f$
     */
    virtual bool IsActive(Time now) const = 0;

    /**
     * \brief Décider s'il faut forger un RREP en réponse à un RREQ (A2.3).
     * \param now instant courant
     * \param hasValidReverseRoute une route inverse utilisable existe-t-elle ?
     * \return la décision ; FORGE_REPLY exige attaque active ET route inverse valide
     */
    virtual RouteReplyDecision ShouldForgeRouteReply(Time now, bool hasValidReverseRoute) const = 0;

    /**
     * \brief Construire le profil du RREP forgé (Éq. 23).
     * \param observedDestinationSequence numéro de séquence destination porté par le RREQ
     * \return le profil à sérialiser dans un RrepHeader AODV standard
     *
     * L'arithmétique est volontairement effectuée sur uint32_t : le repliement du type
     * non signé 32 bits *est* le modulo \f$2^{32}\f$ de l'Éq. (23).
     */
    virtual ForgedReplyProfile CreateForgedReplyProfile(uint32_t observedDestinationSequence) const = 0;

    /**
     * \brief Décider du sort d'un paquet en transit (A2.4).
     * \param now instant courant
     * \param context nature du paquet
     * \return DROP_SILENTLY seulement si l'attaque est active, que le drop est activé,
     *         que le paquet est en transit et qu'il n'est pas exempté comme contrôle
     */
    virtual TransitPacketDecision ShouldDropTransitPacket(Time now,
                                                          const TransitPacketContext& context) const = 0;
};

} // namespace mtcaodv
} // namespace ns3

#endif /* MTC_AODV_ATTACK_BEHAVIOR_H */
