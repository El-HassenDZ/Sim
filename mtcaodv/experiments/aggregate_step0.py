#!/usr/bin/env python3
"""Agrégation des exécutions validées du pilote MTC-AODV (étape 0).

Le script lit des fichiers ``*_metrics.csv``, **valide chacun** via ``validate_step0.py``
et résume par groupe. Une exécution rejetée n'entre pas dans l'agrégat : c'est la règle
A7.2 point 12, *« aggregate only validated runs »*. Aucune valeur n'est imputée.

Statistiques produites par métrique et par groupe : effectif, moyenne, écart-type
(correction de Bessel, Éq. 41), médiane, premier et troisième quartiles, minimum et
maximum. La médiane et l'intervalle interquartile sont produits à côté de la moyenne
parce que la spécification les exige (§17.5) et parce qu'une distribution de PDR sur
plusieurs seeds est régulièrement asymétrique : la moyenne seule masquerait une
réalisation catastrophique.

Une métrique valant NaN dans une exécution est **exclue du calcul de cette métrique**,
et l'effectif correspondant est rapporté séparément. Remplacer un NaN par zéro
fabriquerait une donnée (règle D-22).

Usage
-----
    python3 aggregate_step0.py results/step0/*_metrics.csv
    python3 aggregate_step0.py results/step0/*_metrics.csv --groupBy protocol
    python3 aggregate_step0.py results/step0/*_metrics.csv --csv summary.csv
"""

from __future__ import annotations

import argparse
import math
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from validate_step0 import (  # noqa: E402  (import après ajustement du chemin)
    ValidationError,
    load_schema,
    read_single_row_csv,
    validate_run,
)

# Métriques résumées. L'ordre est celui du schéma normatif, complété par les colonnes
# ajoutées à l'étape 0.
SUMMARISED_METRICS = (
    "pdr",
    "plr",
    "throughput_bps",
    "goodput_bps",
    "meanDelay_s",
    "medianDelay_s",
    "jitter_s",
    "nro",
    "rdf_per_s",
    "meanDegree",
    "connectedGraphFraction",
)


def quantile(sorted_values: list[float], probability: float) -> float:
    """Quantile empirique par interpolation linéaire entre ordres consécutifs.

    Convention explicite (« type 7 », celle de NumPy et de R par défaut) : pour n valeurs
    triées, le quantile d'ordre p est situé à l'indice réel ``(n - 1) * p``. La
    convention est nommée ici pour que l'IQR publié soit reproductible par un tiers.

    :param sorted_values: valeurs triées, non vides
    :param probability: ordre dans [0,1]
    :return: le quantile empirique
    """
    if not sorted_values:
        raise ValueError("quantile d'une série vide")
    if len(sorted_values) == 1:
        return sorted_values[0]

    position = (len(sorted_values) - 1) * probability
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return sorted_values[int(position)]
    weight = position - lower
    return sorted_values[lower] * (1.0 - weight) + sorted_values[upper] * weight


def summarise(values: list[float]) -> dict[str, float]:
    """Résume une série de valeurs finies."""
    ordered = sorted(values)
    return {
        "n": len(ordered),
        "mean": statistics.fmean(ordered),
        # Écart-type d'échantillon (Éq. 41) : indéfini pour une seule observation, ce qui
        # est rapporté NaN plutôt que 0 — une observation unique n'a pas de dispersion
        # nulle, elle a une dispersion inconnue.
        "sd": statistics.stdev(ordered) if len(ordered) >= 2 else float("nan"),
        "median": quantile(ordered, 0.5),
        "q1": quantile(ordered, 0.25),
        "q3": quantile(ordered, 0.75),
        "min": ordered[0],
        "max": ordered[-1],
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("metrics", nargs="+", type=Path, help="fichiers *_metrics.csv")
    parser.add_argument("--groupBy", default="protocol",
                        help="colonne de regroupement (protocol, nodes, maxSpeed…) ; "
                             "« none » pour un agrégat unique")
    parser.add_argument("--csv", type=Path, default=None,
                        help="écrire le résumé dans ce fichier CSV")
    parser.add_argument("--allowInvalid", action="store_true",
                        help="ne pas interrompre si une exécution est rejetée ; "
                             "les exécutions rejetées restent exclues de l'agrégat")
    arguments = parser.parse_args(argv)

    schema = load_schema()
    groups: dict[str, list[dict[str, str]]] = {}
    rejected = 0

    for path in arguments.metrics:
        try:
            validate_run(path, schema)
            _, row = read_single_row_csv(path)
        except ValidationError as error:
            rejected += 1
            print(f"REJETÉ {path.name} : {error}", file=sys.stderr)
            if not arguments.allowInvalid:
                print("\nAgrégation interrompue : une exécution invalide ne doit pas être "
                      "agrégée (A7.2). Relancer avec --allowInvalid pour ignorer ce run.",
                      file=sys.stderr)
                return 1
            continue

        key = "toutes" if arguments.groupBy == "none" else row.get(arguments.groupBy, "?")
        groups.setdefault(str(key), []).append(row)

    if not groups:
        print("aucune exécution valide à agréger", file=sys.stderr)
        return 1

    summary_rows: list[dict[str, object]] = []

    for key in sorted(groups):
        rows = groups[key]
        print(f"\n=== {arguments.groupBy} = {key} — {len(rows)} exécution(s) validée(s) ===")
        print(f"{'métrique':<24}{'n':>4}{'moyenne':>14}{'écart-type':>14}"
              f"{'médiane':>14}{'Q1':>14}{'Q3':>14}{'min':>14}{'max':>14}")

        for metric in SUMMARISED_METRICS:
            values = []
            missing = 0
            for row in rows:
                raw = row.get(metric)
                if raw is None:
                    continue
                if raw.strip() == "NaN":
                    # Non applicable : compté, jamais remplacé par zéro.
                    missing += 1
                    continue
                values.append(float(raw))

            if not values:
                print(f"{metric:<24}{0:>4}{'N/A':>14}"
                      f"   (aucune valeur applicable sur {missing} exécution(s))")
                continue

            stats = summarise(values)
            note = f"   [{missing} NaN]" if missing else ""
            print(f"{metric:<24}{stats['n']:>4}{stats['mean']:>14.6g}{stats['sd']:>14.6g}"
                  f"{stats['median']:>14.6g}{stats['q1']:>14.6g}{stats['q3']:>14.6g}"
                  f"{stats['min']:>14.6g}{stats['max']:>14.6g}{note}")

            summary_rows.append({
                "group": key,
                "groupBy": arguments.groupBy,
                "metric": metric,
                "nApplicable": stats["n"],
                "nNaN": missing,
                "mean": stats["mean"],
                "sd": stats["sd"],
                "median": stats["median"],
                "q1": stats["q1"],
                "q3": stats["q3"],
                "min": stats["min"],
                "max": stats["max"],
            })

    if arguments.csv is not None:
        arguments.csv.parent.mkdir(parents=True, exist_ok=True)
        columns = ["group", "groupBy", "metric", "nApplicable", "nNaN",
                   "mean", "sd", "median", "q1", "q3", "min", "max"]
        with arguments.csv.open("w", encoding="utf-8") as handle:
            handle.write(",".join(columns) + "\n")
            for row in summary_rows:
                handle.write(",".join(str(row[column]) for column in columns) + "\n")
        print(f"\nrésumé écrit dans {arguments.csv}")

    if rejected:
        print(f"\n{rejected} exécution(s) rejetée(s) et exclue(s) de l'agrégat.",
              file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
