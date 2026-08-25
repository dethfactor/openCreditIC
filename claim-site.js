#!/usr/bin/env node
// claim-site.js — exchange a one-time site claim code for a long-lived, read-only,
// per-location pricing token and write it into config.json (remote_pricing.locations).
//
// Usage:  node claim-site.js <baseUrl> <CLAIM-CODE> [label]
//   e.g.  node claim-site.js https://your-host.example.com A3GN-TAPG "Main floor"
//
// The token can only READ this one location's pricing list. To rotate it, RESET the
// site link in the provider's UI (new claim code) and re-run this.

const fs = require('fs');
const path = require('path');

async function main() {
  const [, , baseUrlArg, codeArg, ...labelParts] = process.argv;
  if (!baseUrlArg || !codeArg) {
    console.error('Usage: node claim-site.js <baseUrl> <CLAIM-CODE> [label]');
    process.exit(1);
  }
  const baseUrl = baseUrlArg.replace(/\/+$/, '');
  const claimCode = codeArg.replace(/[^A-Za-z0-9]/g, '').toUpperCase(); // strip dashes/spaces
  const label = labelParts.join(' ').trim() || null;

  let res, data;
  try {
    res = await fetch(`${baseUrl}/api/credit-sync/site-claim`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ claimCode }),
    });
    data = await res.json().catch(() => ({}));
  } catch (e) {
    console.error('Request failed:', e.message);
    process.exit(1);
  }
  if (!res.ok || !data.token) {
    console.error(`Claim failed (HTTP ${res.status}):`, data.error || JSON.stringify(data));
    process.exit(1);
  }

  const cfgPath = path.join(__dirname, 'config.json');
  let config = {};
  if (fs.existsSync(cfgPath)) {
    try { config = JSON.parse(fs.readFileSync(cfgPath, 'utf8')); }
    catch (e) { console.error('config.json is not valid JSON:', e.message); process.exit(1); }
  }
  if (!config.remote_pricing || typeof config.remote_pricing !== 'object') {
    config.remote_pricing = { enabled: true, interval_sec: 60, locations: [] };
  }
  if (!Array.isArray(config.remote_pricing.locations)) config.remote_pricing.locations = [];

  const url = `${baseUrl}/api/credit-sync/pricing`;
  const entry = { label: label || data.name || data.locationId, url, token: data.token };
  // replace any existing entry for the same location (same url or same label), then add
  config.remote_pricing.locations = config.remote_pricing.locations
    .filter((l) => l && l.url !== url && l.label !== entry.label);
  config.remote_pricing.locations.push(entry);

  fs.writeFileSync(cfgPath, JSON.stringify(config, null, 2));
  console.log(`Paired "${entry.label}"${data.locationId ? ` (${data.locationId})` : ''}.`);
  console.log(`Wrote token to config.json -> remote_pricing.locations. Restart the server to start syncing.`);
}

main();
