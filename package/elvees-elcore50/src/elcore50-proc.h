/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2024 RnD Center "ELVEES", JSC
 */
#ifndef _LINUX_ELCORE50_PROC_H
#define _LINUX_ELCORE50_PROC_H

extern struct proc_dir_entry *proc_pdentry;

int elcore50_proc_init(void);

#endif
