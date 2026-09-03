# MTC-AODV for ns-3.48 — Gate 1A

## Delivered increment

This package is the first deliberately small compilation gate of the
MTC-AODV implementation. It contains:

- a separate `contrib/mtcaodv/` module; the stock `src/aodv/` module is not
  modified;
- an attack-policy interface that keeps malicious ground truth outside every
  defensive component;
- a configurable, deterministic full-Blackhole policy;
- attacker selection without replacement on an explicitly assigned ns-3 RNG
  stream;
- exact attacker counts of 5, 10, 20, and 30 for the mandatory 100-node,
  5%, 10%, 20%, and 30% scenarios;
- C++ unit tests for the count equation, reproducibility, uniqueness, endpoint
  exclusion, activation, forged-RREP fields, and transit-drop decisions;
- a minimal ns-3 executable that emits a one-line JSON experiment manifest;
- a dependency-free Python validator that executes and checks all mandatory
  ratios twice.

Gate 1A does **not yet inject forged RREPs into AODV or discard forwarded IPv4
packets**. Those two routing hooks form Gate 1B and will be implemented against
the exact compiler diagnostics from this small ns-3.48 integration gate. This
staging is intentional: it prevents a large AODV fork from hiding basic module,
header-export, or test-runner incompatibilities.

No compilation result is claimed for this package in the current environment,
because ns-3.48 is installed on the researcher's other computer.

## File map

| Path | Responsibility |
|---|---|
| `contrib/mtcaodv/model/attack-behavior.{h,cc}` | Abstract contract between an attacker policy and the future routing adapter |
| `contrib/mtcaodv/model/blackhole-behavior.{h,cc}` | Full-Blackhole activation, forged-reply profile, and transit-drop decisions |
| `contrib/mtcaodv/helper/attack-manager.{h,cc}` | Exact deterministic selection without replacement |
| `contrib/mtcaodv/test/attack-manager-test-suite.cc` | Count, replay, uniqueness, exclusion, and validation tests |
| `contrib/mtcaodv/test/blackhole-behavior-test-suite.cc` | Blackhole policy semantics tests |
| `contrib/mtcaodv/examples/mtcaodv-gate1.cc` | Machine-readable selection-manifest executable |
| `experiments/validate_gate1.py` | External campaign validator for all four required ratios |

## Mathematical traceability

| Scientific item | Implementation | Domain / unit | Gate 1A test |
|---|---|---|---|
| `N_a = floor(r_a N + 0.5)` | `AttackManager::ComputeAttackerCount()` | `r_a in [0,1]`; `N, N_a` in nodes | `RequiredAttackerCountTestCase` |
| Sampling without replacement | `AttackManager::SelectAttackers()` | Unique ns-3 node IDs | `DeterministicSelectionTestCase` |
| Paired-run ground truth | `AttackSelectionResult::attackerNodeIds` | Sorted ID set | C++ replay test and Python replay check |
| `t >= t_attack` | `BlackholeBehavior::IsActive()` | ns-3 simulation time | `FullBlackholePolicyTestCase` |
| `seq_fake = seq_observed + Delta_seq (mod 2^32)` | `CreateForgedReplyProfile()` | 32-bit AODV sequence space | `FullBlackholePolicyTestCase` |
| Full transit-data drop | `ShouldDropTransitPacket()` | Boolean policy decision | `FullBlackholePolicyTestCase` |

## Installation in the tested ns-3.48 tree

Assume that this extracted package is located at
`/path/to/MTC-AODV-ns3.48` and ns-3.48 is located at `/path/to/ns-3.48`.

From the ns-3.48 root:

```bash
cp -R /path/to/MTC-AODV-ns3.48/contrib/mtcaodv ./contrib/mtcaodv
./ns3 configure --enable-examples --enable-tests
./ns3 build 2>&1 | tee mtcaodv-gate1-build.log
```

The copy command creates only `contrib/mtcaodv`. It does not write into
`src/aodv`.

## Unit tests

List the registered test suites first:

```bash
./test.py --list | grep mtcaodv
```

Then execute both Gate 1A suites:

```bash
./test.py -s mtcaodv-attack-manager --verbose \
  2>&1 | tee mtcaodv-attack-manager-test.log

./test.py -s mtcaodv-blackhole-behavior --verbose \
  2>&1 | tee mtcaodv-blackhole-behavior-test.log
```

Expected outcome: both suites report `PASS`. This is an expected result, not a
claim that the tests were already executed here.

## Minimal deterministic checks

Run one ratio manually:

```bash
./ns3 run "mtcaodv-gate1 --nodeCount=100 --attackerRatio=0.05 \
  --seed=12345 --run=1 --attackStream=73001 \
  --excludeTrafficEndpoints=true"
```

The last output line must be valid JSON and must contain
`"attackerCount":5`. The attacker IDs themselves may differ when the seed,
run, or stream differs, but the count cannot differ.

Validate every mandatory ratio twice with Python:

```bash
python3 /path/to/MTC-AODV-ns3.48/experiments/validate_gate1.py \
  --ns3-root /path/to/ns-3.48 \
  --node-count 100 \
  --seed 12345 \
  --run 1 \
  --attack-stream 73001 \
  --output mtcaodv-gate1-validation.json
```

Expected terminal message:

```text
Gate 1A validation PASS: .../mtcaodv-gate1-validation.json
```

The report is generated only after the observed counts are exactly
`5, 10, 20, 30`, every set is unique and excludes the two endpoints, and each
same-coordinate replay returns an identical complete manifest.

Using the same seed, run, and attacker-selection stream across attack ratios
also creates nested selections in separate scenario processes: the 5% set is a
prefix-subset of the 10% set before canonical sorting, and so on. Across
independent run numbers, the selected geography changes. This supports paired
comparisons while avoiding a single fixed malicious placement.

## What to return after compilation

Please return these files without editing their contents:

1. `mtcaodv-gate1-build.log`;
2. `mtcaodv-attack-manager-test.log`;
3. `mtcaodv-blackhole-behavior-test.log`;
4. `mtcaodv-gate1-validation.json` if validation reached the Python stage;
5. the output of `git -C /path/to/ns-3.48 describe --always --dirty --tags`.

If compilation fails, the complete first build log is sufficient to begin a
targeted correction. Do not paraphrase or truncate the first compiler error,
because later diagnostics may be consequences of that initial failure.

## Gate 1B entry criteria

Gate 1B starts only after:

- the module library and example compile under the exact ns-3.48 installation;
- both C++ test suites are registered and pass;
- the Python validator confirms exact deterministic selections.

Gate 1B will then add an isolated AODV-compatible routing implementation,
invoke `BlackholeBehavior` while processing RREQs, and discard only transit
data at the IPv4 forwarding decision. The baseline `src/aodv/` implementation
will remain unchanged.
