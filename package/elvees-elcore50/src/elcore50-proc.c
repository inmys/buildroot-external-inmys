// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 RnD Center "ELVEES", JSC
 */

#include <linux/seq_file.h>

#include "elcore50-pm.h"
#include "elcore50-proc.h"

extern atomic_t bad_exception_counter[32];

static int elcore50_proc_show_usage(struct seq_file *m, void *v)
{
	int i = 0, int_usage, fract_usage;
	struct elcore50_core **cores = &scheduler_data.core[0];

	while (cores[i]) {
		/* In Solaris system timer is unstable (SOLARISSW-1222).
		 * So usage > 100% is possible. For timer period = 500 msec
		 * the fluctuation is insignificant.
		 */
		if (cores[i]->current_usage > 10000) {
			int_usage = 100;
			fract_usage = 0;
		} else {
			int_usage = cores[i]->current_usage / 100;
			fract_usage = cores[i]->current_usage % 100;
		}
		seq_printf(m, "%u.%02u ", int_usage, fract_usage);
		i++;
	}
	seq_puts(m, "\n");
	return 0;
}

static int elcore50_proc_show_info(struct seq_file *m, void *v)
{
	int i = 0;
	struct elcore50_core **cores = &scheduler_data.core[0];
	u32 idr_val;

	while (cores[i]) {
		if (i)
			seq_puts(m, "\n");

		idr_val = cores[i]->idr;

		seq_printf(m, "%-19s: %d\n", "processor", cores[i]->system_idx);
		seq_printf(m, "%-19s: ELcore-50\n", "DSP family");
		seq_printf(m, "%-19s: 0x%02lx\n", "DSP revision",
			   IDR_VER(idr_val));
		seq_printf(m, "%-19s: 0x%02lx\n", "DSP subrevision",
			   IDR_REV(idr_val));
		seq_printf(m, "%-19s: %ld\n", "cluster", IDR_CLUSTER(idr_val));
		seq_printf(m, "%-19s: %ld\n", "position in cluster",
			   IDR_CORENUM(idr_val));
		i++;
	}
	return 0;
}

static int elcore50_proc_show_exceptions(struct seq_file *m, void *v)
{
	int i;

	for (i = 0; i < DQSTR_BITS; ++i)
		seq_printf(m, "%d ", atomic_read(&bad_exception_counter[i]));
	seq_puts(m, "\n");

	return 0;
}

static int elcore50_proc_show_hangs(struct seq_file *m, void *v)
{
	int i = 0, hangs = 0;
	struct elcore50_core **cores = &scheduler_data.core[0];

	while (cores[i]) {
		hangs += cores[i]->broken;
		i++;
	}

	seq_printf(m, "%d\n", hangs);

	return 0;
}

static int elcore50_proc_usage_open(struct inode *inode, struct file *file)
{
	return single_open_size(file, elcore50_proc_show_usage,
				pde_data(file_inode(file)), PAGE_SIZE);
}

static int elcore50_proc_info_open(struct inode *inode, struct file *file)
{
	return single_open_size(file, elcore50_proc_show_info,
				pde_data(file_inode(file)), PAGE_SIZE);
}

static int elcore50_proc_exceptions_open(struct inode *inode, struct file *file)
{
	return single_open_size(file, elcore50_proc_show_exceptions,
				pde_data(file_inode(file)), PAGE_SIZE);
}

static int elcore50_proc_hangs_open(struct inode *inode, struct file *file)
{
	return single_open_size(file, elcore50_proc_show_hangs,
				pde_data(file_inode(file)), PAGE_SIZE);
}

static const struct proc_ops proc_usage_fops = {
	.proc_open = elcore50_proc_usage_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static const struct proc_ops proc_info_fops = {
	.proc_open = elcore50_proc_info_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static const struct proc_ops proc_exception_fops = {
	.proc_open = elcore50_proc_exceptions_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static const struct proc_ops proc_hangs_fops = {
	.proc_open = elcore50_proc_hangs_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

int elcore50_proc_init(void)
{
	struct proc_dir_entry *pde;
	int i;

	pde = proc_create_data("usage", 0444, proc_pdentry, &proc_usage_fops,
			       NULL);
	if (!pde)
		return -ENOMEM;

	pde = proc_create_data("info", 0444, proc_pdentry, &proc_info_fops,
			       NULL);
	if (!pde)
		return -ENOMEM;

	for (i = 0; i < DQSTR_BITS; ++i)
		atomic_set(&bad_exception_counter[i], 0);

	pde = proc_create_data("exceptions", 0444, proc_pdentry,
			       &proc_exception_fops, NULL);
	if (!pde)
		return -ENOMEM;

	pde = proc_create_data("hangs", 0444, proc_pdentry, &proc_hangs_fops,
			       NULL);
	if (!pde)
		return -ENOMEM;

	return 0;
}
