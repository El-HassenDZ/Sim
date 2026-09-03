# Phase-One Feasibility and Design Report v0.0 — critical review

**Reviewed document:** `MTCAODV_Phase1_Design_Report v0.0` (3 September 2026)
**Position in the record:** upstream of `engineering-blueprint-v0.2.md`. Where the
two disagree, the blueprint is the later decision; several disagreements are
improvements and are noted as such.
**Method:** document analysis, plus verification against the ns-3.48 source and
against measurements taken with the Gate 1B implementation in this repository.

---

## 1. What the document gets right

These are not courtesies; each is a decision that materially reduces risk.

1. **§3.2 "Claims that must not be made"** is the single most valuable page.
   Six candidate novelty claims are explicitly renounced before any code exists.
   Most projects discover those renunciations in peer review.
2. **§2.2** identifies impossible requirements and says so: global finality under
   arbitrary partitions, "30% attackers implies local Byzantine safety",
   independence inferable from node IDs, "human-written code" as a test
   criterion. Naming an unfalsifiable requirement and replacing it is harder
   than satisfying a falsifiable one.
3. **§6.3** refuses to call a product of heuristic factors a probability, and
   requires calibration with reliability diagrams, Brier score and ECE, frozen
   before confirmatory testing. This is the correct treatment.
4. **§8** declines the RREP path-trust extension for the minimum implementation.
   The reasoning is right and is confirmed by the ns-3.48 source.
5. **§12.4** requires attacker-path exposure to be reported, so that an attacker
   that never lay on a selected route is not silently counted as a defensive
   success. Few evaluations do this.
6. **§13** lists blockers as blockers rather than as future work.

---

## 2. Defects in the document as a record

### D-01 — The target ns-3 version is inconsistent within one document
`ns-3.47` appears in §1, §2.1, §9.1, §11 (Gate 0), §13 and reference 12.
`ns-3.48` appears in §1 decision 1 and §2.1. The blueprint later settles on
3.48, which is the version this repository builds against. Since the entire
§9.1 integration strategy depends on which headers are private in which
release, a design record that names two releases is not usable as an
authority. Fix the record; do not rely on the reader knowing which is current.

### D-02 — §2.1's environment statement is stale and was load-bearing
The document states the workspace lacks CMake, Ninja and an ns-3 tree, and
concludes that "source-level API inspection, compilation, unit testing, and
simulation cannot yet be performed locally". That conclusion propagated into
the blueprint (§2.1 "Exact build validation here: Not currently possible") and
into the Gate 1A package's refusal to claim a compilation.

It is false for the environment in which this review was performed: CMake
3.28.3, Ninja 1.11.1 and g++ 13.3 are present, the ns-3.48 archive is
reachable, and both Gate 1A and Gate 1B have been compiled and tested here.
A blocker that is asserted once and then inherited by every downstream document
without re-testing is more expensive than the blocker itself.

### D-03 — §9 places the module in `src/`, which is the wrong tree
`src/mtcaodv/` is the location for upstream ns-3 modules. Contributed modules
belong in `contrib/`, which is what `build_lib` and the documented contributed-
module workflow expect. The blueprint corrects this to `contrib/mtcaodv/` and
the implementation follows the blueprint.

### D-04 — Gate 0's verification command does not exist
§11 Gate 0 step 4 says "Run the official AODV tests". The blueprint makes this
concrete as `./test.py -s aodv`. No such suite exists in ns-3.48; the command
exits 2. The four real suites are `routing-aodv`, `aodv-routing-id-cache`,
`routing-aodv-loopback`, `routing-aodv-regression`.

---

## 3. Defects in the design

### D-05 — The attribution score multiplies two different kinds of quantity
§6.3 defines

```text
r_e = c_obs · p_recv · p_opp · p_link · (1 - p_cong)
```

Four of these five factors describe **whether the neighbour could have
forwarded**. One of them, `c_obs`, describes **whether we could have seen it**.
Multiplying them means that poor observation quality is treated as evidence of
the neighbour's innocence.

Follow the consequence through §6.4. Low `c_obs` shrinks the positive update
`A += c_obs·y_e`; it also shrinks `r_e`, hence `q_e`, hence the negative update
`B += q_e(1-y_e)`, sending the mass to `U` instead. With `A` and `B` both
small and `U` large, §6.5 gives `E = A+B` small and

```text
C = [E/(E+ν)] · [E/(E+U+ε)]  ->  0,
T_direct = C·D + (1-C)·0.5   ->  0.5
```

**A node that is persistently hard to observe is pinned at neutral trust for
the entire run, and the design contains no mechanism that notices this.** An
adversary does not need to defeat the trust model; it needs to occupy a
position where `c_obs` is low. The framework cannot distinguish "innocent" from
"unobserved", and it cannot report that it failed to distinguish them.

This is not a hypothetical parameter regime. Whether `c_obs` is typically 0.9
or 0.3 in the intended scenario is unmeasured, and it is the single number on
which the primary contribution rests.

**Proposal.** Separate the two conditionings explicitly:
`P(malicious | not observed) = P(not forwarded | not observed) ·
P(malicious | not forwarded)`, and carry observation coverage as its own
reported quantity per (observer, neighbour) pair. Then add a coverage floor:
when cumulative coverage for a neighbour stays below a pre-registered
threshold, the pair is reported as **uncovered** rather than as trusted. An
uncovered pair is a measurement failure and must appear in the results as one.

### D-06 — Two incompatible trust representations coexist
§6.5 builds `T_direct`, smooths it into `H_ij`, and §6.6 blends recommendations
into `T_ij`. §6.7 then computes the decision statistic from `α_ij, β_ij`
directly — that is, from the raw Beta posterior, bypassing `C_ij`, the
shrinkage toward 0.5, the historical smoothing and the recommendations
entirely.

If that is intended, then the whole of §6.5–6.6 affects route preference only
and contributes **nothing** to false-positive protection; all of that
protection comes from `q_e` and `E_min`. If it is not intended, §6.7 is wrong.
The document never says which. This must be resolved before calibration,
because it determines what has to be calibrated.

### D-07 — The weighted median can be captured at the design's own attacker ratio
§6.6 aggregates recommendations with a weighted median and weights
`ω_ikj = min(ω_max, C_ik·C_kj·F_age·F_context)`.

The breakdown point of a weighted median is set by the **weight mass** an
adversary controls, not by the count of adversaries. Two properties combine
badly here:

- `C_kj` is *k*'s self-reported confidence about *j*. Node *i* cannot verify
  it. A malicious *k* reports the maximum.
- By D-05, honest nodes in poorly observed positions have low `C_ik`, so their
  weights are small.

At the mandated 30% attacker ratio, 30 attackers each claiming maximal weight
against honest nodes whose weights are depressed by their own uncertainty can
plausibly hold more than half the weight mass. The estimator then reports the
attackers' preferred value while `n_eff` still looks healthy, because `n_eff`
measures weight concentration, not independence.

**Proposal.** State and check the condition the design actually needs:
`Σ ω_honest > Σ ω_malicious` under the design's own attacker ratio, with a
per-reporter weight cap chosen to make it hold. Then test it directly at 30%
rather than assuming it.

### D-08 — The ablation ladder cannot support the causal claim it is built for
§3.1 item 5 names a "causal-evaluation gap" and §12.2 answers it with a chain:

```text
A (stock) -> B (detector) -> C0 (detector + Beta) -> C (detector + MOBeta) -> D (full)
```

`C − C0` isolates MOBeta given no ledger. `D − C` isolates the ledger given
MOBeta. **The design contains no cell that isolates the ledger given
conventional Beta**, so any interaction between the two contributions is
unidentifiable. If PTMB helps more with MOBeta than it would with plain Beta —
which is exactly what the coupling argument in §4.2 implies — the experiment
cannot show it, and cannot rule out the opposite.

A document whose stated methodological contribution is causal decomposition
should use the factorial, not the chain. Adding the missing cell
(`C0 + PTMB`) makes it a 2×2 in {Beta, MOBeta} × {no ledger, ledger} plus the
two reference arms: six configurations, 720 confirmatory runs per mobility
regime instead of 600. A 20% increase in compute buys the identifiability of
the claim the paper is about.

### D-09 — There is no scenario-validity criterion, and the scenario needs one
§12 pre-registers PDR as the primary outcome, 100 nodes, four attacker ratios,
30 paired seeds, and three mobility regimes. It never states what the no-attack
baseline must look like for those numbers to mean anything.

Measured with the Gate 1B implementation in this repository (ns-3.48, 100
nodes, 802.11b ad hoc, RandomWaypoint, `evidence/gate1b/`):

| Area | Baseline PDR, no attackers |
|---|---:|
| 1500 × 300 m | 0.052 |
| 800 × 800 m | 0.062 |
| 500 × 500 m | 0.476 |
| 300 × 300 m | 0.292 |

and, at 500 × 500 m with a single flow, across five seeds:
**0.787, 0.000, 0.009, 0.805, 0.207**.

Two findings follow.

1. **The baseline is bimodal, not merely low.** Whether a source–destination
   pair has a usable multi-hop path at all is close to a coin flip per seed.
   The dominant variance source in this scenario is connectivity, not the
   attack. Reducing offered load does not fix it (10 flows at 0.5 kbps still
   gives 0.34), so this is a connectivity regime, not MAC congestion.
2. **A low baseline caps the measurable effect.** With a baseline near 0.35,
   no defence can demonstrate more than 0.35 of recovery, and the report's
   primary outcome is being measured against a ceiling set by the radio
   scenario rather than by the security mechanism.

The design's paired structure is the right answer to point 1 — pairing by seed
removes the shared connectivity draw from the difference — and that is an
argument *for* §12.1's pairing, not against it. But point 2 is not addressed by
pairing, and nothing in §12 would have caught either.

**Proposal.** Add a pre-registered scenario-validity precondition, checked and
reported before any attacked run executes: *the mean no-attack baseline PDR
shall be at least 0.85 with a 95% confidence interval no wider than 0.05.*
Report the baseline distribution, not only its mean. This is cheap, it is a
Gate 0 activity, and it prevents the entire confirmatory campaign from being
run on an unusable operating point.

Note that this cannot be settled until §13 item 3 — the application profile —
is decided, because transmission range, density and speed are all downstream of
it. That decision is now on the critical path for the experimental design, not
only for the mobility parameters.

### D-10 — Compute and storage are never budgeted
§12.2's arithmetic is correct: 5 × 4 × 30 = 600 runs per mobility regime, 1800
across three. The document never estimates what that costs. A single
60-simulated-second, 100-node run of the Gate 1B scenario takes on the order of
tens of seconds of wall clock on one core in this environment; 1800 runs is
therefore a matter of core-hours rather than core-weeks, and is entirely
tractable — but adding the D-08 cell raises it to 2160, and the calibration and
split/merge campaigns are excluded from both figures. State the budget, since
Gate 5's exit criterion requires every raw run to be retained.

### D-11 — `MacPromiscRx` cannot serve as the observation backend
§13 item 4 keeps "an explicit receipt-based fallback ... if ns-3 trace
visibility is insufficient" as an open decision. It is decidable now, and the
answer is documented with line references in `ns3.48-api-verification.md` §2:
the `MacPromiscRx` trace carries `Ptr<const Packet>` and nothing else — no
transmitter, no receiver, no packet type — and does not fire at all unless a
promiscuous receive callback is already installed.

The receipt-based fallback is nevertheless **not** required, because
`Node::RegisterProtocolHandler(handler, protocol, device, promiscuous = true)`
supplies `from`, `to` and `PacketType`, and in IBSS mode `from` is the
immediate transmitter. Close §13 item 4 in favour of promiscuous protocol-
handler registration, and drop the receipt backend from the plan.

---

## 4. The contribution claim, assessed honestly

§4.1 proposes OCEA as the primary contribution and §3 concedes that Khan et
al. (2017) "directly anticipates mobility/congestion-aware loss attribution".
The stated delta is: calibrated uncertainty, posterior-confidence decisions,
AODV integration, multiple attacker ratios, and ledger ablation.

Each element of that delta is a standard technique — isotonic regression, a
Beta posterior tail probability, a paired ablation design. **The contribution
is therefore a systems-integration and evaluation-rigour contribution, not a
new mechanism.** §14's recommended claim is honest about this and is the right
framing.

The strategic consequence should be stated plainly, because it affects where
the work is submitted and how much of it is worth building: a composition of
known mechanisms, however carefully evaluated, is a weak novelty claim in a
crowded field, and §3's own matrix shows the field is crowded. The defensible
strength of this work is the *evaluation*: paired multi-ratio ablations with
pre-registered thresholds, calibration diagnostics, and explicit uncertainty
accounting are rare in this literature and are what a reviewer will find hard
to dismiss.

That suggests an allocation of effort opposite to the current gate order:
the evaluation apparatus and the observation-coverage measurement are the
assets, and PTMB — the most expensive component to build and the one §3's
matrix shows to be most thoroughly anticipated (Blockgraph, C4M, Lwin et al.,
Kudva et al.) — carries the least novelty per unit of effort. If any component
has to be cut for time, PTMB is the candidate, and §12.2's ablation ladder
already provides the language for reporting a result without it (configuration
C is a complete study on its own).

---

## 5. Priority of the recommendations

| Rank | Item | Why first |
|---:|---|---|
| 1 | D-05: separate `c_obs` from the ability factors; measure observation coverage | Determines whether the primary contribution has an input signal at all |
| 2 | D-09: pre-register a baseline-validity criterion; re-parameterise the scenario | The current operating point makes the primary outcome unmeasurable |
| 3 | D-06: state which trust representation drives the decision | Determines what must be calibrated, before calibration is frozen |
| 4 | D-08: add the missing ablation cell | The causal claim is otherwise unidentifiable, and the fix costs 20% compute |
| 5 | D-07: state and test the weight-mass condition at 30% | A capture attack the design currently permits |
| 6 | D-11, D-04, D-01, D-03 | Record corrections; cheap, and each is currently propagating downstream |
