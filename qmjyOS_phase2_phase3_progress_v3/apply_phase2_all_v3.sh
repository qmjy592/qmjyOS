#!/usr/bin/env bash
set -Eeuo pipefail
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
printf '[PHASE 2/3 INTEGRATED V3] Phase 2 foundation + MM_HEAP + BIOS NXE cleanup\n'
bash "$SCRIPT_DIR/apply_phase2_next.sh"
bash "$SCRIPT_DIR/phase2_boot/apply_phase2_boot.sh"
cd "$(dirname -- "$SCRIPT_DIR")"
make -j2 all
git diff --check
printf '[PASS] Integrated Phase 2 v3 source updates and full kernel build.\n'
printf '[NOTE] QEMU runtime is not run by this installer.\n'
