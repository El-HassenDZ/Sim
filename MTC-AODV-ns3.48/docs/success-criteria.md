# Success criteria — recorded objectives and an honest status check

**Trigger:** the user stated the project's final objective as: PDR > 95%,
higher throughput, lower delay, no routing overhead, lower energy consumption,
and very fast attack detection and isolation.

This file records that statement precisely, translates it into the
terminology already used by `engineering-blueprint-v0.2.md` (Equations
19–22) and the Phase-One report (§12.6), and states plainly what is and is
not true about it today. Recording it without the honest part would be more
comfortable and less useful.

## 1. The six objectives, restated operationally

| # | Stated objective | Existing metric | Blueprint reference |
|---|---|---|---|
| 1 | PDR > 95% | Packet delivery ratio | Eq (20) |
| 2 | Higher throughput | Delivered bytes/second | secondary network outcome |
| 3 | Lower delay | End-to-end latency | secondary network outcome |
| 4 | No routing overhead | Normalized routing overhead (control packets/delivered packet) | secondary network outcome |
| 5 | Lower energy consumption | Joules consumed (radio + simulated crypto) | Eq accounting in §7.7 / §8 |
| 6 | Very fast detection and isolation | Detection-to-certificate latency | primary response outcome, Phase-1 §12.6 |

Each metric already exists in the design. Nothing here is new work to define.
What is new is stating them as six simultaneous targets to be maximized or
minimized together, which the design documents never did.

## 2. Where this statement is stronger than the design, and why that matters

The Phase-One report §12.6 pre-registers PDR, false-quarantine rate,
detection-to-certificate latency, and security overhead as outcomes — but as
**paired differences**: stock AODV under attack versus full MTC-AODV under the
*identical* attack, at each attacker ratio, aggregated over ≥30 seeds. It
never commits to an absolute floor like "PDR > 95%".

That was a deliberate choice, not an oversight. An absolute floor conflates
two different questions:

- *Does the defense recover delivery that the attack would otherwise destroy?*
  — the paired-difference question, robust to how hard the underlying scenario
  is.
- *Is the network, independent of any attack, capable of delivering 95% of
  traffic at all?* — a question about topology, density, range and mobility
  that no routing security mechanism can answer, because it is upstream of
  routing security entirely.

Stating "PDR > 95%" as the objective silently assumes the second question is
already answered yes. **It is not**, and the answer is measured, not assumed.

## 3. What is measured today

From `gate1b-record.md` §6 and `evidence/gate1b/gate1b-calibration.txt`, on the
100-node ns-3.48 scenario built for this project, **with zero attackers**:

| Area | Baseline PDR (no attack) |
|---|---:|
| 1500 × 300 m | 0.052 |
| 800 × 800 m | 0.062 |
| 500 × 500 m | 0.476 |
| 300 × 300 m | 0.292 |

and, holding geometry fixed at 500 × 500 m with one flow, across five seeds:
0.787, 0.000, 0.009, 0.805, 0.207.

**No configuration of this scenario reaches 95% with zero attackers.** The
best geometry found so far tops out below 0.5, and the connectivity itself is
a near-coin-flip per seed rather than a stable operating point. This is
finding D-09 in `phase1-design-review.md`, restated here because it is the
direct, provable reason objective 1 is currently unreachable — not through any
weakness of a detector or trust model, none of which exist yet, but because
there is frequently no path for a packet to travel on regardless of who is
attacking.

**PDR > 95% is not a target this project can work toward yet. It is a target
that requires fixing the scenario first**, independent of and prior to any
security-mechanism work. Chasing it against the current scenario would produce
a number that cannot mean what the objective intends it to mean.

## 4. The internal tensions the six objectives don't acknowledge

Stated as a flat list, the six objectives read as independent. They are not;
three real trade-offs are built into this problem and pretending otherwise
would produce a design that quietly fails one axis to satisfy another without
saying so.

**Detection speed vs. overhead and energy.** A detector learns something only
by observing traffic or by exchanging evidence about it. Faster, more
confident detection needs either more observation opportunities (which the
open question of observation coverage bears on directly — see
`ns3.48-api-verification.md` §2) or more frequent evidence/witness/certificate
exchange (which *is* routing-adjacent control overhead, and *is* energy, since
every simulated signature verification is modeled with an explicit energy
cost per blueprint assumption 6). "Very fast" and "no overhead" cannot both be
maximized; the design has to state which one yields first, and by how much,
not aspire to both being best-in-class simultaneously.

**Energy vs. certificate-gated quarantine.** PTMB's entire safety property
rests on signatures, committee votes and certificates (§7 of the blueprint).
Each of those costs simulated energy by explicit design choice. A system
that minimizes energy first would use fewer witnesses, smaller committees, and
issue certificates less eagerly — which directly slows objective 6. This is
also the same tension `phase1-design-review.md` §4 already flagged: PTMB is
the most expensive component to build and the one prior work has most
thoroughly anticipated. If the six objectives are genuinely prioritized in the
order given, that is an argument for a lighter-weight or optional
certification path, not for building PTMB at full generality.

**Throughput vs. delay vs. contention**, already measured directly in this
project: increasing the number of concurrent flows in the same collision
domain does not increase delivered throughput monotonically once 802.11
contention dominates (`gate1b-calibration.txt`, flow-count sweep). "Higher
throughput" and "lower delay" are not free to co-maximize against a fixed
radio and node density; they trade off against each other and against
offered load in ways the scenario's own physical layer decides, not the
security mechanism.

None of this means the objectives are wrong. It means they need an explicit
priority order and a stated acceptable cost on each axis before Gate 5's
confirmatory design is frozen — exactly the kind of pre-registration the
Phase-One report already argues for elsewhere and does not yet apply to this
specific list.

## 5. Honest status against each objective, right now

| # | Objective | Measurable today? | Blocking reason |
|---|---|---|---|
| 1 | PDR > 95% | No | Baseline PDR without attack is 0.05–0.48 (D-09). Scenario must be fixed first. |
| 2 | Higher throughput | Partially | Measured under attack (Gate 1B), but against the same broken baseline; not yet meaningful. |
| 3 | Lower delay | No | Not instrumented yet; no delay metric collected by `mtcaodv-gate1b`. |
| 4 | No routing overhead | No | No security control plane exists yet (Gate 3+ in the blueprint). There is nothing to measure overhead of. |
| 5 | Lower energy consumption | No | No `SimulatedCryptoProvider` or `MetricsCollector` energy accounting exists yet (Gate 3+). |
| 6 | Very fast detection and isolation | No | No detector, trust model, or certificate mechanism exists yet (Gate 2–4). Only the attack (Gate 1B) is implemented. |

Zero of six are currently measurable in a way that would mean what the
objective intends. That is not a failure of this session's work; it is an
accurate description of standing at the end of Gate 1B, where only the attack
exists and no defense has been written.

## 6. What is actually on the critical path toward these objectives

In order, because each blocks the one after it:

1. **Fix the scenario (D-09).** Choose the application profile (Phase-One
   §13 item 3: pedestrian/disaster-response vs. vehicular), then re-derive
   transmission range, density and speed so the no-attack baseline PDR clears
   a pre-registered floor (0.85 was proposed in `phase1-design-review.md`).
   Until this is done, objective 1 cannot be interpreted, and objectives 2–3
   inherit the same problem since they are measured on the same topology.
2. **Measure observation coverage** (the open half of the forwarding-
   observation spike, `ns3.48-api-verification.md` §2, `gate1b-record.md`
   §7). This determines whether objective 6 is achievable at all with passive
   observation, or whether it requires the receipt-based fallback the
   Phase-One report kept open — which itself costs overhead (objective 4) and
   energy (objective 5). The answer changes what Gate 2 should build.
3. **State the priority order among objectives 4, 5 and 6 explicitly**,
   before Gate 3 (PTMB) is built at full generality, because that gate is
   where the tension is spent, not discovered.
4. Only then do Gates 2–5 produce numbers for objectives 2, 3, 4, 5, 6 that
   are attributable to the security mechanism rather than to an unresolved
   scenario or an unstated trade-off.

Recording the objective now, at the end of Gate 1B, is useful precisely
because it exposes that step 1 is not optional scenery — it is the
prerequisite every one of the six numbers depends on.
