/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef MTC_AODV_PILOT_CONFIGURATION_H
#define MTC_AODV_PILOT_CONFIGURATION_H

/**
 * \file
 * \ingroup mtcaodv
 * \brief Configuration normative du scénario pilote MTC-AODV (étape 0 et suivantes).
 *
 * **Rôle du fichier.** `PilotConfiguration` porte l'intégralité des paramètres exogènes
 * d'une exécution du programme `mtc-aodv-pilot` : population, zone, mobilité, radio,
 * trafic, temps, attaque et flux RNG. Elle expose ces paramètres sur la ligne de
 * commande sous les noms normatifs du plan de développement, les valide de façon
 * fail-closed avant toute simulation (§13.3 de la spécification), et produit une
 * empreinte canonique du bloc exogène qui permet de vérifier mécaniquement que deux
 * variantes appariées ont bien tourné sur le même scénario (A7.1, invariant 20.4.4).
 *
 * **Pourquoi une classe distincte de `ExperimentConfiguration`.** Les deux coexistent
 * volontairement et ne servent pas le même objet :
 *
 * - `ExperimentConfiguration` décrit le plan *confirmatoire* du §16.1 (N = 100, 600 s,
 *   cinq variantes A/B/C0/C/D) et pilote le programme hérité `mtcaodv-manet-scenario`.
 * - `PilotConfiguration` décrit le *pilote* réduit et reproductible imposé par l'étape 0
 *   (N = 20 par défaut, 60 s, vitesses 1–20 m/s, adressage 10.1.0.0/24), avec les noms
 *   d'options normatifs `--nodes`, `--simTime`, `--minSpeed`, `--maxSpeed`,
 *   `--attackerRatio`, `--attackStart`, `--seed`, `--run`.
 *
 * Fusionner les deux aurait exigé de renommer les options déjà utilisées par le
 * programme hérité, donc d'invalider ce qui a été mesuré avec lui. La convergence est
 * prévue à l'étape 12, lorsque les paramètres physiques seront gelés ; d'ici là, le
 * pilote est la configuration de référence pour toutes les nouvelles étapes.
 *
 * **Statut des valeurs par défaut.** Les valeurs physiques (zone, propagation,
 * puissance, débits, charge, warm-up) relèvent du point ouvert **C-28** : elles sont
 * *calibrables*, pas gelées. Elles sont donc toutes exposées sur la ligne de commande et
 * jamais enfouies dans le code, conformément à la règle « les valeurs scientifiques
 * encore non gelées doivent rester configurables ».
 */

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
 * \brief Protocole de routage installé sur les nœuds.
 *
 * `STOCK_AODV` utilise le module `src/aodv/` de ns-3.48, laissé strictement intact
 * (invariant 20.3.1) : c'est la baseline A et la référence de l'étape 0.
 * `MTC_AODV_FORK` utilise le fork isolé `contrib/mtcaodv/`. À l'étape 0, le fork est un
 * renommage mécanique d'AODV : les deux doivent donc produire des résultats
 * statistiquement indiscernables, ce qui constitue un test de non-régression du fork.
 */
enum class PilotProtocol
{
    STOCK_AODV,
    MTC_AODV_FORK
};

std::string ToString(PilotProtocol protocol);
PilotProtocol ParsePilotProtocol(const std::string& label);

/**
 * \ingroup mtcaodv
 * \brief Modèle de mobilité du pilote.
 *
 * `RANDOM_WAYPOINT` est `ns3::RandomWaypointMobilityModel`, le Random Waypoint
 * classique demandé par l'étape 0. Sa densité et sa vitesse moyenne dérivent pendant les
 * premières dizaines de secondes (réf. 22 de la spécification, Yoon et al.) : le
 * warm-up, exclu de la fenêtre d'évaluation, existe précisément pour cela.
 *
 * `STEADY_STATE_RWP` est `ns3::SteadyStateRandomWaypointMobilityModel`, la variante en
 * régime stationnaire proposée au §16.1. Elle supprime ce transitoire par construction.
 * Elle est conservée comme option car le §16.1 la propose, mais l'étape 0 utilise par
 * défaut le RWP classique, qui est ce que le plan de développement prescrit.
 */
enum class PilotMobility
{
    RANDOM_WAYPOINT,
    STEADY_STATE_RWP
};

std::string ToString(PilotMobility mobility);
PilotMobility ParsePilotMobility(const std::string& label);

/**
 * \ingroup mtcaodv
 * \brief Modèle de propagation du pilote.
 *
 * `LOG_DISTANCE` produit une portée dépendante de la distance, avec des liens marginaux
 * qui apparaissent et disparaissent. C'est la situation réaliste, et c'est aussi celle
 * que le mécanisme OCEA devra savoir distinguer d'une malveillance (§9.2) : la retenir
 * dès l'étape 0 évite de calibrer la baseline sur un canal que les étapes suivantes
 * n'utiliseront pas.
 *
 * `RANGE_DISC` impose un disque de portée fixe : réception parfaite en deçà, nulle
 * au-delà. Aucun lien marginal, donc aucune perte ambiguë. Réservé aux fixtures
 * causales contrôlées, il retire au scénario la difficulté même que le système prétend
 * traiter et ne doit pas servir de scénario principal.
 */
enum class PilotPropagation
{
    LOG_DISTANCE,
    RANGE_DISC
};

std::string ToString(PilotPropagation propagation);
PilotPropagation ParsePilotPropagation(const std::string& label);

/**
 * \ingroup mtcaodv
 * \brief Paramètres exogènes d'une exécution du pilote.
 *
 * Toutes les grandeurs physiques portent leur unité dans le commentaire du champ.
 * Les champs sont publics : la structure est un porteur de configuration validé en bloc
 * par `Validate()`, et non un objet à invariants continus.
 */
struct PilotConfiguration
{
    // --- Identité de l'exécution ----------------------------------------------------
    PilotProtocol protocol{PilotProtocol::STOCK_AODV}; //!< Protocole évalué.
    uint32_t seed{12345};                              //!< Seed RNG ns-3.
    uint32_t run{1};                                   //!< Numéro de run RNG ns-3.
    std::string outputDirectory{"results"};            //!< Répertoire des sorties.
    std::string runLabel{"pilot"};                     //!< Préfixe des fichiers produits.

    // --- Population, zone et mobilité -----------------------------------------------
    /**
     * Nombre de nœuds \f$N\f$.
     *
     * Plafonné à 254 par la validation : l'adressage normatif du projet pilote est
     * 10.1.0.0/24, qui n'offre pas davantage d'adresses d'hôte. Dépasser silencieusement
     * ce plafond produirait des adresses dupliquées et des résultats ininterprétables.
     */
    uint32_t nodeCount{20};
    double areaWidth{600.0};  //!< Largeur de la zone, en m. Calibrable (C-28).
    double areaHeight{600.0}; //!< Hauteur de la zone, en m. Calibrable (C-28).
    double minSpeed{1.0};     //!< Vitesse minimale, en m/s.
    double maxSpeed{20.0};    //!< Vitesse maximale, en m/s.
    double pauseTime{2.0};    //!< Pause aux points de passage, en s.
    PilotMobility mobility{PilotMobility::RANDOM_WAYPOINT};

    // --- Radio ----------------------------------------------------------------------
    std::string dataRate{"DsssRate11Mbps"};    //!< Débit des trames unicast de données.
    std::string controlRate{"DsssRate1Mbps"};  //!< Débit des trames de contrôle 802.11.
    /**
     * Débit des trames non unicast (diffusions).
     *
     * ns-3 émet par défaut toute diffusion au débit de base le plus bas. Or les HELLO et
     * l'inondation des RREQ sont précisément des diffusions : les laisser à 1 Mbit/s
     * leur fait occuper une fraction très supérieure du canal, et la congestion qui en
     * résulte serait attribuée à tort au protocole évalué.
     */
    std::string nonUnicastRate{"DsssRate11Mbps"};
    double txPowerDbm{16.0};       //!< Puissance d'émission, en dBm.
    double pathLossExponent{2.2};  //!< Exposant du modèle log-distance, sans unité.
    PilotPropagation propagation{PilotPropagation::LOG_DISTANCE};
    double radioRange{215.0};      //!< Portée du disque dur, en m ; modèle RANGE_DISC seul.
    /**
     * Rayon utilisé par le diagnostic topologique hors ligne, en m.
     *
     * N'a aucun effet sur la simulation : il sert uniquement à estimer le degré du
     * graphe et la connectivité géométrique, afin de distinguer une perte imputable au
     * protocole d'une absence pure et simple de chemin.
     */
    double connectivityRadius{215.0};

    // --- AODV -----------------------------------------------------------------------
    bool enableHello{true};     //!< Messages HELLO d'AODV.
    double helloInterval{1.0};  //!< Période des HELLO, en s.

    // --- Trafic ---------------------------------------------------------------------
    uint32_t flowCount{4};       //!< Nombre de flux CBR UDP.
    uint32_t packetSize{512};    //!< Charge utile applicative, en octets, en-tête de mesure compris.
    double packetRate{4.0};      //!< Débit par flux, en paquets/s.
    uint16_t applicationPort{9000}; //!< Port UDP du premier flux ; les suivants incrémentent.

    // --- Temps ----------------------------------------------------------------------
    /**
     * Durée totale simulée, en s. `Simulator::Stop()` est armé sur cette valeur.
     */
    double simulationTime{60.0};
    /**
     * Warm-up exclu de la fenêtre d'évaluation, en s.
     *
     * Deux transitoires distincts se superposent au début d'une exécution : la dérive de
     * densité du Random Waypoint, et la convergence initiale du routage réactif (aucune
     * route n'existe avant la première découverte). Compter ces deux phases comme des
     * pertes protocolaires pénaliserait toutes les variantes de la même façon, mais
     * réduirait la sensibilité de la comparaison sans rien lui apprendre.
     */
    double warmupTime{10.0};
    /**
     * Vidange après l'arrêt du trafic, en s.
     *
     * Le trafic s'arrête à `simulationTime - drainTime` pour laisser aux derniers paquets
     * le temps d'arriver. Sans cette marge, les paquets encore en vol à l'instant final
     * seraient comptés comme perdus : un biais systématique, d'autant plus fort que le
     * délai est grand, donc corrélé à la condition testée.
     */
    double drainTime{5.0};

    // --- Attaque (câblée au fork à l'étape 1) ---------------------------------------
    double attackerRatio{0.0};   //!< \f$r_a\f$, sans unité, dans [0,1].
    double attackStartTime{10.0};//!< \f$t_{attack}\f$, en s.
    bool excludeTrafficEndpoints{true}; //!< Exclure sources et puits du tirage d'attaquants.

    // Profil full Blackhole (§8.1, Annexe C). Ces valeurs restent configurables car ni
    // le plan de développement ni la spécification ne les gèlent pour le pilote ; seule
    // leur sémantique (Éq. 23, règle d'abandon) est fixée.
    uint32_t sequenceNumberOffset{1000}; //!< \f$\Delta_{seq}\f$, Éq. (23).
    uint32_t advertisedHopCount{1};      //!< \f$h_{fake}\f$, sauts annoncés.
    double forgedRouteLifetime{30.0};    //!< \f$T_{fake}\f$, durée annoncée, en s.
    bool dropTransitData{true};          //!< Abandonner les données en transit (A2.4).
    bool preserveControlPlane{true};     //!< Exempter le plan de contrôle de l'abandon.

    // --- Flux RNG réservés (A7.1) ---------------------------------------------------
    // Plages disjointes par composant : deux variantes appariées consomment exactement
    // les mêmes coordonnées aléatoires exogènes. Aucune de ces valeurs n'est tirée au
    // hasard ni dérivée du seed ; ce sont des index fixes.
    int64_t positionStream{70000};          //!< Placement initial des nœuds.
    int64_t mobilityStream{71000};          //!< Trajectoires (vitesse, pause, destinations).
    int64_t wifiStream{72000};              //!< Couche Wi-Fi.
    int64_t attackerSelectionStream{73001}; //!< Tirage des attaquants (Annexe C).
    int64_t trafficStream{74000};           //!< Gigue de démarrage des flux CBR.
    int64_t routingStream{75000};           //!< Aléa interne du protocole de routage.

    /// Instant de fin de simulation.
    Time GetSimulationEnd() const;
    /// Instant de première émission applicative.
    Time GetTrafficStart() const;
    /// Instant de dernière émission applicative possible.
    Time GetTrafficStop() const;
    /// Début de la fenêtre d'évaluation (fin du warm-up).
    Time GetEvaluationStart() const;
    /// Fin de la fenêtre d'évaluation (dernière émission possible).
    Time GetEvaluationEnd() const;
    /// Durée de la fenêtre d'évaluation \f$T_{eval}\f$, en s.
    double GetEvaluationWindowSeconds() const;

    /**
     * \brief Déclare toutes les options sur la ligne de commande ns-3.
     *
     * Les huit options normatives de l'étape 0 (`nodes`, `simTime`, `minSpeed`,
     * `maxSpeed`, `attackerRatio`, `attackStart`, `seed`, `run`) sont déclarées les
     * premières ; les paramètres calibrables suivent.
     *
     * \param commandLine ligne de commande à compléter
     * \param protocolLabel référence recevant l'étiquette de protocole, relue après
     *        `Parse()` — `CommandLine` ne sait pas remplir une énumération
     * \param mobilityLabel idem pour le modèle de mobilité
     * \param propagationLabel idem pour le modèle de propagation
     */
    void RegisterCommandLine(CommandLine& commandLine,
                             std::string& protocolLabel,
                             std::string& mobilityLabel,
                             std::string& propagationLabel);

    /**
     * \brief Contrôle fail-closed de cohérence, avant toute simulation (§13.3).
     * \throw std::invalid_argument à la première violation
     *
     * Aucune valeur n'est corrigée silencieusement : une configuration impossible doit
     * provoquer une erreur, jamais une réduction tacite du paramètre demandé (§21).
     */
    void Validate() const;

    /**
     * \brief Description canonique du bloc exogène, pour le manifest et l'empreinte.
     * \return les paramètres sous forme (clé, valeur), ordonnés par clé
     *
     * Le protocole évalué en est **exclu** : c'est précisément la grandeur qui doit
     * varier entre deux exécutions appariées, alors que tout le reste doit coïncider.
     */
    std::map<std::string, std::string> Describe() const;

    /**
     * \brief Empreinte 32 bits des coordonnées exogènes.
     * \return l'empreinte en hexadécimal sur 8 caractères
     *
     * Deux exécutions appariées doivent afficher la même empreinte. C'est le contrôle
     * mécanique de l'invariant 20.4.4 ; il est vérifié par `validate_step0.py`.
     */
    std::string ComputeScenarioHash() const;
};

} // namespace mtcaodv
} // namespace ns3

#endif /* MTC_AODV_PILOT_CONFIGURATION_H */
