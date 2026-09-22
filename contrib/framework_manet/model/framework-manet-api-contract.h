/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * framework_manet — STEP 0 (structure du module et validation de l'environnement).
 *
 * Rôle du fichier
 * ---------------
 * Déclare le « contrat d'API » de la baseline AODV : la liste explicite des
 * TypeId, attributs et TraceSources de ns-3.48 dont les étapes STEP 1 à STEP 4
 * dépendront, ainsi que les fonctions qui vérifient leur présence à l'exécution
 * dans l'installation ns-3 réellement utilisée.
 *
 * Pourquoi ce fichier existe
 * --------------------------
 * La configuration ns-3 repose largement sur des chaînes de caractères
 * ("ns3::AdhocWifiMac", "TxPowerStart", "RxWithSeqTsSize", ...). Une faute de
 * frappe ou une API renommée entre deux versions de ns-3 ne produit alors pas
 * d'erreur de compilation, mais une erreur d'exécution tardive — ou, pire, un
 * Config::Connect silencieusement sans effet si l'on utilise la variante
 * FailSafe. Le contrat rend cette dépendance explicite, testable et traçable
 * *avant* d'écrire la simulation.
 *
 * Ce que le contrat ne vérifie PAS
 * --------------------------------
 * - la sémantique d'une API (seulement son existence, son niveau de support
 *   et, pour une TraceSource, le nom de la signature déclarée) ;
 * - la justesse scientifique des valeurs par défaut (elles sont seulement
 *   rapportées, pour la traçabilité).
 */

#ifndef FRAMEWORK_MANET_API_CONTRACT_H
#define FRAMEWORK_MANET_API_CONTRACT_H

#include <cstdint>
#include <string>
#include <vector>

namespace ns3
{
/**
 * Espace de noms du module framework_manet.
 *
 * Un espace de noms dédié (comme ns3::aodv pour le module AODV officiel)
 * évite toute collision de symboles avec src/ ou avec d'autres modules de
 * contrib/ présents dans la même arborescence ns-3.
 */
namespace fmanet
{

/**
 * Nature de l'élément d'API requis.
 */
enum class ApiItemKind
{
    TYPE_ID,     //!< Un TypeId enregistré (ex. "ns3::aodv::RoutingProtocol")
    ATTRIBUTE,   //!< Un attribut d'un TypeId (recherche incluant les classes parentes)
    TRACE_SOURCE //!< Une TraceSource d'un TypeId (recherche incluant les classes parentes)
};

/**
 * Résultat de la vérification d'un élément d'API.
 *
 * Les niveaux SUPPORTED / DEPRECATED / OBSOLETE reproduisent
 * ns3::TypeId::SupportLevel ; MISSING signifie que l'élément n'existe pas.
 * Seul SUPPORTED est accepté par la baseline : une API dépréciée peut
 * disparaître dans la version suivante de ns-3 et compromettre la
 * reproductibilité à moyen terme.
 */
enum class ApiStatus
{
    SUPPORTED,  //!< Présent et pleinement supporté
    DEPRECATED, //!< Présent mais marqué obsolescent par ns-3
    OBSOLETE,   //!< Déclaré mais retiré (utilisation = erreur fatale dans ns-3)
    MISSING     //!< Introuvable (nom erroné, module absent ou non chargé)
};

/**
 * Un élément d'API dont la baseline dépend.
 *
 * Chaque exigence porte l'étape consommatrice (neededBy) et sa justification
 * (purpose), de sorte que la liste reste auditable : aucun élément n'est
 * requis « au cas où ».
 */
struct ApiRequirement
{
    ApiItemKind kind;     //!< Nature de l'élément
    std::string typeName; //!< Nom canonique du TypeId (jamais un alias déprécié)
    std::string member;   //!< Nom de l'attribut / TraceSource ; vide pour TYPE_ID
    std::string neededBy; //!< Étape consommatrice ("STEP 1", ...)
    std::string purpose;  //!< Pourquoi la baseline en a besoin
};

/**
 * Résultat de la vérification d'une exigence.
 */
struct ApiCheckResult
{
    ApiRequirement requirement; //!< Exigence vérifiée (copie)
    ApiStatus status;           //!< Verdict
    /**
     * Information de traçabilité, dépendant de la nature de l'élément :
     * - ATTRIBUTE    : valeur initiale (par défaut) sérialisée par ns-3 ;
     * - TRACE_SOURCE : nom de la signature de callback déclarée par ns-3
     *                  (ex. "ns3::Packet::TracedCallback") ;
     * - TYPE_ID      : nom canonique effectivement enregistré ;
     * - tout cas     : message explicatif si status != SUPPORTED.
     */
    std::string detail;
};

/**
 * Empreinte de l'environnement de compilation.
 *
 * Ces informations conditionnent la reproductibilité numérique entre
 * machines. En particulier, HAVE_GSL change le modèle d'erreur DSSS/CCK
 * 5.5 et 11 Mbit/s de ns-3 (src/wifi/model/non-ht/dsss-error-rate-model.cc) :
 * sans GSL, ns-3 utilise une approximation « Matlab » moins précise. Deux
 * machines qui diffèrent sur ce point ne produisent pas les mêmes résultats
 * radio pour la même graine.
 */
struct BuildEnvironment
{
    std::string ns3Version;   //!< Contenu du fichier VERSION de ns-3 (via CMake)
    std::string buildProfile; //!< "debug" (profils debug/default), "release" ou "optimized"
    bool assertsEnabled;      //!< NS_ASSERT actif (NS3_ASSERT_ENABLE)
    bool logsEnabled;         //!< NS_LOG actif (NS3_LOG_ENABLE)
    bool gslEnabled;          //!< HAVE_GSL : modèle d'erreur CCK exact (true) ou approché
    std::string int64x64Impl; //!< Implémentation de ns3::int64x64_t (arithmétique de Time)
    std::string compiler;     //!< Identification du compilateur
    long cxxStandard;         //!< Valeur de __cplusplus
};

/**
 * Version ns-3 ciblée par le projet. Toute autre version est signalée par
 * l'exemple de validation d'environnement.
 */
extern const char* const TARGET_NS3_VERSION;

/**
 * @return l'empreinte de l'environnement de compilation du module.
 *
 * Les macros sont évaluées à la compilation de framework_manet ; elles
 * reflètent donc la configuration ./ns3 configure effectivement utilisée
 * pour ce module (identique à celle de src/ dans une compilation standard).
 */
BuildEnvironment GetBuildEnvironment();

/**
 * Force le chargement des bibliothèques ns-3 dont la baseline dépend.
 *
 * Les TypeId sont enregistrés par des initialiseurs statiques de chaque
 * bibliothèque partagée. Sous Ubuntu, l'éditeur de liens utilise --as-needed
 * par défaut : une bibliothèque dont aucun symbole n'est référencé peut être
 * retirée des dépendances de l'exécutable, et ses TypeId n'existent alors
 * pas à l'exécution. Cette fonction référence un symbole de chaque module
 * requis, ce qui rend la vérification du contrat indépendante de ce
 * comportement de l'éditeur de liens.
 *
 * Elle vérifie aussi, à la compilation, la signature de fonctions statiques
 * utilisées dans les étapes suivantes (export des tables de routage, NetAnim),
 * qui ne sont pas des TypeId et ne peuvent donc pas être vérifiées par nom.
 *
 * @return le nombre de modules ancrés (utilisé seulement pour éviter que le
 *         compilateur n'élimine les appels).
 */
uint32_t AnchorRequiredModules();

/**
 * @return la liste ordonnée des exigences d'API de la baseline (STEP 1–4).
 */
const std::vector<ApiRequirement>& GetBaselineApiRequirements();

/**
 * Vérifie une exigence sans effet de bord.
 *
 * Contrairement à TypeId::LookupTraceSourceByName, cette fonction ne
 * provoque ni message sur std::cerr (élément déprécié) ni NS_FATAL_ERROR
 * (élément obsolète) : elle parcourt elle-même la hiérarchie des TypeId et
 * rapporte le niveau de support, afin que la validation d'environnement
 * puisse lister *tous* les écarts en une seule exécution.
 *
 * @param requirement l'exigence à vérifier
 * @return le verdict et l'information de traçabilité associée
 */
ApiCheckResult CheckApiRequirement(const ApiRequirement& requirement);

/**
 * Vérifie l'ensemble des exigences de la baseline.
 *
 * @return un résultat par exigence, dans l'ordre de GetBaselineApiRequirements()
 */
std::vector<ApiCheckResult> CheckBaselineApiContract();

/**
 * @param results résultats de CheckBaselineApiContract()
 * @return le nombre de résultats dont le statut n'est pas SUPPORTED
 */
uint32_t CountContractViolations(const std::vector<ApiCheckResult>& results);

/**
 * @param kind nature d'un élément d'API
 * @return libellé stable, utilisé dans les sorties texte
 */
std::string ToString(ApiItemKind kind);

/**
 * @param status verdict de vérification
 * @return libellé stable, utilisé dans les sorties texte
 */
std::string ToString(ApiStatus status);

} // namespace fmanet
} // namespace ns3

#endif // FRAMEWORK_MANET_API_CONTRACT_H
