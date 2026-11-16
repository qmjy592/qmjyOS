#!/usr/bin/env bash
set -Eeuo pipefail
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"

if [[ ! -x "$SCRIPT_DIR/phase2_foundation/apply_phase2_foundation.sh" || \
      ! -x "$SCRIPT_DIR/phase2_heap/apply_phase2_heap.sh" ]]; then
    echo "[FAIL] Package is incomplete; extract the entire package directory." >&2
    exit 1
fi

printf '[qmjyOS] Applying Phase 2 foundation tranche.\n'
bash "$SCRIPT_DIR/phase2_foundation/apply_phase2_foundation.sh"
printf '\n[qmjyOS] Applying Phase 2 MM_HEAP tranche.\n'
bash "$SCRIPT_DIR/phase2_heap/apply_phase2_heap.sh"
printf '\n[qmjyOS] Phase 2 foundation + MM_HEAP integration complete.\n'
