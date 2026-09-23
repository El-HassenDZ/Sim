/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * framework_manet — STEP 1a : suite de tests "framework-manet-radio".
 *
 * Rôle du fichier
 * ---------------
 * Tests unitaires et d'intégration de la couche radio de la baseline :
 *
 *  1. fonctions statistiques (Wilson, graphe géométrique aléatoire) ;
 *  2. validation de RadioConfig ;
 *  3. application effective du modèle de propagation (valeurs exactes) ;
 *  4. sondes de liaison sur des cas PHYSIQUEMENT CERTAINS uniquement :
 *     aucun seuil n'est ajusté pour faire passer un test. Chaque attendu est
 *     justifié par un bilan de liaison écrit dans le commentaire du cas ;
 *  5. déterminisme et indépendance vis-à-vis de l'ordre des mesures.
 *
 * Bilan de liaison de référence (profil logdistance de la spécification) :
 *   Prx(d) = 16 − 40.05 − 30·log10(d)  [dBm]
 *   bruit  = k·290 K·20 MHz · NF(7 dB) ≈ −93.97 dBm. Valeur mesurée par le
 *            PHY à STEP 1a : ns-3 intègre le bruit sur 20 MHz, et non sur les
 *            22 MHz du DSSS (voir F-14).
 *
 * Exécution :
 *   ./test.py --suite=framework-manet-radio
 */

#include "ns3/constant-position-mobility-model.h"
#include "ns3/framework-manet-link-probe.h"
#include "ns3/framework-manet-radio-helper.h"
#include "ns3/framework-manet-statistics.h"
#include "ns3/simulator.h"
#include "ns3/test.h"

#include <cmath>
#include <string>

using namespace ns3;
using namespace ns3::fmanet;

namespace
{

/// Trames par sonde dans les tests : suffisant pour des cas certains
/// (prr = 0 ou 1 exactement), et rapide.
constexpr uint32_t TEST_FRAMES = 50;

/**
 * Lance une sonde courte avec la configuration donnée.
 */
LinkProbeResult
Probe(const RadioConfig& radio, double distanceM, FrameClass frameClass)
{
    LinkProbeConfig probe;
    probe.distanceM = distanceM;
    probe.frameClass = frameClass;
    probe.frames = TEST_FRAMES;
    probe.payloadBytes = (frameClass == FrameClass::BROADCAST) ? RREQ_IP_BYTES : DATA_IP_BYTES;
    return RunLinkProbe(radio, probe);
}

} // namespace

// ===========================================================================
// 1. Statistiques
// ===========================================================================

/**
 * @ingroup framework-manet-tests
 *
 * Objectif : valider les formules sur des valeurs de référence calculées à
 *            la main (voir framework-manet-statistics.h).
 * Attendu  : Wilson(0/10) = [0, z²/(n+z²)] ; Wilson(10/10) = [n/(n+z²), 1] ;
 *            Wilson(5/10) ≈ [0.2366, 0.7634] ; F(L) = π − 8/3 + 1/2 ;
 *            valeurs indéfinies = NaN.
 */
class StatisticsTestCase : public TestCase
{
  public:
    StatisticsTestCase()
        : TestCase("Wilson interval and random geometric graph formulas")
    {
    }

  private:
    void DoRun() override
    {
        const double z2 = Z_95 * Z_95;
        const double tol = 1e-12;

        const ProportionInterval none = WilsonInterval(0, 10);
        NS_TEST_EXPECT_MSG_EQ(none.low, 0.0, "Wilson 0/10 : borne basse exactement 0");
        NS_TEST_EXPECT_MSG_EQ_TOL(none.high, z2 / (10.0 + z2), tol, "Wilson 0/10 : borne haute");

        const ProportionInterval all = WilsonInterval(10, 10);
        NS_TEST_EXPECT_MSG_EQ_TOL(all.low, 10.0 / (10.0 + z2), tol, "Wilson 10/10 : borne basse");
        NS_TEST_EXPECT_MSG_EQ(all.high, 1.0, "Wilson 10/10 : borne haute exactement 1");
        NS_TEST_EXPECT_MSG_EQ(WilsonInterval(0, 1000).low, 0.0, "Wilson 0/1000 : pas de résidu");

        const ProportionInterval half = WilsonInterval(5, 10);
        NS_TEST_EXPECT_MSG_EQ_TOL(half.low, 0.2365931, 1e-6, "Wilson 5/10 : borne basse");
        NS_TEST_EXPECT_MSG_EQ_TOL(half.high, 0.7634069, 1e-6, "Wilson 5/10 : borne haute");

        NS_TEST_EXPECT_MSG_EQ(std::isnan(WilsonInterval(0, 0).low), true, "0 essai -> NaN");
        NS_TEST_EXPECT_MSG_EQ(std::isnan(WilsonInterval(3, 2).high), true, "k > n -> NaN");

        NS_TEST_EXPECT_MSG_EQ_TOL(UniformSquareLinkProbability(500.0, 500.0),
                                  M_PI - 8.0 / 3.0 + 0.5,
                                  tol,
                                  "F(L) = π − 8/3 + 1/2");
        NS_TEST_EXPECT_MSG_EQ_TOL(UniformSquareLinkProbability(0.0, 500.0), 0.0, tol, "F(0) = 0");
        NS_TEST_EXPECT_MSG_EQ(std::isnan(UniformSquareLinkProbability(600.0, 500.0)),
                              true,
                              "r > L -> NaN (formule non implémentée)");
        NS_TEST_EXPECT_MSG_EQ_TOL(ExpectedMeanDegree(20, 150.0, 500.0),
                                  19.0 * UniformSquareLinkProbability(150.0, 500.0),
                                  tol,
                                  "degré = (N − 1)·F(r)");
        NS_TEST_EXPECT_MSG_EQ(std::isnan(ExpectedMeanDegree(1, 150.0, 500.0)),
                              true,
                              "N < 2 -> NaN");
    }
};

// ===========================================================================
// 2. Validation de RadioConfig
// ===========================================================================

/**
 * @ingroup framework-manet-tests
 *
 * Objectif : la configuration par défaut est valide ; toute valeur hors
 *            domaine est refusée avec un message (jamais corrigée).
 */
class RadioConfigValidationTestCase : public TestCase
{
  public:
    RadioConfigValidationTestCase()
        : TestCase("RadioConfig validation accepts defaults and rejects invalid values")
    {
    }

  private:
    void DoRun() override
    {
        const RadioConfig defaults;
        NS_TEST_EXPECT_MSG_EQ(ValidateRadioConfig(defaults), "", "défauts valides");

        RadioConfig ofdm;
        ofdm.dataMode = "OfdmRate6Mbps"; // défaut de ConstantRateWifiManager, pas 802.11b
        NS_TEST_EXPECT_MSG_NE(ValidateRadioConfig(ofdm), "", "mode OFDM refusé");

        RadioConfig broadcast11;
        broadcast11.nonUnicastMode = "DsssRate11Mbps";
        NS_TEST_EXPECT_MSG_EQ(ValidateRadioConfig(broadcast11), "", "NonUnicastMode DSSS admis");

        RadioConfig badBroadcast;
        badBroadcast.nonUnicastMode = "fast";
        NS_TEST_EXPECT_MSG_NE(ValidateRadioConfig(badBroadcast), "", "NonUnicastMode inconnu");

        RadioConfig badRange;
        badRange.maxRangeM = -1.0;
        NS_TEST_EXPECT_MSG_NE(ValidateRadioConfig(badRange), "", "maxRange négatif refusé");

        RadioConfig badExponent;
        badExponent.pathLossExponent = 0.0;
        NS_TEST_EXPECT_MSG_NE(ValidateRadioConfig(badExponent), "", "exposant nul refusé");

        PropagationProfile profile = PropagationProfile::LOG_DISTANCE;
        NS_TEST_EXPECT_MSG_EQ(ParsePropagationProfile("range", &profile), true, "range reconnu");
        NS_TEST_EXPECT_MSG_EQ((profile == PropagationProfile::RANGE), true, "range converti");
        NS_TEST_EXPECT_MSG_EQ(ParsePropagationProfile("friis", &profile), false, "inconnu refusé");
    }
};

// ===========================================================================
// 3. Propagation effectivement appliquée
// ===========================================================================

/**
 * @ingroup framework-manet-tests
 *
 * Objectif : vérifier que le canal utilise les paramètres de la
 *            spécification et non les défauts ns-3 (ReferenceLoss 46.6777 dB).
 * Attendu  : Prx(1 m) = 16 − 40.05 = −24.05 dBm ;
 *            Prx(d*) = −82 dBm pour d* = 10^((16 − 40.05 + 82)/30) ≈ 85.44 m ;
 *            profil range 150 m : Prx = 16 dBm à 100 m, −1000 dBm à 160 m.
 */
class PropagationTestCase : public TestCase
{
  public:
    PropagationTestCase()
        : TestCase("configured propagation profile yields exact received powers")
    {
    }

  private:
    void DoRun() override
    {
        auto at = [](double x) {
            Ptr<ConstantPositionMobilityModel> m = CreateObject<ConstantPositionMobilityModel>();
            m->SetPosition(Vector(x, 0.0, 0.0));
            return m;
        };
        const double tol = 1e-9;

        const RadioHelper logDistance(RadioConfig{});
        NS_TEST_EXPECT_MSG_EQ_TOL(logDistance.PredictRxPowerDbm(at(0.0), at(1.0)),
                                  -24.05,
                                  tol,
                                  "Prx à 1 m");
        const double dStar = std::pow(10.0, (16.0 - 40.05 + 82.0) / 30.0);
        NS_TEST_EXPECT_MSG_EQ_TOL(logDistance.PredictRxPowerDbm(at(0.0), at(dStar)),
                                  -82.0,
                                  tol,
                                  "Prx au seuil du détecteur de préambule");

        RadioConfig rangeConfig;
        rangeConfig.propagation = PropagationProfile::RANGE;
        rangeConfig.maxRangeM = 150.0;
        const RadioHelper range(rangeConfig);
        NS_TEST_EXPECT_MSG_EQ_TOL(range.PredictRxPowerDbm(at(0.0), at(100.0)),
                                  16.0,
                                  tol,
                                  "range : Prx = Ptx dans la portée");
        NS_TEST_EXPECT_MSG_EQ_TOL(range.PredictRxPowerDbm(at(0.0), at(160.0)),
                                  -1000.0,
                                  tol,
                                  "range : −1000 dBm hors portée");
        Simulator::Destroy();
    }
};

// ===========================================================================
// 4. Sondes de liaison : cas certains
// ===========================================================================

/**
 * @ingroup framework-manet-tests
 *
 * Objectif : vérifier la chaîne complète (radio, MAC, sonde, traces) sur des
 *            cas dont l'issue ne dépend d'aucun tirage aléatoire.
 *
 * Cas et justification :
 *  (a) 10 m : Prx = −54.05 dBm, SNR ≈ 39.9 dB, au-dessus de tous les
 *      seuils -> tout est reçu, sans retransmission, un ACK par trame ;
 *      données unicast à DsssRate11Mbps (DataMode).
 *  (b) 400 m : Prx = −102.1 dBm < RxSensitivity (−101 dBm) -> rien n'est
 *      reçu, que le détecteur de préambule soit actif ou non.
 *  (c) range 150 m : 140 m -> tout reçu ; 160 m -> rien (disque idéal, F-04).
 *  (d) F-01, 90 m : Prx = −82.68 dBm. Détecteur actif (seuil −82 dBm) ->
 *      aucun broadcast reçu. Détecteur désactivé -> tout reçu, car à 1 Mbit/s
 *      comme à 11 Mbit/s le SNR ≈ 11.3 dB > 10 dB rend le TEB nul dans le
 *      modèle DSSS de ns-3 (sans GSL) et négligeable avec GSL.
 *  (e) D-06 : NonUnicastMode = DsssRate11Mbps -> broadcasts à 11 Mbit/s.
 *  (f) F-06 : NonUnicastMode non fixé -> broadcasts à DsssRate1Mbps
 *      (premier mode de base du PHY 802.11b).
 *  (g) le signal observé par le PHY égale la puissance prédite.
 */
class LinkProbeCertainOutcomesTestCase : public TestCase
{
  public:
    LinkProbeCertainOutcomesTestCase()
        : TestCase("link probe reproduces physically certain outcomes")
    {
    }

  private:
    void DoRun() override
    {
        const RadioConfig spec; // défauts : spécification + ns-3 pour D-05/D-06

        // (a) + (g)
        const LinkProbeResult nearB = Probe(spec, 10.0, FrameClass::BROADCAST);
        NS_TEST_EXPECT_MSG_EQ(nearB.offered, TEST_FRAMES, "10 m broadcast : trames offertes");
        NS_TEST_EXPECT_MSG_EQ(nearB.delivered, TEST_FRAMES, "10 m broadcast : tout reçu");
        NS_TEST_EXPECT_MSG_EQ_TOL(nearB.meanRxSignalDbm,
                                  nearB.predictedRxDbm,
                                  1e-6,
                                  "signal observé = prédit");

        const LinkProbeResult nearU = Probe(spec, 10.0, FrameClass::UNICAST);
        NS_TEST_EXPECT_MSG_EQ(nearU.delivered, TEST_FRAMES, "10 m unicast : tout reçu");
        NS_TEST_EXPECT_MSG_EQ(nearU.senderDataTx,
                              TEST_FRAMES,
                              "10 m unicast : aucune retransmission");
        NS_TEST_EXPECT_MSG_EQ(nearU.receiverAckTx, TEST_FRAMES, "10 m unicast : un ACK par trame");
        NS_TEST_EXPECT_MSG_EQ(nearU.dataMode, "DsssRate11Mbps", "unicast à DataMode");

        // (b)
        RadioConfig noPd = spec;
        noPd.preambleDetection = false;
        for (const RadioConfig& c : {spec, noPd})
        {
            NS_TEST_EXPECT_MSG_EQ(Probe(c, 400.0, FrameClass::BROADCAST).delivered,
                                  0u,
                                  "400 m broadcast : sous RxSensitivity");
            NS_TEST_EXPECT_MSG_EQ(Probe(c, 400.0, FrameClass::UNICAST).delivered,
                                  0u,
                                  "400 m unicast : sous RxSensitivity");
        }

        // (c)
        RadioConfig range = spec;
        range.propagation = PropagationProfile::RANGE;
        range.maxRangeM = 150.0;
        NS_TEST_EXPECT_MSG_EQ(Probe(range, 140.0, FrameClass::UNICAST).delivered,
                              TEST_FRAMES,
                              "range 150 m : 140 m reçu");
        NS_TEST_EXPECT_MSG_EQ(Probe(range, 160.0, FrameClass::UNICAST).delivered,
                              0u,
                              "range 150 m : 160 m perdu");

        // (d)
        NS_TEST_EXPECT_MSG_EQ(Probe(spec, 90.0, FrameClass::BROADCAST).delivered,
                              0u,
                              "90 m, détecteur actif : préambule sous −82 dBm ignoré");
        NS_TEST_EXPECT_MSG_EQ(Probe(noPd, 90.0, FrameClass::BROADCAST).delivered,
                              TEST_FRAMES,
                              "90 m, détecteur désactivé : tout reçu");

        // (e)
        RadioConfig broadcast11 = spec;
        broadcast11.nonUnicastMode = "DsssRate11Mbps";
        NS_TEST_EXPECT_MSG_EQ(Probe(broadcast11, 10.0, FrameClass::BROADCAST).dataMode,
                              "DsssRate11Mbps",
                              "NonUnicastMode fixé -> broadcast à 11 Mbit/s");

        // (f)
        NS_TEST_EXPECT_MSG_EQ(nearB.dataMode,
                              "DsssRate1Mbps",
                              "NonUnicastMode non fixé -> broadcast au premier mode de base");
    }
};

// ===========================================================================
// 5. Déterminisme
// ===========================================================================

/**
 * @ingroup framework-manet-tests
 *
 * Objectif : une mesure ne dépend que de sa configuration, de la graine et du
 *            run, et non des mesures exécutées avant elle dans le même
 *            processus. Si une variable aléatoire échappait à l'affectation
 *            explicite des streams, son stream automatique changerait d'une
 *            mesure à l'autre et ce test échouerait.
 * Entrée   : unicast à 110 m, détecteur désactivé. SNR ≈ 8.7 dB : zone de
 *            transition mesurée à STEP 1a (prr ≈ 0,85 avec ≈ 3,7 essais par
 *            trame). Les réceptions ET les retransmissions y dépendent des
 *            tirages d'erreur PHY et de backoff, ce qui donne au test un
 *            pouvoir discriminant ; à 10 m, il serait trivialement vrai.
 * Attendu  : X, puis une autre mesure, puis X -> résultats identiques.
 */
class LinkProbeDeterminismTestCase : public TestCase
{
  public:
    LinkProbeDeterminismTestCase()
        : TestCase("link probe results are deterministic and order-independent")
    {
    }

  private:
    void DoRun() override
    {
        RadioConfig c;
        c.preambleDetection = false;
        const LinkProbeResult first = Probe(c, 110.0, FrameClass::UNICAST);
        Probe(c, 60.0, FrameClass::BROADCAST); // mesure intercalée
        const LinkProbeResult second = Probe(c, 110.0, FrameClass::UNICAST);

        // Garde-fou : le point doit bien être dans la zone aléatoire, sinon le
        // test ne prouverait rien (issue certaine = résultat trivialement égal).
        NS_TEST_EXPECT_MSG_GT(first.senderDataTx,
                              first.offered,
                              "110 m : des retransmissions doivent avoir lieu");

        NS_TEST_EXPECT_MSG_EQ(first.delivered, second.delivered, "delivered identique");
        NS_TEST_EXPECT_MSG_EQ(first.senderDataTx, second.senderDataTx, "senderDataTx identique");
        NS_TEST_EXPECT_MSG_EQ(first.receiverAckTx, second.receiverAckTx, "receiverAckTx identique");
        NS_TEST_EXPECT_MSG_EQ(first.dataMode, second.dataMode, "dataMode identique");
        NS_TEST_EXPECT_MSG_EQ(first.wifiStreamsUsed, second.wifiStreamsUsed, "streams identiques");
    }
};

// ===========================================================================
// Suite
// ===========================================================================

/**
 * @ingroup framework-manet-tests
 *
 * Suite "framework-manet-radio" (STEP 1a).
 */
class FrameworkManetRadioTestSuite : public TestSuite
{
  public:
    FrameworkManetRadioTestSuite()
        : TestSuite("framework-manet-radio", Type::UNIT)
    {
        AddTestCase(new StatisticsTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new RadioConfigValidationTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new PropagationTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new LinkProbeCertainOutcomesTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new LinkProbeDeterminismTestCase(), TestCase::Duration::QUICK);
    }
};

/// Instance statique : enregistre la suite auprès du TestRunner de ns-3.
static FrameworkManetRadioTestSuite g_frameworkManetRadioTestSuite;
