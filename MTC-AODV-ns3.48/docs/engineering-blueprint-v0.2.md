# MTC-AODV — ns-3.48 Engineering Blueprint v0.2

**Target:** ns-3.48 (`contrib/mtcaodv/`)  
**Languages:** C++17 inside ns-3; Python 3 for experiment control and analysis  
**Status:** pre-implementation architecture gate  
**Date:** 3 September 2026

## 1. Executive decision

The proposed contribution is implementable as a research prototype, but only if three boundaries are enforced.

1. **The stock AODV module remains unchanged.** Configuration A uses `ns3::aodv::RoutingProtocol` from `src/aodv`. Configurations B–D use a separate protocol in `contrib/mtcaodv`.
2. **The secure protocol cannot be implemented reliably as a thin subclass of stock AODV.** In ns-3.48, `RouteOutput`, `RouteInput`, interface notifications, `SetIpv4`, `PrintRoutingTable`, and `AssignStreams` are public, but the control-plane handlers `RecvAodv`, `RecvRequest`, `RecvReply`, `Forwarding`, `SendRequest`, and `SendReply` are private. The safe strategy is therefore to derive a new GPL-compatible implementation from the exact ns-3.48 AODV routing-protocol source, retain the upstream copyright and SPDX notice, rename it, and place all modifications in `contrib/mtcaodv`.
3. **PTMB provides conditional, partition-local certification, not unconditional global consensus.** Evidence remains `PENDING` when a suitable local committee or quorum is unavailable. Independently valid partition histories are reconciled by record digest; a longest-chain rule is not used.

The software will be developed in gates. No component will be described as compiled or validated until it has been built and tested against the researcher's exact ns-3.48 tree.

## 2. Critical feasibility analysis

### 2.1 Component assessment

| Component | Feasibility | Engineering decision | Main validation risk |
|---|---:|---|---|
| Stock AODV baseline | High | Use `src/aodv` without modification and archive its hash | Configuration drift between A and B–D |
| Simultaneous Blackholes | High | Install an AODV-compatible malicious protocol on a deterministic node subset | Attack behavior must be byte-for-byte equivalent across paired variants |
| Robust RREP screening | High | Instrument the copied `RecvReply` path; suspicion never excludes a route | Sequence-number wraparound and small comparison sets |
| Bounded RREP comparison window | Medium | Defer source-node queue release only; preserve intermediate RREP forwarding | Additional route-discovery delay must be measured |
| Forwarding observation | Medium | Use locally observable Wi-Fi promiscuous reception and explicit packet fingerprints | `MacPromiscRx` packet visibility and transmitter attribution require an ns-3.48 integration spike |
| Opportunity-conditioned evidence | High | Pure deterministic C++ component with bounded inputs and mass-conservation tests | Calibration and missing-feature semantics |
| Discounted Beta trust | High | Pure C++ numerical core invoked by ns-3 events | Stable Beta CDF/quantile implementation without hidden dependencies |
| Certificate-gated quarantine | Medium | Temporary evidence-specific committee and expiring certificate | Global attacker ratio does not establish the local Byzantine bound |
| PTMB | High as a simulator model | Bounded evidence-only replicated log | It is not a production cryptographic blockchain |
| Partition reconciliation | High | Digest-set union, duplicate rejection, conflict-to-pending transition | Conflict policy must not manufacture global finality |
| Energy and overhead accounting | High | Count all control bytes and model crypto delay/energy explicitly | Double counting across Wi-Fi, routing, and security traces |
| Exact build validation here | Not currently possible | Perform source design here; compile on the validated ns-3.48 PC | This workspace has no ns-3.48 tree or CMake executable |

### 2.2 Verified ns-3.48 constraints

- The official ns-3 AODV model is IPv4 and implements AODV through `ns3::aodv::RoutingProtocol`, which inherits from `Ipv4RoutingProtocol`.
- The model's public routing boundary is `RouteOutput` for locally originated packets and `RouteInput` for received or forwarded IPv4 packets.
- Native AODV does not implement RREQ, RREP, or HELLO extensions. A path-trust field would therefore require a new packet format and must not be part of the minimum implementation.
- AODV uses UDP port 654 for control traffic.
- ns-3.48's AODV control handlers and forwarding method are private. Simple inheritance cannot intercept the required security events.
- `AodvHelper` uses an `ObjectFactory`, implements `Copy()`, `Create()`, `Set()`, and `AssignStreams()`. `MtcAodvHelper` should follow the same public pattern.
- The ns-3 CMake build supports contributed modules through `build_lib`; examples use `build_lib_example`.
- `WifiMac` exposes the supported `MacPromiscRx` trace. It provides a promising observation source, but a compile-time prototype must verify which packet headers and sender metadata remain visible in the selected Wi-Fi configuration.

### 2.3 Assumptions that must remain explicit

1. Nodes have unique pre-provisioned identities and uncompromised keys.
2. The detector does not know the ground-truth attacker set.
3. Sybil, wormhole, jamming, key compromise, and selective Grayhole behavior are outside the primary claim.
4. Passive non-observation is ambiguous and cannot be converted into one full malicious failure.
5. A global attacker ratio below one third does not prove that a temporary local committee contains fewer than one-third Byzantine members.
6. A simulated signature provider models authentication, byte size, delay, and energy; it is not itself production cryptography.
7. Routing must remain available when PTMB has no quorum or is temporarily unavailable.

## 3. Architecture

### 3.1 Layered view

| Layer | Responsibility | Principal components |
|---|---|---|
| Experiment layer | Construct paired scenarios and preserve ground-truth separation | `ExperimentConfiguration`, `AttackManager`, Python runner |
| AODV-compatible routing layer | Preserve AODV routing invariants and expose explicit security hooks | `MtcAodvRoutingProtocol`, `BlackholeAodvRoutingProtocol` |
| Local observation layer | Screen RREPs and observe next-hop forwarding opportunities | `RrepAnomalyDetector`, `ForwardingObserver`, `PacketFingerprint` |
| Evidence and trust layer | Convert observations into evidence masses and update MOBeta | `EvidenceAttributor`, `MobetaTrustManager`, `TrustRecord`, `BetaDistributionMath` |
| Decision layer | Keep suspicion, accusation, quarantine, and rehabilitation distinct | `SecurityStateMachine`, `EvidenceManager` |
| Distributed certification layer | Validate independent reports without permanent validators | `WitnessAggregator`, `ValidationCommittee`, `CertificateValidator` |
| PTMB layer | Store bounded security records and reconcile partitions | `PtmbLedger`, `MicroBlock`, `ReconciliationManager` |
| Security control plane | Serialize and exchange evidence, votes, and certificates | `SecurityControlProtocol`, typed headers |
| Measurement layer | Record network, security, ledger, timing, storage, and energy costs | `MetricsCollector`, structured CSV writers |

### 3.2 Routing-class strategy

`MtcAodvRoutingProtocol` will be a separately named AODV-compatible implementation derived from the exact ns-3.48 source, not a subclass of `ns3::aodv::RoutingProtocol`. It may reuse exported AODV support types such as packet headers and routing-table classes where their public APIs permit. The adapted file must retain the upstream GPL-2.0-only notice and document every divergence from ns-3.48.

Security hooks are placed at four controlled points:

1. after parsing a protocol-valid RREP and before source-node route commitment;
2. when a data packet is handed to a next hop and an observation window is created;
3. before a candidate route is returned or used for forwarding;
4. when link failure, route error, queue pressure, or neighbor disappearance provides benign-loss context.

The default hook behavior must reproduce unmodified AODV semantics. This creates a regression-testable bridge between the stock baseline and variants B–D.

### 3.3 Security states

```text
NORMAL -> WATCH -> ACCUSED -> QUARANTINED -> REHABILITATING
   ^         |          |            |              |
   |         +----------+------------+--------------+
   +---------------- certificate expiry and sufficient new evidence
```

- `WATCH` increases observation priority but does not invalidate a route.
- `ACCUSED` means that local posterior conditions are met and witness reports may be requested.
- `QUARANTINED` requires a valid, unexpired certificate.
- `REHABILITATING` restores eligibility gradually after expiry or revocation.

### 3.4 Distributed certification

For accused node `j` and validation epoch `e`, candidate witnesses are reachable, authenticated, non-quarantined nodes with fresh, nonduplicate evidence. Committee selection is deterministic from a common input tuple such as `(evidenceRoot, epochId, checkpointDigest, candidateId)`, preventing the accuser from choosing all validators alone.

For committee size `m_c` and design fault bound `f_c`:

```text
m_c >= 3 f_c + 1
q_c = m_c - f_c
```

At the minimum committee size, `q_c = 2 f_c + 1`. The next manuscript revision should express Equation (16) using `q_c` so that the code and the general quorum rule remain identical.

The runtime enforces configured quorum and context-diversity rules. Simulation ground truth is used only after the run to report whether the realized committee actually respected its assumed Byzantine bound.

### 3.5 PTMB semantics

PTMB is not consulted for every packet. It stores only typed, security-critical records:

```text
EVIDENCE_DIGEST
ACCUSATION_SUMMARY
COMMITTEE_MEMBERSHIP
BOUND_VOTE
QUARANTINE_CERTIFICATE
REVOCATION
REHABILITATION
CHECKPOINT
```

Every record has a canonical serialization, unique identifier, issuer, creation time, expiry time, payload digest, and simulated signature metadata. Duplicate identifiers are idempotent. Expired records cannot authorize routing decisions. Conflicting active certificates move the accused node to a locally `CONTESTED/PENDING` decision until fresh corroboration is available.

## 4. Proposed directory tree

```text
MTC-AODV-ns3.48/
|-- README.md
|-- CHANGELOG.md
|-- AUTHORS.md
|-- pyproject.toml
|-- contrib/
|   `-- mtcaodv/
|       |-- CMakeLists.txt
|       |-- doc/
|       |   |-- mtcaodv.rst
|       |   |-- architecture.md
|       |   |-- equation-code-map.md
|       |   `-- upstream-aodv-differences.md
|       |-- helper/
|       |   |-- mtc-aodv-helper.h
|       |   |-- mtc-aodv-helper.cc
|       |   |-- attack-manager.h
|       |   |-- attack-manager.cc
|       |   |-- experiment-configuration.h
|       |   `-- experiment-configuration.cc
|       |-- model/
|       |   |-- mtc-aodv-routing-protocol.h
|       |   |-- mtc-aodv-routing-protocol.cc
|       |   |-- blackhole-aodv-routing-protocol.h
|       |   |-- blackhole-aodv-routing-protocol.cc
|       |   |-- attack-behavior.h
|       |   |-- blackhole-behavior.h
|       |   |-- blackhole-behavior.cc
|       |   |-- rrep-anomaly-detector.h
|       |   |-- rrep-anomaly-detector.cc
|       |   |-- rrep-candidate-buffer.h
|       |   |-- rrep-candidate-buffer.cc
|       |   |-- forwarding-observer.h
|       |   |-- forwarding-observer.cc
|       |   |-- packet-fingerprint.h
|       |   |-- packet-fingerprint.cc
|       |   |-- evidence-mass.h
|       |   |-- evidence-attributor.h
|       |   |-- evidence-attributor.cc
|       |   |-- beta-distribution-math.h
|       |   |-- beta-distribution-math.cc
|       |   |-- trust-record.h
|       |   |-- trust-record.cc
|       |   |-- mobeta-trust-manager.h
|       |   |-- mobeta-trust-manager.cc
|       |   |-- security-state-machine.h
|       |   |-- security-state-machine.cc
|       |   |-- evidence-record.h
|       |   |-- evidence-record.cc
|       |   |-- evidence-manager.h
|       |   |-- evidence-manager.cc
|       |   |-- witness-report.h
|       |   |-- witness-aggregator.h
|       |   |-- witness-aggregator.cc
|       |   |-- validation-committee.h
|       |   |-- validation-committee.cc
|       |   |-- quarantine-certificate.h
|       |   |-- quarantine-certificate.cc
|       |   |-- certificate-validator.h
|       |   |-- certificate-validator.cc
|       |   |-- ledger-record.h
|       |   |-- ledger-record.cc
|       |   |-- micro-block.h
|       |   |-- micro-block.cc
|       |   |-- ptmb-ledger.h
|       |   |-- ptmb-ledger.cc
|       |   |-- reconciliation-manager.h
|       |   |-- reconciliation-manager.cc
|       |   |-- crypto-provider.h
|       |   |-- simulated-crypto-provider.h
|       |   |-- simulated-crypto-provider.cc
|       |   |-- security-control-header.h
|       |   |-- security-control-header.cc
|       |   |-- security-control-protocol.h
|       |   |-- security-control-protocol.cc
|       |   |-- security-parameters.h
|       |   |-- security-types.h
|       |   |-- metrics-collector.h
|       |   `-- metrics-collector.cc
|       |-- examples/
|       |   |-- CMakeLists.txt
|       |   |-- mtcaodv-comparison.cc
|       |   `-- mtcaodv-split-merge.cc
|       `-- test/
|           |-- attack-manager-test-suite.cc
|           |-- rrep-detector-test-suite.cc
|           |-- evidence-attributor-test-suite.cc
|           |-- mobeta-trust-test-suite.cc
|           |-- state-machine-test-suite.cc
|           |-- certificate-test-suite.cc
|           |-- ptmb-ledger-test-suite.cc
|           |-- reconciliation-test-suite.cc
|           |-- routing-regression-test-suite.cc
|           `-- reproducibility-test-suite.cc
|-- experiments/
|   |-- configs/
|   |   |-- pilot.json
|   |   |-- calibration.json
|   |   |-- confirmatory.json
|   |   `-- split-merge.json
|   |-- schemas/
|   |   |-- configuration.schema.json
|   |   |-- run-manifest.schema.json
|   |   `-- metrics.schema.json
|   |-- mtcaodv_experiments/
|   |   |-- __init__.py
|   |   |-- configuration.py
|   |   |-- campaign.py
|   |   |-- manifest.py
|   |   |-- validation.py
|   |   |-- aggregation.py
|   |   |-- statistics.py
|   |   `-- plotting.py
|   |-- run_campaign.py
|   |-- validate_runs.py
|   |-- analyze_results.py
|   `-- plot_results.py
`-- docs/
    |-- assumptions.md
    |-- traceability-matrix.md
    |-- validation-protocol.md
    |-- limitations.md
    `-- compilation-error-log.md
```

## 5. File responsibilities

| File or group | Responsibility |
|---|---|
| `mtc-aodv-routing-protocol.*` | AODV-compatible routing, explicit detector hooks, certificate route gate, trace sources, random-stream assignment |
| `blackhole-aodv-routing-protocol.*` | AODV-interoperable malicious protocol that forges attractive RREPs and drops configured transit traffic |
| `attack-behavior.h` | Extensible abstract policy for future attack models without changing routing logic |
| `rrep-anomaly-detector.*` | Robust sequence, hop-count, and response-time scores; no direct quarantine authority |
| `rrep-candidate-buffer.*` | Bounded candidate window at the source; records its added discovery delay |
| `forwarding-observer.*` | Opens observation windows, consumes only locally observable frames/context, classifies timeout reasons |
| `evidence-attributor.*` | Implements Equations (6)–(10), missing-feature coverage, mass conservation, numerical bounds |
| `beta-distribution-math.*` | Regularized incomplete beta and inverse quantile with deterministic convergence and SciPy reference tests |
| `trust-record.*` | Per-observer/per-neighbor Beta parameters, auxiliary masses, timestamps, counts, and state |
| `mobeta-trust-manager.*` | Implements decay, posterior mean, credible confidence, and accusation criteria |
| `security-state-machine.*` | Enforces legal transitions and hysteresis independently of the numerical score |
| `evidence-manager.*` | Deduplication, expiry, report creation, and bounded local evidence store |
| `witness-aggregator.*` | Reporter weighting, context diversity, capped support, and duplicate suppression |
| `validation-committee.*` | Deterministic temporary committee membership and one-bound-vote rule |
| `certificate-validator.*` | Quorum, context, expiry, epoch, and signature conditions for certificates |
| `ptmb-ledger.*` | Bounded append, checkpoint, prune, lookup, certificate activity, and memory accounting |
| `reconciliation-manager.*` | Header-first digest exchange, missing-record retrieval, idempotent merge, conflict handling |
| `simulated-crypto-provider.*` | Explicit authentication abstraction with modeled signature bytes, delay, verification count, and energy |
| `security-control-protocol.*` | Separate bounded UDP dissemination of evidence, votes, certificates, and reconciliation messages |
| `metrics-collector.*` | Stable event/summary schemas and complete network/security/PTMB overhead accounting |
| `experiment-configuration.*` | Validated ns-3 Attributes and cross-parameter assertions |
| `attack-manager.*` | Deterministic selection without replacement; exact attacker-count assertion and manifest export |
| Python package | Campaign generation, immutable manifests, run validation, paired aggregation, inference, and plots |

## 6. Equation-to-code traceability

| Eq. | Mathematical object | C++ owner | Principal method | Required test | Primary metric |
|---:|---|---|---|---|---|
| (1) | `G(t)=(V,E(t))` | Scenario + `ForwardingObserver` | `IsObservationLocallyAvailable()` | controlled link break | contact/coverage rate |
| (2) | `n_A=round(r_a N)` | `AttackManager` | `ComputeAttackerCount()` | 5/10/20/30 exact counts | actual attacker count |
| (3) | sequence anomaly `z_s` | `RrepAnomalyDetector` | `ComputeSequenceAnomaly()` | normal/wraparound/forged RREP | RREP TPR/FPR |
| (4) | hop anomaly `z_h` | `RrepAnomalyDetector` | `ComputeHopAnomaly()` | ordinary and short routes | RREP TPR/FPR |
| (5) | `A_RREP` | `RrepAnomalyDetector` | `ComputeAnomalyScore()` | weighted-score oracle | WATCH precision |
| (6) | opportunity coverage `c_e` | `EvidenceAttributor` | `ComputeOpportunityCoverage()` | missing-feature cases | coverage calibration |
| (7) | forwarding opportunity `O_e` | `EvidenceAttributor` | `ComputeForwardingOpportunity()` | geometric-mean oracle | Brier/ECE after calibration |
| (8) | `d_e`, benign likelihood `Bbar_e` | `EvidenceAttributor` | `ComputeBenignLossAssessment()` | congestion/link-break cases | benign attribution rate |
| (9) | positive evidence tuple | `EvidenceAttributor` | `AttributeObservedForwarding()` | exact `(1,0,0,0)` | mass error |
| (10) | non-observation tuple | `EvidenceAttributor` | `AttributeMissingForwarding()` | numerical example + edge cases | mass error/FPR |
| (11) | discounted `alpha_ij` | `MobetaTrustManager` | `ApplyPositiveEvidence()` | decay-to-prior oracle | trust calibration |
| (12) | discounted `beta_ij` | `MobetaTrustManager` | `ApplyMaliciousEvidence()` | fractional update oracle | trust calibration |
| (13) | posterior mean and confidence | `MobetaTrustManager`, `BetaDistributionMath` | `ComputePosteriorSummary()` | SciPy reference grid | max numerical error |
| (14) | accusation tail probability | `MobetaTrustManager` | `ShouldAccuse()` | insufficient/sufficient evidence | false accusation |
| (15) | witness weights and `Phi_j` | `WitnessAggregator` | `ComputeWeightedSupport()` | duplicates/collusion caps | support shift |
| (16) | certificate rule | `CertificateValidator` | `TryCreateCertificate()` | quorum/context/fault-bound cases | invalid quarantine |
| (17) | microblock tuple | `MicroBlock` | `SerializeCanonical()`, `ComputeDigest()` | stable serialization/integrity | block bytes |
| (18) | PTMB bounds | `PtmbLedger` | `Append()`, `PruneExpired()`, `Checkpoint()` | size/TTL/budget limits | max ledger bytes |
| (19) | admissible-route order | `MtcAodvRoutingProtocol` | `SelectAdmissibleRoute()` | certificate gate/AODV regression | route availability/PDR |
| (20) | PDR | `MetricsCollector` | `ComputePacketDeliveryRatio()` | counter fixture | PDR |
| (21) | false-quarantine rate | `MetricsCollector` | `ComputeFalseQuarantineRate()` | ground-truth fixture | FPR |
| (22) | security overhead | `MetricsCollector` | `ComputeSecurityOverhead()` | byte-accounting fixture | security bytes/delivered byte |

### 6.1 Naming rules for mathematical variables

| Symbol | C++ name | Unit/range |
|---|---|---|
| `O_e` | `forwardingOpportunity` | dimensionless `[0,1]` |
| `d_e` | `diagnosticCoverage` | dimensionless `[0,1]` |
| `Bbar_e` | `benignLossLikelihood` | dimensionless `[0,1]` |
| `I_e` | `interpretableEvidence` | dimensionless `[0,1]` |
| `g_e` | `positiveEvidenceMass` | dimensionless `[0,1]` |
| `m_e` | `maliciousEvidenceMass` | dimensionless `[0,1]` |
| `b_e` | `benignLossMass` | dimensionless `[0,1]` |
| `u_e` | `uncertainEvidenceMass` | dimensionless `[0,1]` |
| `alpha_ij` | `positiveShape` | positive pseudo-count |
| `beta_ij` | `negativeShape` | positive pseudo-count |
| `mu_ij` | `posteriorMean` | dimensionless `[0,1]` |
| `C_ij` | `posteriorConfidence` | dimensionless `[0,1]` |
| `Phi_j` | `weightedMaliciousSupport` | dimensionless `[0,1]` |
| `m_c` | `committeeSize` | nodes |
| `f_c` | `designedByzantineBound` | nodes |
| `q_c` | `requiredVoteCount` | votes |
| `S_max` | `maximumMicroBlockBytes` | bytes |
| `B_max` | `maximumRetainedBlockCount` | blocks |

## 7. Implementation and validation plan

### Gate 0 — Exact environment and baseline

On the validation PC:

```bash
cd /path/to/ns-3.48
git describe --tags --always --dirty
git rev-parse HEAD
g++ --version
cmake --version
./ns3 configure --enable-examples --enable-tests
./ns3 build
./test.py -s aodv
```

Archive command output and hash `src/aodv`. The prior statement that ns-3.48 was tested is useful, but the project requires a reproducible environment record and a fresh AODV baseline result.

**Exit criterion:** clean AODV tests pass and the exact source/build manifest is retained.

### Gate 1 — Module shell and AODV-compatible attack

1. Create `contrib/mtcaodv/CMakeLists.txt` with `build_lib`.
2. Import the exact routing logic required from ns-3.48 under renamed classes and preserve upstream licensing.
3. Implement `MtcAodvHelper::Copy`, `Create`, `Set`, and `AssignStreams` following the verified AODV helper contract.
4. Implement deterministic attacker selection without replacement.
5. Implement forged attractive RREP and full transit-data dropping while preserving control participation.
6. Assert exact counts for all mandatory ratios.

**Exit criterion:** the attack is reproducible and the same attacker IDs/times are used in paired A–D runs.

### Gate 2 — RREP detector and forwarding-observation spike

1. Add explicit trace sources at RREP parse, route commitment, forwarding, and security-state transition.
2. Implement robust anomaly scores using protocol-aware sequence comparisons.
3. Prototype locally observable forwarding matching through Wi-Fi promiscuous reception.
4. Verify packet fingerprint and transmitter attribution in ns-3.48.
5. If the required headers are not observable, introduce an explicit bounded observation header and count every added byte; do not use a hidden global trace as detector input.

**Exit criterion:** normal RREP, forged RREP, observed forwarding, legitimate link disappearance, and timeout cases are unit/integration tested.

### Gate 3 — EvidenceAttributor and MOBeta core

1. Implement typed inputs whose every probability is range-checked.
2. Implement Equations (6)–(10) and assert mass conservation within numerical tolerance.
3. Implement decay toward the prior, not toward zero.
4. Implement Beta CDF and quantile numerical functions with convergence bounds.
5. Generate an independent SciPy reference grid in Python and test maximum absolute error.
6. Implement `WATCH` and `ACCUSED`; no distributed quarantine yet.

**Exit criterion:** all mathematical tests pass and mobility/congestion examples produce benign or uncertain mass rather than automatic guilt.

### Gate 4 — Certificates and PTMB

1. Implement canonical typed records and replay protection.
2. Implement simulated signature size, delay, verification, and energy accounting.
3. Implement reporter weights, duplicate caps, context diversity, committee selection, one-bound-vote, and quorum.
4. Implement expiring certificates and legal state transitions.
5. Implement bounded blocks, checkpoints, pruning, and memory accounting.
6. Implement split/local decision/merge/conflict scenarios.

**Exit criterion:** insufficient quorum never quarantines; valid quorum can issue only a temporary certificate; conflicting certificates remain auditable and pending.

### Gate 5 — Routing enforcement and regression

1. Reject a route only when an applicable certificate is valid and unexpired.
2. Preserve AODV freshness and lifetime rules.
3. Use trust only as the defined bounded tie-breaker.
4. Verify that routing continues when certification is unavailable.
5. Compare security-disabled custom routing against stock AODV on fixed scenarios.

**Exit criterion:** no exclusion from suspicion alone, no observed loop in the test suite, and no unexplained regression relative to stock AODV.

### Gate 6 — Python campaign and confirmatory analysis

1. Generate immutable configurations for A, B, C0, C, and D.
2. Pair position, mobility, traffic, attacker, and activation streams by seed.
3. Run pilot/calibration data separately.
4. Freeze all thresholds before confirmatory execution.
5. Use at least 30 paired seeds per primary condition.
6. Validate every manifest and raw CSV before aggregation.
7. Report paired effects, 95% intervals, effect sizes, Holm-adjusted families, and protocol-by-attacker-ratio interactions.

**Exit criterion:** every table and figure can be regenerated from immutable raw outputs; no missing or nonapplicable metric is converted to zero.

## 8. Optimization plan

Optimization is permitted only after correctness instrumentation exists.

| Hot path | Initial bound | Intended optimization | Invariant |
|---|---:|---|---|
| Evidence attribution | `O(K+L)` per observation | Fixed-size feature arrays | Same mass tuple within tolerance |
| Trust lookup | expected `O(1)` | `unordered_map<NodeId, TrustRecord>` with expiry | No loss of active record |
| RREP robust statistics | `O(R log R)` per window | Bounded `R`; preallocated scratch buffer | Same median/MAD |
| Witness validation | `O(W log W)` | Bounded witness count; digest dedup set | One report per event/identity/context cap |
| Committee ranking | `O(C log C)` | Partial selection for fixed committee size | Deterministic membership |
| Ledger append | expected `O(1)` lookup plus bounded prune | Digest index and expiry queue | No active certificate pruned |
| Reconciliation | `O(M)` in missing digests | Header-first exchange and bounded batches | Idempotent union |

Memory, transmitted bytes, simulated CPU delay, and energy are first-class outputs. An optimization that reduces overhead but changes evidence attribution or certificate safety is rejected.

## 9. Immediate blockers and required feedback loop

The next coding gate can begin in this workspace for pure C++ mathematical components, but ns-3 integration cannot honestly be reported as compiled here. For each integration increment, the validation PC must return:

1. the complete command;
2. compiler standard output and standard error;
3. exit status;
4. ns-3 tag/commit;
5. the changed-file hash;
6. the relevant test output.

Compilation errors will be corrected from their complete diagnostics, not guessed from partial screenshots.

## 10. Source references used for the API gate

1. ns-3 Project, **AODV Model Library**, current documentation: <https://www.nsnam.org/docs/models/html/aodv.html>.
2. ns-3 Project, **ns-3.48 release tag**, 2 June 2026: <https://gitlab.com/nsnam/ns-3-dev/-/tags/ns-3.48>.
3. ns-3.48, **AODV routing protocol header**: <https://gitlab.com/nsnam/ns-3-dev/-/blob/ns-3.48/src/aodv/model/aodv-routing-protocol.h>.
4. ns-3.48, **AODV routing protocol implementation**: <https://gitlab.com/nsnam/ns-3-dev/-/blob/ns-3.48/src/aodv/model/aodv-routing-protocol.cc>.
5. ns-3.48, **AODV helper interface**: <https://gitlab.com/nsnam/ns-3-dev/-/blob/ns-3.48/src/aodv/helper/aodv-helper.h>.
6. ns-3.48, **AODV module CMakeLists**: <https://gitlab.com/nsnam/ns-3-dev/-/blob/ns-3.48/src/aodv/CMakeLists.txt>.
7. ns-3 Project, **Working with CMake — adding modules and contributed modules**: <https://www.nsnam.org/docs/manual/html/working-with-cmake.html>.
8. ns-3 Project, **WifiMac API and MacPromiscRx trace source**: <https://www.nsnam.org/doxygen/d3/d57/classns3_1_1_wifi_mac.html>.

