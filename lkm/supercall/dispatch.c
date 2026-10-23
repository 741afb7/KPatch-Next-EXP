// SPDX-License-Identifier: GPL-2.0-or-later
/* Root-only supercall dispatch. SU and manager commands are intentionally absent. */
#include "dispatch.h"

#include <linux/errno.h>
#include <linux/types.h>
#include <scdefs.h>

#include "../include/kp_lkm.h"
#include "../kpm/kpm.h"

long kp_handle_supercall(long cmd, long a1, long a2, long a3, long a4)
{
	/* Supercall arguments can contain user pointers or other sensitive data. */
	logkd("supercall cmd 0x%lx\n", cmd);

	switch (cmd) {
	case SUPERCALL_HELLO:
		logki(SUPERCALL_HELLO_ECHO "\n");
		return SUPERCALL_HELLO_MAGIC;
	case SUPERCALL_KERNELPATCH_VER:
		return kpver;
	case SUPERCALL_KERNEL_VER:
		return kp_kver();
	case SUPERCALL_KPM_LOAD:
		return kp_kpm_load_sc((const char __user *)a1, (const char __user *)a2,
				      (void __user *)a3);
	case SUPERCALL_KPM_UNLOAD:
		return kp_kpm_unload_sc((const char __user *)a1, (void __user *)a2);
	case SUPERCALL_KPM_CONTROL:
		return kp_kpm_control_sc((const char __user *)a1, (const char __user *)a2,
					 (char __user *)a3, (int)a4);
	case SUPERCALL_KPM_NUMS:
		return kp_kpm_nums_sc();
	case SUPERCALL_KPM_LIST:
		return kp_kpm_list_sc((char __user *)a1, (int)a2);
	case SUPERCALL_KPM_INFO:
		return kp_kpm_info_sc((const char __user *)a1, (char __user *)a2, (int)a3);
	default:
		logkd("unsupported supercall cmd 0x%lx\n", cmd);
		return -ENOSYS;
	}
}
