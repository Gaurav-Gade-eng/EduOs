#!/usr/bin/env python3
"""Create or update the raw disk image.
  mkimage.py create <layout.conf> <builddir> [--force]   new zeroed image + boot + kernel (DESTRUCTIVE)
  mkimage.py update <layout.conf> <builddir>              rewrite boot/kernel sectors only; EduFS untouched
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from gen_layout import load, validate, SECTOR  # noqa: E402


def read(path):
    with open(path, "rb") as f:
        return f.read()


def pad(b):
    return b + b"\0" * (-len(b) % SECTOR)


def write_boot_and_kernel(img, c, build):
    s1, s2, k = (read(os.path.join(build, n)) for n in ("stage1.bin", "stage2.bin", "kernel.bin"))
    if len(s1) != 512 or s1[510:512] != b"\x55\xaa":
        sys.exit("stage1.bin is not a valid 512-byte boot sector")
    if len(s2) != c["STAGE2_SECTORS"] * SECTOR:
        sys.exit("stage2.bin size does not match STAGE2_SECTORS")
    k = pad(k)
    if len(k) // SECTOR > c["KERNEL_MAX_SECTORS"]:
        sys.exit("kernel too large for loader region")
    img.seek(0); img.write(s1)
    img.seek(c["STAGE2_LBA"] * SECTOR); img.write(s2)
    img.seek(c["KERNEL_LBA"] * SECTOR); img.write(k)
    print(f"wrote boot sector, stage2 ({len(s2)//SECTOR} sectors), kernel ({len(k)//SECTOR} sectors)")


def main():
    if len(sys.argv) < 4 or sys.argv[1] not in ("create", "update"):
        sys.exit(__doc__)
    mode, conf, build = sys.argv[1:4]
    c = load(conf)
    validate(c)
    path = os.path.join(build, "eduos.img")
    size = c["IMAGE_SIZE_MIB"] * 1024 * 1024
    if mode == "create":
        if os.path.exists(path) and "--force" not in sys.argv:
            sys.exit(f"{path} exists; refusing to overwrite (it may hold EduFS data). Use FORCE=1 to recreate.")
        with open(path, "wb") as f:
            f.truncate(size)
            write_boot_and_kernel(f, c, build)
        print(f"created {path} ({c['IMAGE_SIZE_MIB']} MiB). Boot it and run 'format confirm' once.")
    else:
        if not os.path.exists(path):
            sys.exit(f"{path} missing; run 'make image' first")
        if os.path.getsize(path) != size:
            sys.exit("image size differs from layout.conf; refusing to modify")
        with open(path, "r+b") as f:
            write_boot_and_kernel(f, c, build)
        print("updated boot + kernel regions; EduFS data untouched")


if __name__ == "__main__":
    main()
