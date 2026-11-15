// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2020 RnD Center "ELVEES", JSC
 */

#include <linux/anon_inodes.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#include "elcore50-syscall.h"
#include "elcore50-trace.h"
#include "elcore50-mmu.h"

// Profiler data
extern u32 timer_period_usec;

__packed struct timeval_compat {
	uint64_t sec;
	uint64_t usec;
};

__packed struct stat_compat {
	int16_t st_dev;
	uint16_t st_ino;
	uint32_t st_mode;
	uint16_t st_nlink;
	uint16_t st_uid;
	uint16_t st_gid;
	int16_t st_rdev;
	int32_t st_size;
	int32_t st_atime;
	int32_t st_spare1;
	int32_t st_mtime;
	int32_t st_spare2;
	int32_t st_ctime;
	int32_t st_spare3;
	int32_t st_blksize;
	int32_t st_blocks;

	int32_t st_spare4[2];
};

__packed struct tms_compat {
	uint64_t tms_utime;
	uint64_t tms_stime;
	uint64_t tms_cutime;
	uint64_t tms_cstime;
};

static int syscall_gettimeofday(struct timeval_compat *tv)
{
	struct timespec64 ts;

	ktime_get_real_ts64(&ts);
	tv->sec = ts.tv_sec;
	tv->usec = ts.tv_nsec / 1000;

	if (ts.tv_sec > U32_MAX)
		return -ERANGE;
	return 0;
}

static enum hrtimer_restart timer_callback(struct hrtimer *timer)
{
	struct elcore50_job_inst_desc *job_inst =
		container_of(timer, struct elcore50_job_inst_desc, timer);

	struct elcore50_core *core = job_inst->core;
	struct elcore50_profile_desc *desc = job_inst->profile_desc;
	uint32_t pc_val, bin_idx;

	// Job instance is in debug mode. Timer will be stopped.
	if ((job_inst->debug_request != DBG_REQUEST_NONE) || job_inst->attached)
		return HRTIMER_NORESTART;

	hrtimer_forward_now(timer, ns_to_ktime(timer_period_usec * 1000));

	if (desc->num_samples == 0)
		ktime_get_real_ts64(&desc->profiler_start_timestamp);

	pc_val = elcore50_read(core, DSP_PC);
	bin_idx = (((pc_val - desc->offset) / 2) * desc->scale) / 65536;
	desc->samples[bin_idx] += 1;
	desc->num_samples += 1;
	ktime_get_real_ts64(&desc->profiler_end_timestamp);

	return HRTIMER_RESTART;
}

static int syscall_profil(uint16_t *samples, size_t size, off_t offset,
			  int scale, struct userptr_mapper *mapper,
			  off_t mapper_offset,
			  struct elcore50_job_inst_desc *job_inst)
{
	struct elcore50_core *core = job_inst->core;
	unsigned long duration, last_timer_tick;
	u32 lost_samples = 0, num_samples_expected;
	struct timespec64 ts, diff;

	if (scale) {
		job_inst->profile_desc = kzalloc(
			sizeof(struct elcore50_profile_desc), GFP_KERNEL);

		job_inst->profile = 1;
		job_inst->profile_desc->samples = samples;
		job_inst->profile_desc->size = size;
		job_inst->profile_desc->offset = offset;
		job_inst->profile_desc->scale = scale;
		job_inst->profile_desc->mapper = mapper;
		job_inst->profile_desc->mapper_offset = mapper_offset;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 15, 0)
		hrtimer_setup(&job_inst->timer, &timer_callback,
			      CLOCK_MONOTONIC, HRTIMER_MODE_REL);
#else
		hrtimer_init(&job_inst->timer, CLOCK_MONOTONIC,
			     HRTIMER_MODE_REL);
		job_inst->timer.function = &timer_callback;
#endif
		hrtimer_start(&job_inst->timer,
			      ns_to_ktime(timer_period_usec * 1000),
			      HRTIMER_MODE_REL);
	} else {
		hrtimer_cancel(&job_inst->timer);
		ktime_get_real_ts64(&ts);

		diff = timespec64_sub(
			ts, job_inst->profile_desc->profiler_end_timestamp);
		last_timer_tick = timespec64_to_ns(&diff) / NSEC_PER_USEC;
		diff = timespec64_sub(
			job_inst->profile_desc->profiler_end_timestamp,
			job_inst->profile_desc->profiler_start_timestamp);
		duration = timespec64_to_ns(&diff) / NSEC_PER_USEC;

		if (job_inst->job_desc->xyram &&
		    (job_inst->profile_desc->mapper ==
		     job_inst->job_desc->xyram->mapper)) {
			mapper_offset = job_inst->profile_desc->mapper_offset;
			elcore50_buf_cpy(core->drv_priv,
					 core->xyram + mapper_offset,
					 job_inst->profile_desc->samples,
					 job_inst->profile_desc->size);
		} else {
			sync_buffer(job_inst->profile_desc->size, 0,
				    job_inst->profile_desc->mapper,
				    ELCORE50_BUF_SYNC_DIR_TO_DEVICE);
		}
		job_inst->profile = 0;

		// Time has stalled if the last tick was longer
		// than timer_period
		lost_samples = last_timer_tick / timer_period_usec;

		// Check the timer stability
		num_samples_expected = duration / timer_period_usec + 1;
		if (job_inst->profile_desc->num_samples < num_samples_expected)
			lost_samples += num_samples_expected -
					job_inst->profile_desc->num_samples;
		kfree(job_inst->profile_desc);
	}
	return lost_samples;
}

static const char *get_syscallname_by_id(int syscall_idx)
{
	switch (syscall_idx) {
	case SC_GETTIMEOFDAY:
		return "SC_GETTIMEOFDAY";
	case SC_READ:
		return "SC_READ";
	case SC_WRITE:
		return "SC_WRITE";
	case SC_OPEN:
		return "SC_OPEN";
	case SC_CLOSE:
		return "SC_CLOSE";
	case SC_FSTAT:
		return "SC_FSTAT";
	case SC_LSEEK:
		return "SC_LSEEK";
	case SC_ISATTY:
		return "SC_ISATTY";
	case SC_CHDIR:
		return "SC_CHDIR";
	case SC_STAT:
		return "SC_STAT";
	case SC_TIMES:
		return "SC_TIMES";
	case SC_LINK:
		return "SC_LINK";
	case SC_UNLINK:
		return "SC_UNLINK";
	case SC_PROFIL:
		return "SC_PROFIL";
	case SC_PMMAP:
		return "SC_PMMAP";
	case SC_PMUNMAP:
		return "SC_PMUNMAP";
	case SC_GET_ENV:
		return "SC_GET_ENV";
	case SC_GET_KERNELNAME:
		return "SC_GET_KERNELNAME";
	}
	return "SC_UNKNOWN";
}

/*
 * Map a DSP-side buffer pointed to by @mem->value into kernel space and fill
 * in @mem->vaddr / @mem->offset / @mem->cpu_value / @mem->mapper. Returns 0 on
 * success, -EINVAL if the address cannot be translated or -EACCES if it has no
 * backing CPU mapping.
 */
static inline int syscall_map_arg(struct elcore50_job_inst_desc *job_inst,
				  struct syscall_argument *mem)
{
	mem->vaddr = elcore50_map_from_users(job_inst, mem->value, &mem->mapper,
					     &mem->offset, &mem->cpu_value,
					     mem->size);
	if (!mem->vaddr)
		return -EINVAL;
	if (!mem->cpu_value)
		return -EACCES;
	return 0;
}

/* Flush the updated page tables and trigger a VMMU page-table-walk refresh. */
static inline void
syscall_mmu_sync_and_invalidate(struct elcore50_job_inst_desc *job_inst)
{
	struct elcore50_core *core = job_inst->core;
	u32 regval;

	elcore50_mmu_sync(job_inst);
	regval = elcore50_read(core, VMMU_PTW_CFG);
	elcore50_write(regval | VMMU_PTW_CFG_INV, core, VMMU_PTW_CFG);
}

int syscall_handler(struct elcore50_job_inst_desc *job_inst)
{
	struct elcore50_core *core = job_inst->core;
	u32 syscall_idx, size_tmp;
	unsigned long flags_state;
	struct syscall_argument syscall_args[MAX_ARGUMENTS], *mem;
	const char *syscall_name;
	int i, self_processed, ret;

	job_inst->message.type = ELCORE50_MESSAGE_EMPTY;
	spin_lock_irqsave(&job_inst->state_lock, flags_state);
	job_inst->state = ELCORE50_JOB_STATUS_SYSCALL;
	spin_unlock_irqrestore(&job_inst->state_lock, flags_state);

	for (i = 0; i < MAX_ARGUMENTS; i++) {
		syscall_args[i].value =
			elcore50_read(core, DSP_R0 + i * DSP_RF_NEXT);
		syscall_args[i].cpu_value = syscall_args[i].value;
		syscall_args[i].flush = 0;
	}

	syscall_idx = DSP_IRQ_INDEX_SCN(elcore50_read(core, DSP_IRQ_INDEX));
	syscall_name = get_syscallname_by_id(syscall_idx);
	switch (syscall_idx) {
	case SC_READ: // (int fd, void *buf, size_t cout)
	case SC_WRITE: // (int fd, void *buf, size_t cout)
		mem = &syscall_args[1];
		mem->size = syscall_args[2].value;
		if (syscall_idx == SC_READ)
			mem->flush = 1;
		trace_syscall(syscall_name, 0, mem->size);

		ret = syscall_map_arg(job_inst, mem);
		if (ret)
			return ret;
		self_processed = 0;
		break;
	case SC_GETTIMEOFDAY: // (struct timeval *tv, struct timezone *tz)
		mem = &syscall_args[0];
		mem->size = sizeof(struct timeval_compat);
		mem->flush = 1;
		trace_syscall(syscall_name, mem->size, 0);

		ret = syscall_map_arg(job_inst, mem);
		if (ret)
			return ret;

		job_inst->message.retval = syscall_gettimeofday(mem->vaddr);
		self_processed = 1;
		break;
	case SC_PROFIL: // (char *samples, size_t size, off_t offset, int scale)
		mem = &syscall_args[0];
		mem->size = syscall_args[1].value;
		trace_syscall(syscall_name, mem->size, 0);

		mem->vaddr = NULL;
		if (syscall_args[3].value) {
			ret = syscall_map_arg(job_inst, mem);
			if (ret)
				return ret;
		}

		job_inst->message.retval = syscall_profil(
			mem->vaddr, syscall_args[1].value,
			syscall_args[2].value, syscall_args[3].value,
			mem->mapper, mem->offset, job_inst);
		self_processed = 1;
		break;
	case SC_OPEN: // (char *filename, int flags, int mode)
		mem = &syscall_args[0];
		// The string's length is in R0.L1
		mem->size = elcore50_read(core, DSP_R0 + 4);
		trace_syscall(syscall_name, mem->size, 0);

		ret = syscall_map_arg(job_inst, mem);
		if (ret)
			return ret;
		WARN_ON(strlen(mem->vaddr) != mem->size);
		self_processed = 0;
		break;
	case SC_CLOSE: // (int file)
	case SC_ISATTY: // (int file)
	case SC_LSEEK: // (int file, int offset, int dir)
		self_processed = 0;
		trace_syscall(syscall_name, 0, 0);
		break;
	case SC_FSTAT: // (int file, struct stat *st)
		mem = &syscall_args[1];
		mem->size = sizeof(struct stat_compat);
		mem->flush = 1;
		trace_syscall(syscall_name, 0, mem->size);

		ret = syscall_map_arg(job_inst, mem);
		if (ret)
			return ret;

		self_processed = 0;
		break;
	case SC_STAT: // (const char *filename, struct stat *buf)
		mem = &syscall_args[0];
		// The string's length is in R0.L1
		mem->size = elcore50_read(core, DSP_R0 + 4);
		trace_syscall(syscall_name, mem->size,
			      sizeof(struct stat_compat));

		ret = syscall_map_arg(job_inst, mem);
		if (ret)
			return ret;
		WARN_ON(strlen(mem->vaddr) != mem->size);

		mem = &syscall_args[1];
		mem->size = sizeof(struct stat_compat);
		mem->flush = 1;
		ret = syscall_map_arg(job_inst, mem);
		if (ret)
			return ret;
		self_processed = 0;
		break;
	case SC_LINK: // (const char *oldpath, const char *newpath)
		mem = &syscall_args[0];
		// The string's lengths are in R0.L1 and R1.L1
		mem->size = elcore50_read(core, DSP_R0 + 4);
		size_tmp = elcore50_read(core, DSP_R1 + 4);
		trace_syscall(syscall_name, mem->size, size_tmp);

		ret = syscall_map_arg(job_inst, mem);
		if (ret)
			return ret;
		WARN_ON(strlen(mem->vaddr) != mem->size);

		mem = &syscall_args[1];
		mem->size = size_tmp;
		ret = syscall_map_arg(job_inst, mem);
		if (ret)
			return ret;
		WARN_ON(strlen(mem->vaddr) != mem->size);
		self_processed = 0;
		break;
	case SC_UNLINK: // (const char *path)
	case SC_CHDIR: // (const char *path)
		mem = &syscall_args[0];
		// The string's length is in R0.L1
		mem->size = elcore50_read(core, DSP_R0 + 4);
		trace_syscall(syscall_name, mem->size, 0);

		ret = syscall_map_arg(job_inst, mem);
		if (ret)
			return ret;
		WARN_ON(strlen(mem->vaddr) != mem->size);
		self_processed = 0;
		break;
	case SC_TIMES: // (struct times *buf)
		mem = &syscall_args[0];
		mem->size = sizeof(struct tms_compat);
		mem->flush = 1;
		trace_syscall(syscall_name, mem->size, 0);

		ret = syscall_map_arg(job_inst, mem);
		if (ret)
			return ret;
		self_processed = 0;
		break;
	case SC_PMMAP: // (uint32_t pref_vaddr, size_t length, uint64_t paddr)
		self_processed = 1;
		trace_syscall(syscall_name, 0, 0);
		// We need to get 64bit values instead of default 32bit
		syscall_args[2].value = elcore50_read64(core, DSP_R2);
		job_inst->message.retval = elcore50_syscall_pmmap(
			job_inst, syscall_args[0].value, syscall_args[1].value,
			syscall_args[2].value, false, NULL);
		syscall_mmu_sync_and_invalidate(job_inst);
		break;
	case SC_PMUNMAP: // (uint32_t vaddr, size_t length)
		self_processed = 1;
		trace_syscall(syscall_name, 0, 0);
		job_inst->message.retval = elcore50_syscall_pmunmap(
			job_inst, syscall_args[0].value, syscall_args[1].value);
		syscall_mmu_sync_and_invalidate(job_inst);
		break;
	case SC_GET_ENV: // (char *env, uint32_t *size)
		mem = &syscall_args[1];
		if (mem->value == 0)
			return -EINVAL;
		mem->size = sizeof(uint32_t);
		mem->flush = 1;
		trace_syscall(syscall_name, mem->size, 0);

		ret = syscall_map_arg(job_inst, mem);
		if (ret)
			return ret;

		size_tmp = *((u32 *)mem->vaddr);
		mem = &syscall_args[0];
		if (mem->value) {
			mem->size = size_tmp;
			mem->flush = 1;

			ret = syscall_map_arg(job_inst, mem);
			if (ret)
				return ret;
		}
		self_processed = 0;
		break;
	case SC_GET_KERNELNAME: // (char *name)
		mem = &syscall_args[0];
		mem->size = strlen(job_inst->name) + 1;
		mem->flush = 1;
		trace_syscall(syscall_name, mem->size, 0);

		ret = syscall_map_arg(job_inst, mem);
		if (ret)
			return ret;

		strscpy(mem->vaddr, job_inst->name, mem->size);
		job_inst->message.retval = 0;
		self_processed = 1;
		break;
	default:
		return -EINVAL;
	}

	if (!self_processed) {
		job_inst->message.arg0 = syscall_args[0].cpu_value;
		job_inst->message.arg1 = syscall_args[1].cpu_value;
		job_inst->message.arg2 = syscall_args[2].cpu_value;
		job_inst->message.num = syscall_idx;
		job_inst->message.type = ELCORE50_MESSAGE_SYSCALL;
		job_inst->syscall_handled = 0;

		wake_up(&job_inst->poll_waitq);
		if (irq_timeout_msec)
			ret = !wait_event_timeout(
				job_inst->syscall_waitq,
				job_inst->syscall_handled || job_inst->abort,
				msecs_to_jiffies(irq_timeout_msec));
		else {
			wait_event(job_inst->syscall_waitq,
				   job_inst->syscall_handled ||
					   job_inst->abort);
			ret = 0;
		}

		job_inst->syscall_handled = 0;
		job_inst->message.type = ELCORE50_MESSAGE_EMPTY;

		if (ret || job_inst->abort)
			return -ETIME;
	}

	for (i = 0; i < MAX_ARGUMENTS; i++) {
		mem = &syscall_args[i];
		if (!mem->flush)
			continue;
		if (job_inst->job_desc->xyram &&
		    (mem->mapper == job_inst->job_desc->xyram->mapper)) {
			elcore50_buf_cpy(core->drv_priv,
					 core->xyram + mem->offset, mem->vaddr,
					 mem->size);
		} else {
			ret = sync_buffer(mem->size, mem->offset, mem->mapper,
					  ELCORE50_BUF_SYNC_DIR_TO_DEVICE);
			if (ret)
				return ret;
		}
	}

	elcore50_write(elcore50_read(core, DSP_IRQR) & (~IRQR_SC), core,
		       DSP_IRQR);
	elcore50_write(elcore50_read(core, DSP_DQSTR) & (~DQSTR_SC), core,
		       DSP_DQSTR);
	elcore50_write(job_inst->message.retval, core, DSP_R0);
	return 0;
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ELcore-50 syscall implementations");
