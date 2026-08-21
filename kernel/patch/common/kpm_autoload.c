/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <accctl.h>
#include <common.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/container_of.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <log.h>
#include <module.h>
#include <predata.h>
#include <symbol.h>

#define INSTALLED_KPM_DIR "/data/adb/kp-next/installed-kpm/"
#define INSTALLED_KPM_NAME_LEN 128
#define INSTALLED_KPM_MAX_MODULES 256

struct installed_kpm_scan_ctx {
    struct dir_context dctx;
    char *names;
    int count;
};

struct installed_kpm_scan_ctx_int {
    struct dir_context_int dctx;
    char *names;
    int count;
};

static int installed_kpm_valid_name(const char *name, int namelen)
{
    if (!name || namelen <= 0 || namelen >= INSTALLED_KPM_NAME_LEN) return 0;
    if ((namelen == 1 && name[0] == '.') || (namelen == 2 && name[0] == '.' && name[1] == '.')) return 0;
    for (int i = 0; i < namelen; i++) {
        if (name[i] == '/' || name[i] == '\\') return 0;
    }
    return 1;
}

static bool installed_kpm_scan_actor(struct dir_context *dctx, const char *name, int namelen,
                                     loff_t offset, u64 ino, unsigned int d_type)
{
    struct installed_kpm_scan_ctx *ctx = container_of(dctx, struct installed_kpm_scan_ctx, dctx);
    if (!ctx || ctx->count >= INSTALLED_KPM_MAX_MODULES || !installed_kpm_valid_name(name, namelen)) return true;

    char *slot = ctx->names + ctx->count * INSTALLED_KPM_NAME_LEN;
    memcpy(slot, name, namelen);
    slot[namelen] = '\0';
    ctx->count++;
    return true;
}

static int installed_kpm_scan_actor_int(struct dir_context_int *dctx, const char *name, int namelen,
                                        loff_t offset, u64 ino, unsigned int d_type)
{
    struct installed_kpm_scan_ctx_int *ctx = container_of(dctx, struct installed_kpm_scan_ctx_int, dctx);
    if (!ctx || ctx->count >= INSTALLED_KPM_MAX_MODULES || !installed_kpm_valid_name(name, namelen)) return 0;

    char *slot = ctx->names + ctx->count * INSTALLED_KPM_NAME_LEN;
    memcpy(slot, name, namelen);
    slot[namelen] = '\0';
    ctx->count++;
    return 0;
}

int load_installed_kpm_modules(void)
{
    struct file *dir;
    char *names;
    int count = 0, loaded = 0, skipped = 0;
    int rc;

    names = vmalloc((size_t)INSTALLED_KPM_MAX_MODULES * INSTALLED_KPM_NAME_LEN);
    if (!names) return -ENOMEM;
    memset(names, 0, (size_t)INSTALLED_KPM_MAX_MODULES * INSTALLED_KPM_NAME_LEN);

    set_priv_sel_allow(current, true);
    dir = filp_open(INSTALLED_KPM_DIR, O_RDONLY | O_NOFOLLOW, 0);
    if (!dir || IS_ERR(dir)) {
        rc = dir ? PTR_ERR(dir) : -ENOENT;
        set_priv_sel_allow(current, false);
        kvfree(names);
        if (rc != -ENOENT) log_boot("open installed KPM directory failed: %d\n", rc);
        return rc == -ENOENT ? 0 : rc;
    }

    if (kver >= VERSION(6, 1, 0)) {
        struct installed_kpm_scan_ctx ctx = { .names = names };
        ctx.dctx.actor = installed_kpm_scan_actor;
        iterate_dir(dir, &ctx.dctx);
        count = ctx.count;
    } else {
        struct installed_kpm_scan_ctx_int ctx = { .names = names };
        ctx.dctx.actor = installed_kpm_scan_actor_int;
        iterate_dir_int(dir, &ctx.dctx);
        count = ctx.count;
    }
    filp_close(dir, 0);
    set_priv_sel_allow(current, false);

    for (int i = 0; i < count; i++) {
        char *id = names + i * INSTALLED_KPM_NAME_LEN;
        char path[INSTALLED_KPM_NAME_LEN * 2 + sizeof(INSTALLED_KPM_DIR) + 8];
        char disable[INSTALLED_KPM_NAME_LEN + sizeof(INSTALLED_KPM_DIR) + 16];
        int path_len = snprintf(path, sizeof(path), INSTALLED_KPM_DIR "%s/%s.kpm", id, id);
        int disable_len = snprintf(disable, sizeof(disable), INSTALLED_KPM_DIR "%s/disable", id);
        struct file *marker;

        if (path_len <= 0 || path_len >= (int)sizeof(path) ||
            disable_len <= 0 || disable_len >= (int)sizeof(disable)) {
            skipped++;
            continue;
        }

        set_priv_sel_allow(current, true);
        marker = filp_open(disable, O_RDONLY | O_NOFOLLOW, 0);
        if (marker && !IS_ERR(marker)) {
            filp_close(marker, 0);
            set_priv_sel_allow(current, false);
            log_boot("skip disabled installed KPM: %s\n", id);
            skipped++;
            continue;
        }
        set_priv_sel_allow(current, false);

        rc = load_module_path_event(path, 0, EXTRA_EVENT_POST_FS_DATA, 0);
        log_boot("load installed KPM: %s, event: %s, rc: %d\n", path, EXTRA_EVENT_POST_FS_DATA, rc);
        if (!rc) loaded++;
    }

    kvfree(names);
    log_boot("installed KPM loading done: loaded=%d skipped=%d total=%d\n", loaded, skipped, count);
    return loaded;
}
KP_EXPORT_SYMBOL(load_installed_kpm_modules);