// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2025 RnD Center "ELVEES", JSC
 */

#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "elcore50-job-instance.h"
#include "elcore50-kretprobes.h"

struct elcore50_kretprobe {
	struct kretprobe rp;
	size_t idx;
};

static u32 max_active;
static size_t num_funcs, job_inst_run_idx, core_reset_idx;
static struct elcore50_kretprobe **kretprobes;
static struct func_data *metrics;
static struct dentry **func_dentries, *kretprobes_dentry;
static char running_jobnames[255][255];

static struct list_head job_head;
static spinlock_t global_lock;

static ssize_t reset_attr_write(struct file *file, const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	char c;
	size_t i;
	unsigned long flags;
	struct job *node;

	if (!count)
		return -EINVAL;
	if (get_user(c, user_buf))
		return -EFAULT;
	if (c != '1')
		return -EINVAL;

	for (i = 0; i < num_funcs; i++) {
		write_lock_irqsave(&metrics[i].lock, flags);
		metrics[i].attrs = (struct attrs_data){ .min_time = -1ULL };
		write_unlock_irqrestore(&metrics[i].lock, flags);
	}

	spin_lock_irqsave(&global_lock, flags);
	list_for_each_entry(node, &job_head, node) {
		write_lock(&node->lock);
		node->attrs = (struct attrs_data){ .min_time = -1ULL };
		write_unlock(&node->lock);
	}
	spin_unlock_irqrestore(&global_lock, flags);

	pr_debug("elcore50-kretprobes: Metric was reset\n");
	return count;
}

static const struct file_operations elcore50_reset_fops = {
	.owner = THIS_MODULE,
	.write = reset_attr_write,
};

static void job_debugfs_create(struct work_struct *work)
{
	struct job *job = container_of(work, struct job, debugfs_work);

	job->dentry = debugfs_create_dir(job->jobname, kretprobes_dentry);
	if (IS_ERR_OR_NULL(job->dentry)) {
		pr_err("Failed to create dentry for job '%s'\n", job->jobname);
		job->dentry = NULL;
		return;
	}

	u64 *value_ptr = (u64 *)&job->attrs.total_time;

	for (size_t i = 0; i < NUM_ATTRS; i++)
		debugfs_create_u64(attrs_names[i], 0444, job->dentry,
				   value_ptr++);

	debugfs_create_u64("exec_time", 0444, job->dentry, &job->exec_time);
}

static struct job *create_job_node(const char *jobname)
{
	unsigned long flags;
	struct job *new_node;

	if (!jobname || !jobname[0])
		return NULL;

	new_node = kzalloc(sizeof(*new_node), GFP_ATOMIC);
	if (!new_node)
		return NULL;

	rwlock_init(&new_node->lock);
	strscpy(new_node->jobname, jobname, sizeof(new_node->jobname));
	new_node->attrs.min_time = -1ULL;
	/*
	 * create_job_node() is called from a kretprobe handler (atomic
	 * context), but debugfs_create_dir() may sleep. Defer dentry
	 * creation to a workqueue.
	 */
	INIT_WORK(&new_node->debugfs_work, job_debugfs_create);

	spin_lock_irqsave(&global_lock, flags);
	list_add_tail(&new_node->node, &job_head);
	spin_unlock_irqrestore(&global_lock, flags);
	schedule_work(&new_node->debugfs_work);

	return new_node;
}

static struct job *find_job_by_name(const char *jobname)
{
	struct job *node;

	list_for_each_entry(node, &job_head, node) {
		if (!strcmp(node->jobname, jobname))
			return node;
	}

	return NULL;
}

static void calculate_attr(struct attrs_data *attrs, u64 measured_time)
{
	attrs->total_time += measured_time;
	attrs->avg_time = attrs->total_time / ++attrs->call_times;
	if (measured_time < attrs->min_time)
		attrs->min_time = measured_time;
	if (measured_time > attrs->max_time)
		attrs->max_time = measured_time;
}

static void core_reset_handler(struct elcore50_core *core)
{
	char jobname[sizeof(running_jobnames[0])];
	u64 tic_cntr = elcore50_read64(core, DSP_TIC_CNTR);
	u64 freq = core->drv_priv->devfreq_clk_last_value / HZ_PER_MHZ;
	u64 exec_time;
	unsigned long flags;
	struct job *node;

	if (freq == 0) {
		dev_warn(core->dev, "elcore50-kretprobes: freq == 0!\n");
		return;
	}
	exec_time = tic_cntr * NSEC_PER_USEC / freq;
	dev_dbg(core->dev, "elcore50-kretprobes: exec_time: %lld ns\n",
		exec_time);
	spin_lock_irqsave(&global_lock, flags);
	strscpy(jobname, running_jobnames[core->system_idx], sizeof(jobname));
	node = find_job_by_name(jobname);
	spin_unlock_irqrestore(&global_lock, flags);
	if (!node) {
		dev_dbg(core->dev,
			"elcore50-kretprobes: job node not found, creating\n");
		node = create_job_node(jobname);
	}
	if (node) {
		write_lock_irqsave(&node->lock, flags);
		node->exec_time = exec_time;
		calculate_attr(&node->attrs, exec_time);
		write_unlock_irqrestore(&node->lock, flags);
	} else
		dev_dbg(core->dev,
			"elcore50-kretprobes: job creation failed!\n");
}

static void job_start_stop_handler(size_t i, struct pt_regs *regs)
{
	if (!regs)
		return;

	if (i == job_inst_run_idx) {
		struct elcore50_job_inst_desc *desc = (void *)regs->regs[0];

		if (WARN_ON(!desc))
			return;

		if (!desc->core || !strlen(desc->name))
			return;

		strscpy(running_jobnames[desc->core->system_idx], desc->name,
			sizeof(running_jobnames[0]));
		dev_dbg(desc->core->dev,
			"elcore50-kretprobes: running job is: %s\n",
			desc->name);
	} else if (i == core_reset_idx) {
		struct elcore50_core *core = (void *)regs->regs[0];

		if (WARN_ON(!core))
			return;

		core_reset_handler(core);
	}
}

static int entry_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct elcore50_kretprobe *ekrp;

	ekrp = container_of(ri->rph->rp, struct elcore50_kretprobe, rp);
	*(u64 *)ri->data = ktime_get_ns();
	job_start_stop_handler(ekrp->idx, regs);
	return 0;
}

static int ret_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct elcore50_kretprobe *ekrp;
	u64 start, end, elapsed_time;
	unsigned long flags;

	ekrp = container_of(ri->rph->rp, struct elcore50_kretprobe, rp);
	start = *(u64 *)ri->data;
	end = ktime_get_ns();
	elapsed_time = end - start;

	write_lock_irqsave(&metrics[ekrp->idx].lock, flags);
	calculate_attr(&metrics[ekrp->idx].attrs, elapsed_time);
	write_unlock_irqrestore(&metrics[ekrp->idx].lock, flags);

	return 0;
}

static ssize_t find_symbol_idx(const char *needle)
{
	ssize_t i = num_funcs;

	while (--i >= 0)
		if (!strcmp(symbol_names[i], needle))
			break;

	return i;
}

static void free_mem(void)
{
	kfree(func_dentries);
	kfree(metrics);
	while (num_funcs--)
		kfree(kretprobes[num_funcs]);
	kfree(kretprobes);

	struct job *cursor, *store;

	list_for_each_entry_safe(cursor, store, &job_head, node) {
		list_del(&cursor->node);
		cancel_work_sync(&cursor->debugfs_work);
		kfree(cursor);
	}
}

static void unregister_krps(ssize_t num)
{
	struct kretprobe **rps;
	ssize_t i, n = 0;

	rps = kmalloc_array(num, sizeof(*rps), GFP_KERNEL);
	if (!rps) {
		// Fallback: this way to free kretprobes is slower, but without alloc.
		for (i = 0; i < num; i++)
			if (kretprobes[i] && kretprobes[i]->rp.kp.addr)
				unregister_kretprobe(&kretprobes[i]->rp);
		return;
	}

	for (i = 0; i < num; i++)
		if (kretprobes[i] && kretprobes[i]->rp.kp.addr)
			rps[n++] = &kretprobes[i]->rp;

	if (n > 0)
		unregister_kretprobes(rps, n);

	kfree(rps);
}

int elcore50_kprobe_init(struct dentry *debugfs_pdentry,
			 u32 kretprobes_max_active)
{
	if (!debugfs_pdentry) {
		pr_err("elcore50-kretprobes: debugfs_pdentry is null\n");
		return -EFAULT;
	}
	max_active = kretprobes_max_active;
	pr_debug("elcore50-kretprobes: Maximum active: %u\n", max_active);

	spin_lock_init(&global_lock);
	INIT_LIST_HEAD(&job_head);
	num_funcs = ARRAY_SIZE(symbol_names);

	job_inst_run_idx = find_symbol_idx("elcore50_job_inst_run");
	core_reset_idx = find_symbol_idx("elcore50_core_reset");
	if ((ssize_t)job_inst_run_idx < 0LL || (ssize_t)core_reset_idx < 0LL) {
		pr_err("elcore50-kretprobes: No common functions in symbol names\n");
		return -EFAULT;
	}

	kretprobes = kcalloc(num_funcs, sizeof(struct elcore50_kretprobe *),
			     GFP_KERNEL);
	metrics = kcalloc(num_funcs, sizeof(struct func_data), GFP_KERNEL);
	func_dentries = kcalloc(num_funcs, sizeof(struct dentry *), GFP_KERNEL);
	if (!kretprobes || !metrics || !func_dentries) {
		kfree(kretprobes);
		kfree(metrics);
		kfree(func_dentries);
		return -ENOMEM;
	}

	int ret = 0;
	ssize_t i = -1;

	kretprobes_dentry = debugfs_create_dir("kretprobes", debugfs_pdentry);
	if (!kretprobes_dentry) {
		pr_err("elcore50-kretprobes: Failed to create kretprobes dentry\n");
		ret = -ENOMEM;
		goto fail;
	}

	if (!debugfs_create_file("reset", 0200, kretprobes_dentry, NULL,
				 &elcore50_reset_fops)) {
		pr_err("elcore50-kretprobes: Failed to create 'reset'\n");
		ret = -ENOMEM;
		goto fail;
	}

	u64 *value_ptr;

	for (i = 0; i < num_funcs; i++) {
		rwlock_init(&metrics[i].lock);
		metrics[i].attrs.min_time = -1ULL;
		kretprobes[i] =
			kzalloc(sizeof(struct elcore50_kretprobe), GFP_KERNEL);
		if (!kretprobes[i]) {
			ret = -ENOMEM;
			goto fail;
		}
		kretprobes[i]->rp.kp.symbol_name = symbol_names[i];
		kretprobes[i]->rp.entry_handler = entry_handler;
		kretprobes[i]->rp.handler = ret_handler;
		kretprobes[i]->rp.maxactive = max_active;
		kretprobes[i]->rp.data_size = sizeof(u64);
		kretprobes[i]->idx = i;

		ret = register_kretprobe(&kretprobes[i]->rp);
		if (ret < 0) {
			pr_warn("elcore50-kretprobes: WARN: Unable to set Kprobe for %s (Probably inlined) (CODE: %d)\n",
				symbol_names[i], ret);
			kfree(kretprobes[i]);
			kretprobes[i] = NULL;
			continue;
		}

		func_dentries[i] = debugfs_create_dir(
			kretprobes[i]->rp.kp.symbol_name, kretprobes_dentry);
		if (!func_dentries[i]) {
			pr_err("elcore50-kretprobes: Failed to create directory for %s\n",
			       kretprobes[i]->rp.kp.symbol_name);
			ret = -ENOMEM;
			goto fail;
		}

		size_t j = 0;

		value_ptr = (u64 *)&metrics[i].attrs.total_time;
		for (j = 0; j < NUM_ATTRS; j++)
			debugfs_create_u64(attrs_names[j], 0444,
					   func_dentries[i], value_ptr++);
	}

	return 0;

fail:
	unregister_krps(i + 1);
	debugfs_remove_recursive(kretprobes_dentry);
	free_mem();

	return ret;
}

void elcore50_kprobe_exit(void)
{
	unregister_krps(num_funcs);
	debugfs_remove_recursive(kretprobes_dentry);
	free_mem();
}
