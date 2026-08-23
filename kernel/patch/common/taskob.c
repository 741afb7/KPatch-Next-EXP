/* SPDX-License-Identifier: GPL-2.0-or-later */
/* 
 * Copyright (C) 2023 bmax121. All Rights Reserved.
 */

#include <taskob.h>
#include <taskext.h>
#include <kallsyms.h>
#include <hook.h>
#include <asm/current.h>
#include <linux/sched/task.h>
#include <linux/pid.h>
#include <linux/security.h>
#include <log.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <pgtable.h>
#include <linux/fs.h>
#include <linux/seccomp.h>
#include <uapi/asm-generic/errno.h>
#include <predata.h>
#include <symbol.h>
#include <baselib.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>

/*
 * task_ext per-task slot table.
 *
 * The ext used to live at the top/bottom of the task's kernel stack
 * (get_task_ext = end_of_stack + 1), but on affected kernels writing the ext
 * into task->stack corrupts a later task's ->stack. Store it here instead,
 * keyed by the task pointer.
 *
 * A slot is NOT allocated for every fork: only tasks whose parent carries an
 * active task-local SELinux bypass state get one, plus tasks that explicitly
 * request task-local state (see kf_task_ext_ensure). Tasks without such state
 * return the shared task_ext_invalid, which the security checks treat as
 * "no bypass". This keeps the table nearly empty regardless of thread count.
 * Slots are freed on task exit via the do_exit hook (best-effort); 1024 slots
 * provide ample capacity even if that hook fails to install.
 */
#define TASK_EXT_SLOT_NUM 1024

struct task_ext_slot {
    struct task_struct *task;
    struct task_ext ext;
};

static struct task_ext_slot task_ext_slots[TASK_EXT_SLOT_NUM];
static struct task_ext task_ext_invalid;
static spinlock_t task_ext_lock;
/* count of occupied slots, so every fork/exit/SELinux check (called for every
 * task system-wide) can skip the 1024-entry scan when no task-local state is
 * active, which is the common case */
static atomic_t task_ext_active_count = ATOMIC_INIT(0);

static inline void task_ext_reset(struct task_ext *ext)
{
    /* sizeof(struct task_ext) is not necessarily a multiple of uintptr_t.
     * The old 8-byte loop wrote past the object when its size was 20 bytes. */
    lib_memset(ext, 0, sizeof(*ext));
    ext->size = task_ext_size;
    ext->_magic = TASK_EXT_MAGIC;
}

/*
 * Linear scan, no open-addressing probing: task_ext_free leaves holes, and a
 * probe-with-break lookup stops at a hole and can miss later entries. Scanning
 * the whole table is O(n) but always correct. create/free take the spinlock;
 * the lookup is a lock-free read of individual slots.
 */
struct task_ext *kf_get_task_ext(const struct task_struct *task)
{
    if (unlikely(!task)) return &task_ext_invalid;
    if (likely(!atomic_read(&task_ext_active_count))) return &task_ext_invalid;

    for (int i = 0; i < TASK_EXT_SLOT_NUM; i++) {
        if (task_ext_slots[i].task == task) return &task_ext_slots[i].ext;
    }
    return &task_ext_invalid;
}

static struct task_ext *task_ext_create(struct task_struct *task)
{
    struct task_ext *ret = NULL;

    spin_lock(&task_ext_lock);
    for (int i = 0; i < TASK_EXT_SLOT_NUM; i++) {
        if (task_ext_slots[i].task == task) {
            ret = &task_ext_slots[i].ext;
            break;
        }
    }
    if (!ret) {
        for (int i = 0; i < TASK_EXT_SLOT_NUM; i++) {
            if (!task_ext_slots[i].task) {
                task_ext_slots[i].task = task;
                ret = &task_ext_slots[i].ext;
                atomic_inc(&task_ext_active_count);
                break;
            }
        }
    }
    spin_unlock(&task_ext_lock);
    return ret;
}

static void task_ext_free(struct task_struct *task)
{
    if (likely(!atomic_read(&task_ext_active_count))) return;

    spin_lock(&task_ext_lock);
    for (int i = 0; i < TASK_EXT_SLOT_NUM; i++) {
        if (task_ext_slots[i].task == task) {
            task_ext_slots[i].task = NULL;
            atomic_dec(&task_ext_active_count);
            break;
        }
    }
    spin_unlock(&task_ext_lock);
}

/*
 * Lazily give `task` a valid ext. Tasks without inherited task-local state do
 * not receive a slot until they explicitly request one. Returns NULL only if
 * the slot table is full.
 */
struct task_ext *kf_task_ext_ensure(struct task_struct *task)
{
    if (unlikely(!task)) return &task_ext_invalid;

    struct task_ext *ext = kf_get_task_ext(task);
    if (task_ext_valid(ext)) return ext;

    ext = task_ext_create(task);
    if (!ext) return NULL;
    task_ext_reset(ext);
    ext->pid = __task_pid_nr_ns(task, PIDTYPE_PID, 0);
    ext->tgid = __task_pid_nr_ns(task, PIDTYPE_TGID, 0);
    dsb(ish);
    return ext;
}

static inline void prepare_init_ext(struct task_struct *task)
{
    struct task_ext *ext = task_ext_create(task);
    if (!ext) {
        logkfe("task_ext_create(init) FAILED\n");
        return;
    }
    task_ext_reset(ext);
    dsb(ish);
}

static void prepare_task_ext(struct task_struct *new, struct task_struct *old)
{
    struct task_ext *old_ext = get_task_ext(old);
    /*
     * Slots are only needed when a task carries an active task-local SELinux
     * bypass state: a task without one holds no ext (invalid = shared
     * task_ext_invalid), which is normal, not an error. When such a task forks,
     * the child receives a slot so the state is inherited. This keeps the table
     * from filling up with one entry per thread.
     */
    if (unlikely(!task_ext_valid(old_ext))) return;
    if (!old_ext->priv_sel_allow) return;

    struct task_ext *new_ext = task_ext_create(new);
    if (!new_ext) {
        logkfe("task_ext slot table full, skip\n");
        return;
    }
    task_ext_reset(new_ext);

    new_ext->pid = __task_pid_nr_ns(new, PIDTYPE_PID, 0);
    new_ext->tgid = __task_pid_nr_ns(new, PIDTYPE_TGID, 0);
    new_ext->priv_sel_allow = old_ext->priv_sel_allow;

    dsb(ish);
}

int task_ext_size = offsetof(struct task_ext, _magic);
KP_EXPORT_SYMBOL(task_ext_size);

static void after_copy_process(hook_fargs8_t *args, void *udata)
{
    struct task_struct *new = (struct task_struct *)args->ret;
    if (unlikely(!new || IS_ERR(new))) return;
    prepare_task_ext(new, current);
}

static void after_cgroup_post_fork(hook_fargs4_t *args, void *udata)
{
    struct task_struct *new = (struct task_struct *)args->arg0;
    prepare_task_ext(new, current);
}

static void before_do_exit(hook_fargs1_t *args, void *udata)
{
    task_ext_free(current);
}

int task_observer()
{
    int rc = 0;

    spin_lock_init(&task_ext_lock);
    prepare_init_ext(init_task);

    unsigned long copy_process_addr = patch_config->copy_process;
    if (copy_process_addr) {
        rc |= hook_wrap8((void *)copy_process_addr, 0, after_copy_process, 0);
        log_boot("hook copy_process: %llx, rc: %d\n", copy_process_addr, rc);
    } else {
        unsigned long cgroup_post_fork_addr = patch_config->cgroup_post_fork;
        if (cgroup_post_fork_addr) {
            rc |= hook_wrap4((void *)cgroup_post_fork_addr, 0, after_cgroup_post_fork, 0);
            log_boot("hook cgroup_post_fork: %llx, rc: %d\n", cgroup_post_fork_addr, rc);
        } else {
            rc = HOOK_BAD_ADDRESS;
        }
    }

    /* reclaim task_ext slot when a task exits, so the table doesn't fill up */
    unsigned long do_exit_addr = kallsyms_lookup_name("do_exit");
    if (do_exit_addr) {
        rc |= hook_wrap1((void *)do_exit_addr, before_do_exit, 0, 0);
        log_boot("hook do_exit: %llx, rc: %d\n", do_exit_addr, rc);
    } else {
        log_boot("hook do_exit: addr=0 NOT FOUND\n");
    }

    return rc;
}