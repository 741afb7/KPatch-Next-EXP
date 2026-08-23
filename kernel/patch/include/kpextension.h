/* SPDX-License-Identifier: GPL-2.0-or-later */
/* 
 * Copyright (C) 2024 bmax121. All Rights Reserved.
 */

#ifndef _KP_KPEXTENSION_H_
#define _KP_KPEXTENSION_H_

#include <ktypes.h>

extern const char sh_path[];

int get_ap_mod_exclude(uid_t uid);
int set_ap_mod_exclude(uid_t uid, int exclude);

#endif
