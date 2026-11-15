// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2020 RnD Center "ELVEES", JSC
 */

#include <linux/debugfs.h>
#include <linux/uaccess.h>

#include "elcore50-core.h"
#include "elcore50-coredump.h"
#include "elcore50-debug.h"
#include "elcore50-pm.h"
#include "elcore50-proc.h"
#include "elcore50-syscall.h"

#define VDMA_RESET_TIMEOUT_USEC (5 * USEC_PER_SEC)

static u32 uptime_timer_period_msec = 500;

static enum hrtimer_restart timer_callback(struct hrtimer *timer)
{
	struct elcore50_core *core =
		container_of(timer, struct elcore50_core, timer);
	struct cluster_priv *drv_priv = core->drv_priv;
	unsigned long clk_rate = drv_priv->devfreq_clk_last_value;
	unsigned long flags;
	u64 eport_rws, total_ticks, cur_ticks = 0;

	hrtimer_forward_now(timer, ms_to_ktime(uptime_timer_period_msec));

	// Increment usage counter to protect from device suspending
	pm_runtime_get_noresume(drv_priv->dev);
	if (pm_runtime_active(drv_priv->dev)) {
		cur_ticks = elcore50_read64(core, DSP_TIC_CNTR);

		eport_rws = elcore50_read(core, DSP_EPORTA_WCNT) +
			    elcore50_read(core, DSP_EPORTA_RCNT) +
			    elcore50_read(core, DSP_EPORTUA_WCNT) +
			    elcore50_read(core, DSP_EPORTUA_RCNT);
		// Workaround for #MCOM03-2167
		if (eport_rws >= 0xFF000000) {
			// Reset all debug counters (PCU[15])
			elcore50_write(0, core, DSP_EPORTA_RCNT);
			elcore50_write(DBDCSR_RDBG | DBDCSR_WRE, core,
				       DSP_DBDCSR);
			elcore50_write(DBDCSR_WRE, core, DSP_DBDCSR);
			dev_info(core->dev, "EPort counters have been reset\n");
		}
	}
	pm_runtime_put(drv_priv->dev);

	spin_lock_irqsave(&core->ticks_lock, flags);
	total_ticks = core->ticks_acc + (cur_ticks - core->last_ticks);
	core->ticks_acc = 0;
	core->last_ticks = cur_ticks;
	spin_unlock_irqrestore(&core->ticks_lock, flags);

	// percentage * 100
	core->current_usage = (total_ticks * 1000 * 100 * 100) /
			      ((u64)uptime_timer_period_msec * clk_rate);

	return HRTIMER_RESTART;
}

void elcore50_get_platform_info(struct elcore50_core *core,
				struct elcore50_device_info *device_info)
{
	device_info->nclusters = IDR_NCLUSTERS(core->idr);
	device_info->cluster_id = IDR_CLUSTER(core->idr);
	device_info->cluster_cap = IDR_CLUSTERCAP(core->idr);
	device_info->core_in_cluster_id = IDR_CORENUM(core->idr);
}

int elcore50_get_core_idx(struct elcore50_core *core, void __user *arg)
{
	struct elcore50_device_info device_info;
	int ret;

	if (core == NULL) {
		// Possible global job instance is still not scheduled on DSP
		return -EPERM;
	}

	elcore50_get_platform_info(core, &device_info);

	ret = copy_to_user(arg, &device_info,
			   sizeof(struct elcore50_device_info));
	if (ret)
		return ret;

	return 0;
}

static int elcore50_open(struct inode *inode, struct file *file)
{
	struct elcore50_core *core;
	int ret;

	core = container_of(inode->i_cdev, struct elcore50_core, cdev);

	file->private_data = core;

	ret = pm_runtime_resume_and_get(core->drv_priv->dev);
	return ret < 0 ? ret : 0;
}

static long elcore50_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	struct elcore50_core *pdata =
		(struct elcore50_core *)file->private_data;
	void __user *const uptr = (void __user *)arg;

	if (pdata->broken) {
		dev_alert(pdata->dev, "DSP is broken, reboot is needed\n");
		return -EIO;
	}

	switch (cmd) {
	case ELCORE50_IOC_CREATE_JOB:
		return elcore50_create_job(file, pdata->drv_priv->dev, uptr);
	case ELCORE50_IOC_ENQUEUE_JOB:
		return elcore50_enqueue_job_inst(pdata, uptr);
	case ELCORE50_IOC_GET_JOB_STATUS:
		return elcore50_get_job_inst_status(uptr);
	case ELCORE50_IOC_GET_JOB_COUNT:
		return elcore50_get_job_inst_count(pdata, uptr);
	case ELCORE50_IOC_GET_JOB_LIST:
		return elcore50_get_job_inst_list(pdata, uptr);
	case ELCORE50_IOC_DBG_JOB_ATTACH:
		return elcore50_job_dbg_attach(pdata, uptr);
	case ELCORE50_IOC_GET_CORE_IDX:
		return elcore50_get_core_idx(pdata, uptr);
	case ELCORE50_IOC_CREATE_MAPPER:
		/* Deprecated IOCTL */
		return elcore50_create_mapper(pdata->drv_priv->dev, uptr);
	case ELCORE50_IOC_CREATE_BUFFER:
		/* Deprecated IOCTL */
		return elcore50_create_buffer(pdata->drv_priv->dev, uptr);
	case ELCORE50_IOC_SYNC_BUFFER:
		/* Deprecated IOCTL */
		return elcore50_sync_buffer(file, uptr);
	}
	return -ENOTTY;
}

static int elcore50_release(struct inode *inode, struct file *file)
{
	struct elcore50_core *core;

	core = container_of(inode->i_cdev, struct elcore50_core, cdev);

	pm_runtime_put(core->drv_priv->dev);

	return 0;
}

static const struct file_operations elcore50_fops = {
	.owner = THIS_MODULE,
	.open = elcore50_open,
	.unlocked_ioctl = elcore50_ioctl,
	.release = elcore50_release,
};

void elcore50_core_abort(struct elcore50_core *core)
{
	/* switch off device interrupts and force-stop it */
#ifndef ELCORE50_NO_IRQS
	elcore50_write(0, core, DSP_DMASKR);
#endif
	elcore50_write(DCSR_STOP, core, DSP_DCSR);
}

bool elcore50_vdma_active(struct elcore50_core *core)
{
	u32 reg_tmp, csr_addr;
	int i;

	for (i = 0; i < VDMA_CHANNELS; i++) {
		csr_addr = VDMA_CSR + i * VDMA_CHANNELS_NEXT;

		reg_tmp = elcore50_read(core, csr_addr);
		if (reg_tmp & (VDMA_CSR_BUSY | VDMA_CSR_ACTV))
			return true;
	}

	return false;
}

void elcore50_stop_vdma(struct elcore50_core *core)
{
	u32 reg_tmp, csr_addr;
	int i;
	u64 attempts = 0;
	ktime_t timeout;

	for (i = 0; i < VDMA_CHANNELS; i++) {
		csr_addr = VDMA_CSR + i * VDMA_CHANNELS_NEXT;
		attempts = 0;
		timeout = ktime_add_us(ktime_get(), VDMA_RESET_TIMEOUT_USEC);
		do {
			if (ktime_after(ktime_get(), timeout)) {
				dev_err(core->dev,
					"Failed to reset VDMA channel %d for %llu attempts\n",
					i, attempts);
				core->broken = 1;
				return;
			}
			reg_tmp = elcore50_read(core, csr_addr);
			reg_tmp &= ~(VDMA_CSR_RUN | VDMA_CSR_INIT_MASK);
			elcore50_write(reg_tmp, core, csr_addr);
			cond_resched();
			attempts++;
		} while (elcore50_read(core, csr_addr) &
			 (VDMA_CSR_BUSY | VDMA_CSR_ACTV));
	}
}

void elcore50_core_reset(struct elcore50_core *core)
{
	if (core->broken)
		return;

	do {
		elcore50_write(DCSR_RESET, core, DSP_DCSR);
	} while (elcore50_read(core, DSP_MBARREG) != 0);
}

static int elcore50_core_init(struct elcore50_core *core,
			      struct cluster_priv *drv_priv, unsigned int major,
			      unsigned int minor, int corenum)
{
	struct device *dev;
	u32 regval;
	int core_system_idx, ret;

	core->broken = 0;
#ifdef ELCORE50_MCOM03
	core->xyram = drv_priv->mem;
	core->dsp_regs = drv_priv->regs;
#else
	core->xyram = drv_priv->mem + SZ_2M * corenum;
	core->dsp_regs = drv_priv->regs + DSPNEXT_OFF * corenum;
#endif
	core->pram = core->xyram + SZ_1M;

	regval = ioread32(core->dsp_regs + DSP_IDR);
	core_system_idx = IDR_CLUSTER(regval) * IDR_CLUSTERCAP(regval) +
			  IDR_CORENUM(regval);
	core->idr = regval;
	core->system_idx = core_system_idx;

	WARN_ON(scheduler_data.core[core_system_idx] != NULL);

	scheduler_data.core[core_system_idx] = core;

	core->drv_priv = drv_priv;
	cdev_init(&core->cdev, &elcore50_fops);
	ret = cdev_add(&core->cdev, MKDEV(major, minor), 1);
	if (ret < 0) {
		dev_err_probe(drv_priv->dev, ret,
			      "Failed to add ELcore-50 cdev\n");
		goto err_none;
	}

	dev = device_create(elcore50_class, drv_priv->dev, MKDEV(major, minor),
			    NULL, "elcore%d", core_system_idx);
	if (IS_ERR(dev)) {
		/* this way we can be assured cores[i] is deallocated */
		ret = PTR_ERR(dev);
		dev_err_probe(drv_priv->dev, ret,
			      "Failed to create ELcore-50 device\n");
		goto err_cdev;
	}
	core->dev = dev;
	core->corenum = corenum;

	INIT_LIST_HEAD(&core->job_queue);
	spin_lock_init(&core->queue_lock);

	core->kthread_scheduler = kthread_run(
		kthread_handler, core, "kthread-%s", core->dev->init_name);
	if (IS_ERR(core->kthread_scheduler)) {
		ret = PTR_ERR(core->kthread_scheduler);
		goto err_device;
	}

	spin_lock_init(&core->ticks_lock);

	core->ticks_acc = 0;
	core->last_ticks = 0;
	core->current_usage = 0;

	/* hrtimer_setup() (v6.15) replaced hrtimer_init() + ->function (removed in v6.16). */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 15, 0)
	hrtimer_setup(&core->timer, &timer_callback, CLOCK_MONOTONIC,
		      HRTIMER_MODE_REL);
#else
	hrtimer_init(&core->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	core->timer.function = &timer_callback;
#endif
	hrtimer_start(&core->timer, ms_to_ktime(uptime_timer_period_msec),
		      HRTIMER_MODE_REL);

	return 0;
err_device:
	device_destroy(elcore50_class, MKDEV(major, minor));
err_cdev:
	cdev_del(&core->cdev);
err_none:
	scheduler_data.core[core_system_idx] = NULL;
	return ret;
}

static void elcore50_core_destroy(struct elcore50_core *core,
				  struct cluster_priv *drv_priv)
{
	struct elcore50_job_inst_desc *cursor, *store;

	scheduler_data.core[core->system_idx] = NULL;

	hrtimer_cancel(&core->timer);

	kthread_stop(core->kthread_scheduler);
	wake_up(&scheduler_data.scheduler_waitq);

	list_for_each_entry_safe(cursor, store, &core->job_queue, queue_node) {
		elcore50_cancel_job_inst(cursor);
	}
	device_destroy(elcore50_class, core->cdev.dev);
	cdev_del(&core->cdev);
}

int elcore50_cores_init(struct cluster_priv *drv_priv, int ncores)
{
	int ret, major, minor, i;

	ret = alloc_chrdev_region(&drv_priv->dev_num, 0, ncores, "elcore50");
	if (ret < 0)
		return dev_err_probe(drv_priv->dev, ret,
				     "Failed to allocate chrdev region\n");

	drv_priv->ncores = ncores;
	drv_priv->cores = devm_kcalloc(drv_priv->dev, ncores,
				       sizeof(struct elcore50_core),
				       GFP_KERNEL);

	major = MAJOR(drv_priv->dev_num);
	for (i = 0; i < ncores; i++) {
		minor = MINOR(drv_priv->dev_num) + i;
		ret = elcore50_core_init(&drv_priv->cores[i], drv_priv, major,
					 minor, i);
		if (ret) {
			dev_err_probe(drv_priv->dev, ret,
				      "Failed to initialize core %d\n", i);
			goto err_dev;
		}
	}
	return 0;
err_dev:
	while (i > 0) {
		i--;
		elcore50_core_destroy(&drv_priv->cores[i], drv_priv);
	}
	unregister_chrdev_region(drv_priv->dev_num, ncores);
	return ret;
}

void elcore50_cores_destroy(struct cluster_priv *drv_priv)
{
	int i;

	for (i = 0; i < drv_priv->ncores; i++)
		elcore50_core_destroy(&drv_priv->cores[i], drv_priv);
	unregister_chrdev_region(drv_priv->dev_num, drv_priv->ncores);
}

#ifndef ELCORE50_NO_IRQS
irqreturn_t elcore50_irq(int irq, void *priv)
{
	struct elcore50_core *core = (struct elcore50_core *)priv;
	struct elcore50_job_inst_desc *desc;
	unsigned long flags;
	int found = 0;

	spin_lock_irqsave(&scheduler_data.running_list_lock, flags);
	list_for_each_entry(desc, &scheduler_data.running_job_list,
			    queue_node) {
		if (desc->core == core) {
			found = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&scheduler_data.running_list_lock, flags);

	if (!found) {
		dev_err(core->dev, "IRQ received, but no jobs in queue!\n");
		return IRQ_NONE;
	}

	elcore50_write(0, core, DSP_DMASKR);

	desc->core_stopped_by_irq = 1;
	desc->irq_processed = 0;

	if (job_status_dump_enable)
		dev_info(core->dev, "IRQ");

	wake_up(&desc->irq_waitq);
	return IRQ_HANDLED;
}
#endif

/**
 * elcore50_list_count - count nodes in the list
 * @head:	the head for your list.
 */
int elcore50_list_count(struct list_head *head)
{
	struct list_head *pos;
	int count = 0;

	list_for_each(pos, head)
		count++;

	return count;
}
