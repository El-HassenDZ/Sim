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
| 2. Import ns-3.48 routing logic under renamed classes | **Gate 1B** | `model/blackhole-aodv-routing-protocol.{h,cc}` |
| 3. Helper `Copy/Create/Set/AssignStreams` | **Gate 1B** | `helper/blackhole-aodv-helper.{h,cc}` (malicious side; the defensive `MtcAodvHelper` follows in a later gate) |
| 4. Deterministic attacker selection without replacement | Yes | `helper/attack-manager.{h,cc}` |
| 5. Forged attractive RREP and transit-data dropping | **Gate 1B** | policy in `model/blackhole-behavior.{h,cc}`; the two routing hooks in the forked protocol |
| 6. Assert exact counts for all mandatory ratios | Yes | `test/attack-manager-test-suite.cc`, `experiments/validate_gate1.py` |

All six steps are now implemented. Steps 2, 3 and 5 were closed in Gate 1B;
see `gate1b-record.md`. The Gate 1A observation stands as a record of how the
gate boundary was drawn: its stated entry criteria for Gate 1B ("the module
library and example compile") were satisfiable by a module containing no
helper at all, so the criteria as written did not guarantee what they appeared
to guarantee.

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

## 3b. Correction to the Gate 0 command

The blueprint's Gate 0 specifies `./test.py -s aodv`. That suite name does not
exist in ns-3.48 and the command exits 2. The four real suites are
`routing-aodv`, `aodv-routing-id-cache`, `routing-aodv-loopback` and
`routing-aodv-regression`; all four pass with `contrib/mtcaodv` installed
(`evidence/gate1a/aodv-baseline-test.log`).

## 4. Open deviations to record before Gate 1B

1. ~~`MtcAodvHelper` is absent~~ — closed in Gate 1B by `BlackholeAodvHelper`.
   The defensive helper is still outstanding and belongs to a later gate.
2. ~~`AttackManager` and `AttackBehavior` are never connected~~ — closed in
   Gate 1B by `AttackManager::PartitionByAttackers` plus helper composition.
3. The package asserts two reproducibility properties that nothing tests:
   nested attacker sets across ratios, and changed selections across run
   numbers. Both were verified by execution (`gate1a-review.md` §7.3) but
   neither is covered by a test suite. Still open.
3b. The Gate 1B measurement scenario has no valid baseline: the no-attack PDR
   is 0.052 as parameterised, and the regime is marginal connectivity rather
   than congestion. See `gate1b-record.md` §6 and `phase1-design-review.md`
   D-09. Blocks any confirmatory campaign.
4. The blueprint's Gate 0 command names a non-existent test suite (§3b).
5. Licence: the module files carry `SPDX-License-Identifier: GPL-2.0-only`,
   which is correct for an ns-3 contributed module. The repository root
   currently carries an unrelated MIT `LICENSE`. This must be resolved before
   any publication or distribution.
