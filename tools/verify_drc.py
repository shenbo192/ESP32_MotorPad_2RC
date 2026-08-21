#!/usr/bin/env python3
"""Independent DRC check on the COMMITTED board dump (round-trip through LCEDA)."""
import json, math

DUMP = "tools/routing_dump.json"
MIN = 6.0  # mil clearance spec

def load():
    with open(DUMP) as f:
        w = json.load(f)
    d = w["result"] if isinstance(w, dict) else w
    if isinstance(d, str):
        d = json.loads(d)
    return d

def seg_dist(px, py, x1, y1, x2, y2):
    dx, dy = x2 - x1, y2 - y1
    if dx == 0 and dy == 0:
        return math.hypot(px - x1, py - y1)
    t = ((px - x1) * dx + (py - y1) * dy) / (dx*dx + dy*dy)
    t = max(0.0, min(1.0, t))
    return math.hypot(px - (x1 + t*dx), py - (y1 + t*dy))

def seg_seg(a, b):
    return min(seg_dist(a["x1"], a["y1"], b["x1"], b["y1"], b["x2"], b["y2"]),
               seg_dist(a["x2"], a["y2"], b["x1"], b["y1"], b["x2"], b["y2"]),
               seg_dist(b["x1"], b["y1"], a["x1"], a["y1"], a["x2"], a["y2"]),
               seg_dist(b["x2"], b["y2"], a["x1"], a["y1"], a["x2"], a["y2"]))

def main():
    d = load()
    lines = d["lines"]; vias = d["vias"]
    # normalize coords (LCEDA may return strings)
    for l in lines:
        for k in ("x1","y1","x2","y2","w","layer"):
            l[k] = float(l[k]) if l[k] is not None else 0.0
        l["net"] = l.get("net")
    for v in vias:
        for k in ("x","y","dia","hole"):
            v[k] = float(v[k]) if v[k] is not None else 0.0
        v["net"] = v.get("net")

    # ---- connectivity per net (tracks + vias as graph) ----
    # Model: a "site" is (layer, gx, gy) where gx=round(x/10), gy=round(y/10)
    # (10mil grid cell, matching the router's own junction model). Tracks connect
    # their two endpoints; two tracks sharing a site (a plain junction, no via)
    # connect; a via connects BOTH layers at its site (through-hole copper).
    from collections import defaultdict, deque
    adj = defaultdict(set)
    def addE(a, b):
        adj[a].add(b); adj[b].add(a)

    # site for a via: through-hole -> connects layer 0 and layer 1 at its cell
    via_site = {}
    for i, v in enumerate(vias):
        gx, gy = round(v["x"]/10), round(v["y"]/10)
        s0, s1 = ("S",0,gx,gy), ("S",1,gx,gy)
        addE(s0, s1)                 # through-hole joins both layers
        via_site[i] = (s0, s1)

    # tracks: connect their two endpoint sites
    tnode = {}
    for i, l in enumerate(lines):
        gx1, gy1 = round(l["x1"]/10), round(l["y1"]/10)
        gx2, gy2 = round(l["x2"]/10), round(l["y2"]/10)
        a = ("S", int(l["layer"]), gx1, gy1)
        b = ("S", int(l["layer"]), gx2, gy2)
        tnode[(i,"s")] = a; tnode[(i,"e")] = b
        addE(a, b)
        # also bind each track endpoint to a coincident via at the same cell
        for vi, (s0, s1) in via_site.items():
            if vias[vi]["net"] != l["net"]:
                continue
            # same cell as either endpoint site -> the track lands on the via
            if (int(l["layer"]), gx1, gy1) == (s0[1], s0[2], s0[3]) or \
               (int(l["layer"]), gx2, gy2) == (s0[1], s0[2], s0[3]) or \
               (int(l["layer"]), gx1, gy1) == (s1[1], s1[2], s1[3]) or \
               (int(l["layer"]), gx2, gy2) == (s1[1], s1[2], s1[3]):
                addE(a, s0); addE(b, s0)

    nets_of = {}
    for i, v in enumerate(vias):
        for s in via_site[i]:
            nets_of[s] = v["net"]
    for i, l in enumerate(lines):
        nets_of[tnode[(i,"s")]] = l["net"]; nets_of[tnode[(i,"e")]] = l["net"]

    disconn = []
    net_eps = defaultdict(list)
    for node, net in nets_of.items():
        if net:
            net_eps[net].append(node)
    for net, nodes in net_eps.items():
        if len(nodes) < 2:
            continue
        seen = set([nodes[0]]); q = deque([nodes[0]])
        while q:
            n = q.popleft()
            for m in adj[n]:
                if m not in seen:
                    seen.add(m); q.append(m)
        for n in nodes:
            if n not in seen:
                disconn.append(net); break

    # ---- clearance checks ----
    violVT = violVV = violTT = 0
    track_objs = [l for l in lines if l["net"]]
    for v in vias:
        if not v["net"]:
            continue
        for l in track_objs:
            if l["net"] == v["net"]:
                continue
            dd = seg_dist(v["x"], v["y"], l["x1"], l["y1"], l["x2"], l["y2"])
            gap = dd - v["dia"]/2 - l["w"]/2
            if gap < MIN:
                violVT += 1
    for i in range(len(vias)):
        for j in range(i+1, len(vias)):
            a, b = vias[i], vias[j]
            if not a["net"] or not b["net"] or a["net"] == b["net"]:
                continue
            dd = math.hypot(a["x"]-b["x"], a["y"]-b["y"])
            gap = dd - a["dia"]/2 - b["dia"]/2
            if gap < MIN:
                violVV += 1
    for i in range(len(track_objs)):
        for j in range(i+1, len(track_objs)):
            a, b = track_objs[i], track_objs[j]
            if a["net"] == b["net"] or a["layer"] != b["layer"]:
                continue
            dd = seg_seg(a, b)
            gap = dd - a["w"]/2 - b["w"]/2
            if gap < MIN:
                violTT += 1

    print(f"connectivity failures (disconnected nets): {len(disconn)} {disconn[:8]}")
    print(f"violVT (via-track <{MIN}mil): {violVT}")
    print(f"violVV (via-via   <{MIN}mil): {violVV}")
    print(f"violTT (track-track <{MIN}mil): {violTT}")
    ok = not disconn and violVT == 0 and violVV == 0 and violTT == 0
    print("DRC RESULT:", "PASS - board is clean" if ok else "FAIL - see above")

if __name__ == "__main__":
    main()
