#ifndef EDUOS_INTERRUPTS_H
#define EDUOS_INTERRUPTS_H
#include "common.h"

/* Layout produced by isr_common in interrupts.asm (ring 0 only: no user esp/ss). */
typedef struct interrupt_frame {
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;   /* pushad order */
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;                                /* pushed by CPU */
} interrupt_frame_t;

typedef void (*irq_handler_t)(void);

void pic_init(void);
void pic_send_eoi(uint8_t irq);
void pic_unmask(uint8_t irq);
void irq_register(uint8_t irq, irq_handler_t handler);
uint32_t irq_count(uint8_t irq);

/* Called from assembly. */
void interrupt_dispatch(interrupt_frame_t *frame);

#endif
