/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "network-metrics.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

namespace ns3
{
namespace mtcaodv
{

namespace
{

/**
 * Tolérance de l'identité \f$PDR+PLR=1\f$.
 *
 * Les deux valeurs sont calculées en double à partir des mêmes entiers ; l'écart
 * attendu est de l'ordre de l'epsilon machine. Le seuil de 1e-12 laisse une marge de
 * plusieurs ordres de grandeur tout en restant très inférieur à toute erreur qui
 * signalerait un vrai défaut de calcul.
 */
constexpr double INVARIANT_TOLERANCE = 1e-12;

/// Vrai si la métrique est présente et si son contenu est fini.
bool
IsDefinedAndFinite(const MetricValue& value)
{
    return value.has_value() && std::isfinite(*value);
}

} // namespace

DerivedNetworkMetrics
ComputeDerivedNetworkMetrics(const ObservedCounters& counters,
                             const std::vector<double>& delaysSeconds,
                             double evaluationWindowSeconds)
{
    DerivedNetworkMetrics metrics;

    // -----------------------------------------------------------------------------
    // Équation (20) : PDR = N_app^rx / N_app^tx
    // Équation (24) : PLR = (N_app^tx - N_app^rx) / N_app^tx = 1 - PDR
    //
    // Le §21 (« Zéro paquet applicatif émis ») déclare l'exécution invalide pour le PDR
    // lorsque le dénominateur est nul. On n'invente donc pas un ratio : les deux
    // métriques restent absentes. La seconde est écrite sous la forme 1-PDR, qui est
    // l'identité donnée par l'Éq. (24) elle-même ; cela garantit mécaniquement que les
    // deux valeurs exportées sont cohérentes entre elles.
    // -----------------------------------------------------------------------------
    if (counters.applicationTxPackets > 0)
    {
        const double deliveryRatio = static_cast<double>(counters.applicationRxPackets) /
                                     static_cast<double>(counters.applicationTxPackets);
        metrics.packetDeliveryRatio = deliveryRatio;
        metrics.packetLossRatio = 1.0 - deliveryRatio;
    }

    // -----------------------------------------------------------------------------
    // Équation (25) : Throughput = 8 * B_network^rx / T_eval
    //                 Goodput    = 8 * B_app,payload^rx / T_eval
    //
    // Le facteur 8 convertit les octets en bits ; les deux sorties sont en bit/s. La
    // distinction throughput/goodput n'est pas cosmétique : le premier inclut les
    // en-têtes portés par le périmètre de comptage déclaré, le second seulement la
    // charge utile applicative. Comparer un throughput à un goodput entre deux
    // variantes produirait un écart purement comptable.
    //
    // Équation (28), second terme : RDF = N_routeDiscovery / T_eval, en s^-1.
    // -----------------------------------------------------------------------------
    if (evaluationWindowSeconds > 0.0)
    {
        metrics.throughputBitsPerSecond =
            8.0 * static_cast<double>(counters.deliveredNetworkBytes) / evaluationWindowSeconds;
        metrics.goodputBitsPerSecond =
            8.0 * static_cast<double>(counters.applicationRxPayloadBytes) / evaluationWindowSeconds;
        metrics.routeDiscoveryFrequency =
            static_cast<double>(counters.routeDiscoveries) / evaluationWindowSeconds;
    }

    // -----------------------------------------------------------------------------
    // Équation (26) : d_barre = (1/N_app^rx) * somme des (t_p^rx - t_p^tx)
    //
    // La moyenne n'est définie que sur les paquets *livrés*. Un paquet perdu n'a pas de
    // délai infini : il n'a pas de délai du tout, et l'inclure d'une manière ou d'une
    // autre fausserait la métrique. C'est pourquoi un délai moyen doit toujours être lu
    // conjointement au PDR : une variante qui ne livre que les paquets faciles affiche
    // mécaniquement un meilleur délai.
    //
    // La médiane est ajoutée au titre du §17.1 (« médiane/percentiles délai ») : elle
    // résiste aux quelques paquets à délai extrême que produit la bufferisation AODV
    // pendant une redécouverte de route.
    // -----------------------------------------------------------------------------
    if (!delaysSeconds.empty())
    {
        const double sum = std::accumulate(delaysSeconds.begin(), delaysSeconds.end(), 0.0);
        metrics.meanEndToEndDelay = sum / static_cast<double>(delaysSeconds.size());

        std::vector<double> sorted = delaysSeconds;
        std::sort(sorted.begin(), sorted.end());
        const size_t middle = sorted.size() / 2;
        metrics.medianEndToEndDelay =
            (sorted.size() % 2 == 0) ? 0.5 * (sorted[middle - 1] + sorted[middle]) : sorted[middle];
    }

    // -----------------------------------------------------------------------------
    // Équation (27) : Jitter = (1/(N_app^rx - 1)) * somme_{k=2..N} |d_k - d_{k-1}|
    //
    // La somme porte sur des différences successives : elle exige au moins deux paquets
    // livrés, sans quoi le dénominateur N_app^rx - 1 est nul et la métrique est
    // indéfinie (et non nulle). L'ordre utilisé est l'ordre de réception, conformément à
    // la définition de d_k au §17.1.
    // -----------------------------------------------------------------------------
    if (delaysSeconds.size() >= 2)
    {
        double absoluteVariation = 0.0;
        for (size_t k = 1; k < delaysSeconds.size(); ++k)
        {
            absoluteVariation += std::fabs(delaysSeconds[k] - delaysSeconds[k - 1]);
        }
        metrics.jitter = absoluteVariation / static_cast<double>(delaysSeconds.size() - 1);
    }

    // -----------------------------------------------------------------------------
    // Équation (28), premier terme : NRO = N_AODV,hop^control / N_app^rx
    //
    // Le dénominateur est le nombre de paquets *livrés*, pas émis : la surcharge est
    // normalisée par le service effectivement rendu. Conséquence directe et voulue : un
    // protocole qui inonde beaucoup et ne livre rien a un NRO très grand, non pas nul.
    // Si rien n'est livré, la métrique est indéfinie.
    // -----------------------------------------------------------------------------
    if (counters.applicationRxPackets > 0)
    {
        metrics.normalizedRoutingOverhead =
            static_cast<double>(counters.aodvControlTransmissions) /
            static_cast<double>(counters.applicationRxPackets);
    }

    return metrics;
}

bool
CheckNetworkMetricInvariants(const DerivedNetworkMetrics& metrics, std::string* firstViolation)
{
    auto fail = [firstViolation](const std::string& message) {
        if (firstViolation != nullptr)
        {
            *firstViolation = message;
        }
        return false;
    };

    // 0 <= PDR <= 1. Une valeur hors bornes signale soit une duplication applicative non
    // filtrée, soit un comptage incohérent entre source et puits : dans les deux cas un
    // défaut de mesure, à corriger et non à écrêter.
    if (metrics.packetDeliveryRatio.has_value())
    {
        const double pdr = *metrics.packetDeliveryRatio;
        if (!std::isfinite(pdr) || pdr < 0.0 || pdr > 1.0)
        {
            return fail("PDR hors de [0,1] : " + std::to_string(pdr));
        }
    }

    if (metrics.packetLossRatio.has_value())
    {
        const double plr = *metrics.packetLossRatio;
        if (!std::isfinite(plr) || plr < 0.0 || plr > 1.0)
        {
            return fail("PLR hors de [0,1] : " + std::to_string(plr));
        }
    }

    // Éq. (24) : PLR = 1 - PDR. Les deux métriques existent ou sont absentes ensemble.
    if (metrics.packetDeliveryRatio.has_value() != metrics.packetLossRatio.has_value())
    {
        return fail("PDR et PLR doivent être définis ou absents simultanément");
    }
    if (metrics.packetDeliveryRatio.has_value())
    {
        const double sum = *metrics.packetDeliveryRatio + *metrics.packetLossRatio;
        if (std::fabs(sum - 1.0) > INVARIANT_TOLERANCE)
        {
            return fail("PDR + PLR != 1 : " + std::to_string(sum));
        }
    }

    // Débits, délais et fréquences : finis et non négatifs lorsqu'ils sont définis.
    const std::pair<const MetricValue*, const char*> nonNegative[] = {
        {&metrics.throughputBitsPerSecond, "throughput"},
        {&metrics.goodputBitsPerSecond, "goodput"},
        {&metrics.meanEndToEndDelay, "délai moyen"},
        {&metrics.medianEndToEndDelay, "délai médian"},
        {&metrics.jitter, "jitter"},
        {&metrics.normalizedRoutingOverhead, "NRO"},
        {&metrics.routeDiscoveryFrequency, "RDF"},
    };

    for (const auto& [value, name] : nonNegative)
    {
        if (!value->has_value())
        {
            continue;
        }
        if (!IsDefinedAndFinite(*value) || **value < 0.0)
        {
            return fail(std::string(name) + " non fini ou négatif : " +
                        std::to_string(value->value()));
        }
    }

    return true;
}

std::string
FormatJsonNumber(double value)
{
    if (!std::isfinite(value))
    {
        return "null";
    }
    std::ostringstream stream;
    stream.precision(9);
    stream << std::defaultfloat << value;
    return stream.str();
}

std::string
FormatJsonNumber(const MetricValue& value)
{
    if (!value.has_value())
    {
        return "null";
    }
    return FormatJsonNumber(*value);
}

std::string
FormatMetric(const MetricValue& value)
{
    if (!IsDefinedAndFinite(value))
    {
        return "NaN";
    }
    std::ostringstream stream;
    stream.precision(9);
    stream << std::defaultfloat << *value;
    return stream.str();
}

} // namespace mtcaodv
} // namespace ns3
