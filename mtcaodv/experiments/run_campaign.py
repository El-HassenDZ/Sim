#!/usr/bin/env python3
"""Orchestration des campagnes MTC-AODV (A7, A7.1).

Le script ne simule rien : il génère les conditions expérimentales, lance ns-3, et
collecte les fichiers produits. Toute la logique réseau reste en C++ (§2.2).

Principe d'appariement (A7.1, invariant 20.4.4) : pour une condition et un seed donnés,
les coordonnées exogènes — positions, mobilité, trafic, sélection d'attaquants, flux RNG —
sont identiques d'une variante à l'autre. Seule la configuration du protocole évalué
change. Le contrôle est mécanique : les manifests d'un même bloc doivent porter le même
`scenarioHash`.

Usage :
    python3 run_campaign.py --config configs/pilot.json --outputDir ../results/pilot
    python3 run_campaign.py --config configs/pilot.json --dryRun
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import subprocess
import sys
import time
from pathlib import Path

SCENARIO_PROGRAM = "mtcaodv-manet-scenario"

# Options ns-3 dérivées directement d'une entrée du plan de campagne.
SCENARIO_OPTIONS = (
    "nodeCount", "areaWidth", "areaHeight", "speedMin", "speedMax", "pauseTime",
    "dataRate", "controlRate", "nonUnicastRate", "txPowerDbm", "pathLossExponent",
    "propagationModel", "radioRange", "mobilityProfile",
    "enableHello", "helloInterval", "flowCount", "packetSize", "packetRate",
    "warmupEnd", "trafficStart", "trafficStop", "drainTime",
    "attackerRatio", "attackStartTime", "excludeTrafficEndpoints",
    "connectivityRadius", "initialEnergy",
)


class RunFailed(RuntimeError):
    """Une exécution ns-3 n'a pas abouti. Aucun résultat partiel n'est conservé."""


def build_run_label(variant, attacker_ratio, seed):
    """Étiquette lisible et triable, utilisée comme préfixe de fichier."""
    return f"v{variant}_ra{attacker_ratio:.2f}_s{seed}"


def build_command(ns3_dir, plan, variant, attacker_ratio, seed, output_dir):
    """Construit la ligne de commande ns-3 d'une exécution."""
    arguments = [
        f"--protocolVariant={variant}",
        f"--seed={seed}",
        f"--run={plan.get('run', 1)}",
        f"--attackerRatio={attacker_ratio}",
        f"--outputDir={output_dir}",
        f"--runLabel={build_run_label(variant, attacker_ratio, seed)}",
    ]
    for option in SCENARIO_OPTIONS:
        if option == "attackerRatio":
            continue  # déjà positionné, et propre à la condition
        if option in plan:
            value = plan[option]
            if isinstance(value, bool):
                value = "true" if value else "false"
            arguments.append(f"--{option}={value}")

    return [str(Path(ns3_dir) / "ns3"), "run", f"{SCENARIO_PROGRAM} {' '.join(arguments)}", "--no-build"]


def execute_run(ns3_dir, plan, variant, attacker_ratio, seed, output_dir, timeout):
    """Exécute une simulation et renvoie (étiquette, durée, sortie standard)."""
    label = build_run_label(variant, attacker_ratio, seed)
    command = build_command(ns3_dir, plan, variant, attacker_ratio, seed, output_dir)

    started = time.monotonic()
    completed = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    elapsed = time.monotonic() - started

    if completed.returncode != 0:
        raise RunFailed(f"{label} : code {completed.returncode}\n{completed.stderr[-2000:]}")

    return label, elapsed, completed.stdout


def expand_plan(plan):
    """Produit la liste des (variante, ratio, seed) de la campagne.

    L'ordre est déterministe : il rend le journal d'exécution comparable d'une campagne
    à l'autre, ce qui facilite le repérage d'une divergence.
    """
    conditions = []
    for attacker_ratio in plan["attackerRatios"]:
        for seed in plan["seeds"]:
            for variant in plan["variants"]:
                conditions.append((variant, float(attacker_ratio), int(seed)))
    return conditions


def verify_pairing(output_dir, plan):
    """Vérifie que les variantes d'un même bloc partagent le même scenarioHash.

    C'est le contrôle mécanique de l'invariant 20.4.4 : si deux variantes appariées ne
    voient pas le même scénario, le contraste mesuré n'a aucune valeur causale.
    """
    blocks = {}
    for manifest_path in Path(output_dir).glob("*_manifest.json"):
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        parameters = manifest["parameters"]
        key = (parameters["attackerRatio"], parameters["seed"])
        blocks.setdefault(key, {})[manifest["variant"]] = manifest["scenarioHash"]

    mismatches = []
    for (attacker_ratio, seed), hashes in sorted(blocks.items()):
        distinct = set(hashes.values())
        if len(distinct) > 1:
            mismatches.append((attacker_ratio, seed, hashes))

    return blocks, mismatches


def main(argv=None):
    parser = argparse.ArgumentParser(description="Campagne MTC-AODV")
    parser.add_argument("--config", required=True, help="plan de campagne JSON")
    parser.add_argument("--ns3Dir", default=os.environ.get("NS3_DIR", str(Path.home() / "ns3" / "ns-3.48")))
    parser.add_argument("--outputDir", default=None, help="remplace outputDir du plan")
    parser.add_argument("--jobs", type=int, default=2,
                        help="exécutions simultanées ; chaque simulation est mono-thread")
    parser.add_argument("--timeout", type=int, default=7200, help="délai maximal par exécution (s)")
    parser.add_argument("--dryRun", action="store_true", help="afficher les commandes sans exécuter")
    arguments = parser.parse_args(argv)

    plan = json.loads(Path(arguments.config).read_text(encoding="utf-8"))
    output_dir = Path(arguments.outputDir or plan.get("outputDir", "results"))
    output_dir.mkdir(parents=True, exist_ok=True)

    conditions = expand_plan(plan)
    print(f"campagne : {len(conditions)} exécutions, {arguments.jobs} en parallèle")

    if arguments.dryRun:
        for variant, attacker_ratio, seed in conditions:
            print(" ".join(build_command(arguments.ns3Dir, plan, variant, attacker_ratio, seed, output_dir)))
        return 0

    failures = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=arguments.jobs) as pool:
        futures = {
            pool.submit(execute_run, arguments.ns3Dir, plan, variant, attacker_ratio, seed,
                        output_dir, arguments.timeout): (variant, attacker_ratio, seed)
            for variant, attacker_ratio, seed in conditions
        }
        for future in concurrent.futures.as_completed(futures):
            variant, attacker_ratio, seed = futures[future]
            try:
                label, elapsed, _ = future.result()
                print(f"  OK   {label}  ({elapsed:.0f} s)")
            except Exception as error:  # noqa: BLE001 - on veut le motif exact
                failures.append((variant, attacker_ratio, seed, str(error)))
                print(f"  ÉCHEC v{variant} r_a={attacker_ratio} seed={seed} : {error}", file=sys.stderr)

    blocks, mismatches = verify_pairing(output_dir, plan)
    print(f"\nappariement : {len(blocks)} blocs vérifiés")
    for attacker_ratio, seed, hashes in mismatches:
        print(f"  APPARIEMENT ROMPU r_a={attacker_ratio} seed={seed} : {hashes}", file=sys.stderr)

    if failures or mismatches:
        # Fail-closed (A7.2) : une campagne partiellement valide n'est pas agrégeable.
        print(f"\ncampagne INVALIDE : {len(failures)} échec(s), "
              f"{len(mismatches)} bloc(s) mal apparié(s)", file=sys.stderr)
        return 1

    print("campagne complète et appariée")
    return 0


if __name__ == "__main__":
    sys.exit(main())
