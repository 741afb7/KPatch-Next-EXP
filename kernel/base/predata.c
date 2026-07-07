/* SPDX-License-Identifier: GPL-2.0-or-later */
/* 
 * Copyright (C) 2023 bmax121. All Rights Reserved.
 */

#include <predata.h>
#include <common.h>
#include <log.h>
#include <sha256.h>
#include <symbol.h>
#include <kconfig.h>

#include "start.h"
#include "pgtable.h"
#include "baselib.h"

extern start_preset_t start_preset;

struct patch_config *patch_config = 0;
KP_EXPORT_SYMBOL(patch_config);

static const char *kernel_config = 0;
static int kernel_config_size = 0;
static char kconfig_value_buf[128];
static const char kconfig_no_value[] = "n";
static int kconfig_scan_done = 0;

static void ensure_kconfig_loaded(void);

static uint64_t _rand_next = 1000000007;

uint64_t rand_next()
{
    _rand_next = 1103515245 * _rand_next + 12345;
    return _rand_next;
}

const char *get_build_time()
{
    return setup_header->compile_time;
}

static const char *skip_config_prefix(const char *name)
{
    return !lib_strncmp(name, "CONFIG_", 7) ? name + 7 : name;
}

static int config_name_eq(const char *line, const char *name, int name_len)
{
    return !lib_strncmp(line, "CONFIG_", 7) && !lib_strncmp(line + 7, name, name_len) && line[7 + name_len] == '=';
}

static const char *find_config_line(const char *name)
{
    const char *cfg = kernel_config;
    const char *end = kernel_config + kernel_config_size;
    int name_len;

    if (!cfg || !name) return 0;

    name = skip_config_prefix(name);
    name_len = lib_strlen(name);
    if (!name_len) return 0;

    while (cfg < end && *cfg) {
        const char *line = cfg;
        const char *next = lib_memchr(line, '\n', end - line);
        if (!next) next = end;

        if (line + 7 + name_len < next && config_name_eq(line, name, name_len)) return line + 7 + name_len + 1;

        cfg = next + 1;
    }

    return 0;
}

static int config_not_set_eq(const char *line, const char *next, const char *name, int name_len)
{
    const char prefix[] = "# CONFIG_";
    const char suffix[] = " is not set";
    int prefix_len = sizeof(prefix) - 1;
    int suffix_len = sizeof(suffix) - 1;

    return next - line == prefix_len + name_len + suffix_len &&
           !lib_strncmp(line, prefix, prefix_len) &&
           !lib_strncmp(line + prefix_len, name, name_len) &&
           !lib_strncmp(line + prefix_len + name_len, suffix, suffix_len);
}

static int config_is_not_set(const char *name)
{
    const char *cfg = kernel_config;
    const char *end = kernel_config + kernel_config_size;
    int name_len;

    if (!cfg || !name) return 0;

    name = skip_config_prefix(name);
    name_len = lib_strlen(name);
    if (!name_len) return 0;

    while (cfg < end && *cfg) {
        const char *line = cfg;
        const char *next = lib_memchr(line, '\n', end - line);
        if (!next) next = end;

        if (config_not_set_eq(line, next, name, name_len)) return 1;

        cfg = next + 1;
    }

    return 0;
}

int kp_kconfig_available(void)
{
    ensure_kconfig_loaded();
    return kernel_config && kernel_config_size > 0;
}
KP_EXPORT_SYMBOL(kp_kconfig_available);

int kp_kconfig_enabled(const char *name)
{
    ensure_kconfig_loaded();
    const char *value = find_config_line(name);

    return value && (*value == 'y' || *value == 'm');
}
KP_EXPORT_SYMBOL(kp_kconfig_enabled);

const char *kp_kconfig_value(const char *name)
{
    ensure_kconfig_loaded();
    const char *value = find_config_line(name);
    int i = 0;

    if (!value) return config_is_not_set(name) ? kconfig_no_value : 0;

    while (i < (int)sizeof(kconfig_value_buf) - 1 && value[i] && value[i] != '\n' && value[i] != '\r') {
        kconfig_value_buf[i] = value[i];
        i++;
    }
    kconfig_value_buf[i] = 0;
    return kconfig_value_buf;
}
KP_EXPORT_SYMBOL(kp_kconfig_value);

const char *kp_kconfig_data(void)
{
    ensure_kconfig_loaded();
    return kernel_config;
}
KP_EXPORT_SYMBOL(kp_kconfig_data);

int kp_kconfig_size(void)
{
    ensure_kconfig_loaded();
    return kernel_config_size;
}
KP_EXPORT_SYMBOL(kp_kconfig_size);

static int init_kconfig_extra(const patch_extra_item_t *extra, const char *arg, const void *con, void *udata)
{
    (void)arg;
    (void)udata;

    if (extra->type != EXTRA_TYPE_KCONFIG) return 0;

    kernel_config = con;
    kernel_config_size = extra->con_size;
    log_boot("kconfig extra found by scan, size: %d\n", kernel_config_size);
    return 1;
}

static void ensure_kconfig_loaded(void)
{
    if (kernel_config || kconfig_scan_done) return;
    if (!_kp_extra_start || !_kp_extra_end || _kp_extra_start >= _kp_extra_end) return;

    if (start_preset.kconfig_offset > 0 && start_preset.kconfig_size > 0 &&
        start_preset.kconfig_offset + start_preset.kconfig_size <= start_preset.extra_size) {
        kernel_config = (const char *)(_kp_extra_start + start_preset.kconfig_offset);
        kernel_config_size = start_preset.kconfig_size;
        kconfig_scan_done = 1;
        log_boot("kconfig extra found by preset, offset: %lld, size: %d\n", start_preset.kconfig_offset,
                 kernel_config_size);
        return;
    }

    on_each_extra_item(init_kconfig_extra, 0);
    kconfig_scan_done = 1;
    if (!kernel_config) log_boot("kconfig extra not found\n");
}

int on_each_extra_item(int (*callback)(const patch_extra_item_t *extra, const char *arg, const void *con, void *udata),
                       void *udata)
{
    int rc = 0;
    uint64_t item_addr = _kp_extra_start;
    while (item_addr < _kp_extra_end) {
        patch_extra_item_t *item = (patch_extra_item_t *)item_addr;
        if (item->type == EXTRA_TYPE_NONE) break;
        if (lib_memcmp(item->magic, EXTRA_HDR_MAGIC, sizeof(item->magic))) break;
        const char *args = item->args_size > 0 ? (const char *)(item_addr + sizeof(patch_extra_item_t)) : 0;
        const void *con = (void *)(item_addr + sizeof(patch_extra_item_t) + item->args_size);
        rc = callback(item, args, con, udata);
        if (rc) break;
        item_addr += sizeof(patch_extra_item_t);
        item_addr += item->args_size;
        item_addr += item->con_size;
    }
    return rc;
}

void predata_init()
{
    char *compile_time = start_preset.header.compile_time;

    // RNG
    _rand_next *= kernel_va;
    _rand_next *= kver;
    _rand_next *= kpver;
    _rand_next *= _kp_region_start;
    _rand_next *= _kp_region_end;
    if (*(uint64_t *)compile_time) _rand_next *= *(uint64_t *)compile_time;

    patch_config = &start_preset.patch_config;

    ensure_kconfig_loaded();

    for (uintptr_t addr = (uint64_t)patch_config; addr < (uintptr_t)patch_config + PATCH_CONFIG_LEN;
         addr += sizeof(uintptr_t)) {
        uintptr_t *p = (uintptr_t *)addr;
        if (*p) *p += kernel_va;
    }

    dsb(ish);
}