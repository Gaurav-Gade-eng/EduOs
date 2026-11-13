#ifndef EDUOS_FILESYSTEM_H
#define EDUOS_FILESYSTEM_H
#include "common.h"

#define FS_NAME_MAX   32                 /* including NUL */
#define FS_MAX_FILE   6144u              /* 12 direct blocks * 512 */

typedef struct {
    char     name[FS_NAME_MAX];
    uint32_t size;
    uint32_t blocks;
} fs_stat_t;

typedef void (*fs_list_cb)(const fs_stat_t *entry, void *ctx);

int  fs_mount(void);                     /* never formats */
int  fs_format(void);                    /* explicit, destructive */
bool fs_mounted(void);
int  fs_create(const char *name);
int  fs_write(const char *name, const void *data, uint32_t len);   /* create-or-replace */
int  fs_read(const char *name, void *buf, uint32_t max, uint32_t *bytes_out);
int  fs_delete(const char *name);
int  fs_stat(const char *name, fs_stat_t *out);
int  fs_list(fs_list_cb cb, void *ctx);
uint32_t fs_file_count(void);
void fs_print_info(void);

#endif
