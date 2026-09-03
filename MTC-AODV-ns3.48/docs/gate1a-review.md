# Gate 1A v0.1 — engineering review

**Reviewed artefact:** MTC-AODV ns-3.48 Gate 1A v0.1 (8 C++ files, 1 Python file)
**Reviewed against:** `docs/engineering-blueprint-v0.2.md`
**Method:** source reading, plus execution of the checks recorded in §2 and §3.
**Scope note:** the module sources were not modified. Every correction proposed
here is a proposal, not an applied change.

Findings are labelled `R-nn`. Severity is stated as the consequence if the
finding is not addressed, not as a subjective ranking.

---

## 1. Summary judgement

The package does what it says it does, and it says clearly what it does not do.
The honesty of the README is the strongest property of this deliverable: it
refuses to claim a compilation it did not perform, it names the deferred
routing hooks, and it asks for unedited logs rather than paraphrase. That
discipline is unusual and worth keeping.

The substantive criticism is not about code quality. It is about **what was
chosen to be built first**. Gate 1A implements the two components of the design
with the lowest integration risk — an arithmetic function and a boolean policy
object — while the two claims that could invalidate the research programme
remain untested. The gate therefore reduces schedule risk almost not at all.
See §4.

---

## 2. Verified by execution

### 2.1 Compilation against ns-3.48

The build the package declares impossible in its authoring environment was
performed for this review. `contrib/mtcaodv` compiles and links clean. Details
in §7.

### 2.2 Validator decision logic

`experiments/tests/test_validate_gate1.py` (added by this review, runs without
ns-3) exercises `validate_gate1.py` with synthetic manifests. Result: 29
properties, 0 failures. The validator correctly rejects a count below the
equation, unsorted identifiers, duplicate identifiers, a selected endpoint,
mismatched seed / stream / population coordinates, an incompatible gate or
schema label, a cardinality mismatch, a missing field, and stdout containing
no manifest. Manifest extraction from stdout interleaved with build logging
works as designed.

### 2.3 Runtime behaviour of `mtcaodv-gate1`

Every reproducibility property the package README asserts was executed and
holds. Details and manifests in §7.

---

## 3. Findings

### R-01 — `ShouldDropTransitPacket` cannot tell that a packet is transit
**File:** `model/attack-behavior.h:111-113`, `model/blackhole-behavior.cc:129-150`
**Consequence:** silent change of the attack model in Gate 1B.

The most safety-critical decision in the whole attack model has this signature:

```cpp
bool ShouldDropTransitPacket(Time now, bool isRoutingControl, bool isSecurityControl) const;
```

Nothing in the arguments identifies the packet as transit. The word "transit"
appears only in the method name and the documentation. The caller — which does
not exist yet — carries the entire obligation. In Gate 1B there will be at
least two plausible call sites (the `RouteInput` forwarding decision and any
local-delivery path). If either passes a locally originated or locally
destined packet, the Blackhole silently starts dropping its own traffic, and
the resulting PDR degradation will be attributed to the attack rather than to
the bug.

**Proposed correction, before any caller exists:**

```cpp
struct TransitDecisionContext
{
    Time observationTime;
    bool isTransit{false};          // false for locally sourced or delivered
    bool isRoutingControl{false};
    bool isSecurityControl{false};
};

bool ShouldDropPacket(const TransitDecisionContext& context) const;
```

with `if (!context.isTransit) { return false; }` as the first guard. This
converts an unenforced precondition into a value the compiler makes the caller
supply. The cost of the change is minutes now and a multi-site refactor later.

### R-02 — `AttackManager` and `AttackBehavior` are never connected
**Files:** all
**Consequence:** the seam that Gate 1B depends on is unproven at Gate 1B entry.

`AttackManager` produces node identifiers. `BlackholeBehavior` is an `Object`
with a `TypeId` and attributes. No code aggregates the second onto the nodes
named by the first. `MtcAodvHelper` — blueprint Gate 1 step 3 — does not exist.
The package therefore proves that two isolated compilation units build; it does
not prove that the object model works.

**Proposal:** add a ~60-line `MtcAodvHelper` that does only installation
(`Copy`, `Create`, `Set`, `AssignStreams` per the verified `AodvHelper`
contract at `src/aodv/helper/aodv-helper.h:36,46,53,65`), plus one test that
selects attackers and asserts that exactly those nodes carry an aggregated
`BlackholeBehavior` and the others do not. This is the missing half of Gate 1A
and it is cheap.

### R-03 — Validator accepts identifiers outside the node population
**File:** `experiments/validate_gate1.py:319-334`
**Consequence:** a corrupted manifest passes validation.

A manifest declaring `nodeCount: 100` and `attackerNodeIds: [3,17,42,55,999999]`
is accepted. So is one containing `-5`. Verified in
`experiments/tests/test_validate_gate1.py::test_known_validator_gaps`.

**Proposal:** assert `0 <= node_id < node_count` for every identifier.

### R-04 — Endpoint-exclusion check is vacuous and self-referential
**File:** `experiments/validate_gate1.py:329-330`
**Consequence:** a PASS report can certify a protection that was not applied.

The check is `excluded_node_ids.intersection(attacker_node_ids)`, where
`excluded_node_ids` is read **from the manifest the executable produced**. If
`mtcaodv-gate1` ignored `--excludeTrafficEndpoints`, it would emit
`excludedNodeIds: []`, the intersection would be empty, and the validator would
report PASS. The validator is asking the accused to state the charges.
A manifest with `excludedNodeIds: [7, 8]` is likewise accepted, even though the
command line requested endpoints 0 and 99.

This is not hypothetical. Running the compiled binary with
`--excludeTrafficEndpoints=false` emits `"excludedNodeIds":[]` and selects
node 1 (§7.3). That manifest passes `validate_manifest` unchanged.

**Proposal:** the validator knows what it requested. When
`--excludeTrafficEndpoints=true` is passed, assert
`set(manifest["excludedNodeIds"]) == {0, node_count - 1}` before using it for
anything else. This is the difference between validating an invariant and
reading it back.

### R-05 — Ratio is applied to `N`, sampling is from `N - |excluded|`
**Files:** `helper/attack-manager.h:27-32`, `helper/attack-manager.cc:144-181`
**Consequence:** a definitional ambiguity in every per-node metric.

The convention is documented and defensible: `r_a = 0.30` with `N = 100` and
two excluded endpoints yields 30 attackers among 98 eligible nodes, i.e. 30.6%
of the eligible population and 30.0% of the nominal population. Both numbers
are correct; they answer different questions.

**Proposal:** fix the denominator convention in the metrics definitions now,
before Gate 6 freezes them, and state it in the manuscript. The false-quarantine
rate of Equation (21) in particular must declare whether its denominator is the
98 eligible nodes or the 100 nominal ones.

### R-06 — Nested attacker sets across ratios are a design decision, not a free property
**File:** `README.md`, "Minimal deterministic checks"
**Consequence:** an unstated dependence structure in the confirmatory analysis.

The README presents nesting (the 5% set ⊂ the 10% set ⊂ …) as a benefit. The
mechanism is real: partial Fisher-Yates draw `i` uses the range `[i, E-1]`
independently of `N_a`, so a shared stream produces identical prefixes. This
review verified the structural property with an independent model of the
algorithm; it holds for any underlying RNG.

But the consequence is that **attacker ratio is not independently randomised
within a seed**. The four ratio levels are positively dependent by
construction: whatever is peculiar about the position of the first five
selected nodes propagates to all higher intensities. The blueprint's Gate 6
promises "protocol-by-attacker-ratio interactions" with Holm-adjusted families.
A matched design across ratios is a legitimate choice and is often the more
powerful one — but it is a choice, and the inference must be built for it.

**Proposal:** decide explicitly and record the decision. Either (a) keep the
nesting and treat ratio as a within-seed repeated-measures factor, or (b)
derive a per-ratio stream (`baseStream + ratioIndex`) and treat ratio levels as
independent. Do not leave this to be discovered during analysis.

### R-07 — The nesting property is asserted, holds, and is not tested
**Files:** `README.md`; `test/attack-manager-test-suite.cc`; `experiments/validate_gate1.py`
**Consequence:** a documented reproducibility guarantee can break silently.

The property is real: it was executed against the compiled binary (§7.3) and
holds for all three inclusions. But neither C++ suite nor the Python validator
checks it. A future refactor of the sampling loop — for example, drawing over
`[0, E-1]` and rejecting duplicates instead of using the partial shuffle —
would preserve every existing test while destroying the property the README
advertises.

**Proposal:** one test case asserting
`set(select(r=0.05)) ⊂ set(select(r=0.10)) ⊂ set(select(r=0.20)) ⊂ set(select(r=0.30))`
under fixed seed, run, and stream.

### R-08 — "Across independent run numbers, the selected geography changes" holds and is untested
**File:** `README.md`
**Consequence:** as R-07.

Verified in §7.3: runs 1, 2 and 3 produce three disjointly different sets. But
nothing in the test suites asserts it. If a future stream/substream wiring
error made the run number inert, every test in the package would still pass,
and paired runs across replications would silently share one malicious
placement — which would invalidate the variance estimates of the entire
campaign.

**Proposal:** one test case comparing selections at `run = 1` and `run = 2`.

### R-09 — Global RNG state is mutated inside a test case
**File:** `test/attack-manager-test-suite.cc:92-93`
**Consequence:** order-dependent behaviour if suites share a process.

`RngSeedManager::SetSeed(12345)` and `SetRun(7)` are called in `DoRun` and
never restored. `test.py` normally isolates suites in separate processes, so
this is currently benign; a direct `test-runner` invocation running several
suites in one process would inherit the mutated state.

**Proposal:** save and restore the previous seed and run, or document the
requirement for process isolation.

### R-10 — `Validate()` documents an exception it does not exclusively throw
**File:** `helper/attack-manager.h:47-53`, `helper/attack-manager.cc:47-52`
**Consequence:** documentation only.

`Validate()` is documented as throwing `std::logic_error`. It calls
`ComputeAttackerCount`, which can throw `std::invalid_argument` (a
`logic_error`, so consistent) and `std::overflow_error` (a `runtime_error`, so
not). The overflow path is unreachable given the preceding `[0, 1]` domain
check, so this is a contract-documentation defect rather than a live bug.

### R-11 — `hasUsableReverseRoute` is close to vacuous
**File:** `model/attack-behavior.h:93`, `model/blackhole-behavior.cc:105-108`
**Consequence:** none today; a maintenance question for Gate 1B.

In AODV, an intermediate node establishes the reverse route to the originator
while processing the RREQ, before any reply is generated. The parameter will
therefore be `true` at essentially every call site. Keep it only if Gate 1B
identifies a real path where it is false; otherwise it is a guard that reads
as protective while testing nothing.

### R-12 — Licence conflict with the host repository
**Files:** all module sources (`SPDX-License-Identifier: GPL-2.0-only`); repository `LICENSE` (MIT, Microsoft)
**Consequence:** a distribution question that must be answered before publication.

GPL-2.0-only is the correct choice for an ns-3 contributed module and matches
upstream. The repository root carries an unrelated MIT licence inherited from a
GitHub Codespaces template. Resolve explicitly — a per-directory `LICENSE` for
`MTC-AODV-ns3.48/`, or relicensing the repository — rather than leaving two
contradictory grants in one tree.

---

## 4. The ordering criticism

This is the finding that matters more than the eleven above it.

The blueprint's own §2.1 risk table marks two rows as `Medium` feasibility with
named validation risks: **forwarding observation** ("`MacPromiscRx` packet
visibility and transmitter attribution require an ns-3.48 integration spike")
and **certificate-gated quarantine**. Those are the rows that can end the
research programme. Gate 1A touches neither. It delivers, instead, two
components the same table rates `High` feasibility with no unresolved question:
attacker selection and the attack policy.

The result is a gate that could not have failed. `floor(r*N + 0.5) == 5` was
never in doubt. A gate whose failure was not a live possibility has not
purchased information.

The forwarding-observation spike is the item that should have gone first. It
needs no MTC-AODV code, no AODV fork, and no attack model — a plain
AODV + Wi-Fi ad hoc scenario, a promiscuous handler, and a count of how many
neighbour forwards are actually observable. It is perhaps 100 lines. Its
outcome determines whether the evidence layer (Equations 6-10), the trust layer
that consumes it, and the overhead accounting are built on an available signal
or on an assumption.

`docs/ns3.48-api-verification.md` performs part of that spike statically and
already returns a correction: `MacPromiscRx` carries `Ptr<const Packet>` and
nothing else — no transmitter, no receiver, no packet type — and does not fire
at all unless a promiscuous receive callback has already been installed. The
attribution the design needs exists, but through
`Node::RegisterProtocolHandler(..., promiscuous = true)`, not through the trace
the blueprint names. Had Gate 1A been the observation spike, this would have
been known before an AODV fork was scheduled around it.

**The dynamic-behaviour half of that spike is still open**, and it is the half
that carries the real risk: what fraction of a neighbour's forwards is actually
overheard at the chosen transmission range, mobility model, and traffic load.
A design whose evidence layer assumes high observation coverage behaves very
differently at 30% coverage. That number is measurable in a day and is not yet
measured.

## 5. The process bottleneck

The package states: "No compilation result is claimed for this package in the
current environment, because ns-3.48 is installed on the researcher's other
computer." Methodologically this is correct and it is to the author's credit.

Operationally it is the single largest constraint on the project, and it is not
a code problem. Every gate carries a human round-trip: package, transfer, run,
copy logs back, correct. With roughly seven gates and multiple correction
cycles each, the schedule is governed by that latency rather than by the
difficulty of the work.

This review compiled ns-3.48 from source in the review environment in under an
hour of wall-clock time, unattended, on four cores. The `--enable-modules`
switch reduces the build to the dependency closure of `aodv` and `mtcaodv`.
There is no technical reason for the feedback loop to involve a second machine
and a human courier.

**Highest-leverage change available, and it is not a code change:** put ns-3.48
where the code is written — a container image, a VM, or CI running
`./ns3 configure --enable-modules=aodv,mtcaodv,point-to-point --enable-tests`
followed by `./ns3 build` and `./test.py -s mtcaodv-*` on every commit. The
`COMPILATION_FEEDBACK_TEMPLATE.md` round-trip then becomes a fallback for
hardware-specific results rather than the primary channel.

## 6. What is genuinely good and should not be traded away

1. The refusal to claim unverified compilation, and the request for complete
   unedited first diagnostics rather than screenshots.
2. Ground-truth separation: `AttackManager` output is architecturally barred
   from reaching the detector, and the header documents that barrier.
3. Failing loudly instead of degrading silently — `SelectAttackers` throws when
   the eligible population is too small rather than lowering the ratio, and
   `validate_gate1.py` never emits a PASS report on missing evidence. Both are
   the correct behaviour for an experimental instrument.
4. Explicit RNG stream assignment with a hard error when it is missing.
5. Every equation in the code is annotated with its symbol, domain, and unit.

## 7. Build and test record

### 7.1 Environment

| Item | Value |
|---|---|
| ns-3 source | `ns-3.48` archive from gitlab.com/nsnam/ns-3-dev, `VERSION` = `3.48` |
| Compiler | g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0 |
| CMake / generator | 3.28.3 / Ninja 1.11.1 |
| Standard and flags | `-std=c++23 -Wall -Wpedantic`, `NS3_WARNINGS_AS_ERRORS=OFF` (ns-3 default profile) |
| Installation | `cp -R contrib/mtcaodv <ns3>/contrib/mtcaodv`; `src/aodv` untouched |
| Configure | `./ns3 configure --enable-modules=aodv,mtcaodv,point-to-point --enable-examples --enable-tests` |

Note on the configure line: a first attempt with `--enable-modules=aodv,mtcaodv`
failed, but **not in `mtcaodv`** — ns-3's own `src/aodv/examples/aodv-example.cc`
includes `ns3/point-to-point-module.h`, which that module set excludes. Adding
`point-to-point` resolves it. This is a property of ns-3's example, not of the
contribution, and it is worth recording in the installation instructions
because anyone following the package README's `--enable-examples` step with a
reduced module set will hit it.

### 7.2 Build result

```text
[264/1215] Linking CXX shared library build/lib/libns3.48-mtcaodv-default.so
[265/1215] Linking CXX shared library build/lib/libns3.48-mtcaodv-test-default.so
[266/1215] Linking CXX executable build/contrib/mtcaodv/examples/ns3.48-mtcaodv-gate1-default
```

**Module library, test library, and example all build and link. Zero compiler
errors, zero compiler warnings** across the three `mtcaodv` targets under
`-Wall -Wpedantic`. The `build_lib` / `build_lib_example` declarations, the
`ns3/`-prefixed header exports, and the `TEST_SOURCES` registration are all
correct as written.

### 7.3 Executed behaviour

`build/contrib/mtcaodv/examples/ns3.48-mtcaodv-gate1-default`, invoked directly.

Four mandatory ratios, `--seed=12345 --run=1 --attackStream=73001
--excludeTrafficEndpoints=true`:

```text
r=0.05  count=5   ids=[2,3,24,75,93]
r=0.10  count=10  ids=[2,3,21,24,36,42,47,59,75,93]
r=0.20  count=20  ids=[2,3,13,20,21,24,30,33,34,36,40,42,47,51,55,59,75,80,83,93]
r=0.30  count=30  ids=[2,3,8,13,20,21,24,29,30,33,34,36,39,40,42,47,51,53,55,59,
                       60,67,75,80,81,83,90,93,94,97]
```

| Property | Result |
|---|---|
| Exact counts 5 / 10 / 20 / 30 | Confirmed |
| Endpoints 0 and 99 excluded | Confirmed (`excludedNodeIds":[0,99]`) |
| Identifiers unique and sorted | Confirmed |
| Nesting 0.05 ⊂ 0.10 ⊂ 0.20 ⊂ 0.30 | Confirmed, all three inclusions |
| Replay: identical coordinates twice | Byte-identical manifests |
| Run sensitivity: run 1 / 2 / 3 at r=0.05 | `[2,3,24,75,93]` / `[6,55,85,94,97]` / `[8,42,43,51,69]` — three distinct sets |
| Stream sensitivity: 73001 vs 73002 | `[2,3,24,75,93]` vs `[28,39,53,57,83]` |

Error paths (exit code 2, message on stderr, no manifest emitted):

```text
--attackerRatio=1.5   -> attackerRatio must be finite and within [0, 1]
--attackerRatio=-0.1  -> attackerRatio must be finite and within [0, 1]
--nodeCount=3 --attackerRatio=0.9 -> too few eligible nodes for the requested attacker ratio
```

The R-04 probe:

```text
--excludeTrafficEndpoints=false
  -> "excludedNodeIds":[], "attackerNodeIds":[1,2,24,76,94]
```

This manifest is accepted by `validate_manifest` without complaint, which is
the concrete demonstration of finding R-04.

### 7.4 Conclusion of the build gate

The package's Gate 1B entry criteria are **met for compilation and for
deterministic selection**. Every reproducibility property the README asserts
was executed and holds. The remaining entry criterion — both C++ suites
registered and passing under `./test.py` — requires the full ns-3 test-runner
link and is recorded separately.

The `COMPILATION_FEEDBACK_TEMPLATE.md` round-trip is, for this gate,
unnecessary: the answer is a clean build.
