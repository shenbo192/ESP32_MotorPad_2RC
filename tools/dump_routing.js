// dump routing primitives from the live PCB for SVG preview
const PCB_UUID = "31a230f467289f63";
await eda.dmt_EditorControl.openDocument(PCB_UUID);
const lines = await eda.pcb_PrimitiveLine.getAll();
const vias = await eda.pcb_PrimitiveVia.getAll();
function g(pr, n) { try { return pr[n](); } catch (e) { return null; } }
const out = { lines: [], vias: [] };
for (const l of lines) {
  out.lines.push({
    net: g(l, "getState_Net"), layer: g(l, "getState_Layer"),
    x1: g(l, "getState_StartX"), y1: g(l, "getState_StartY"),
    x2: g(l, "getState_EndX"), y2: g(l, "getState_EndY"),
    w: g(l, "getState_LineWidth")
  });
}
for (const v of vias) {
  out.vias.push({
    net: g(v, "getState_Net"), x: g(v, "getState_X"), y: g(v, "getState_Y"),
    dia: g(v, "getState_Diameter"), hole: g(v, "getState_HoleDiameter")
  });
}
return JSON.stringify(out);
