// remote-pricing-sync.js
// Pull machine play-prices from a remote pricing provider (a configurable HTTPS
// endpoint that returns a per-location machine list) and apply them to the local
// in-memory db. Provider-agnostic: URL + bearer token + a generic JSON contract.
// Outbound-only (never opens ports); fail-quiet (keeps last-known prices on error).
//
// config.json:
//   "remote_pricing": {
//     "enabled": true,
//     "interval_sec": 60,
//     "locations": [
//       { "label": "Main floor", "url": "https://<host>/api/credit-sync/pricing", "token": "<site-token>" }
//     ]
//   }
//   (A single top-level { "url", "token" } instead of "locations" is also accepted.)
//
// Expected response per location:
//   { "machines": [ { "externalDeviceId": "AA:BB:CC:...", "name": "Cherry Master", "cost": 2, "freeplay": false }, ... ] }
//   externalDeviceId = the reader MAC (uppercased before indexing db.machines);
//   cost = credits per play; null = leave the local price as-is;
//   name = machine display name (surfaced in server logs, the UI, and to the reader).

function normalizeLocations(rp) {
  if (!rp) return [];
  if (Array.isArray(rp.locations)) return rp.locations.filter((l) => l && l.url && l.token);
  if (rp.url && rp.token) return [{ url: rp.url, token: rp.token, label: rp.label }];
  return [];
}

async function syncLocation(loc, db, log) {
  const where = loc.label || loc.url;
  let res;
  try {
    res = await fetch(loc.url, { headers: { Authorization: `Bearer ${loc.token}` } });
  } catch (e) {
    log(`[remote-pricing] ${where}: fetch failed (${e.message}) — keeping current prices`);
    return 0;
  }
  if (!res.ok) {
    log(`[remote-pricing] ${where}: HTTP ${res.status} — keeping current prices`);
    return 0;
  }
  let data;
  try { data = await res.json(); } catch (e) {
    log(`[remote-pricing] ${where}: invalid JSON (${e.message})`);
    return 0;
  }

  db.machines = db.machines || {};
  let changes = 0;
  for (const row of (data.machines || [])) {
    const mac = String(row.externalDeviceId || '').toUpperCase();
    if (!mac) continue; // machine not mapped to a reader yet
    const m = (db.machines[mac] = db.machines[mac] || {});
    const freeplay = !!row.freeplay;
    if (m.free_play !== freeplay) { m.free_play = freeplay; changes++; }
    if (!freeplay && row.cost != null && Number(row.cost) >= 0 && m.cost !== Number(row.cost)) {
      m.cost = Number(row.cost);
      changes++;
    }
    if (row.name != null && m.name !== String(row.name)) { m.name = String(row.name); changes++; }
  }
  if (changes) log(`[remote-pricing] ${where}: applied ${changes} change(s) across ${(data.machines || []).length} machine(s)`);
  return changes;
}

function start({ config, db, saveDatabase, log = console.log } = {}) {
  const rp = config && config.remote_pricing;
  if (!rp || rp.enabled === false) return null;
  const locations = normalizeLocations(rp);
  if (locations.length === 0) {
    log('[remote-pricing] enabled but no locations configured — skipping');
    return null;
  }
  const intervalMs = Math.max(15, Number(rp.interval_sec) || 60) * 1000;

  async function syncAll() {
    let total = 0;
    for (const loc of locations) total += await syncLocation(loc, db, log);
    if (total > 0 && typeof saveDatabase === 'function') saveDatabase();
  }

  syncAll(); // initial pull on startup
  const timer = setInterval(syncAll, intervalMs);
  if (timer.unref) timer.unref();
  log(`[remote-pricing] polling ${locations.length} location(s) every ${intervalMs / 1000}s`);
  return { syncAll, stop: () => clearInterval(timer) };
}

module.exports = { start, normalizeLocations, syncLocation };
