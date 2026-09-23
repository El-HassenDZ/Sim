/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * framework_manet — STEP 1a. Implémentation des fonctions statistiques
 * (voir l'en-tête pour les formules et leurs justifications).
 */

#include "framework-manet-statistics.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ns3
{
namespace fmanet
{

namespace
{
/// NaN silencieux : une quantité indéfinie n'est jamais remplacée par 0.
constexpr double NOT_A_NUMBER = std::numeric_limits<double>::quiet_NaN();
} // namespace

ProportionInterval
WilsonInterval(uint64_t successes, uint64_t trials, double z)
{
    if (trials == 0 || successes > trials)
    {
        return {NOT_A_NUMBER, NOT_A_NUMBER};
    }
    const double n = static_cast<double>(trials);
    const double p = static_cast<double>(successes) / n;
    const double z2 = z * z;
    const double denominator = 1.0 + z2 / n;
    const double center = (p + z2 / (2.0 * n)) / denominator;
    const double halfWidth = z * std::sqrt(p * (1.0 - p) / n + z2 / (4.0 * n * n)) / denominator;
    // Aux extrémités, la borne vaut exactement 0 (k = 0) ou 1 (k = n) ; le
    // calcul en virgule flottante laisserait un résidu de l'ordre de 1e-19
    // (observé à STEP 1a), d'où l'affectation exacte. Ailleurs, le bornage à
    // [0, 1] ne fait que protéger contre l'arrondi.
    const double low = (successes == 0) ? 0.0 : std::max(0.0, center - halfWidth);
    const double high = (successes == trials) ? 1.0 : std::min(1.0, center + halfWidth);
    return {low, high};
}

double
UniformSquareLinkProbability(double rangeM, double sideM)
{
    if (!(sideM > 0.0) || !(rangeM >= 0.0) || rangeM > sideM)
    {
        return NOT_A_NUMBER;
    }
    const double x = rangeM / sideM;
    // F(r) = π·x² − (8/3)·x³ + x⁴/2
    return M_PI * x * x - (8.0 / 3.0) * x * x * x + 0.5 * x * x * x * x;
}

double
ExpectedMeanDegree(uint32_t numNodes, double rangeM, double sideM)
{
    if (numNodes < 2)
    {
        return NOT_A_NUMBER;
    }
    // Degré moyen = (N − 1)·F(r) ; NaN propagé si F est indéfinie.
    return static_cast<double>(numNodes - 1) * UniformSquareLinkProbability(rangeM, sideM);
}

} // namespace fmanet
} // namespace ns3
