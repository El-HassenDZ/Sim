/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef MTC_AODV_NETWORK_METRICS_H
#define MTC_AODV_NETWORK_METRICS_H

/**
 * \file
 * \ingroup mtcaodv
 * \brief Métriques réseau primaires de la spécification, sous forme purement numérique.
 *
 * **Rôle du fichier.** Ce fichier isole les équations (20), (24), (25), (26), (27) et
 * (28) de la spécification de tout ce qui relève du simulateur. Aucune fonction déclarée
 * ici ne connaît `Simulator`, `Packet`, `Node` ni une quelconque trace ns-3 : elles
 * transforment des *compteurs observés* en métriques dérivées, rien de plus.
 *
 * **Pourquoi cette séparation.** Deux raisons scientifiques, pas esthétiques.
 *
 * 1. *Testabilité déterministe.* Une métrique calculée à l'intérieur d'un collecteur
 *    branché sur des traces ne peut être vérifiée qu'en exécutant une simulation
 *    complète, dont le résultat dépend du canal radio, de la mobilité et du hasard. Les
 *    équations, elles, sont des identités arithmétiques : elles doivent être vérifiables
 *    au niveau 1 (tests unitaires), avec des oracles calculés à la main. C'est ce que
 *    permet ce fichier, et c'est ce qu'exige la matrice de tests du §18.2.
 * 2. *Unicité de la définition.* Le PDR doit être calculé en un seul endroit du dépôt.
 *    Deux implémentations parallèles — une pour le collecteur, une pour les tests —
 *    divergeraient silencieusement. `MetricsCollector::ComputeReport()` délègue donc
 *    ici : il n'existe qu'un seul code pour l'Éq. (20).
 *
 * **Règle fail-closed (invariant 20.4.6, décision D-22).** Une métrique dont le
 * dénominateur est nul n'est *pas* nulle : elle est indéfinie. Le type `MetricValue`
 * rend cette absence exprimable, et `FormatMetric()` l'exporte « NaN ». Un zéro
 * fabriqué serait indiscernable d'une mesure réelle de zéro et corromprait toute
 * agrégation ultérieure.
 *
 * Unités : les octets sont des octets, les durées des secondes, les débits des bit/s.
 * Aucune conversion implicite n'est faite ailleurs que dans les fonctions ci-dessous.
 */

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ns3
{
namespace mtcaodv
{

/**
 * \ingroup mtcaodv
 * \brief Valeur de métrique éventuellement non applicable.
 *
 * `std::nullopt` signifie « la métrique n'est pas définie pour cette exécution », par
 * exemple parce que son dénominateur est nul. Cette valeur est exportée « NaN » et
 * jamais 0 (invariant 20.4.6, décision D-22).
 */
using MetricValue = std::optional<double>;

/**
 * \ingroup mtcaodv
 * \brief Compteurs effectivement observés pendant la fenêtre d'évaluation.
 *
 * Aucun de ces champs ne doit être déduit de la configuration. La spécification est
 * explicite : « le dénominateur doit être observé, non supposé égal à la charge
 * configurée » (§5.9, Éq. 20) et l'invariant 20.4.5 en fait une règle. Un paquet
 * programmé mais refusé par la socket n'est donc pas un paquet émis.
 */
struct ObservedCounters
{
    /// \f$N_{app}^{tx}\f$ — paquets applicatifs réellement remis aux sockets, en paquets.
    uint64_t applicationTxPackets{0};
    /// \f$N_{app}^{rx}\f$ — paquets applicatifs réellement livrés aux puits, en paquets.
    uint64_t applicationRxPackets{0};
    /// Octets de charge utile applicative émis, en octets.
    uint64_t applicationTxPayloadBytes{0};
    /// \f$B_{app,payload}^{rx}\f$ — octets de charge utile applicative livrés, en octets.
    uint64_t applicationRxPayloadBytes{0};
    /**
     * \f$B_{network}^{rx}\f$ — octets réseau livrés, selon le périmètre de comptage
     * préenregistré, en octets.
     *
     * Le périmètre est déclaré par l'appelant et doit rester identique entre variantes
     * appariées, faute de quoi les débits de l'Éq. (25) ne seraient pas comparables.
     */
    uint64_t deliveredNetworkBytes{0};
    /// \f$N_{AODV,hop}^{control}\f$ — transmissions de contrôle AODV comptées hop par hop.
    uint64_t aodvControlTransmissions{0};
    /// \f$N_{routeDiscovery}\f$ — découvertes de route effectivement lancées.
    uint64_t routeDiscoveries{0};
};

/**
 * \ingroup mtcaodv
 * \brief Métriques réseau dérivées des compteurs observés.
 *
 * Chaque champ porte le numéro de l'équation qu'il implémente. Un champ vide
 * (`std::nullopt`) signifie « non applicable », jamais « zéro ».
 */
struct DerivedNetworkMetrics
{
    MetricValue packetDeliveryRatio;         //!< Éq. (20), sans unité, dans [0,1].
    MetricValue packetLossRatio;             //!< Éq. (24), sans unité, dans [0,1].
    MetricValue throughputBitsPerSecond;     //!< Éq. (25), en bit/s.
    MetricValue goodputBitsPerSecond;        //!< Éq. (25), en bit/s.
    MetricValue meanEndToEndDelay;           //!< Éq. (26), en secondes.
    MetricValue medianEndToEndDelay;         //!< Quantile empirique (§17.1), en secondes.
    MetricValue jitter;                      //!< Éq. (27), en secondes.
    MetricValue normalizedRoutingOverhead;   //!< Éq. (28), sans unité.
    MetricValue routeDiscoveryFrequency;     //!< Éq. (28), en s^-1.
};

/**
 * \ingroup mtcaodv
 * \brief Calcule les métriques réseau primaires à partir des compteurs observés.
 *
 * Correspondance équation → code, dans l'ordre du corps de la fonction :
 *
 * | Équation | Métrique | Condition d'existence |
 * |---|---|---|
 * | (20) | `packetDeliveryRatio` | \f$N_{app}^{tx}>0\f$ |
 * | (24) | `packetLossRatio` | \f$N_{app}^{tx}>0\f$ |
 * | (25) | `throughputBitsPerSecond`, `goodputBitsPerSecond` | \f$T_{eval}>0\f$ |
 * | (26) | `meanEndToEndDelay` | au moins un paquet livré |
 * | (27) | `jitter` | au moins deux paquets livrés |
 * | (28) | `normalizedRoutingOverhead` | \f$N_{app}^{rx}>0\f$ |
 * | (28) | `routeDiscoveryFrequency` | \f$T_{eval}>0\f$ |
 *
 * \param counters compteurs observés pendant la fenêtre d'évaluation
 * \param delaysSeconds délais de bout en bout des paquets livrés, **dans l'ordre de
 *        réception** (l'Éq. 27 est définie sur cet ordre et non sur l'ordre d'émission),
 *        en secondes
 * \param evaluationWindowSeconds \f$T_{eval}\f$, durée de la fenêtre d'évaluation, en
 *        secondes ; une valeur nulle ou négative rend les débits indéfinis plutôt que
 *        de provoquer une division par zéro
 * \return les métriques dérivées, chacune définie ou explicitement absente
 *
 * \note La fonction ne borne ni ne corrige aucune valeur. Si le nombre de paquets reçus
 *       dépasse le nombre émis — duplication applicative, comptage erroné — le PDR
 *       obtenu dépasse 1 et le contrôle d'invariants le signale. Écrêter la valeur
 *       masquerait le défaut au lieu de le révéler.
 */
DerivedNetworkMetrics ComputeDerivedNetworkMetrics(const ObservedCounters& counters,
                                                   const std::vector<double>& delaysSeconds,
                                                   double evaluationWindowSeconds);

/**
 * \ingroup mtcaodv
 * \brief Vérifie les invariants numériques applicables aux métriques réseau.
 *
 * Invariants contrôlés, tirés du §20.1 de la spécification et de la liste d'invariants
 * du plan de développement :
 * - \f$0\le PDR\le1\f$ ;
 * - \f$0\le PLR\le1\f$ ;
 * - \f$PDR+PLR=1\f$ à la tolérance numérique près (conséquence de l'Éq. 24) ;
 * - tout débit, délai ou fréquence défini est fini et non négatif.
 *
 * \param metrics métriques à contrôler
 * \param firstViolation si non nul, reçoit la description de la première violation
 * \return true si tous les invariants applicables sont satisfaits
 *
 * \note Une métrique absente ne viole rien : l'absence est un état légitime.
 */
bool CheckNetworkMetricInvariants(const DerivedNetworkMetrics& metrics,
                                  std::string* firstViolation = nullptr);

/**
 * \ingroup mtcaodv
 * \brief Rend une métrique sous forme textuelle pour l'export.
 *
 * \param value métrique, éventuellement absente
 * \return la valeur en notation par défaut avec 9 chiffres significatifs, ou « NaN »
 *         si la métrique est absente ou non finie
 *
 * Le choix de « NaN » plutôt que d'une cellule vide est délibéré : une cellule vide est
 * lue comme zéro par de nombreux outils de tableur, ce que la règle D-22 interdit.
 */
std::string FormatMetric(const MetricValue& value);

/**
 * \ingroup mtcaodv
 * \brief Rend une grandeur numérique sous forme de littéral JSON valide.
 *
 * \param value valeur à écrire
 * \return la valeur numérique, ou « null » si elle n'est pas finie
 *
 * JSON ne possède pas de littéral pour « non un nombre » : écrire `nan` produit un
 * fichier que tout analyseur conforme rejette. La convention du projet est donc
 * `null` dans les manifests JSON et « NaN » dans les CSV. Les deux disent la même
 * chose — la grandeur n'est pas applicable — et aucune des deux n'est zéro
 * (invariant 20.4.6, règle D-22).
 *
 * Cette distinction n'est pas cosmétique : un manifest JSON invalide fait échouer la
 * validation A7.2 de l'exécution entière, alors que la grandeur non applicable qui l'a
 * provoquée pouvait être parfaitement légitime — un flux dont les extrémités n'ont
 * jamais été connectées n'a pas de nombre de sauts moyen.
 */
std::string FormatJsonNumber(double value);

/// Surcharge pour une métrique éventuellement absente ; « null » si absente.
std::string FormatJsonNumber(const MetricValue& value);

} // namespace mtcaodv
} // namespace ns3

#endif /* MTC_AODV_NETWORK_METRICS_H */
