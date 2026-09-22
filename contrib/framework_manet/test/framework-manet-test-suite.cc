/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * framework_manet — STEP 0 : suite de tests unitaires "framework-manet".
 *
 * Rôle du fichier
 * ---------------
 * Vérifie les trois propriétés dont dépendent toutes les étapes suivantes :
 *
 *  1. le module est compilé contre ns-3.48 (injection CMake de la version) ;
 *  2. toutes les API ns-3 requises par la baseline existent et sont
 *     SUPPORTED dans l'installation réelle (contrat d'API) — avec un contrôle
 *     négatif prouvant que le vérificateur sait détecter une absence ou une
 *     dépréciation (sinon un vérificateur qui répond toujours « OK »
 *     passerait le test) ;
 *  3. le mécanisme de reproductibilité seed + run + stream de ns-3 se
 *     comporte comme la conception expérimentale le suppose.
 *
 * Exécution :
 *   ./test.py --suite=framework-manet
 *   ./ns3 run "test-runner --suite=framework-manet --verbose"
 */

#include "ns3/double.h"
#include "ns3/framework-manet-api-contract.h"
#include "ns3/random-variable-stream.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/test.h"

#include <array>
#include <cstdint>

using namespace ns3;
using namespace ns3::fmanet;

// ===========================================================================
// 1. Version de ns-3
// ===========================================================================

/**
 * @ingroup framework-manet-tests
 *
 * Vérifie que la version ns-3 injectée par CMake est bien la version cible.
 *
 * Objectif  : détecter une compilation contre une autre arborescence ns-3.
 * Entrée    : macro FMANET_NS3_VERSION (fichier VERSION de ns-3).
 * Attendu   : "3.48".
 */
class Ns3VersionTestCase : public TestCase
{
  public:
    Ns3VersionTestCase()
        : TestCase("ns-3 version injected by CMake equals the target version")
    {
    }

  private:
    void DoRun() override
    {
        const BuildEnvironment env = GetBuildEnvironment();
        NS_TEST_ASSERT_MSG_EQ(env.ns3Version,
                              std::string(TARGET_NS3_VERSION),
                              "framework_manet compilé contre ns-"
                                  << env.ns3Version << " au lieu de ns-" << TARGET_NS3_VERSION);
    }
};

// ===========================================================================
// 2a. Contrat d'API — contrôle positif
// ===========================================================================

/**
 * @ingroup framework-manet-tests
 *
 * Vérifie que chaque exigence du contrat est SUPPORTED.
 *
 * Objectif  : garantir que STEP 1–4 n'utiliseront que des API existantes.
 * Entrée    : GetBaselineApiRequirements().
 * Attendu   : 0 violation. Chaque violation est rapportée individuellement
 *             (NS_TEST_EXPECT et non ASSERT) pour obtenir la liste complète.
 */
class ApiContractTestCase : public TestCase
{
  public:
    ApiContractTestCase()
        : TestCase("every baseline API requirement is SUPPORTED in this ns-3 build")
    {
    }

  private:
    void DoRun() override
    {
        const auto results = CheckBaselineApiContract();
        NS_TEST_ASSERT_MSG_EQ(results.empty(), false, "le contrat d'API ne peut pas être vide");

        for (const auto& r : results)
        {
            NS_TEST_EXPECT_MSG_EQ(ToString(r.status),
                                  ToString(ApiStatus::SUPPORTED),
                                  r.requirement.neededBy
                                      << " " << ToString(r.requirement.kind) << " "
                                      << r.requirement.typeName
                                      << (r.requirement.member.empty() ? "" : "::")
                                      << r.requirement.member << " -> " << r.detail);
        }
        NS_TEST_EXPECT_MSG_EQ(CountContractViolations(results), 0u, "violations du contrat");
    }
};

// ===========================================================================
// 2b. Contrat d'API — contrôle négatif
// ===========================================================================

/**
 * @ingroup framework-manet-tests
 *
 * Vérifie que le vérificateur détecte réellement les écarts.
 *
 * Objectif  : un test du contrat n'a de valeur que si le vérificateur peut
 *             échouer. On lui soumet des exigences volontairement fausses.
 * Entrée    : TypeId, attribut et TraceSource inexistants ; alias déprécié
 *             "ns3::BasicEnergySource" (AddDeprecatedName dans
 *             src/energy/model/basic-energy-source.cc de ns-3.48).
 * Attendu   : MISSING pour les trois premiers, DEPRECATED pour l'alias.
 *
 * Remarque : la résolution de l'alias déprécié fait écrire à ns-3 un
 * avertissement « Deprecation warning for name ns3::BasicEnergySource » sur
 * std::cerr. Ce message est attendu et fait partie du contrôle.
 */
class ApiContractNegativeControlTestCase : public TestCase
{
  public:
    ApiContractNegativeControlTestCase()
        : TestCase("API checker reports MISSING and DEPRECATED items (negative control)")
    {
    }

  private:
    void DoRun() override
    {
        using K = ApiItemKind;

        const ApiRequirement missingType{K::TYPE_ID,
                                         "ns3::fmanet::DoesNotExist",
                                         "",
                                         "TEST",
                                         "contrôle négatif"};
        NS_TEST_EXPECT_MSG_EQ(ToString(CheckApiRequirement(missingType).status),
                              ToString(ApiStatus::MISSING),
                              "un TypeId inexistant doit être MISSING");

        const ApiRequirement missingAttribute{K::ATTRIBUTE,
                                              "ns3::aodv::RoutingProtocol",
                                              "NoSuchAttribute",
                                              "TEST",
                                              "contrôle négatif"};
        NS_TEST_EXPECT_MSG_EQ(ToString(CheckApiRequirement(missingAttribute).status),
                              ToString(ApiStatus::MISSING),
                              "un attribut inexistant doit être MISSING");

        const ApiRequirement missingTrace{K::TRACE_SOURCE,
                                          "ns3::PacketSink",
                                          "NoSuchTraceSource",
                                          "TEST",
                                          "contrôle négatif"};
        NS_TEST_EXPECT_MSG_EQ(ToString(CheckApiRequirement(missingTrace).status),
                              ToString(ApiStatus::MISSING),
                              "une TraceSource inexistante doit être MISSING");

        // Garantit que les bibliothèques sont chargées avant la recherche de
        // l'alias (sinon le résultat serait MISSING pour une autre raison).
        AnchorRequiredModules();
        const ApiRequirement deprecatedAlias{K::TYPE_ID,
                                             "ns3::BasicEnergySource",
                                             "",
                                             "TEST",
                                             "contrôle négatif"};
        NS_TEST_EXPECT_MSG_EQ(ToString(CheckApiRequirement(deprecatedAlias).status),
                              ToString(ApiStatus::DEPRECATED),
                              "un alias déprécié doit être DEPRECATED");
    }
};

// ===========================================================================
// 3. Reproductibilité seed + run + stream
// ===========================================================================

/**
 * @ingroup framework-manet-tests
 *
 * Vérifie les propriétés du générateur MRG32k3a de ns-3 sur lesquelles
 * repose la conception expérimentale (réplications, expériences appariées).
 *
 * Objectif  :
 *  (a) même (seed, run, stream)       -> même séquence (reproductibilité) ;
 *  (b) même (seed, run), autre stream -> séquence différente (indépendance
 *      des mécanismes : mobilité, flux, start jitter, futurs attaquants) ;
 *  (c) même (seed, stream), autre run -> séquence différente (réplications).
 * Entrée    : seed = 12345, run ∈ {1, 2}, streams ∈ {7, 8}.
 * Attendu   : (a) égalité exacte, (b) et (c) au moins une valeur différente.
 *
 * Remarque : ns-3 lit la graine et le run au moment de SetStream() ; le run
 * doit donc être fixé avant l'affectation du stream. L'état global
 * (seed, run) est restauré en fin de test pour ne pas influencer les autres
 * suites exécutées dans le même processus.
 */
class RngReproducibilityTestCase : public TestCase
{
  public:
    RngReproducibilityTestCase()
        : TestCase("seed/run/stream triple fully determines a random sequence")
    {
    }

  private:
    /// Nombre de tirages comparés ; suffisant pour qu'une égalité fortuite
    /// de deux séquences distinctes soit négligeable (U(0,1) en double).
    static constexpr std::size_t N_DRAWS = 5;
    using Draws = std::array<double, N_DRAWS>;

    /**
     * Crée une variable U(0,1) sur le stream donné et renvoie N_DRAWS tirages.
     * @param stream index de stream RNG explicite
     * @return les tirages successifs
     */
    static Draws DrawSequence(int64_t stream)
    {
        Ptr<UniformRandomVariable> u = CreateObject<UniformRandomVariable>();
        u->SetAttribute("Min", DoubleValue(0.0));
        u->SetAttribute("Max", DoubleValue(1.0));
        u->SetStream(stream);
        Draws d{};
        for (auto& x : d)
        {
            x = u->GetValue();
        }
        return d;
    }

    void DoRun() override
    {
        const uint32_t savedSeed = RngSeedManager::GetSeed();
        const uint64_t savedRun = RngSeedManager::GetRun();

        RngSeedManager::SetSeed(12345);
        RngSeedManager::SetRun(1);
        const Draws a = DrawSequence(7);
        const Draws aBis = DrawSequence(7);
        const Draws otherStream = DrawSequence(8);

        RngSeedManager::SetRun(2);
        const Draws otherRun = DrawSequence(7);

        // (a) reproductibilité exacte : comparaison bit à bit volontaire.
        for (std::size_t i = 0; i < N_DRAWS; ++i)
        {
            NS_TEST_EXPECT_MSG_EQ(a[i], aBis[i], "tirage " << i << " non reproductible");
        }
        // (b) et (c) : au moins une différence suffit à établir la distinction.
        NS_TEST_EXPECT_MSG_EQ((a != otherStream), true, "deux streams donnent la même séquence");
        NS_TEST_EXPECT_MSG_EQ((a != otherRun), true, "deux runs donnent la même séquence");

        RngSeedManager::SetSeed(savedSeed);
        RngSeedManager::SetRun(savedRun);
    }
};

// ===========================================================================
// Suite
// ===========================================================================

/**
 * @ingroup framework-manet-tests
 *
 * Suite "framework-manet" (type UNIT, durée QUICK).
 */
class FrameworkManetTestSuite : public TestSuite
{
  public:
    FrameworkManetTestSuite()
        : TestSuite("framework-manet", Type::UNIT)
    {
        AddTestCase(new Ns3VersionTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new ApiContractTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new ApiContractNegativeControlTestCase(), TestCase::Duration::QUICK);
        AddTestCase(new RngReproducibilityTestCase(), TestCase::Duration::QUICK);
    }
};

/// Instance statique : enregistre la suite auprès du TestRunner de ns-3.
static FrameworkManetTestSuite g_frameworkManetTestSuite;
