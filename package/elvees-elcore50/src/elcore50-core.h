/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2020 RnD Center "ELVEES", JSC
 */
#ifndef _LINUX_ELCORE50_CORE_H
#define _LINUX_ELCORE50_CORE_H

#include <linux/cdev.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/devfreq.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include "linux/kthread.h"
#include <linux/module.h>
#include <linux/version.h>
#include <linux/proc_fs.h>
#include <linux/sizes.h>
#include <linux/types.h>

#include "elcore50.h"
#include "elcore50-bus.h"
#include "elcore50-regs.h"

extern u32 job_status_dump_enable;
extern const struct dev_pm_ops elcore50_pm_ops;

extern struct class *elcore50_class;

extern struct elcore50_device_info elcore50_platform_info;
extern spinlock_t elcore50_platform_lock;

extern struct elcore50_scheduler_data scheduler_data;

struct devfreq_priv {
	unsigned long busy_time;
	ktime_t total_time;
};

struct cluster_priv {
	int ncores;
	int nirqs;
	dev_t dev_num;
	struct elcore50_core *cores;
	int *irqs;
	struct device *dev;
	void __iomem *mem;
	void __iomem *regs;
	struct reset_control *resets;
	int clock_count;
	struct clk **clocks;
	int devfreq_clk_idx;

	unsigned long devfreq_clk_last_value;

	struct devfreq *devfreq;
	struct devfreq_priv devfreq_priv;
};

struct elcore50_core {
	struct cluster_priv *drv_priv;
	int corenum;
	int system_idx;
	struct device *dev;
	struct cdev cdev;
	// The list is used for:
	//  a) telling interrupts which job is current and
	//  b) cleaning up all jobs on remove()
	struct list_head job_queue;
	spinlock_t queue_lock;
	void __iomem *xyram;
	void __iomem *pram;
	void __iomem *dsp_regs;

	struct task_struct *kthread_scheduler;

	/* DSP load measurement */
	struct hrtimer timer;
	u64 ticks_acc;
	u64 last_ticks;
	spinlock_t ticks_lock;
	u32 current_usage;

	u32 idr;

	int broken;
};

struct elcore50_scheduler_data {
	struct list_head shared_job_queue;
	spinlock_t shared_queue_lock;

	wait_queue_head_t scheduler_waitq;

	struct list_head running_job_list;
	spinlock_t running_list_lock;

	struct elcore50_core *core[255];
};

static inline void elcore50_buf_cpy(struct cluster_priv *drv_priv,
				    void __iomem *dst, void *src, size_t size)
{
	memcpy_toio(dst, src, size);
}

static inline u32 elcore50_read(struct elcore50_core *core,
				unsigned int const reg)
{
	u32 value = ioread32(core->dsp_regs + reg);

	dev_dbg(core->dev, "rd reg 0x%08x val 0x%08x\n", (unsigned int)reg,
		value);
	return value;
}

static inline u64 elcore50_read64(struct elcore50_core *core,
				  unsigned int const reg)
{
	u64 value;

	value = readq(core->dsp_regs + reg);

	dev_dbg(core->dev, "rd reg 0x%0x val 0x%016llx\n", (unsigned int)reg,
		value);
	return value;
}

static inline void elcore50_write(u32 const value, struct elcore50_core *core,
				  unsigned int const reg)
{
	dev_dbg(core->dev, "wr reg 0x%08x val 0x%x\n", (unsigned int)reg,
		value);
	iowrite32(value, core->dsp_regs + reg);
}

int elcore50_list_count(struct list_head *head);

#define elcore50_pollreg(core, addr, val, cond, sleep_us)                    \
	({                                                                   \
		might_sleep_if(sleep_us);                                    \
		for (;;) {                                                   \
			(val) = elcore50_read(core, addr);                   \
			if (cond)                                            \
				break;                                       \
			if (sleep_us)                                        \
				usleep_range((sleep_us >> 2) + 1, sleep_us); \
		}                                                            \
		0;                                                           \
	})

#define elcore50_pollreg_timeout(core, addr, val, cond, sleep_us, timeout_us) \
	({                                                                    \
		ktime_t timeout = ktime_add_us(ktime_get(), timeout_us);      \
		might_sleep_if(sleep_us);                                     \
		for (;;) {                                                    \
			(val) = elcore50_read(core, addr);                    \
			if (cond)                                             \
				break;                                        \
			if (timeout_us &&                                     \
			    ktime_compare(ktime_get(), timeout) > 0) {        \
				(val) = elcore50_read(core, addr);            \
				break;                                        \
			}                                                     \
			if (sleep_us)                                         \
				usleep_range((sleep_us >> 2) + 1, sleep_us);  \
		}                                                             \
		(cond) ? 0 : -ETIMEDOUT;                                      \
	})

void elcore50_buf_cpy(struct cluster_priv *drv_priv, void __iomem *dst,
		      void *src, size_t size);
u32 elcore50_read(struct elcore50_core *core, unsigned int const reg);
u64 elcore50_read64(struct elcore50_core *core, unsigned int const reg);
void elcore50_write(u32 const value, struct elcore50_core *core,
		    unsigned int const reg);
void elcore50_get_platform_info(struct elcore50_core *core,
				struct elcore50_device_info *device_info);
int elcore50_get_core_idx(struct elcore50_core *core, void __user *arg);
int elcore50_cores_init(struct cluster_priv *drv_priv, int ncores);
void elcore50_cores_destroy(struct cluster_priv *drv_priv);
void elcore50_core_abort(struct elcore50_core *core);
void elcore50_core_reset(struct elcore50_core *core);
#ifndef ELCORE50_NO_IRQS
irqreturn_t elcore50_irq(int irq, void *priv);
int elcore50_enable_irqs(struct cluster_priv *priv);
void elcore50_disable_irqs(struct cluster_priv *priv);
#endif
int elcore50_probe(struct platform_device *pdev);
int elcore50_remove(struct platform_device *pdev);
bool elcore50_vdma_active(struct elcore50_core *core);
void elcore50_stop_vdma(struct elcore50_core *core);

#endif
