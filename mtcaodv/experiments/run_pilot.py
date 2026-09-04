#!/usr/bin/env python3
"""Orchestration du pilote MTC-AODV (étape 0) : balayage de seeds reproductible.

Le script ne simule rien. Il compose des lignes de commande ``./ns3 run``, les exécute,
et laisse le C++ produire chaque CSV et chaque manifest. Toute la logique réseau reste
dans le module ns-3 (§2.2 de la spécification).

Deux usages complémentaires :

* **balayage de seeds** — caractériser la baseline sur plusieurs réalisations aléatoires,
  ce qu'une exécution isolée ne permet jamais de faire ;
* **bloc apparié** (``--protocols aodv,mtcaodv``) — exécuter plusieurs protocoles sur
  *exactement* le même scénario, conformément à A7.1. Les empreintes de scénario doivent
  alors coïncider, ce que ``validate_step0.py --paired`` vérifie.

Le script n'agrège ni ne filtre aucun résultat : une exécution défavorable est conservée
comme les autres. La sélection de seeds favorables est explicitement interdite par le
protocole expérimental.

Usage
-----
    python3 run_pilot.py --ns3Dir /home/hassen/res/ns-3.48 --seeds 1001-1010
    python3 run_pilot.py --ns3Dir /home/hassen/res/ns-3.48 --seeds 1001-1010 \\
                         --protocols aodv,mtcaodv --outputDir results/step0
    python3 run_pilot.py --ns3Dir ... --seeds 1001 --set maxSpeed=5 --tag lowspeed
    python3 run_pilot.py --ns3Dir ... --seeds 1001-1003 --dryRun
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

PROGRAM = "mtc-aodv-pilot"


def parse_seeds(text: str) -> list[int]:
    """Interprète « 1001-1010 », « 1001,1005 » ou une combinaison des deux.

    :param text: spécification textuelle des seeds
    :return: la liste des seeds, dans l'ordre donné, sans doublon
    :raises ValueError: sur une plage inversée ou une valeur non entière
    """
    seeds: list[int] = []
    for chunk in text.split(","):
        chunk = chunk.strip()
        if not chunk:
            continue
        if "-" in chunk:
            first, last = chunk.split("-", 1)
            start, stop = int(first), int(last)
            if stop < start:
                raise ValueError(f"plage de seeds inversée : « {chunk} »")
            seeds.extend(range(start, stop + 1))
        else:
            seeds.append(int(chunk))

    unique: list[int] = []
    for seed in seeds:
        if seed not in unique:
            unique.append(seed)
    return unique


def parse_overrides(assignments: list[str]) -> dict[str, str]:
    """Transforme une liste « clé=valeur » en dictionnaire d'options ns-3."""
    overrides: dict[str, str] = {}
    for assignment in assignments:
        if "=" not in assignment:
            raise ValueError(f"option mal formée : « {assignment} » (attendu clé=valeur)")
        key, value = assignment.split("=", 1)
        overrides[key.strip()] = value.strip()
    return overrides


def build_command(
    ns3_dir: Path,
    protocol: str,
    seed: int,
    run: int,
    output_dir: Path,
    label: str,
    overrides: dict[str, str],
) -> list[str]:
    """Compose la commande ``./ns3 run`` d'une exécution.

    Les options explicites du balayage ne peuvent pas être écrasées par ``--set`` :
    protocole, seed, run, répertoire et étiquette définissent l'identité de l'exécution.
    Permettre de les redéfinir produirait des fichiers qui ne correspondent plus à leur
    nom.
    """
    reserved = {"protocol", "seed", "run", "outputDir", "label"}
    conflicts = reserved.intersection(overrides)
    if conflicts:
        raise ValueError(
            f"options réservées au balayage, non redéfinissables par --set : {sorted(conflicts)}"
        )

    arguments = [
        f"--protocol={protocol}",
        f"--seed={seed}",
        f"--run={run}",
        f"--outputDir={output_dir}",
        f"--label={label}",
    ]
    arguments += [f"--{key}={value}" for key, value in sorted(overrides.items())]

    return ["./ns3", "run", f"{PROGRAM} {' '.join(arguments)}"]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--ns3Dir", type=Path, required=True,
                        help="racine de l'arbre ns-3.48 (contenant ./ns3)")
    parser.add_argument("--seeds", default="1001-1010",
                        help="seeds, par exemple « 1001-1010 » ou « 1,2,5 »")
    parser.add_argument("--run", type=int, default=1, help="numéro de run RNG ns-3")
    parser.add_argument("--protocols", default="aodv",
                        help="protocoles à exécuter, séparés par des virgules")
    parser.add_argument("--outputDir", type=Path, default=Path("results/step0"),
                        help="répertoire des sorties, relatif à ns3Dir s'il est relatif")
    parser.add_argument("--tag", default="",
                        help="suffixe ajouté à l'étiquette des fichiers produits")
    parser.add_argument("--set", dest="overrides", action="append", default=[],
                        metavar="CLE=VALEUR",
                        help="option supplémentaire passée au pilote (répétable)")
    parser.add_argument("--dryRun", action="store_true",
                        help="afficher les commandes sans les exécuter")
    parser.add_argument("--stopOnError", action="store_true",
                        help="interrompre au premier échec au lieu de poursuivre")
    arguments = parser.parse_args(argv)

    ns3_dir = arguments.ns3Dir.expanduser().resolve()
    if not (ns3_dir / "ns3").is_file():
        print(f"erreur : {ns3_dir} ne contient pas l'exécutable ./ns3", file=sys.stderr)
        return 1

    try:
        seeds = parse_seeds(arguments.seeds)
        overrides = parse_overrides(arguments.overrides)
    except ValueError as error:
        print(f"erreur : {error}", file=sys.stderr)
        return 1

    protocols = [item.strip() for item in arguments.protocols.split(",") if item.strip()]
    if not protocols:
        print("erreur : aucun protocole demandé", file=sys.stderr)
        return 1

    output_dir = arguments.outputDir
    if not output_dir.is_absolute():
        output_dir = ns3_dir / output_dir

    suffix = f"_{arguments.tag}" if arguments.tag else ""

    # Journal de campagne : il conserve ce qui a été demandé, indépendamment de ce que
    # les exécutions ont produit. Sans lui, une exécution manquante serait indiscernable
    # d'une exécution jamais lancée.
    plan = {
        "program": PROGRAM,
        "ns3Dir": str(ns3_dir),
        "seeds": seeds,
        "run": arguments.run,
        "protocols": protocols,
        "overrides": overrides,
        "tag": arguments.tag,
        "outputDir": str(output_dir),
    }

    commands = []
    for seed in seeds:
        for protocol in protocols:
            # L'étiquette porte le protocole et le seed : les fichiers d'un même bloc
            # apparié sont ainsi voisins et immédiatement comparables.
            label = f"{protocol}_s{seed}_r{arguments.run}{suffix}"
            commands.append(
                (label, build_command(ns3_dir, protocol, seed, arguments.run,
                                      output_dir, label, overrides))
            )

    if arguments.dryRun:
        for label, command in commands:
            print(f"{label}: {' '.join(command)}")
        return 0

    output_dir.mkdir(parents=True, exist_ok=True)
    with (output_dir / f"campaign_plan{suffix}.json").open("w", encoding="utf-8") as handle:
        json.dump(plan, handle, indent=2, ensure_ascii=False)
        handle.write("\n")

    failures = 0
    started = time.time()
    for index, (label, command) in enumerate(commands, start=1):
        print(f"[{index}/{len(commands)}] {label}", flush=True)
        completed = subprocess.run(command, cwd=ns3_dir, capture_output=True, text=True)
        if completed.returncode != 0:
            failures += 1
            print(f"  ÉCHEC (code {completed.returncode})", file=sys.stderr)
            print(completed.stdout[-2000:], file=sys.stderr)
            print(completed.stderr[-2000:], file=sys.stderr)
            if arguments.stopOnError:
                return 1
            continue
        # La dernière ligne utile du programme résume l'exécution ; on la relaie pour
        # que le déroulement du balayage reste lisible sans ouvrir les CSV.
        for line in completed.stdout.splitlines():
            if line.startswith("  appTx=") or line.startswith("protocole="):
                print(f"  {line.strip()}")

    elapsed = time.time() - started
    print(f"\n{len(commands) - failures}/{len(commands)} exécutions réussies "
          f"en {elapsed:.1f} s ; sorties dans {output_dir}")
    if failures:
        print(f"{failures} échec(s).", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
