import json

# ---- 1. 网名 -> 引脚号（简短） ----
def to_pin(net):
    if net is None:
        return None
    if net.startswith("GPIO_"):
        return net[len("GPIO_"):]          # GPIO_23 -> 23
    m = {
        "FRONT_IN1":"IN1", "FRONT_IN2":"IN2", "FRONT_ENA":"EN1",
        "MIDDLE_IN1":"IN3", "MIDDLE_IN2":"IN4", "MIDDLE_ENA":"EN2",
        "REAR_IN1":"IN5",   "REAR_IN2":"IN6",  "REAR_ENA":"EN3",
        "STEER_IN1":"IN7",  "STEER_IN2":"IN8", "STEER_ENA":"EN4",
        "LED_STRIP":"21", "STATUS_LED":"22",
        "EN":"EN", "3V3":"3V3", "5V":"5V", "GND":"GND",
        "5V_MOD":"5VM", "VBAT":"VBAT",
    }
    return m.get(net, net)

# ---- 2. 读孔 ----
w = json.load(open("tools/current_dump.json"))
d = w["result"]
if isinstance(d, str):
    d = json.loads(d)
vias = d["vias"]
for v in vias:
    for k in ("x", "y", "dia", "hole"):
        v[k] = float(v[k]) if v[k] is not None else 0.0
    v["net"] = v.get("net")

cx, cy = 1378, 1260
FS = 22          # 字号(mil)
LW = 3           # 线宽(mil)
charW = FS * 0.60
h = FS + 6

def wbox(t):
    return len(t) * charW + 6

def box(lx, ly, align, w_, h_):
    if align == 2:
        return (lx, lx + w_, ly - h_/2, ly + h_/2)
    if align == 8:
        return (lx - w_, lx, ly - h_/2, ly + h_/2)
    return (lx - w_/2, lx + w_/2, ly - h_/2, ly + h_/2)

def inter(a, b):
    return not (a[1] <= b[0] + 2 or a[0] >= b[1] - 2 or a[3] <= b[2] + 2 or a[2] >= b[3] - 2)

labels = []
for v in vias:
    px, py, dia, net = v["x"], v["y"], v["dia"], v["net"]
    padR = dia / 2
    txt = to_pin(net)
    dx, dy = px - cx, py - cy
    if px <= 360:
        base_x = px + padR + 10
        align = 2
        base_y = py
    else:
        if abs(dx) >= abs(dy):
            side = "left" if dx < 0 else "right"
        else:
            side = "above" if dy < 0 else "below"
        if side == "left":
            base_x, align = px - padR - 10, 8
        elif side == "right":
            base_x, align = px + padR + 10, 2
        else:
            base_x, align = px, 5
        if side in ("left", "right"):
            base_y = py
        elif side == "above":
            base_y = py - padR - 10 - 14
        else:
            base_y = py + padR + 10 + 14
    labels.append({"px": px, "py": py, "txt": txt, "bx": base_x, "by": base_y, "align": align})

labels.sort(key=lambda L: (L["bx"], L["by"]))
placed, final = [], []
for L in labels:
    w_ = wbox(L["txt"])
    bx, by, al = L["bx"], L["by"], L["align"]
    chosen = None
    for k in range(0, 8):
        for dyo in ([0, k*30, -k*30] if k > 0 else [0]):
            nx, ny = bx, by + dyo
            b = box(nx, ny, al, w_, h)
            if b[0] < 10 or b[1] > 2746 or b[2] < 10 or b[3] > 2510:
                continue
            if not any(inter(b, p) for p in placed):
                chosen = (nx, ny)
                break
        if chosen:
            break
    if not chosen:
        chosen = (bx, by)
    b = box(chosen[0], chosen[1], al, w_, h)
    placed.append(b)
    final.append({"x": round(chosen[0]), "y": round(chosen[1]), "t": L["txt"], "a": al})

json.dump(final, open("tools/silk_labels.json", "w"), ensure_ascii=False)
print("labels:", len(final))
from collections import Counter
print("align:", Counter(f["a"] for f in final))
print("sample:", final[:8])

# ---- 3. 生成重建脚本（先删 layer3 全部字符串，再写新，最后 save） ----
arr = json.dumps(final, ensure_ascii=False)
js = (
    'const PCB_UUID = "31a230f467289f63";\n'
    'await eda.dmt_EditorControl.openDocument(PCB_UUID);\n'
    'const labels = ' + arr + ';\n'
    '// 删旧\n'
    'let del=0;\n'
    'const old = await eda.pcb_PrimitiveString.getAll();\n'
    'for (const s of old) {\n'
    '  const ly = s.getState_Layer ? s.getState_Layer() : null;\n'
    '  if (ly === 3) { try { await eda.pcb_PrimitiveString.delete(s); del++; } catch(e){} }\n'
    '}\n'
    'let ok=0, fail=0; const fails=[];\n'
    'for (const L of labels) {\n'
    '  try {\n'
    '    const s = await eda.pcb_PrimitiveString.create(3, L.x, L.y, L.t);\n'
    '    if (!s) { fail++; fails.push(L.t+":null"); continue; }\n'
    '    if (s.setState_FontSize) s.setState_FontSize(22);\n'
    '    if (s.setState_LineWidth) s.setState_LineWidth(3);\n'
    '    if (s.setState_AlignMode) s.setState_AlignMode(L.a);\n'
    '    ok++;\n'
    '  } catch(e){ fail++; fails.push(L.t+":"+e.message); }\n'
    '}\n'
    'try { await eda.pcb_Document.save(); } catch(e){ fails.push("save:"+e.message); }\n'
    'return JSON.stringify({deleted: del, ok, fail, fails});\n'
)
open("tools/code_silk_rebuild.js", "w").write(js)
print("wrote code_silk_rebuild.js")
