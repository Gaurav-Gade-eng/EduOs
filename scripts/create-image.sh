#!/usr/bin/env bash
# Creates a NEW disk image. Refuses to overwrite an existing one unless you confirm.
set -euo pipefail
cd "$(dirname "$0")/.."
if [ -f build/eduos.img ]; then
  read -r -p "build/eduos.img exists; recreating DESTROYS all EduFS files. Type 'yes': " a
  [ "$a" = "yes" ] || { echo "aborted"; exit 1; }
  make image FORCE=1
else
  make image
fi
