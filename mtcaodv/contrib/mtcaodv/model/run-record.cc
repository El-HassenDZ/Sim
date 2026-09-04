/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "run-record.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace ns3
{
namespace mtcaodv
{

namespace
{

/**
 * Colonnes obligatoires de l'étape 0, dans l'ordre normatif.
 *
 * Les dix premières décrivent la *condition expérimentale* : elles suffisent à
 * réexécuter la simulation à l'identique. Les neuf suivantes sont les compteurs observés
 * et les métriques primaires. Aucun autre ordre n'est admis, et aucune de ces colonnes
 * ne peut être renommée par une étape ultérieure : les scripts d'agrégation en dépendent.
 *
 * Correspondance avec la spécification :
 * - `attackerCount` : Éq. (2) ;
 * - `appTxPackets`, `appRxPackets` : \f$N_{app}^{tx}\f$, \f$N_{app}^{rx}\f$ ;
 * - `appTxBytes`, `appRxBytes` : octets de charge utile applicative, \f$B_{app,payload}\f$ ;
 * - `pdr` : Éq. (20) ; `plr` : Éq. (24) ;
 * - `throughput_bps`, `goodput_bps` : Éq. (25) ; `meanDelay_s` : Éq. (26).
 */
const std::vector<std::string> MANDATORY_COLUMNS = {
    // --- Identité et paramètres de la condition -----------------------------------
    "protocol",
    "nodes",
    "simTime",
    "minSpeed",
    "maxSpeed",
    "seed",
    "run",
    "attackerRatio",
    "attackerCount",
    "attackStart",
    // --- Compteurs observés --------------------------------------------------------
    "appTxPackets",
    "appRxPackets",
    "appTxBytes",
    "appRxBytes",
    // --- Métriques primaires -------------------------------------------------------
    "pdr",
    "plr",
    "throughput_bps",
    "goodput_bps",
    "meanDelay_s",
};

} // namespace

RunRecord::RunRecord()
{
    // Les colonnes obligatoires sont déclarées vides : leur position est ainsi fixée
    // avant tout remplissage, et Validate() peut distinguer « pas encore renseignée »
    // de « renseignée à NaN ».
    for (const std::string& name : MANDATORY_COLUMNS)
    {
        m_index[name] = m_columns.size();
        m_columns.emplace_back(name, std::string());
        m_filled[name] = false;
    }
}

const std::vector<std::string>&
RunRecord::GetMandatoryColumns()
{
    return MANDATORY_COLUMNS;
}

void
RunRecord::RejectSeparators(const std::string& text, const std::string& context)
{
    // Un échappement CSV correct existe, mais aucune valeur du projet — identifiants,
    // nombres, étiquettes de protocole — n'a besoin de ces caractères. Les refuser
    // transforme une corruption silencieuse du fichier en erreur immédiate.
    if (text.find(',') != std::string::npos || text.find('"') != std::string::npos ||
        text.find('\n') != std::string::npos || text.find('\r') != std::string::npos)
    {
        throw std::invalid_argument("RunRecord : séparateur CSV interdit dans " + context +
                                    " (« " + text + " »)");
    }
}

void
RunRecord::SetString(const std::string& name, const std::string& value)
{
    RejectSeparators(name, "un nom de colonne");
    RejectSeparators(value, "la valeur de la colonne « " + name + " »");

    auto position = m_index.find(name);
    if (position == m_index.end())
    {
        // Colonne nouvelle : elle est ajoutée en fin de ligne. C'est le mécanisme
        // d'extension utilisé par les étapes suivantes.
        m_index[name] = m_columns.size();
        m_columns.emplace_back(name, value);
    }
    else
    {
        m_columns[position->second].second = value;
    }
    m_filled[name] = true;
}

void
RunRecord::SetUnsigned(const std::string& name, uint64_t value)
{
    SetString(name, std::to_string(value));
}

void
RunRecord::SetDouble(const std::string& name, double value)
{
    if (!std::isfinite(value))
    {
        // Distinction volontaire avec SetMetric() : une métrique peut être indéfinie,
        // un paramètre de configuration non. Une valeur non finie ici est un défaut de
        // configuration, qui doit interrompre l'exécution plutôt que produire un
        // fichier de résultats trompeur.
        throw std::invalid_argument("RunRecord : valeur non finie pour la colonne « " + name +
                                    " » ; utiliser SetMetric() pour une métrique non applicable");
    }
    std::ostringstream stream;
    stream.precision(9);
    stream << std::defaultfloat << value;
    SetString(name, stream.str());
}

void
RunRecord::SetMetric(const std::string& name, const MetricValue& value)
{
    // FormatMetric() applique la règle D-22 : « NaN » pour une métrique absente ou non
    // finie, jamais 0.
    SetString(name, FormatMetric(value));
}

void
RunRecord::Validate() const
{
    for (const std::string& name : MANDATORY_COLUMNS)
    {
        const auto filled = m_filled.find(name);
        if (filled == m_filled.end() || !filled->second)
        {
            throw std::runtime_error(
                "RunRecord : colonne obligatoire non renseignée « " + name +
                " ». Une métrique obligatoire absente est une erreur de validation, "
                "jamais un zéro (invariant 20.4.6).");
        }
    }
}

std::string
RunRecord::GetHeaderLine() const
{
    std::ostringstream line;
    for (std::size_t i = 0; i < m_columns.size(); ++i)
    {
        line << (i ? "," : "") << m_columns[i].first;
    }
    return line.str();
}

std::string
RunRecord::GetValueLine() const
{
    std::ostringstream line;
    for (std::size_t i = 0; i < m_columns.size(); ++i)
    {
        line << (i ? "," : "") << m_columns[i].second;
    }
    return line.str();
}

void
RunRecord::WriteCsv(const std::string& path) const
{
    Validate();

    std::ofstream file(path);
    if (!file)
    {
        throw std::runtime_error("RunRecord : impossible d'écrire " + path);
    }
    file << GetHeaderLine() << '\n' << GetValueLine() << '\n';
    if (!file)
    {
        throw std::runtime_error("RunRecord : écriture incomplète de " + path);
    }
}

std::size_t
RunRecord::GetColumnCount() const
{
    return m_columns.size();
}

} // namespace mtcaodv
} // namespace ns3
