# EduOS Telemetry Protocol (version 1)

One record per line over COM1, redirected by QEMU to `build/eduos_events.log`:

```
EDUOS|1|00001234|CATEGORY|EVENT|key=value|key=value
```

* `1` protocol version, `00001234` kernel event sequence number (monotonic, per boot).
* Keys match `[A-Za-z0-9_]+`. Values are percent-encoded except `A-Za-z0-9 . _ : / -`, so a value can never contain `|`, `=` or a newline.
* Max line length 190 characters; an over-long record is dropped (counted in `dropped`), never truncated.
* A new boot always begins with `BOOT|START`; the backend resets its state on it.
* Emitters only enqueue (queue of 64 lines); the kernel main loop transmits. Timer ISR never touches the serial port.

| Category | Event | Fields |
|---|---|---|
| BOOT | START | version, architecture, boot_id |
| KERNEL | READY / PANIC / INFO / STATS | version, architecture / code, message / id, detail / created, sent, dropped |
| MODULE | READY / ERROR | name, detail / name, reason |
| PROCESS | CREATE / SNAPSHOT | pid, name, state, priority [, cpu_ticks] |
| PROCESS | STATE / EXIT | pid, state, reason / pid, exit_code, reason |
| SCHEDULER | SWITCH / SNAPSHOT | from, to, reason, total / algorithm, quantum, current_pid, switches, total_ticks, idle_ticks |
| MEMORY | SNAPSHOT | total_frames, used_frames, free_frames, frame_size, allocs, frees |
| PAGING | MAP / UNMAP / FAULT | virtual, physical, flags, result / virtual, result / address, error, expected, eip |
| SYSCALL | CALL | number, name, pid, result |
| INTERRUPT | COUNT | irq, count |
| DISK | READY / READ / WRITE | device, sectors, sector_size / lba, count, result |
| FILESYSTEM | MOUNT / FORMAT | version, block_size, total_blocks, files, result / total_blocks, result |
| FILESYSTEM | CREATE / WRITE / READ / DELETE | name, size\|bytes, files, result |

`result` is `OK` or an error name (`ERR_EXIST`, `ERR_NOENT`, `ERR_INVAL`, …). Success events are only emitted after the operation succeeded; failures carry a failure `result`.

Consumers must ignore unknown categories/events and unknown versions, and must not treat a final line without `\n` as complete.
