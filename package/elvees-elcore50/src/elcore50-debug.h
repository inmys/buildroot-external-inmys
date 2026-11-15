/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2021 RnD Center "ELVEES", JSC
 */
#ifndef _LINUX_ELCORE50_DEBUG_H
#define _LINUX_ELCORE50_DEBUG_H

#include "elcore50-job-instance.h"

extern int mod_dbg_registers;

/*
 * Internal debug job instance data structure
 */
struct elcore50_job_inst_dbg_desc {
	struct elcore50_job_inst_desc *inst;
	struct file *inst_file;
};

enum elcore50_job_inst_dbg_rw {
	ELCORE50_JOB_INST_DBG_READ,
	ELCORE50_JOB_INST_DBG_WRITE
};

int elcore50_job_dbg_attach(struct elcore50_core *core, void __user *arg);
int export_dbg_fd(struct elcore50_job_inst_dbg_desc *inst_desc);
long elcore50_dbg_ioctl_safe(struct file *file, unsigned int cmd,
			     unsigned long arg);

#endif
