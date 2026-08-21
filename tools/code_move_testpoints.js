// Move the 11 unused-GPIO test-point vias into the former OUT1-8 column
// (rightmost area, x=2556), as a single vertical column extended downward.
const PCB_UUID = "31a230f467289f63";
await eda.dmt_EditorControl.openDocument(PCB_UUID);

const allVias = await eda.pcb_PrimitiveVia.getAll();
const gid = (pr) => { try { return pr.primitiveId; } catch (e) { try { return pr.id; } catch (e2) { return null; } } };
const g = (pr, n) => { try { return pr[n](); } catch (e) { return null; } };

// 1) delete old test points (dia ~40, the only dia<50 vias on the board)
let del = 0;
for (const v of allVias) {
  const dia = g(v, "getState_Diameter") || 40;
  if (dia < 50) { await eda.pcb_PrimitiveVia.delete(gid(v)); del++; }
}

// 2) recreate 11 test points in the former OUT1-8 column (x=2556, y=560..1560 step 100)
const COL_X = 2556;
const nets = ["GPIO_1","GPIO_3","GPIO_18","GPIO_19","GPIO_23",
              "GPIO_32","GPIO_33","GPIO_34","GPIO_35","GPIO_36","GPIO_39"];
const placed = [];
for (let i = 0; i < nets.length; i++) {
  const y = 560 + i * 100;
  await eda.pcb_PrimitiveVia.create(nets[i], COL_X, y, 20, 40, 0, false, null);
  placed.push({ net: nets[i], x: COL_X, y });
}

await eda.pcb_Document.save();
return JSON.stringify({ deletedOld: del, placed });
