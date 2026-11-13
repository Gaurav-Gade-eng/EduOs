#!/usr/bin/env python3
"""Verify the boot image: verify-image.py <layout.conf> <builddir>"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from gen_layout import load, validate, SECTOR  # noqa: E402

conf, build = sys.argv[1:3]
c = load(conf)
validate(c)
errors = []
with open(os.path.join(build, "eduos.img"), "rb") as f:
    img = f.read(c["EDUFS_START_LBA"] * SECTOR)
if len(img) < c["EDUFS_START_LBA"] * SECTOR:
    errors.append("image shorter than EduFS start")
if img[510:512] != b"\x55\xaa":
    errors.append("missing 0x55AA boot signature at offsets 510-511")
s2 = img[c["STAGE2_LBA"] * SECTOR:][:4]
if s2 != b"ST2!":
    errors.append("Stage 2 signature 'ST2!' not at STAGE2_LBA")
kernel = open(os.path.join(build, "kernel.bin"), "rb").read()
at = c["KERNEL_LBA"] * SECTOR
if img[at:at + len(kernel)] != kernel:
    errors.append("kernel.bin does not match the image at KERNEL_LBA (run 'make update-image')")
if (len(kernel) + SECTOR - 1) // SECTOR > c["KERNEL_MAX_SECTORS"]:
    errors.append("kernel exceeds KERNEL_MAX_SECTORS")
if errors:
    print("IMAGE CHECK FAILED:\n  - " + "\n  - ".join(errors))
    sys.exit(1)
print(f"image OK: boot signature, Stage 2 at LBA {c['STAGE2_LBA']}, kernel {len(kernel)} bytes at LBA {c['KERNEL_LBA']}, "
      f"EduFS starts at LBA {c['EDUFS_START_LBA']}")
