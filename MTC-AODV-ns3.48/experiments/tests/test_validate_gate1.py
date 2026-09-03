#!/usr/bin/env python3
"""Dependency-free unit tests for the Gate 1A campaign validator.

These tests exercise the pure decision logic of ``validate_gate1.py`` with
synthetic manifests.  They deliberately require neither ns-3 nor pytest, so the
validator can be checked on any machine before the ns-3.48 host is available.

They are a check of the *validator*, not of the simulation: a passing run here
says that malformed manifests are rejected, not that any selection was made.

Usage:
    python3 experiments/tests/test_validate_gate1.py
Exit status:
    0 when every assertion holds; 1 otherwise.
"""

from __future__ import annotations

import importlib.util
import math
import sys
from pathlib import Path
from typing import Any, Final

VALIDATOR_PATH: Final[Path] = Path(__file__).resolve().parents[1] / "validate_gate1.py"
"""Location of the module under test, resolved relative to this file."""


def load_validator() -> Any:
    """Import ``validate_gate1.py`` by path without requiring a package.

    Returns:
        The imported module object.

    Raises:
        ImportError: If the validator cannot be located or executed.
    """

    specification = importlib.util.spec_from_file_location("validate_gate1", VALIDATOR_PATH)
    if specification is None or specification.loader is None:
        raise ImportError(f"cannot load validator from {VALIDATOR_PATH}")

    module = importlib.util.module_from_spec(specification)
    # Registering the module before execution is required because the validator
    # defines dataclasses, which resolve their annotations through sys.modules.
    sys.modules["validate_gate1"] = module
    specification.loader.exec_module(module)
    return module


VALIDATOR: Final[Any] = load_validator()
COORDINATES: Final[Any] = VALIDATOR.ValidationCoordinates(
    node_count=100, seed=12345, run=1, attack_stream=73001
)
"""Reference coordinates matching the documented Gate 1A command line."""

FAILURES: Final[list[str]] = []
"""Names of assertions that did not hold during this execution."""


def check(name: str, condition: bool, detail: str = "") -> None:
    """Record one assertion outcome without aborting the remaining checks.

    Args:
        name: Short identifier of the property under test.
        condition: Result of the property evaluation.
        detail: Diagnostic text printed only when the property fails.
    """

    print(f"{'PASS' if condition else 'FAIL'} {name}" + ("" if condition else f" :: {detail}"))

    # Collecting rather than raising keeps a single execution informative when
    # several independent properties regress at the same time.
    if not condition:
        FAILURES.append(name)


def reference_manifest(**overrides: Any) -> dict[str, Any]:
    """Build a schema-complete, internally consistent Gate 1A manifest.

    Args:
        **overrides: Fields replacing the reference values, used to construct
            exactly one defect per negative test.

    Returns:
        A manifest dictionary in the schema emitted by ``mtcaodv-gate1``.
    """

    manifest = {
        "schemaVersion": 1,
        "gate": "1A",
        "nodeCount": 100,
        "attackerRatio": 0.05,
        "attackerCount": 5,
        "seed": 12345,
        "run": 1,
        "attackerSelectionStream": 73001,
        "excludedNodeIds": [0, 99],
        "attackerNodeIds": [3, 17, 42, 55, 80],
    }
    manifest.update(overrides)
    return manifest


def test_count_equation() -> None:
    """The Python reference implements floor(r_a * N + 0.5) for every ratio."""

    # The four mandatory intensities plus both probability-domain endpoints.
    for attacker_ratio, expected_count in ((0.05, 5), (0.10, 10), (0.20, 20), (0.30, 30),
                                           (0.0, 0), (1.0, 100)):
        observed_count = VALIDATOR.expected_attacker_count(100, attacker_ratio)
        check(f"count equation r={attacker_ratio}", observed_count == expected_count,
              f"expected {expected_count}, observed {observed_count}")


def test_ratio_round_trip() -> None:
    """Eight fixed decimals survive the C++ print and the JSON parse exactly."""

    # The validator compares ratios with abs_tol=1e-12, which is only safe if
    # the printed representation round-trips within that tolerance.
    for attacker_ratio in VALIDATOR.MANDATORY_ATTACKER_RATIOS:
        printed_ratio = f"{attacker_ratio:.8f}"
        check(f"ratio round-trip {attacker_ratio}",
              math.isclose(float(printed_ratio), attacker_ratio, abs_tol=1e-12),
              printed_ratio)


def test_valid_manifest_is_accepted() -> None:
    """A consistent manifest passes and reports the declared attacker count."""

    try:
        result = VALIDATOR.validate_manifest(reference_manifest(), COORDINATES, 0.05)
        check("consistent manifest accepted", result.observed_count == 5,
              f"observed_count={result.observed_count}")
    except ValueError as error:
        check("consistent manifest accepted", False, str(error))


def test_defective_manifests_are_rejected() -> None:
    """Each single-defect manifest must raise ValueError, never pass silently."""

    defective_manifests = {
        "count below the equation": reference_manifest(
            attackerCount=4, attackerNodeIds=[3, 17, 42, 55]),
        "identifiers not sorted": reference_manifest(
            attackerNodeIds=[17, 3, 42, 55, 80]),
        "duplicate identifiers": reference_manifest(
            attackerNodeIds=[3, 3, 42, 55, 80]),
        "excluded endpoint selected": reference_manifest(
            attackerNodeIds=[0, 17, 42, 55, 80]),
        "seed coordinate mismatch": reference_manifest(seed=999),
        "stream coordinate mismatch": reference_manifest(attackerSelectionStream=1),
        "population coordinate mismatch": reference_manifest(nodeCount=50),
        "incompatible gate label": reference_manifest(gate="1B"),
        "incompatible schema version": reference_manifest(schemaVersion=2),
        "cardinality below attackerCount": reference_manifest(
            attackerNodeIds=[3, 17, 42, 55]),
        "missing required field": {key: value
                                   for key, value in reference_manifest().items()
                                   if key != "seed"},
    }

    # Every entry carries exactly one defect, so a rejection is attributable.
    for defect_name, manifest in defective_manifests.items():
        try:
            VALIDATOR.validate_manifest(manifest, COORDINATES, 0.05)
            check(f"reject {defect_name}", False, "manifest was accepted")
        except ValueError as error:
            check(f"reject {defect_name}", True, str(error))


def test_manifest_extraction() -> None:
    """The manifest is recovered from stdout that also carries build logging."""

    import json

    noisy_output = "\n".join([
        "[ 42%] Building CXX object contrib/mtcaodv/CMakeFiles/...",
        "{ this brace-prefixed line is not JSON }",
        json.dumps(reference_manifest()),
    ])
    check("extract manifest from noisy stdout",
          VALIDATOR.extract_manifest(noisy_output)["attackerCount"] == 5)

    try:
        VALIDATOR.extract_manifest("no manifest was printed")
        check("reject stdout without a manifest", False, "malformed output accepted")
    except ValueError:
        # Absent evidence must fail the gate rather than yield a default record.
        check("reject stdout without a manifest", True)


def test_command_is_shell_free() -> None:
    """The ns-3 invocation is an argument vector, not a shell string."""

    command = VALIDATOR.build_run_command(Path("/opt/ns-3.48"), COORDINATES, 0.05)
    check("ns-3 command is an argv list",
          isinstance(command, list) and command[1] == "run", repr(command))


def test_known_validator_gaps() -> None:
    """Document manifest defects the current validator does not detect.

    These assertions encode present behaviour, not desired behaviour.  They are
    expected to be inverted once the corresponding checks are added, at which
    point this test becomes the regression guard for that fix.
    """

    undetected_defects = {
        "GAP identifier above the population": reference_manifest(
            attackerNodeIds=[3, 17, 42, 55, 999999]),
        "GAP negative identifier": reference_manifest(
            attackerNodeIds=[-5, 17, 42, 55, 80]),
        "GAP endpoint exclusion silently empty": reference_manifest(excludedNodeIds=[]),
        "GAP endpoints replaced by other nodes": reference_manifest(excludedNodeIds=[7, 8]),
    }

    # A vacuously satisfied exclusion check is the dangerous case: an executable
    # that ignored --excludeTrafficEndpoints would still be reported as PASS.
    for defect_name, manifest in undetected_defects.items():
        try:
            VALIDATOR.validate_manifest(manifest, COORDINATES, 0.05)
            check(defect_name + " (currently undetected)", True)
        except ValueError as error:
            check(defect_name + " (currently undetected)", False,
                  f"now detected, update this test: {error}")


def main() -> int:
    """Execute every property and return a process exit status."""

    test_count_equation()
    test_ratio_round_trip()
    test_valid_manifest_is_accepted()
    test_defective_manifests_are_rejected()
    test_manifest_extraction()
    test_command_is_shell_free()
    test_known_validator_gaps()

    print(f"\n{len(FAILURES)} failing propert" + ("y" if len(FAILURES) == 1 else "ies"))
    for failure_name in FAILURES:
        print(f"  - {failure_name}")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
