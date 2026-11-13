/*
 * EduFS v1 -- see docs/filesystem-design.md. 512-byte blocks, volume starts at EDUFS_START_LBA.
 *   block 0            superblock
 *   bitmap blocks      1 bit per block (set = used)
 *   inode blocks       64 inodes x 128 B; the inode table IS the flat root directory
 *   data blocks        file contents (12 direct blocks per file, max 6144 B)
 * Write-through: metadata and data are written to disk by each operation.
 * Mount never formats; fs_format() is explicit.
 */
#include "filesystem.h"
#include "ata.h"
#include "telemetry.h"
#include "console.h"
#include "config.h"

#define BLOCK 512u
#define MAGIC 0x53464445u              /* 'EDFS' little-endian */
#define VERSION 1u
#define MAX_INODES 64u
#define INODE_SIZE 128u
#define DIRECT 12u
#define MAX_BLOCKS 65536u
#define INODE_BLOCKS ((MAX_INODES * INODE_SIZE) / BLOCK)   /* 16 */

typedef struct {
    uint32_t magic, version, block_size, total_blocks;
    uint32_t bitmap_start, bitmap_blocks, inode_start, inode_blocks, data_start, inode_count;
    uint8_t  reserved[BLOCK - 10 * 4];
} superblock_t;

typedef struct {
    uint32_t used;
    uint32_t size;
    char     name[FS_NAME_MAX];
    uint32_t blocks[DIRECT];
    uint8_t  reserved[INODE_SIZE - 8 - FS_NAME_MAX - DIRECT * 4];
} inode_t;

_Static_assert(sizeof(superblock_t) == BLOCK, "superblock size");
_Static_assert(sizeof(inode_t) == INODE_SIZE, "inode size");

static superblock_t sb;
static uint8_t bitmap[MAX_BLOCKS / 8];
static inode_t inodes[MAX_INODES];
static bool mounted;

static int rd(uint32_t blk, void *buf, uint32_t n) { return ata_read(EDUFS_START_LBA + blk, n, buf); }
static int wr(uint32_t blk, const void *buf, uint32_t n) { return ata_write(EDUFS_START_LBA + blk, n, buf); }

static bool bit(uint32_t b) { return bitmap[b / 8] & (1u << (b % 8)); }
static void bset(uint32_t b) { bitmap[b / 8] |= (uint8_t)(1u << (b % 8)); }
static void bclr(uint32_t b) { bitmap[b / 8] &= (uint8_t)~(1u << (b % 8)); }

static int flush_bitmap_block(uint32_t bitmap_blk) { return wr(sb.bitmap_start + bitmap_blk, bitmap + bitmap_blk * BLOCK, 1); }
static int flush_inode(uint32_t idx) {
    uint32_t blk = idx / (BLOCK / INODE_SIZE);
    return wr(sb.inode_start + blk, (uint8_t *)inodes + blk * BLOCK, 1);
}

bool fs_mounted(void) { return mounted; }

uint32_t fs_file_count(void) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < MAX_INODES; i++) if (inodes[i].used) n++;
    return mounted ? n : 0;
}

static bool valid_name(const char *name) {
    if (!name || !*name) return false;
    size_t len = strlen(name);
    if (len >= FS_NAME_MAX) return false;
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-')) return false;
    }
    return true;
}

static int find(const char *name) {
    for (uint32_t i = 0; i < MAX_INODES; i++)
        if (inodes[i].used && strcmp(inodes[i].name, name) == 0) return (int)i;
    return -1;
}

int fs_mount(void) {
    mounted = false;
    if (!ata_present()) return E_NODEV;
    if (rd(0, &sb, 1) != E_OK) { telemetry_emit_fs_mount(0, 0, 0, 0, "ERR_IO"); return E_IO; }
    uint32_t bm_blocks = sb.total_blocks / 8 / BLOCK + 1;
    if (sb.magic != MAGIC) { telemetry_emit_fs_mount(0, 0, 0, 0, "NOT_FORMATTED"); return E_NOFS; }
    if (sb.version != VERSION || sb.block_size != BLOCK || sb.total_blocks == 0 || sb.total_blocks > MAX_BLOCKS ||
        sb.inode_count != MAX_INODES || sb.inode_blocks != INODE_BLOCKS || sb.bitmap_blocks > bm_blocks ||
        sb.data_start >= sb.total_blocks || sb.bitmap_start + sb.bitmap_blocks > sb.inode_start ||
        sb.inode_start + sb.inode_blocks > sb.data_start ||
        EDUFS_START_LBA + sb.total_blocks > ata_sector_count()) {
        telemetry_emit_fs_mount(sb.version, sb.block_size, sb.total_blocks, 0, "ERR_CORRUPT");
        return E_NOFS;
    }
    memset(bitmap, 0, sizeof bitmap);
    if (rd(sb.bitmap_start, bitmap, sb.bitmap_blocks) != E_OK ||
        rd(sb.inode_start, inodes, sb.inode_blocks) != E_OK) {
        telemetry_emit_fs_mount(sb.version, sb.block_size, sb.total_blocks, 0, "ERR_IO");
        return E_IO;
    }
    mounted = true;
    telemetry_emit_fs_mount(sb.version, sb.block_size, sb.total_blocks, fs_file_count(), "OK");
    return E_OK;
}

int fs_format(void) {
    if (!ata_present()) return E_NODEV;
    uint32_t avail = ata_sector_count() > EDUFS_START_LBA ? ata_sector_count() - EDUFS_START_LBA : 0;
    uint32_t total = avail < MAX_BLOCKS ? avail : MAX_BLOCKS;
    if (total < 256) { telemetry_emit_fs_format(0, "ERR_NOSPC"); return E_NOSPC; }

    memset(&sb, 0, sizeof sb);
    sb.magic = MAGIC; sb.version = VERSION; sb.block_size = BLOCK; sb.total_blocks = total;
    sb.bitmap_start = 1;
    sb.bitmap_blocks = (total / 8 + BLOCK - 1) / BLOCK;
    sb.inode_start = sb.bitmap_start + sb.bitmap_blocks;
    sb.inode_blocks = INODE_BLOCKS;
    sb.data_start = sb.inode_start + sb.inode_blocks;
    sb.inode_count = MAX_INODES;

    memset(bitmap, 0, sizeof bitmap);
    for (uint32_t b = 0; b < sb.data_start; b++) bset(b);
    memset(inodes, 0, sizeof inodes);

    int rc = wr(sb.bitmap_start, bitmap, sb.bitmap_blocks);
    if (rc == E_OK) rc = wr(sb.inode_start, inodes, sb.inode_blocks);
    if (rc == E_OK) rc = wr(0, &sb, 1);        /* superblock last: a half-written format stays "unformatted" */
    telemetry_emit_fs_format(total, err_name(rc));
    if (rc != E_OK) { mounted = false; return rc; }
    mounted = true;
    telemetry_emit_fs_mount(sb.version, sb.block_size, sb.total_blocks, 0, "OK");
    return E_OK;
}

static int alloc_blocks(uint32_t n, uint32_t *out) {
    uint32_t got = 0;
    for (uint32_t b = sb.data_start; b < sb.total_blocks && got < n; b++)
        if (!bit(b)) { bset(b); out[got++] = b; }
    if (got < n) { for (uint32_t i = 0; i < got; i++) bclr(out[i]); return E_NOSPC; }
    return E_OK;
}

static int flush_all_bitmap(void) {
    for (uint32_t i = 0; i < sb.bitmap_blocks; i++) {
        int rc = flush_bitmap_block(i);
        if (rc != E_OK) return rc;
    }
    return E_OK;
}

int fs_create(const char *name) {
    int rc = E_OK;
    if (!mounted) rc = E_NOFS;
    else if (!valid_name(name)) rc = E_INVAL;
    else if (find(name) >= 0) rc = E_EXIST;
    else {
        int slot = -1;
        for (uint32_t i = 0; i < MAX_INODES; i++) if (!inodes[i].used) { slot = (int)i; break; }
        if (slot < 0) rc = E_NOSPC;
        else {
            memset(&inodes[slot], 0, sizeof(inode_t));
            inodes[slot].used = 1;
            strlcpy(inodes[slot].name, name, FS_NAME_MAX);
            rc = flush_inode((uint32_t)slot);
            if (rc != E_OK) inodes[slot].used = 0;
        }
    }
    telemetry_emit_fs_op("CREATE", valid_name(name) ? name : "invalid", "size", 0, fs_file_count(), err_name(rc));
    return rc;
}

int fs_write(const char *name, const void *data, uint32_t len) {
    int rc = E_OK;
    if (!mounted) rc = E_NOFS;
    else if (!valid_name(name) || (len && !data)) rc = E_INVAL;
    else if (len > FS_MAX_FILE) rc = E_NOSPC;
    else {
        int idx = find(name);
        if (idx < 0) {
            rc = fs_create(name);                       /* emits its own CREATE event */
            if (rc == E_OK) idx = find(name);
        }
        if (rc == E_OK) {
            inode_t *in = &inodes[idx];
            uint32_t need = (len + BLOCK - 1) / BLOCK;
            uint32_t nb[DIRECT];
            rc = alloc_blocks(need, nb);                /* allocate before touching old data */
            if (rc == E_OK) {
                const uint8_t *src = data;
                for (uint32_t i = 0; i < need && rc == E_OK; i++) {
                    uint8_t blk[BLOCK];
                    memset(blk, 0, BLOCK);
                    uint32_t chunk = len - i * BLOCK < BLOCK ? len - i * BLOCK : BLOCK;
                    memcpy(blk, src + i * BLOCK, chunk);
                    rc = wr(nb[i], blk, 1);
                }
                if (rc != E_OK) {
                    for (uint32_t i = 0; i < need; i++) bclr(nb[i]);   /* roll back */
                } else {
                    for (uint32_t i = 0; i < DIRECT; i++)
                        if (in->blocks[i]) { bclr(in->blocks[i]); in->blocks[i] = 0; }
                    for (uint32_t i = 0; i < need; i++) in->blocks[i] = nb[i];
                    in->size = len;
                    rc = flush_inode((uint32_t)idx);
                    if (rc == E_OK) rc = flush_all_bitmap();
                }
            }
        }
    }
    telemetry_emit_fs_op("WRITE", valid_name(name) ? name : "invalid", "size", len, fs_file_count(), err_name(rc));
    return rc;
}

int fs_read(const char *name, void *buf, uint32_t max, uint32_t *bytes_out) {
    int rc = E_OK;
    uint32_t got = 0;
    if (!mounted) rc = E_NOFS;
    else if (!valid_name(name) || !buf) rc = E_INVAL;
    else {
        int idx = find(name);
        if (idx < 0) rc = E_NOENT;
        else {
            inode_t *in = &inodes[idx];
            uint8_t blk[BLOCK];
            uint32_t want = in->size < max ? in->size : max;
            for (uint32_t i = 0; got < want && rc == E_OK; i++) {
                rc = rd(in->blocks[i], blk, 1);
                if (rc != E_OK) break;
                uint32_t chunk = want - got < BLOCK ? want - got : BLOCK;
                memcpy((uint8_t *)buf + got, blk, chunk);
                got += chunk;
            }
        }
    }
    if (bytes_out) *bytes_out = got;
    telemetry_emit_fs_op("READ", valid_name(name) ? name : "invalid", "bytes", got, fs_file_count(), err_name(rc));
    return rc;
}

int fs_delete(const char *name) {
    int rc = E_OK;
    if (!mounted) rc = E_NOFS;
    else if (!valid_name(name)) rc = E_INVAL;
    else {
        int idx = find(name);
        if (idx < 0) rc = E_NOENT;
        else {
            inode_t *in = &inodes[idx];
            for (uint32_t i = 0; i < DIRECT; i++) if (in->blocks[i]) bclr(in->blocks[i]);
            memset(in, 0, sizeof *in);
            rc = flush_inode((uint32_t)idx);
            if (rc == E_OK) rc = flush_all_bitmap();
        }
    }
    telemetry_emit_fs_op("DELETE", valid_name(name) ? name : "invalid", NULL, 0, fs_file_count(), err_name(rc));
    return rc;
}

int fs_stat(const char *name, fs_stat_t *out) {
    if (!mounted) return E_NOFS;
    if (!valid_name(name) || !out) return E_INVAL;
    int idx = find(name);
    if (idx < 0) return E_NOENT;
    strlcpy(out->name, inodes[idx].name, FS_NAME_MAX);
    out->size = inodes[idx].size;
    out->blocks = (inodes[idx].size + BLOCK - 1) / BLOCK;
    return E_OK;
}

int fs_list(fs_list_cb cb, void *ctx) {
    if (!mounted) return E_NOFS;
    for (uint32_t i = 0; i < MAX_INODES; i++) {
        if (!inodes[i].used) continue;
        fs_stat_t s;
        strlcpy(s.name, inodes[i].name, FS_NAME_MAX);
        s.size = inodes[i].size;
        s.blocks = (inodes[i].size + BLOCK - 1) / BLOCK;
        cb(&s, ctx);
    }
    return E_OK;
}

void fs_print_info(void) {
    if (!mounted) { console_write("EduFS: not mounted (run 'format confirm' on a blank disk)\n"); return; }
    uint32_t used = 0;
    for (uint32_t b = 0; b < sb.total_blocks; b++) if (bit(b)) used++;
    console_printf("EduFS v%u: block=%u total=%u used=%u free=%u files=%u/%u\n", sb.version, sb.block_size,
                   sb.total_blocks, used, sb.total_blocks - used, fs_file_count(), MAX_INODES);
}
