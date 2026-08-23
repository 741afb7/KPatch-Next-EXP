/* SPDX-License-Identifier: GPL-2.0-or-later */
/* 
 * Copyright (C) 2023 bmax121. All Rights Reserved.
 */

#ifndef _KP_ACCCTL_H_
#define _KP_ACCCTL_H_

#include <linux/sched.h>
#include <pgtable.h>
#include <taskext.h>
#include <asm/current.h>

/**
 * @brief Whether to make the current task bypass all selinux permission checks.
 * 
 * @param task 
 * @param val 
 */
static inline void set_priv_sel_allow(struct task_struct *task, bool val)
{
    struct task_ext *ext = kf_task_ext_ensure(task);
    if (!ext) return;
    ext->priv_sel_allow = val;
    dsb(ish);
}

#endif