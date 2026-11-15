// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2020-2021 RnD Center "ELVEES", JSC
 */

#include <linux/anon_inodes.h>
#include <linux/hash.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/uaccess.h>

#include "elcore50-debug.h"
#include "elcore50-core.h"
#include "elcore50-job-instance.h"
#include "elcore50-mmu.h"
#include "elcore50-pm.h"
#include "elcore50-coredump.h"
#include "elcore50-syscall.h"
#include "elcore50-trace.h"

#define E50_CACHE_L1 BIT(0)
#define E50_CACHE_L2 BIT(1)

#define MBAR_RESET_TIMEOUT_USEC	       (5 * USEC_PER_SEC)
#define ELCORE50_COREDUMP_TIMEOUT_MSEC 150000

atomic_t bad_exception_counter[32];

static void set_exception_counters(u32 dqstr_val)
{
	u8 bit_id;

	while (dqstr_val) {
		bit_id = __ffs(dqstr_val);
		WARN_ON(bit_id > 31);

		atomic_inc(&bad_exception_counter[bit_id]);

		dqstr_val &= ~BIT(bit_id);
	}
}

static int elcore50_parse_args(struct elcore50_job_inst_desc *desc,
			       struct elcore50_job_instance *inst,
			       struct device *dev, int argc)
{
	struct elcore50_job_arg *earg;
	char *stack_cur = desc->stack_args;
	int reg = 0, i, j, ret, larg = 0;
	uint32_t local_addr;
	int buf_fd;
	struct fd fd;

	if (argc) {
		desc->args = kcalloc(argc, sizeof(struct userptr_mapper *),
				     GFP_KERNEL);
		if (!desc->args)
			return -ENOMEM;

		desc->arg_files =
			kcalloc(argc, sizeof(struct file *), GFP_KERNEL);
		if (!desc->arg_files) {
			kfree(desc->args);
			return -ENOMEM;
		}
	}

	for (i = 0, j = 0; i < inst->argc; i++) {
		void *basic_dest;

		earg = &inst->args[i];
		switch (earg->type) {
		case ELCORE50_TYPE_BASIC:
			if (reg < E50_ARG_REGS && earg->basic.size <= 0x8) {
				basic_dest = &desc->arg_regs[reg];
				reg++;
			} else {
				basic_dest = stack_cur;
				stack_cur += round_up(earg->basic.size, 0x8);
			}
			ret = copy_from_user(basic_dest,
					     (void __user *)earg->basic.p,
					     earg->basic.size);
			if (ret != 0) {
				dev_err(dev,
					"could not copy buffer, remaining: %x\n",
					ret);
				ret = -EFAULT;
				goto free_args;
			}
			break;
		case ELCORE50_TYPE_NC_GLOBAL_MEMORY:
		case ELCORE50_TYPE_GLOBAL_MEMORY:
		case ELCORE50_TYPE_DMA_MEMORY:
			if (earg->type == ELCORE50_TYPE_DMA_MEMORY)
				buf_fd = earg->dma_memory.mapper_fd;
			else
				buf_fd = earg->global_memory.mapper_fd;

			fd = fdget(buf_fd);
			if (!fd_file(fd) ||
			    fd_file(fd)->f_op != &elcore50_mapper_ops) {
				fdput(fd);
				ret = -EINVAL;
				goto free_args;
			}

			desc->args[j] = kzalloc(
				sizeof(struct elcore50_buf_desc), GFP_KERNEL);
			if (!desc->args[j]) {
				ret = -ENOMEM;
				goto free_args;
			}
			desc->arg_files[j] = get_file(fd_file(fd));
			desc->args[j]->mapper = fd_file(fd)->private_data;
			fdput(fd);

			/*
			 * Since the MMU is not yet initialized, and therefore
			 * the virtual addresses are unknown, this pointer is
			 * set up to be written once the MMU is filled.
			 */
			if (reg < E50_ARG_REGS) {
				desc->args[j]->wr_addr =
					(uint64_t *)&desc->arg_regs[reg];
				reg++;
			} else {
				desc->args[j]->wr_addr = (uint64_t *)stack_cur;
				stack_cur += 0x8;
			}
			desc->args[j]->arg_type = earg->type;
			j++;
			break;
		case ELCORE50_TYPE_LOCAL_MEMORY:
			local_addr = desc->local_args_addr[larg];
			if (reg < E50_ARG_REGS) {
				desc->arg_regs[reg] = local_addr;
				reg++;
			} else {
				*(uint32_t *)stack_cur = local_addr;
				stack_cur += 0x8;
			}
			larg++;
			break;
		}
	}
	return 0;
free_args:
	if (argc) {
		for (i = 0; i < desc->argc; ++i) {
			if (desc->arg_files[i])
				fput(desc->arg_files[i]);
		}
		kfree(desc->arg_files);
		for (i = 0; i < desc->argc; ++i)
			kfree(desc->args[i]);
		kfree(desc->args);
	}
	return ret;
}

struct elcore50_job_arg_sort {
	struct elcore50_job_arg arg;
	int origin_index;
};

static int local_mem_compare(const void *a, const void *b)
{
	const struct elcore50_job_arg_sort *arg1 = a, *arg2 = b;

	return arg1->arg.local_memory.size - arg2->arg.local_memory.size;
}

static int find_args_by_closest_total_size(struct elcore50_job_arg_sort *args,
					   const int argc,
					   const int contiguous_memsize,
					   int *are_args_in_lowbank)
{
	int curr_sum, max_sum = 0, start = 0, i = 0;
	int *are_args_in_lowbank_tmp;

	are_args_in_lowbank_tmp = kcalloc(argc, sizeof(int), GFP_KERNEL);
	if (!are_args_in_lowbank_tmp)
		return -ENOMEM;

	curr_sum = args[0].arg.local_memory.size;

	// To find max_sum less than contiguous_memsize
	for (i = 1; i < argc; i++) {
		// Update max_sum if it becomes greater than curr_sum
		if (max_sum < curr_sum && curr_sum <= contiguous_memsize) {
			max_sum = curr_sum;
			memcpy(are_args_in_lowbank, are_args_in_lowbank_tmp,
			       sizeof(int) * argc);
		}

		// If curr_sum becomes greater than
		// contiguous_memsize subtract starting elements of array
		while (curr_sum + args[i].arg.local_memory.size >
			       contiguous_memsize &&
		       start < i) {
			curr_sum -= args[start].arg.local_memory.size;
			// Clear index of this element from bit array
			are_args_in_lowbank_tmp[args[start].origin_index] = 0;
			start++;
		}

		// Add elements to curr_sum
		curr_sum += args[i].arg.local_memory.size;
		// Set index of this element to bit array
		are_args_in_lowbank_tmp[args[i].origin_index] = 1;
	}

	//Adding an extra check for last subarray
	if (max_sum < curr_sum && curr_sum <= contiguous_memsize) {
		max_sum = curr_sum;
		memcpy(are_args_in_lowbank, are_args_in_lowbank_tmp,
		       sizeof(int) * argc);
	}

	kfree(are_args_in_lowbank_tmp);
	return max_sum;
}

static int try_arrange_local_args(struct elcore50_job_inst_desc *desc,
				  struct elcore50_job_instance *inst, int largc,
				  uint32_t args_size,
				  uint32_t localmem_required,
				  struct elcore50_job_arg_sort *sort_earg)
{
	uint32_t localmem = PHYS_INTERNAL_INTERLEAVE_DSP;
	uint32_t localmem_high = PHYS_INTERNAL_INTERLEAVE_DSP + 0x40000;
	int i, ret, *are_args_in_lowbank = NULL;

	if (localmem_required == SZ_512K) {
		for (i = 0, largc = 0; i < inst->argc; ++i) {
			if (inst->args[i].type != ELCORE50_TYPE_LOCAL_MEMORY)
				continue;
			desc->local_args_addr[largc] = localmem;
			localmem += inst->args[i].local_memory.size;
			largc++;
		}
		return 0;
	}

	are_args_in_lowbank = kcalloc(largc, sizeof(int), GFP_KERNEL);
	if (!are_args_in_lowbank)
		return -ENOMEM;

	ret = find_args_by_closest_total_size(
		sort_earg, largc, localmem_required / 2, are_args_in_lowbank);
	if (ret < 0) {
		kfree(are_args_in_lowbank);
		return ret;
	}

	args_size = args_size - ret;
	if ((ret > localmem_required / 2) ||
	    (args_size > localmem_required / 2)) {
		kfree(are_args_in_lowbank);
		return -EINVAL;
	}

	for (i = 0, largc = 0; i < inst->argc; ++i) {
		if (inst->args[i].type != ELCORE50_TYPE_LOCAL_MEMORY)
			continue;
		if (are_args_in_lowbank[largc]) {
			desc->local_args_addr[largc] = localmem;
			localmem += inst->args[i].local_memory.size;
		} else {
			desc->local_args_addr[largc] = localmem_high;
			localmem_high += inst->args[i].local_memory.size;
		}
		largc++;
	}

	kfree(are_args_in_lowbank);
	return 0;
}

static int arrange_local_args(struct elcore50_job_inst_desc *desc,
			      struct elcore50_job_instance *inst, int largc,
			      uint32_t localmem_arg_size,
			      uint32_t localmem_required)
{
	int i, ret;
	struct elcore50_job_arg_sort *sort_earg;

	// create copy of job local args for sorting
	sort_earg = kcalloc(largc, sizeof(struct elcore50_job_arg_sort),
			    GFP_KERNEL);
	if (!sort_earg)
		return -ENOMEM;

	for (i = 0, largc = 0; i < inst->argc; ++i) {
		if (inst->args[i].type != ELCORE50_TYPE_LOCAL_MEMORY)
			continue;
		memcpy(&sort_earg[largc].arg, &inst->args[i],
		       sizeof(struct elcore50_job_arg));
		sort_earg[largc].origin_index = largc;
		largc++;
	}

	/* sort ascending arg size */
	sort(sort_earg, largc, sizeof(struct elcore50_job_arg_sort),
	     &local_mem_compare, NULL);

	do {
		ret = try_arrange_local_args(desc, inst, largc,
					     localmem_arg_size,
					     localmem_required, sort_earg);
		if (ret == -ENOMEM)
			break;
		if (ret) {
			localmem_required += SZ_128K;
			desc->l2_size--;
		}
	} while (ret);

	kfree(sort_earg);
	return ret;
}

static int elcore50_job_inst_release(struct inode *inode, struct file *file)
{
	int i;
	struct elcore50_job_inst_desc *desc = file->private_data;

	if (desc->profile) {
		hrtimer_cancel(&desc->timer);
		desc->profile = 0;
		kfree(desc->profile_desc);
	}
	elcore50_cancel_job_inst(desc);
	for (i = 0; i < desc->argc; ++i)
		fput(desc->arg_files[i]);
	fput(desc->job_file);
	if (desc->dma_pool)
		mmu_pool_destroy(desc->dma_pool);
	kfree(desc->arg_files);
	kfree(desc->local_args_addr);
	for (i = 0; i < desc->argc; ++i)
		kfree(desc->args[i]);
	kfree(desc->args);
	kfree(desc->stack_args);
#ifdef ELCORE50_DUMP
	kfree(desc->raw_args);
#endif
	kfree(desc);
	return 0;
}

static unsigned int elcore50_job_inst_poll(struct file *file, poll_table *wait)
{
	struct elcore50_job_inst_desc *desc = file->private_data;

	poll_wait(file, &desc->poll_waitq, wait);
	/* The spec doesn't suggest which events the job waits for, so
	 * we'll signal every IO event */
	if (desc->state == ELCORE50_JOB_STATUS_DONE)
		return POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM;

	// Send event if syscall data is ready
	if ((desc->state == ELCORE50_JOB_STATUS_SYSCALL) &&
	    (desc->message.type == ELCORE50_MESSAGE_SYSCALL))
		return POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM;
	return 0;
}

static ssize_t elcore50_job_inst_read(struct file *file, char __user *buf,
				      size_t size, loff_t *ppos)
{
	struct elcore50_job_inst_desc *desc =
		(struct elcore50_job_inst_desc *)file->private_data;

	WARN_ON(size != sizeof(struct elcore50_message));

	if (desc->state != ELCORE50_JOB_STATUS_SYSCALL)
		return -EINVAL;

	return sizeof(struct elcore50_message) -
	       copy_to_user(buf, &desc->message,
			    sizeof(struct elcore50_message));
}

static ssize_t elcore50_job_inst_write(struct file *file,
				       const char __user *buf, size_t size,
				       loff_t *ppos)
{
	struct elcore50_job_inst_desc *desc =
		(struct elcore50_job_inst_desc *)file->private_data;
	struct elcore50_message message;
	ssize_t ret;

	if (desc->state != ELCORE50_JOB_STATUS_SYSCALL)
		return -EINVAL;

	WARN_ON(size != sizeof(struct elcore50_message));

	ret = copy_from_user(&message, buf, sizeof(struct elcore50_message));
	if (ret)
		return ret;

	if (message.type == ELCORE50_MESSAGE_SYSCALL_REPLY) {
		desc->syscall_handled = 1;
		memcpy(&desc->message, &message,
		       sizeof(struct elcore50_message));
		wake_up(&desc->syscall_waitq);
	} else
		WARN_ON(1);

	return sizeof(struct elcore50_message);
}

static long elcore50_job_inst_ioctl(struct file *file, unsigned int cmd,
				    unsigned long arg)
{
	struct elcore50_job_inst_desc *desc =
		(struct elcore50_job_inst_desc *)file->private_data;
	void __user *const uptr = (void __user *)arg;

	switch (cmd) {
	case ELCORE50_IOC_GET_CORE_IDX:
		return elcore50_get_core_idx(desc->core, uptr);
	case ELCORE50_IOC_CANCEL_JOB_INSTANCE:
		return elcore50_cancel_job_inst(desc);
#ifdef ELCORE50_DUMP
	case ELCORE50_IOC_COREDUMP_INFO:
		return elcore50_coredump_info(desc, uptr);
	case ELCORE50_IOC_COREDUMP_ARGC:
		return elcore50_coredump_argc(desc, uptr);
	case ELCORE50_IOC_COREDUMP_ARG_INFO:
		return elcore50_coredump_arg_info(desc, uptr);
	case ELCORE50_IOC_COREDUMP_REG:
		return elcore50_coredump_reg(desc->core, uptr);
	case ELCORE50_IOC_COREDUMP_PRAM:
	case ELCORE50_IOC_COREDUMP_XYRAM:
		return elcore50_coredump_internal(desc, cmd, uptr);
	case ELCORE50_IOC_COREDUMP_DONE:
		return elcore50_coredump_done(desc);
#endif
	}
	return -ENOTTY;
}

static const struct file_operations elcore50_job_inst_fops = {
	.release = elcore50_job_inst_release,
	.poll = elcore50_job_inst_poll,
	.read = elcore50_job_inst_read,
	.write = elcore50_job_inst_write,
	.unlocked_ioctl = elcore50_job_inst_ioctl,
	.owner = THIS_MODULE,
};

static struct elcore50_job_inst_desc *
elcore50_create_job_inst(struct device *dev, void __user *arg)
{
	struct elcore50_job_inst_desc *desc;
	struct elcore50_job_arg *earg;
	struct elcore50_job_instance *inst;
	int ret, i, free_regs, argc = 0, largc = 0;
	size_t stack_args, local_mem, localmem_required, dma_mem, nc_mem;
	struct fd fd;

	free_regs = E50_ARG_REGS;
	stack_args = 0;
	local_mem = 0;
	largc = 0;
	nc_mem = 0;
	dma_mem = 0;

	desc = kzalloc(sizeof(struct elcore50_job_inst_desc), GFP_KERNEL);
	inst = kzalloc(sizeof(struct elcore50_job_instance), GFP_KERNEL);
	if (!desc || !inst)
		return ERR_PTR(-ENOMEM);

	ret = copy_from_user(inst, arg, sizeof(struct elcore50_job_instance));
	if (ret) {
		ret = -EACCES;
		goto clean_kfree;
	}
#ifdef ELCORE50_DUMP
	desc->raw_argc = inst->argc;
	desc->raw_args =
		kcalloc(inst->argc, sizeof(*desc->raw_args), GFP_KERNEL);
	if (!desc->raw_args) {
		ret = -ENOMEM;
		goto clean_kfree;
	}

	for (i = 0; i < inst->argc; i++) {
		struct elcore50_job_arg *src = &inst->args[i];
		struct elcore50_job_arg_saved *dst = &desc->raw_args[i];

		dst->raw = *src;

		if (src->type == ELCORE50_TYPE_BASIC) {
			size_t sz = min_t(size_t, 8, src->basic.size);
			void __user *uptr =
				(void __user *)(uintptr_t)src->basic.p;
			if (copy_from_user(dst->basic_value, uptr, sz)) {
				kfree(desc->raw_args);
				desc->raw_args = NULL;
				ret = -EFAULT;
				goto clean_kfree;
			}
		}
	}
#endif
	fd = fdget(inst->job_fd);
	if (!fd_file(fd) || fd_file(fd)->f_op != &elcore50_job_fops) {
		fdput(fd);
		ret = -EBADFD;
		WARN_ON(1);
		goto job_fdput;
	}
	desc->job_file = get_file(fd_file(fd));
	desc->job_desc = fd_file(fd)->private_data;
	desc->dsp_pool = desc->job_desc->dsp_pool;
	fdput(fd);

	desc->scheduled = 0;
	desc->removable = 0;

	desc->pid = task_pid_nr(current);
	strcpy(desc->name, inst->name);

	spin_lock_init(&desc->state_lock);
	spin_lock_init(&desc->position_lock);
	mutex_init(&desc->debug_lock);

	for (i = 0; i < inst->argc; i++) {
		earg = &inst->args[i];
		switch (earg->type) {
		case ELCORE50_TYPE_BASIC:
			if (free_regs > 0 && earg->basic.size <= 0x8)
				free_regs--;
			else
				stack_args += round_up(earg->basic.size, 8);
			break;
		case ELCORE50_TYPE_DMA_MEMORY:
		case ELCORE50_TYPE_GLOBAL_MEMORY:
		case ELCORE50_TYPE_NC_GLOBAL_MEMORY:
			argc++;
			if (free_regs > 0)
				free_regs--;
			else
				stack_args += 8;
			break;
		case ELCORE50_TYPE_LOCAL_MEMORY:
			local_mem += earg->local_memory.size;
			if (free_regs > 0)
				free_regs--;
			else
				stack_args += 8;
			largc++;
			break;
		}
	}

	/* Local args cannot be used in conjunction with XYRAM sections */
	if (local_mem && desc->job_desc->xyram) {
		dev_err(dev,
			"Local memory cannot be used in conjunction with XYRAM sections\n");
		ret = -ENOMEM;
		goto job_fdput;
	}

	/* TODO: users should be able to specify stack size */
	if (local_mem == 0) {
		desc->l2_size = L2_CACHE_512;
		localmem_required = 0;
	} else if (local_mem <= SZ_512K - SZ_256K) {
		desc->l2_size = L2_CACHE_256;
		localmem_required = SZ_256K;
	} else if (local_mem <= SZ_512K - SZ_128K) {
		desc->l2_size = L2_CACHE_128;
		localmem_required = SZ_512K - SZ_128K;
	} else if (local_mem <= SZ_512K) {
		desc->l2_size = L2_CACHE_NONE;
		localmem_required = SZ_512K;
	} else {
		dev_err(dev, "Not enough local memory for this job\n");
		ret = -ENOMEM;
		goto job_fdput;
	}

	desc->stack_args_size = stack_args;
	desc->argc = argc;

	if (desc->stack_args_size) {
		desc->stack_args = kzalloc(desc->stack_args_size, GFP_KERNEL);
		if (!desc->stack_args) {
			ret = -ENOMEM;
			goto job_fdput;
		}
	}

	if (largc) {
		desc->local_args_addr =
			kcalloc(largc, sizeof(uint32_t), GFP_KERNEL);
		if (!desc->local_args_addr)
			goto clean_stackargs;

		ret = arrange_local_args(desc, inst, largc, local_mem,
					 localmem_required);
		if (ret)
			goto clean_local_args;
	}

	ret = elcore50_parse_args(desc, inst, dev, argc);
	if (ret) {
		dev_err(dev, "Failed to parse arguments\n");
		goto clean_local_args;
	}

	for (i = 0; i < desc->argc; i++) {
		if (desc->args[i]->arg_type == ELCORE50_TYPE_DMA_MEMORY)
			dma_mem += desc->args[i]->mapper->size_aligned;
		else if (desc->args[i]->arg_type ==
			 ELCORE50_TYPE_NC_GLOBAL_MEMORY)
			nc_mem += desc->args[i]->mapper->size_aligned;
	}

	if (nc_mem) {
		i = 31 - (nc_mem - 1) / (128 * SZ_1M);
		desc->noncached_regions = GENMASK(31, i);
		desc->nc_mem_current = (1UL << 32) - round_up(nc_mem, SZ_128M);
	}

	desc->launcher_vaddr = inst->launcher_virtual_address;
	desc->entry_point_vaddr = inst->entry_point_virtual_address;
	desc->laucnher_stop_vaddr = inst->launcher_stop_address;

	init_waitqueue_head(&desc->poll_waitq);
	init_waitqueue_head(&desc->poll_dbg_waitq);
	init_waitqueue_head(&desc->irq_waitq);
	init_waitqueue_head(&desc->syscall_waitq);
	init_waitqueue_head(&desc->debug_waitq);
	init_waitqueue_head(&desc->finish_waitq);
#ifdef ELCORE50_DUMP
	init_waitqueue_head(&desc->coredump_waitq);
#endif
	INIT_LIST_HEAD(&desc->pmapper_list);
	INIT_LIST_HEAD(&desc->pmapper_reserved_list);

	if (dma_mem) {
		desc->dma_pool = mmu_pool_create(ilog2(E50_PAGE_SIZE), -1);
		if (IS_ERR(desc->dma_pool)) {
			ret = PTR_ERR(desc->dma_pool);
			goto clean_args;
		}

		ret = mmu_pool_add(desc->dma_pool, (1ULL << 32), dma_mem, -1);
		if (ret)
			goto clean_dmapool;
	}

	ret = anon_inode_getfd("elcorejobinstance", &elcore50_job_inst_fops,
			       desc, O_RDWR);
	if (ret < 0)
		goto clean_dmapool;
	inst->job_instance_fd = ret;

	fd = fdget(ret);
	if (!fd_file(fd)) {
		ret = -EBADF;
		goto clean_fd;
	}
	desc->self_file = fd_file(fd);
	fdput(fd);

	if (inst->debug_enable) {
		desc->debug_state = ELCORE50_DBG_INTERRUPTED;
		desc->stop_reason = ELCORE50_STOP_REASON_DBG_INTERRUPT;
	}

	ret = copy_to_user(arg, inst, sizeof(struct elcore50_job_instance));
	if (ret) {
		ret = -EACCES;
		goto clean_fd;
	}

	kfree(inst);
	return desc;
clean_fd:
	put_unused_fd(inst->job_instance_fd);
clean_dmapool:
	if (desc->dma_pool)
		mmu_pool_destroy(desc->dma_pool);
clean_args:
	for (i = 0; i < desc->argc; ++i)
		fput(desc->arg_files[i]);
	kfree(desc->arg_files);
	for (i = 0; i < desc->argc; ++i)
		kfree(desc->args[i]);
	kfree(desc->args);
clean_local_args:
	kfree(desc->local_args_addr);
clean_stackargs:
	kfree(desc->stack_args);
job_fdput:
	fput(desc->job_file);
clean_kfree:
	kfree(desc);
	kfree(inst);
	return ERR_PTR(ret);
}

static void
set_job_inst_position_locked(struct elcore50_job_inst_desc *desc,
			     enum elcore50_job_inst_position position)
{
	unsigned long flags;

	if (desc == NULL) {
		WARN_ON(1);
		return;
	}

	WARN_ON(position >= LAST_RESERVED);

	spin_lock_irqsave(&desc->position_lock, flags);
	desc->position = position;
	spin_unlock_irqrestore(&desc->position_lock, flags);
}

static spinlock_t *
get_job_inst_queue_spinlock(struct elcore50_job_inst_desc *desc)
{
	spinlock_t *lock = NULL;
	unsigned long flags;

	if (desc == NULL) {
		WARN_ON(1);
		goto queue_spinlock_done;
	}

	spin_lock_irqsave(&desc->position_lock, flags);
	switch (desc->position) {
	case PRIVATE_ENQUEUE_QUEUE:
		if (desc->core == NULL)
			WARN_ON(1);
		else
			lock = &desc->core->queue_lock;
		break;
	case SHARED_ENQUEUE_QUEUE:
		lock = &scheduler_data.shared_queue_lock;
		break;
	case RUNNINT_LIST:
		lock = &scheduler_data.running_list_lock;
		break;
	case NO_POSITION:
	case LAST_RESERVED:
	default:
		WARN_ON(1);
		break;
	}
	spin_unlock_irqrestore(&desc->position_lock, flags);

queue_spinlock_done:
	return lock;
}

int elcore50_enqueue_job_inst(struct elcore50_core *core, void __user *arg)
{
	struct elcore50_job_inst_desc *desc;
	unsigned long flags;

	desc = elcore50_create_job_inst(core->dev, arg);
	if (IS_ERR(desc)) {
		dev_err(core->dev, "queueing failed %ld\n", PTR_ERR(desc));
		return PTR_ERR(desc);
	}
	desc->core = core;
	desc->start_position = PRIVATE_ENQUEUE_QUEUE;

	spin_lock_irqsave(&core->queue_lock, flags);
	list_add_tail(&desc->queue_node, &core->job_queue);
	set_job_inst_position_locked(desc, PRIVATE_ENQUEUE_QUEUE);
	spin_unlock_irqrestore(&core->queue_lock, flags);
	wake_up(&scheduler_data.scheduler_waitq);
	return 0;
}

int elcore50_enqueue_shared_job_inst(struct device *dev, void __user *arg)
{
	struct elcore50_job_inst_desc *desc;
	unsigned long flags;

	desc = elcore50_create_job_inst(dev, arg);
	if (IS_ERR(desc)) {
		dev_err(dev, "queueing failed %ld\n", PTR_ERR(desc));
		return PTR_ERR(desc);
	}
	/* Core will be specified while scheduling. */
	desc->core = NULL;
	desc->start_position = SHARED_ENQUEUE_QUEUE;

	spin_lock_irqsave(&scheduler_data.shared_queue_lock, flags);
	list_add_tail(&desc->queue_node, &scheduler_data.shared_job_queue);
	set_job_inst_position_locked(desc, SHARED_ENQUEUE_QUEUE);
	spin_unlock_irqrestore(&scheduler_data.shared_queue_lock, flags);
	wake_up(&scheduler_data.scheduler_waitq);
	return 0;
}

int elcore50_cancel_job_inst(struct elcore50_job_inst_desc *desc)
{
	struct elcore50_job_inst_desc *cursor, *store;
	struct elcore50_core *core;
	unsigned long flags;
	int found = 0;

	if (desc->core && !desc->removable)
		dev_dbg(desc->core->dev,
			"cancel running job %s (scheduled=%d, dqstr=%#x)\n",
			desc->name, desc->scheduled, desc->dqstr);

	desc->abort = 1;
#ifndef ELCORE50_NO_IRQS
	wake_up(&desc->irq_waitq);
#endif
	wake_up(&desc->syscall_waitq);
#ifdef ELCORE50_DUMP
	wake_up(&desc->coredump_waitq);
#endif

	if (desc->start_position == SHARED_ENQUEUE_QUEUE) {
		// Search desc in the shared queue
		spin_lock_irqsave(&scheduler_data.shared_queue_lock, flags);
		list_for_each_entry_safe(cursor, store,
					 &scheduler_data.shared_job_queue,
					 queue_node) {
			if (cursor == desc) {
				found = 1;
				list_del(&desc->queue_node);
				break;
			}
		}
		spin_unlock_irqrestore(&scheduler_data.shared_queue_lock,
				       flags);

		if (found)
			goto cancel_job_instance_done;
	}

	if (desc->start_position == PRIVATE_ENQUEUE_QUEUE) {
		core = desc->core;
		// Search desc in the private queue
		spin_lock_irqsave(&core->queue_lock, flags);
		list_for_each_entry_safe(cursor, store, &core->job_queue,
					 queue_node) {
			if (cursor == desc) {
				found = 1;
				list_del(&desc->queue_node);
				break;
			}
		}
		spin_unlock_irqrestore(&core->queue_lock, flags);
	}

	// If found == 0 then job is running or already complete

cancel_job_instance_done:
	if (!desc->scheduled) {
		desc->debug_result = -EINVAL;
		desc->debug_state = ELCORE50_DBG_EXITED;
		wake_up(&desc->debug_waitq);
	} else {
		wait_event(desc->finish_waitq, desc->removable);
	}
	return 0;
}

static void get_job_inst_results(struct elcore50_job_inst_desc *desc)
{
	uint32_t irq_status;
	unsigned long flags;
	struct elcore50_core *core = desc->core;

	irq_status = elcore50_read(core, DSP_DQSTR);

	spin_lock_irqsave(&desc->state_lock, flags);
	if (irq_status & DQSTR_ERRS) {
		desc->error = ELCORE50_JOB_STATUS_ERROR;
		set_exception_counters(irq_status & DQSTR_ERRS);
		dev_warn(core->dev, "Job %s failed with DQSTR: %x", desc->name,
			 irq_status);
	} else if ((irq_status & DQSTR_STP) == DQSTR_STP) {
		desc->error = desc->vdma_active ? ELCORE50_JOB_STATUS_ERROR :
						  ELCORE50_JOB_STATUS_SUCCESS;
	} else {
		WARN_ON(1);
	}
	spin_unlock_irqrestore(&desc->state_lock, flags);
}

static inline unsigned int get_cache_prefetch_boundary(void)
{
	return L1_CTRL_PFB_2M;
}

static void caches_setup(struct elcore50_job_inst_desc *desc)
{
	uint32_t reg_tmp;
	struct elcore50_core *core = desc->core;

	/* L0 setup */
	elcore50_write(L0DC_CTRL_L0En | L0DC_CTRL_L0CNTREn, core, L0DC_CTRL);
	reg_tmp = CTRL_ASIDINV | CTRL_PRAMEn | CTRL_XYEn | CTRL_EPortEn |
		  CTRL_ST_PRED | CTRL_PF | CTRL_PFN(1) | CTRL_DOPF |
		  CTRL_DOPFN(2) | CTRL_PFB(get_cache_prefetch_boundary()) |
		  CTRL_VW_RM | CTRL_VR_DM | CTRL_SVISync | CTRL_SVBSync |
		  CTRL_BrCtrlEn | CTRL_MBAR | CTRL_AddrCheck | CTRL_EPROTSP(2);
	elcore50_write(reg_tmp, core, DSP_CTRL);

	/* L1 setup */
	if (mod_caches & E50_CACHE_L1) {
		reg_tmp = L1DC_CTRL_EN | L1DC_CTRL_CEN | L1DC_CTRL_WRBK |
			  L1DC_CTRL_WRAL | L1DC_CTRL_INVL2 |
			  L1DC_CTRL_REFILL(4) |
			  L1DC_CTRL_PFSN(L1DC_CTRL_PFN_64) |
			  L1DC_CTRL_PFVN(L1DC_CTRL_PFN_64) |
			  L1DC_CTRL_PFIN(L1DC_CTRL_PFN_0) |
			  L1DC_CTRL_PFB(get_cache_prefetch_boundary()) |
			  L1DC_CTRL_PFBWEN;
		elcore50_write(reg_tmp, core, L1DC_CTRL);

		reg_tmp = L1IC_CTRL_EN | L1IC_CTRL_PF | L1IC_CTRL_L1PFN(3) |
			  L1IC_CTRL_PFB(get_cache_prefetch_boundary()) |
			  L1IC_CTRL_CEN;
		elcore50_write(reg_tmp, core, L1IC_CTRL);
	} else {
		reg_tmp = elcore50_read(core, L1DC_CTRL);
		elcore50_write(reg_tmp & ~L1DC_CTRL_EN, core, L1DC_CTRL);
		reg_tmp = elcore50_read(core, L1IC_CTRL);
		elcore50_write(reg_tmp & ~L1IC_CTRL_EN, core, L1IC_CTRL);
	}

	/* L2 setup */
	if (mod_caches & E50_CACHE_L2 && !desc->job_desc->xyram) {
		reg_tmp = desc->l2_size << L2_CTRL_CV_OFFT;
		elcore50_write(reg_tmp | L2_CTRL_EN | L2_CTRL_CountersEN |
				       L2_CTRL_WR(2) | L2_CTRL_UCAXICACHE(0xf) |
				       L2_CTRL_AXIPROT(2),
			       core, L2_CTRL);
	} else {
		reg_tmp = elcore50_read(core, L2_CTRL) |
			  L2_CTRL_UCAXICACHE(0xf) | L2_CTRL_AXIPROT(2);
		elcore50_write(reg_tmp & ~L2_CTRL_EN, core, L2_CTRL);
	}
}

static void caches_inval(struct elcore50_job_inst_desc *desc)
{
	uint32_t reg_tmp;
	int ret;
	struct elcore50_core *core = desc->core;

	if (!desc->launcher_vaddr) {
		elcore50_write(DSP_INCTRL_INVAL_ALL, core, DSP_INVCTRL);
		ret = elcore50_pollreg_timeout(core, DSP_MBARREG, reg_tmp,
					       reg_tmp == 0, 0,
					       E50_CACHE_TIMEOUT_USEC);
		WARN_ON(ret);
	}
}

static void caches_flush_after_run(struct elcore50_job_inst_desc *desc)
{
	uint32_t i, reg_tmp;
	struct elcore50_core *core = desc->core;
	u64 attempts = 0;
	ktime_t timeout;

	reg_tmp = elcore50_read(core, DSP_PC);
	if (!desc->launcher_vaddr || (reg_tmp != desc->laucnher_stop_vaddr)) {
		/* Stop prefetchers */
		reg_tmp = elcore50_read(core, DSP_CTRL);
		reg_tmp &= ~(CTRL_PF | CTRL_DOPF);
		elcore50_write(reg_tmp, core, DSP_CTRL);

		if (mod_caches & E50_CACHE_L1) {
			reg_tmp = elcore50_read(core, L1DC_CTRL);
			reg_tmp &= ~L1DC_CTRL_PREFETCHERS;
			elcore50_write(reg_tmp, core, L1DC_CTRL);

			reg_tmp = elcore50_read(core, L1IC_CTRL);
			reg_tmp &= ~L1IC_CTRL_PF;
			elcore50_write(reg_tmp, core, L1IC_CTRL);
		}

		elcore50_write(DSP_INVCTRL_FLUSH_ALL, core, DSP_INVCTRL);
	}

	timeout = ktime_add_us(ktime_get(), MBAR_RESET_TIMEOUT_USEC);
	while (elcore50_read(core, DSP_MBARREG)) {
		if (ktime_after(ktime_get(), timeout)) {
			dev_err(core->dev,
				"Failed to reset MBARReg for %lld attempts\n",
				attempts);
			core->broken = 1;
			return;
		}
		for (i = 0; i < VMMU_TLBS; ++i) {
			reg_tmp = elcore50_read(core, VMMU_TLB_CTRL +
							      i * sizeof(u32));
			reg_tmp |= VMMU_TLB_CTRL_DUMMY;
			elcore50_write(reg_tmp, core,
				       VMMU_TLB_CTRL + i * sizeof(u32));
		}
		cond_resched();
		attempts++;
	}
	if (attempts && !core->broken) {
		dev_dbg(core->dev,
			"MBAR reset after %lld attempts in %llu usecs\n",
			attempts,
			MBAR_RESET_TIMEOUT_USEC -
				ktime_us_delta(timeout, ktime_get()));
	}
}

static void elcore50_write_regs(struct elcore50_job_inst_desc *desc)
{
	struct elcore50_core *core = desc->core;
	struct cluster_priv *drv_priv = core->drv_priv;
	struct elcore50_job_desc *job_desc = desc->job_desc;
	int i;
	uint32_t stack_wr;
	off_t stack_offset;

	for (i = 0; i < E50_ARG_REGS; i++)
		elcore50_buf_cpy(drv_priv, i * 0x8 + DSP_R0 + core->dsp_regs,
				 &desc->arg_regs[i], 0x8);
	stack_wr = job_desc->stack_virtual_address - desc->stack_args_size;
	elcore50_write(stack_wr, core, DSP_R12);
	elcore50_write(stack_wr, core, DSP_R13);
	if (desc->launcher_vaddr)
		elcore50_write(desc->launcher_vaddr, core, DSP_PC);
	memcpy(job_desc->stack_start - desc->stack_args_size, desc->stack_args,
	       desc->stack_args_size);
	if (desc->stack_args_size) {
		stack_offset = job_desc->stack_start - desc->stack_args_size -
			       job_desc->stack_mapper->vaddr;
		sync_buffer(desc->stack_args_size, stack_offset,
			    job_desc->stack_mapper,
			    ELCORE50_BUF_SYNC_DIR_TO_DEVICE);
	}
	elcore50_write(desc->entry_point_vaddr, core,
		       desc->launcher_vaddr ? DSP_R8 : DSP_PC);
	elcore50_write(desc->entry_point_vaddr, core, DSP_ERTAR);
	elcore50_write(desc->entry_point_vaddr, core, DSP_DRTAR);
	elcore50_write(desc->entry_point_vaddr, core, DSP_IRTAR);
	elcore50_write((u32)job_desc->pt4_dma_addr, core, VMMU_PTW_PBA_L);
	elcore50_write((u32)(job_desc->pt4_dma_addr >> 32), core,
		       VMMU_PTW_PBA_H);
// VP behaves differently, rf#12206
#ifdef ELCORE50_VP
	elcore50_write(VMMU_PTW_CFG_41B, core, VMMU_PTW_CFG);
#else
	elcore50_write(VMMU_PTW_CFG_41B | VMMU_PTW_CFG_INV |
			       VMMU_PTW_CFG_A_CACHE(0xf) |
			       VMMU_PTW_CFG_A_PROT(2) | VMMU_PTW_CFG_PREFETCH,
		       core, VMMU_PTW_CFG);
#endif
	for (i = 0; i < VMMU_TLBS; i++)
		elcore50_write(0, core, VMMU_TLB_CTRL + i * sizeof(u32));
	elcore50_write(0xFFFFFFFF, core, DSP_MREGIONS);
	elcore50_write(0xFFFFFFFF & ~desc->noncached_regions, core,
		       DSP_CREGIONS);
	elcore50_write(0, core, DSP_IMASKR);
}

static void elcore50_core_run(struct elcore50_job_inst_desc *desc)
{
	struct elcore50_core *core = desc->core;
	unsigned long flags_state;

	elcore50_write(DBDCSR_WRE, core, DSP_DBDCSR);
#ifndef ELCORE50_NO_IRQS
	elcore50_write(DQSTR_STP | DQSTR_ERRS, core, DSP_DMASKR);
	desc->core_stopped_by_irq = 0;
#endif
	spin_lock_irqsave(&desc->state_lock, flags_state);
	desc->state = ELCORE50_JOB_STATUS_RUN;
	spin_unlock_irqrestore(&desc->state_lock, flags_state);
	elcore50_write(DCSR_RUN, core, DSP_DCSR);
}

static char *job_instance_state_to_str(enum elcore50_job_instance_state state)
{
	switch (state) {
	case ELCORE50_JOB_STATUS_ENQUEUED:
		return "ENQUEUED";
	case ELCORE50_JOB_STATUS_RUN:
		return "RUN";
	case ELCORE50_JOB_STATUS_INTERRUPTED:
		return "INTERRUPTED";
	case ELCORE50_JOB_STATUS_SYSCALL:
		return "SYSCALL";
	case ELCORE50_JOB_STATUS_DONE:
		return "DONE";
	}
	return "UNKNOWN";
}

static char *stop_reason_to_str(enum elcore50_stop_reason reason)
{
	switch (reason) {
	case ELCORE50_STOP_REASON_HW_BREAKPOINT:
		return "HW_BREAKPOINT";
	case ELCORE50_STOP_REASON_SW_BREAKPOINT:
		return "SW_BREAKPOINT";
	case ELCORE50_STOP_REASON_EXTERNAL_REQUEST:
		return "EXTERNAL_REQUEST";
	case ELCORE50_STOP_REASON_STEP:
		return "STEP";
	case ELCORE50_STOP_REASON_DBG_INTERRUPT:
		return "INTERRUPT";
	case ELCORE50_STOP_REASON_APP_EXCEPTION:
		return "APP_EXCEPTION";
	}
	return "UNKNOWN";
}

static char *debug_state_to_str(enum elcore50_debug_state state)
{
	switch (state) {
	case ELCORE50_DBG_RUN:
		return "RUN";
	case ELCORE50_DBG_INTERRUPTED:
		return "INTERRUPTED";
	case ELCORE50_DBG_EXITED:
		return "EXITED";
	case ELCORE50_DBG_LAST:
		return "OUPS!";
	}
	return "UNKNOWN";
}

static char *debug_request_to_str(enum elcore50_debug_request request)
{
	switch (request) {
	case DBG_REQUEST_NONE:
		return "NONE";
	case DBG_REQUEST_IOCTL:
		return "IOCTL";
	case DBG_REQUEST_ATTACH:
		return "ATTACH";
	case DBG_REQUEST_DETACH:
		return "DETACH";
	case DBG_READY_TO_PROCESS:
		return "RDY_TO_PROCESS";
	case DBG_PROCESSED:
		return "PROCESSED";
	}
	return "UNKNOWN";
}

static void dump_job_status(struct elcore50_job_inst_desc *desc)
{
	if (job_status_dump_enable == 0)
		return;

	dev_info(desc->core->dev, "State: %s; Stop reason: %s; Dbg state: %s",
		 job_instance_state_to_str(desc->state),
		 stop_reason_to_str(desc->stop_reason),
		 debug_state_to_str(desc->debug_state));
}

static void dump_job_debug_request(struct elcore50_job_inst_desc *desc)
{
	if (job_status_dump_enable == 0)
		return;

	if (desc->debug_request != DBG_REQUEST_NONE)
		dev_info(desc->core->dev, "Dbg request: %s",
			 debug_request_to_str(desc->debug_request));
}

#ifndef ELCORE50_NO_IRQS
static int event_handler(struct elcore50_job_inst_desc *desc, int ended)
{
	struct elcore50_core *core = desc->core;
	uint32_t reg_tmp, terminate_irq_mask;
	unsigned long flags_state;
	int debug_request, ret, debug_id;
	int debug_stopped = 0;

	if (desc->debug_state == ELCORE50_DBG_INTERRUPTED) {
		spin_lock_irqsave(&desc->state_lock, flags_state);
		desc->state = ELCORE50_JOB_STATUS_INTERRUPTED;
		spin_unlock_irqrestore(&desc->state_lock, flags_state);
		wake_up(&desc->poll_dbg_waitq);
	}

	if (desc->debug_state != ELCORE50_DBG_INTERRUPTED && !ended)
		elcore50_core_run(desc);

	dump_job_status(desc);

	if (irq_timeout_msec)
		ret = !wait_event_timeout(
			desc->irq_waitq,
			desc->core_stopped_by_irq & !desc->irq_processed ||
				desc->abort ||
				(desc->debug_request != DBG_REQUEST_NONE),
			msecs_to_jiffies(irq_timeout_msec));
	else {
		wait_event(desc->irq_waitq,
			   desc->core_stopped_by_irq & !desc->irq_processed ||
				   desc->abort ||
				   (desc->debug_request != DBG_REQUEST_NONE));
		ret = 0;
	}

	if (desc->step_breakpoint)
		debug_request = DBG_REQUEST_NONE;
	else
		debug_request = desc->debug_request;

	dump_job_debug_request(desc);

	if (!desc->core_stopped_by_irq && (debug_request != DBG_REQUEST_NONE))
		elcore50_write(elcore50_read(core, DSP_DCSR) & (~DCSR_RUN),
			       core, DSP_DCSR);

	if (ret || desc->abort) {
		elcore50_core_abort(core);
		elcore50_write(0, core, DSP_DMASKR);
		ended = 1;
		desc->stop_reason = ELCORE50_STOP_REASON_APP_EXCEPTION;
	}

	reg_tmp = elcore50_read(core, DSP_DQSTR);
	if (reg_tmp != 0 && job_status_dump_enable)
		dev_info(core->dev, "DQSTR: %#x", reg_tmp);
	desc->dqstr = reg_tmp;
	if (!ended && (reg_tmp & DQSTR_SC) != 0) {
		if (syscall_handler(desc)) {
			// In case of wrong syscall number DSP will be stopped
			ended = 1;
			desc->stop_reason = ELCORE50_STOP_REASON_APP_EXCEPTION;
		}
		desc->irq_processed = 1;
	}
	if (reg_tmp & DQSTR_DBG) {
		if (desc->attached) {
			desc->debug_state = ELCORE50_DBG_INTERRUPTED;
			debug_stopped = 1;
			debug_id = elcore50_read(core, DBG_INDEX) & DBG_ID;
			switch (debug_id) {
			case DBG_ID_DBSAR0:
			case DBG_ID_DBSAR1:
			case DBG_ID_DBSAR2:
			case DBG_ID_DBSAR3:
				desc->stop_reason =
					ELCORE50_STOP_REASON_HW_BREAKPOINT;
				break;
			case DBG_ID_DBCNTR:
				desc->stop_reason = ELCORE50_STOP_REASON_STEP;
				break;
			case DBG_ID_DBBREAK:
				desc->stop_reason =
					ELCORE50_STOP_REASON_SW_BREAKPOINT;
				break;
			case DBG_ID_QLIC:
				desc->stop_reason =
					ELCORE50_STOP_REASON_EXTERNAL_REQUEST;
				break;
			}

			elcore50_write(elcore50_read(core, DSP_IRQR) &
					       (~IRQR_DBG),
				       core, DSP_IRQR);
			elcore50_write(elcore50_read(core, DSP_DQSTR) &
					       (~DQSTR_DBG),
				       core, DSP_DQSTR);
		} else {
			ended = 1;
			desc->stop_reason = ELCORE50_STOP_REASON_APP_EXCEPTION;
		}
		desc->irq_processed = 1;
	}

	terminate_irq_mask = (DQSTR_STP | DQSTR_ERRS) & ~(DQSTR_SC | DQSTR_DBG);
	if (desc->core_stopped_by_irq && (reg_tmp & terminate_irq_mask)) {
		ended = 1;
		desc->irq_processed = 1;
		desc->stop_reason = ELCORE50_STOP_REASON_APP_EXCEPTION;
	}

	if (debug_stopped && desc->step_breakpoint &&
	    (elcore50_read(core, DSP_DBCNTR) != 0)) {
		elcore50_write(desc->dbsar_value, core, desc->dbsar_addr);
		desc->step_breakpoint = 0;
		desc->dbcnt_rest_plus_1 = 0;
	}

	if (desc->step_breakpoint && (elcore50_read(core, DSP_DBCNTR) == 0)) {
		if (desc->dbcnt_rest_plus_1 != 1)
			desc->debug_state = ELCORE50_DBG_RUN;
		if (desc->dbcnt_rest_plus_1)
			elcore50_write(desc->dbcnt_rest_plus_1 - 1, core,
				       DSP_DBCNTR);
		elcore50_write(desc->dbsar_value, core, desc->dbsar_addr);
		desc->step_breakpoint = 0;
		desc->dbcnt_rest_plus_1 = 0;
	}

	switch (debug_request) {
	case DBG_REQUEST_IOCTL:
		desc->debug_request = DBG_READY_TO_PROCESS;
		wake_up(&desc->debug_waitq);
		wait_event(desc->irq_waitq,
			   desc->debug_request == DBG_PROCESSED);
		break;
	case DBG_REQUEST_ATTACH:
		if (desc->attached)
			desc->debug_result = -EBUSY;
		else {
			desc->debug_state = ELCORE50_DBG_INTERRUPTED;
			desc->stop_reason = ELCORE50_STOP_REASON_DBG_INTERRUPT;
			desc->attached = 1;
			desc->debug_result = 0;
		}
		break;
	case DBG_REQUEST_DETACH:
		if (!desc->attached)
			desc->debug_result = -EINVAL;
		else {
			desc->debug_state = ELCORE50_DBG_RUN;
			desc->attached = 0;
			desc->debug_result = 0;
		}
		break;
	case DBG_REQUEST_NONE:
		break;
	}

	spin_lock_irqsave(&desc->state_lock, flags_state);
	if ((desc->step_breakpoint == 0) &&
	    (desc->debug_state == ELCORE50_DBG_INTERRUPTED)) {
		desc->state = ELCORE50_JOB_STATUS_INTERRUPTED;
	} else if (desc->debug_state == ELCORE50_DBG_RUN) {
		desc->state = ELCORE50_JOB_STATUS_RUN;
	}
	spin_unlock_irqrestore(&desc->state_lock, flags_state);

	if (debug_request != DBG_REQUEST_NONE) {
		desc->debug_request = DBG_REQUEST_NONE;
		wake_up(&desc->debug_waitq);
	}
	return ended;
}
#else
//TODO: Implement for noninterrupt mode
#endif

static void job_inst_notify_userspace(struct elcore50_job_inst_desc *desc)
{
	unsigned long flags;

	spin_lock_irqsave(&desc->state_lock, flags);
	desc->state = ELCORE50_JOB_STATUS_DONE;
	spin_unlock_irqrestore(&desc->state_lock, flags);

	wake_up(&desc->poll_waitq);
}

#ifdef ELCORE50_DUMP
static void sync_buffers(struct elcore50_job_inst_desc *desc)
{
	struct elcore50_buf_desc *arg;
	struct elcore50_buf_desc *elf;
	struct userptr_mapper *mapper;
	int i;

	for (i = 0; i < desc->argc; ++i) {
		arg = desc->args[i];
		switch (arg->arg_type) {
		case ELCORE50_TYPE_GLOBAL_MEMORY:
		case ELCORE50_TYPE_NC_GLOBAL_MEMORY:
		case ELCORE50_TYPE_DMA_MEMORY:
			mapper = arg->mapper;
			sync_buffer(mapper->size, 0, mapper,
				    ELCORE50_BUF_SYNC_DIR_TO_CPU);
			break;
		default:
			continue;
		}
	}

	size_t section_idx;

	for (section_idx = 0; section_idx < desc->job_desc->num_elf_sections;
	     ++section_idx) {
		elf = desc->job_desc->elf[section_idx];
		if (elcore50_is_addr_pram_or_xyram(desc->job_desc, elf))
			continue;
		mapper = elf->mapper;
		sync_buffer(mapper->size, 0, mapper,
			    ELCORE50_BUF_SYNC_DIR_TO_CPU);
	}
}
#endif

void elcore50_job_inst_run(struct elcore50_job_inst_desc *desc)
{
	unsigned long flags_state, flags;
	int ret;
	int ended = 0;
	u64 cur_ticks;
#ifdef ELCORE50_NO_IRQS
	uint32_t irq_status;
	uint32_t reg_tmp;
#endif
	off_t offset;
	struct elcore50_core *core = desc->core;
	struct cluster_priv *drv_priv = core->drv_priv;
	spinlock_t *cur_queue_lock;

	ret = elcore50_mmu_fill_args(desc);
	if (ret) {
		dev_err(core->dev,
			"Failed to fill MMU for job arguments. The job will be skipped\n");
		spin_lock_irqsave(&desc->state_lock, flags_state);
		desc->error = ELCORE50_JOB_STATUS_ERROR;
		spin_unlock_irqrestore(&desc->state_lock, flags_state);
		job_inst_notify_userspace(desc);
		goto done;
	}
	elcore50_mmu_sync(desc);

	caches_setup(desc);
	caches_inval(desc);

	elcore50_write_regs(desc);
	if (desc->job_desc->pram) {
		offset = desc->job_desc->pram->vaddr_mmu_dsp -
			 PHYS_INTERNAL_PRAM_DSP;
		elcore50_buf_cpy(drv_priv, core->pram + offset,
				 desc->job_desc->pram->mapper->vaddr,
				 desc->job_desc->pram->mapper->size);
	}
	if (desc->job_desc->xyram) {
		offset = desc->job_desc->xyram->vaddr_mmu_dsp -
			 PHYS_INTERNAL_DSP;
		elcore50_buf_cpy(drv_priv, core->xyram + offset,
				 desc->job_desc->xyram->mapper->vaddr,
				 desc->job_desc->xyram->mapper->size);
	}
	if (job_status_dump_enable)
		dev_info(core->dev, "Starting a job instance: %x",
			 hash_long((u64)desc, sizeof(long) * 8));
#ifndef ELCORE50_NO_IRQS
	while (1) {
		ended = event_handler(desc, ended);
		if (ended) {
			desc->step_breakpoint = 0;
			if (!desc->attached)
				break;
			desc->debug_state = ELCORE50_DBG_INTERRUPTED;
			desc->stop_reason = ELCORE50_STOP_REASON_APP_EXCEPTION;
		}
	}
#else
	// TODO: Implement event_handler
	reg_tmp = (DQSTR_STP | DQSTR_ERRS);
	while (1) {
		if (irq_timeout_msec)
			ret = elcore50_pollreg_timeout(
				core, DSP_DQSTR, irq_status,
				(irq_status & reg_tmp) || desc->abort, 20000,
				irq_timeout_msec * 1000);
		else
			ret = elcore50_pollreg(
				core, DSP_DQSTR, irq_status,
				(irq_status & reg_tmp) || desc->abort, 20000);
		if ((irq_status & DQSTR_SC) == 0)
			break;
		// In case of wrong syscall number DSP will be stopped
		if (syscall_handler(desc))
			break;
		elcore50_core_run(desc);
	}
	if (desc->abort || ret)
		elcore50_core_abort(core);
#endif
	trace_uptime(core, desc->name);

	elcore50_write(elcore50_read(core, DSP_DCSR) & (~DCSR_RUN), core,
		       DSP_DCSR);

	desc->vdma_active = elcore50_vdma_active(core);
	caches_flush_after_run(desc);
	if (desc->abort || ret) {
		dev_err(core->dev, ret ? "job timed out\n" : "job aborted\n");
		spin_lock_irqsave(&desc->state_lock, flags_state);
		desc->error = ELCORE50_JOB_STATUS_ERROR;
		spin_unlock_irqrestore(&desc->state_lock, flags_state);
	} else
		get_job_inst_results(desc);
	if (desc->job_desc->xyram) {
		offset = desc->job_desc->xyram->vaddr_mmu_dsp -
			 PHYS_INTERNAL_DSP;
		memcpy_fromio(desc->job_desc->xyram->mapper->vaddr,
			      core->xyram + offset,
			      desc->job_desc->xyram->mapper->size);
	}
#ifdef ELCORE50_DUMP
	if (desc->error == ELCORE50_JOB_STATUS_ERROR)
		sync_buffers(desc);
#endif
	/* Here we can notify users about completing job instance. */
	job_inst_notify_userspace(desc);
	desc->debug_result = -EINVAL;
	desc->debug_state = ELCORE50_DBG_EXITED;
	wake_up(&desc->debug_waitq);

	cur_ticks = elcore50_read64(core, DSP_TIC_CNTR);
	spin_lock_irqsave(&core->ticks_lock, flags);
	core->ticks_acc += cur_ticks - core->last_ticks;
	core->last_ticks = 0;
	spin_unlock_irqrestore(&core->ticks_lock, flags);

#ifdef ELCORE50_DUMP
	if (desc->error == ELCORE50_JOB_STATUS_ERROR)
		wait_event_timeout(
			desc->coredump_waitq,
			desc->coredump_done || desc->abort,
			msecs_to_jiffies(ELCORE50_COREDUMP_TIMEOUT_MSEC));
#endif

	if (desc->vdma_active) {
		dev_err(core->dev,
			"Job is finished but VDMA is still active\n");
		elcore50_stop_vdma(core);
	}

	elcore50_core_reset(core);
	elcore50_mmu_free_args(desc);
done:
	cur_queue_lock = get_job_inst_queue_spinlock(desc);

	spin_lock_irqsave(cur_queue_lock, flags);
	list_del(&desc->queue_node);
	spin_unlock_irqrestore(cur_queue_lock, flags);
}

long elcore50_get_job_inst_status(void __user *arg)
{
	struct fd fd;
	struct elcore50_job_inst_desc *desc;
	struct elcore50_job_instance_status *status;
	int ret = 0;

	status = kzalloc(sizeof(struct elcore50_job_instance_status),
			 GFP_KERNEL);
	if (!status)
		return -ENOMEM;

	ret = copy_from_user(status, arg,
			     sizeof(struct elcore50_job_instance_status));
	if (ret) {
		ret = -EACCES;
		goto clean_status;
	}
	fd = fdget(status->job_instance_fd);
	if (!fd_file(fd) || fd_file(fd)->f_op != &elcore50_job_inst_fops) {
		ret = -EBADFD;
		WARN_ON(1);
		goto clean_fd;
	}

	desc = fd_file(fd)->private_data;

	status->state = desc->state;
	status->error = desc->error;

	ret = copy_to_user(arg, status,
			   sizeof(struct elcore50_job_instance_status));
clean_fd:
	fdput(fd);
clean_status:
	kfree(status);
	return ret;
}

int elcore50_get_shared_job_inst_count(void __user *arg)
{
	u32 count;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&scheduler_data.shared_queue_lock, flags);
	count = elcore50_list_count(&scheduler_data.shared_job_queue);
	spin_unlock_irqrestore(&scheduler_data.shared_queue_lock, flags);

	ret = copy_to_user(arg, &count, sizeof(u32));
	if (ret)
		return ret;
	return 0;
}

int elcore50_get_job_inst_count(struct elcore50_core *core, void __user *arg)
{
	u32 count;
	int ret;
	unsigned long flags;
	struct elcore50_job_inst_desc *desc;

	spin_lock_irqsave(&core->queue_lock, flags);
	count = elcore50_list_count(&core->job_queue);
	spin_unlock_irqrestore(&core->queue_lock, flags);

	spin_lock_irqsave(&scheduler_data.running_list_lock, flags);
	list_for_each_entry(desc, &scheduler_data.running_job_list,
			    queue_node) {
		if (desc->core == core)
			count += 1;
	}
	spin_unlock_irqrestore(&scheduler_data.running_list_lock, flags);

	ret = copy_to_user(arg, &count, sizeof(u32));
	if (ret)
		return ret;
	return 0;
}

int elcore50_get_shared_job_inst_list(void __user *arg)
{
	struct elcore50_job_inst_desc *desc;
	struct elcore50_job_instance_info list_elem;
	struct elcore50_job_instance_list list;
	int ret = 0;
	u32 count = 0;
	unsigned long flags;

	ret = copy_from_user(&list, arg,
			     sizeof(struct elcore50_job_instance_list));
	if (ret)
		return -EACCES;

	spin_lock_irqsave(&scheduler_data.shared_queue_lock, flags);
	list_for_each_entry(desc, &scheduler_data.shared_job_queue,
			    queue_node) {
		if (count == list.job_instance_count)
			break;
		list_elem.id = hash_long((u64)desc, sizeof(long) * 8);
		list_elem.state = desc->state;
		list_elem.pid = desc->pid;
		strcpy(list_elem.name, desc->name);
		ret = copy_to_user(&list.info[count], &list_elem,
				   sizeof(struct elcore50_job_instance_info));
		if (ret) {
			spin_unlock_irqrestore(
				&scheduler_data.shared_queue_lock, flags);
			return ret;
		}
		count += 1;
	}
	spin_unlock_irqrestore(&scheduler_data.shared_queue_lock, flags);

	list.job_instance_ret = count;

	return copy_to_user(arg, &list,
			    sizeof(struct elcore50_job_instance_list));
}

int elcore50_get_job_inst_list(struct elcore50_core *core, void __user *arg)
{
	struct elcore50_job_inst_desc *desc;
	struct elcore50_job_instance_info list_elem;
	struct elcore50_job_instance_list list;
	int ret = 0;
	u32 count = 0;
	unsigned long flags;

	ret = copy_from_user(&list, arg,
			     sizeof(struct elcore50_job_instance_list));
	if (ret)
		return -EACCES;

	spin_lock_irqsave(&scheduler_data.running_list_lock, flags);
	list_for_each_entry(desc, &scheduler_data.running_job_list,
			    queue_node) {
		if (desc->core != core)
			continue;
		if (count == list.job_instance_count)
			break;
		list_elem.id = hash_long((u64)desc, sizeof(long) * 8);
		list_elem.state = desc->state;
		list_elem.pid = desc->pid;
		strcpy(list_elem.name, desc->name);
		ret = copy_to_user(&list.info[count], &list_elem,
				   sizeof(struct elcore50_job_instance_info));
		if (ret) {
			spin_unlock_irqrestore(
				&scheduler_data.running_list_lock, flags);
			return ret;
		}
		count += 1;
	}
	spin_unlock_irqrestore(&scheduler_data.running_list_lock, flags);

	spin_lock_irqsave(&core->queue_lock, flags);
	list_for_each_entry(desc, &core->job_queue, queue_node) {
		if (count == list.job_instance_count)
			break;
		list_elem.id = hash_long((u64)desc, sizeof(long) * 8);
		list_elem.state = desc->state;
		list_elem.pid = desc->pid;
		strcpy(list_elem.name, desc->name);
		ret = copy_to_user(&list.info[count], &list_elem,
				   sizeof(struct elcore50_job_instance_info));
		if (ret) {
			spin_unlock_irqrestore(&core->queue_lock, flags);
			return ret;
		}
		count += 1;
	}
	spin_unlock_irqrestore(&core->queue_lock, flags);

	list.job_instance_ret = count;

	return copy_to_user(arg, &list,
			    sizeof(struct elcore50_job_instance_list));
}

static bool check_job_inst_unlocked(struct elcore50_job_inst_desc *desc)
{
	struct elcore50_job_inst_desc *run_desc;

	if (!desc) {
		WARN_ON(1);
		return false;
	}

	list_for_each_entry(run_desc, &scheduler_data.running_job_list,
			    queue_node) {
		if (run_desc->job_desc == desc->job_desc)
			return false;
	}

	return true;
}

static struct elcore50_job_inst_desc *do_schedule(struct elcore50_core *core)
{
	struct elcore50_job_inst_desc *desc = NULL;
	int found = 0;
	unsigned long flags_queue, flags_list;

	// First, looking through the shared queue
	spin_lock_irqsave(&scheduler_data.shared_queue_lock, flags_queue);
	list_for_each_entry(desc, &scheduler_data.shared_job_queue,
			    queue_node) {
		spin_lock_irqsave(&scheduler_data.running_list_lock,
				  flags_list);
		if (check_job_inst_unlocked(desc)) {
			desc->scheduled = 1;
			desc->core = core;
			list_del(&desc->queue_node);
			list_add_tail(&desc->queue_node,
				      &scheduler_data.running_job_list);
			set_job_inst_position_locked(desc, RUNNINT_LIST);
			found = 1;
		}
		spin_unlock_irqrestore(&scheduler_data.running_list_lock,
				       flags_list);
		if (found)
			break;
	}
	spin_unlock_irqrestore(&scheduler_data.shared_queue_lock, flags_queue);

	if (!found) {
		// Looking through the private queue
		spin_lock_irqsave(&core->queue_lock, flags_queue);
		list_for_each_entry(desc, &core->job_queue, queue_node) {
			spin_lock_irqsave(&scheduler_data.running_list_lock,
					  flags_list);
			if (check_job_inst_unlocked(desc)) {
				desc->scheduled = 1;
				list_del(&desc->queue_node);
				list_add_tail(&desc->queue_node,
					      &scheduler_data.running_job_list);
				set_job_inst_position_locked(desc,
							     RUNNINT_LIST);
				found = 1;
			}
			spin_unlock_irqrestore(
				&scheduler_data.running_list_lock, flags_list);
			if (found)
				break;
		}
		spin_unlock_irqrestore(&core->queue_lock, flags_queue);
	}

	return found ? desc : NULL;
}

int kthread_handler(void *priv)
{
	struct elcore50_core *core = (struct elcore50_core *)priv;
	struct elcore50_job_inst_desc *desc;

	while (!kthread_should_stop()) {
		desc = do_schedule(core);
		if (!desc) {
			/* If the are job_instances with the same job_desc
			 * in the queues wait_event() will return immediately
			 * and CPU will in infinit loop before another
			 * job_instances are in processing by DSPs.
			 */
			wait_event(
				scheduler_data.scheduler_waitq,
				kthread_should_stop() ||
					!list_empty(
						&scheduler_data
							 .shared_job_queue) ||
					!list_empty(&core->job_queue));
			continue;
		}

		elcore50_job_inst_run(desc);
		desc->removable = 1;
		wake_up(&desc->finish_waitq);

		if (!core->broken)
			continue;

		dev_alert(core->dev, "DSP is broken, reboot is needed\n");
		while (!kthread_should_stop()) {
			usleep_range(20000, 25000);
			continue;
		}
		break;
	}

	return 0;
}
