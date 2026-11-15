/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2020 RnD Center "ELVEES", JSC
 */

#ifndef _LINUX_ELCORE50_SYSCALL_H
#define _LINUX_ELCORE50_SYSCALL_H

#include "elcore50-job-instance.h"

#define MAX_ARGUMENTS 4

struct syscall_argument {
	struct userptr_mapper *mapper;
	u64 value;
	u64 cpu_value;
	void *vaddr;
	size_t size;
	size_t offset;
	int flush;
};

int syscall_handler(struct elcore50_job_inst_desc *job_inst);

#endif
