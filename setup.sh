#!/usr/bin/env bash
#
# openCreditIC card-server — one-shot Pi/Linux bootstrap.
#
# Installs Node 20, clones (or updates) this repo, installs deps, writes a
# starter config.json, and runs the server under pm2 so it survives reboots.
#
# Usage — fresh Pi (nothing cloned yet):
#   curl -fsSL https://raw.githubusercontent.com/dethfactor/openCreditIC/feature/remote-pricing-sync/setup.sh | bash
#
# Or, if you've already cloned the repo, just run it from inside:
#   ./setup.sh
#
# Optional environment overrides:
#   REPO_URL      git URL to clone            (default: dethfactor fork over HTTPS)
#   BRANCH        branch to check out         (default: feature/remote-pricing-sync)
#   TARGET_DIR    where to clone              (default: $HOME/openCreditIC)
#   ARCADE_NAME   name shown in the web UI    (default: "Arcade")
#   PM2_NAME      pm2 process name            (default: cardserver)
#   CLAIM_URL     pricing-server base URL for pairing a location (e.g. https://your-pricing-server.example.com)
#   CLAIM_CODE    one-time site claim code    (pairs a location if both CLAIM_* are set)
#   CLAIM_LABEL   label for the paired location (default: "Main floor")
#
set -euo pipefail

REPO_URL="${REPO_URL:-https://github.com/dethfactor/openCreditIC.git}"
BRANCH="${BRANCH:-feature/remote-pricing-sync}"
TARGET_DIR="${TARGET_DIR:-$HOME/openCreditIC}"
ARCADE_NAME="${ARCADE_NAME:-Arcade}"
PM2_NAME="${PM2_NAME:-cardserver}"
NODE_MAJOR=20

log()  { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m!!\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31mxx\033[0m %s\n' "$*" >&2; exit 1; }

need_sudo() {
  if [ "$(id -u)" -eq 0 ]; then SUDO=""; else
    command -v sudo >/dev/null 2>&1 || die "Need root or sudo to install system packages."
    SUDO="sudo"
  fi
}

# 32 hex chars — good enough for a device/web key.
randkey() {
  if command -v openssl >/dev/null 2>&1; then openssl rand -hex 16
  else head -c 16 /dev/urandom | od -An -tx1 | tr -d ' \n'; fi
}

# ---------------------------------------------------------------------------
# 1. System packages: git + Node 20 (via NodeSource if missing/too old)
# ---------------------------------------------------------------------------
need_sudo

if ! command -v git >/dev/null 2>&1; then
  log "Installing git…"
  $SUDO apt-get update -y && $SUDO apt-get install -y git
fi

node_ok=0
if command -v node >/dev/null 2>&1; then
  cur="$(node -v | sed 's/^v//; s/\..*//')"
  [ "${cur:-0}" -ge 18 ] && node_ok=1 || warn "Node $(node -v) is too old; upgrading to ${NODE_MAJOR}.x."
fi
if [ "$node_ok" -eq 0 ]; then
  log "Installing Node ${NODE_MAJOR}.x (NodeSource)…"
  curl -fsSL "https://deb.nodesource.com/setup_${NODE_MAJOR}.x" | $SUDO -E bash -
  $SUDO apt-get install -y nodejs
fi
log "node $(node -v), npm $(npm -v)"

# ---------------------------------------------------------------------------
# 2. Clone or update the repo, on the right branch
# ---------------------------------------------------------------------------
if [ -d "$TARGET_DIR/.git" ]; then
  log "Repo exists at $TARGET_DIR — fetching & checking out $BRANCH…"
  git -C "$TARGET_DIR" fetch origin --prune
  git -C "$TARGET_DIR" checkout "$BRANCH"
  git -C "$TARGET_DIR" pull --ff-only origin "$BRANCH"
elif [ -f "$(dirname "$0")/card-server.js" ]; then
  # Being run from inside an existing checkout
  TARGET_DIR="$(cd "$(dirname "$0")" && pwd)"
  log "Running from existing checkout at $TARGET_DIR"
  git -C "$TARGET_DIR" fetch origin --prune || true
  git -C "$TARGET_DIR" checkout "$BRANCH" || warn "Could not switch to $BRANCH; staying on current branch."
else
  log "Cloning $REPO_URL → $TARGET_DIR (branch $BRANCH)…"
  git clone --branch "$BRANCH" "$REPO_URL" "$TARGET_DIR"
fi

cd "$TARGET_DIR"

# ---------------------------------------------------------------------------
# 3. Dependencies
# ---------------------------------------------------------------------------
log "Installing npm dependencies…"
if [ -f package-lock.json ]; then npm ci; else npm install; fi

# ---------------------------------------------------------------------------
# 4. config.json — create if absent (never clobber an existing one)
# ---------------------------------------------------------------------------
if [ -f config.json ]; then
  log "config.json already present — leaving it untouched."
else
  DEVICE_KEY="$(randkey)"
  WEB_KEY="$(randkey)"
  log "Writing a fresh config.json (device_key & web_key generated)…"
  cat > config.json <<JSON
{
  "arcade_name": "${ARCADE_NAME}",
  "device_key": "${DEVICE_KEY}",
  "web_key": "${WEB_KEY}",
  "show_delete_actions": false,
  "remote_pricing": {
    "enabled": true,
    "interval_sec": 60,
    "locations": []
  }
}
JSON
  printf '\n'
  printf '  device_key (card readers):  %s\n' "$DEVICE_KEY"
  printf '  web_key    (web UI login):  %s\n' "$WEB_KEY"
  printf '\n'
  warn "Save these. device_key goes in each reader's config.h; web_key logs into the web UI."
fi

# ---------------------------------------------------------------------------
# 5. Optionally pair a location (claim a site token from the pricing server)
# ---------------------------------------------------------------------------
if [ -n "${CLAIM_URL:-}" ] && [ -n "${CLAIM_CODE:-}" ]; then
  log "Pairing location via claim code…"
  node ./claim-site.js "$CLAIM_URL" "$CLAIM_CODE" "${CLAIM_LABEL:-Main floor}" \
    || warn "Claim failed — pair later with:  npm run claim <baseUrl> <CLAIM-CODE> \"Label\""
else
  warn "No CLAIM_URL/CLAIM_CODE given — pair a location later with:"
  warn "    npm run claim <baseUrl> <CLAIM-CODE> \"Main floor\""
fi

# ---------------------------------------------------------------------------
# 6. Run under pm2, persist across reboots
# ---------------------------------------------------------------------------
if ! command -v pm2 >/dev/null 2>&1; then
  log "Installing pm2 globally…"
  $SUDO npm install -g pm2
fi

if pm2 describe "$PM2_NAME" >/dev/null 2>&1; then
  log "Restarting existing pm2 process '$PM2_NAME'…"
  pm2 restart "$PM2_NAME"
else
  log "Starting card-server under pm2 as '$PM2_NAME'…"
  pm2 start card-server.js --name "$PM2_NAME"
fi
pm2 save

STARTUP_CMD="$(pm2 startup systemd -u "$USER" --hp "$HOME" 2>/dev/null | grep -E '^\s*sudo' || true)"

WEB_KEY_SHOW="$(node -e 'try{process.stdout.write((require("./config.json").web_key)||"")}catch(e){}' 2>/dev/null || true)"
IP="$(hostname -I 2>/dev/null | awk '{print $1}')"

printf '\n\033[1;32m✓ card-server is up under pm2 (%s).\033[0m\n\n' "$PM2_NAME"
echo   "Web UI:   http://${IP:-<pi-ip>}:1777/?key=${WEB_KEY_SHOW:-<web_key>}"
echo   "Logs:     pm2 logs $PM2_NAME"
echo   "Status:   pm2 status"
if [ -n "$STARTUP_CMD" ]; then
  printf '\nTo start automatically on boot, run this ONE command:\n  %s\n' "$STARTUP_CMD"
fi
printf '\n'
