/* Line-oriented shell running in the kernel task (PID 1). */
#include "shell.h"
#include "keyboard.h"
#include "console.h"
#include "process.h"
#include "scheduler.h"
#include "memory.h"
#include "paging.h"
#include "syscall.h"
#include "timer.h"
#include "filesystem.h"
#include "ata.h"
#include "telemetry.h"
#include "config.h"
#include "io.h"

#define LINE_MAX 8192
#define DEMO_VADDR 0x40000000u

static char line[LINE_MAX];
static uint32_t len;
static uint32_t demo_frame;       /* frame owned by the `vmap` demo mapping */
static int spawn_count;

static void prompt(void) { console_write("eduos> "); }

static void worker_entry(void) {
    uint32_t last = timer_get_ticks();
    volatile uint32_t counter = 0;
    for (;;) {
        counter++;
        if (timer_get_ticks() - last >= 500) {     /* one voluntary yield every ~5 s */
            last = timer_get_ticks();
            sys_yield();
        }
    }
}

void shell_init(void) {
    len = 0;
    console_write("EduOS " EDUOS_VERSION " (" EDUOS_ARCH ") -- type 'help'\n");
    prompt();
}

typedef struct { const char *name; const char *help; } help_t;
static const help_t helps[] = {
    {"help", "list commands"}, {"clear", "clear screen"}, {"echo <text>", "print text"},
    {"ps", "list processes"}, {"mem", "frame allocator stats"}, {"sched", "scheduler info"},
    {"yield", "yield the CPU"}, {"block <pid>", "block a task"}, {"unblock <pid>", "wake a task"},
    {"kill <pid>", "terminate a task"}, {"spawn", "start a demo worker task"}, {"uptime", "timer ticks"},
    {"touch <name>", "create file"}, {"write <name> <text>", "write file"}, {"cat <name>", "read file"},
    {"ls", "list files"}, {"rm <name>", "delete file"}, {"stat <name>", "file info"},
    {"format confirm", "format EduFS (destroys files)"}, {"disk", "disk + fs info"},
    {"vminfo", "paging info"}, {"vmap [virt phys]", "map a page (no args: demo)"},
    {"vunmap <virt>", "unmap a page"}, {"vtest", "deliberate page fault"},
    {"events", "telemetry counters"}, {"reboot", "reset the machine"},
};

static void ls_cb(const fs_stat_t *e, void *ctx) { UNUSED(ctx); console_printf("%-32s %5u B\n", e->name, e->size); }

static bool need_pid(const char *arg, uint32_t *pid) {
    if (!arg || !parse_uint(arg, pid)) { console_write("usage: <cmd> <pid>\n"); return false; }
    return true;
}

static void report(const char *what, int rc) {
    if (rc == E_OK) console_printf("%s: ok\n", what);
    else console_printf("%s: failed (%s)\n", what, err_name(rc));
}

static void run(char *cmd) {
    /* split: cmd, arg1, rest */
    char *arg1 = NULL, *rest = NULL;
    char *p = cmd;
    while (*p && *p != ' ') p++;
    if (*p) { *p++ = '\0'; while (*p == ' ') p++; if (*p) arg1 = p; }
    if (arg1) {
        char *q = arg1;
        while (*q && *q != ' ') q++;
        if (*q) { *q++ = '\0'; while (*q == ' ') q++; if (*q) rest = q; }
    }
    uint32_t v;

    if (!*cmd) return;
    else if (!strcmp(cmd, "help")) for (uint32_t i = 0; i < ARRAY_LEN(helps); i++) console_printf("  %-22s %s\n", helps[i].name, helps[i].help);
    else if (!strcmp(cmd, "clear")) console_clear();
    else if (!strcmp(cmd, "echo")) { if (arg1) console_write(arg1); if (rest) { console_write(" "); console_write(rest); } console_write("\n"); }
    else if (!strcmp(cmd, "ps")) process_list();
    else if (!strcmp(cmd, "mem")) memory_print();
    else if (!strcmp(cmd, "sched")) scheduler_print();
    else if (!strcmp(cmd, "yield")) sys_yield();
    else if (!strcmp(cmd, "block")) { if (need_pid(arg1, &v)) report("block", sys_block((int)v)); }
    else if (!strcmp(cmd, "unblock")) { if (need_pid(arg1, &v)) report("unblock", sys_unblock((int)v)); }
    else if (!strcmp(cmd, "kill")) { if (need_pid(arg1, &v)) report("kill", sys_kill((int)v)); }
    else if (!strcmp(cmd, "spawn")) {
        char name[PROCESS_NAME_MAX];
        ksnprintf(name, sizeof name, "worker-%d", ++spawn_count);
        int pid = process_create(name, worker_entry, 2);
        if (pid < 0) report("spawn", pid); else console_printf("spawned %s pid=%d\n", name, pid);
    }
    else if (!strcmp(cmd, "uptime")) console_printf("%u ticks (%u s)\n", timer_get_ticks(), timer_get_ticks() / timer_get_hz());
    else if (!strcmp(cmd, "touch")) { if (arg1) report("touch", fs_create(arg1)); else console_write("usage: touch <name>\n"); }
    else if (!strcmp(cmd, "write")) {
        if (!arg1 || !rest) console_write("usage: write <name> <text>\n");
        else report("write", fs_write(arg1, rest, (uint32_t)strlen(rest)));
    }
    else if (!strcmp(cmd, "cat")) {
        if (!arg1) console_write("usage: cat <name>\n");
        else {
            static char buf[FS_MAX_FILE + 1];
            uint32_t n = 0;
            int rc = fs_read(arg1, buf, FS_MAX_FILE, &n);
            if (rc == E_OK) { buf[n] = '\0'; console_write(buf); console_write("\n"); } else report("cat", rc);
        }
    }
    else if (!strcmp(cmd, "ls")) { int rc = fs_list(ls_cb, NULL); if (rc != E_OK) report("ls", rc); }
    else if (!strcmp(cmd, "rm")) { if (arg1) report("rm", fs_delete(arg1)); else console_write("usage: rm <name>\n"); }
    else if (!strcmp(cmd, "stat")) {
        fs_stat_t s;
        if (!arg1) console_write("usage: stat <name>\n");
        else if (fs_stat(arg1, &s) == E_OK) console_printf("%s: %u bytes, %u blocks\n", s.name, s.size, s.blocks);
        else console_write("stat: not found\n");
    }
    else if (!strcmp(cmd, "format")) {
        if (arg1 && !strcmp(arg1, "confirm")) report("format", fs_format());
        else console_write("This ERASES the EduFS volume. Run 'format confirm' to proceed.\n");
    }
    else if (!strcmp(cmd, "disk")) { ata_print_info(); fs_print_info(); }
    else if (!strcmp(cmd, "vminfo")) paging_print_info();
    else if (!strcmp(cmd, "vmap")) {
        if (!arg1) {
            if (demo_frame) { console_write("demo mapping already active; vunmap 0x40000000 first\n"); return; }
            void *f = frame_alloc_zeroed();
            if (!f) { console_write("vmap: out of frames\n"); return; }
            int rc = paging_map_page(DEMO_VADDR, (uint32_t)(uintptr_t)f, PAGE_RW);
            if (rc != E_OK) { frame_free(f); report("vmap", rc); return; }
            demo_frame = (uint32_t)(uintptr_t)f;
            *(volatile uint32_t *)(uintptr_t)DEMO_VADDR = 0xED00ED00u;
            uint32_t back = *(volatile uint32_t *)(uintptr_t)f;       /* read through the identity mapping */
            console_printf("mapped 0x%08x -> 0x%08x; wrote via virtual, read via physical: 0x%08x\n", DEMO_VADDR, demo_frame, back);
        } else {
            uint32_t va, pa;
            if (!rest || !parse_uint(arg1, &va) || !parse_uint(rest, &pa)) { console_write("usage: vmap [virt phys]\n"); return; }
            report("vmap", paging_map_page(va, pa, PAGE_RW));
        }
    }
    else if (!strcmp(cmd, "vunmap")) {
        if (!arg1 || !parse_uint(arg1, &v)) { console_write("usage: vunmap <virt>\n"); return; }
        int rc = paging_unmap_page(v);
        if (rc == E_OK && v == DEMO_VADDR && demo_frame) { frame_free((void *)(uintptr_t)demo_frame); demo_frame = 0; }
        report("vunmap", rc);
    }
    else if (!strcmp(cmd, "vtest")) {
        uint32_t val = 0;
        bool faulted = paging_probe(0x50000000u, &val);
        console_printf("probe of unmapped 0x50000000: %s\n", faulted ? "page fault caught and recovered" : "NO FAULT (unexpected)");
    }
    else if (!strcmp(cmd, "events")) {
        console_printf("telemetry: created=%u sent=%u dropped=%u; syscalls=%u; faults=%u\n", telemetry_created(),
                       telemetry_sent(), telemetry_dropped(), syscall_total(), paging_fault_count());
    }
    else if (!strcmp(cmd, "reboot")) { outb(0x64, 0xFE); }
    else console_printf("unknown command: %s (try 'help')\n", cmd);
}

void shell_poll(void) {
    int c;
    while ((c = keyboard_getchar()) >= 0) {
        if (c == '\n') {
            console_putchar('\n');
            line[len] = '\0';
            run(line);
            len = 0;
            prompt();
        } else if (c == '\b') {
            if (len > 0) { len--; console_backspace(); }
        } else if (c >= 32 && c < 127 && len < LINE_MAX - 1) {
            line[len++] = (char)c;
            console_putchar((char)c);
        }
    }
}
