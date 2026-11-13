#include "panic.h"
#include "console.h"
#include "telemetry.h"
#include "io.h"

void kernel_panic(const char *code, const char *msg) {
    cpu_cli();
    console_set_color(15, 4);
    console_printf("\n*** KERNEL PANIC [%s]: %s ***\n", code, msg);
    console_set_color(7, 0);
    telemetry_emit_panic(code, msg);
    telemetry_flush();
    for (;;) cpu_hlt();
}
