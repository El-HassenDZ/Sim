/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * framework_manet — STEP 1a.
 *
 * Rôle du fichier
 * ---------------
 * Fonctions statistiques pures (sans état ns-3), partagées par les
 * programmes de mesure et testées isolément :
 *
 *  - intervalle de confiance de Wilson pour une proportion (taux de
 *    réception d'une liaison, plus tard PDR) ;
 *  - probabilité de lien et degré moyen d'un graphe géométrique aléatoire
 *    dans un carré, qui relient une portée radio mesurée à la connectivité
 *    attendue du scénario (décision D-05).
 *
 * Unités : distances en mètres ; proportions dans [0, 1].
 */

#ifndef FRAMEWORK_MANET_STATISTICS_H
#define FRAMEWORK_MANET_STATISTICS_H

#include <cstdint>

namespace ns3
{
namespace fmanet
{

/**
 * Intervalle [low, high] d'une proportion. Les deux bornes valent NaN si
 * l'intervalle est indéfini (aucun essai).
 */
struct ProportionInterval
{
    double low;  //!< borne inférieure, dans [0, 1]
    double high; //!< borne supérieure, dans [0, 1]
};

/// Quantile 0,975 de la loi normale centrée réduite (IC bilatéral à 95 %).
constexpr double Z_95 = 1.959963984540054;

/**
 * Intervalle de score de Wilson pour une proportion binomiale.
 *
 *   p̂ = k/n,  c = (p̂ + z²/2n) / (1 + z²/n),
 *   h = z·sqrt(p̂(1−p̂)/n + z²/4n²) / (1 + z²/n),   IC = [c − h, c + h].
 *
 * Choisi plutôt que l'intervalle de Wald (p̂ ± z·sqrt(p̂(1−p̂)/n)) parce que
 * celui-ci dégénère en [0, 0] ou [1, 1] aux extrémités, précisément là où
 * se trouvent la plupart des points d'une courbe de réception.
 *
 * @param successes nombre de succès k (k ≤ n)
 * @param trials nombre d'essais n
 * @param z quantile normal (Z_95 par défaut)
 * @return l'intervalle ; {NaN, NaN} si n = 0 ou si k > n (entrée invalide)
 */
ProportionInterval WilsonInterval(uint64_t successes, uint64_t trials, double z = Z_95);

/**
 * Probabilité que deux points indépendants, uniformes dans un carré de côté
 * L, soient à distance ≤ r (fonction de répartition de la distance) :
 *
 *   F(r) = π·x² − (8/3)·x³ + x⁴/2,  avec x = r/L, valable pour 0 ≤ r ≤ L.
 *
 * Cette formule tient compte des effets de bord ; π·r²/L² les ignorerait et
 * surestimerait la connectivité.
 *
 * @param rangeM portée r en mètres
 * @param sideM côté L du carré en mètres (> 0)
 * @return F(r) ; NaN si r < 0, si L ≤ 0 ou si r > L (formule non implémentée
 *         au-delà, inutile pour les portées étudiées)
 */
double UniformSquareLinkProbability(double rangeM, double sideM);

/**
 * Degré moyen attendu d'un nœud dans un graphe géométrique aléatoire :
 * (N − 1)·F(r).
 *
 * Hypothèse : positions uniformes et indépendantes. Le régime stationnaire du
 * RandomWaypoint concentre les nœuds au centre, ce qui augmente légèrement le
 * degré réel (voir D-15) : cette valeur est un ordre de grandeur, pas une
 * prédiction.
 *
 * @param numNodes N (≥ 2)
 * @param rangeM portée r en mètres
 * @param sideM côté L du carré en mètres
 * @return le degré moyen ; NaN si une entrée est hors domaine
 */
double ExpectedMeanDegree(uint32_t numNodes, double rangeM, double sideM);

} // namespace fmanet
} // namespace ns3

#endif // FRAMEWORK_MANET_STATISTICS_H
