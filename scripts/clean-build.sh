#!/usr/bin/env bash
# Removes rebuildable artifacts only. The disk image (build/eduos.img) and logs are kept.
set -euo pipefail
cd "$(dirname "$0")/.."
make clean
