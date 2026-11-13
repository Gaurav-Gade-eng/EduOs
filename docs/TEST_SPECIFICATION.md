# EduOS — Test Specification and Step-by-Step Procedure

> **Status:** The kernel, bootloader and boot image have **not been built or run** by the author
> (no NASM/GCC/QEMU on the authoring machine). Everything in Parts A–D is a procedure *for you to execute*.
> Only the dashboard unit/API tests (Part E) were run, and they passed (7/7).
> Treat any first-build compiler/assembler error as expected feedback, not a regression.

Record the result of each step in the **Evidence** column of your own copy (paste serial-log lines or console text).

---

## Part A — One-time environment setup (WSL2 + Ubuntu)

| # | Step | Expected |
|---|---|---|
| A1 | PowerShell (admin): `wsl --install -d Ubuntu`, reboot, create the Linux user | `wsl -l -v` lists Ubuntu, version 2 |
| A2 | In Ubuntu: `sudo apt update && sudo apt install -y build-essential gcc binutils nasm make qemu-system-x86 gdb python3 file` | exits 0 |
| A3 | `gcc -m32 -x c -c /dev/null -o /tmp/t.o && echo ok` | prints `ok` (32-bit support). If it fails: `sudo apt install gcc-multilib`, or use an `i686-elf-` cross compiler and run `make CROSS=i686-elf-` |
| A4 | `nasm -v; ld --version; qemu-system-i386 --version; python3 --version` | all print versions |
| A5 | Copy/clone the project **inside the Linux filesystem** (e.g. `cp -r /mnt/c/Users/<you>/EduOS ~/EduOS`; `cd ~/EduOS`) — avoids slow/CRLF-corrupted `/mnt/c` builds | `chmod +x scripts/*.sh` works |
| A6 | Check line endings: `file scripts/run-qemu.sh Makefile` | no "CRLF" (fix with `sed -i 's/\r$//' scripts/*.sh Makefile`) |
| A7 | Node.js LTS for the dashboard (Windows host or WSL): `node --version` ≥ 18 | OK |

---

## Part B — Build tests (stage by stage)

Run from the project root. **Do not continue to the next row until the current one passes.**

| ID | Command | Pass criterion |
|---|---|---|
| BLD-01 | `make layout` | `build/gen/boot_constants.inc`, `layout.h`, `layout.mk` exist |
| BLD-02 | Edit `config/layout.conf`, set `KERNEL_LBA=10`; `make layout` | **fails** with "overlaps Stage 2 sectors". Revert to `17` |
| BLD-03 | `make kernel` | zero warnings/errors (`-Wall -Wextra -Werror`); prints `kernel: N bytes, M sectors` |
| BLD-04 | `file build/kernel.elf` and `readelf -h build/kernel.elf \| grep -E 'Class\|Machine\|Entry'` | ELF32, Intel 80386, entry = `0x10000` |
| BLD-05 | `nm build/kernel.elf \| grep -E ' (_start\|kernel_main\|__kernel_start\|__kernel_end)$'` | all four present; `_start` == `0x10000` |
| BLD-06 | `nm build/kernel.elf \| grep -E ' U '` | **no undefined symbols** (a `__udivdi3`-style entry means 64-bit math crept in) |
| BLD-07 | `make boot` | `build/stage1.bin` is exactly 512 bytes; stage2.bin is exactly 8192 bytes (`stat -c %s`) |
| BLD-08 | `xxd -s 510 -l 2 build/stage1.bin` | `55aa` |
| BLD-09 | Temporarily set `KERNEL_MAX_SECTORS=4` in `config/layout.conf`; `make kernel` | **fails**: "kernel is N sectors; loader limit is 4". Revert |
| BLD-10 | `make image` | creates `build/eduos.img`, 32 MiB (`ls -l`) |
| BLD-11 | `make image` again | **refuses** to overwrite (protects EduFS data) |
| BLD-12 | `make check` | `image OK: boot signature, Stage 2 at LBA 1, kernel … at LBA 17, EduFS starts at LBA 2048` |

---

## Part C — Boot and kernel tests in QEMU

Start the VM with `make run` (QEMU window = VGA console; serial telemetry goes to `build/eduos_events.log`).
In a second terminal watch telemetry: `tail -f build/eduos_events.log`.

### C1. Boot path

| ID | Step | Expected |
|---|---|---|
| BOOT-01 | `make run` | Briefly shows `STAGE2_OK`, then `EduOS kernel entered`, kernel image range, then the banner and `eduos>` prompt |
| BOOT-02 | Read the log | First line is `EDUOS\|1\|00000001\|BOOT\|START\|version=0.1.0\|architecture=x86-32\|boot_id=…` followed by `MODULE\|READY` lines for Kernel_Core, Serial_Telemetry, GDT, IDT, Interrupt_Controller, Physical_Memory_Manager, Paging, Timer, Keyboard, Process_Manager, Scheduler, System_Call_Interface, ATA_Driver, then EduFS (or `MODULE\|ERROR…NOT_FORMATTED` on a blank disk), Shell, then `KERNEL\|READY` — in that order, sequence numbers strictly increasing |
| BOOT-03 | Close QEMU, `make run` again | Same sequence; log file was restarted (truncated by QEMU) |
| BOOT-04 | Failure path: `cp build/eduos.img /tmp/bad.img`, then `dd if=/dev/zero of=/tmp/bad.img bs=512 seek=17 count=4 conv=notrunc` and boot `/tmp/bad.img` (`qemu-system-i386 -drive file=/tmp/bad.img,format=raw,if=ide`) | Does not boot cleanly (hang/triple-fault/panic is acceptable at this stage, but must not silently show a working shell). Corrupted *Stage 2* signature (`seek=1`) must print `S1 E4: bad stage2` |
| BOOT-05 | Corrupt boot_info magic is detected: covered by code review of `kernel_main` (panic `BAD_BOOT_INFO`) | n/a (optional: use GDB, see C9) |

### C2. Interrupts, timer, keyboard

| ID | Step | Expected |
|---|---|---|
| INT-01 | Shell: `uptime` twice, ~5 s apart | Tick count rises ~100/s |
| INT-02 | Type characters, use Backspace and Enter | Characters echo; Backspace erases; Enter runs the line |
| INT-03 | After ~10 s the log shows `INTERRUPT\|COUNT\|irq=0\|count=…` | irq0 count ≈ 100 × uptime seconds; `irq=1` increases as you type |
| INT-04 | Leave the VM idle 2 min | Still responsive; no reboot; no interrupt storm (log growth stays ~ a few lines per second) |
| INT-05 | *(Optional, needs a code tweak)* add `__asm__("ud2");` to a shell command | Red panic screen "Invalid opcode", `KERNEL\|PANIC\|code=CPU_EXCEPTION` in log |

### C3. Processes and scheduler

| ID | Step | Expected |
|---|---|---|
| PROC-01 | `ps` | PIDs 0 `idle`, 1 `kernel`, 2 `worker-a`, 3 `worker-b`; exactly one RUNNING |
| PROC-02 | `spawn` | `spawned worker-1 pid=4`; log has `PROCESS\|CREATE\|pid=4…`; PIDs are never reused |
| SCHED-01 | `ps` several times 3 s apart | `TICKS` of workers grow; shell stays usable; log shows `SCHEDULER\|SWITCH` lines with `reason=TIMER` rotating through PIDs 1→2→3→… |
| SCHED-02 | `sched` | `algorithm=round_robin quantum=10 …`; `switches` matches the last `total=` in the log (±events still queued) |
| PROC-03 | `yield` | Returns to prompt; log: `SYSCALL\|CALL\|number=4\|name=yield\|pid=1` then a `SWITCH … reason=YIELD` |
| PROC-04 | `block 2`, then `ps` | `worker-a` BLOCKED; its TICKS stop growing; log `PROCESS\|STATE\|pid=2\|state=BLOCKED\|reason=BLOCK` |
| PROC-05 | `unblock 2` | READY → resumes; log `…state=READY\|reason=WAKE` |
| PROC-06 | `kill 3` | `ps` shows TERMINATED; log `PROCESS\|EXIT\|pid=3\|exit_code=-1\|reason=KILL`; no more SWITCH events involving pid 3 |
| PROC-07 | Negative: `block 0`, `block 1`, `kill 1`, `block 99`, `unblock 1` | `failed (ERR_PERM)` / `ERR_NOENT` / `ERR_STATE`; **no** state-change event emitted; system keeps running |
| PROC-08 | `spawn` until the table is full (~14×) | Eventually `spawn: failed (ERR_NOMEM)`; kernel stable. `kill` some, `spawn` works again (slot reuse) |

### C4. Physical memory and paging

| ID | Step | Expected |
|---|---|---|
| MEM-01 | `mem` | `total=3840` frames, `free = total − used`; log `MEMORY\|SNAPSHOT` with same numbers |
| MEM-02 | `mem`, `vmap`, `mem`, `vunmap 0x40000000`, `mem` | `used` rises by **2** after `vmap` (data frame + a page-table frame), and returns to within 1 frame of the start after `vunmap` (the page table frame stays allocated by design); `allocs`/`frees` counters move accordingly |
| PAGE-01 | `vminfo` | paging enabled, identity map 0..16 MiB, dynamic window listed |
| PAGE-02 | `vmap` | Prints `mapped 0x40000000 -> 0x001xxxxx; … read via physical: 0xed00ed00`; log `PAGING\|MAP\|…result=OK` |
| PAGE-03 | `vmap` again | "demo mapping already active" |
| PAGE-04 | `vmap 0x40000000 0x200000` while mapped | `failed (ERR_EXIST)`; log `PAGING\|MAP…result=ERR_EXIST` |
| PAGE-05 | `vmap 0x1000 0x200000` and `vmap 0x40000001 0x200000` | `ERR_PERM` (identity region protected) and `ERR_INVAL` (unaligned) |
| PAGE-06 | `vunmap 0x40000000` twice | first OK, second `ERR_NOENT`; log shows both |
| PAGE-07 | `vtest` | `page fault caught and recovered`; log `PAGING\|FAULT\|address=0x50000000\|…\|expected=1`; shell still works |
| MEM-03 | Double free guard: code review of `frame_free` (returns `E_INVAL`) or a temporary shell hook | no allocator corruption |

### C5. System calls

| ID | Step | Expected |
|---|---|---|
| SYS-01 | `yield`, `block`/`unblock`/`kill` shell commands | They go through `int 0x80` (see `SYSCALL\|CALL` lines with `number`, `name`, `pid`, `result`) |
| SYS-02 | Wait 10 s | Workers emit `SYSCALL\|CALL\|name=yield` about once per 5 s each |
| SYS-03 | `events` | `syscalls=` equals the count of `SYSCALL\|CALL` lines (±queue) |
| SYS-04 | *(Optional)* temporary task calling `syscall3(99,0,0,0)` | result `-1` (`E_INVAL`), no crash |

### C6. Disk (run with the disk attached — it is by default)

| ID | Step | Expected |
|---|---|---|
| DISK-01 | `disk` | `ata0 sectors=65536 (32 MiB)`; log `DISK\|READY\|device=ata0\|sectors=65536\|sector_size=512` |
| DISK-02 | No-disk behaviour: the loader boots *from* the disk, so this cannot be run end to end. Verify by code review that `ata_init()` returns `E_NODEV`/`E_TIMEOUT` and `kernel_main` then emits `MODULE\|ERROR\|name=ATA_Driver` and continues | Reviewed |
| DISK-03 | Bootloader protection: code review `ata_write` rejects `lba < 2048` (`ERR_PERM`) | confirmed |

### C7. EduFS (disk-backed file system)

| ID | Step | Expected |
|---|---|---|
| FS-01 | First boot of a fresh image: `ls` | `failed (ERR_NOFS)`; log has `MODULE\|ERROR\|name=EduFS\|reason=NOT_FORMATTED` and `FILESYSTEM\|MOUNT…result=NOT_FORMATTED` — **no automatic format** |
| FS-02 | `format` (no arg) | warns; does nothing |
| FS-03 | `format confirm` | `format: ok`; log `FILESYSTEM\|FORMAT…result=OK` then `MOUNT…files=0\|result=OK` |
| FS-04 | `touch a.txt`, `ls` | file listed, 0 B; log `FILESYSTEM\|CREATE\|name=a.txt\|size=0\|files=1\|result=OK` |
| FS-05 | `write a.txt hello world`, `cat a.txt` | prints `hello world`; log `WRITE…size=11` and `DISK\|WRITE` lines |
| FS-06 | `write big.txt <200 chars>`; `stat big.txt` | size 200, 1 block |
| FS-07 | `touch a.txt` again | `failed (ERR_EXIST)`; log result `ERR_EXIST` (never a success event) |
| FS-08 | `cat nope`, `rm nope`, `touch bad/name`, `touch ""`, a 40-char name | `ERR_NOENT` / `ERR_NOENT` / `ERR_INVAL` / usage / `ERR_INVAL` |
| FS-09 | `write` text > 6144 bytes (use a long paste) | `ERR_NOSPC`, existing content unchanged |
| FS-10 | `rm a.txt`, `ls`, `cat a.txt` | gone; `cat` → `ERR_NOENT`; log `DELETE…files=…` |
| FS-11 | Create 64 files (`touch f1`…`f64`), then a 65th | 65th → `ERR_NOSPC` |
| **FS-12 persistence** | `write keep.txt survives`, close QEMU **window** cleanly, `make run` again, `cat keep.txt` | prints `survives`; log shows `MOUNT…files=N…result=OK` (no FORMAT) |
| FS-13 | `make update-image` (rebuild kernel) then `make run`, `cat keep.txt` | file still present — routine rebuilds never touch LBA ≥ 2048 |
| FS-14 | `make image` | refuses to overwrite; `make image FORCE=1` recreates a blank, unformatted volume (**destructive**) |

### C8. Telemetry protocol (kernel side)

| ID | Step | Expected |
|---|---|---|
| TEL-01 | `grep -vc '^EDUOS|1|[0-9]\{8\}|' build/eduos_events.log` | 0 (every line is a well-formed record) |
| TEL-02 | `awk -F'\|' 'length($0)>190' build/eduos_events.log \| wc -l` | 0 (bounded line length) |
| TEL-03 | `touch 'a\|b'` style names (rejected by FS) and `echo` lines containing `\|` | the log never contains a forged second `EDUOS` record; rejected names appear percent-encoded |
| TEL-04 | Sequence numbers strictly increasing: `grep -o 'EDUOS|1|[0-9]*' … \| cut -d'\|' -f3 \| sort -c` | no output (sorted) |
| TEL-05 | Run `spawn` ×10 quickly, then `events` | `dropped=` is 0 normally; if non-zero, the dashboard shows the dropped-events warning |
| TEL-06 | Failed operations (FS-07, PAGE-04) never emit a success result | confirmed by log |

### C9. Debugging aids (optional)

`make debug` starts QEMU paused with a GDB stub; in another terminal:
`gdb build/kernel.elf` → `target remote :1234` → `b kernel_main` → `c` → `bt`, `info registers`, `x/8i $eip`.
Use `info registers cr0 cr3` after `paging_init` to confirm PG is set (CR0 bit 31).

---

## Part D — Integration test (kernel + dashboard live)

| ID | Step | Expected |
|---|---|---|
| INTG-01 | Terminal 1 (inside the project): `cd dashboard && npm install && npm start` | `EduOS dashboard: http://127.0.0.1:3000` |
| INTG-02 | Browser → http://127.0.0.1:3000 before starting QEMU | Banner "Serial log not found…"; badge `Log missing`; no JS errors |
| INTG-03 | If the dashboard runs on Windows and QEMU in WSL: set the log path, e.g. `set EDUOS_LOG=\\wsl$\Ubuntu\home\<you>\EduOS\build\eduos_events.log` (PowerShell: `$env:EDUOS_LOG=…`) before `npm start`; simplest is running both inside WSL and opening the URL from Windows (WSL2 forwards localhost) | Same data either way |
| INTG-04 | Terminal 2: `make run` | Within ~3 s the badge turns `Telemetry live`; Services page lists every module as **Ready** |
| INTG-05 | Processes page | Same rows as shell `ps`; state changes (PROC-04/05) highlight the changed row for one refresh; search box filters by PID/name/state |
| INTG-06 | Performance → Memory | Total 3840 frames matches `mem`; after `vmap` and a wait ≤ 5 s (next periodic snapshot) used count increases |
| INTG-07 | Performance → Scheduler | Algorithm `round_robin`, quantum 10, switch counter increases; switch rate is non-zero; "Busy" shows a value derived from idle/total ticks |
| INTG-08 | Details | Event table follows the log; category/text filter works; paging and file-system boxes show mappings, FAULT, file operations; `Sessions seen` increments after restarting QEMU |
| INTG-09 | Stop QEMU (close window) and wait 15 s | Badge → `Telemetry stale`, banner explains; last data stays visible, not zeroed |
| INTG-10 | `make run` again | Dashboard clears and rebuilds from the new BOOT START (log truncation handled); Sessions seen +1 |
| INTG-11 | Stop the dashboard while QEMU runs, restart it | Rebuilds state from the log; because only periodic snapshots are replayed it may show some "—" values until the next 5 s snapshot — never invented numbers |
| INTG-12 | Kill the Node process / open the page offline | Banner "Cannot reach the dashboard backend…", page doesn't crash, recovers when backend returns |
| INTG-13 | Cause a panic (INT-05) | Banner "KERNEL PANIC: …" in red; status `PANIC` |

---

## Part E — Dashboard automated tests (already executed once: 7/7 passed)

```bash
cd dashboard
npm install
npm test
```

| Test | Verifies |
|---|---|
| parser: valid line / decoding / unknown version | field parsing, percent-decoding, `unsupported` flag for protocol ≠ 1 |
| parser: malformed / noise | malformed lines rejected, un-prefixed lines ignored, over-long lines rejected, no exceptions |
| state store reconstructs fixture | `tests/sample-events/boot-session.log` → modules, processes, scheduler, memory, paging, disk, filesystem, syscalls, counters |
| BOOT START resets state | a second boot in one log starts clean; session counter increments |
| log reader | partial trailing line withheld until newline, appends, truncation detection |
| HTTP API end-to-end | `/api/status`, `/api/processes`, `/api/events/latest` validation (400 for `limit=0`/`abc`), 404, static page |
| missing log | `/api/health` reports `LOG_MISSING` |

No-QEMU demo of the UI (replays the fixture into the log the dashboard watches):

```bash
cd dashboard
npm start            # terminal 1
npm run demo         # terminal 2 — appends the sample session to ../build/eduos_events.log
```

---

## Part F — Final acceptance checklist

- [ ] BLD-01 … BLD-12 pass; build has zero warnings
- [ ] BOOT-01 … BOOT-03 pass; boot sequence in log is deterministic
- [ ] INT-01 … INT-04; PROC-01 … PROC-08; SCHED-01/02
- [ ] MEM-01/02; PAGE-01 … PAGE-07 (including the recovered deliberate fault)
- [ ] SYS-01 … SYS-03
- [ ] DISK-01; FS-01 … FS-14 (**FS-12 persistence** and **FS-13 rebuild-safe** are mandatory)
- [ ] TEL-01 … TEL-06
- [ ] INTG-01 … INTG-13
- [ ] Dashboard `npm test` passes

## Known first-boot risk areas (check these first if something fails)

| Symptom | Likely cause / where to look |
|---|---|
| Nothing after `STAGE2_OK` | Kernel not at `0x10000` — compare `nm build/kernel.elf \| grep _start` with `KERNEL_LOAD_ADDR`; check `make check` |
| Immediate reboot loop (add `-d int,cpu_reset -no-shutdown` to QEMU) | GDT/IDT/paging setup; triple fault → run `make debug`, break at `kernel_main` |
| Hang after enabling interrupts | PIC remap/EOI (`pic.c`, `interrupts.c`), `interrupts.asm` frame layout vs `interrupt_frame_t` |
| First context switch crashes | New-task stack frame in `process_create` vs `context_switch.asm` pop order |
| `NO_DISK` | QEMU `-drive …if=ide,index=0` missing, or ATA IDENTIFY timeout |
| NASM errors in `stage2.asm` | Stage 2 outgrew 8192 bytes (`times` negative) — raise `STAGE2_SECTORS` in `config/layout.conf` |
| Dashboard shows "Log missing" | `EDUOS_LOG` path wrong; QEMU not started from project root |
