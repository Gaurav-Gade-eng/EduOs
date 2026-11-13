/* PS/2 keyboard, scancode set 1, US layout, ring buffer. */
#include "keyboard.h"
#include "interrupts.h"
#include "io.h"

#define BUF 128
static volatile uint8_t buf[BUF];
static volatile uint32_t head, tail;
static bool shift, e0;

static const char normal[0x3A] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b', '\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0, '\\',
    'z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' '
};
static const char shifted[0x3A] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b', '\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,
    'A','S','D','F','G','H','J','K','L',':','"','~', 0, '|',
    'Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' '
};

static void push(char c) {
    uint32_t next = (head + 1) % BUF;
    if (next != tail) { buf[head] = (uint8_t)c; head = next; }
}

static void keyboard_irq(void) {
    uint8_t sc = inb(0x60);
    if (sc == 0xE0) { e0 = true; return; }
    if (e0) { e0 = false; return; }               /* ignore extended keys */
    if (sc == 0x2A || sc == 0x36) { shift = true; return; }
    if (sc == 0xAA || sc == 0xB6) { shift = false; return; }
    if (sc & 0x80) return;                        /* key release */
    if (sc < sizeof normal) {
        char c = shift ? shifted[sc] : normal[sc];
        if (c) push(c);
    }
}

void keyboard_init(void) {
    irq_register(1, keyboard_irq);
    pic_unmask(1);
}

int keyboard_getchar(void) {
    int c = -1;
    uint32_t f = irq_save();
    if (tail != head) { c = buf[tail]; tail = (tail + 1) % BUF; }
    irq_restore(f);
    return c;
}
