/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * framework_manet — STEP 1a.
 *
 * Rôle du fichier
 * ---------------
 * Sonde de liaison radio : deux nœuds immobiles à une distance d, un
 * émetteur qui envoie N trames de niveau 2 (PacketSocket, sans IP ni ARP ni
 * AODV), un récepteur qui les compte. La sonde mesure ce que la radio de la
 * baseline sait réellement transporter à la distance d, séparément pour :
 *
 *  - les trames BROADCAST : chemin des RREQ, HELLO et RERR d'AODV
 *    (pas d'ACK ni de retransmission MAC) ;
 *  - les trames UNICAST : chemin des données et des RREP (ACK et
 *    retransmissions MAC).
 *
 * Pourquoi PacketSocket et pas UDP/IP
 * -----------------------------------
 * En unicast IP, une résolution ARP (broadcast) précède la première trame :
 * à une distance où le broadcast échoue, l'unicast ne serait jamais émis et
 * l'échec serait imputé à tort à l'unicast. La sonde isole la couche radio.
 * La taille des charges utiles reproduit celle des paquets IP réels (le
 * WifiNetDevice ajoute le même en-tête LLC/SNAP dans les deux cas).
 */

#ifndef FRAMEWORK_MANET_LINK_PROBE_H
#define FRAMEWORK_MANET_LINK_PROBE_H

#include "framework-manet-radio-helper.h"

#include "ns3/nstime.h"

#include <cstdint>
#include <string>

namespace ns3
{
namespace fmanet
{

/**
 * Type de trame sondé.
 */
enum class FrameClass
{
    BROADCAST, //!< adresse destination ff:ff:ff:ff:ff:ff, sans ACK
    UNICAST    //!< adresse MAC du récepteur, avec ACK et retransmissions
};

/**
 * @param frameClass type de trame
 * @return "broadcast" ou "unicast"
 */
std::string ToString(FrameClass frameClass);

/// Charge utile par défaut d'une trame broadcast : taille IP d'un RREQ AODV
/// = 20 (IPv4) + 8 (UDP) + 24 (RREQ : 1 octet de type + 23, aodv-packet.cc).
constexpr uint32_t RREQ_IP_BYTES = 52;

/// Charge utile par défaut d'une trame unicast : taille IP d'un paquet de
/// données = 20 (IPv4) + 8 (UDP) + 512 (charge utile UDP de la spécification).
constexpr uint32_t DATA_IP_BYTES = 540;

/// Premier index de stream RNG du bloc Wi-Fi (plan D-07).
constexpr int64_t WIFI_STREAM_BLOCK_START = 20000;

/// Taille du bloc Wi-Fi : un dépassement est une erreur fatale.
constexpr int64_t WIFI_STREAM_BLOCK_SIZE = 10000;

/**
 * Paramètres d'une mesure à une distance.
 */
struct LinkProbeConfig
{
    double distanceM = 10.0;                     //!< distance émetteur–récepteur [m]
    FrameClass frameClass = FrameClass::UNICAST; //!< type de trame
    uint32_t frames = 1000;                      //!< trames offertes par l'émetteur
    uint32_t payloadBytes = DATA_IP_BYTES;       //!< charge utile au-dessus de LLC [octets]
    /// Intervalle entre trames. Il doit dépasser la durée du pire cas de
    /// retransmissions (≈ 36 ms pour 7 essais avec backoff exponentiel en
    /// 802.11b) ; sinon la file MAC grossit et des trames expirent
    /// (WifiMacQueue MaxDelay = 500 ms), ce qui confondrait perte radio et
    /// perte de file.
    Time interval = MilliSeconds(50);
};

/**
 * Résultat d'une mesure.
 *
 * Toutes les quantités sont des comptages bruts ou des valeurs observées :
 * les proportions et intervalles sont calculés par l'appelant.
 */
struct LinkProbeResult
{
    double distanceM;      //!< distance [m]
    double predictedRxDbm; //!< puissance reçue prédite par le modèle [dBm]
    uint32_t offered;      //!< trames remises au socket par l'émetteur
    uint32_t delivered;    //!< trames reçues par l'application du récepteur
    uint32_t
        senderDataTx; //!< trames de données émises par le PHY émetteur, retransmissions incluses
    uint32_t receiverAckTx;  //!< ACK émis par le PHY récepteur
    std::string dataMode;    //!< débit(s) PHY observé(s) des trames de données ("|" si plusieurs)
    std::string ackMode;     //!< débit(s) PHY observé(s) des ACK ; "-" si aucun
    double meanRxSignalDbm;  //!< signal moyen des trames de données reçues [dBm] ; NaN si aucune
    double meanRxNoiseDbm;   //!< bruit moyen associé [dBm] ; NaN si aucune
    int64_t wifiStreamsUsed; //!< streams RNG consommés par la radio
};

/**
 * Exécute une mesure complète dans une simulation indépendante.
 *
 * Effets : appelle Simulator::Run() puis Simulator::Destroy(). La fonction ne
 * doit donc pas être appelée pendant une autre simulation. Les streams RNG
 * Wi-Fi sont réaffectés à chaque appel à partir de WIFI_STREAM_BLOCK_START :
 * deux appels identiques donnent des résultats identiques, quel que soit
 * l'ordre des appels (propriété testée).
 *
 * @param radio configuration radio (validée par RadioHelper)
 * @param probe paramètres de la mesure
 * @return les comptages et observations
 */
LinkProbeResult RunLinkProbe(const RadioConfig& radio, const LinkProbeConfig& probe);

} // namespace fmanet
} // namespace ns3

#endif // FRAMEWORK_MANET_LINK_PROBE_H
