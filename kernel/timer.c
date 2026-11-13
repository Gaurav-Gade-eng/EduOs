#include "timer.h"
#include "interrupts.h"
#include "scheduler.h"
#include "io.h"

static volatile uint32_t ticks;
static uint32_t hz;

void timer_init(unsigned int frequency_hz) {
    hz = frequency_hz;
    uint32_t divisor = 1193182u / frequency_hz;
    outb(0x43, 0x36);                       /* channel 0, lo/hi, mode 3 */
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)(divisor >> 8));
    irq_register(0, timer_interrupt_handler);
    pic_unmask(0);
}

uint32_t timer_get_ticks(void) { return ticks; }
uint32_t timer_get_hz(void) { return hz; }

/* ISR context: counters + scheduling only, no formatting or serial output. */
void timer_interrupt_handler(void) {
    ticks++;
    scheduler_tick();
}
