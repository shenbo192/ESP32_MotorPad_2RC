#!/usr/bin/env python3
"""Verify a ROUTER PLAN (segments + vias) from router_out.json, independent of LCEDA.
Mirrors verify_drc.py but reads the in-script plan, not the committed board."""
import json, math
from collections import defaultdict, deque

MIN = 6.0
D = json.load(open("tools/router_out.json"))
out = D["result"] if isinstance(D, dict) and "result" in D else D
if isinstance(out, str):
    out = json.loads(out)
lines = out["segments"]; vias = out["vias"]

def seg_dist(px, py, x1, y1, x2, y2):
    dx, dy = x2 - x1, y2 - y1
    if dx == 0 and dy == 0:
        return math.hypot(px - x1, py - y1)
    t = max(0.0, min(1.0, ((px - x1) * dx + (py - y1) * dy) / (dx*dx + dy*dy)))
    return math.hypot(px - (x1 + t*dx), py - (y1 + t*dy))

def seg_seg(a, b):
    return min(seg_dist(a["x1"], a["y1"], b["x1"], b["y1"], b["x2"], b["y2"]),
               seg_dist(a["x2"], a["y2"], b["x1"], b["y1"], b["x2"], b["y2"]),
               seg_dist(b["x1"], b["y1"], a["x1"], a["y1"], a["x2"], a["y2"]),
               seg_dist(b["x2"], b["y2"], a["x1"], a["y1"], a["x2"], a["y2"]))

# connectivity: union-find over primitives that touch (<=12mil) or share a via pad
parent = {}
def find(x):
    parent.setdefault(x, x)
    while parent[x] != x:
        parent[x] = parent[parent[x]]; x = parent[x]
    return x
def uni(a, b):
    parent[find(a)] = find(b)

def near_via(x, y):
    best = None; bd = 1e9
    for i, v in enumerate(vias):
        dd = math.hypot(v["x"] - x, v["y"] - y)
        if dd < bd:
            bd = dd; best = i
    return best, bd

nodes = []
for i, l in enumerate(lines):
    nodes.append(("L", i))
for i, v in enumerate(vias):
    nodes.append(("V", i))
# connect vias that share (x,y)
byxy = defaultdict(list)
for i, v in enumerate(vias):
    byxy[(round(v["x"]), round(v["y"]))].append(i)
for k, lst in byxy.items():
    for a in lst:
        for b in lst:
            if a != b:
                uni(("V", a), ("V", b))
# connect track endpoints to vias whose pad contains the endpoint
for i, l in enumerate(lines):
    for (ex, ey) in ((l["x1"], l["y1"]), (l["x2"], l["y2"])):
        vi, bd = near_via(ex, ey)
        if bd <= vias[vi]["dia"] / 2 + 12:
            uni(("L", i), ("V", vi))
# connect touching track endpoints
import itertools
eps = []
for i, l in enumerate(lines):
    eps.append(((l["x1"], l["y1"]), ("L", i), "s"))
    eps.append(((l["x2"], l["y2"]), ("L", i), "e"))
for a, b in itertools.combinations(eps, 2):
    if a[2] == b[2]:  # same endpoint role irrelevant
        pass
    if math.hypot(a[0][0] - b[0][0], a[0][1] - b[0][1]) <= 12:
        uni(a[1], b[1])

net_of = {}
for i, l in enumerate(lines):
    net_of[("L", i)] = l["net"]
for i, v in enumerate(vias):
    net_of[("V", i)] = v["net"]

net_nodes = defaultdict(list)
for n in nodes:
    if net_of[n]:
        net_nodes[net_of[n]].append(n)
disconn = []
for net, ns in net_nodes.items():
    if len(ns) < 2:
        continue
    root = find(ns[0])
    if any(find(n) != root for n in ns):
        disconn.append(net)

violVT = violVV = violTT = 0
track_objs = [l for l in lines if l.get("net")]
for v in vias:
    if not v.get("net"):
        continue
    for l in track_objs:
        if l["net"] == v["net"]:
            continue
        dd = seg_dist(v["x"], v["y"], l["x1"], l["y1"], l["x2"], l["y2"])
        gap = dd - v["dia"] / 2 - (l.get("w") or 10) / 2
        if gap < MIN:
            violVT += 1
for i in range(len(vias)):
    for j in range(i + 1, len(vias)):
        a, b = vias[i], vias[j]
        if not a.get("net") or not b.get("net") or a["net"] == b["net"]:
            continue
        dd = math.hypot(a["x"] - b["x"], a["y"] - b["y"])
        gap = dd - a["dia"] / 2 - b["dia"] / 2
        if gap < MIN:
            violVV += 1
for i in range(len(track_objs)):
    for j in range(i + 1, len(track_objs)):
        a, b = track_objs[i], track_objs[j]
        if a["net"] == b["net"] or a["layer"] != b["layer"]:
            continue
        dd = seg_seg(a, b)
        gap = dd - (a.get("w") or 10) / 2 - (b.get("w") or 10) / 2
        if gap < MIN:
            violTT += 1

print(f"PLAN connectivity failures: {len(disconn)} {disconn[:8]}")
print(f"PLAN violVT: {violVT}  violVV: {violVV}  violTT: {violTT}")
ok = not disconn and violVT == 0 and violVV == 0 and violTT == 0
print("PLAN DRC:", "PASS" if ok else "FAIL")
