/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef MTC_AODV_RUN_RECORD_H
#define MTC_AODV_RUN_RECORD_H

/**
 * \file
 * \ingroup mtcaodv
 * \brief Enregistrement CSV d'une exécution : schéma ordonné, extensible et fail-closed.
 *
 * **Rôle du fichier.** Une exécution de scénario produit exactement une ligne de
 * résultats. `RunRecord` construit cette ligne et garantit trois propriétés que le plan
 * expérimental exige et qu'un simple `ofstream` ne donne pas :
 *
 * 1. **Ordre de colonnes stable et déclaré.** Les colonnes obligatoires de l'étape 0
 *    apparaissent toujours en tête, dans l'ordre normatif, quel que soit l'ordre dans
 *    lequel le programme les remplit. Les étapes ultérieures ajoutent leurs colonnes
 *    *après*, sans jamais déplacer ni renommer les précédentes : un script d'agrégation
 *    écrit à l'étape 0 continue donc de fonctionner à l'étape 11.
 * 2. **Règle fail-closed (invariant 20.4.6, décision D-22).** Une colonne obligatoire
 *    laissée vide provoque une erreur de validation à l'écriture. Une métrique
 *    réellement indéfinie doit être écrite explicitement « NaN » via `SetMetric()`.
 *    L'absence de valeur et la valeur zéro ne sont jamais confondues.
 * 3. **Absence de corruption du format.** Une valeur contenant une virgule, un guillemet
 *    ou un saut de ligne décalerait silencieusement toutes les colonnes suivantes. Elle
 *    est refusée plutôt qu'échappée : aucune donnée du projet n'a besoin de ces
 *    caractères, et une erreur bruyante vaut mieux qu'un fichier subtilement faux.
 *
 * **Ce que la classe ne fait pas.** Elle ne calcule aucune métrique et n'en connaît
 * aucune définition ; elle ne lit ni n'agrège de fichier existant. C'est un formateur.
 */

#include "network-metrics.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ns3
{
namespace mtcaodv
{

/**
 * \ingroup mtcaodv
 * \brief Une ligne de résultats d'exécution, au format CSV normatif du projet.
 *
 * Utilisation typique dans un programme de scénario :
 * \code
 *   RunRecord record;
 *   record.SetString("protocol", "aodv");
 *   record.SetUnsigned("nodes", 20);
 *   ...
 *   record.SetMetric("pdr", report.packetDeliveryRatio);
 *   record.WriteCsv("results/pilot.csv");   // valide puis écrit
 * \endcode
 */
class RunRecord
{
  public:
    /**
     * \brief Construit un enregistrement dont les colonnes obligatoires sont déjà
     *        déclarées, dans l'ordre normatif, mais non renseignées.
     *
     * Pré-déclarer les colonnes fixe l'ordre indépendamment de l'ordre de remplissage.
     * Tant qu'une colonne obligatoire n'a pas été renseignée, `Validate()` échoue.
     */
    RunRecord();

    /**
     * \brief Liste ordonnée des colonnes obligatoires de l'étape 0.
     *
     * Cet ordre est normatif : il est repris tel quel par les scripts de validation et
     * d'agrégation. Une étape ultérieure peut *ajouter* des colonnes, jamais modifier
     * celles-ci.
     *
     * \return les noms de colonnes, dans l'ordre d'écriture
     */
    static const std::vector<std::string>& GetMandatoryColumns();

    /**
     * \brief Renseigne une colonne textuelle.
     * \param name nom de colonne ; s'il est déjà déclaré, la valeur remplace la
     *        précédente **à la même position**, sinon la colonne est ajoutée en fin
     * \param value valeur, sans virgule, guillemet ni saut de ligne
     * \throw std::invalid_argument si le nom ou la valeur contient un séparateur
     */
    void SetString(const std::string& name, const std::string& value);

    /**
     * \brief Renseigne une colonne entière non signée.
     * \param name nom de colonne
     * \param value compteur observé
     */
    void SetUnsigned(const std::string& name, uint64_t value);

    /**
     * \brief Renseigne une colonne réelle toujours définie (un paramètre, pas une mesure).
     * \param name nom de colonne
     * \param value valeur finie ; une valeur non finie est refusée, car un paramètre de
     *        configuration non fini est une erreur de configuration, pas une mesure
     *        manquante
     * \throw std::invalid_argument si la valeur n'est pas finie
     */
    void SetDouble(const std::string& name, double value);

    /**
     * \brief Renseigne une colonne de métrique, éventuellement non applicable.
     * \param name nom de colonne
     * \param value métrique ; `std::nullopt` est écrit « NaN » (règle D-22)
     */
    void SetMetric(const std::string& name, const MetricValue& value);

    /**
     * \brief Vérifie que toutes les colonnes obligatoires sont renseignées.
     * \throw std::runtime_error en nommant la première colonne manquante
     *
     * C'est le point d'application concret de la règle « une métrique obligatoire
     * absente est une erreur de validation, jamais un zéro ».
     */
    void Validate() const;

    /// Ligne d'en-tête CSV, sans saut de ligne final.
    std::string GetHeaderLine() const;
    /// Ligne de valeurs CSV, sans saut de ligne final.
    std::string GetValueLine() const;

    /**
     * \brief Valide puis écrit le fichier CSV (en-tête + une ligne de données).
     * \param path chemin du fichier, écrasé s'il existe
     * \throw std::runtime_error si la validation échoue ou si le fichier est inécrivable
     */
    void WriteCsv(const std::string& path) const;

    /// Nombre de colonnes actuellement déclarées.
    std::size_t GetColumnCount() const;

  private:
    /// Refuse un nom ou une valeur qui casserait le format CSV.
    static void RejectSeparators(const std::string& text, const std::string& context);

    /// Colonnes dans leur ordre d'écriture : (nom, valeur).
    std::vector<std::pair<std::string, std::string>> m_columns;
    /// Index nom -> position dans m_columns, pour un remplacement en place.
    std::map<std::string, std::size_t> m_index;
    /// Colonnes déclarées mais pas encore renseignées.
    std::map<std::string, bool> m_filled;
};

} // namespace mtcaodv
} // namespace ns3

#endif /* MTC_AODV_RUN_RECORD_H */
