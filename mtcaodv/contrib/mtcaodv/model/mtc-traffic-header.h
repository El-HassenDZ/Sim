/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef MTC_AODV_TRAFFIC_HEADER_H
#define MTC_AODV_TRAFFIC_HEADER_H

#include "ns3/header.h"
#include "ns3/nstime.h"

namespace ns3
{
namespace mtcaodv
{

/**
 * \ingroup mtcaodv
 * \brief En-tête applicatif porté par chaque paquet CBR de mesure.
 *
 * Les métriques réseau de la spécification exigent des compteurs *observés* et non la
 * charge configurée (Éq. 20, §17.1). Un identifiant de flux, un numéro de séquence et
 * l'instant d'émission suffisent à reconstruire, côté récepteur et sans horloge globale
 * de protocole, les grandeurs \f$N_{app}^{tx}\f$, \f$N_{app}^{rx}\f$, \f$t_p^{rx}-t_p^{tx}\f$
 * (Éq. 26) et la suite ordonnée des délais (Éq. 27).
 *
 * L'instant d'émission est stocké en nanosecondes depuis le début de la simulation :
 * c'est une valeur du simulateur, pas une horloge que les nœuds s'échangeraient. Aucun
 * mécanisme du protocole ne la lit ; seule la couche de mesure l'exploite.
 *
 * Taille sérialisée : 14 octets.
 */
class MtcTrafficHeader : public Header
{
  public:
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;

    MtcTrafficHeader();

    void SetFlowId(uint16_t flowId);
    uint16_t GetFlowId() const;

    void SetSequenceNumber(uint32_t sequenceNumber);
    uint32_t GetSequenceNumber() const;

    void SetSendTime(Time sendTime);
    Time GetSendTime() const;

    // Interface Header.
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
    void Print(std::ostream& os) const override;

  private:
    uint16_t m_flowId;         //!< Identifiant du flux applicatif.
    uint32_t m_sequenceNumber; //!< Numéro de séquence au sein du flux, à partir de 0.
    uint64_t m_sendTimeNs;     //!< Instant d'émission, en nanosecondes.
};

} // namespace mtcaodv
} // namespace ns3

#endif /* MTC_AODV_TRAFFIC_HEADER_H */
