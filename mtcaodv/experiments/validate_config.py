#!/usr/bin/env python3
"""Validation d'entrée de la configuration MTC-AODV.

Implémente le §13.3 de la spécification :

    « Toute probabilité doit être finie et dans [0,1], toute durée non négative, tout
      poids normalisé, toute limite de stockage positive, et tout flux RNG
      explicitement attribué. Une configuration impossible [...] provoque une erreur
      et non une réduction silencieuse du ratio. »

Le contrôle est *fail-closed* : toute violation est une erreur bloquante, jamais un
avertissement ni une correction silencieuse. Le script sert à la fois de garde-fou en
tête de campagne (A7.1) et de test exécutable de la configuration par défaut.

Usage :
    python3 validate_config.py [chemin/vers/parameters.json]
"""

from __future__ import annotations

import json
import math
import sys
from pathlib import Path

DEFAULT_CONFIG = Path(__file__).parent / "configs" / "default_parameters.json"

# Tolérance de normalisation des poids. Les poids sont saisis en décimal (0,35 + 0,35 +
# 0,20 + 0,10) et leur somme binaire ne vaut pas exactement 1 : c'est attendu et sans
# conséquence, contrairement à une somme réellement différente de 1.
WEIGHT_TOLERANCE = 1e-9

# Statuts admis pour chaque entrée paramétrique (cf. docs/PARAMETERS.md).
VALID_STATUSES = {"FIXED", "PROVISIONAL", "FROZEN"}


class ConfigurationError(Exception):
    """Configuration rejetée. Aucune exécution ne doit suivre."""


def _walk_entries(node, path=""):
    """Parcourt récursivement le JSON et produit (chemin, entrée) pour chaque entrée
    paramétrique, c'est-à-dire chaque dictionnaire portant une clé 'value'."""
    if isinstance(node, dict):
        if "value" in node and "status" in node:
            yield path, node
            return
        for key, child in node.items():
            if key.startswith("_"):  # clés de commentaire
                continue
            yield from _walk_entries(child, f"{path}.{key}" if path else key)
    elif isinstance(node, list):
        for index, child in enumerate(node):
            yield from _walk_entries(child, f"{path}[{index}]")


def _require(condition, message):
    if not condition:
        raise ConfigurationError(message)


def check_statuses(config):
    """Chaque entrée doit déclarer un statut connu ; sans quoi la discipline de gel
    (§16.1, A7) n'est plus vérifiable."""
    for path, entry in _walk_entries(config):
        _require(
            entry["status"] in VALID_STATUSES,
            f"{path}: statut inconnu '{entry['status']}' (attendu {sorted(VALID_STATUSES)})",
        )


def check_probabilities(config):
    """Toute grandeur déclarée comme probabilité, ratio ou seuil dans [0,1] est vérifiée
    explicitement. La liste est nominative : une vérification par heuristique de nom
    laisserait passer des cas et en signalerait de faux."""
    bounded = [
        ("rrepDetector.watchThreshold", config["rrepDetector"]["watchThreshold"]["value"]),
        ("forwardingObserver.observationSamplingRate",
         config["forwardingObserver"]["observationSamplingRate"]["value"]),
        ("mobetaTrust.retentionFactor", config["mobetaTrust"]["retentionFactor"]["value"]),
        ("mobetaTrust.benignRetentionFactor",
         config["mobetaTrust"]["benignRetentionFactor"]["value"]),
        ("mobetaTrust.uncertainRetentionFactor",
         config["mobetaTrust"]["uncertainRetentionFactor"]["value"]),
        ("mobetaTrust.credibleMassOutside",
         config["mobetaTrust"]["credibleMassOutside"]["value"]),
        ("securityStateMachine.badBehaviorLimit",
         config["securityStateMachine"]["badBehaviorLimit"]["value"]),
        ("securityStateMachine.accusationRiskThreshold",
         config["securityStateMachine"]["accusationRiskThreshold"]["value"]),
        ("securityStateMachine.minimumConfidence",
         config["securityStateMachine"]["minimumConfidence"]["value"]),
        ("securityStateMachine.minimumRecentCoverage",
         config["securityStateMachine"]["minimumRecentCoverage"]["value"]),
        ("securityStateMachine.watchThresholdMean",
         config["securityStateMachine"]["watchThresholdMean"]["value"]),
        ("securityStateMachine.releaseThresholdMean",
         config["securityStateMachine"]["releaseThresholdMean"]["value"]),
        ("certification.supportThreshold",
         config["certification"]["supportThreshold"]["value"]),
        ("campaign.confidenceLevel", config["campaign"]["confidenceLevel"]["value"]),
        ("campaign.familywiseAlpha", config["campaign"]["familywiseAlpha"]["value"]),
    ]
    for name, value in bounded:
        _require(math.isfinite(value), f"{name}: valeur non finie")
        _require(0.0 <= value <= 1.0, f"{name}: {value} hors de [0,1]")

    for ratio in config["attack"]["attackerRatios"]["value"]:
        _require(math.isfinite(ratio) and 0.0 <= ratio <= 1.0,
                 f"attack.attackerRatios: ratio invalide {ratio}")

    # delta_C doit être strictement dans ]0,1[ : les quantiles de l'Éq. (13b) ne sont
    # pas définis autrement.
    delta_c = config["mobetaTrust"]["credibleMassOutside"]["value"]
    _require(0.0 < delta_c < 1.0, f"mobetaTrust.credibleMassOutside: {delta_c} doit être dans ]0,1[")


def check_durations(config):
    """Toute durée est non négative et finie ; les fenêtres actives sont strictement
    positives (une fenêtre nulle ne peut rien observer)."""
    strictly_positive = [
        ("rrepDetector.comparisonWindow_s", config["rrepDetector"]["comparisonWindow_s"]["value"]),
        ("forwardingObserver.observationWindow_s",
         config["forwardingObserver"]["observationWindow_s"]["value"]),
        ("mobetaTrust.decayScale_s", config["mobetaTrust"]["decayScale_s"]["value"]),
        ("mobetaTrust.coverageEwmaHalfLife_s",
         config["mobetaTrust"]["coverageEwmaHalfLife_s"]["value"]),
        ("ptmb.evidenceTtl_s", config["ptmb"]["evidenceTtl_s"]["value"]),
        ("ptmb.certificateTtl_s", config["ptmb"]["certificateTtl_s"]["value"]),
        ("scenario.simulationDuration_s", config["scenario"]["simulationDuration_s"]["value"]),
    ]
    for name, value in strictly_positive:
        _require(math.isfinite(value) and value > 0.0,
                 f"{name}: durée {value} doit être finie et strictement positive")

    non_negative = [
        ("attack.attackStartTime_s", config["attack"]["attackStartTime_s"]["value"]),
        ("scenario.warmupEnd_s", config["scenario"]["warmupEnd_s"]["value"]),
        ("scenario.trafficStart_s", config["scenario"]["trafficStart_s"]["value"]),
    ]
    for name, value in non_negative:
        _require(math.isfinite(value) and value >= 0.0, f"{name}: durée négative {value}")


def check_weights(config):
    """Éq. (6) et (8) : les poids d'opportunité et de diagnostic somment à 1."""
    for family in ("opportunityFeatures", "benignLossIndicators"):
        features = config["ocea"][family]
        _require(len(features) > 0, f"ocea.{family}: liste vide")
        total = math.fsum(f["weight"] for f in features)
        _require(abs(total - 1.0) <= WEIGHT_TOLERANCE,
                 f"ocea.{family}: somme des poids = {total!r}, attendu 1 ± {WEIGHT_TOLERANCE}")
        for feature in features:
            _require(0.0 <= feature["weight"] <= 1.0,
                     f"ocea.{family}: poids hors [0,1] pour {feature['name']}")
        names = [f["name"] for f in features]
        _require(len(names) == len(set(names)), f"ocea.{family}: noms dupliqués")


def check_storage_bounds(config):
    """Éq. (18) : toutes les bornes de stockage sont strictement positives et la mémoire
    maximale du registre est explicitement calculable."""
    ptmb = config["ptmb"]
    positive_ints = ["maximumBlockBytes", "maximumRetainedBlocks", "maximumRecordsPerBlock",
                     "maximumPendingPool", "maximumParentsPerBlock", "disseminationFanout",
                     "reconciliationBatchSize"]
    for key in positive_ints:
        value = ptmb[key]["value"]
        _require(isinstance(value, int) and value > 0,
                 f"ptmb.{key}: {value} doit être un entier strictement positif")

    retained = ptmb["maximumRetainedBlocks"]["value"] * ptmb["maximumBlockBytes"]["value"]
    _require(retained > 0, "ptmb: borne mémoire M_i = B_max * S_max non calculable")

    # Un enregistrement signé ne peut pas dépasser la taille d'un bloc.
    signature_bytes = config["crypto"]["signatureSizeBytes"]["value"]
    digest_bytes = config["crypto"]["digestSizeBytes"]["value"]
    minimum_record = signature_bytes + digest_bytes
    per_record_budget = ptmb["maximumBlockBytes"]["value"] / ptmb["maximumRecordsPerBlock"]["value"]
    _require(per_record_budget > minimum_record,
             f"ptmb: budget par enregistrement ({per_record_budget:.0f} o) inférieur au "
             f"minimum signature+digest ({minimum_record} o)")


def check_committee(config):
    """Éq. (16a) : m_c >= 3 f_c + 1 et q_c = m_c - f_c."""
    cert = config["certification"]
    committee_size = cert["committeeSize"]["value"]
    byzantine_bound = cert["designedByzantineBound"]["value"]
    _require(byzantine_bound >= 1, "certification: f_c doit être >= 1")
    _require(committee_size >= 3 * byzantine_bound + 1,
             f"certification: m_c={committee_size} viole m_c >= 3*f_c+1 "
             f"(f_c={byzantine_bound})")
    quorum = committee_size - byzantine_bound
    _require(quorum <= committee_size, "certification: quorum incohérent")
    _require(cert["minimumDistinctContexts"]["value"] >= 1,
             "certification: q_min_ctx doit être >= 1")


def check_hysteresis(config):
    """§5.3 et §9.5 : le seuil de réhabilitation est strictement supérieur au seuil
    d'entrée en surveillance, sans quoi la machine à états oscille."""
    machine = config["securityStateMachine"]
    watch = machine["watchThresholdMean"]["value"]
    release = machine["releaseThresholdMean"]["value"]
    _require(release > watch,
             f"securityStateMachine: theta_release={release} doit être > theta_W={watch}")


def check_beta_prior(config):
    """Invariant 20.1.3 : alpha_0 > 0 et beta_0 > 0. Le §16.1 impose en outre un prior
    neutre symétrique."""
    trust = config["mobetaTrust"]
    alpha0 = trust["priorPositiveShape"]["value"]
    beta0 = trust["priorNegativeShape"]["value"]
    _require(alpha0 > 0 and beta0 > 0, "mobetaTrust: le prior Beta doit être strictement positif")
    _require(math.isclose(alpha0, beta0),
             f"mobetaTrust: prior non symétrique (alpha_0={alpha0}, beta_0={beta0})")


def check_streams(config):
    """A7.1 : chaque composant reçoit une plage de flux RNG explicite et disjointe.

    Un helper ns-3 consomme des index consécutifs à partir de sa base ; la plage réelle
    est donc [base, base + span). On vérifie le recouvrement de ces intervalles plutôt
    qu'un écart forfaitaire entre bases : la sélection des attaquants ne consomme qu'un
    index, et son index 73001 est imposé par l'Annexe C.

    Un flux non assigné ou chevauchant corrèle silencieusement deux sources aléatoires
    et rend l'appariement expérimental irreproductible (§21, invariant 20.4.4).
    """
    entries = {name: entry for name, entry in config["streams"].items()
               if not name.startswith("_")}
    _require(len(entries) >= 6, "streams: composants manquants")

    intervals = []
    for name, entry in entries.items():
        base, span = entry["value"], entry.get("span")
        _require(isinstance(base, int) and base >= 0,
                 f"streams.{name}: index de base invalide ({base})")
        _require(isinstance(span, int) and span >= 1,
                 f"streams.{name}: 'span' manquant ou non positif — une plage doit être "
                 f"explicitement dimensionnée")
        intervals.append((base, base + span, name))

    intervals.sort()
    for (lo1, hi1, name1), (lo2, hi2, name2) in zip(intervals, intervals[1:]):
        _require(hi1 <= lo2,
                 f"streams: plages {name1} [{lo1},{hi1}) et {name2} [{lo2},{hi2}) "
                 f"se chevauchent")


def check_attacker_counts(config):
    """Éq. (2) sur la population principale : les comptes obligatoires 5/10/20/30
    doivent être retrouvés exactement pour N = 100."""
    node_count = config["scenario"]["nodeCount"]["value"]
    expected = {0.05: 5, 0.10: 10, 0.20: 20, 0.30: 30}
    for ratio in config["attack"]["attackerRatios"]["value"]:
        computed = math.floor(ratio * node_count + 0.5)
        if node_count == 100:
            _require(computed == expected[round(ratio, 2)],
                     f"Eq.(2): r_a={ratio}, N={node_count} donne {computed}, "
                     f"attendu {expected[round(ratio, 2)]}")
        _require(computed <= node_count, f"Eq.(2): N_A={computed} > N={node_count}")


def check_temporal_consistency(config):
    """Cohérences temporelles qui n'apparaissent dans aucune équation mais rendraient le
    scénario ininterprétable si elles étaient violées."""
    scenario = config["scenario"]
    attack_start = config["attack"]["attackStartTime_s"]["value"]
    duration = scenario["simulationDuration_s"]["value"]
    warmup = scenario["warmupEnd_s"]["value"]

    _require(attack_start < duration,
             f"attaque démarrée à {attack_start} s après la fin de simulation ({duration} s)")
    _require(scenario["trafficStart_s"]["value"] < duration,
             "le trafic démarre après la fin de la simulation")
    _require(warmup < duration, "le warm-up dépasse la durée de simulation")

    # Le TTL de certificat doit permettre d'observer une réhabilitation (Éq. 34) à
    # l'intérieur du run, sinon RehabilitationTime est structurellement N/A.
    certificate_ttl = config["ptmb"]["certificateTtl_s"]["value"]
    if attack_start + 2 * certificate_ttl > duration:
        print(f"[avertissement] attackStart={attack_start} s + 2*TTL_cert="
              f"{2 * certificate_ttl} s dépasse la durée {duration} s : "
              f"RehabilitationTime (Éq. 34) risque d'être N/A par construction.",
              file=sys.stderr)


def check_calibration_separation(config):
    """A7 : les seeds de calibration et de confirmation doivent être disjoints, faute de
    quoi la phase confirmatoire est contaminée."""
    campaign = config["campaign"]
    calib_lo, calib_hi = campaign["calibrationSeedRange"]["value"]
    conf_lo, conf_hi = campaign["confirmatorySeedRange"]["value"]
    _require(calib_lo <= calib_hi and conf_lo <= conf_hi, "campaign: plage de seeds inversée")
    _require(calib_hi < conf_lo or conf_hi < calib_lo,
             f"campaign: plages de seeds calibration [{calib_lo},{calib_hi}] et "
             f"confirmation [{conf_lo},{conf_hi}] se chevauchent")
    _require(conf_hi - conf_lo + 1 >= campaign["pairedSeedCount"]["value"],
             "campaign: plage confirmatoire trop étroite pour pairedSeedCount")


CHECKS = (
    check_statuses,
    check_probabilities,
    check_durations,
    check_weights,
    check_storage_bounds,
    check_committee,
    check_hysteresis,
    check_beta_prior,
    check_streams,
    check_attacker_counts,
    check_temporal_consistency,
    check_calibration_separation,
)


def validate(config):
    """Exécute tous les contrôles. Lève ConfigurationError au premier échec."""
    for check in CHECKS:
        check(config)
    return True


def main(argv):
    path = Path(argv[1]) if len(argv) > 1 else DEFAULT_CONFIG
    config = json.loads(path.read_text(encoding="utf-8"))
    try:
        validate(config)
    except ConfigurationError as error:
        print(f"CONFIGURATION REJETÉE : {error}", file=sys.stderr)
        return 1

    provisional = [p for p, e in _walk_entries(config) if e["status"] == "PROVISIONAL"]
    frozen = [p for p, e in _walk_entries(config) if e["status"] == "FROZEN"]
    fixed = [p for p, e in _walk_entries(config) if e["status"] == "FIXED"]
    print(f"CONFIGURATION VALIDE : {path}")
    print(f"  {len(CHECKS)} familles de contrôles passées")
    print(f"  FIXED={len(fixed)}  PROVISIONAL={len(provisional)}  FROZEN={len(frozen)}")
    if provisional and not frozen:
        print("  note : aucun paramètre gelé — configuration non utilisable "
              "pour une phase confirmatoire (A7).")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
