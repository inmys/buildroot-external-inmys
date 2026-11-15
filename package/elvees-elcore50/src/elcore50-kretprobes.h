/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2025 RnD Center "ELVEES", JSC
 */

#ifndef __LINUX_ELCORE50_KRETPROBES_H__
#define __LINUX_ELCORE50_KRETPROBES_H__

#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/version.h>
#include <linux/units.h>
#include <linux/workqueue.h>

#include "elcore50-job-instance.h"

#define NUM_ATTRS 5

static const char symbol_names[][64] = { "elcore50_add_prefetch_pages",
					 "elcore50_bus_ioctl",
					 "elcore50_bus_open",
					 "elcore50_bus_probe",
					 "elcore50_bus_release",
					 "elcore50_bus_remove",
					 "elcore50_buf_cpy",
					 "elcore50_cancel_job_inst",
					 "elcore50_clock_destroy",
					 "elcore50_clock_disable",
					 "elcore50_clock_enable",
					 "elcore50_clock_init",
					 "elcore50_core_abort",
					 "elcore50_core_reset",
					 "elcore50_cores_destroy",
					 "elcore50_cores_init",
					 "elcore50_create_buffer",
					 "elcore50_create_job",
					 "elcore50_create_job_inst",
					 "elcore50_create_mapper",
					 "elcore50_disable_irqs",
					 "elcore50_dmabuf_mmap",
					 "elcore50_dmabuf_release",
					 "elcore50_dmabuf_vmap",
					 "elcore50_dmabuf_vunmap",
					 "elcore50_enable_irqs",
					 "elcore50_enqueue_job_inst",
					 "elcore50_enqueue_shared_job_inst",
					 "elcore50_free_buffer",
					 "elcore50_freq_target",
					 "elcore50_get_core_idx",
					 "elcore50_get_cur_freq",
					 "elcore50_get_dev_status",
					 "elcore50_get_job_inst_count",
					 "elcore50_get_job_inst_list",
					 "elcore50_get_job_inst_status",
					 "elcore50_get_platform_info",
					 "elcore50_get_shared_job_inst_count",
					 "elcore50_get_shared_job_inst_list",
					 "elcore50_ioctl",
					 "elcore50_irq",
					 "elcore50_job_inst_ioctl",
					 "elcore50_job_inst_poll",
					 "elcore50_job_inst_read",
					 "elcore50_job_inst_release",
					 "elcore50_job_inst_run",
					 "elcore50_job_inst_write",
					 "elcore50_job_release",
					 "elcore50_list_count",
					 "elcore50_map_dmabuf",
					 "elcore50_map_from_users",
					 "elcore50_mapper_release",
					 "elcore50_mmu_alloc_fixed",
					 "elcore50_mmu_fill",
					 "elcore50_mmu_fill_args",
					 "elcore50_mmu_free",
					 "elcore50_mmu_free_args",
					 "elcore50_mmu_free_fixed",
					 "elcore50_mmu_sync",
					 "elcore50_open",
					 "elcore50_probe",
					 "elcore50_read",
					 "elcore50_read64",
					 "elcore50_release",
					 "elcore50_release_physmapper",
					 "elcore50_remove",
					 "elcore50_reset",
					 "elcore50_reset_init",
					 "elcore50_runtime_resume",
					 "elcore50_runtime_suspend",
					 "elcore50_stop_vdma",
					 "elcore50_sync_buffer",
					 "elcore50_syscall_pmmap",
					 "elcore50_syscall_pmunmap",
					 "elcore50_unmap_dmabuf",
					 "elcore50_vaddr_is_internal",
					 "elcore50_vaddr_to_kernel",
					 "elcore50_vaddr_to_mapper",
					 "elcore50_vaddr_to_mapper_internal",
					 "elcore50_vdma_active",
					 "elcore50_write",
					 "event_handler",
					 "qlic_handle_irq",
					 "write_pte_entry" };

static const char attrs_names[NUM_ATTRS][11] = { "total_time", "min_time",
						 "max_time", "avg_time",
						 "call_times" };

struct attrs_data {
	u64 total_time;
	u64 min_time;
	u64 max_time;
	u64 avg_time;
	u64 call_times;
};

struct func_data {
	struct attrs_data attrs;
	rwlock_t lock;
};

struct job {
	struct attrs_data attrs;
	struct dentry *dentry;
	u64 exec_time;
	char jobname[255];

	rwlock_t lock;
	struct list_head node;
	struct work_struct debugfs_work;
};

int elcore50_kprobe_init(struct dentry *debugfs_pdentry,
			 u32 kretprobes_max_active);
void elcore50_kprobe_exit(void);

#endif // __LINUX_ELCORE50_KRETPROBES_H__
