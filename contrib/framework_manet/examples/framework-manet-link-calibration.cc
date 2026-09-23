/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * framework_manet — STEP 1a : calibration radio statique.
 *
 * Rôle du programme
 * -----------------
 * Mesure, pour UNE configuration radio donnée, le taux de réception d'une
 * liaison isolée en fonction de la distance, séparément pour les trames
 * broadcast (RREQ/HELLO/RERR d'AODV) et unicast (données, RREP). Chaque
 * point de mesure est une simulation indépendante de deux nœuds immobiles
 * (helper/framework-manet-link-probe.h).
 *
 * But scientifique : fournir, AVANT toute simulation MANET et donc avant de
 * connaître un PDR, les éléments factuels des décisions D-05 (détecteur de
 * préambule) et D-06 (débit des broadcasts), et valider le contrôle
 * positif « range ».
 *
 * Sorties
 * -------
 *  - stdout : configuration demandée, configuration effective relue sur les
 *             objets ns-3, résumé par type de trame, contrôles de cohérence ;
 *  - <outputDir>/link_calibration_<tag>.csv : une ligne par (type de trame,
 *             distance), colonnes décrites dans README.md (STEP 1a).
 *
 * Définitions
 * -----------
 *  prr                 = delivered / offered (taux de réception applicatif de
 *                        niveau 2, après retransmissions MAC en unicast) ;
 *                        NaN si offered = 0 ;
 *  prrCi95Low/High     = intervalle de Wilson à 95 % de prr ;
 *  attemptsPerOffered  = senderDataTx / offered (1 = aucune retransmission) ;
 *  dReliable_m         = plus grande distance de la grille telle que
 *                        prr ≥ 0,9 pour TOUTES les distances ≤ elle ;
 *  dDead_m             = plus petite distance de la grille où prr ≤ 0,1.
 *
 * Code de retour : 0 si les contrôles de cohérence passent, 1 sinon, 2 si
 * les paramètres sont invalides.
 */

#include "ns3/command-line.h"
#include "ns3/framework-manet-link-probe.h"
#include "ns3/framework-manet-radio-helper.h"
#include "ns3/framework-manet-statistics.h"
#include "ns3/node-container.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/simulator.h"
#include "ns3/system-path.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;
using namespace ns3::fmanet;

namespace
{

/// Seuils de lecture de la courbe de réception (définitions ci-dessus).
constexpr double PRR_RELIABLE = 0.9;
constexpr double PRR_DEAD = 0.1;

/// Écart toléré entre la puissance prédite par le modèle et celle observée
/// par le PHY [dB]. La propagation étant déterministe, l'écart attendu est
/// nul aux erreurs d'arrondi près.
constexpr double RX_POWER_TOLERANCE_DB = 1e-6;

/**
 * Formate un réel pour le CSV : "NaN" pour une valeur indéfinie (et non le
 * "nan" de iostream), 10 chiffres significatifs sinon.
 */
std::string
Fmt(double x)
{
    if (std::isnan(x))
    {
        return "NaN";
    }
    std::ostringstream os;
    os << std::setprecision(10) << x;
    return os.str();
}

/**
 * Construit le suffixe des fichiers de sortie à partir de la configuration,
 * pour que plusieurs configurations puissent coexister dans outputDir.
 */
std::string
MakeTag(const RadioConfig& c)
{
    std::ostringstream os;
    os << ToString(c.propagation);
    if (c.propagation == PropagationProfile::RANGE)
    {
        os << "-r" << c.maxRangeM;
    }
    os << "_pd-" << (c.preambleDetection ? "on" : "off");
    if (c.preambleDetection)
    {
        os << c.preambleMinRssiDbm;
    }
    os << "_nu-" << c.nonUnicastMode;
    return os.str();
}

/**
 * Installe la radio sur un nœud temporaire, relit sa configuration
 * effective, puis détruit la simulation. Aucun stream RNG n'est tiré.
 */
std::string
DescribeEffectiveRadio(const RadioConfig& config)
{
    NodeContainer node;
    node.Create(1);
    RadioHelper radio(config);
    NetDeviceContainer devices = radio.Install(node);
    const std::string description = DescribeInstalledRadio(devices.Get(0), radio.GetLossModel());
    Simulator::Destroy();
    return description;
}

/**
 * Une ligne de résultat enrichie des quantités dérivées.
 */
struct Row
{
    FrameClass frameClass; //!< type de trame
    uint32_t payloadBytes; //!< charge utile
    LinkProbeResult probe; //!< comptages bruts
    double prr;            //!< delivered / offered
    ProportionInterval ci; //!< IC de Wilson à 95 %
    double attempts;       //!< senderDataTx / offered
};

} // namespace

int
main(int argc, char* argv[])
{
    // ---- Paramètres (valeurs par défaut = spécification + défauts ns-3 pour
    //      les points ouverts D-05 et D-06).
    std::string propagationModel = "logdistance";
    RadioConfig radio;
    double dMin = 5.0;
    double dMax = 400.0;
    double dStep = 5.0;
    uint32_t frames = 1000;
    double intervalMs = 50.0;
    uint32_t broadcastPayload = RREQ_IP_BYTES;
    uint32_t unicastPayload = DATA_IP_BYTES;
    std::string frameClassArg = "both";
    uint32_t numNodes = 20;
    double areaSize = 500.0;
    uint32_t seed = 12345;
    uint64_t run = 1;
    std::string outputDir = "resulls_framwork"; // valeur littérale de la spécification (D-03)

    CommandLine cmd(__FILE__);
    cmd.AddValue("propagationModel", "logdistance | range", propagationModel);
    cmd.AddValue("maxRange", "Portée du profil range [m]", radio.maxRangeM);
    cmd.AddValue("pathLossExponent", "Exposant LogDistance [-]", radio.pathLossExponent);
    cmd.AddValue("referenceLoss", "Perte à 1 m [dB]", radio.referenceLossDb);
    cmd.AddValue("txPower", "Puissance d'émission [dBm]", radio.txPowerDbm);
    cmd.AddValue("preambleDetection",
                 "Détecteur de préambule actif (D-05)",
                 radio.preambleDetection);
    cmd.AddValue("pdMinRssi", "MinimumRssi du détecteur [dBm]", radio.preambleMinRssiDbm);
    cmd.AddValue("pdThreshold", "Seuil SNR du détecteur [dB]", radio.preambleThresholdDb);
    cmd.AddValue("nonUnicastMode",
                 "default (ns-3) ou DsssRate1Mbps|DsssRate2Mbps|DsssRate5_5Mbps|DsssRate11Mbps "
                 "(D-06)",
                 radio.nonUnicastMode);
    cmd.AddValue("dMin", "Première distance [m]", dMin);
    cmd.AddValue("dMax", "Dernière distance [m]", dMax);
    cmd.AddValue("dStep", "Pas de distance [m]", dStep);
    cmd.AddValue("frames", "Trames offertes par point", frames);
    cmd.AddValue("intervalMs", "Intervalle entre trames [ms]", intervalMs);
    cmd.AddValue("broadcastPayload", "Charge utile broadcast [octets]", broadcastPayload);
    cmd.AddValue("unicastPayload", "Charge utile unicast [octets]", unicastPayload);
    cmd.AddValue("frameClass", "both | broadcast | unicast", frameClassArg);
    cmd.AddValue("numNodes", "N pour le degré moyen théorique", numNodes);
    cmd.AddValue("areaSize", "Côté de la zone pour le degré moyen théorique [m]", areaSize);
    cmd.AddValue("seed", "Graine globale ns-3 (>= 1)", seed);
    cmd.AddValue("run", "Numéro de réplication", run);
    cmd.AddValue("outputDir", "Répertoire de sortie", outputDir);
    cmd.Parse(argc, argv);

    // ---- Validation : toute erreur est explicite, rien n'est corrigé.
    std::vector<std::string> errors;
    if (!ParsePropagationProfile(propagationModel, &radio.propagation))
    {
        errors.emplace_back("propagationModel doit valoir logdistance ou range");
    }
    const std::string radioErrors = ValidateRadioConfig(radio);
    if (!radioErrors.empty())
    {
        errors.push_back(radioErrors);
    }
    if (!(dMin >= 0.0) || !(dStep > 0.0) || !(dMax >= dMin))
    {
        errors.emplace_back("il faut 0 <= dMin <= dMax et dStep > 0");
    }
    if (frames == 0 || !(intervalMs > 0.0))
    {
        errors.emplace_back("frames >= 1 et intervalMs > 0 sont requis");
    }
    if (frameClassArg != "both" && frameClassArg != "broadcast" && frameClassArg != "unicast")
    {
        errors.emplace_back("frameClass doit valoir both, broadcast ou unicast");
    }
    if (numNodes < 2 || !(areaSize > 0.0))
    {
        errors.emplace_back("numNodes >= 2 et areaSize > 0 sont requis");
    }
    if (seed == 0)
    {
        errors.emplace_back("seed doit être >= 1");
    }
    if (!errors.empty())
    {
        for (const auto& e : errors)
        {
            std::cerr << "ERREUR : " << e << "\n";
        }
        return 2;
    }

    RngSeedManager::SetSeed(seed);
    RngSeedManager::SetRun(run);

    // Grille calculée par index (et non par accumulation de dStep) pour
    // éviter la dérive d'arrondi en virgule flottante.
    const auto nDistances = static_cast<uint32_t>(std::floor((dMax - dMin) / dStep + 1e-9)) + 1;

    std::vector<FrameClass> classes;
    if (frameClassArg != "unicast")
    {
        classes.push_back(FrameClass::BROADCAST);
    }
    if (frameClassArg != "broadcast")
    {
        classes.push_back(FrameClass::UNICAST);
    }

    const std::string tag = MakeTag(radio);
    SystemPath::MakeDirectories(outputDir);
    const std::string csvPath = outputDir + "/link_calibration_" + tag + ".csv";

    std::cout << "=== framework_manet :: STEP 1a :: link calibration ===\n"
              << "[config] " << DescribeRadioConfig(radio) << "\n"
              << "[config] seed=" << seed << " run=" << run << " frames=" << frames
              << " intervalMs=" << intervalMs << " distances=" << nDistances << " (" << dMin << ".."
              << dMax << " m, pas " << dStep << " m)\n"
              << "[radio.effective]\n"
              << DescribeEffectiveRadio(radio);

    // ---- Balayage.
    std::vector<Row> rows;
    for (const FrameClass frameClass : classes)
    {
        const uint32_t payload =
            (frameClass == FrameClass::BROADCAST) ? broadcastPayload : unicastPayload;
        for (uint32_t i = 0; i < nDistances; ++i)
        {
            LinkProbeConfig probe;
            probe.distanceM = dMin + i * dStep;
            probe.frameClass = frameClass;
            probe.frames = frames;
            probe.payloadBytes = payload;
            probe.interval = MicroSeconds(static_cast<int64_t>(std::llround(intervalMs * 1000.0)));

            Row row{frameClass, payload, RunLinkProbe(radio, probe), 0.0, {}, 0.0};
            const auto& r = row.probe;
            const double nan = std::numeric_limits<double>::quiet_NaN();
            // prr = delivered / offered ; indéfini (NaN) si rien n'a été offert.
            row.prr = (r.offered > 0) ? static_cast<double>(r.delivered) / r.offered : nan;
            row.ci = WilsonInterval(r.delivered, r.offered);
            row.attempts = (r.offered > 0) ? static_cast<double>(r.senderDataTx) / r.offered : nan;
            rows.push_back(row);
        }
    }

    // ---- CSV brut.
    std::ofstream csv(csvPath);
    if (!csv)
    {
        std::cerr << "ERREUR : impossible d'écrire " << csvPath << "\n";
        return 2;
    }
    csv << "profile,maxRange_m,pathLossExponent,referenceLoss_dB,txPower_dBm,"
           "preambleDetection,preambleMinRssi_dBm,nonUnicastMode,dataMode,seed,run,"
           "frameClass,payload_bytes,distance_m,predictedRx_dBm,meanRxSignal_dBm,"
           "meanRxNoise_dBm,offered,delivered,prr,prrCi95Low,prrCi95High,senderDataTx,"
           "attemptsPerOffered,receiverAckTx,observedDataMode,observedAckMode\n";
    for (const auto& row : rows)
    {
        const auto& r = row.probe;
        csv << ToString(radio.propagation) << "," << Fmt(radio.maxRangeM) << ","
            << Fmt(radio.pathLossExponent) << "," << Fmt(radio.referenceLossDb) << ","
            << Fmt(radio.txPowerDbm) << "," << (radio.preambleDetection ? "on" : "off") << ","
            << Fmt(radio.preambleMinRssiDbm) << "," << radio.nonUnicastMode << "," << radio.dataMode
            << "," << seed << "," << run << "," << ToString(row.frameClass) << ","
            << row.payloadBytes << "," << Fmt(r.distanceM) << "," << Fmt(r.predictedRxDbm) << ","
            << Fmt(r.meanRxSignalDbm) << "," << Fmt(r.meanRxNoiseDbm) << "," << r.offered << ","
            << r.delivered << "," << Fmt(row.prr) << "," << Fmt(row.ci.low) << ","
            << Fmt(row.ci.high) << "," << r.senderDataTx << "," << Fmt(row.attempts) << ","
            << r.receiverAckTx << "," << r.dataMode << "," << r.ackMode << "\n";
    }
    csv.close();

    // ---- Contrôles de cohérence (invariants, pas des valeurs attendues).
    uint32_t violations = 0;
    double maxRxPowerGapDb = 0.0;
    for (const auto& row : rows)
    {
        const auto& r = row.probe;
        if (r.offered != frames)
        {
            ++violations; // l'émetteur n'a pas émis toutes ses trames
            std::cout << "[check] FAIL offered=" << r.offered << " != frames=" << frames
                      << " à d=" << r.distanceM << " m\n";
        }
        if (r.delivered > r.offered)
        {
            ++violations; // anomalie d'instrumentation : plus de reçus que d'émis
            std::cout << "[check] FAIL delivered > offered à d=" << r.distanceM << " m\n";
        }
        if (r.senderDataTx < r.offered)
        {
            ++violations; // chaque trame offerte passe au moins une fois par le PHY
            std::cout << "[check] FAIL senderDataTx < offered à d=" << r.distanceM << " m\n";
        }
        if (row.frameClass == FrameClass::BROADCAST && r.senderDataTx != r.offered)
        {
            ++violations; // un broadcast n'est jamais retransmis
            std::cout << "[check] FAIL broadcast retransmis à d=" << r.distanceM << " m\n";
        }
        if (!std::isnan(r.meanRxSignalDbm))
        {
            maxRxPowerGapDb =
                std::max(maxRxPowerGapDb, std::fabs(r.meanRxSignalDbm - r.predictedRxDbm));
        }
    }
    if (maxRxPowerGapDb > RX_POWER_TOLERANCE_DB)
    {
        ++violations;
    }
    std::cout << "[check] max |signal observé − prédit| = " << Fmt(maxRxPowerGapDb) << " dB"
              << (maxRxPowerGapDb > RX_POWER_TOLERANCE_DB ? "  FAIL" : "  OK") << "\n";

    // ---- Résumé par type de trame.
    std::cout << "[summary] seuils : fiable si prr >= " << PRR_RELIABLE
              << ", mort si prr <= " << PRR_DEAD << " ; degré moyen théorique pour N=" << numNodes
              << ", zone " << areaSize << " m (hypothèse uniforme, voir D-15)\n";
    for (const FrameClass frameClass : classes)
    {
        double dReliable = std::numeric_limits<double>::quiet_NaN();
        double dDead = std::numeric_limits<double>::quiet_NaN();
        bool contiguous = true;
        std::string dataModes;
        for (const auto& row : rows)
        {
            if (row.frameClass != frameClass)
            {
                continue;
            }
            // dReliable : on s'arrête au premier point non fiable (contiguïté).
            if (contiguous && row.prr >= PRR_RELIABLE)
            {
                dReliable = row.probe.distanceM;
            }
            else
            {
                contiguous = false;
            }
            if (std::isnan(dDead) && row.prr <= PRR_DEAD)
            {
                dDead = row.probe.distanceM;
            }
            if (row.probe.dataMode != "-" &&
                dataModes.find(row.probe.dataMode) == std::string::npos)
            {
                dataModes += (dataModes.empty() ? "" : ",") + row.probe.dataMode;
            }
        }
        std::cout << "  " << std::left << std::setw(10) << ToString(frameClass)
                  << " dReliable_m=" << Fmt(dReliable) << "  dDead_m=" << Fmt(dDead)
                  << "  degréMoyen(dReliable)="
                  << Fmt(ExpectedMeanDegree(numNodes, dReliable, areaSize))
                  << "  débitObservé=" << (dataModes.empty() ? "-" : dataModes) << "\n";
    }

    std::cout << "[output] " << csvPath << " (" << rows.size() << " lignes)\n"
              << "VERDICT : " << (violations == 0 ? "PASS" : "FAIL") << " (" << violations
              << " violation(s) d'invariant)" << std::endl;
    return violations == 0 ? 0 : 1;
}
