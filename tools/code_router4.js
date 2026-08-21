// router4 — clearance-aware 2-layer router (fixes DRC spacing + orphan errors)
// Key fixes vs router3:
//  - seed BOTH layers for every endpoint (escape to open bottom layer)
//  - foreign tracks block ONLY their own layer (don't wall off layer 1)
//  - foreign connector vias block both layers (they are through-hole)
const PCB_UUID = "31a230f467289f63";
await eda.dmt_EditorControl.openDocument(PCB_UUID);

const CELL = 10;
const W = 2756, H = 2520;            // board size (mil)
const GW = Math.round(W / CELL) + 1;
const GH = Math.round(H / CELL) + 1;
const CLEAR_VIA = 6;                  // via-vs-obstacle clearance (exact DRC; vias have no corner clip)
const CLEAR_TRACK = 6;                // track-vs-obstacle clearance (exact DRC spec; over-blocking walls off dense clusters)
const GEO_MIN = 6;                    // geometric clearance acceptance threshold (DRC spec)
const VIA_PEN = 8;                    // cost of a layer-switch via
const VIA_HOLE = 20, VIA_DIA = 40;    // routing via size
const TRACK_W = { "5V": 20, "5V_MOD": 20, "GND": 20 };
const SKIP_COMMIT = false;   // commit to board

function cx(x) { return Math.round(x / CELL); }
function cy(y) { return Math.round(y / CELL); }
function cxi(i) { return i * CELL; }
function idx(x, y) { return y * GW + x; }
function inBoard(x, y) { return x >= 0 && x <= GW - 1 && y >= 0 && y <= GH - 1; }
function dec(s) { const l = s % 2; const r = (s - l) / 2; return [r % GW, Math.floor(r / GW), l]; }
function stateIndex(x, y, l) { return (y * GW + x) * 2 + l; }

// ---------- read current primitives ----------
const allLines = await eda.pcb_PrimitiveLine.getAll();
const allVias  = await eda.pcb_PrimitiveVia.getAll();

function g(pr, n) { try { return pr[n](); } catch (e) { return null; } }
function gid(pr) { try { return pr.primitiveId; } catch (e) { try { return pr.id; } catch (e2) { return null; } } }

// classify vias: keep connector(70) + mounting(138); delete routing(40)
const keepVias = [], delVias = [];
for (const v of allVias) {
  const dia = g(v, "getState_Diameter") || 40;
  const o = { id: gid(v), net: g(v, "getState_Net"), x: g(v, "getState_X"), y: g(v, "getState_Y"),
              hole: g(v, "getState_HoleDiameter"), dia: dia };
  if (Math.abs(dia - 40) < 1) delVias.push(o); else keepVias.push(o);
}

// ---------- build via map from kept vias ----------
const viaMap = new Map();           // key cx,cy -> via
for (const v of keepVias) {
  const X = cx(v.x), Y = cy(v.y);
  viaMap.set(X + "," + Y, v);
}

// nets from kept vias
const viaByNet = {};
for (const v of keepVias) (viaByNet[v.net] = viaByNet[v.net] || []).push({ x: v.x, y: v.y, X: cx(v.x), Y: cy(v.y), dia: v.dia, hole: v.hole });
const netList = Object.keys(viaByNet);
const netId = {}; netList.forEach((n, i) => netId[n] = i + 1);

// committed tracks (net N) for clearance against OTHER nets; each: {x1,y1,x2,y2,layer,net,w}
const committedTracks = [];
const DBG2 = { switchPushed: 0, l1popped: 0, punch: 0 };

const failures = [];
const newSegments = [];   // {x1,y1,x2,y2,layer,net,w}
const newVias = [];       // {x,y,net} (routing vias only)

// Euclidean clearance halo stamped at the obstacle's REAL mil coordinate (not the
// grid-rounded cell). Rounding a via to its cell center can be off by up to 5mil,
// which silently under-blocks and produces <6mil clips. Stamping from the true
// coordinate makes the maze's blocked map agree exactly with the geometric check.
function stampDisk(arr, Xm, Ym, rMils) {
  const r2 = rMils * rMils;
  const x0 = Math.max(0, Math.floor((Xm - rMils) / CELL));
  const x1 = Math.min(GW - 1, Math.ceil((Xm + rMils) / CELL));
  const y0 = Math.max(0, Math.floor((Ym - rMils) / CELL));
  const y1 = Math.min(GH - 1, Math.ceil((Ym + rMils) / CELL));
  for (let yy = y0; yy <= y1; yy++) {
    for (let xx = x0; xx <= x1; xx++) {
      const mx = xx * CELL, my = yy * CELL;       // cell-center in mils
      const dx = mx - Xm, dy = my - Ym;
      if (dx * dx + dy * dy > r2) continue;
      arr[idx(xx, yy)] = 1;
    }
  }
}
function stampLineMils(arr, x1, y1, x2, y2, rMils) {
  const len = Math.hypot(x2 - x1, y2 - y1);
  const steps = Math.max(1, Math.ceil(len / CELL));
  for (let s = 0; s <= steps; s++) {
    const t = s / steps;
    stampDisk(arr, x1 + (x2 - x1) * t, y1 + (y2 - y1) * t, rMils);
  }
}

// Two obstacle maps per layer:
//   b*  = TRACK-blocked  (used for 4-neighbour movement; thin radius -> less boxing-in)
//   vb* = VIA-blocked    (used for layer-switch punch-through; thick radius -> vias stay clear)
// Radii are in MILS and stamped at real coordinates, so dense clusters keep their
// true DRC gaps and the maze never routes a track into a <6mil halo.
function buildBlocked(netN, wN, ct, cv) {
  if (ct === undefined) ct = CLEAR_TRACK;
  if (cv === undefined) cv = CLEAR_VIA;
  const b0 = new Uint8Array(GW * GH), b1 = new Uint8Array(GW * GH);
  const vb0 = new Uint8Array(GW * GH), vb1 = new Uint8Array(GW * GH);
  const obs = keepVias.concat(newVias.map(function (v) { return { x: v.x, y: v.y, net: v.net, dia: VIA_DIA }; }));
  for (const v of obs) {
    if (netId[v.net] === netN) continue;
    const rT = v.dia / 2 + wN / 2 + ct;       // mils
    const rV = v.dia / 2 + VIA_DIA / 2 + cv;   // mils
    stampDisk(b0, v.x, v.y, rT); stampDisk(b1, v.x, v.y, rT);
    stampDisk(vb0, v.x, v.y, rV); stampDisk(vb1, v.x, v.y, rV);
  }
  for (const t of committedTracks) {
    if (netId[t.net] === netN) continue;
    const rT = t.w / 2 + wN / 2 + ct;
    const rV = t.w / 2 + VIA_DIA / 2 + cv;
    const arrT = t.layer === 0 ? b0 : b1;
    const arrV = t.layer === 0 ? vb0 : vb1;
    stampLineMils(arrT, t.x1, t.y1, t.x2, t.y2, rT);
    stampLineMils(arrV, t.x1, t.y1, t.x2, t.y2, rV);
  }
  for (let yy = 0; yy < GH; yy++) for (let xx = 0; xx < GW; xx++) {
    if (xx === 0 || yy === 0 || xx === GW - 1 || yy === GH - 1) {
      b0[idx(xx, yy)] = 1; b1[idx(xx, yy)] = 1; vb0[idx(xx, yy)] = 1; vb1[idx(xx, yy)] = 1;
    }
  }
  return [b0, b1, vb0, vb1];
}

// binary min-heap
function Heap() { this.a = []; }
Heap.prototype.push = function (node) {
  const a = this.a; a.push(node); let i = a.length - 1;
  while (i > 0) { const p = (i - 1) >> 1; if (a[p].d <= a[i].d) break; const t = a[p]; a[p] = a[i]; a[i] = t; i = p; }
};
Heap.prototype.pop = function () {
  const a = this.a; if (a.length === 0) return null; const top = a[0]; const last = a.pop();
  if (a.length > 0) { a[0] = last; let i = 0; const n = a.length;
    while (true) { let l = 2 * i + 1, r = 2 * i + 2, m = i;
      if (l < n && a[l].d < a[m].d) m = l; if (r < n && a[r].d < a[m].d) m = r;
      if (m === i) break; const t = a[m]; a[m] = a[i]; a[i] = t; i = m; } }
  return top;
};
Heap.prototype.size = function () { return this.a.length; };

function dijkstra(blocked, vblocked, sources) {
  const N = GW * GH * 2;
  const dist = new Float64Array(N).fill(Infinity);
  const prev = new Int32Array(N).fill(-1);
  const heap = new Heap();
  for (const s of sources) {
    if (dist[s] > 0) { dist[s] = 0; heap.push({ d: 0, s }); }
  }
  const closed = new Uint8Array(N);
  while (heap.size() > 0) {
    const cur = heap.pop(); const u = cur.s;
    if (closed[u]) continue; closed[u] = 1;
    const ul = u % 2;
    if (ul === 1) DBG2.l1popped++;
    const r = (u - ul) / 2;
    const x = r % GW, y = Math.floor(r / GW);
    // 4-neighbors same layer
    const nbx = [x + 1, x - 1, x, x];
    const nby = [y, y, y + 1, y - 1];
    for (let ni = 0; ni < 4; ni++) {
      const nx = nbx[ni], ny = nby[ni];
      if (nx < 0 || ny < 0 || nx >= GW || ny >= GH) continue;
      if (blocked[ul][idx(nx, ny)]) continue;
      const v = (ny * GW + nx) * 2 + ul;
      const nd = dist[u] + 1;
      if (nd < dist[v]) { dist[v] = nd; prev[v] = u; heap.push({ d: nd, s: v }); }
    }
    // layer switch (punch-through) — needs BOTH layers free in the VIA-blocked map
    const ol = 1 - ul;
    if (vblocked[ul][idx(x, y)] === 0 && vblocked[ol][idx(x, y)] === 0) {
      const v = (y * GW + x) * 2 + ol;
      const nd = dist[u] + VIA_PEN;
      if (nd < dist[v]) { dist[v] = nd; prev[v] = u; heap.push({ d: nd, s: v }); DBG2.switchPushed++; }
    }
  }
  return { dist, prev };
}

// attempt to route one net at a given clearance; returns true on success or a
// failure-detail string on failure (caller rolls back between fallbacks).
// wOverride lets GND fall back to a thinner trace when 20mil cannot close in a
// congested board (connectivity beats fatness for a prototype reference plane).
function attemptRoute(net, ct, cv, wOverride) {
  const N = netId[net];
  const eps = viaByNet[net];
  if (!eps || eps.length < 2) return true; // single-pad net: nothing to route
  const wN = wOverride || (TRACK_W[net] || 10);
  const _seg0 = newSegments.length, _trk0 = committedTracks.length, _via0 = newVias.length;
  const bk = buildBlocked(N, wN, ct, cv);
  const b0 = bk[0], b1 = bk[1], vb0 = bk[2], vb1 = bk[3];
  const blocked = [b0, b1], vblocked = [vb0, vb1];

  // Pick a root endpoint that can actually reach at least one other endpoint.
  // (A via can be boxed in on both layers; trying each endpoint avoids false failures.)
  let chosenRoot = -1;
  for (let r0 = 0; r0 < eps.length; r0++) {
    const src = [stateIndex(eps[r0].X, eps[r0].Y, 0), stateIndex(eps[r0].X, eps[r0].Y, 1)];
    const probe = dijkstra(blocked, vblocked, src);
    let reachable = false;
    for (let ei = 0; ei < eps.length; ei++) {
      if (ei === r0) continue;
      if (isFinite(probe.dist[stateIndex(eps[ei].X, eps[ei].Y, 0)]) || isFinite(probe.dist[stateIndex(eps[ei].X, eps[ei].Y, 1)])) { reachable = true; break; }
    }
    if (reachable) { chosenRoot = r0; break; }
  }
  if (chosenRoot < 0) {
    // Replicate the maze reachability from endpoint 0 to see if it's a true pocket.
    const seen = new Uint8Array(GW * GH * 2);
    const q = [stateIndex(eps[0].X, eps[0].Y, 0), stateIndex(eps[0].X, eps[0].Y, 1)];
    for (const s of q) seen[s] = 1;
    let reached = 0, minD1 = Infinity;
    while (q.length) {
      const u = q.pop(); reached++;
      const ul = u % 2; const r = (u - ul) / 2; const x = r % GW, y = Math.floor(r / GW);
      const d1 = Math.abs(x - eps[1].X) + Math.abs(y - eps[1].Y);
      if (d1 < minD1) minD1 = d1;
      const nbx = [x + 1, x - 1, x, x], nby = [y, y, y + 1, y - 1];
      for (let ni = 0; ni < 4; ni++) {
        const nx = nbx[ni], ny = nby[ni];
        if (nx < 0 || ny < 0 || nx >= GW || ny >= GH) continue;
        const v = (ny * GW + nx) * 2 + ul;
        if (seen[v]) continue;
        if (blocked[ul][idx(nx, ny)]) continue;
        seen[v] = 1; q.push(v);
      }
      const ol = 1 - ul;
      if (vblocked[ul][idx(x, y)] === 0 && vblocked[ol][idx(x, y)] === 0) {
        const v2 = (y * GW + x) * 2 + ol;
        if (!seen[v2]) { seen[v2] = 1; q.push(v2); }
      }
    }
    const diag = { x0: eps[0].x, y0: eps[0].y, x1: eps[1].x, y1: eps[1].y,
      reachedCells: reached, minCellDistToEp1: minD1,
      ep1Reached: seen[stateIndex(eps[1].X, eps[1].Y, 0)] === 1 || seen[stateIndex(eps[1].X, eps[1].Y, 1)] === 1 };
    newSegments.length = _seg0; committedTracks.length = _trk0; newVias.length = _via0;
    return "net " + net + " unreachable (boxed) " + JSON.stringify(diag);
  }

  const visited = new Set([chosenRoot]);
  const sources = [stateIndex(eps[chosenRoot].X, eps[chosenRoot].Y, 0), stateIndex(eps[chosenRoot].X, eps[chosenRoot].Y, 1)];

  let guard = 0;
  while (visited.size < eps.length && guard++ < 400) {
    const { dist, prev } = dijkstra(blocked, vblocked, sources);
    // find nearest unvisited endpoint (min over both layers)
    let best = -1, bestEp = -1, bestLayer = 0, bestD = Infinity;
    let minFinD = Infinity, minFinEp = -1;
    for (let ei = 0; ei < eps.length; ei++) {
      if (visited.has(ei)) continue;
      for (let l = 0; l < 2; l++) {
        const s = stateIndex(eps[ei].X, eps[ei].Y, l);
        if (dist[s] < bestD) { bestD = dist[s]; best = s; bestEp = ei; bestLayer = l; }
        if (isFinite(dist[s]) && dist[s] < minFinD) { minFinD = dist[s]; minFinEp = ei; }
      }
    }
    if (best < 0 || !isFinite(bestD)) { newSegments.length = _seg0; committedTracks.length = _trk0; newVias.length = _via0; return "net " + net + " unreachable (nearest finite dist=" + (isFinite(minFinD) ? minFinD : "none") + " at ep#" + minFinEp + ")"; }
    // reconstruct
    const path = [];
    let cur = best;
    while (cur !== -1) { path.push(cur); cur = prev[cur]; }
    path.reverse();
    // commit path: emit track segments + vias
    let prevState = null, segStart = null, segLayer = null, segDir = null;
    const flush = () => {
      if (segStart && prevState && segStart !== prevState) {
        const ds = dec(segStart), dp = dec(prevState);
        newSegments.push({ x1: cxi(ds[0]), y1: cxi(ds[1]), x2: cxi(dp[0]), y2: cxi(dp[1]), layer: segLayer, net, w: wN });
        committedTracks.push({ x1: cxi(ds[0]), y1: cxi(ds[1]), x2: cxi(dp[0]), y2: cxi(dp[1]), layer: segLayer, net, w: wN });
      }
    };
    for (let i = 0; i < path.length; i++) {
      const s = path[i];
      const d = dec(s); const x = d[0], y = d[1], l = d[2];
      if (i === 0) { segStart = s; segLayer = l; }
      else {
        const pd = dec(prevState); const px = pd[0], py = pd[1], pl = pd[2];
        if (l !== pl) {
          const k = x + "," + y;
          if (!viaMap.has(k)) {
            newVias.push({ x: cxi(x), y: cxi(y), net });
            viaMap.set(k, { net, x: cxi(x), y: cxi(y), dia: VIA_DIA, hole: VIA_HOLE, routing: true });
          }
          flush(); segStart = s; segLayer = l;
        } else {
          const dx = x - px, dy = y - py;
          if (dx !== 0 || dy !== 0) {
            if (segDir && (dx !== segDir.dx || dy !== segDir.dy)) { flush(); segStart = prevState; }
          }
          segDir = { dx: dx, dy: dy };
        }
      }
      prevState = s;
    }
    flush();
    visited.add(bestEp);
    sources.push(stateIndex(eps[bestEp].X, eps[bestEp].Y, 0));
    sources.push(stateIndex(eps[bestEp].X, eps[bestEp].Y, 1));
  }
  return true;
}

// route one net at the exact DRC clearance. The geometry post-check rejects anything <6mil.
// GND falls back to 10mil if its 20mil loop cannot close in a congested board.
function routeNet(net) {
  let r = attemptRoute(net, CLEAR_TRACK, CLEAR_VIA);
  if (r !== true && net === "GND") r = attemptRoute(net, CLEAR_TRACK, CLEAR_VIA, 10);
  if (r !== true) failures.push(r);
}

// route all nets: power nets buried in the dense H3 cluster (5V_MOD, 5V) go FIRST so
// they claim their corridors before the cluster's signal pins fill in. Signals run
// next, thin, weaving through gaps. GND is LAST with a 10mil fallback so its reference
// plane still closes even in a congested board (connectivity beats fatness here).
function prio(n) { if (n === "5V_MOD") return 0; if (n === "5V") return 1; if (n === "GND") return 3; return 2; }
const order = netList.slice().sort((a, b) => {
  const pa = prio(a), pb = prio(b);
  if (pa !== pb) return pa - pb;
  const ca = (viaByNet[a] || []).length, cb = (viaByNet[b] || []).length;
  if (ca !== cb) return ca - cb;
  return (TRACK_W[a] || 10) - (TRACK_W[b] || 10);
});
for (const net of order) routeNet(net);

// ---------- validate plan: connectivity + clearance (geometric) ----------
function segDist(px, py, x1, y1, x2, y2) {
  const dx = x2 - x1, dy = y2 - y1;
  if (dx === 0 && dy === 0) return Math.hypot(px - x1, py - y1);
  let t = ((px - x1) * dx + (py - y1) * dy) / (dx * dx + dy * dy); t = Math.max(0, Math.min(1, t));
  return Math.hypot(px - (x1 + t * dx), py - (y1 + t * dy));
}
function segSegDist(a, b) {
  return Math.min(segDist(a.x1, a.y1, b.x1, b.y1, b.x2, b.y2), segDist(a.x2, a.y2, b.x1, b.y1, b.x2, b.y2),
                  segDist(b.x1, b.y1, a.x1, a.y1, a.x2, a.y2), segDist(b.x2, b.y2, a.x1, a.y1, a.x2, a.y2));
}
function geoCheck() {
  const viasAll = [...keepVias.map(v => ({ ...v, routing: false })),
                   ...newVias.map(v => ({ x: v.x, y: v.y, net: v.net, dia: VIA_DIA, hole: VIA_HOLE, routing: true }))];
  const tracks = newSegments;
  const disconn = [];
  for (const net of netList) {
    const N = netId[net];
    const epsN = viaByNet[net];
    if (!epsN || epsN.length < 2) continue;
    const adj = new Map();
    const addE = (a, b) => {
      if (!adj.has(a)) adj.set(a, new Set()); adj.get(a).add(b);
      if (!adj.has(b)) adj.set(b, new Set()); adj.get(b).add(a);
    };
    for (const t of tracks) {
      if (t.net !== net) continue;
      addE(t.layer + ":" + cx(t.x1) + "," + cy(t.y1), t.layer + ":" + cx(t.x2) + "," + cy(t.y2));
    }
    for (const v of viasAll) {
      if (v.net !== net) continue;
      const X = cx(v.x), Y = cy(v.y);
      addE("0:" + X + "," + Y, "1:" + X + "," + Y);
    }
    const start = "0:" + epsN[0].X + "," + epsN[0].Y;
    const seen = new Set([start]); const q = [start];
    while (q.length) { const n = q.shift(); for (const m of (adj.get(n) || [])) if (!seen.has(m)) { seen.add(m); q.push(m); } }
    for (let i = 0; i < epsN.length; i++) {
      const X = epsN[i].X, Y = epsN[i].Y;
      if (!seen.has("0:" + X + "," + Y) && !seen.has("1:" + X + "," + Y)) disconn.push(net + "#" + i);
    }
  }
  const violVT = [], violVV = [], violTT = [];
  for (const v of viasAll) {
    if (v.net == null) continue;
    for (const t of tracks) {
      if (t.net === v.net || t.net == null) continue;
      const dd = segDist(v.x, v.y, t.x1, t.y1, t.x2, t.y2);
      const gap = dd - (v.dia / 2) - (t.w / 2);
      if (gap < GEO_MIN) violVT.push({ net: v.net + "/" + t.net, gap: +gap.toFixed(2),
        via: { x: +v.x.toFixed(1), y: +v.y.toFixed(1), dia: v.dia, net: v.net },
        track: { x1: t.x1, y1: t.y1, x2: t.x2, y2: t.y2, layer: t.layer, net: t.net, w: t.w } });
    }
  }
  for (let i = 0; i < viasAll.length; i++) for (let j = i + 1; j < viasAll.length; j++) {
    const a = viasAll[i], b = viasAll[j];
    if (a.net === b.net || a.net == null || b.net == null) continue;
    const dd = Math.hypot(a.x - b.x, a.y - b.y);
    const gap = dd - a.dia / 2 - b.dia / 2;
    if (gap < GEO_MIN) violVV.push({ net: a.net + "/" + b.net, gap: +gap.toFixed(2) });
  }
  for (let i = 0; i < tracks.length; i++) for (let j = i + 1; j < tracks.length; j++) {
    const a = tracks[i], b = tracks[j];
    if (a.net === b.net || a.net == null || b.net == null) continue;
    if (a.layer !== b.layer) continue;
    const dd = segSegDist(a, b);
    const gap = dd - a.w / 2 - b.w / 2;
    if (gap < GEO_MIN) violTT.push({ net: a.net + "/" + b.net, gap: +gap.toFixed(2) });
  }
  return { disconn, violVT: violVT.length, violVV: violVV.length, violTT: violTT.length,
           violVTsample: violVT.slice(0, 8), violVVsample: violVV.slice(0, 8), violTTsample: violTT.slice(0, 8) };
}

const check = geoCheck();

// ---------- commit to board ONLY if clean ----------
let commitResult = "skipped (plan not clean)";
if (!SKIP_COMMIT && failures.length === 0 && check.disconn.length === 0 && check.violVT === 0 && check.violVV === 0 && check.violTT === 0) {
  let delLines = 0, delViasN = 0;
  for (const l of allLines) { await eda.pcb_PrimitiveLine.delete(gid(l)); delLines++; }
  for (const v of delVias) { await eda.pcb_PrimitiveVia.delete(v.id); delViasN++; }
  let madeLines = 0;
  // Snap each track endpoint onto its own net's via EXACT coordinate when already
  // within 8mil. This makes LCEDA see a real pad connection and stops it from
  // re-stitching the net (which draws straight tracks that can short foreign vias).
  const viaPts = keepVias.concat(newVias.map(function (v) { return { x: v.x, y: v.y, net: v.net }; }));
  function snapPt(x, y, net) {
    for (const v of viaPts) {
      if (v.net === net && Math.abs(v.x - x) <= 8 && Math.abs(v.y - y) <= 8) return [v.x, v.y];
    }
    return [x, y];
  }
  for (const s of newSegments) {
    // Line.create is 0-indexed and direct: 0=TOP, 1=BOTTOM (matches getState_Layer).
    // A pad-style 1/2 mapping would flip/mis-map every layer — do NOT use it here.
    const createLayer = s.layer;
    const p1 = snapPt(s.x1, s.y1, s.net), p2 = snapPt(s.x2, s.y2, s.net);
    await eda.pcb_PrimitiveLine.create(s.net, createLayer, p1[0], p1[1], p2[0], p2[1], s.w, false);
    madeLines++;
  }
  let madeVias = 0;
  for (const v of newVias) {
    await eda.pcb_PrimitiveVia.create(v.net, v.x, v.y, VIA_HOLE, VIA_DIA, 0, false, null);
    madeVias++;
  }
  await eda.pcb_Document.save();
  commitResult = { delLines, delViasN, madeLines, madeVias, saved: true };
}

const layerDist = {};
for (const s of newSegments) layerDist[s.layer] = (layerDist[s.layer] || 0) + 1;
const out = {
  keepVias: keepVias.length, delVias: delVias.length,
  routedNets: order.length, newSegments: newSegments.length, newVias: newVias.length,
  failures, check, commitResult,
  dbg2: DBG2, layerDist: layerDist,
  segments: newSegments,
  vias: keepVias.concat(newVias.map(function (v) { return { x: v.x, y: v.y, net: v.net, dia: VIA_DIA, hole: VIA_HOLE }; }))
};
return JSON.stringify(out);
