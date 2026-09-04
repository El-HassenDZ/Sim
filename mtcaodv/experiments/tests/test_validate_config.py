"""Tests du validateur de configuration.

Un validateur qui n'accepte que la configuration par défaut ne prouve rien. Chaque test
ci-dessous corrompt un invariant précis de la spécification et vérifie que la
configuration est *rejetée*, avec le contrôle attendu.

Exécution :
    python3 -m pytest experiments/tests/ -q
"""

from __future__ import annotations

import copy
import json
import math
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from validate_config import (  # noqa: E402
    DEFAULT_CONFIG,
    ConfigurationError,
    check_attacker_counts,
    check_committee,
    check_hysteresis,
    check_storage_bounds,
    check_streams,
    check_weights,
    validate,
)


@pytest.fixture
def config():
    """Copie profonde de la configuration par défaut, corruptible sans effet de bord."""
    return copy.deepcopy(json.loads(DEFAULT_CONFIG.read_text(encoding="utf-8")))


def test_default_configuration_is_valid(config):
    assert validate(config) is True


# --- Éq. (6) et (8) : normalisation des poids OCEA ---------------------------------

def test_opportunity_weights_must_sum_to_one(config):
    config["ocea"]["opportunityFeatures"][0]["weight"] += 0.05
    with pytest.raises(ConfigurationError, match="somme des poids"):
        check_weights(config)


def test_benign_weights_must_sum_to_one(config):
    config["ocea"]["benignLossIndicators"][-1]["weight"] = 0.0
    with pytest.raises(ConfigurationError, match="somme des poids"):
        check_weights(config)


def test_duplicate_feature_names_are_rejected(config):
    features = config["ocea"]["opportunityFeatures"]
    features[1]["name"] = features[0]["name"]
    with pytest.raises(ConfigurationError, match="dupliqués"):
        check_weights(config)


# --- Éq. (16a) : taille de comité et borne byzantine ------------------------------

def test_committee_smaller_than_three_f_plus_one_is_rejected(config):
    config["certification"]["committeeSize"]["value"] = 3
    config["certification"]["designedByzantineBound"]["value"] = 1
    with pytest.raises(ConfigurationError, match=r"m_c >= 3\*f_c\+1"):
        check_committee(config)


def test_committee_at_the_minimum_is_accepted(config):
    """m_c = 3 f_c + 1 est le minimum admis, pas une violation."""
    config["certification"]["committeeSize"]["value"] = 7
    config["certification"]["designedByzantineBound"]["value"] = 2
    check_committee(config)  # ne doit pas lever


# --- §5.3, §9.5 : hystérésis de la machine à états --------------------------------

def test_release_threshold_must_exceed_watch_threshold(config):
    config["securityStateMachine"]["releaseThresholdMean"]["value"] = 0.55
    config["securityStateMachine"]["watchThresholdMean"]["value"] = 0.60
    with pytest.raises(ConfigurationError, match="theta_release"):
        check_hysteresis(config)


def test_equal_thresholds_are_rejected(config):
    """Des seuils égaux suppriment l'hystérésis et autorisent l'oscillation (T-21)."""
    value = config["securityStateMachine"]["watchThresholdMean"]["value"]
    config["securityStateMachine"]["releaseThresholdMean"]["value"] = value
    with pytest.raises(ConfigurationError):
        check_hysteresis(config)


# --- A7.1, invariant 20.4.4 : disjonction des flux RNG ----------------------------

def test_overlapping_stream_ranges_are_rejected(config):
    config["streams"]["traffic"]["value"] = 72500  # tombe dans [72000, 73000) de wifi
    with pytest.raises(ConfigurationError, match="chevauchent"):
        check_streams(config)


def test_stream_without_declared_span_is_rejected(config):
    del config["streams"]["routing"]["span"]
    with pytest.raises(ConfigurationError, match="span"):
        check_streams(config)


def test_attacker_stream_of_span_one_coexists_with_neighbours(config):
    """La valeur 73001 imposée par l'Annexe C ne laisse que 999 index avant la plage
    suivante ; comme A2.2 ne consomme qu'un flux, ce n'est pas un conflit."""
    assert config["streams"]["attackerSelection"]["value"] == 73001
    assert config["streams"]["attackerSelection"]["span"] == 1
    check_streams(config)


# --- Éq. (2) : comptes d'attaquants obligatoires ----------------------------------

def test_mandatory_attacker_counts_for_hundred_nodes(config):
    """§16.1 : les quatre ratios doivent donner exactement 5, 10, 20 et 30."""
    check_attacker_counts(config)
    node_count = config["scenario"]["nodeCount"]["value"]
    assert node_count == 100
    assert [math.floor(r * node_count + 0.5)
            for r in config["attack"]["attackerRatios"]["value"]] == [5, 10, 20, 30]


def test_rounding_rule_is_half_up_not_truncation(config):
    """Éq. (2) est un arrondi au plus proche, pas une troncature : N=50 et r_a=0,05
    donnent 3 attaquants, pas 2. Ce point surprend et doit rester testé."""
    assert math.floor(0.05 * 50 + 0.5) == 3
    assert math.floor(0.20 * 50 + 0.5) == 10   # cas de test exigé par le cahier des charges
    assert math.floor(0.10 * 15 + 0.5) == 2


# --- Éq. (18) : bornes de stockage PTMB -------------------------------------------

def test_record_budget_must_exceed_signature_and_digest(config):
    """Un bloc qui ne peut pas contenir ses propres enregistrements signés est une
    configuration impossible, pas une contrainte à ignorer silencieusement."""
    config["ptmb"]["maximumRecordsPerBlock"]["value"] = 64
    with pytest.raises(ConfigurationError, match="budget par enregistrement"):
        check_storage_bounds(config)


def test_zero_block_bound_is_rejected(config):
    config["ptmb"]["maximumRetainedBlocks"]["value"] = 0
    with pytest.raises(ConfigurationError, match="strictement positif"):
        check_storage_bounds(config)


# --- A7 : séparation calibration / confirmation -----------------------------------

def test_overlapping_seed_ranges_are_rejected(config):
    config["campaign"]["calibrationSeedRange"]["value"] = [1, 1500]
    with pytest.raises(ConfigurationError, match="chevauchent"):
        validate(config)


# --- §13.3 : bornes de probabilité ------------------------------------------------

def test_probability_outside_unit_interval_is_rejected(config):
    config["securityStateMachine"]["accusationRiskThreshold"]["value"] = 1.4
    with pytest.raises(ConfigurationError, match=r"hors de \[0,1\]"):
        validate(config)


def test_non_finite_probability_is_rejected(config):
    config["mobetaTrust"]["retentionFactor"]["value"] = float("nan")
    with pytest.raises(ConfigurationError, match="non finie"):
        validate(config)


# --- Invariant 20.1.3 : prior Beta ------------------------------------------------

def test_non_positive_beta_prior_is_rejected(config):
    config["mobetaTrust"]["priorPositiveShape"]["value"] = 0.0
    with pytest.raises(ConfigurationError, match="prior Beta"):
        validate(config)


def test_asymmetric_prior_is_rejected(config):
    """§16.1 impose un prior neutre symétrique ; alpha_0 != beta_0 introduirait un biais
    initial en faveur ou en défaveur de tout nouveau voisin."""
    config["mobetaTrust"]["priorNegativeShape"]["value"] = 2.0
    with pytest.raises(ConfigurationError, match="symétrique"):
        validate(config)
