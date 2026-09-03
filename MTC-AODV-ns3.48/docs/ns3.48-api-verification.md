# ns-3.48 API verification for the MTC-AODV blueprint

**Source under inspection:** `ns-3.48` (`VERSION` = `3.48`), obtained from
<https://gitlab.com/nsnam/ns-3-dev/-/archive/ns-3.48/ns-3-dev-ns-3.48.tar.bz2>
**Purpose:** replace the blueprint's cited-documentation claims (§2.2) with
line-level evidence from the source tree that will actually be compiled.
**Method:** direct reading of the extracted ns-3.48 tree. Every row below cites
`file:line`. Nothing in this file is inferred from documentation or memory.

## 1. Confirmed claims

| Blueprint claim (§2.2) | Verdict | Evidence |
|---|---|---|
| `RouteOutput` is public | Confirmed | `src/aodv/model/aodv-routing-protocol.h:63` |
| `RouteInput` is public | Confirmed | `src/aodv/model/aodv-routing-protocol.h:67` |
| Interface notifications are public | Confirmed | `aodv-routing-protocol.h:74-75` (`NotifyInterfaceUp`/`Down`) |
| `SetIpv4` is public | Confirmed | `aodv-routing-protocol.h:78` |
| `PrintRoutingTable` is public | Confirmed | `aodv-routing-protocol.h:79` |
| `AssignStreams` is public | Confirmed | `aodv-routing-protocol.h:193` |
| `RecvAodv` is private | Confirmed | `aodv-routing-protocol.h:376` |
| `RecvRequest` is private | Confirmed | `aodv-routing-protocol.h:383` |
| `RecvReply` is private | Confirmed | `aodv-routing-protocol.h:390` |
| `SendRequest` is private | Confirmed | `aodv-routing-protocol.h:419` |
| `SendReply` is private | Confirmed | `aodv-routing-protocol.h:424` |
| `Forwarding` is private | Confirmed | `aodv-routing-protocol.h:307` |
| AODV control traffic uses UDP port 654 | Confirmed | `src/aodv/model/aodv-routing-protocol.cc:53` |
| `AodvHelper` exposes `Copy`/`Create`/`Set`/`AssignStreams` | Confirmed | `src/aodv/helper/aodv-helper.h:36,46,53,65` |
| `WifiMac` exposes a `MacPromiscRx` trace source | Confirmed (but see §2) | `src/wifi/model/wifi-mac.cc:314-320` |

The decision in §1.2 of the blueprint — that a thin subclass of
`ns3::aodv::RoutingProtocol` cannot intercept the required control-plane events
— is therefore correct as stated. Six of the six handlers the security hooks
need are private.

## 2. Claim requiring correction: the forwarding-observation source

The blueprint calls `MacPromiscRx` "a promising observation source" and defers
to Gate 2 the question of "which packet headers and sender metadata remain
visible". That question is answerable now, and the answer is negative for the
trace as named.

### 2.1 The trace carries no transmitter identity

```text
src/wifi/model/wifi-mac.h:1338
    TracedCallback<Ptr<const Packet>> m_macPromiscRxTrace;

src/wifi/model/wifi-mac.cc:742-745
    WifiMac::NotifyPromiscRx(Ptr<const Packet> packet)
    {
        m_macPromiscRxTrace(packet);
    }
```

The callback signature is a bare packet. No source address, no destination
address, no `PacketType`. `ForwardingObserver` cannot attribute an overheard
frame to a next hop through this trace, which is precisely the attribution the
evidence layer (Equations 6-10) depends on.

### 2.2 The trace does not fire in a default AODV scenario

```text
src/wifi/model/wifi-net-device.cc:538-542
    if (!m_promiscRx.IsNull())
    {
        m_mac->NotifyPromiscRx(copy);
        m_promiscRx(this, copy, llc.GetType(), from, to, type);
    }
```

`MacPromiscRx` fires only when a promiscuous receive callback is already
installed on the device. Installing an Internet stack plus AODV does not
install one. A `ForwardingObserver` connected only to this trace would record
zero observations and would be indistinguishable from a network in which no
neighbour ever forwards anything.

### 2.3 The mechanism that does supply attribution

`Node::RegisterProtocolHandler` accepts a promiscuous flag and, when set,
installs the device-level promiscuous callback itself:

```text
src/network/model/node.h:147-154   ProtocolHandler signature
    Callback<void, Ptr<NetDevice>, Ptr<const Packet>, uint16_t,
             const Address& /*from*/, const Address& /*to*/,
             NetDevice::PacketType>

src/network/model/node.h:168-171
    void RegisterProtocolHandler(ProtocolHandler handler,
                                 uint16_t protocolType,
                                 Ptr<NetDevice> device,
                                 bool promiscuous = false);

src/network/model/node.cc:236-246
    dev->SetPromiscReceiveCallback(
        MakeCallback(&Node::PromiscReceiveFromDevice, this));
```

Three properties make this the correct hook rather than
`NetDevice::SetPromiscReceiveCallback` directly:

1. It carries `from`, `to`, the protocol number, and `PacketType`, so an
   overheard frame is identifiable as `PACKET_OTHERHOST` and attributable to a
   transmitter.
2. `Node` keeps a handler list, so registering does not evict a promiscuous
   consumer installed by another component. Calling
   `SetPromiscReceiveCallback` directly would.
3. Registering it is what causes `MacPromiscRx` to start firing, so the trace
   becomes usable for byte accounting once this handler exists.

In ad hoc (IBSS) mode `from` is the immediate transmitter, not an upstream
originator:

```text
src/wifi/model/adhoc-wifi-mac.cc:137-138
    Mac48Address from = hdr->GetAddr2();
    Mac48Address to = hdr->GetAddr1();
```

`WifiNetDevice::ForwardUp` removes the LLC/SNAP header before dispatching
(`wifi-net-device.cc:527,536`), so the delivered packet begins at the IPv4
header. `PacketFingerprint` can therefore be computed over IPv4 identification,
addresses, and payload without re-parsing an 802.11 header.

### 2.4 Consequence for the plan

The Gate 2 fallback described in the blueprint — "introduce an explicit bounded
observation header and count every added byte" — is not required on account of
attribution. Attribution is available. The fallback remains relevant only for
information the MAC layer genuinely cannot supply, such as proving that a
neighbour *chose* to drop rather than never received. That distinction is the
real content of Equations (6)-(8) and is unaffected by this correction.

The blueprint's §2.2 row should be rewritten from "`MacPromiscRx` provides a
promising observation source" to "promiscuous protocol-handler registration
provides attributable observation; `MacPromiscRx` is a byte-accounting trace,
not an attribution source."

## 3. Build-environment facts observed on this host

| Item | Value |
|---|---|
| Compiler | g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0 |
| CMake | 3.28.3 |
| Generator | Ninja 1.11.1 |
| C++ standard applied by ns-3.48 | `-std=c++23` |
| Warning flags applied | `-Wall -Wpedantic` |
| Warnings as errors | `-DNS3_WARNINGS_AS_ERRORS=OFF` in the default profile |

The last row matters for the module: sign-comparison and narrowing warnings in
`contrib/mtcaodv` will not fail the default build. They will fail a build
configured with `--werror`, which the validation protocol should adopt so that
warnings are caught at the gate that introduces them.
