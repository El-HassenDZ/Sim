# Validation status — supersedes the "no compilation result" note in README.md

`README.md` states: *"No compilation result is claimed for this package in the
current environment, because ns-3.48 is installed on the researcher's other
computer."* That was true of the v0.1 package. It is no longer the status of
this tree.

The module has since been compiled and tested against an unmodified ns-3.48
source tree. `README.md` is kept verbatim as authored; this file records the
outcome.

| Check | Result |
|---|---|
| `./ns3 build` with `contrib/mtcaodv` installed | exit 0 — module library, test library and example linked, **0 errors, 0 warnings** |
| `./test.py -s mtcaodv-attack-manager` | **PASS** |
| `./test.py -s mtcaodv-blackhole-behavior` | **PASS** |
| Stock AODV suites (Gate 0 baseline) | all 4 **PASS**, `src/aodv` unmodified |
| `experiments/validate_gate1.py` | **PASS** — counts 5/10/20/30, deterministic replay on all four ratios |
| Nesting 0.05 ⊂ 0.10 ⊂ 0.20 ⊂ 0.30 | Confirmed by execution |
| Run-number sensitivity | runs 1/2/3 give three distinct attacker sets |

Environment: ns-3.48, g++ 13.3.0, CMake 3.28.3, Ninja 1.11.1, `-std=c++23
-Wall -Wpedantic`.

Raw unedited command output: `evidence/gate1a/`.

## Corrections found during validation

1. **Configure line.** `--enable-modules=aodv,mtcaodv` fails to build, in
   ns-3's own `src/aodv/examples/aodv-example.cc` (it includes
   `ns3/point-to-point-module.h`). Use
   `--enable-modules=aodv,mtcaodv,point-to-point`, or the full module set.
2. **Gate 0 command.** `./test.py -s aodv` names a suite that does not exist in
   ns-3.48. The AODV suites are `routing-aodv`, `aodv-routing-id-cache`,
   `routing-aodv-loopback`, `routing-aodv-regression`.
3. **Observation source.** `MacPromiscRx` cannot support the planned
   `ForwardingObserver`. See `docs/ns3.48-api-verification.md` §2.

## Where to read next

| Document | Contents |
|---|---|
| `docs/gate1a-review.md` | Twelve findings (R-01…R-12), the build record, and the argument about gate ordering |
| `docs/ns3.48-api-verification.md` | 15 blueprint API claims checked line-by-line against ns-3.48; 14 confirmed, 1 corrected |
| `docs/gate-status.md` | Planned file tree versus what exists; recorded deviations |
| `docs/engineering-blueprint-v0.2.md` | The architecture gate document this package implements |
| `experiments/tests/test_validate_gate1.py` | 29 properties of the campaign validator, runnable without ns-3 |
