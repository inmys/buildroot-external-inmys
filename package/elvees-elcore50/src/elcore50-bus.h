/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2022 RnD Center "ELVEES", JSC
 */
#ifndef _LINUX_ELCORE50_BUS_H
#define _LINUX_ELCORE50_BUS_H

#include <linux/cdev.h>
#include <linux/platform_device.h>

struct elcore50_bus_priv {
	struct platform_device *pdev;
	struct device *dev;
	dev_t dev_num;
	struct cdev cdev;
};

#endif // _LINUX_ELCORE50_BUS_H
