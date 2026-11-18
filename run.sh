#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export NATSUKI_SHIMEJI_HOME="$ROOT"
export LD_LIBRARY_PATH="$ROOT/rust_core/target/release${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
if [[ -z "${DDLC_SHIMEJI_SPRITES:-}" && -d "$ROOT/chikinenas" ]]; then
  export DDLC_SHIMEJI_SPRITES="$ROOT/chikinenas"
fi
if [[ -z "${NATSUKI_L4D2_WORKSHOP_DIR:-}" && -d "$ROOT/l4d2_workshop_vpks" ]]; then
  export NATSUKI_L4D2_WORKSHOP_DIR="$ROOT/l4d2_workshop_vpks"
fi
# Prevent stacking duplicate instances if user launches via desktop icon twice
pkill -x natsuki-shimeji 2>/dev/null || true
sleep 0.15
exec "$ROOT/natsuki-shimeji" "$@"

