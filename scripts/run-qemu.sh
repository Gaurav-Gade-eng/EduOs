#!/usr/bin/env bash
# Launch EduOS in QEMU. Serial (COM1) telemetry is written to build/eduos_events.log.
set -euo pipefail
cd "$(dirname "$0")/.."
[ -f build/eduos.img ] || { echo "build/eduos.img missing: run 'make image' first" >&2; exit 1; }
exec qemu-system-i386 \
  -machine pc \
  -m 128M \
  -drive file=build/eduos.img,format=raw,if=ide,index=0 \
  -serial file:build/eduos_events.log \
  -no-reboot "$@"
