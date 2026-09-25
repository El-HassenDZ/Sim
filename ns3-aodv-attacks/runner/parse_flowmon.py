#!/usr/bin/env python3
"""
runner/parse_flowmon.py
=======================
Independent cross-check of the metrics the C++ program writes: recompute
PDR / throughput / delay directly from a FlowMonitor XML dump, counting
only the data flows (UDP destination port 8000). If these disagree with
<prefix>_metrics.csv, trust neither until the cause is found.

Usage:
    python3 parse_flowmon.py outputs/baseline_r1_flowmon.xml
"""

import sys
import xml.etree.ElementTree as ET

SINK_PORT = 8000


def parse(path):
    tree = ET.parse(path)
    root = tree.getroot()

    # Map flowId -> destinationPort from the classifier section.
    dport = {}
    for cls in root.iter("Ipv4FlowClassifier"):
        for flow in cls.findall("Flow"):
            dport[flow.get("flowId")] = int(flow.get("destinationPort"))

    tx = rx = rx_bytes = lost = 0
    delay_sum = 0.0
    first_rx, last_rx = None, None
    for stats in root.iter("FlowStats"):
        for flow in stats.findall("Flow"):
            fid = flow.get("flowId")
            if dport.get(fid) != SINK_PORT:
                continue
            tx += int(flow.get("txPackets"))
            rx += int(flow.get("rxPackets"))
            rx_bytes += int(flow.get("rxBytes"))
            lost += int(flow.get("lostPackets"))
            delay_sum += _ns(flow.get("delaySum"))
            if int(flow.get("rxPackets")) > 0:
                f = _ns(flow.get("timeFirstRxPacket"))
                l = _ns(flow.get("timeLastRxPacket"))
                first_rx = f if first_rx is None else min(first_rx, f)
                last_rx = l if last_rx is None else max(last_rx, l)

    pdr = 100.0 * rx / tx if tx else 0.0
    span = (last_rx - first_rx) if (first_rx is not None and last_rx and last_rx > first_rx) else 1.0
    tp_kbps = rx_bytes * 8.0 / span / 1000.0 if span else 0.0
    delay_ms = 1000.0 * delay_sum / rx if rx else 0.0
    return dict(tx_packets=tx, rx_packets=rx, lost_packets=lost,
                pdr_percent=round(pdr, 3), throughput_kbps=round(tp_kbps, 3),
                avg_delay_ms=round(delay_ms, 4))


def _ns(timestr):
    """FlowMonitor serialises times like '+1.23e+09ns' -> seconds."""
    s = timestr.strip().lstrip("+")
    if s.endswith("ns"):
        return float(s[:-2]) * 1e-9
    if s.endswith("s"):
        return float(s[:-1])
    return float(s)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: parse_flowmon.py <flowmon.xml>")
    for k, v in parse(sys.argv[1]).items():
        print(f"{k:>18}: {v}")
