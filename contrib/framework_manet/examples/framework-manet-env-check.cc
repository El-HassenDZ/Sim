/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * framework_manet — STEP 0 : validation de l'environnement ns-3.48.
 *
 * Rôle du programme
 * -----------------
 * Programme de diagnostic, sans simulation réseau (aucun nœud n'est créé,
 * Simulator::Run() n'est pas appelé). Il produit sur la sortie standard :
 *
 *  [build]         l'empreinte de l'environnement de compilation (version,
 *                  profil, GSL, int64x64, compilateur) ;
 *  [api-contract]  le verdict de chaque API ns-3 requise par STEP 1–4, avec
 *                  la valeur par défaut des attributs et la signature des
 *                  TraceSources telles que déclarées par ns-3 ;
 *  [rng]           une empreinte du générateur (premiers tirages U(0,1) pour
 *                  seed/run/stream donnés), comparable entre machines ;
 *  VERDICT         PASS ou FAIL.
 *
 * Code de retour : 0 si PASS, 1 si FAIL (version différente de 3.48 ou au
 * moins une API non SUPPORTED). ./ns3 run signale donc l'échec.
 *
 * Usage :
 *   ./ns3 run framework-manet-env-check
 *   ./ns3 run "framework-manet-env-check --seed=12345 --run=1 --rngDraws=3"
 */

#include "ns3/command-line.h"
#include "ns3/double.h"
#include "ns3/framework-manet-api-contract.h"
#include "ns3/random-variable-stream.h"
#include "ns3/rng-seed-manager.h"

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace ns3;
using namespace ns3::fmanet;

namespace
{

/**
 * Affiche l'empreinte de compilation.
 * @param env empreinte retournée par GetBuildEnvironment()
 * @param executable argv[0] : sous ns-3, le nom de l'exécutable encode la
 *        version et le profil (ns<version>-<programme>-<profil>)
 * @return true si la version ns-3 est la version cible
 */
bool
PrintBuildEnvironment(const BuildEnvironment& env, const std::string& executable)
{
    const bool versionOk = (env.ns3Version == TARGET_NS3_VERSION);
    std::cout << "[build]\n"
              << "  ns3Version    : " << env.ns3Version << "  (cible " << TARGET_NS3_VERSION << ") "
              << (versionOk ? "OK" : "MISMATCH") << "\n"
              << "  buildProfile  : " << env.buildProfile << "\n"
              << "  NS_ASSERT     : " << (env.assertsEnabled ? "ON" : "OFF") << "\n"
              << "  NS_LOG        : " << (env.logsEnabled ? "ON" : "OFF") << "\n"
              << "  GSL           : " << (env.gslEnabled ? "ON" : "OFF")
              << (env.gslEnabled
                      ? "  -> erreur DSSS/CCK 5.5/11 Mbit/s : modèle exact (intégration GSL)"
                      : "  -> erreur DSSS/CCK 5.5/11 Mbit/s : approximation Matlab")
              << "\n"
              << "  int64x64      : " << env.int64x64Impl << "\n"
              << "  compiler      : " << env.compiler << "\n"
              << "  __cplusplus   : " << env.cxxStandard << "\n"
              << "  executable    : " << executable << "\n";
    return versionOk;
}

/**
 * Affiche le verdict de chaque exigence du contrat d'API.
 * @param results résultats de CheckBaselineApiContract()
 * @return nombre de violations (statut différent de SUPPORTED)
 */
uint32_t
PrintApiContract(const std::vector<ApiCheckResult>& results)
{
    std::cout << "[api-contract]  " << results.size() << " exigences\n";
    for (const auto& r : results)
    {
        const std::string item = r.requirement.typeName +
                                 (r.requirement.member.empty() ? "" : "::" + r.requirement.member);
        std::cout << "  " << std::left << std::setw(10) << ToString(r.status) << " " << std::setw(7)
                  << r.requirement.neededBy << " " << std::setw(12) << ToString(r.requirement.kind)
                  << " " << item << "\n"
                  << "             " << r.detail << "\n";
    }
    const uint32_t violations = CountContractViolations(results);
    std::cout << "  violations    : " << violations << "\n";
    return violations;
}

/**
 * Affiche une empreinte du générateur pseudo-aléatoire.
 *
 * Deux machines qui affichent les mêmes valeurs pour les mêmes
 * (seed, run, stream) partagent le même générateur MRG32k3a et la même
 * dérivation des sous-flux : c'est une condition nécessaire (non suffisante)
 * à la reproductibilité inter-machines des simulations. La précision 17
 * chiffres rend visible toute différence au dernier bit d'un double.
 *
 * @param seed graine globale
 * @param run numéro de réplication
 * @param draws nombre de tirages par stream
 */
void
PrintRngFingerprint(uint32_t seed, uint64_t run, uint32_t draws)
{
    RngSeedManager::SetSeed(seed);
    RngSeedManager::SetRun(run);
    std::cout << "[rng]  MRG32k3a, seed=" << seed << " run=" << run << "\n";

    // Streams 0 et 1 : deux sous-flux indépendants quelconques. L'affectation
    // définitive des streams aux mécanismes de la baseline sera fixée à STEP 1.
    for (int64_t stream = 0; stream < 2; ++stream)
    {
        Ptr<UniformRandomVariable> u = CreateObject<UniformRandomVariable>();
        u->SetAttribute("Min", DoubleValue(0.0));
        u->SetAttribute("Max", DoubleValue(1.0));
        u->SetStream(stream); // doit suivre SetSeed/SetRun (lus à cet instant)
        std::cout << "  stream " << stream << " :";
        for (uint32_t i = 0; i < draws; ++i)
        {
            std::cout << " " << std::setprecision(17) << u->GetValue();
        }
        std::cout << "\n";
    }
}

} // namespace

int
main(int argc, char* argv[])
{
    // Valeurs par défaut identiques à celles de la future baseline
    // (seed = 12345, run = 1) afin que l'empreinte soit directement comparable.
    uint32_t seed = 12345;
    uint64_t run = 1;
    uint32_t rngDraws = 3;

    CommandLine cmd(__FILE__);
    cmd.AddValue("seed", "Graine globale ns-3 (RngSeedManager::SetSeed), >= 1", seed);
    cmd.AddValue("run", "Numéro de réplication (RngSeedManager::SetRun)", run);
    cmd.AddValue("rngDraws", "Nombre de tirages affichés par stream (1..20)", rngDraws);
    cmd.Parse(argc, argv);

    // Contrôles des paramètres : une valeur invalide est une erreur explicite,
    // jamais corrigée silencieusement.
    if (seed == 0)
    {
        std::cerr << "ERREUR : seed doit être >= 1 (ns-3 refuse une graine nulle)\n";
        return 2;
    }
    if (rngDraws < 1 || rngDraws > 20)
    {
        std::cerr << "ERREUR : rngDraws doit être dans [1, 20]\n";
        return 2;
    }

    std::cout << "=== framework_manet :: STEP 0 :: environment check ===\n";

    const bool versionOk = PrintBuildEnvironment(GetBuildEnvironment(), argv[0]);
    const uint32_t violations = PrintApiContract(CheckBaselineApiContract());
    PrintRngFingerprint(seed, run, rngDraws);

    const bool pass = versionOk && (violations == 0);
    std::cout << "VERDICT : " << (pass ? "PASS" : "FAIL") << std::endl;
    return pass ? 0 : 1;
}
