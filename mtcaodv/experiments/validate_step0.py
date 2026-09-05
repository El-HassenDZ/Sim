#!/usr/bin/env python3
"""Validation fail-closed d'une exécution du pilote MTC-AODV (algorithme A7.2, étape 0).

La règle est celle de la spécification : *« interdire l'agrégation de résultats
incomplets, mal étiquetés ou non appariés »* et *« rejeter les métriques manquantes ;
préserver les NaN valides pour les métriques non applicables »* (A7.2, points 7 et 9).

Ce script ne corrige rien et n'impute aucune valeur. Il répond par VALID ou par une
erreur bloquante nommant précisément l'invariant violé.

Contrôles effectués
-------------------
1. Schéma : présence, ordre et types des colonnes obligatoires, d'après
   ``schemas/run_record_step0.json``.
2. Domaines : bornes de chaque colonne ; NaN accepté seulement là où le schéma l'autorise.
3. Cohérence inter-champs X-01 à X-08 : PDR recalculé depuis les compteurs, identité
   PDR + PLR = 1 (Éq. 24), Éq. (2) sur le nombre d'attaquants, etc.
4. Manifest MF-01 à MF-05 : version ns-3, adressage normatif 10.1.0.0/24, unicité et tri
   des identifiants d'attaquants, concordance avec le CSV.
5. Appariement (optionnel) : deux exécutions déclarées appariées doivent porter la même
   empreinte de scénario (invariant 20.4.4).

Usage
-----
    python3 validate_step0.py results/pilot_s12345_metrics.csv
    python3 validate_step0.py results/*_metrics.csv
    python3 validate_step0.py --paired results/a_metrics.csv results/b_metrics.csv

Le code de sortie vaut 0 si toutes les exécutions sont VALID, 1 sinon.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import sys
from pathlib import Path

SCHEMA_PATH = Path(__file__).parent / "schemas" / "run_record_step0.json"

# Tolérance des recalculs. Le CSV écrit 9 chiffres significatifs ; l'écart entre la
# valeur relue et la valeur recalculée depuis les compteurs entiers reste très en deçà.
NUMERIC_TOLERANCE = 1e-6


class ValidationError(Exception):
    """Exécution rejetée. Elle ne doit entrer dans aucune agrégation."""


def _is_nan_token(text: str) -> bool:
    """Vrai si la cellule est le marqueur normatif d'une métrique non applicable.

    Une cellule vide n'est **pas** acceptée : de nombreux outils la lisent comme un
    zéro, ce que la règle D-22 interdit précisément.
    """
    return text.strip() == "NaN"


def _parse_number(text: str, column: str) -> float:
    try:
        return float(text)
    except ValueError as exc:
        raise ValidationError(f"colonne « {column} » : « {text} » n'est pas un nombre") from exc


def _parse_integer(text: str, column: str) -> int:
    try:
        return int(text)
    except ValueError as exc:
        raise ValidationError(
            f"colonne « {column} » : « {text} » n'est pas un entier"
        ) from exc


def load_schema(path: Path = SCHEMA_PATH) -> dict:
    """Charge le schéma normatif de l'étape 0."""
    if not path.is_file():
        raise ValidationError(f"schéma introuvable : {path}")
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


def read_single_row_csv(path: Path) -> dict[str, str]:
    """Lit un CSV à une seule ligne de données et retourne le dictionnaire colonne->texte.

    Un fichier vide, tronqué ou portant plusieurs lignes de données est rejeté : le
    contrat du pilote est « une exécution, une ligne » et une divergence signale une
    écriture concurrente ou un fichier corrompu (A7.2, point 6).
    """
    if not path.is_file():
        raise ValidationError(f"fichier de métriques introuvable : {path}")

    with path.open(encoding="utf-8", newline="") as handle:
        rows = list(csv.reader(handle))

    if len(rows) < 2:
        raise ValidationError(f"{path.name} : en-tête ou ligne de données manquante")
    if len(rows) > 2:
        raise ValidationError(
            f"{path.name} : {len(rows) - 1} lignes de données, une seule est attendue"
        )

    header, values = rows[0], rows[1]
    if len(header) != len(values):
        raise ValidationError(
            f"{path.name} : {len(header)} colonnes d'en-tête pour {len(values)} valeurs"
        )
    return header, dict(zip(header, values))


def check_schema(header: list[str], row: dict[str, str], schema: dict) -> None:
    """Contrôle 1 et 2 : présence, ordre, types et domaines des colonnes obligatoires."""
    mandatory = schema["mandatoryColumns"]

    # Ordre : les colonnes obligatoires doivent être les premières, dans l'ordre exact.
    expected = [entry["name"] for entry in mandatory]
    if header[: len(expected)] != expected:
        raise ValidationError(
            "les colonnes obligatoires ne sont pas en tête dans l'ordre normatif.\n"
            f"  attendu : {expected}\n"
            f"  observé : {header[: len(expected)]}"
        )

    for entry in mandatory:
        name = entry["name"]
        raw = row[name]

        if _is_nan_token(raw):
            if not entry.get("nanAllowed", False):
                raise ValidationError(
                    f"colonne « {name} » : NaN interdit pour ce champ. "
                    "Un paramètre de condition ne peut pas être non applicable."
                )
            continue

        if raw.strip() == "":
            raise ValidationError(
                f"colonne « {name} » : cellule vide. Une métrique absente s'écrit « NaN », "
                "jamais rien (invariant 20.4.6)."
            )

        kind = entry["type"]
        if kind == "string":
            allowed = entry.get("allowed")
            if allowed is not None and raw not in allowed:
                raise ValidationError(
                    f"colonne « {name} » : « {raw} » hors de {allowed}"
                )
            continue

        if kind == "integer":
            value = float(_parse_integer(raw, name))
        else:  # number, metric
            value = _parse_number(raw, name)
            if not math.isfinite(value):
                raise ValidationError(
                    f"colonne « {name} » : valeur non finie « {raw} » écrite autrement "
                    "que par le marqueur « NaN »"
                )

        if "min" in entry and value < entry["min"] - NUMERIC_TOLERANCE:
            raise ValidationError(f"colonne « {name} » : {value} < min {entry['min']}")
        if "exclusiveMin" in entry and value <= entry["exclusiveMin"]:
            raise ValidationError(
                f"colonne « {name} » : {value} <= borne exclusive {entry['exclusiveMin']}"
            )
        if "max" in entry and value > entry["max"] + NUMERIC_TOLERANCE:
            raise ValidationError(f"colonne « {name} » : {value} > max {entry['max']}")


def check_cross_fields(row: dict[str, str]) -> None:
    """Contrôle 3 : cohérence inter-champs X-01 à X-08."""
    tx_packets = _parse_integer(row["appTxPackets"], "appTxPackets")
    rx_packets = _parse_integer(row["appRxPackets"], "appRxPackets")
    tx_bytes = _parse_integer(row["appTxBytes"], "appTxBytes")
    rx_bytes = _parse_integer(row["appRxBytes"], "appRxBytes")
    nodes = _parse_integer(row["nodes"], "nodes")
    attacker_count = _parse_integer(row["attackerCount"], "attackerCount")
    ratio = _parse_number(row["attackerRatio"], "attackerRatio")
    min_speed = _parse_number(row["minSpeed"], "minSpeed")
    max_speed = _parse_number(row["maxSpeed"], "maxSpeed")

    # X-08 — aucune émission rend l'exécution invalide pour le PDR (§21). C'est un rejet,
    # pas un PDR nul : on ne peut rien conclure d'un scénario où rien n'a été envoyé.
    if tx_packets == 0:
        raise ValidationError(
            "X-08 : aucun paquet applicatif émis ; l'exécution est invalide pour le PDR "
            "et ne doit pas être agrégée (§21)"
        )

    # X-01 — on ne livre pas plus qu'on n'émet.
    if rx_packets > tx_packets:
        raise ValidationError(
            f"X-01 : appRxPackets ({rx_packets}) > appTxPackets ({tx_packets})"
        )

    # X-06 — même contrainte sur les octets.
    if rx_bytes > tx_bytes:
        raise ValidationError(f"X-06 : appRxBytes ({rx_bytes}) > appTxBytes ({tx_bytes})")

    # X-02 — le PDR publié doit être recalculable depuis les compteurs. C'est le contrôle
    # qui empêche qu'une métrique soit « ajustée » indépendamment de ses compteurs.
    if _is_nan_token(row["pdr"]):
        raise ValidationError(
            "X-02 : pdr vaut NaN alors que des paquets ont été émis ; incohérent"
        )
    pdr = _parse_number(row["pdr"], "pdr")
    recomputed = rx_packets / tx_packets
    if abs(pdr - recomputed) > NUMERIC_TOLERANCE:
        raise ValidationError(
            f"X-02 : pdr publié {pdr} != {recomputed} recalculé depuis les compteurs"
        )

    # X-03 — Éq. (24).
    if _is_nan_token(row["plr"]):
        raise ValidationError("X-03 : plr vaut NaN alors que pdr est défini")
    plr = _parse_number(row["plr"], "plr")
    if abs(pdr + plr - 1.0) > NUMERIC_TOLERANCE:
        raise ValidationError(f"X-03 : pdr + plr = {pdr + plr} != 1 (Éq. 24)")

    # X-04 — Éq. (2) : N_A = floor(r_a * N + 0.5).
    expected_attackers = math.floor(ratio * nodes + 0.5)
    if attacker_count != expected_attackers:
        raise ValidationError(
            f"X-04 : attackerCount {attacker_count} != floor({ratio} * {nodes} + 0.5) "
            f"= {expected_attackers} (Éq. 2)"
        )

    # X-05 — bande de vitesse cohérente.
    if max_speed < min_speed:
        raise ValidationError(f"X-05 : maxSpeed {max_speed} < minSpeed {min_speed}")

    # X-07 — la charge utile est incluse dans les octets réseau comptés.
    if not _is_nan_token(row["goodput_bps"]) and not _is_nan_token(row["throughput_bps"]):
        goodput = _parse_number(row["goodput_bps"], "goodput_bps")
        throughput = _parse_number(row["throughput_bps"], "throughput_bps")
        if goodput > throughput + NUMERIC_TOLERANCE:
            raise ValidationError(
                f"X-07 : goodput {goodput} > throughput {throughput} ; le périmètre de "
                "comptage réseau doit inclure la charge utile"
            )

    # X-09 (étape 1) — sans attaquant, aucun événement d'attaque. Les colonnes de l'étape
    # 1 sont facultatives : on ne contrôle que si elles sont présentes.
    if "forgedRrepCount" in row and "blackholeDropCount" in row:
        forged = _parse_integer(row["forgedRrepCount"], "forgedRrepCount")
        dropped = _parse_integer(row["blackholeDropCount"], "blackholeDropCount")
        if attacker_count == 0 and (forged != 0 or dropped != 0):
            raise ValidationError(
                f"X-09 : attackerCount = 0 mais forgedRrepCount = {forged}, "
                f"blackholeDropCount = {dropped} ; aucun événement d'attaque n'est "
                "possible sans attaquant"
            )


def check_manifest(csv_path: Path, row: dict[str, str]) -> dict:
    """Contrôle 4 : manifest MF-01 à MF-05. Retourne le manifest chargé."""
    manifest_path = Path(str(csv_path).replace("_metrics.csv", "_manifest.json"))
    if not manifest_path.is_file():
        raise ValidationError(
            f"manifest absent : {manifest_path.name}. Une exécution sans manifest n'est "
            "pas rejouable et ne peut pas être appariée (A7.2)."
        )

    with manifest_path.open(encoding="utf-8") as handle:
        manifest = json.load(handle)

    # MF-01
    if manifest.get("ns3Version") != "3.48":
        raise ValidationError(
            f"MF-01 : ns3Version = {manifest.get('ns3Version')!r}, attendu « 3.48 »"
        )

    # MF-02 — adressage normatif du projet pilote.
    if manifest.get("ipv4Network") != "10.1.0.0" or manifest.get("ipv4Mask") != "255.255.255.0":
        raise ValidationError(
            "MF-02 : adressage non normatif "
            f"({manifest.get('ipv4Network')}/{manifest.get('ipv4Mask')}), "
            "attendu 10.1.0.0 / 255.255.255.0"
        )

    attack = manifest.get("attack", {})
    ids = attack.get("attackerNodeIds", [])

    # MF-03 — unicité et tri (A2.2, invariant 20.4.3).
    if len(set(ids)) != len(ids):
        raise ValidationError(f"MF-03 : identifiants d'attaquants dupliqués : {ids}")
    if list(ids) != sorted(ids):
        raise ValidationError(f"MF-03 : identifiants d'attaquants non triés : {ids}")

    # MF-04
    csv_attacker_count = _parse_integer(row["attackerCount"], "attackerCount")
    if attack.get("attackerCount") != csv_attacker_count:
        raise ValidationError(
            f"MF-04 : attackerCount manifest {attack.get('attackerCount')} != CSV "
            f"{csv_attacker_count}"
        )
    if len(ids) != csv_attacker_count:
        raise ValidationError(
            f"MF-04 : {len(ids)} identifiants listés pour attackerCount = {csv_attacker_count}"
        )

    # MF-05 — les paramètres du manifest doivent reproduire les colonnes de condition.
    parameters = manifest.get("parameters", {})
    for manifest_key, csv_column in (
        ("nodeCount", "nodes"),
        ("simulationTime", "simTime"),
        ("minSpeed", "minSpeed"),
        ("maxSpeed", "maxSpeed"),
        ("seed", "seed"),
        ("run", "run"),
    ):
        if manifest_key not in parameters:
            raise ValidationError(f"MF-05 : paramètre « {manifest_key} » absent du manifest")
        if abs(float(parameters[manifest_key]) - float(row[csv_column])) > NUMERIC_TOLERANCE:
            raise ValidationError(
                f"MF-05 : {manifest_key} = {parameters[manifest_key]} dans le manifest mais "
                f"{csv_column} = {row[csv_column]} dans le CSV"
            )

    if manifest.get("protocol") != row["protocol"]:
        raise ValidationError(
            f"MF-05 : protocole {manifest.get('protocol')!r} dans le manifest, "
            f"{row['protocol']!r} dans le CSV"
        )

    # MF-06 (étape 1) — cohérence des compteurs d'attaque manifest/CSV, et de l'indicateur
    # « installed ». Contrôlé seulement si le manifest porte ces champs.
    if "forgedRrepCount" in attack and "forgedRrepCount" in row:
        if attack["forgedRrepCount"] != _parse_integer(row["forgedRrepCount"], "forgedRrepCount"):
            raise ValidationError(
                f"MF-06 : forgedRrepCount manifest {attack['forgedRrepCount']} != CSV "
                f"{row['forgedRrepCount']}"
            )
    if "blackholeDropCount" in attack and "blackholeDropCount" in row:
        if attack["blackholeDropCount"] != _parse_integer(row["blackholeDropCount"],
                                                          "blackholeDropCount"):
            raise ValidationError(
                f"MF-06 : blackholeDropCount manifest {attack['blackholeDropCount']} != CSV "
                f"{row['blackholeDropCount']}"
            )
    if "installed" in attack:
        expected_installed = csv_attacker_count > 0
        if bool(attack["installed"]) != expected_installed:
            raise ValidationError(
                f"MF-06 : attack.installed = {attack['installed']} mais attackerCount = "
                f"{csv_attacker_count} (installed doit valoir attackerCount>0)"
            )

    return manifest


def validate_run(csv_path: Path, schema: dict) -> dict:
    """Valide une exécution complète et retourne son résumé.

    :raises ValidationError: au premier invariant violé
    """
    header, row = read_single_row_csv(csv_path)
    check_schema(header, row, schema)
    check_cross_fields(row)
    manifest = check_manifest(csv_path, row)
    return {
        "path": csv_path,
        "row": row,
        "scenarioHash": manifest.get("scenarioHash"),
        "protocol": row["protocol"],
    }


def check_pairing(summaries: list[dict]) -> None:
    """Contrôle 5 : des exécutions appariées partagent la même empreinte de scénario.

    L'invariant 20.4.4 exige que mobilité, trafic, radio, attaquants, activation, seed et
    flux exogènes coïncident entre variantes d'un même bloc. L'empreinte, qui exclut le
    protocole, en est le contrôle mécanique.
    """
    hashes = {summary["scenarioHash"] for summary in summaries}
    if len(hashes) != 1:
        details = "\n".join(
            f"  {summary['path'].name} : {summary['protocol']} -> {summary['scenarioHash']}"
            for summary in summaries
        )
        raise ValidationError(
            "appariement rompu : les exécutions ne partagent pas la même empreinte de "
            f"scénario (invariant 20.4.4).\n{details}"
        )

    protocols = [summary["protocol"] for summary in summaries]
    if len(set(protocols)) != len(protocols):
        raise ValidationError(
            f"appariement suspect : le même protocole apparaît plusieurs fois ({protocols})"
        )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("metrics", nargs="+", type=Path, help="fichiers *_metrics.csv")
    parser.add_argument(
        "--paired",
        action="store_true",
        help="vérifier en outre que toutes les exécutions fournies sont appariées",
    )
    arguments = parser.parse_args(argv)

    schema = load_schema()
    summaries = []
    failures = 0

    for path in arguments.metrics:
        try:
            summary = validate_run(path, schema)
        except ValidationError as error:
            print(f"INVALIDE {path.name} : {error}", file=sys.stderr)
            failures += 1
            continue
        summaries.append(summary)
        row = summary["row"]
        print(
            f"VALID    {path.name} : protocol={row['protocol']} N={row['nodes']} "
            f"seed={row['seed']} run={row['run']} pdr={row['pdr']} "
            f"scenarioHash={summary['scenarioHash']}"
        )

    if arguments.paired and failures == 0:
        try:
            check_pairing(summaries)
        except ValidationError as error:
            print(f"INVALIDE appariement : {error}", file=sys.stderr)
            failures += 1
        else:
            print(f"VALID    appariement de {len(summaries)} exécutions")

    if failures:
        print(f"\n{failures} exécution(s) rejetée(s) ; aucune agrégation ne doit suivre.",
              file=sys.stderr)
        return 1

    print(f"\n{len(summaries)} exécution(s) VALID.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
