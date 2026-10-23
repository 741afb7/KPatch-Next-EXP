// SPDX-License-Identifier: GPL-2.0-or-later
/* syscall 45 (truncate) is used as the root-only KP supercall channel. */
#include "supercall.h"
#include "dispatch.h"

#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/syscalls.h>
#include <linux/user_namespace.h>
#include <scdefs.h>

#include "../include/kp_lkm.h"
#include "../infra/syscall_table.h"

#define KP_SUPERCALL_NR __NR_supercall

static kp_syscall_fn_t kp_orig_truncate;

static long kp_supercall_handler(const struct pt_regs *regs)
{
	uid_t uid = from_kuid(current_user_ns(), current_uid());
	long ver_xx_cmd;
	long cmd;

	/* KPatch-Next has no manager allowlist in the LKM path. */
	if (uid != 0) {
		if (kp_orig_truncate)
			return kp_orig_truncate(regs);
		return -ENOSYS;
	}

	ver_xx_cmd = regs->regs[1];
	cmd = ver_xx_cmd & 0xffff;
	if (cmd < SUPERCALL_HELLO || cmd > SUPERCALL_MAX)
		return kp_orig_truncate ? kp_orig_truncate(regs) : -EINVAL;

	return kp_handle_supercall(cmd, regs->regs[2], regs->regs[3], regs->regs[4],
				   regs->regs[5]);
}

int kp_supercall_install(void)
{
	int rc = kp_syscall_hook(KP_SUPERCALL_NR, kp_supercall_handler, &kp_orig_truncate);

	if (rc)
		return rc;
	logki("root-only supercall installed on syscall %d\n", KP_SUPERCALL_NR);
	return 0;
}

void kp_supercall_uninstall(void)
{
	kp_syscall_unhook(KP_SUPERCALL_NR, kp_orig_truncate);
	kp_orig_truncate = NULL;
}
