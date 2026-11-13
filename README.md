# EduOS — Educational Operating System + Visualization Dashboard

32-bit x86 protected-mode teaching kernel (C + NASM) run in QEMU, with a Node.js/Express + vanilla-JS dashboard fed by serial telemetry. Built from `EduOS_Full_From_Scratch_Implementation_Specification.md`.

## Status — please read

| Part | State |
|---|---|
| Bootloader (stage 1/2), kernel, drivers, EduFS, shell, telemetry | **Source written, NOT yet built or run.** No NASM/GCC/QEMU was available where it was authored. Expect to fix first-build errors. |
| Dashboard (backend, frontend, parser, tests) | Written; its 7 automated tests pass. UI not yet visually checked in a browser. |
| How to verify everything | **`docs/TEST_SPECIFICATION.md`** — step-by-step procedure |

## Layout

```
boot/        stage1.asm (512 B sector), stage2.asm (loader: kernel load, E820, A20, protected mode)
config/      layout.conf  <- single source of truth for disk/memory layout (generates asm/C/make constants)
kernel/      C kernel + arch/x86 (asm stubs, GDT/IDT/PIC), linker.ld, include/
dashboard/   Node.js backend (src/), static frontend (public/), tests (test/), replay tool (tools/)
scripts/     gen_layout.py, mkimage.py, verify-image.py, run/debug/build/clean shell scripts
tests/       sample-events/boot-session.log fixture
docs/        TEST_SPECIFICATION.md, telemetry-protocol.md, filesystem-design.md
```

## Quick start (WSL2 Ubuntu; packages listed in docs/TEST_SPECIFICATION.md Part A)

```bash
make            # build bootloader + kernel
make image      # create build/eduos.img (first time only; refuses to overwrite)
make run        # boot in QEMU; telemetry -> build/eduos_events.log
# in the EduOS shell, first time only:  format confirm
cd dashboard && npm install && npm start     # http://127.0.0.1:3000
```

After code changes: `make update-image && make run` (keeps your files). `make check` inspects the ELF and image.

Dashboard without QEMU: `cd dashboard && npm start` and, in another terminal, `npm run demo`.

## Key design decisions

* 32-bit protected mode, ring 0 only (no user-mode isolation is claimed). Kernel loaded at 0x10000 by a two-stage BIOS loader; max kernel 384 KiB (`config/layout.conf`).
* Frame allocator manages 1–16 MiB (3840 frames); first 16 MiB identity-mapped; dynamic mapping window at 0x40000000.
* Round-Robin scheduler, 10-tick quantum at 100 Hz, preemptive via the timer IRQ; idle task runs only when nothing else is ready.
* Telemetry emitters enqueue only; the main loop drains to COM1. Protocol: `docs/telemetry-protocol.md`.
* The dashboard shows `—` / "Not reported" for anything the kernel has not reported.

## Shell commands

`help clear echo ps mem sched yield block unblock kill spawn uptime touch write cat ls rm stat format disk vminfo vmap vunmap vtest events reboot`
