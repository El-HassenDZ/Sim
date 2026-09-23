/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * framework_manet — STEP 1a.
 *
 * Rôle du fichier
 * ---------------
 * Point UNIQUE de configuration de la couche radio de la baseline :
 * IEEE 802.11b ad hoc (AdhocWifiMac), PHY/canal Yans, débit fixe
 * (ConstantRateWifiManager), puissance d'émission et profil de propagation.
 *
 * Pourquoi un helper dédié
 * ------------------------
 * La calibration de liaison (STEP 1a) et le scénario MANET (STEP 1b) doivent
 * utiliser *exactement* la même radio. Si chaque programme configurait le
 * Wi-Fi lui-même, une divergence silencieuse (une valeur par défaut oubliée,
 * par exemple ReferenceLoss = 46.6777 dB au lieu de 40.05 dB) invaliderait la
 * calibration. Le helper centralise la configuration et permet de relire la
 * configuration *effective* sur les objets installés (DescribeInstalledRadio).
 *
 * Points ouverts paramétrés, sans décision prise ici (doc/DECISIONS.md) :
 *  - D-05 : détecteur de préambule (défaut ns-3 : actif, MinimumRssi −82 dBm) ;
 *  - D-06 : débit des trames non-unicast (défaut ns-3 : NonUnicastMode non fixé).
 * Les valeurs par défaut de RadioConfig reproduisent le comportement ns-3.48
 * pour ces deux points, et la spécification du projet pour tous les autres.
 */

#ifndef FRAMEWORK_MANET_RADIO_HELPER_H
#define FRAMEWORK_MANET_RADIO_HELPER_H

#include "ns3/mobility-model.h"
#include "ns3/net-device-container.h"
#include "ns3/node-container.h"
#include "ns3/propagation-loss-model.h"
#include "ns3/yans-wifi-channel.h"

#include <cstdint>
#include <string>

namespace ns3
{
namespace fmanet
{

/**
 * Profil de propagation.
 */
enum class PropagationProfile
{
    LOG_DISTANCE, //!< profil principal : LogDistancePropagationLossModel
    RANGE         //!< contrôle positif : RangePropagationLossModel (disque idéal)
};

/// Valeur de RadioConfig::nonUnicastMode signifiant « ne pas fixer
/// l'attribut NonUnicastMode » (comportement par défaut de ns-3, D-06).
extern const char* const NON_UNICAST_MODE_NS3_DEFAULT;

/**
 * Configuration radio complète de la baseline.
 *
 * Unités : dBm pour les puissances absolues, dB pour les pertes et seuils
 * relatifs, mètres pour les distances.
 */
struct RadioConfig
{
    PropagationProfile propagation = PropagationProfile::LOG_DISTANCE; //!< profil
    double pathLossExponent = 3.0;              //!< exposant n de LogDistance [-]
    double referenceLossDb = 40.05;             //!< perte à la distance de référence [dB] (D-17)
    double referenceDistanceM = 1.0;            //!< distance de référence d0 [m]
    double maxRangeM = 250.0;                   //!< portée du profil RANGE [m] (D-02)
    double txPowerDbm = 16.0;                   //!< puissance d'émission [dBm] (D-17)
    std::string dataMode = "DsssRate11Mbps";    //!< débit des trames unicast de données
    std::string controlMode = "DsssRate11Mbps"; //!< débit des trames de contrôle (RTS)
    std::string nonUnicastMode = NON_UNICAST_MODE_NS3_DEFAULT; //!< débit broadcast (D-06)
    bool preambleDetection = true;     //!< détecteur de préambule actif (D-05)
    double preambleMinRssiDbm = -82.0; //!< MinimumRssi du détecteur [dBm] (défaut ns-3)
    double preambleThresholdDb = 4.0;  //!< seuil de SNR du détecteur [dB] (défaut ns-3)
};

/**
 * @param profile profil de propagation
 * @return "logdistance" ou "range" (noms de la CLI)
 */
std::string ToString(PropagationProfile profile);

/**
 * @param name nom CLI ("logdistance" ou "range")
 * @param[out] profile profil correspondant si la conversion réussit
 * @return true si le nom est reconnu
 */
bool ParsePropagationProfile(const std::string& name, PropagationProfile* profile);

/**
 * Vérifie la cohérence d'une configuration radio.
 *
 * Seuls des contrôles de domaine sont faits (valeurs finies, positives,
 * modes DSSS existants) : aucune valeur n'est jugée « bonne » ou « mauvaise »
 * du point de vue scientifique.
 *
 * @param config configuration à vérifier
 * @return chaîne vide si la configuration est valide, sinon la liste des
 *         erreurs séparées par "; "
 */
std::string ValidateRadioConfig(const RadioConfig& config);

/**
 * Résumé d'une ligne de la configuration *demandée*, pour les journaux et
 * le nommage des fichiers de sortie.
 * @param config configuration
 * @return résumé textuel
 */
std::string DescribeRadioConfig(const RadioConfig& config);

/**
 * Relit la configuration radio *effective* sur un équipement installé :
 * standard, fréquence, largeur de canal, puissances et seuils du PHY,
 * détecteur de préambule, gestionnaire de débit et ses modes, modèle de
 * propagation. C'est la preuve que la configuration demandée a bien été
 * appliquée par ns-3.
 *
 * @param device un WifiNetDevice installé par RadioHelper
 * @param lossModel modèle de propagation du canal (RadioHelper::GetLossModel)
 * @return description multi-lignes
 */
std::string DescribeInstalledRadio(Ptr<NetDevice> device, Ptr<PropagationLossModel> lossModel);

/**
 * Installe la radio de la baseline sur des nœuds.
 *
 * Tous les équipements installés par une même instance partagent un seul
 * canal Yans (un seul domaine de collision et de propagation).
 */
class RadioHelper
{
  public:
    /**
     * Construit le canal et le modèle de propagation.
     *
     * @param config configuration radio ; une configuration invalide
     *        (ValidateRadioConfig non vide) provoque NS_ABORT : une erreur de
     *        configuration n'est jamais corrigée silencieusement.
     */
    explicit RadioHelper(const RadioConfig& config);

    /**
     * Installe PHY, MAC ad hoc et gestionnaire de débit sur chaque nœud.
     * Précondition : un MobilityModel est agrégé à chaque nœud avant le
     * premier envoi (exigence du canal Yans, pas de l'installation).
     *
     * @param nodes nœuds cibles
     * @return les WifiNetDevice installés, dans l'ordre des nœuds
     */
    NetDeviceContainer Install(const NodeContainer& nodes) const;

    /**
     * Affecte des streams RNG explicites à toutes les variables aléatoires
     * Wi-Fi des équipements (erreur PHY, backoff MAC, gestionnaire de débit).
     *
     * @param devices équipements retournés par Install()
     * @param stream premier index de stream (bloc Wi-Fi du plan D-07)
     * @return nombre de streams consommés
     */
    static int64_t AssignStreams(NetDeviceContainer devices, int64_t stream);

    /**
     * @return le modèle de propagation du canal (déterministe dans les deux
     *         profils : aucun stream RNG n'est consommé)
     */
    Ptr<PropagationLossModel> GetLossModel() const;

    /**
     * Puissance reçue prédite par le modèle de propagation configuré,
     * sans évanouissement ni gain d'antenne (TxGain = RxGain = 0 dB).
     *
     * @param a mobilité de l'émetteur
     * @param b mobilité du récepteur
     * @return puissance reçue [dBm]
     */
    double PredictRxPowerDbm(Ptr<MobilityModel> a, Ptr<MobilityModel> b) const;

    /**
     * @return la configuration utilisée
     */
    const RadioConfig& GetConfig() const;

  private:
    RadioConfig m_config;                  //!< configuration validée
    Ptr<YansWifiChannel> m_channel;        //!< canal partagé
    Ptr<PropagationLossModel> m_lossModel; //!< modèle de propagation du canal
};

} // namespace fmanet
} // namespace ns3

#endif // FRAMEWORK_MANET_RADIO_HELPER_H
