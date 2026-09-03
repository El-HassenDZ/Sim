# Gate 1B — AODV-interoperable Blackhole routing

**Status:** implemented, compiled and tested against ns-3.48.
**Scope:** the two routing hooks the Gate 1A package deferred, the helper
contract that package omitted, and the API correction the Gate 1A review
required before any caller existed.

---

## 1. What was built

| Component | File | Purpose |
|---|---|---|
| Forked routing protocol | `model/blackhole-aodv-routing-protocol.{h,cc}` | AODV-interoperable malicious protocol with two attack hooks |
| Routing helper | `helper/blackhole-aodv-helper.{h,cc}` | `Copy` / `Create` / `Set` / `AssignStreams`, plus policy composition |
| Population split | `AttackManager::PartitionByAttackers` | Maps a validated selection record onto two `NodeContainer`s |
| Drop-decision API | `model/attack-behavior.h` | `PacketDropContext` replaces three loose booleans (review finding R-01) |
| Integration tests | `test/blackhole-routing-test-suite.cc` | Regression bridge, transit drop, forged-reply capture, population split |
| Measurement scenario | `examples/mtcaodv-gate1b.cc` | 100-node PDR / forged-reply / drop measurement with a JSON manifest |

## 2. Fork strategy and its one important property

`src/aodv` is not modified. Only `aodv-routing-protocol.{h,cc}` is forked
(500 + 2289 lines). Every AODV support type — packet headers, routing table,
request queue, neighbour table, id cache, duplicate detection — is **consumed
from the stock module** through the exported `ns3/aodv-*.h` headers, which
`src/aodv/CMakeLists.txt` lists in `HEADER_FILES`.

The consequence is worth stating because it is the reason the fork is
maintainable: the fork **cannot drift from upstream wire format or table
semantics**, because it does not own them. An honest node running
`ns3::aodv::RoutingProtocol` and an attacker running
`ns3::mtcaodv::BlackholeAodvRoutingProtocol` interoperate with no negotiation,
which is what makes the attack realistic.

Renamed for coexistence: namespace, class, include guard, TypeId strings
(`ns3::mtcaodv::BlackholeAodvRoutingProtocol`,
`ns3::mtcaodv::DeferredRouteOutputTag`), attribute group, and log component.
A duplicate TypeId string would abort registration at start-up, so the
`DeferredRouteOutputTag` rename is load-bearing, not cosmetic.

## 3. The two hooks

**Hook 1 — forged route reply**, in `RecvRequest`, placed after the
`IsMyOwnAddress(dst)` check and after the reverse route to the originator has
been created or refreshed. Both placements matter: the first keeps a node
answering for itself on the standard path, the second makes the forged reply
deliverable. Returning from the hook also suppresses onward RREQ propagation,
which is what wins the race against the real destination's reply.

The advertised sequence number, hop count and lifetime come from
`AttackBehavior::CreateForgedReplyProfile`; this class supplies only AODV
serialization and holds no attack parameter of its own.

**Hook 2 — transit drop**, at the head of `Forwarding`. `RouteInput` reaches
`Forwarding` only after multicast rejection, broadcast handling and unicast
local delivery, so every packet arriving there is genuinely in transit. That is
why `isTransit` is asserted true at this single call site and nowhere else.

A route error is deliberately *not* sent on the dropped packet: a Blackhole
that advertised its own failure would let the source repair the route, which is
not the modelled attack.

**Both hooks are inert when no policy is attached.** A node with a null
`AttackBehavior` reproduces unmodified ns-3.48 AODV. This is the regression
bridge the blueprint requires (§3.2), and it is tested, not asserted.

## 4. R-01 applied before the first caller

The Gate 1A review found that `ShouldDropTransitPacket(Time, bool, bool)` had
no way to know a packet was in transit — the word appeared only in the method
name. It is now:

```cpp
struct PacketDropContext
{
    Time observationTime{Seconds(0)};
    bool isTransit{false};          // defaults to the benign answer
    bool isRoutingControl{false};
    bool isSecurityControl{false};
};

virtual bool ShouldDropPacket(const PacketDropContext& context) const = 0;
```

`BlackholeBehavior::ShouldDropPacket` returns false first if `!isTransit`. Two
test cases pin the property: an explicitly non-transit context, and a
default-constructed one, must both yield no drop. A caller that forgets to
state the transit property therefore gets the benign decision rather than an
attacker that discards its own traffic.

## 5. Test results

```text
$ ./test.py --list | grep mtcaodv
unit                 mtcaodv-attack-manager        PASS
unit                 mtcaodv-blackhole-behavior    PASS
unit                 mtcaodv-blackhole-routing     PASS
```

`mtcaodv-blackhole-routing` contains four integration cases, each running a
full ns-3 scenario with deterministic range-based propagation and fixed
positions:

| Case | Asserts |
|---|---|
| `PartitionByAttackers` | Split sizes, membership both ways, rejection of a record describing another population |
| `ForkRegression` | Inert fork delivers **exactly** the byte count stock AODV delivers on the same 3-hop chain; zero forged replies, zero drops |
| `OnPathBlackhole` | Benign chain delivers; the same chain with a malicious relay delivers **zero**; the drop trace fires |
| `ForgedReplyCapture` | An attacker in range of the source but not the destination — so with no route to advertise — emits a forged reply and reduces delivery |

The four stock AODV suites (`routing-aodv`, `aodv-routing-id-cache`,
`routing-aodv-loopback`, `routing-aodv-regression`) still pass with the fork
installed, confirming `src/aodv` is untouched.

## 6. Attack effect at scale, and a scenario finding

`mtcaodv-gate1b`, 100 nodes, 1500 × 300 m, 10 flows, seed 12345, run 1:

| Attacker ratio | Attackers | PDR | Forged replies | Transit drops |
|---:|---:|---:|---:|---:|
| 0.00 | 0 | 0.052 | 0 | 0 |
| 0.05 | 5 | 0.050 | 13 | 318 |
| 0.10 | 10 | 0.011 | 40 | 1356 |
| 0.20 | 20 | 0.015 | 35 | 2163 |
| 0.30 | 30 | 0.000 | 30 | 2285 |

The attack mechanism is demonstrably working: forged replies are emitted,
transit drops scale with the attacker count, and delivery reaches zero at 30%.

**But the no-attack baseline is 0.052, and that makes the table
scientifically useless as a measurement of attack impact.** It is reported here
as an instrument check, not as a result.

Diagnosis, from `evidence/gate1b/gate1b-calibration.txt`:

- The area is the dominant factor: 1500 × 300 gives 0.052, 500 × 500 gives
  0.476, 300 × 300 gives 0.292. Both extremes are bad, for opposite reasons.
- It is **not** MAC congestion: reducing the offered load twentyfold (10 flows
  at 0.5 kbps instead of 4 kbps) leaves the baseline at 0.34.
- It is a **marginal-connectivity regime**: single-flow baseline PDR across
  five seeds at 500 × 500 is 0.787, 0.000, 0.009, 0.805, 0.207. Whether a given
  source–destination pair has a usable multi-hop path is close to a coin flip
  per seed. Multiple flows average over that lottery, which is why the 10-flow
  numbers look better behaved than the 1-flow ones while being no healthier.

The consequences for the experimental design are recorded as finding D-09 in
`phase1-design-review.md`. In short: a pre-registered baseline-validity
criterion is needed before the confirmatory campaign, and it cannot be settled
until the application profile is chosen, because range, density and speed all
follow from it.

## 7. What Gate 1B does not establish

- No defensive component exists. There is no detector, no trust model, no
  ledger; nothing in this gate observes the attack from inside the network.
- The scenario is not calibrated. §6 above is an instrument check, not a
  measurement of attack impact against a valid baseline.
- Observation coverage — the fraction of a neighbour's forwards that is
  actually overheard — is still unmeasured. It remains the input on which the
  entire evidence layer depends, and it is the correct subject of the next
  gate.
