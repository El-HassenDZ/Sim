# Gate status: blueprint plan versus delivered artefact

**Blueprint:** `docs/engineering-blueprint-v0.2.md` (v0.2, 3 September 2026)
**Artefact under review:** MTC-AODV ns-3.48 Gate 1A v0.1

This file exists so that the distance between the plan and the code is a
recorded quantity rather than an impression. It is updated at each gate.

## 1. Blueprint Gate 1 decomposition

The blueprint's Gate 1 (§7) lists six steps. The delivered package renames
itself "Gate 1A" and covers a subset. The mapping is:

| Blueprint Gate 1 step | Delivered in 1A | Location |
|---|---|---|
| 1. `contrib/mtcaodv/CMakeLists.txt` with `build_lib` | Yes | `contrib/mtcaodv/CMakeLists.txt` |
| 2. Import ns-3.48 routing logic under renamed classes | **No** | deferred to 1B |
| 3. `MtcAodvHelper::Copy/Create/Set/AssignStreams` | **No** | not present, not mentioned in the package README |
| 4. Deterministic attacker selection without replacement | Yes | `helper/attack-manager.{h,cc}` |
| 5. Forged attractive RREP and transit-data dropping | **Policy only** | `model/blackhole-behavior.{h,cc}`; no RREP is emitted and no packet is dropped |
| 6. Assert exact counts for all mandatory ratios | Yes | `test/attack-manager-test-suite.cc`, `experiments/validate_gate1.py` |

Steps 2 and 3 are the two that carry integration risk. Neither is exercised.
The package README documents the deferral of step 5's routing hooks; it does
not document the absence of step 3. That omission matters because Gate 1B's
stated entry criteria ("the module library and example compile") are satisfied
by a module that contains no helper, so the criteria as written do not
guarantee that the helper contract compiles before the AODV fork lands.

## 2. Blueprint directory tree versus files on disk

The blueprint §4 tree declares 90 source and configuration files. Present:

| Group | Declared | Present | Note |
|---|---:|---:|---|
| `contrib/mtcaodv/model/` | 39 | 4 | attack policy only |
| `contrib/mtcaodv/helper/` | 6 | 2 | `attack-manager` only; no `mtc-aodv-helper`, no `experiment-configuration` |
| `contrib/mtcaodv/test/` | 10 | 2 | attack-manager, blackhole-behavior |
| `contrib/mtcaodv/examples/` | 3 | 2 | `mtcaodv-gate1` replaces the two planned examples |
| `contrib/mtcaodv/doc/` | 4 | 1 | Doxygen group definition only |
| `experiments/` (Python package) | 16 | 1 | `validate_gate1.py`; no package, no schemas, no configs |
| `docs/` | 5 | 0 in package | supplied separately in this repository |
| Root metadata | 4 | 2 | `README.md`, feedback template; no `CHANGELOG.md`, `AUTHORS.md`, `pyproject.toml` |

None of the equation owners for Equations (3)-(22) exist yet. Equation (2)
(`n_A = round(r_a N)`) is the only one implemented, as
`AttackManager::ComputeAttackerCount`.

## 3. Traceability of Equation (2)

The blueprint writes Equation (2) as `n_A = round(r_a N)`. The implementation
uses `N_a = floor(r_a N + 0.5)` (`helper/attack-manager.cc:88`). These agree
for all non-negative inputs; they differ from `std::round` only in tie
direction, and only for negative arguments, which the domain check excludes.
The divergence is documented in the code and is acceptable. The manuscript
should nevertheless state the half-up convention explicitly, because
`round()` in several languages is half-to-even.

## 4. Open deviations to record before Gate 1B

1. `MtcAodvHelper` is absent (blueprint Gate 1 step 3).
2. `AttackManager` and `AttackBehavior` are never connected: nothing in the
   package installs a `BlackholeBehavior` onto the node identifiers that
   `AttackManager` selects. Both halves compile; the seam between them does
   not exist yet.
3. The package asserts two reproducibility properties that nothing tests:
   nested attacker sets across ratios, and changed selections across run
   numbers. See `docs/gate1a-review.md`, findings R-07 and R-08.
4. Licence: the module files carry `SPDX-License-Identifier: GPL-2.0-only`,
   which is correct for an ns-3 contributed module. The repository root
   currently carries an unrelated MIT `LICENSE`. This must be resolved before
   any publication or distribution.
