// Remove the 8 OUT1-OUT8 motor-output holes (single-pin nets) from the base board.
// Motors will be wired directly to the L298N module's own terminals instead.
// Keep everything else (VBAT, connectors, mount holes, GPIO test points).
const PCB_UUID = "31a230f467289f63";
await eda.dmt_EditorControl.openDocument(PCB_UUID);

const OUT = new Set(["OUT1","OUT2","OUT3","OUT4","OUT5","OUT6","OUT7","OUT8"]);
const vias = await eda.pcb_PrimitiveVia.getAll();
let del = 0;
for (const v of vias) {
  const id = (v && (v.primitiveId !== undefined ? v.primitiveId : v.id));
  let net = null;
  try { net = v.getState_Net(); } catch (e) {}
  if (id !== undefined && id !== null && net && OUT.has(net)) {
    try { await eda.pcb_PrimitiveVia.delete(id); del++; } catch (e) {}
  }
}
await eda.pcb_Document.save();
// verify remaining
const rest = (await eda.pcb_PrimitiveVia.getAll());
const stillOut = rest.filter(v => { try { return OUT.has(v.getState_Net()); } catch(e){ return false; } });
return JSON.stringify({ deleted: del, remainingVias: rest.length, outLeft: stillOut.length });
