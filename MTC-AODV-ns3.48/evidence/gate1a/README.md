# Gate 1A evidence — build and test record

Produced on 3 September 2026 by compiling this module against an unmodified
ns-3.48 tree. Files are the raw command outputs, unedited.

| File | Command | Outcome |
|---|---|---|
| `mtcaodv-gate1-build.log` | `./ns3 build` | exit 0; mtcaodv library, test library and example linked with 0 errors, 0 warnings |
| `mtcaodv-attack-manager-test.log` | `./test.py -s mtcaodv-attack-manager --verbose` | PASS |
| `mtcaodv-blackhole-behavior-test.log` | `./test.py -s mtcaodv-blackhole-behavior --verbose` | PASS |
| `aodv-baseline-test.log` | the four stock AODV suites | all PASS (Gate 0 baseline) |
| `mtcaodv-gate1-validation.json` | `validate_gate1.py` | status PASS, four ratios, replay confirmed |

## Environment

```text
ns-3 VERSION                : 3.48
Compiler                    : g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
CMake / generator           : 3.28.3 / Ninja 1.11.1
Standard and warning flags  : -std=c++23 -Wall -Wpedantic
NS3_WARNINGS_AS_ERRORS      : OFF (ns-3 default profile)
Configure                   : ./ns3 configure --enable-modules=aodv,mtcaodv,point-to-point \
                                             --enable-examples --enable-tests
```

`src/aodv` was not modified. Only `contrib/mtcaodv` was added.
