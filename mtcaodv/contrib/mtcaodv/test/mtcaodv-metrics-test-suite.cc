/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file
 * \ingroup mtcaodv
 * \brief Tests de niveau 1 de l'étape 0 : équations réseau, schéma CSV et configuration.
 *
 * Ces tests ne lancent aucune simulation. Ils vérifient des identités arithmétiques et
 * des règles de format, avec des oracles calculés à la main. C'est précisément ce que
 * permet la séparation entre `network-metrics.{h,cc}` (les équations) et
 * `metrics-collector.{h,cc}` (la collecte via traces ns-3).
 *
 * Couverture :
 *
 * | Test | Objet | Référence |
 * |---|---|---|
 * | M-01 | PDR et PLR sur compteurs connus | Éq. (20), (24) |
 * | M-02 | Zéro paquet émis : PDR/PLR indéfinis, jamais nuls | §21, D-22, inv. 20.4.6 |
 * | M-03 | Throughput, goodput et RDF | Éq. (25), (28) |
 * | M-04 | Délai moyen et médian | Éq. (26), §17.1 |
 * | M-05 | Jitter, y compris le cas à moins de deux paquets | Éq. (27) |
 * | M-06 | NRO et son indéfinition sans livraison | Éq. (28) |
 * | M-07 | Invariants numériques 0 <= PDR,PLR <= 1 et PDR+PLR = 1 | §20.1 |
 * | M-08 | Ordre normatif des colonnes CSV | schéma d'étape |
 * | M-09 | Colonne obligatoire manquante : erreur, pas zéro | inv. 20.4.6 |
 * | M-10 | Extension du schéma sans déplacement des colonnes | continuité inter-étapes |
 * | M-11 | Validation fail-closed de la configuration pilote | §13.3, §21 |
 * | M-12 | Empreinte de scénario identique entre protocoles appariés | inv. 20.4.4 |
 * | M-13 | Convention JSON : « null » pour une grandeur non applicable | A7.2, D-22 |
 */

#include "ns3/network-metrics.h"
#include "ns3/pilot-configuration.h"
#include "ns3/run-record.h"
#include "ns3/test.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ns3;
using namespace ns3::mtcaodv;

namespace
{

/// Tolérance des comparaisons numériques des tests.
constexpr double TEST_TOLERANCE = 1e-9;

} // namespace

/**
 * \ingroup mtcaodv
 * M-01 — Éq. (20) et (24) sur des compteurs connus.
 */
class DeliveryRatioTestCase : public TestCase
{
  public:
    DeliveryRatioTestCase()
        : TestCase("M-01 : Eq.(20) PDR et Eq.(24) PLR sur compteurs connus")
    {
    }

  private:
    void DoRun() override
    {
        ObservedCounters counters;
        counters.applicationTxPackets = 800;
        counters.applicationRxPackets = 720;

        const DerivedNetworkMetrics metrics = ComputeDerivedNetworkMetrics(counters, {}, 45.0);

        NS_TEST_ASSERT_MSG_EQ(metrics.packetDeliveryRatio.has_value(), true, "PDR doit être défini");
        NS_TEST_ASSERT_MSG_EQ_TOL(*metrics.packetDeliveryRatio, 0.9, TEST_TOLERANCE,
                                  "720/800 = 0,9");
        NS_TEST_ASSERT_MSG_EQ(metrics.packetLossRatio.has_value(), true, "PLR doit être défini");
        NS_TEST_ASSERT_MSG_EQ_TOL(*metrics.packetLossRatio, 0.1, TEST_TOLERANCE,
                                  "PLR = 1 - PDR = 0,1");

        // Cas limite haut : la livraison intégrale donne exactement 1 et 0.
        counters.applicationRxPackets = 800;
        const DerivedNetworkMetrics perfect = ComputeDerivedNetworkMetrics(counters, {}, 45.0);
        NS_TEST_ASSERT_MSG_EQ_TOL(*perfect.packetDeliveryRatio, 1.0, TEST_TOLERANCE, "PDR = 1");
        NS_TEST_ASSERT_MSG_EQ_TOL(*perfect.packetLossRatio, 0.0, TEST_TOLERANCE, "PLR = 0");
    }
};

/**
 * \ingroup mtcaodv
 * M-02 — Sans paquet émis, PDR et PLR sont indéfinis et exportés « NaN ».
 *
 * C'est la règle fail-closed : le §21 déclare l'exécution invalide pour le PDR, et un
 * zéro fabriqué serait indiscernable d'une livraison réellement nulle.
 */
class UndefinedRatioTestCase : public TestCase
{
  public:
    UndefinedRatioTestCase()
        : TestCase("M-02 : zéro paquet émis donne NaN, jamais zéro")
    {
    }

  private:
    void DoRun() override
    {
        const ObservedCounters counters; // tous les compteurs à zéro
        const DerivedNetworkMetrics metrics = ComputeDerivedNetworkMetrics(counters, {}, 45.0);

        NS_TEST_ASSERT_MSG_EQ(metrics.packetDeliveryRatio.has_value(), false,
                              "PDR doit être absent, pas nul");
        NS_TEST_ASSERT_MSG_EQ(metrics.packetLossRatio.has_value(), false,
                              "PLR doit être absent, pas nul");
        NS_TEST_ASSERT_MSG_EQ(FormatMetric(metrics.packetDeliveryRatio), std::string("NaN"),
                              "l'export d'une métrique absente doit être « NaN »");

        // Une métrique absente ne viole aucun invariant : l'absence est un état légitime.
        std::string violation;
        NS_TEST_ASSERT_MSG_EQ(CheckNetworkMetricInvariants(metrics, &violation), true,
                              "une métrique absente ne doit pas être signalée comme violation");
    }
};

/**
 * \ingroup mtcaodv
 * M-03 — Éq. (25) débits et Éq. (28) fréquence de découverte.
 */
class ThroughputTestCase : public TestCase
{
  public:
    ThroughputTestCase()
        : TestCase("M-03 : Eq.(25) throughput/goodput et Eq.(28) RDF")
    {
    }

  private:
    void DoRun() override
    {
        ObservedCounters counters;
        counters.applicationTxPackets = 100;
        counters.applicationRxPackets = 100;
        counters.applicationRxPayloadBytes = 51200; // 100 paquets de 512 octets
        counters.deliveredNetworkBytes = 54000;     // charge utile + en-têtes IPv4/UDP
        counters.routeDiscoveries = 9;

        const double window = 45.0; // s
        const DerivedNetworkMetrics metrics = ComputeDerivedNetworkMetrics(counters, {}, window);

        NS_TEST_ASSERT_MSG_EQ_TOL(*metrics.goodputBitsPerSecond, 8.0 * 51200.0 / 45.0,
                                  TEST_TOLERANCE, "goodput = 8 * B_app^rx / T_eval");
        NS_TEST_ASSERT_MSG_EQ_TOL(*metrics.throughputBitsPerSecond, 8.0 * 54000.0 / 45.0,
                                  TEST_TOLERANCE, "throughput = 8 * B_network^rx / T_eval");
        NS_TEST_ASSERT_MSG_EQ_TOL(*metrics.routeDiscoveryFrequency, 0.2, TEST_TOLERANCE,
                                  "RDF = 9 / 45 = 0,2 s^-1");

        // Le throughput doit dépasser le goodput : le premier inclut des en-têtes.
        NS_TEST_ASSERT_MSG_GT(*metrics.throughputBitsPerSecond, *metrics.goodputBitsPerSecond,
                              "le throughput inclut des octets que le goodput exclut");

        // Fenêtre nulle : débits indéfinis plutôt que division par zéro.
        const DerivedNetworkMetrics degenerate = ComputeDerivedNetworkMetrics(counters, {}, 0.0);
        NS_TEST_ASSERT_MSG_EQ(degenerate.throughputBitsPerSecond.has_value(), false,
                              "une fenêtre nulle rend le throughput indéfini");
    }
};

/**
 * \ingroup mtcaodv
 * M-04 — Éq. (26) délai moyen, et médiane du §17.1.
 */
class DelayTestCase : public TestCase
{
  public:
    DelayTestCase()
        : TestCase("M-04 : Eq.(26) délai moyen et médiane empirique")
    {
    }

  private:
    void DoRun() override
    {
        ObservedCounters counters;
        counters.applicationTxPackets = 4;
        counters.applicationRxPackets = 4;

        // Moyenne = (0,10 + 0,20 + 0,30 + 1,40) / 4 = 0,50 s
        // Médiane = (0,20 + 0,30) / 2 = 0,25 s
        // L'écart entre les deux illustre pourquoi le §17.1 demande aussi la médiane :
        // un seul paquet retardé par une redécouverte de route déplace fortement la
        // moyenne sans que le comportement typique ait changé.
        const std::vector<double> delays = {0.10, 0.20, 0.30, 1.40};

        const DerivedNetworkMetrics metrics = ComputeDerivedNetworkMetrics(counters, delays, 45.0);
        NS_TEST_ASSERT_MSG_EQ_TOL(*metrics.meanEndToEndDelay, 0.5, TEST_TOLERANCE,
                                  "moyenne des délais");
        NS_TEST_ASSERT_MSG_EQ_TOL(*metrics.medianEndToEndDelay, 0.25, TEST_TOLERANCE,
                                  "médiane de quatre valeurs = moyenne des deux centrales");

        // Nombre impair : la médiane est la valeur centrale.
        const std::vector<double> odd = {0.10, 0.20, 0.30};
        const DerivedNetworkMetrics oddMetrics = ComputeDerivedNetworkMetrics(counters, odd, 45.0);
        NS_TEST_ASSERT_MSG_EQ_TOL(*oddMetrics.medianEndToEndDelay, 0.20, TEST_TOLERANCE,
                                  "médiane de trois valeurs");

        // Aucun paquet livré : le délai est indéfini, pas nul.
        const DerivedNetworkMetrics empty = ComputeDerivedNetworkMetrics(counters, {}, 45.0);
        NS_TEST_ASSERT_MSG_EQ(empty.meanEndToEndDelay.has_value(), false,
                              "sans paquet livré le délai moyen est indéfini");
    }
};

/**
 * \ingroup mtcaodv
 * M-05 — Éq. (27) jitter, dans l'ordre de réception, et cas dégénéré.
 */
class JitterTestCase : public TestCase
{
  public:
    JitterTestCase()
        : TestCase("M-05 : Eq.(27) jitter et cas à moins de deux paquets")
    {
    }

  private:
    void DoRun() override
    {
        ObservedCounters counters;
        counters.applicationTxPackets = 4;
        counters.applicationRxPackets = 4;

        // |0,20-0,10| + |0,15-0,20| + |0,35-0,15| = 0,10 + 0,05 + 0,20 = 0,35
        // Jitter = 0,35 / (4-1) = 0,116666...
        const std::vector<double> delays = {0.10, 0.20, 0.15, 0.35};
        const DerivedNetworkMetrics metrics = ComputeDerivedNetworkMetrics(counters, delays, 45.0);
        NS_TEST_ASSERT_MSG_EQ_TOL(*metrics.jitter, 0.35 / 3.0, TEST_TOLERANCE,
                                  "somme des variations absolues divisée par N_rx - 1");

        // L'ordre compte : la même multiset dans un autre ordre donne un autre jitter.
        // C'est voulu — l'Éq. (27) est définie sur la suite ordonnée des réceptions.
        const std::vector<double> reordered = {0.10, 0.15, 0.20, 0.35};
        const DerivedNetworkMetrics other =
            ComputeDerivedNetworkMetrics(counters, reordered, 45.0);
        NS_TEST_ASSERT_MSG_EQ_TOL(*other.jitter, 0.25 / 3.0, TEST_TOLERANCE,
                                  "0,05 + 0,05 + 0,15 = 0,25 sur trois intervalles");

        // Un seul paquet livré : le dénominateur N_rx - 1 est nul, la métrique est
        // indéfinie et non nulle.
        const std::vector<double> single = {0.10};
        const DerivedNetworkMetrics degenerate =
            ComputeDerivedNetworkMetrics(counters, single, 45.0);
        NS_TEST_ASSERT_MSG_EQ(degenerate.jitter.has_value(), false,
                              "moins de deux paquets rend le jitter indéfini");
    }
};

/**
 * \ingroup mtcaodv
 * M-06 — Éq. (28) NRO, normalisé par les paquets livrés.
 */
class RoutingOverheadTestCase : public TestCase
{
  public:
    RoutingOverheadTestCase()
        : TestCase("M-06 : Eq.(28) NRO normalisé par les paquets livrés")
    {
    }

  private:
    void DoRun() override
    {
        ObservedCounters counters;
        counters.applicationTxPackets = 200;
        counters.applicationRxPackets = 180;
        counters.aodvControlTransmissions = 540;

        const DerivedNetworkMetrics metrics = ComputeDerivedNetworkMetrics(counters, {}, 45.0);
        NS_TEST_ASSERT_MSG_EQ_TOL(*metrics.normalizedRoutingOverhead, 3.0, TEST_TOLERANCE,
                                  "540 transmissions de contrôle pour 180 paquets livrés");

        // Aucun paquet livré : le NRO est indéfini. Le mettre à zéro laisserait croire à
        // une surcharge nulle alors que le contrôle a bien été émis.
        counters.applicationRxPackets = 0;
        const DerivedNetworkMetrics nothingDelivered =
            ComputeDerivedNetworkMetrics(counters, {}, 45.0);
        NS_TEST_ASSERT_MSG_EQ(nothingDelivered.normalizedRoutingOverhead.has_value(), false,
                              "sans livraison, le NRO est indéfini");
    }
};

/**
 * \ingroup mtcaodv
 * M-07 — Invariants numériques du §20.1 sur les métriques réseau.
 */
class MetricInvariantTestCase : public TestCase
{
  public:
    MetricInvariantTestCase()
        : TestCase("M-07 : invariants 0 <= PDR,PLR <= 1 et PDR + PLR = 1")
    {
    }

  private:
    void DoRun() override
    {
        // Cas conforme.
        ObservedCounters counters;
        counters.applicationTxPackets = 100;
        counters.applicationRxPackets = 95;
        const DerivedNetworkMetrics valid = ComputeDerivedNetworkMetrics(counters, {}, 45.0);
        std::string violation;
        NS_TEST_ASSERT_MSG_EQ(CheckNetworkMetricInvariants(valid, &violation), true,
                              "un jeu de métriques cohérent ne doit signaler aucune violation");

        // Cas pathologique : plus de paquets reçus qu'émis. Le calcul n'écrête pas — il
        // produit un PDR > 1 — et le contrôle d'invariants doit le détecter. Écrêter
        // masquerait un défaut de comptage au lieu de le révéler.
        counters.applicationRxPackets = 120;
        const DerivedNetworkMetrics impossible = ComputeDerivedNetworkMetrics(counters, {}, 45.0);
        NS_TEST_ASSERT_MSG_GT(*impossible.packetDeliveryRatio, 1.0,
                              "le calcul ne doit pas écrêter silencieusement");
        NS_TEST_ASSERT_MSG_EQ(CheckNetworkMetricInvariants(impossible, &violation), false,
                              "un PDR supérieur à 1 doit être signalé");
        NS_TEST_ASSERT_MSG_EQ(violation.empty(), false, "la violation doit être décrite");

        // Un délai négatif est impossible physiquement et doit être signalé.
        DerivedNetworkMetrics negativeDelay;
        negativeDelay.meanEndToEndDelay = -0.01;
        NS_TEST_ASSERT_MSG_EQ(CheckNetworkMetricInvariants(negativeDelay, &violation), false,
                              "un délai négatif doit être signalé");
    }
};

/**
 * \ingroup mtcaodv
 * M-08 — Le CSV commence par les colonnes normatives, dans l'ordre imposé.
 */
class CsvSchemaOrderTestCase : public TestCase
{
  public:
    CsvSchemaOrderTestCase()
        : TestCase("M-08 : ordre normatif des colonnes CSV")
    {
    }

  private:
    void DoRun() override
    {
        const std::vector<std::string> expected = {
            "protocol",     "nodes",         "simTime",      "minSpeed",   "maxSpeed",
            "seed",         "run",           "attackerRatio","attackerCount","attackStart",
            "appTxPackets", "appRxPackets",  "appTxBytes",   "appRxBytes", "pdr",
            "plr",          "throughput_bps","goodput_bps",  "meanDelay_s"};

        const std::vector<std::string>& mandatory = RunRecord::GetMandatoryColumns();
        NS_TEST_ASSERT_MSG_EQ(mandatory.size(), expected.size(),
                              "le schéma obligatoire doit compter 19 colonnes");
        for (size_t i = 0; i < expected.size(); ++i)
        {
            NS_TEST_ASSERT_MSG_EQ(mandatory[i], expected[i],
                                  "colonne obligatoire " << i << " hors de l'ordre normatif");
        }

        // L'ordre de la ligne d'en-tête ne dépend pas de l'ordre de remplissage : on
        // remplit délibérément à l'envers.
        RunRecord record;
        for (auto it = expected.rbegin(); it != expected.rend(); ++it)
        {
            record.SetString(*it, "0");
        }

        std::string header = record.GetHeaderLine();
        std::string joined;
        for (size_t i = 0; i < expected.size(); ++i)
        {
            joined += (i ? "," : "") + expected[i];
        }
        NS_TEST_ASSERT_MSG_EQ(header, joined,
                              "l'en-tête doit suivre l'ordre normatif quel que soit le "
                              "remplissage");
    }
};

/**
 * \ingroup mtcaodv
 * M-09 — Une colonne obligatoire non renseignée est une erreur, pas un zéro.
 */
class CsvFailClosedTestCase : public TestCase
{
  public:
    CsvFailClosedTestCase()
        : TestCase("M-09 : colonne obligatoire manquante rejetée (invariant 20.4.6)")
    {
    }

  private:
    void DoRun() override
    {
        RunRecord record;
        record.SetString("protocol", "aodv");
        record.SetUnsigned("nodes", 20);
        // Toutes les autres colonnes obligatoires restent vides.

        bool rejected = false;
        try
        {
            record.Validate();
        }
        catch (const std::runtime_error&)
        {
            rejected = true;
        }
        NS_TEST_ASSERT_MSG_EQ(rejected, true,
                              "une colonne obligatoire manquante doit provoquer une erreur");

        // En revanche, une métrique explicitement non applicable est acceptée : elle est
        // écrite « NaN », ce qui est une information, pas une absence.
        RunRecord complete;
        for (const std::string& name : RunRecord::GetMandatoryColumns())
        {
            complete.SetString(name, "0");
        }
        complete.SetMetric("pdr", std::nullopt);
        bool accepted = true;
        try
        {
            complete.Validate();
        }
        catch (const std::exception&)
        {
            accepted = false;
        }
        NS_TEST_ASSERT_MSG_EQ(accepted, true, "une métrique « NaN » explicite est valide");
        NS_TEST_ASSERT_MSG_EQ(complete.GetValueLine().find("NaN") != std::string::npos, true,
                              "la valeur non applicable doit apparaître « NaN »");

        // Une valeur contenant une virgule casserait l'alignement des colonnes : refus.
        bool separatorRejected = false;
        try
        {
            complete.SetString("protocol", "aodv,fork");
        }
        catch (const std::invalid_argument&)
        {
            separatorRejected = true;
        }
        NS_TEST_ASSERT_MSG_EQ(separatorRejected, true,
                              "une valeur contenant une virgule doit être refusée");
    }
};

/**
 * \ingroup mtcaodv
 * M-10 — Une étape ultérieure ajoute des colonnes sans déplacer les précédentes.
 *
 * C'est la propriété qui garantit qu'un script d'agrégation écrit à l'étape 0 reste
 * valide à l'étape 11.
 */
class CsvExtensionTestCase : public TestCase
{
  public:
    CsvExtensionTestCase()
        : TestCase("M-10 : extension du schéma sans déplacement des colonnes validées")
    {
    }

  private:
    void DoRun() override
    {
        RunRecord record;
        for (const std::string& name : RunRecord::GetMandatoryColumns())
        {
            record.SetString(name, "1");
        }
        const std::string baseHeader = record.GetHeaderLine();
        const size_t baseCount = record.GetColumnCount();

        // Colonnes typiques des étapes 1 et 2.
        record.SetUnsigned("forgedRrepCount", 12);
        record.SetUnsigned("blackholeDropCount", 340);
        record.SetUnsigned("watchCount", 3);

        const std::string extendedHeader = record.GetHeaderLine();
        NS_TEST_ASSERT_MSG_EQ(extendedHeader.rfind(baseHeader, 0), 0,
                              "l'en-tête étendu doit commencer par l'en-tête de l'étape 0");
        NS_TEST_ASSERT_MSG_EQ(record.GetColumnCount(), baseCount + 3,
                              "trois colonnes doivent avoir été ajoutées");

        // Réécrire une colonne existante ne la déplace pas en fin de ligne.
        record.SetString("pdr", "0.95");
        NS_TEST_ASSERT_MSG_EQ(record.GetHeaderLine(), extendedHeader,
                              "réécrire une valeur ne doit pas modifier l'ordre des colonnes");
    }
};

/**
 * \ingroup mtcaodv
 * M-11 — Validation fail-closed de la configuration pilote (§13.3, §21).
 */
class PilotValidationTestCase : public TestCase
{
  public:
    PilotValidationTestCase()
        : TestCase("M-11 : la configuration pilote rejette les cas impossibles")
    {
    }

  private:
    /// Vrai si Validate() lève sur la configuration fournie.
    static bool IsRejected(const PilotConfiguration& config)
    {
        try
        {
            config.Validate();
        }
        catch (const std::invalid_argument&)
        {
            return true;
        }
        return false;
    }

    void DoRun() override
    {
        // La configuration par défaut de l'étape 0 doit être acceptée telle quelle.
        const PilotConfiguration reference;
        NS_TEST_ASSERT_MSG_EQ(IsRejected(reference), false,
                              "la configuration par défaut de l'étape 0 doit être valide");
        NS_TEST_ASSERT_MSG_EQ(reference.nodeCount, 20u, "N = 20 par défaut");
        NS_TEST_ASSERT_MSG_EQ_TOL(reference.simulationTime, 60.0, TEST_TOLERANCE,
                                  "durée de 60 s par défaut");
        NS_TEST_ASSERT_MSG_EQ_TOL(reference.minSpeed, 1.0, TEST_TOLERANCE, "vitesse min 1 m/s");
        NS_TEST_ASSERT_MSG_EQ_TOL(reference.maxSpeed, 20.0, TEST_TOLERANCE, "vitesse max 20 m/s");

        // Fenêtre d'évaluation : 60 - 10 - 5 = 45 s.
        NS_TEST_ASSERT_MSG_EQ_TOL(reference.GetEvaluationWindowSeconds(), 45.0, TEST_TOLERANCE,
                                  "T_eval = simTime - warmup - drain");

        // Plafond d'adressage : un /24 n'offre que 254 adresses d'hôte.
        PilotConfiguration tooManyNodes = reference;
        tooManyNodes.nodeCount = 255;
        NS_TEST_ASSERT_MSG_EQ(IsRejected(tooManyNodes), true,
                              "255 nœuds ne tiennent pas dans 10.1.0.0/24");

        // Vitesse minimale nulle : refusée (les deux modèles RWP exigent v_min > 0).
        PilotConfiguration zeroSpeed = reference;
        zeroSpeed.minSpeed = 0.0;
        NS_TEST_ASSERT_MSG_EQ(IsRejected(zeroSpeed), true, "minSpeed nul doit être refusé");

        // Vitesses inversées.
        PilotConfiguration invertedSpeed = reference;
        invertedSpeed.minSpeed = 20.0;
        invertedSpeed.maxSpeed = 1.0;
        NS_TEST_ASSERT_MSG_EQ(IsRejected(invertedSpeed), true, "maxSpeed < minSpeed refusé");

        // Warm-up et vidange couvrant toute la simulation : fenêtre vide.
        PilotConfiguration emptyWindow = reference;
        emptyWindow.warmupTime = 40.0;
        emptyWindow.drainTime = 25.0;
        NS_TEST_ASSERT_MSG_EQ(IsRejected(emptyWindow), true,
                              "une fenêtre d'évaluation vide doit être refusée");

        // Ratio d'attaquants hors domaine (décision D-01).
        PilotConfiguration badRatio = reference;
        badRatio.attackerRatio = 1.5;
        NS_TEST_ASSERT_MSG_EQ(IsRejected(badRatio), true, "ratio > 1 refusé");
        badRatio.attackerRatio = std::nan("");
        NS_TEST_ASSERT_MSG_EQ(IsRejected(badRatio), true, "ratio non fini refusé");

        // Trop de flux pour la population : chaque flux consomme deux nœuds.
        PilotConfiguration tooManyFlows = reference;
        tooManyFlows.flowCount = 11; // 22 nœuds requis pour 20 disponibles
        NS_TEST_ASSERT_MSG_EQ(IsRejected(tooManyFlows), true,
                              "22 extrémités pour 20 nœuds doit être refusé");

        // Flux RNG négatif : la reproductibilité ne serait plus garantie.
        PilotConfiguration badStream = reference;
        badStream.mobilityStream = -1;
        NS_TEST_ASSERT_MSG_EQ(IsRejected(badStream), true, "index de flux négatif refusé");
    }
};

/**
 * \ingroup mtcaodv
 * M-12 — L'empreinte de scénario ignore le protocole et capte tout le reste.
 *
 * C'est le contrôle mécanique de l'appariement (invariant 20.4.4) : deux variantes
 * doivent partager exactement les mêmes coordonnées exogènes.
 */
class ScenarioHashTestCase : public TestCase
{
  public:
    ScenarioHashTestCase()
        : TestCase("M-12 : empreinte de scénario identique entre protocoles appariés")
    {
    }

  private:
    void DoRun() override
    {
        PilotConfiguration stock;
        PilotConfiguration fork = stock;
        fork.protocol = PilotProtocol::MTC_AODV_FORK;

        NS_TEST_ASSERT_MSG_EQ(stock.ComputeScenarioHash(), fork.ComputeScenarioHash(),
                              "changer de protocole ne doit pas changer le scénario");

        // Toute modification d'une coordonnée exogène doit au contraire changer
        // l'empreinte, sans quoi le contrôle d'appariement serait aveugle.
        PilotConfiguration otherSeed = stock;
        otherSeed.seed = stock.seed + 1;
        NS_TEST_ASSERT_MSG_NE(stock.ComputeScenarioHash(), otherSeed.ComputeScenarioHash(),
                              "un seed différent doit changer l'empreinte");

        PilotConfiguration otherSpeed = stock;
        otherSpeed.maxSpeed = 15.0;
        NS_TEST_ASSERT_MSG_NE(stock.ComputeScenarioHash(), otherSpeed.ComputeScenarioHash(),
                              "une vitesse différente doit changer l'empreinte");

        PilotConfiguration otherArea = stock;
        otherArea.areaWidth = 700.0;
        NS_TEST_ASSERT_MSG_NE(stock.ComputeScenarioHash(), otherArea.ComputeScenarioHash(),
                              "une zone différente doit changer l'empreinte");

        // L'empreinte est stable d'un appel à l'autre pour une même configuration.
        NS_TEST_ASSERT_MSG_EQ(stock.ComputeScenarioHash(), stock.ComputeScenarioHash(),
                              "l'empreinte doit être déterministe");
        NS_TEST_ASSERT_MSG_EQ(stock.ComputeScenarioHash().size(), 8u,
                              "l'empreinte fait 8 caractères hexadécimaux");
    }
};

/**
 * \ingroup mtcaodv
 * M-13 — Une grandeur non applicable s'écrit « null » en JSON et « NaN » en CSV.
 *
 * Régression : la première version du manifest écrivait la valeur brute, donc « nan »
 * pour un flux dont les extrémités n'avaient jamais été connectées. Le manifest devenait
 * un JSON invalide et faisait échouer la validation A7.2 de l'exécution entière — pour
 * une grandeur pourtant légitimement absente. Le défaut a été détecté par
 * validate_step0.py sur le balayage à faible mobilité (seed 1004).
 */
class JsonNumberTestCase : public TestCase
{
  public:
    JsonNumberTestCase()
        : TestCase("M-13 : « null » en JSON pour une grandeur non applicable")
    {
    }

  private:
    void DoRun() override
    {
        // Valeur finie : littéral numérique, jamais de guillemets.
        NS_TEST_ASSERT_MSG_EQ(FormatJsonNumber(2.5), std::string("2.5"),
                              "une valeur finie s'écrit telle quelle");
        NS_TEST_ASSERT_MSG_EQ(FormatJsonNumber(0.0), std::string("0"),
                              "un zéro mesuré reste un zéro");

        // Non applicable : « null », jamais « nan » (JSON invalide) ni 0 (donnée fausse).
        NS_TEST_ASSERT_MSG_EQ(FormatJsonNumber(std::nan("")), std::string("null"),
                              "NaN doit devenir null en JSON");
        NS_TEST_ASSERT_MSG_EQ(FormatJsonNumber(std::numeric_limits<double>::infinity()),
                              std::string("null"), "une valeur infinie doit devenir null");
        NS_TEST_ASSERT_MSG_EQ(FormatJsonNumber(MetricValue{}), std::string("null"),
                              "une métrique absente doit devenir null");
        NS_TEST_ASSERT_MSG_EQ(FormatJsonNumber(MetricValue{0.75}), std::string("0.75"),
                              "une métrique présente s'écrit telle quelle");

        // Les deux conventions coexistent : « NaN » dans le CSV, « null » dans le JSON.
        NS_TEST_ASSERT_MSG_EQ(FormatMetric(MetricValue{}), std::string("NaN"),
                              "le CSV conserve le marqueur « NaN »");
    }
};

/**
 * \ingroup mtcaodv
 * Suite des tests de métriques, de schéma CSV et de configuration pilote (étape 0).
 */
class MtcAodvMetricsTestSuite : public TestSuite
{
  public:
    MtcAodvMetricsTestSuite()
        : TestSuite("mtcaodv-metrics", Type::UNIT)
    {
        AddTestCase(new DeliveryRatioTestCase, TestCase::Duration::QUICK);
        AddTestCase(new UndefinedRatioTestCase, TestCase::Duration::QUICK);
        AddTestCase(new ThroughputTestCase, TestCase::Duration::QUICK);
        AddTestCase(new DelayTestCase, TestCase::Duration::QUICK);
        AddTestCase(new JitterTestCase, TestCase::Duration::QUICK);
        AddTestCase(new RoutingOverheadTestCase, TestCase::Duration::QUICK);
        AddTestCase(new MetricInvariantTestCase, TestCase::Duration::QUICK);
        AddTestCase(new CsvSchemaOrderTestCase, TestCase::Duration::QUICK);
        AddTestCase(new CsvFailClosedTestCase, TestCase::Duration::QUICK);
        AddTestCase(new CsvExtensionTestCase, TestCase::Duration::QUICK);
        AddTestCase(new PilotValidationTestCase, TestCase::Duration::QUICK);
        AddTestCase(new ScenarioHashTestCase, TestCase::Duration::QUICK);
        AddTestCase(new JsonNumberTestCase, TestCase::Duration::QUICK);
    }
};

static MtcAodvMetricsTestSuite g_mtcAodvMetricsTestSuite; //!< Instance statique de la suite.
