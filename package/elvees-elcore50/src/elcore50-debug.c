// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2021 RnD Center "ELVEES", JSC
 */

#include <linux/anon_inodes.h>
#include <linux/fdtable.h>
#include <linux/hash.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "elcore50-debug.h"
#include "elcore50-mmu.h"

static int internal_memory_rw(struct elcore50_job_inst_dbg_desc *desc,
			      struct elcore50_dbg_mem *mem,
			      enum elcore50_job_inst_dbg_rw rw, int is_pram)
{
	int ret;
	struct elcore50_job_inst_desc *job_inst = desc->inst;
	struct elcore50_core *core = job_inst->core;
	void *data;
	off_t offset;
	void __iomem *internal_mem;

	data = kmalloc(mem->size, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (is_pram) {
		internal_mem = core->pram;
		offset = mem->vaddr - PHYS_INTERNAL_PRAM_DSP;
	} else {
		internal_mem = core->xyram;
		offset = mem->vaddr - PHYS_INTERNAL_DSP;
	}

	if (rw == ELCORE50_JOB_INST_DBG_READ) {
		memcpy_fromio(data, internal_mem + offset, mem->size);
		ret = copy_to_user(mem->data, data, mem->size);
		if (ret)
			return -EACCES;
	} else {
		ret = copy_from_user(data, mem->data, mem->size);
		if (ret)
			return -EACCES;
		elcore50_buf_cpy(core->drv_priv, internal_mem + offset, data,
				 mem->size);
	}

	kfree(data);
	return 0;
}

static int external_memory_rw(struct elcore50_job_inst_dbg_desc *desc,
			      struct elcore50_dbg_mem *mem,
			      enum elcore50_job_inst_dbg_rw rw)
{
	int ret;
	uint32_t reg_tmp;
	struct elcore50_job_inst_desc *job_inst = desc->inst;
	struct elcore50_core *core = job_inst->core;
	struct userptr_mapper *mapper = NULL;
	void *data;
	u64 user_ptr;
	size_t offset;

	data = elcore50_map_from_users(job_inst, mem->vaddr, &mapper, &offset,
				       &user_ptr, mem->size);
	if (!data)
		return -EACCES;

	if (rw == ELCORE50_JOB_INST_DBG_READ) {
		ret = copy_to_user(mem->data, data, mem->size);
		if (ret)
			return -EACCES;
	} else {
		ret = copy_from_user(data, mem->data, mem->size);
		if (ret)
			return -EACCES;

		sync_buffer(mem->size, offset, mapper,
			    ELCORE50_BUF_SYNC_DIR_TO_DEVICE);

		// Invalidate all DSP caches
		elcore50_write(DSP_INCTRL_INVAL_ALL, core, DSP_INVCTRL);
		ret = elcore50_pollreg_timeout(core, DSP_MBARREG, reg_tmp,
					       reg_tmp == 0, 0,
					       E50_CACHE_TIMEOUT_USEC);
		WARN_ON(ret);
	}
	return 0;
}

static int elcore50_dbg_memory_rw(struct elcore50_job_inst_dbg_desc *desc,
				  void __user *arg,
				  enum elcore50_job_inst_dbg_rw rw)
{
	int ret = 0;
	uint32_t reg_tmp;
	unsigned long start, end;
	uint32_t pfn, old_pfn;
	struct elcore50_dbg_mem mem;
	struct elcore50_job_inst_desc *job_inst = desc->inst;
	struct elcore50_core *core = job_inst->core;

	ret = copy_from_user((void *)&mem, (const void __user *)arg,
			     sizeof(struct elcore50_dbg_mem));
	if (ret)
		return -EACCES;

	old_pfn = elcore50_read(core, DSP_CTRL);
	// Disable prefetcher
	pfn = old_pfn & ~(3 << 16);
	elcore50_write(pfn, core, DSP_CTRL);

	// Flush all DSP caches
	elcore50_write(DSP_INVCTRL_FLUSH_ALL, core, DSP_INVCTRL);
	ret = elcore50_pollreg_timeout(core, DSP_MBARREG, reg_tmp, reg_tmp == 0,
				       0, E50_CACHE_TIMEOUT_USEC);
	WARN_ON(ret);

	// Reset pipeline
	elcore50_write(pfn | CTRL_PipelineFlush, core, DSP_CTRL);

	start = mem.vaddr;
	end = mem.vaddr + mem.size;
	if ((start >= PHYS_INTERNAL_DSP) &&
	    (end <= (PHYS_INTERNAL_INTERLEAVE_DSP + INTERNAL_DSP_SIZE))) {
		// XYRAM
		ret = internal_memory_rw(desc, &mem, rw, 0);
	} else if ((start >= PHYS_INTERNAL_PRAM_DSP) &&
		   (end <= (PHYS_INTERNAL_PRAM_DSP + INTERNAL_DSP_PRAM_SIZE))) {
		// PRAM
		ret = internal_memory_rw(desc, &mem, rw, 1);
	} else if ((start >= PHYS_INTERNAL_REGS_DSP) &&
		   (end <= (PHYS_INTERNAL_REGS_DSP + INTERNAL_DSP_REGS_SIZE))) {
		// Local Regs
		WARN_ON(1);
		ret = -EINVAL;
	} else {
		// DDR
		ret = external_memory_rw(desc, &mem, rw);
	}
	// Restore pfn
	elcore50_write(old_pfn, core, DSP_CTRL);

	return ret;
}

static int elcore50_dbg_register_rw(struct elcore50_job_inst_dbg_desc *desc,
				    void __user *arg,
				    enum elcore50_job_inst_dbg_rw rw)
{
	int ret = 0, reg, i = 0;
	struct elcore50_dbg_mem mem;
	struct elcore50_job_inst_desc *job_inst = desc->inst;
	struct elcore50_core *core = job_inst->core;
	int is_regfile, is_vecfile, is_pc;
	uint32_t *data;

	ret = copy_from_user(&mem, arg, sizeof(struct elcore50_dbg_mem));
	if (ret)
		return -EACCES;

	if (mod_dbg_registers) {
		reg = mem.vaddr;
		is_regfile = (reg >= DSP_R0) && (reg + mem.size < DSP_RF_LAST);
		is_vecfile = (reg >= DSP_VF0) && (reg + mem.size < DSP_VF_LAST);
		is_pc = (reg == DSP_PC) && (mem.size == 4);

		if ((is_regfile == 0) && (is_vecfile == 0) && (is_pc == 0))
			return -EACCES;
	}

	data = kmalloc(mem.size, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (rw == ELCORE50_JOB_INST_DBG_READ) {
		for (reg = mem.vaddr; reg < mem.vaddr + mem.size; reg += 4)
			data[i++] = elcore50_read(core, reg);
		ret = copy_to_user(mem.data, data, mem.size);
		if (ret) {
			ret = -EACCES;
			goto err;
		}
	} else if (rw == ELCORE50_JOB_INST_DBG_WRITE) {
		ret = copy_from_user(data, mem.data, mem.size);
		if (ret)
			return -EACCES;
		for (reg = mem.vaddr; reg < mem.vaddr + mem.size; reg += 4)
			elcore50_write(data[i++], core, reg);
	} else {
		WARN_ON(1);
		ret = -EINVAL;
		goto err;
	}

	kfree(data);
	return 0;
err:
	kfree(data);
	return ret;
}

static int
elcore50_dbg_job_inst_interrupt(struct elcore50_job_inst_dbg_desc *desc,
				void __user *arg)
{
	struct elcore50_job_inst_desc *job_inst = desc->inst;

	job_inst->debug_state = ELCORE50_DBG_INTERRUPTED;
	job_inst->stop_reason = ELCORE50_STOP_REASON_DBG_INTERRUPT;
	return 0;
}

static int
elcore50_dbg_job_inst_continue(struct elcore50_job_inst_dbg_desc *desc,
			       void __user *arg)
{
	struct elcore50_job_inst_desc *job_inst = desc->inst;
	struct elcore50_core *core = job_inst->core;
	u32 reg, dbsar;

	reg = elcore50_read(core, DSP_PC);
	for (dbsar = DSP_DBSAR0; dbsar <= DSP_DBSAR3; dbsar += DSP_DBSARNEXT) {
		if (elcore50_read(core, dbsar) == reg) {
			elcore50_write(1, core, DSP_DBCNTR);
			job_inst->step_breakpoint = 1;
			elcore50_write(0xFFFFFFFF, core, dbsar);
			job_inst->dbsar_addr = dbsar;
			job_inst->dbsar_value = reg;
			job_inst->dbcnt_rest_plus_1 = 0;
			break;
		}
	}

	job_inst->debug_state = ELCORE50_DBG_RUN;
	return 0;
}

static int elcore50_dbg_get_stop_reason(struct elcore50_job_inst_dbg_desc *desc,
					void __user *arg)
{
	struct elcore50_job_inst_desc *job_inst = desc->inst;
	struct elcore50_dbg_stop_reason stop_reason;

	stop_reason.reason = job_inst->stop_reason;

	return copy_to_user(arg, &stop_reason,
			    sizeof(struct elcore50_dbg_stop_reason));
}

static int
elcore50_dbg_hw_breakpoint_set(struct elcore50_job_inst_dbg_desc *desc,
			       void __user *arg)
{
	struct elcore50_job_inst_desc *job_inst = desc->inst;
	struct elcore50_core *core = job_inst->core;
	u32 vaddr, regval;
	int reg, ret;

	ret = copy_from_user(&vaddr, arg, sizeof(u32));
	if (ret)
		return -EACCES;

	for (reg = DSP_DBSAR0; reg <= DSP_DBSAR3; reg += DSP_DBSARNEXT) {
		regval = elcore50_read(core, reg);
		// Check if the register is free
		if (regval == 0xffffffff) {
			elcore50_write(vaddr, core, reg);
			return 0;
		}
	}

	return -EBUSY;
}

static int
elcore50_dbg_hw_breakpoint_clear(struct elcore50_job_inst_dbg_desc *desc,
				 void __user *arg)
{
	struct elcore50_job_inst_desc *job_inst = desc->inst;
	struct elcore50_core *core = job_inst->core;
	u32 vaddr, regval;
	int reg, ret;

	ret = copy_from_user(&vaddr, arg, sizeof(u32));
	if (ret)
		return -EACCES;

	for (reg = DSP_DBSAR0; reg <= DSP_DBSAR3; reg += DSP_DBSARNEXT) {
		regval = elcore50_read(core, reg);
		if (regval == vaddr)
			elcore50_write(0xffffffff, core, reg);
	}

	return 0;
}

static int elcore50_dbg_step(struct elcore50_job_inst_dbg_desc *desc,
			     void __user *arg)
{
	struct elcore50_job_inst_desc *job_inst = desc->inst;
	struct elcore50_core *core = job_inst->core;
	u32 steps;
	u32 reg, dbsar;
	int ret = 0;

	ret = copy_from_user(&steps, arg, sizeof(u32));
	if (ret)
		return -EACCES;

	if (steps == 0)
		return -EINVAL;

	if (job_inst->debug_state != ELCORE50_DBG_INTERRUPTED)
		return -EINVAL;

	reg = elcore50_read(core, DSP_PC);
	for (dbsar = DSP_DBSAR0; dbsar <= DSP_DBSAR3; dbsar += DSP_DBSARNEXT) {
		if (elcore50_read(core, dbsar) == reg) {
			elcore50_write(0xFFFFFF, core, dbsar);
			job_inst->step_breakpoint = 1;
			job_inst->dbsar_addr = dbsar;
			job_inst->dbsar_value = reg;
			job_inst->dbcnt_rest_plus_1 = steps;
			steps = 1;
			break;
		}
	}
	elcore50_write(steps, core, DSP_DBCNTR);

	job_inst->debug_state = ELCORE50_DBG_RUN;
	return 0;
}

long elcore50_dbg_ioctl_safe(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	struct elcore50_job_inst_dbg_desc *pdata =
		(struct elcore50_job_inst_dbg_desc *)file->private_data;
	void __user *const uptr = (void __user *)arg;
	int ret;

	switch (cmd) {
	case ELCORE50_IOC_DBG_MEMORY_READ:
		ret = elcore50_dbg_memory_rw(pdata, uptr,
					     ELCORE50_JOB_INST_DBG_READ);
		break;
	case ELCORE50_IOC_DBG_MEMORY_WRITE:
		ret = elcore50_dbg_memory_rw(pdata, uptr,
					     ELCORE50_JOB_INST_DBG_WRITE);
		break;
	case ELCORE50_IOC_DBG_REGISTER_READ:
		ret = elcore50_dbg_register_rw(pdata, uptr,
					       ELCORE50_JOB_INST_DBG_READ);
		break;
	case ELCORE50_IOC_DBG_REGISTER_WRITE:
		ret = elcore50_dbg_register_rw(pdata, uptr,
					       ELCORE50_JOB_INST_DBG_WRITE);
		break;
	case ELCORE50_IOC_DBG_JOB_INSTANCE_INTERRUPT:
		ret = elcore50_dbg_job_inst_interrupt(pdata, uptr);
		break;
	case ELCORE50_IOC_DBG_JOB_INSTANCE_CONTINUE:
		ret = elcore50_dbg_job_inst_continue(pdata, uptr);
		break;
	case ELCORE50_IOC_DBG_GET_STOP_REASON:
		ret = elcore50_dbg_get_stop_reason(pdata, uptr);
		break;
	case ELCORE50_IOC_DBG_HW_BREAKPOINT_SET:
		ret = elcore50_dbg_hw_breakpoint_set(pdata, uptr);
		break;
	case ELCORE50_IOC_DBG_HW_BREAKPOINT_CLEAR:
		ret = elcore50_dbg_hw_breakpoint_clear(pdata, uptr);
		break;
	case ELCORE50_IOC_DBG_STEP:
		ret = elcore50_dbg_step(pdata, uptr);
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

static long elcore50_dbg_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	struct elcore50_job_inst_dbg_desc *pdata =
		(struct elcore50_job_inst_dbg_desc *)file->private_data;
	struct elcore50_job_inst_desc *job_inst = pdata->inst;
	int ret = 0;

	mutex_lock(&job_inst->debug_lock);
	job_inst->debug_request = DBG_REQUEST_IOCTL;
	wake_up(&job_inst->irq_waitq);
	wait_event(job_inst->debug_waitq,
		   job_inst->debug_request == DBG_REQUEST_NONE ||
			   job_inst->debug_state == ELCORE50_DBG_EXITED ||
			   job_inst->debug_request == DBG_READY_TO_PROCESS);
	if (job_inst->debug_request == DBG_READY_TO_PROCESS) {
		ret = elcore50_dbg_ioctl_safe(file, cmd, arg);
		job_inst->debug_request = DBG_PROCESSED;
		wake_up(&job_inst->irq_waitq);
	}
	wait_event(job_inst->debug_waitq,
		   job_inst->debug_request == DBG_REQUEST_NONE ||
			   job_inst->debug_state == ELCORE50_DBG_EXITED);
	if (job_inst->debug_request != DBG_REQUEST_NONE)
		ret = -EACCES;
	mutex_unlock(&job_inst->debug_lock);

	return ret;
}

static int elcore50_job_inst_dbg_release(struct inode *inode, struct file *file)
{
	struct elcore50_job_inst_dbg_desc *desc = file->private_data;
	struct elcore50_job_inst_desc *inst = desc->inst;
	int ret = 0;

	mutex_lock(&inst->debug_lock);
	inst->debug_request = DBG_REQUEST_DETACH;
	wake_up(&inst->irq_waitq);
	wait_event(inst->debug_waitq,
		   desc->inst->debug_request == DBG_REQUEST_NONE ||
			   desc->inst->debug_state == ELCORE50_DBG_EXITED);
	if (inst->debug_state != ELCORE50_DBG_EXITED)
		ret = inst->debug_result;
	mutex_unlock(&inst->debug_lock);

	fput(desc->inst_file);
	kfree(desc);
	return ret;
}

static unsigned int elcore50_job_inst_dbg_poll(struct file *file,
					       poll_table *wait)
{
	struct elcore50_job_inst_dbg_desc *desc = file->private_data;
	struct elcore50_job_inst_desc *inst = desc->inst;

	poll_wait(file, &inst->poll_dbg_waitq, wait);

	// The spec doesn't suggest which events the job waits for, so
	// we'll signal every IO event
	if (inst->state == ELCORE50_JOB_STATUS_INTERRUPTED)
		return POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM;

	return 0;
}

static const struct file_operations elcore50_job_inst_dbg_fops = {
	.unlocked_ioctl = elcore50_dbg_ioctl,
	.release = elcore50_job_inst_dbg_release,
	.poll = elcore50_job_inst_dbg_poll,
};

int export_dbg_fd(struct elcore50_job_inst_dbg_desc *desc)
{
	return anon_inode_getfd("elcorejobinstancedebug",
				&elcore50_job_inst_dbg_fops, desc, O_RDWR);
}

int elcore50_job_dbg_attach(struct elcore50_core *core, void __user *arg)
{
	int ret;
	long hashval;
	struct elcore50_job_inst_desc *inst_desc, *inst_next;
	struct elcore50_job_instance_dbg inst_dbg;
	struct elcore50_job_inst_dbg_desc *desc;
	unsigned long flags;
	int found = 0;

	desc = kzalloc(sizeof(struct elcore50_job_inst_dbg_desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	ret = copy_from_user(&inst_dbg, arg,
			     sizeof(struct elcore50_job_instance_dbg));
	if (ret) {
		ret = -EACCES;
		goto clean_kfree;
	}

	spin_lock_irqsave(&scheduler_data.running_list_lock, flags);
	list_for_each_entry(inst_desc, &scheduler_data.running_job_list,
			    queue_node) {
		hashval = hash_long((u64)inst_desc, sizeof(long) * 8);
		if (inst_dbg.job_instance_id == hashval) {
			desc->inst = inst_desc;
			found = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&scheduler_data.running_list_lock, flags);

	if (!found) {
		spin_lock_irqsave(&core->queue_lock, flags);
		list_for_each_entry_safe(inst_desc, inst_next, &core->job_queue,
					 queue_node) {
			hashval = hash_long((u64)inst_desc, sizeof(long) * 8);
			if (inst_dbg.job_instance_id == hashval) {
				desc->inst = inst_desc;
				found = 1;
				break;
			}
		}
		spin_unlock_irqrestore(&core->queue_lock, flags);
	}

	if (desc->inst) {
		if (!desc->inst->self_file)
			ret = -EBADF;
		else
			desc->inst_file = get_file(desc->inst->self_file);
	}
	if (!desc->inst)
		ret = -EINVAL;
	else if (desc->inst->core != core) {
		// We found the global job for another core
		WARN_ON(1);
		ret = -EINVAL;
	}
	if (ret)
		goto clean_kfree;

	ret = export_dbg_fd(desc);
	if (ret < 0)
		goto clean_inst_fd;

	inst_dbg.job_instance_dbg_fd = ret;

	ret = copy_to_user(arg, &inst_dbg,
			   sizeof(struct elcore50_job_instance_dbg));
	if (ret) {
		ret = -EACCES;
		goto clean_fd;
	}

	mutex_lock(&desc->inst->debug_lock);
	desc->inst->debug_request = DBG_REQUEST_ATTACH;
	wake_up(&desc->inst->irq_waitq);
	wait_event(desc->inst->debug_waitq,
		   desc->inst->debug_request == DBG_REQUEST_NONE ||
			   desc->inst->debug_state == ELCORE50_DBG_EXITED);
	if (desc->inst->debug_state == ELCORE50_DBG_EXITED)
		ret = -EINVAL;
	else
		ret = desc->inst->debug_result;
	mutex_unlock(&desc->inst->debug_lock);

	if (ret < 0)
		goto clean_fd;

	return 0;
clean_fd:
	close_fd(inst_dbg.job_instance_dbg_fd);
	// desc and inst_file will be free by the release callback
	// so we can return from function here
	return ret;
clean_inst_fd:
	fput(desc->inst_file);
clean_kfree:
	kfree(desc);
	return ret;
}
