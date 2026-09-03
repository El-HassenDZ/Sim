#!/usr/bin/env python3
"""Validate MTC-AODV Gate 1A against an installed ns-3.48 tree.

The script runs the C++ example twice for each mandatory attacker ratio,
checks the exact cardinality, rejects duplicates and endpoint leakage, and
verifies deterministic replay under identical seed/run/stream coordinates.
It never fabricates missing output: a failed or malformed ns-3 execution ends
the validation with a non-zero status.
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Final


MANDATORY_ATTACKER_RATIOS: Final[tuple[float, ...]] = (0.05, 0.10, 0.20, 0.30)
"""Attacker proportions required by the MTC-AODV experimental design."""


@dataclass(frozen=True)
class ValidationCoordinates:
    """Reproducibility coordinates shared by all Gate 1A executions.

    Attributes:
        node_count: Total MANET population, measured in nodes.
        seed: Global ns-3 pseudo-random seed.
        run: ns-3 replication/substream number.
        attack_stream: RNG stream reserved for attacker identities.
    """

    node_count: int
    seed: int
    run: int
    attack_stream: int


@dataclass(frozen=True)
class RatioValidationResult:
    """Validated evidence for one mandatory attacker ratio.

    Attributes:
        attacker_ratio: Requested dimensionless attacker proportion.
        expected_count: Count obtained from floor(ratio * N + 0.5).
        observed_count: Count reported by the ns-3 example.
        attacker_node_ids: Sorted malicious-node identifiers.
        deterministic_replay: Whether two identical executions matched exactly.
    """

    attacker_ratio: float
    expected_count: int
    observed_count: int
    attacker_node_ids: tuple[int, ...]
    deterministic_replay: bool


def parse_arguments() -> argparse.Namespace:
    """Parse and return command-line parameters supplied by the researcher."""

    parser = argparse.ArgumentParser(
        description="Validate exact and deterministic Gate 1A attacker selection."
    )
    parser.add_argument(
        "--ns3-root",
        type=Path,
        required=True,
        help="Path to the root of the tested ns-3.48 source tree.",
    )
    parser.add_argument(
        "--node-count",
        type=int,
        default=100,
        help="Total MANET population (default: 100 nodes).",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=12345,
        help="Global ns-3 seed used for validation.",
    )
    parser.add_argument(
        "--run",
        type=int,
        default=1,
        help="ns-3 replication run number.",
    )
    parser.add_argument(
        "--attack-stream",
        type=int,
        default=73001,
        help="RNG stream reserved for attacker identity selection.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("gate1-validation.json"),
        help="Destination for the validated JSON report.",
    )
    return parser.parse_args()


def validate_coordinates(coordinates: ValidationCoordinates) -> None:
    """Reject invalid reproducibility coordinates before launching ns-3.

    Args:
        coordinates: Candidate population and RNG coordinates.

    Raises:
        ValueError: If a value is outside its supported domain.
    """

    # A non-positive population cannot represent the intended MANET experiment.
    if coordinates.node_count <= 0:
        raise ValueError("node_count must be positive")

    # ns-3 requires a positive global seed; zero is not a valid experiment seed.
    if coordinates.seed <= 0:
        raise ValueError("seed must be positive")

    # Runs and explicit streams are non-negative reproducibility coordinates.
    if coordinates.run < 0 or coordinates.attack_stream < 0:
        raise ValueError("run and attack_stream must be non-negative")


def build_run_command(ns3_root: Path, coordinates: ValidationCoordinates, ratio: float) -> list[str]:
    """Build one shell-free ns-3 wrapper invocation.

    Args:
        ns3_root: Root of the ns-3.48 tree.
        coordinates: Fixed population and RNG coordinates.
        ratio: Requested attacker proportion.

    Returns:
        Argument vector suitable for subprocess.run without shell expansion.
    """

    scenario_arguments = (
        f"mtcaodv-gate1 --nodeCount={coordinates.node_count} "
        f"--attackerRatio={ratio:.8f} --seed={coordinates.seed} "
        f"--run={coordinates.run} --attackStream={coordinates.attack_stream} "
        "--excludeTrafficEndpoints=true"
    )
    return [str(ns3_root / "ns3"), "run", scenario_arguments]


def extract_manifest(standard_output: str) -> dict[str, Any]:
    """Extract the final one-line JSON manifest from ns-3 output.

    Args:
        standard_output: Complete stdout captured from an ns-3 execution.

    Returns:
        Parsed manifest object.

    Raises:
        ValueError: If no valid JSON object is present.
    """

    # Search from the final line upward because optional component logging may
    # precede the manifest.  The loop stops at the first valid JSON object.
    for output_line in reversed(standard_output.splitlines()):
        stripped_line = output_line.strip()

        # Empty and non-object lines cannot be the machine-readable manifest.
        if not stripped_line.startswith("{"):
            continue

        try:
            parsed_value = json.loads(stripped_line)
        except json.JSONDecodeError:
            # A log line beginning with a brace is ignored only if another valid
            # manifest follows earlier in the reverse search.
            continue

        # The schema requires a JSON object so named fields can be validated.
        if isinstance(parsed_value, dict):
            return parsed_value

    raise ValueError("ns-3 output did not contain a valid Gate 1A JSON manifest")


def run_selection(
    ns3_root: Path, coordinates: ValidationCoordinates, ratio: float
) -> dict[str, Any]:
    """Execute the Gate 1A example once and return its parsed manifest.

    Args:
        ns3_root: Root of the ns-3.48 source tree.
        coordinates: Reproducibility coordinates.
        ratio: Attacker proportion to test.

    Returns:
        Parsed manifest emitted by the C++ scenario.

    Raises:
        RuntimeError: If ns-3 exits unsuccessfully.
        ValueError: If stdout lacks a valid manifest.
    """

    command = build_run_command(ns3_root, coordinates, ratio)
    try:
        completed_process = subprocess.run(
            command,
            cwd=ns3_root,
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError as error:
        # Executable, permission, and working-directory failures are normalized
        # as explicit run failures so the caller emits one concise FAIL record.
        raise RuntimeError(f"could not execute ns-3 wrapper: {error}") from error

    # A failed simulation provides no admissible scientific observation.  Both
    # output streams are retained in the exception for compiler/runtime triage.
    if completed_process.returncode != 0:
        raise RuntimeError(
            "Gate 1A execution failed\n"
            f"command: {command!r}\n"
            f"stdout:\n{completed_process.stdout}\n"
            f"stderr:\n{completed_process.stderr}"
        )

    return extract_manifest(completed_process.stdout)


def expected_attacker_count(node_count: int, attacker_ratio: float) -> int:
    """Implement the manuscript count equation for independent validation."""

    # This is the same declared equation, independently evaluated in Python:
    # N_a = floor(r_a * N + 0.5) for non-negative inputs.
    return math.floor(attacker_ratio * node_count + 0.5)


def validate_manifest(
    manifest: dict[str, Any],
    coordinates: ValidationCoordinates,
    attacker_ratio: float,
) -> RatioValidationResult:
    """Validate schema, count, identities, exclusions, and RNG coordinates.

    Args:
        manifest: Parsed C++ scenario output.
        coordinates: Expected population and RNG coordinates.
        attacker_ratio: Expected attacker proportion.

    Returns:
        Normalized validation result.  Replay is filled by the caller.

    Raises:
        ValueError: If any scientific or schema invariant is violated.
    """

    required_fields = {
        "schemaVersion",
        "gate",
        "nodeCount",
        "attackerRatio",
        "attackerCount",
        "seed",
        "run",
        "attackerSelectionStream",
        "excludedNodeIds",
        "attackerNodeIds",
    }

    # A missing field makes the record unsuitable for reproducible comparison.
    missing_fields = required_fields.difference(manifest)
    if missing_fields:
        raise ValueError(f"manifest is missing fields: {sorted(missing_fields)}")

    # Schema and gate labels prevent a newer or unrelated executable from being
    # accepted accidentally by this Gate 1A validator.
    if int(manifest["schemaVersion"]) != 1 or manifest["gate"] != "1A":
        raise ValueError("manifest schemaVersion or gate label is incompatible")

    expected_count = expected_attacker_count(coordinates.node_count, attacker_ratio)

    try:
        attacker_node_ids = tuple(int(node_id) for node_id in manifest["attackerNodeIds"])
        excluded_node_ids = {int(node_id) for node_id in manifest["excludedNodeIds"]}
    except (TypeError, ValueError) as error:
        # Identity arrays must contain integer-compatible scalar values.  An
        # incompatible schema is rejected rather than partially coerced.
        raise ValueError("manifest node-ID arrays are malformed") from error

    # These exact coordinates identify the intended simulation rather than a
    # stale output file or an execution with modified randomization settings.
    expected_coordinates = (
        coordinates.node_count,
        coordinates.seed,
        coordinates.run,
        coordinates.attack_stream,
    )
    observed_coordinates = (
        int(manifest["nodeCount"]),
        int(manifest["seed"]),
        int(manifest["run"]),
        int(manifest["attackerSelectionStream"]),
    )
    if observed_coordinates != expected_coordinates:
        raise ValueError(
            f"manifest RNG/population coordinates differ: {observed_coordinates}"
        )

    # Floating-point ratios are compared with a tight absolute tolerance because
    # the C++ manifest deliberately prints eight decimal places.
    if not math.isclose(float(manifest["attackerRatio"]), attacker_ratio, abs_tol=1e-12):
        raise ValueError("manifest attackerRatio differs from the requested ratio")

    # Declared count, list cardinality, and independent mathematical expectation
    # must all agree; otherwise the experimental attack intensity is mislabeled.
    if int(manifest["attackerCount"]) != expected_count:
        raise ValueError("manifest attackerCount differs from the count equation")
    if len(attacker_node_ids) != expected_count:
        raise ValueError("attackerNodeIds cardinality differs from attackerCount")

    # Set cardinality rejects duplicate identities without changing list order.
    if len(set(attacker_node_ids)) != len(attacker_node_ids):
        raise ValueError("attackerNodeIds contains duplicates")

    # Endpoint leakage would alter source/destination roles across attack levels.
    if excluded_node_ids.intersection(attacker_node_ids):
        raise ValueError("an excluded endpoint appears in attackerNodeIds")

    # Canonical sorting is required for byte-stable manifests and paired checks.
    if attacker_node_ids != tuple(sorted(attacker_node_ids)):
        raise ValueError("attackerNodeIds is not sorted")

    return RatioValidationResult(
        attacker_ratio=attacker_ratio,
        expected_count=expected_count,
        observed_count=int(manifest["attackerCount"]),
        attacker_node_ids=attacker_node_ids,
        deterministic_replay=False,
    )


def main() -> int:
    """Run every mandatory ratio twice and write a validated evidence report."""

    arguments = parse_arguments()
    ns3_root = arguments.ns3_root.expanduser().resolve()
    coordinates = ValidationCoordinates(
        node_count=arguments.node_count,
        seed=arguments.seed,
        run=arguments.run,
        attack_stream=arguments.attack_stream,
    )

    try:
        validate_coordinates(coordinates)

        # The wrapper must exist at the supplied ns-3 root; rejecting a wrong
        # directory early produces a clearer diagnostic than subprocess ENOENT.
        if not (ns3_root / "ns3").is_file():
            raise FileNotFoundError(f"ns-3 wrapper not found at {ns3_root / 'ns3'}")

        validated_results: list[RatioValidationResult] = []

        # Each mandatory ratio is executed twice under identical coordinates.
        # The invariant after iteration k is that all k prior ratios have exact
        # counts, valid roles, and bit-identical parsed manifests.
        for attacker_ratio in MANDATORY_ATTACKER_RATIOS:
            first_manifest = run_selection(ns3_root, coordinates, attacker_ratio)
            second_manifest = run_selection(ns3_root, coordinates, attacker_ratio)
            normalized_result = validate_manifest(
                first_manifest, coordinates, attacker_ratio
            )
            validate_manifest(second_manifest, coordinates, attacker_ratio)

            # Equality of the complete objects checks IDs and every recorded RNG
            # coordinate, not merely the expected attacker cardinality.
            if first_manifest != second_manifest:
                raise ValueError(
                    f"deterministic replay failed for attackerRatio={attacker_ratio}"
                )

            validated_results.append(
                RatioValidationResult(
                    attacker_ratio=normalized_result.attacker_ratio,
                    expected_count=normalized_result.expected_count,
                    observed_count=normalized_result.observed_count,
                    attacker_node_ids=normalized_result.attacker_node_ids,
                    deterministic_replay=True,
                )
            )

        output_document = {
            "schemaVersion": 1,
            "gate": "1A",
            "status": "PASS",
            "coordinates": asdict(coordinates),
            "results": [asdict(result) for result in validated_results],
        }
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(
            json.dumps(output_document, indent=2) + "\n", encoding="utf-8"
        )
        print(f"Gate 1A validation PASS: {arguments.output.resolve()}")
        return 0
    except (FileNotFoundError, RuntimeError, ValueError) as error:
        # Validation failures remain explicit and never generate a PASS report.
        print(f"Gate 1A validation FAIL: {error}", file=sys.stderr)
        return 1


# This condition makes imports side-effect free while retaining direct command-
# line execution for the validation campaign.
if __name__ == "__main__":
    sys.exit(main())
