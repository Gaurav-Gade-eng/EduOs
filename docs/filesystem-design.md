# EduFS v1

Custom educational file system, **not** FAT/ext2. Volume starts at LBA 2048 of the disk image (`EDUFS_START_LBA`); the kernel refuses ATA writes below it.

| Region | Blocks (512 B) | Content |
|---|---|---|
| Superblock | 0 | magic `0x53464445` ("EDFS"), version 1, block size, total blocks, region offsets, inode count |
| Bitmap | 1 … | 1 bit per block, set = used (metadata blocks pre-marked) |
| Inode table | 16 blocks | 64 inodes × 128 B: used, size, name[32], 12 direct block numbers. **The table is the flat root directory.** |
| Data | rest | file contents |

* Max 64 files, max 6144 bytes per file, names `[A-Za-z0-9._-]` up to 31 chars (this also keeps telemetry safe).
* Write-through: each operation writes data, the inode sector and the bitmap before reporting success. Replacing a file allocates new blocks first, then frees old ones (a failed write leaves the old content).
* Format writes the superblock last, so an interrupted format stays "unformatted".
* **Mount never formats.** Formatting is the explicit shell command `format confirm`.
* Limits: no subdirectories, no journaling, no crash-consistency guarantees beyond the ordering above.
