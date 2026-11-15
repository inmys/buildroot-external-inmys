/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2022 RnD Center "ELVEES", JSC
 */
#ifndef _LINUX_ELCORE50_PM_H
#define _LINUX_ELCORE50_PM_H

#include <linux/pm_runtime.h>

#include "elcore50-core.h"

extern struct mutex elcore50_devfreq_lock;

int elcore50_clock_init(struct cluster_priv *drv_priv);
void elcore50_clock_destroy(struct cluster_priv *drv_priv);
void elcore50_clock_disable(struct cluster_priv *drv_priv);

#endif
