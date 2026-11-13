#!/usr/bin/env bash
# QEMU halted at start with a GDB stub on :1234.  gdb: target remote :1234 / symbol-file build/kernel.elf
set -euo pipefail
cd "$(dirname "$0")/.."
exec ./scripts/run-qemu.sh -s -S "$@"
