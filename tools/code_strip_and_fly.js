// Strip all my routed tracks + my routing vias, back to bare pads.
// Then add one outward test-point via per unused GPIO pin so a flying wire (ratsnest)
// appears for it, so the user can route those spare pins themselves.
const PCB_UUID = "31a230f467289f63";
await eda.dmt_EditorControl.openDocument(PCB_UUID);

const allLines = await eda.pcb_PrimitiveLine.getAll();
const allVias  = await eda.pcb_PrimitiveVia.getAll();
const gid = (pr) => { try { return pr.primitiveId; } catch (e) { try { return pr.id; } catch (e2) { return null; } } };
const g = (pr, n) => { try { return pr[n](); } catch (e) { return null; } };

// 1) delete every track line I drew (board outline is a Polyline, not a Line -> untouched)
let delLines = 0;
for (const l of allLines) { await eda.pcb_PrimitiveLine.delete(gid(l)); delLines++; }

// 2) delete my routing vias (dia ~40). Keep connector(70) + mounting(138) pads.
let delVias = 0;
for (const v of allVias) {
  const dia = g(v, "getState_Diameter") || 40;
  if (Math.abs(dia - 40) < 1) { await eda.pcb_PrimitiveVia.delete(gid(v)); delVias++; }
}

// 3) for each unused GPIO pin (dia 70, net like GPIO_*), add an outward test-point via
const MOD_CX = 1378;   // ESP32 module centre x (span 878..1878)
const OFF = 150;        // mils outward from the pin
const added = [];
for (const v of allVias) {
  const dia = g(v, "getState_Diameter") || 40;
  const net = g(v, "getState_Net");
  const x = g(v, "getState_X"), y = g(v, "getState_Y");
  if (Math.abs(dia - 70) < 1 && net && net.startsWith("GPIO_")) {
    const nx = x <= MOD_CX ? x - OFF : x + OFF;   // outward from module centre
    await eda.pcb_PrimitiveVia.create(net, nx, y, 20, 40, 0, false, null);
    added.push({ net, x: nx, y });
  }
}

await eda.pcb_Document.save();
return JSON.stringify({ delLines, delVias, addedTestPoints: added });
