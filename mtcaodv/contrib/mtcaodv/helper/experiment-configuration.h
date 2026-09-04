/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef MTC_AODV_EXPERIMENT_CONFIGURATION_H
#define MTC_AODV_EXPERIMENT_CONFIGURATION_H

#include "ns3/command-line.h"
#include "ns3/nstime.h"

#include <cstdint>
#include <map>
#include <string>

namespace ns3
{
namespace mtcaodv
{

/**
 * \ingroup mtcaodv
 * \brief Variante de protocole évaluée (§16.2).
 *
 * L'ablation causale de la spécification comporte cinq variantes. Seule D peut modifier
 * une décision de routage : l'Éq. (19) place la fraîcheur AODV en premier critère et la
 * confiance en dernier départage, et seul le filtre dur par certificat évince un
 * prochain saut. B, C0 et C sont des variantes de détection et de coût.
 */
enum class ProtocolVariant
{
    A,  //!< AODV standard (module src/aodv non modifié). Baseline.
    B,  //!< Fork + détection RREP et observation du forwarding, sans confiance ni PTMB.
    C0, //!< Fork + détection + Beta conventionnel binaire.
    C,  //!< Fork + détection + MOBeta, décision locale, sans PTMB.
    D   //!< MTC-AODV complet.
};

std::string ToString(ProtocolVariant variant);
ProtocolVariant ParseProtocolVariant(const std::string& label);

/**
 * \ingroup mtcaodv
 * \brief Paramètres exogènes d'une exécution, communs à toutes les variantes appariées.
 *
 * A7.1 exige que mobilité, radio, trafic, attaquants, activation, seed et flux soient
 * identiques entre variantes d'un même bloc : seule la configuration du protocole évalué
 * change. Cette structure regroupe donc exactement ce qui doit rester constant, plus la
 * variante elle-même et le ratio d'attaquants qui définissent la condition.
 */
struct ExperimentConfiguration
{
    // --- Identité de l'exécution ----------------------------------------------------
    ProtocolVariant variant{ProtocolVariant::A};
    uint32_t seed{1};
    uint32_t run{1};
    std::string outputDirectory{"results"};
    std::string runLabel{"run"};

    // --- Topologie et mobilité ------------------------------------------------------
    uint32_t nodeCount{100};
    double areaWidth{1000.0};   //!< m
    double areaHeight{1000.0};  //!< m
    double speedMin{1.0};       //!< m/s
    double speedMax{5.0};       //!< m/s
    double pauseTime{2.0};      //!< s

    // --- Radio ----------------------------------------------------------------------
    std::string dataRate{"DsssRate11Mbps"};
    std::string controlRate{"DsssRate1Mbps"};
    /**
     * Débit des trames non unicast (diffusions).
     *
     * ns-3 émet par défaut toute diffusion au débit de base le plus bas, soit 1 Mbit/s
     * en 802.11b. Or les HELLO et l'inondation des RREQ sont précisément des
     * diffusions : à 100 nœuds, elles occupent alors une fraction majeure du canal et
     * provoquent la congestion qu'elles sont censées résoudre. La sonde de portée
     * montre que la coupure de réception est fixée par CcaSensitivity et non par la
     * modulation : passer les diffusions au débit des données ne réduit donc pas la
     * portée. Effet mesuré : docs/PARAMETERS.md.
     */
    std::string nonUnicastRate{"DsssRate11Mbps"};
    double txPowerDbm{16.0};
    double pathLossExponent{2.2};

    /**
     * Messages HELLO d'AODV.
     *
     * ns-3 les active par défaut, mais AODV dispose aussi d'un retour d'erreur de la
     * couche MAC 802.11, qui détecte une rupture de lien de façon à la fois plus rapide
     * et plus fiable. Laisser les deux actifs produit, à forte densité, des ruptures de
     * lien fantômes : les HELLO entrent en collision, le voisin est déclaré perdu, un
     * RERR est émis et la route est redécouverte inutilement. L'effet mesuré est
     * documenté dans docs/PARAMETERS.md.
     */
    bool enableHello{false};

    // --- Trafic ---------------------------------------------------------------------
    uint32_t flowCount{10};
    uint32_t packetSize{512};   //!< octets de charge utile applicative
    double packetRate{4.0};     //!< paquets par seconde et par flux
    uint16_t applicationPort{9000};

    // --- Temps ----------------------------------------------------------------------
    double warmupEnd{30.0};        //!< s ; exclu des métriques
    double trafficStart{30.0};     //!< s
    double trafficStop{600.0};     //!< s ; dernière émission possible
    double drainTime{10.0};        //!< s de vidange après l'arrêt du trafic

    // --- Attaque --------------------------------------------------------------------
    double attackerRatio{0.0};
    double attackStartTime{50.0};  //!< s
    bool excludeTrafficEndpoints{true};

    // --- Énergie --------------------------------------------------------------------
    double initialEnergy{1000.0};  //!< J

    // --- Flux RNG (A7.1) ------------------------------------------------------------
    int64_t mobilityStream{71000};
    int64_t wifiStream{72000};
    int64_t attackerSelectionStream{73001};
    int64_t trafficStream{74000};
    int64_t routingStream{75000};

    /// Instant de fin de simulation, vidange comprise.
    Time GetSimulationEnd() const;
    /// Fenêtre d'évaluation : du warm-up à la dernière émission possible.
    Time GetEvaluationStart() const;
    Time GetEvaluationEnd() const;

    /// Déclare toutes les options sur la ligne de commande ns-3.
    void RegisterCommandLine(CommandLine& commandLine);

    /**
     * \brief Contrôle fail-closed de cohérence (§13.3).
     * \throw std::invalid_argument à la première violation ; aucune correction silencieuse
     */
    void Validate() const;

    /// Représentation du bloc exogène, destinée au manifest et au hash de scénario (A7.1).
    std::map<std::string, std::string> Describe() const;

    /**
     * \brief Empreinte canonique des coordonnées exogènes.
     *
     * Deux variantes appariées doivent produire le même hash : c'est le contrôle
     * mécanique de l'invariant 20.4.4. La variante elle-même en est donc exclue.
     */
    std::string ComputeScenarioHash() const;
};

} // namespace mtcaodv
} // namespace ns3

#endif /* MTC_AODV_EXPERIMENT_CONFIGURATION_H */
