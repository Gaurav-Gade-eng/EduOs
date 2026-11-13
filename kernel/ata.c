/* ATA PIO driver, primary bus master, LBA28, polled with bounded timeouts. */
#include "ata.h"
#include "telemetry.h"
#include "console.h"
#include "config.h"
#include "io.h"

#define IO   0x1F0
#define CTRL 0x3F6
#define ST_ERR 0x01
#define ST_DRQ 0x08
#define ST_DF  0x20
#define ST_BSY 0x80
#define TIMEOUT 400000u

static bool present;
static uint32_t sectors;

static void delay400ns(void) { for (int i = 0; i < 4; i++) inb(CTRL); }

static int wait_not_busy(void) {
    for (uint32_t i = 0; i < TIMEOUT; i++)
        if (!(inb(IO + 7) & ST_BSY)) return E_OK;
    return E_TIMEOUT;
}

static int wait_drq(void) {
    for (uint32_t i = 0; i < TIMEOUT; i++) {
        uint8_t s = inb(IO + 7);
        if (s & (ST_ERR | ST_DF)) return E_IO;
        if (!(s & ST_BSY) && (s & ST_DRQ)) return E_OK;
    }
    return E_TIMEOUT;
}

int ata_init(void) {
    present = false;
    outb(IO + 6, 0xA0);                    /* select master */
    delay400ns();
    outb(IO + 2, 0); outb(IO + 3, 0); outb(IO + 4, 0); outb(IO + 5, 0);
    outb(IO + 7, 0xEC);                    /* IDENTIFY */
    if (inb(IO + 7) == 0) return E_NODEV;  /* nothing attached */
    if (wait_not_busy() != E_OK) return E_TIMEOUT;
    if (inb(IO + 4) || inb(IO + 5)) return E_NODEV;     /* not an ATA device */
    if (wait_drq() != E_OK) return E_IO;
    uint16_t id[256];
    insw(IO, id, 256);
    sectors = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
    if (sectors == 0) return E_NODEV;
    present = true;
    telemetry_emit_disk_ready("ata0", sectors, ATA_SECTOR_SIZE);
    return E_OK;
}

bool ata_present(void) { return present; }
uint32_t ata_sector_count(void) { return sectors; }

static int rw(bool write, uint32_t lba, uint32_t count, void *buf) {
    uint8_t *p = buf;
    if (!present) return E_NODEV;
    if (count == 0 || count > 255 || !buf || lba >= sectors || lba + count > sectors) return E_INVAL;
    for (uint32_t i = 0; i < count; i++, lba++, p += ATA_SECTOR_SIZE) {
        if (wait_not_busy() != E_OK) return E_TIMEOUT;
        outb(IO + 6, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
        outb(IO + 2, 1);
        outb(IO + 3, (uint8_t)lba);
        outb(IO + 4, (uint8_t)(lba >> 8));
        outb(IO + 5, (uint8_t)(lba >> 16));
        outb(IO + 7, write ? 0x30 : 0x20);
        int rc = wait_drq();
        if (rc != E_OK) return rc;
        if (write) outsw(IO, p, 256); else insw(IO, p, 256);
    }
    if (write) {
        outb(IO + 7, 0xE7);                /* flush cache */
        if (wait_not_busy() != E_OK) return E_TIMEOUT;
    }
    return E_OK;
}

int ata_read(uint32_t lba, uint32_t count, void *buf) {
    int rc = rw(false, lba, count, buf);
    telemetry_emit_disk_op("READ", lba, count, err_name(rc));
    return rc;
}

int ata_write(uint32_t lba, uint32_t count, const void *buf) {
    int rc;
    if (lba < EDUFS_START_LBA) rc = E_PERM;          /* protect bootloader + kernel */
    else rc = rw(true, lba, count, (void *)(uintptr_t)buf);
    telemetry_emit_disk_op("WRITE", lba, count, err_name(rc));
    return rc;
}

void ata_print_info(void) {
    if (!present) { console_write("disk: not present\n"); return; }
    console_printf("disk: ata0 sectors=%u (%u MiB) sector_size=%u; writes allowed from LBA %u\n", sectors,
                   sectors / 2048, ATA_SECTOR_SIZE, EDUFS_START_LBA);
}
