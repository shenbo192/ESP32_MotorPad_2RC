#!/usr/bin/env python3
"""Render the live PCB routing dump into a human-checkable SVG preview."""
import json, colorsys, sys

DUMP = "tools/routing_dump.json"
OUT = "pcb_routing_preview.svg"
W, H = 2756, 2520  # board size in mil

def load():
    with open(DUMP) as f:
        wrapper = json.load(f)
    # eda_run.py returns {"success":True,"result":"..."} where result is a JSON string
    data = wrapper["result"] if isinstance(wrapper, dict) else wrapper
    if isinstance(data, str):
        data = json.loads(data)
    return data

def net_color(net, i, n):
    if net in (None, "None", ""):
        return "#888888"
    h = (i * 0.61803398875) % 1.0
    r, g, b = colorsys.hsv_to_rgb(h, 0.55, 0.85)
    return "#%02x%02x%02x" % (int(r*255), int(g*255), int(b*255))

def main():
    data = load()
    lines = data["lines"]
    vias = data["vias"]
    # assign colors
    nets = sorted({l["net"] for l in lines} | {v["net"] for v in vias})
    nets = [n for n in nets if n not in (None, "None", "")]
    cmap = {n: net_color(n, i, len(nets)) for i, n in enumerate(nets)}

    SCALE = 0.28  # px per mil
    pad = 20
    vw = int(W * SCALE) + pad*2
    vh = int(H * SCALE) + pad*2
    def sx(x): return pad + x * SCALE
    def sy(y): return vh - (pad + y * SCALE)  # flip Y

    parts = []
    parts.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{vw}" height="{vh}" viewBox="0 0 {vw} {vh}">')
    parts.append(f'<rect x="0" y="0" width="{vw}" height="{vh}" fill="#fafafa"/>')
    # board outline
    parts.append(f'<rect x="{sx(0):.1f}" y="{sy(H):.1f}" width="{W*SCALE:.1f}" height="{H*SCALE:.1f}" fill="none" stroke="#333" stroke-width="2"/>')

    # vias first (under tracks visually)
    for v in vias:
        c = cmap.get(v["net"], "#888")
        r = (v["dia"] or 40) / 2 * SCALE
        parts.append(f'<circle cx="{sx(v["x"]):.1f}" cy="{sy(v["y"]):.1f}" r="{r:.1f}" fill="none" stroke="{c}" stroke-width="1.2"/>')

    # tracks: top layer solid, bottom layer dashed
    for l in lines:
        c = cmap.get(l["net"], "#888")
        x1, y1, x2, y2 = l["x1"], l["y1"], l["x2"], l["y2"]
        wpx = max(0.6, (l["w"] or 10) / 2 * SCALE)
        dash = ' stroke-dasharray="5,4"' if l["layer"] == 1 else ""
        parts.append(f'<line x1="{sx(x1):.1f}" y1="{sy(y1):.1f}" x2="{sx(x2):.1f}" y2="{sy(y2):.1f}" stroke="{c}" stroke-width="{wpx:.1f}"{dash} stroke-linecap="round"/>')

    parts.append('</svg>')
    with open(OUT, "w") as f:
        f.write("\n".join(parts))
    print(f"wrote {OUT}: {len(lines)} tracks, {len(vias)} vias, {len(nets)} nets")

if __name__ == "__main__":
    main()
