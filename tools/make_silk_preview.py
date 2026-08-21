import json

W, H = 2756, 2520
BX0, BY0, BX1, BY1 = 0, 0, 2756, 2520  # 板外形

w = json.load(open("tools/current_dump.json"))
d = w["result"]
if isinstance(d, str):
    d = json.loads(d)
vias = d["vias"]
for v in vias:
    for k in ("x", "y", "dia", "hole"):
        v[k] = float(v[k]) if v[k] is not None else 0.0
    v["net"] = v.get("net")

labels = json.load(open("tools/silk_labels.json"))

parts = []
# 背景板
parts.append(f'<rect x="{BX0}" y="{BY0}" width="{BX1-BX0}" height="{BY1-BY0}" fill="#1e2430" stroke="#46506a" stroke-width="6"/>')
# 焊盘
for v in vias:
    x, y, dia = v["x"], v["y"], v["dia"]
    fill = "#3a4252" if dia < 100 else "#2a3140"
    parts.append(f'<circle cx="{x}" cy="{y}" r="{dia/2:.1f}" fill="{fill}" stroke="#6b7689" stroke-width="2"/>')
    parts.append(f'<circle cx="{x}" cy="{y}" r="{v["hole"]/2:.1f}" fill="#11151c" stroke="none"/>')
# 丝印文字（放大约 2.5 倍显示，亮色便于看清）
for L in labels:
    anchor = {2: "start", 8: "end", 5: "middle"}[L["a"]]
    # 垂直居中微调（SVG 基线）
    dy = 8
    parts.append(
        f'<text x="{L["x"]}" y="{L["y"]+dy}" font-family="monospace" font-size="62" '
        f'fill="#ffe14d" text-anchor="{anchor}" font-weight="bold">{L["t"]}</text>'
    )

svg = (
    f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="760" '
    f'height="{760*H//W}">\n' + "\n".join(parts) + "\n</svg>\n"
)
open("pcb_silk_preview.svg", "w").write(svg)
print("wrote pcb_silk_preview.svg  labels:", len(labels))
