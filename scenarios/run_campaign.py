"""Run replications of manet_scenario.py for several protocol variants in parallel.

Each replication is a separate process, because an ns-3 simulation is a
process-wide singleton. Variants are described in a JSON file::

    [
      {"label": "aodv-default", "protocol": "aodv", "args": ["--aodv-hello"]},
      {"label": "odr", "protocol": "odr", "args": []}
    ]

Every variant is run for replications --first-run .. --first-run + --runs - 1
with the scenario arguments given after ``--``, identical for all variants
so that replications pair up (same config_id, seed and run). Replications
whose metadata already exists are skipped, so an interrupted campaign can be
resumed by running the same command again.

The script must run in the ns-3 environment, e.g.::

    ./ns3 run "contrib/odr/scenarios/run_campaign.py \\
        contrib/odr/scenarios/campaigns/aodv_screening.json --runs 10 --jobs 4 \\
        -- --outdir results/screening"
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

SCENARIO = Path(__file__).resolve().parent / "manet_scenario.py"


@dataclass(frozen=True)
class Job:
    """One replication of one variant."""

    label: str
    run: int
    argv: tuple[str, ...]


def load_variants(path: Path) -> list[dict]:
    """Read and validate the variant file.

    Args:
        path: JSON file listing the variants.

    Returns:
        The variants, each with "label", "protocol" and "args".
    """
    variants = json.loads(path.read_text(encoding="utf-8"))
    labels = [variant.get("label") for variant in variants]
    if not variants or any(not label for label in labels):
        raise SystemExit(f"{path}: every variant needs a non-empty label")
    if len(set(labels)) != len(labels):
        raise SystemExit(f"{path}: duplicate labels")
    for variant in variants:
        if "protocol" not in variant or not isinstance(variant.get("args", []), list):
            raise SystemExit(
                f"{path}: variant {variant['label']} needs a protocol and an args list"
            )
    return variants


def build_jobs(
    variants: Sequence[dict], first_run: int, runs: int, common: Sequence[str]
) -> list[Job]:
    """Expand variants and replications into scenario command lines.

    Replications are interleaved across variants, so that an interrupted
    campaign leaves every variant with a comparable number of runs.

    Args:
        variants: Validated variants.
        first_run: First RngRun value.
        runs: Number of replications per variant.
        common: Scenario arguments shared by every job.

    Returns:
        The jobs, in launch order.
    """
    jobs = []
    for run in range(first_run, first_run + runs):
        for variant in variants:
            argv = (
                "--protocol",
                variant["protocol"],
                "--label",
                variant["label"],
                "--run",
                str(run),
                "--skip-existing",
                *variant.get("args", []),
                *common,
            )
            jobs.append(Job(variant["label"], run, argv))
    return jobs


def execute(job: Job, log_dir: Path) -> tuple[Job, bool, float]:
    """Run one replication, keeping its output only if it fails.

    Args:
        job: The replication.
        log_dir: Where failure logs are written.

    Returns:
        (job, success, wall-clock seconds).
    """
    start = time.monotonic()
    result = subprocess.run(
        [sys.executable, str(SCENARIO), *job.argv], capture_output=True, text=True
    )
    elapsed = time.monotonic() - start
    if result.returncode != 0:
        log = log_dir / f"failed_{job.label}_r{job.run}.log"
        log.write_text(result.stdout + result.stderr, encoding="utf-8")
    return job, result.returncode == 0, elapsed


def main(argv: Sequence[str]) -> int:
    """Entry point.

    Args:
        argv: Command-line arguments, program name excluded.

    Returns:
        0 if every replication succeeded, 1 otherwise.
    """
    if "--" in argv:
        split = list(argv).index("--")
        own, common = list(argv[:split]), list(argv[split + 1 :])
    else:
        own, common = list(argv), []
    parser = argparse.ArgumentParser(description="Run a campaign of MANET scenario replications.")
    parser.add_argument("variants", type=Path, help="JSON file listing the variants")
    parser.add_argument("--runs", type=int, default=10)
    parser.add_argument("--first-run", type=int, default=1)
    parser.add_argument("--jobs", type=int, default=4, help="Parallel simulations")
    args = parser.parse_args(own)
    if args.runs < 1 or args.jobs < 1:
        parser.error("--runs and --jobs must be positive")
    if any(flag in common for flag in ("--protocol", "--run", "--label")):
        parser.error("--protocol, --run and --label are set per job, not in the common arguments")

    variants = load_variants(args.variants)
    jobs = build_jobs(variants, args.first_run, args.runs, common)
    log_dir = Path("campaign-logs")
    log_dir.mkdir(exist_ok=True)

    failures = 0
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(execute, job, log_dir) for job in jobs]
        for done, future in enumerate(as_completed(futures), start=1):
            job, ok, elapsed = future.result()
            failures += not ok
            status = "ok" if ok else f"FAILED, see {log_dir}"
            print(f"[{done}/{len(jobs)}] {job.label} run {job.run}: {status} ({elapsed:.0f} s)")
    print(f"{len(jobs) - failures} of {len(jobs)} replications succeeded")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
