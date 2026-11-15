/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2021 RnD Center "ELVEES", JSC
 */
#ifndef _LINUX_ELCORE50_JOB_INSTANCE_H
#define _LINUX_ELCORE50_JOB_INSTANCE_H

#include <linux/hrtimer.h>

#include "elcore50-mapper.h"
#include "elcore50-job.h"

#define E50_CACHE_TIMEOUT_USEC 10000

extern int mod_caches;
extern u32 irq_timeout_msec;
extern const struct file_operations elcore50_mapper_ops;
extern const struct file_operations elcore50_job_fops;
extern const struct dma_buf_ops elcore50_dmabuf_ops;

extern struct elcore50_scheduler_data scheduler_data;

enum elcore50_l2_size {
	L2_CACHE_NONE = 0,
	L2_CACHE_128 = 1,
	L2_CACHE_256 = 2,
	L2_CACHE_512 = 3
};

enum elcore50_debug_state {
	ELCORE50_DBG_RUN,
	ELCORE50_DBG_INTERRUPTED,
	ELCORE50_DBG_EXITED,
	ELCORE50_DBG_LAST,
};

enum elcore50_debug_request {
	DBG_REQUEST_NONE,
	DBG_REQUEST_IOCTL,
	DBG_REQUEST_ATTACH,
	DBG_REQUEST_DETACH,
	DBG_READY_TO_PROCESS,
	DBG_PROCESSED,
};

enum elcore50_job_inst_position {
	NO_POSITION,
	SHARED_ENQUEUE_QUEUE,
	PRIVATE_ENQUEUE_QUEUE,
	RUNNINT_LIST,
	LAST_RESERVED, // Should also be last
};

struct elcore50_profile_desc {
	uint16_t *samples;
	size_t size;
	off_t offset;
	int scale;
	u32 num_samples;
	struct timespec64 profiler_start_timestamp;
	struct timespec64 profiler_end_timestamp;
	struct userptr_mapper *mapper;
	off_t mapper_offset;
};

struct phys_mapper {
	uint64_t vaddr;
	dma_addr_t paddr;
	struct userptr_mapper mapper_stub;
	size_t size;
	struct list_head queue_node;
	uint32_t prefetch_owner_vaddr;
	size_t prefetch_owner_size;
};

#ifdef ELCORE50_DUMP
struct elcore50_job_arg_saved {
	struct elcore50_job_arg raw;
	char basic_value[8];
};
#endif

/*
 * Internal job instance data structure
 */
struct elcore50_job_inst_desc {
	struct elcore50_core *core;
	struct elcore50_buf_desc **args;
	int argc;
	uint32_t *local_args_addr;
	enum elcore50_job_instance_state state;
	enum elcore50_job_instance_error error;
#ifdef ELCORE50_DUMP
	struct elcore50_job_arg_saved *raw_args;
	int raw_argc;
	bool coredump_done;
#endif
	uint32_t launcher_vaddr;
	uint32_t entry_point_vaddr;
	uint32_t laucnher_stop_vaddr;
	uint32_t noncached_regions;
	struct list_head queue_node;
	wait_queue_head_t poll_waitq;
	wait_queue_head_t poll_dbg_waitq;
	wait_queue_head_t irq_waitq;
	wait_queue_head_t syscall_waitq;
	wait_queue_head_t debug_waitq;
	wait_queue_head_t finish_waitq;
#ifdef ELCORE50_DUMP
	wait_queue_head_t coredump_waitq;
#endif
	struct work_struct worker;
	enum elcore50_l2_size l2_size;
	uint32_t nc_mem_current;
	struct mmu_pool *dsp_pool;
	struct mmu_pool *dma_pool;
#ifndef ELCORE50_NO_IRQS
	int core_stopped_by_irq;
	int irq_processed;
#endif
	int abort;
	void *stack_args;
	size_t stack_args_size;
	uint64_t arg_regs[E50_ARG_REGS];
	spinlock_t state_lock;
	struct mutex debug_lock;

	struct file **arg_files;

	struct elcore50_message message;
	int syscall_handled;

	int pid;
	char name[255];

	struct file *self_file;

	struct list_head pmapper_list;
	struct list_head pmapper_reserved_list;

	int attached;
	int scheduled;

	int removable;

	enum elcore50_debug_state debug_state;
	enum elcore50_stop_reason stop_reason;
	enum elcore50_debug_request debug_request;
	int debug_result;
	int step_breakpoint;
	u32 dbsar_value;
	u32 dbsar_addr;
	u32 dbcnt_rest_plus_1;

	int profile;
	struct elcore50_profile_desc *profile_desc;
	struct hrtimer timer;

	struct file *job_file;
	struct elcore50_job_desc *job_desc;

	enum elcore50_job_inst_position start_position;
	enum elcore50_job_inst_position position;
	spinlock_t position_lock;

	u32 dqstr;
	int vdma_active;
};

int elcore50_cancel_job_inst(struct elcore50_job_inst_desc *instance);
int elcore50_enqueue_job_inst(struct elcore50_core *core, void __user *arg);
int elcore50_enqueue_shared_job_inst(struct device *dev, void __user *arg);
int elcore50_get_job_inst_count(struct elcore50_core *core, void __user *arg);
int elcore50_get_shared_job_inst_count(void __user *arg);
int elcore50_get_job_inst_list(struct elcore50_core *core, void __user *arg);
int elcore50_get_shared_job_inst_list(void __user *arg);
long elcore50_get_job_inst_status(void __user *arg);
int elcore50_job_inst_count(struct elcore50_core *core);
int kthread_handler(void *priv);
void elcore50_job_inst_run(struct elcore50_job_inst_desc *desc);

#endif
