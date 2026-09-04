"""Tests du validateur d'exécution de l'étape 0.

Un validateur qui n'accepte que des fichiers corrects ne prouve rien. Chaque test
ci-dessous fabrique une exécution puis en corrompt **un** invariant précis, et vérifie
que l'exécution est *rejetée* avec le contrôle attendu (X-01…X-08, MF-01…MF-05).

Le point le plus important est le test `test_falsified_pdr_is_rejected` : il vérifie
qu'une métrique retouchée sans ses compteurs est détectée. C'est le garde-fou mécanique
contre l'ajustement d'un résultat, que le protocole expérimental interdit explicitement.

Écrit en `unittest` afin d'être exécutable sans dépendance externe :

    python3 -m unittest discover -s experiments/tests -q
    python3 -m pytest experiments/tests/ -q      # pytest collecte aussi unittest
"""

from __future__ import annotations

import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from validate_step0 import (  # noqa: E402
    ValidationError,
    check_pairing,
    load_schema,
    validate_run,
)

# Une exécution de référence, cohérente : 720 paquets émis, 639 livrés, PDR = 0,8875.
# Les valeurs proviennent d'une exécution réelle du pilote (seed 12345), de sorte que le
# cas nominal du test soit une donnée observée et non une donnée inventée.
REFERENCE_COLUMNS = [
    ("protocol", "aodv"),
    ("nodes", "20"),
    ("simTime", "60"),
    ("minSpeed", "1"),
    ("maxSpeed", "20"),
    ("seed", "12345"),
    ("run", "1"),
    ("attackerRatio", "0"),
    ("attackerCount", "0"),
    ("attackStart", "10"),
    ("appTxPackets", "720"),
    ("appRxPackets", "639"),
    ("appTxBytes", "368640"),
    ("appRxBytes", "327168"),
    ("pdr", "0.8875"),
    ("plr", "0.1125"),
    ("throughput_bps", "61344"),
    ("goodput_bps", "58163.2"),
    ("meanDelay_s", "0.0182166289"),
    ("scenarioHash", "cb9e2c8e"),
]

REFERENCE_MANIFEST = {
    "ns3Version": "3.48",
    "step": 0,
    "protocol": "aodv",
    "scenarioHash": "cb9e2c8e",
    "ipv4Network": "10.1.0.0",
    "ipv4Mask": "255.255.255.0",
    "evaluationWindowSeconds": 45,
    "parameters": {
        "nodeCount": "20",
        "simulationTime": "60",
        "minSpeed": "1",
        "maxSpeed": "20",
        "seed": "12345",
        "run": "1",
    },
    "attack": {
        "installed": False,
        "attackerRatioRequested": 0,
        "attackerRatioAmongEligible": 0,
        "attackerCount": 0,
        "eligibleCount": 12,
        "attackerNodeIds": [],
    },
}


class ValidateStep0TestCase(unittest.TestCase):
    """Cas de base : écrit une paire CSV/manifest dans un répertoire temporaire."""

    def setUp(self) -> None:
        self.schema = load_schema()
        self._directory = tempfile.TemporaryDirectory()
        self.root = Path(self._directory.name)

    def tearDown(self) -> None:
        self._directory.cleanup()

    def write_run(self, name="run", columns=None, manifest=None) -> Path:
        """Écrit une exécution et retourne le chemin du CSV.

        :param name: préfixe des fichiers
        :param columns: liste (colonne, valeur) ; défaut = l'exécution de référence
        :param manifest: manifest ; défaut = le manifest de référence
        :return: le chemin du fichier ``*_metrics.csv``
        """
        columns = REFERENCE_COLUMNS if columns is None else columns
        manifest = REFERENCE_MANIFEST if manifest is None else manifest

        csv_path = self.root / f"{name}_metrics.csv"
        csv_path.write_text(
            ",".join(column for column, _ in columns) + "\n"
            + ",".join(value for _, value in columns) + "\n",
            encoding="utf-8",
        )
        (self.root / f"{name}_manifest.json").write_text(
            json.dumps(manifest, indent=2), encoding="utf-8"
        )
        return csv_path

    @staticmethod
    def with_column(value_by_column: dict[str, str]) -> list[tuple[str, str]]:
        """Retourne les colonnes de référence, une ou plusieurs valeurs remplacées."""
        return [
            (column, value_by_column.get(column, value))
            for column, value in REFERENCE_COLUMNS
        ]

    # --- Cas nominal ---------------------------------------------------------------

    def test_reference_run_is_valid(self):
        summary = validate_run(self.write_run(), self.schema)
        self.assertEqual(summary["protocol"], "aodv")
        self.assertEqual(summary["scenarioHash"], "cb9e2c8e")

    # --- Contrôles inter-champs ----------------------------------------------------

    def test_falsified_pdr_is_rejected(self):
        """X-02 : un PDR retouché sans ses compteurs doit être détecté."""
        columns = self.with_column({"pdr": "0.99", "plr": "0.01"})
        with self.assertRaises(ValidationError) as context:
            validate_run(self.write_run("falsified", columns), self.schema)
        self.assertIn("X-02", str(context.exception))

    def test_broken_pdr_plr_identity_is_rejected(self):
        """X-03 : l'identité PDR + PLR = 1 de l'Éq. (24) est vérifiée."""
        columns = self.with_column({"plr": "0.5"})
        with self.assertRaises(ValidationError) as context:
            validate_run(self.write_run("identity", columns), self.schema)
        self.assertIn("X-03", str(context.exception))

    def test_more_received_than_sent_is_rejected(self):
        """X-01 : on ne livre pas plus de paquets qu'on n'en émet."""
        columns = self.with_column({"appRxPackets": "800", "pdr": "1.111", "plr": "-0.111"})
        with self.assertRaises(ValidationError):
            validate_run(self.write_run("excess", columns), self.schema)

    def test_zero_transmitted_is_rejected(self):
        """X-08 : sans émission, l'exécution est invalide pour le PDR (§21)."""
        columns = self.with_column(
            {"appTxPackets": "0", "appRxPackets": "0", "appTxBytes": "0",
             "appRxBytes": "0", "pdr": "NaN", "plr": "NaN",
             "throughput_bps": "0", "goodput_bps": "0", "meanDelay_s": "NaN"}
        )
        with self.assertRaises(ValidationError) as context:
            validate_run(self.write_run("empty", columns), self.schema)
        self.assertIn("X-08", str(context.exception))

    def test_attacker_count_must_match_equation_2(self):
        """X-04 : N_A = floor(r_a * N + 0,5)."""
        columns = self.with_column({"attackerRatio": "0.2", "attackerCount": "0"})
        with self.assertRaises(ValidationError) as context:
            validate_run(self.write_run("ratio", columns), self.schema)
        self.assertIn("X-04", str(context.exception))

    def test_goodput_above_throughput_is_rejected(self):
        """X-07 : la charge utile est incluse dans les octets réseau comptés."""
        columns = self.with_column({"goodput_bps": "99999"})
        with self.assertRaises(ValidationError) as context:
            validate_run(self.write_run("goodput", columns), self.schema)
        self.assertIn("X-07", str(context.exception))

    # --- Schéma et politique NaN ---------------------------------------------------

    def test_column_order_is_enforced(self):
        """Les colonnes obligatoires doivent être en tête, dans l'ordre normatif."""
        columns = list(REFERENCE_COLUMNS)
        columns[0], columns[1] = columns[1], columns[0]
        with self.assertRaises(ValidationError) as context:
            validate_run(self.write_run("order", columns), self.schema)
        self.assertIn("ordre normatif", str(context.exception))

    def test_empty_cell_is_rejected(self):
        """Une cellule vide serait lue comme zéro : elle est refusée (invariant 20.4.6)."""
        columns = self.with_column({"meanDelay_s": ""})
        with self.assertRaises(ValidationError) as context:
            validate_run(self.write_run("blank", columns), self.schema)
        self.assertIn("vide", str(context.exception))

    def test_nan_forbidden_on_condition_columns(self):
        """Un paramètre de condition ne peut pas être « non applicable »."""
        columns = self.with_column({"nodes": "NaN"})
        with self.assertRaises(ValidationError) as context:
            validate_run(self.write_run("nannodes", columns), self.schema)
        self.assertIn("NaN interdit", str(context.exception))

    def test_pdr_out_of_domain_is_rejected(self):
        columns = self.with_column({"appRxPackets": "639", "pdr": "1.5", "plr": "-0.5"})
        with self.assertRaises(ValidationError):
            validate_run(self.write_run("domain", columns), self.schema)

    # --- Manifest ------------------------------------------------------------------

    def test_missing_manifest_is_rejected(self):
        csv_path = self.write_run("orphan")
        (self.root / "orphan_manifest.json").unlink()
        with self.assertRaises(ValidationError) as context:
            validate_run(csv_path, self.schema)
        self.assertIn("manifest absent", str(context.exception))

    def test_wrong_ns3_version_is_rejected(self):
        manifest = copy.deepcopy(REFERENCE_MANIFEST)
        manifest["ns3Version"] = "3.47"
        with self.assertRaises(ValidationError) as context:
            validate_run(self.write_run("version", manifest=manifest), self.schema)
        self.assertIn("MF-01", str(context.exception))

    def test_non_normative_addressing_is_rejected(self):
        """MF-02 : le plan d'adressage 10.1.0.0/24 est normatif."""
        manifest = copy.deepcopy(REFERENCE_MANIFEST)
        manifest["ipv4Mask"] = "255.255.0.0"
        with self.assertRaises(ValidationError) as context:
            validate_run(self.write_run("mask", manifest=manifest), self.schema)
        self.assertIn("MF-02", str(context.exception))

    def test_duplicate_attacker_ids_are_rejected(self):
        """MF-03 : le tirage A2.2 est sans remise (invariant 20.4.3)."""
        manifest = copy.deepcopy(REFERENCE_MANIFEST)
        manifest["attack"]["attackerNodeIds"] = [3, 3, 7]
        manifest["attack"]["attackerCount"] = 3
        columns = self.with_column({"attackerRatio": "0.15", "attackerCount": "3"})
        with self.assertRaises(ValidationError) as context:
            validate_run(self.write_run("dup", columns, manifest), self.schema)
        self.assertIn("MF-03", str(context.exception))

    def test_unsorted_attacker_ids_are_rejected(self):
        manifest = copy.deepcopy(REFERENCE_MANIFEST)
        manifest["attack"]["attackerNodeIds"] = [7, 3, 11]
        manifest["attack"]["attackerCount"] = 3
        columns = self.with_column({"attackerRatio": "0.15", "attackerCount": "3"})
        with self.assertRaises(ValidationError) as context:
            validate_run(self.write_run("unsorted", columns, manifest), self.schema)
        self.assertIn("MF-03", str(context.exception))

    def test_manifest_parameters_must_match_csv(self):
        manifest = copy.deepcopy(REFERENCE_MANIFEST)
        manifest["parameters"]["maxSpeed"] = "5"
        with self.assertRaises(ValidationError) as context:
            validate_run(self.write_run("mismatch", manifest=manifest), self.schema)
        self.assertIn("MF-05", str(context.exception))

    def test_invalid_json_manifest_is_rejected(self):
        """Régression D-I9 : un manifest contenant « nan » n'est pas du JSON valide."""
        csv_path = self.write_run("badjson")
        (self.root / "badjson_manifest.json").write_text(
            '{"ns3Version": "3.48", "meanHopCount": nan}', encoding="utf-8"
        )
        with self.assertRaises(Exception):
            validate_run(csv_path, self.schema)

    # --- Appariement ---------------------------------------------------------------

    def test_pairing_requires_identical_scenario_hash(self):
        """Invariant 20.4.4 : seul le protocole doit différer entre variantes appariées."""
        first = validate_run(self.write_run("a"), self.schema)

        other_columns = self.with_column({"protocol": "mtcaodv"})
        other_manifest = copy.deepcopy(REFERENCE_MANIFEST)
        other_manifest["protocol"] = "mtcaodv"
        second = validate_run(
            self.write_run("b", other_columns, other_manifest), self.schema
        )
        check_pairing([first, second])  # ne doit pas lever

        # Une empreinte différente signale un scénario différent : appariement rompu.
        second["scenarioHash"] = "deadbeef"
        with self.assertRaises(ValidationError) as context:
            check_pairing([first, second])
        self.assertIn("appariement rompu", str(context.exception))

    def test_pairing_rejects_duplicate_protocols(self):
        first = validate_run(self.write_run("c"), self.schema)
        second = validate_run(self.write_run("d"), self.schema)
        with self.assertRaises(ValidationError) as context:
            check_pairing([first, second])
        self.assertIn("plusieurs fois", str(context.exception))


if __name__ == "__main__":
    unittest.main()
