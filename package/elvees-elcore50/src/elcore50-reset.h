/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2020 RnD Center "ELVEES", JSC
 */
#ifndef _LINUX_ELCORE50_RESET_H
#define _LINUX_ELCORE50_RESET_H

#include "elcore50-core.h"

int elcore50_reset_init(struct cluster_priv *drv_priv);
int elcore50_reset(struct cluster_priv *drv_priv, bool off);

#endif
