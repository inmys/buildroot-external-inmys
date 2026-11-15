/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2022 RnD Center "ELVEES", JSC */

#pragma once

#include <linux/ioctl.h>

#define DMABUF_IOCTL_CREATE _IOR('G', 64, long)
#define DMABUF_IOCTL_EXPORT _IOR('G', 65, long)
