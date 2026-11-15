/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2026 RnD Center "ELVEES", JSC
 */

#ifndef __LINUX_ELCORE50_COREDUMP_H__
#define __LINUX_ELCORE50_COREDUMP_H__

#ifdef ELCORE50_DUMP
#include "elcore50-job-instance.h"
#include "elcore50-regs.h"

void elcore50_dump_registers(struct elcore50_core *core,
			     struct registers *regs);
int elcore50_coredump_reg(struct elcore50_core *core, void *arg);
int elcore50_coredump_info(struct elcore50_job_inst_desc *desc, void *arg);
int elcore50_coredump_argc(struct elcore50_job_inst_desc *desc, void *arg);
int elcore50_coredump_arg_info(struct elcore50_job_inst_desc *desc, void *arg);
int elcore50_coredump_arg_value(struct elcore50_job_inst_desc *desc, void *arg);
int elcore50_coredump_internal(struct elcore50_job_inst_desc *desc, u32 cmd,
			       void *arg);
int elcore50_coredump_done(struct elcore50_job_inst_desc *desc);
#endif

#endif // __LINUX_ELCORE50_COREDUMP_H__
