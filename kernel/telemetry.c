/*
 * Kernel telemetry: typed event builders -> bounded queue -> COM1.
 * See docs/telemetry-protocol.md. Emitters are interrupt-safe (they only format into a
 * line buffer and enqueue); telemetry_flush() does the slow serial transmission.
 */
#include "telemetry.h"
#include "serial.h"
#include "config.h"
#include "io.h"

#define LINE_MAX   192
#define QUEUE_LINES 64

static char     queue[QUEUE_LINES][LINE_MAX];
static uint32_t q_head, q_count;
static uint32_t seq, created, sent, dropped;
static bool     flushing;

/* line under construction (guarded by interrupts being disabled) */
static char     line[LINE_MAX];
static uint32_t line_len;
static bool     line_overflow;
static uint32_t line_flags;

static void raw(const char *s) {
    while (*s) {
        if (line_len < LINE_MAX - 2) line[line_len++] = *s;
        else line_overflow = true;
        s++;
    }
}

static void begin(const char *category, const char *event) {
    line_flags = irq_save();
    line_len = 0;
    line_overflow = false;
    char head[48];
    ksnprintf(head, sizeof head, "EDUOS|%u|%08u|", (unsigned)TELEMETRY_VERSION, (unsigned)++seq);
    raw(head);
    raw(category);
    raw("|");
    raw(event);
}

static void key(const char *k) { raw("|"); raw(k); raw("="); }

/* percent-encode everything outside [A-Za-z0-9._:/-] */
static void kv_str(const char *k, const char *v) {
    key(k);
    if (!v) v = "";
    for (; *v; v++) {
        unsigned char c = (unsigned char)*v;
        bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                     c == '.' || c == '_' || c == ':' || c == '/' || c == '-';
        if (plain) {
            char t[2] = { (char)c, 0 };
            raw(t);
        } else {
            char t[4];
            ksnprintf(t, sizeof t, "%%%02x", (unsigned)c);
            raw(t);
        }
    }
}

static void kv_u(const char *k, uint32_t v) {
    char t[12];
    ksnprintf(t, sizeof t, "%u", (unsigned)v);
    key(k); raw(t);
}

static void kv_i(const char *k, int32_t v) {
    char t[12];
    ksnprintf(t, sizeof t, "%d", (int)v);
    key(k); raw(t);
}

static void kv_hex(const char *k, uint32_t v) {
    char t[12];
    ksnprintf(t, sizeof t, "0x%08x", (unsigned)v);
    key(k); raw(t);
}

static void end(void) {
    created++;
    if (line_overflow) {
        /* never emit a truncated record that could be mistaken for a valid one */
        dropped++;
    } else if (q_count == QUEUE_LINES) {
        dropped++;
    } else {
        uint32_t tail = (q_head + q_count) % QUEUE_LINES;
        memcpy(queue[tail], line, line_len);
        queue[tail][line_len] = '\0';
        q_count++;
    }
    irq_restore(line_flags);
}

void telemetry_init(void) {
    q_head = q_count = 0;
    seq = created = sent = dropped = 0;
}

void telemetry_flush(void) {
    if (flushing) return;
    flushing = true;
    for (;;) {
        char out[LINE_MAX];
        uint32_t f = irq_save();
        if (q_count == 0) { irq_restore(f); break; }
        strlcpy(out, queue[q_head], sizeof out);
        q_head = (q_head + 1) % QUEUE_LINES;
        q_count--;
        sent++;
        irq_restore(f);
        serial_write(out);
        serial_write_char('\n');
    }
    flushing = false;
}

uint32_t telemetry_created(void) { return created; }
uint32_t telemetry_sent(void) { return sent; }
uint32_t telemetry_dropped(void) { return dropped; }

/* ------------------------------------------------------------------ BOOT / MODULE / KERNEL */

static uint8_t cmos(uint8_t reg) { outb(0x70, reg); return inb(0x71); }

void telemetry_emit_boot(const char *version, const char *architecture) {
    /* boot_id from the RTC (raw BCD day/hour/min/sec) so a restarted VM is distinguishable */
    uint32_t id = ((uint32_t)cmos(0x07) << 24) | ((uint32_t)cmos(0x04) << 16) |
                  ((uint32_t)cmos(0x02) << 8) | cmos(0x00);
    begin("BOOT", "START");
    kv_str("version", version);
    kv_str("architecture", architecture);
    kv_hex("boot_id", id);
    end();
}

void telemetry_emit_kernel_ready(void) {
    begin("KERNEL", "READY");
    kv_str("version", EDUOS_VERSION);
    kv_str("architecture", EDUOS_ARCH);
    end();
}

void telemetry_emit_module_ready(const char *name, const char *detail) {
    begin("MODULE", "READY");
    kv_str("name", name);
    if (detail) kv_str("detail", detail);
    end();
}

void telemetry_emit_module_error(const char *name, const char *reason) {
    begin("MODULE", "ERROR");
    kv_str("name", name);
    kv_str("reason", reason);
    end();
}

void telemetry_emit_info(const char *message_id, const char *detail) {
    begin("KERNEL", "INFO");
    kv_str("id", message_id);
    if (detail) kv_str("detail", detail);
    end();
}

void telemetry_emit_panic(const char *code, const char *message) {
    begin("KERNEL", "PANIC");
    kv_str("code", code);
    kv_str("message", message);
    end();
}

void telemetry_emit_stats(void) {
    uint32_t c, s, d;
    uint32_t f = irq_save();
    c = created; s = sent; d = dropped;
    irq_restore(f);
    begin("KERNEL", "STATS");
    kv_u("created", c);
    kv_u("sent", s);
    kv_u("dropped", d);
    end();
}

/* ------------------------------------------------------------------ PROCESS / SCHEDULER */

void telemetry_emit_process_create(uint32_t pid, const char *name, const char *state, int priority) {
    begin("PROCESS", "CREATE");
    kv_u("pid", pid); kv_str("name", name); kv_str("state", state); kv_i("priority", priority);
    end();
}

void telemetry_emit_process_state(uint32_t pid, const char *state, const char *reason) {
    begin("PROCESS", "STATE");
    kv_u("pid", pid); kv_str("state", state); kv_str("reason", reason);
    end();
}

void telemetry_emit_process_exit(uint32_t pid, int exit_code, const char *reason) {
    begin("PROCESS", "EXIT");
    kv_u("pid", pid); kv_i("exit_code", exit_code); kv_str("reason", reason);
    end();
}

void telemetry_emit_process_snapshot(uint32_t pid, const char *name, const char *state,
                                     int priority, uint32_t cpu_ticks) {
    begin("PROCESS", "SNAPSHOT");
    kv_u("pid", pid); kv_str("name", name); kv_str("state", state);
    kv_i("priority", priority); kv_u("cpu_ticks", cpu_ticks);
    end();
}

void telemetry_emit_scheduler_snapshot(const char *algorithm, uint32_t quantum, uint32_t current_pid,
                                       uint32_t switches, uint32_t total_ticks, uint32_t idle_ticks) {
    begin("SCHEDULER", "SNAPSHOT");
    kv_str("algorithm", algorithm); kv_u("quantum", quantum); kv_u("current_pid", current_pid);
    kv_u("switches", switches); kv_u("total_ticks", total_ticks); kv_u("idle_ticks", idle_ticks);
    end();
}

void telemetry_emit_scheduler_switch(uint32_t from_pid, uint32_t to_pid, const char *reason, uint32_t total) {
    begin("SCHEDULER", "SWITCH");
    kv_u("from", from_pid); kv_u("to", to_pid); kv_str("reason", reason); kv_u("total", total);
    end();
}

/* ------------------------------------------------------------------ MEMORY / PAGING */

void telemetry_emit_memory_snapshot(uint32_t total, uint32_t used, uint32_t free_frames, uint32_t frame_size,
                                    uint32_t allocs, uint32_t frees) {
    begin("MEMORY", "SNAPSHOT");
    kv_u("total_frames", total); kv_u("used_frames", used); kv_u("free_frames", free_frames);
    kv_u("frame_size", frame_size); kv_u("allocs", allocs); kv_u("frees", frees);
    end();
}

void telemetry_emit_paging_map(uint32_t virt, uint32_t phys, const char *flags, const char *result) {
    begin("PAGING", "MAP");
    kv_hex("virtual", virt); kv_hex("physical", phys); kv_str("flags", flags); kv_str("result", result);
    end();
}

void telemetry_emit_paging_unmap(uint32_t virt, const char *result) {
    begin("PAGING", "UNMAP");
    kv_hex("virtual", virt); kv_str("result", result);
    end();
}

void telemetry_emit_paging_fault(uint32_t address, uint32_t error, bool expected, uint32_t eip) {
    begin("PAGING", "FAULT");
    kv_hex("address", address); kv_hex("error", error); kv_u("expected", expected ? 1 : 0);
    kv_hex("eip", eip);
    end();
}

/* ------------------------------------------------------------------ SYSCALL / INTERRUPT */

void telemetry_emit_syscall(uint32_t number, const char *name, uint32_t pid, int result) {
    begin("SYSCALL", "CALL");
    kv_u("number", number); kv_str("name", name); kv_u("pid", pid); kv_i("result", result);
    end();
}

void telemetry_emit_interrupt_count(uint32_t irq, uint32_t count) {
    begin("INTERRUPT", "COUNT");
    kv_u("irq", irq); kv_u("count", count);
    end();
}

/* ------------------------------------------------------------------ DISK / FILESYSTEM */

void telemetry_emit_disk_ready(const char *device, uint32_t sectors, uint32_t sector_size) {
    begin("DISK", "READY");
    kv_str("device", device); kv_u("sectors", sectors); kv_u("sector_size", sector_size);
    end();
}

void telemetry_emit_disk_op(const char *op, uint32_t lba, uint32_t count, const char *result) {
    begin("DISK", op);
    kv_u("lba", lba); kv_u("count", count); kv_str("result", result);
    end();
}

void telemetry_emit_fs_mount(uint32_t version, uint32_t block_size, uint32_t total_blocks,
                             uint32_t files, const char *result) {
    begin("FILESYSTEM", "MOUNT");
    kv_u("version", version); kv_u("block_size", block_size); kv_u("total_blocks", total_blocks);
    kv_u("files", files); kv_str("result", result);
    end();
}

void telemetry_emit_fs_format(uint32_t total_blocks, const char *result) {
    begin("FILESYSTEM", "FORMAT");
    kv_u("total_blocks", total_blocks); kv_str("result", result);
    end();
}

void telemetry_emit_fs_op(const char *event, const char *name, const char *size_key, uint32_t size,
                          uint32_t files, const char *result) {
    begin("FILESYSTEM", event);
    kv_str("name", name);
    if (size_key) kv_u(size_key, size);
    kv_u("files", files);
    kv_str("result", result);
    end();
}
