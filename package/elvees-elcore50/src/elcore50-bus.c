// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2022 RnD Center "ELVEES", JSC
 */

#include <linux/debugfs.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>

#include "elcore50-bus.h"
#include "elcore50-core.h"
#include "elcore50-job.h"
#include "elcore50-job-instance.h"
#include "elcore50-kretprobes.h"
#include "elcore50-mapper.h"
#include "elcore50-pm.h"
#include "elcore50-proc.h"

struct class *elcore50_class;

struct mutex elcore50_devfreq_lock;

struct elcore50_scheduler_data scheduler_data;

static struct dentry *debugfs_pdentry;

struct proc_dir_entry *proc_pdentry;

int mod_caches = 3;
int mod_dbg_registers;
u32 job_status_dump_enable;

u32 irq_timeout_msec;

u32 timer_period_usec = 10000; // 10 msec
bool use_kretprobes;
u32 kretprobes_max_active = 16;

module_param_named(caches, mod_caches, int, 0);
module_param_named(dbg_registers_simple, mod_dbg_registers, int, 0);
module_param_named(timer_period, timer_period_usec, int, 0);
module_param_named(use_kretprobes, use_kretprobes, bool, false);
module_param_named(kretprobes_max_active, kretprobes_max_active, uint, 0);

static int elcore50_bus_open(struct inode *inode, struct file *file)
{
	struct elcore50_bus_priv *priv;
	struct elcore50_core **cores = &scheduler_data.core[0];
	int i = 0, ret;

	priv = container_of(inode->i_cdev, struct elcore50_bus_priv, cdev);
	file->private_data = priv;

	/* Enable clocks for all available DSP cores */
	while (cores[i]) {
		ret = pm_runtime_resume_and_get(cores[i]->drv_priv->dev);
		if (ret < 0)
			goto err_runtime_put;
		i++;
	}

	return 0;
err_runtime_put:
	while (--i >= 0)
		pm_runtime_put(cores[i]->drv_priv->dev);
	return ret;
}

static long elcore50_bus_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	struct elcore50_bus_priv *pdata =
		(struct elcore50_bus_priv *)file->private_data;
	void __user *const uptr = (void __user *)arg;
	struct device *dev = &pdata->pdev->dev;

	switch (cmd) {
	case ELCORE50_IOC_CREATE_BUFFER:
		return elcore50_create_buffer(dev, uptr);
	case ELCORE50_IOC_CREATE_MAPPER:
		return elcore50_create_mapper(dev, uptr);
	case ELCORE50_IOC_SYNC_BUFFER:
		return elcore50_sync_buffer(file, uptr);
	case ELCORE50_IOC_CREATE_JOB:
		return elcore50_create_job(file, dev, uptr);
	case ELCORE50_IOC_ENQUEUE_JOB:
		return elcore50_enqueue_shared_job_inst(dev, uptr);
	case ELCORE50_IOC_GET_JOB_STATUS:
		return elcore50_get_job_inst_status(uptr);
	case ELCORE50_IOC_GET_JOB_COUNT:
		return elcore50_get_shared_job_inst_count(uptr);
	case ELCORE50_IOC_GET_JOB_LIST:
		return elcore50_get_shared_job_inst_list(uptr);
	}

	return -ENOTTY;
}

static int elcore50_bus_release(struct inode *inode, struct file *file)
{
	struct elcore50_core **cores = &scheduler_data.core[0];
	int i = 0;

	/* Disable clocks for all DSP cores*/
	while (cores[i]) {
		pm_runtime_put(cores[i]->drv_priv->dev);
		i++;
	}
	return 0;
}

static const struct file_operations elcore50_bus_fops = {
	.owner = THIS_MODULE,
	.open = elcore50_bus_open,
	.unlocked_ioctl = elcore50_bus_ioctl,
	.release = elcore50_bus_release,
};

static int elcore50_bus_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct elcore50_bus_priv *priv;
	int ret, major, minor;
	struct device *dev;

	dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	// Prevent splitting scatter-gather elements by dma_map_sg()
	dma_set_max_seg_size(&pdev->dev, UINT_MAX);
	if (of_device_is_compatible(node, "elvees,elcore50"))
		return elcore50_probe(pdev);

	priv = devm_kzalloc(&pdev->dev, sizeof(struct elcore50_bus_priv),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->pdev = pdev;

	ret = alloc_chrdev_region(&priv->dev_num, 0, 1, "elcore50");
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to allocate bus chrdev region\n");
		goto bus_priv_free;
	}

	major = MAJOR(priv->dev_num);
	minor = MINOR(priv->dev_num);

	cdev_init(&priv->cdev, &elcore50_bus_fops);

	ret = cdev_add(&priv->cdev, MKDEV(major, minor), 1);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to add bus cdev\n");
		goto bus_chrdev_free;
	}

	dev = device_create(elcore50_class, &pdev->dev, MKDEV(major, minor),
			    NULL, "elcore");
	if (IS_ERR(dev)) {
		dev_err(&pdev->dev, "Failed to create bus device\n");
		ret = PTR_ERR(dev);
		goto bus_cdev_del;
	}
	priv->dev = dev;

	elcore50_proc_init();

	platform_set_drvdata(pdev, priv);
	return 0;
bus_cdev_del:
	cdev_del(&priv->cdev);
bus_chrdev_free:
	unregister_chrdev_region(priv->dev_num, 1);
bus_priv_free:
	devm_kfree(&pdev->dev, priv);
	return ret;
}

/*
 * platform_driver::remove lost its int return value in v6.11 (the previous
 * .remove_new transition was folded back into .remove returning void).
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
static void elcore50_bus_remove(struct platform_device *pdev)
#else
static int elcore50_bus_remove(struct platform_device *pdev)
#endif
{
	struct device_node *node = pdev->dev.of_node;
	struct elcore50_bus_priv *priv = platform_get_drvdata(pdev);

	if (of_device_is_compatible(node, "elvees,elcore50")) {
		elcore50_remove(pdev);
	} else {
		unregister_chrdev_region(priv->dev_num, 1);
		device_destroy(elcore50_class, priv->cdev.dev);
		cdev_del(&priv->cdev);
		devm_kfree(&pdev->dev, priv);
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
	return 0;
#endif
}

#ifdef CONFIG_OF
static const struct of_device_id elcore50_dt_ids[] = {
	{ .compatible = "elvees,elcore50" },
	{ .compatible = "elvees,elcore50-bus" },
	{}
};
MODULE_DEVICE_TABLE(of, elcore50_dt_ids);
#endif

static struct platform_driver elcore50_driver = {
	.driver = {
		.name = "elcore50",
		.pm = &elcore50_pm_ops,
		.of_match_table = of_match_ptr(elcore50_dt_ids),
	},
	.probe = elcore50_bus_probe,
	.remove = elcore50_bus_remove,
};

static int __init elcore50_init(void)
{
	int ret, i;

	elcore50_class = class_create("elcore50");
	if (IS_ERR(elcore50_class))
		return PTR_ERR(elcore50_class);

	debugfs_pdentry = debugfs_create_dir("elcore50", NULL);
	if (IS_ERR(debugfs_pdentry) && (PTR_ERR(debugfs_pdentry) != -ENODEV)) {
		ret = PTR_ERR(debugfs_pdentry);
		goto err_class_destroy;
	}

	proc_pdentry = proc_mkdir("elcore50", NULL);
	if (!proc_pdentry) {
		ret = -ENOMEM;
		goto err_debugfs_remove;
	}

	mutex_init(&elcore50_devfreq_lock);
	spin_lock_init(&elcore50_platform_lock);

	init_waitqueue_head(&scheduler_data.scheduler_waitq);

	INIT_LIST_HEAD(&scheduler_data.shared_job_queue);
	INIT_LIST_HEAD(&scheduler_data.running_job_list);

	spin_lock_init(&scheduler_data.shared_queue_lock);
	spin_lock_init(&scheduler_data.running_list_lock);

	for (i = 0; i < 255; ++i)
		scheduler_data.core[i] = NULL;

	debugfs_create_u32("irq-timeout-msec", 0600, debugfs_pdentry,
			   &irq_timeout_msec);
	debugfs_create_u32("job-status-dump-enable", 0600, debugfs_pdentry,
			   &job_status_dump_enable);

	ret = platform_driver_register(&elcore50_driver);
	if (ret)
		goto err_procfs_remove;

	if (use_kretprobes) {
		if (kretprobes_max_active == 0) {
			ret = -EINVAL;
			platform_driver_unregister(&elcore50_driver);
			goto err_procfs_remove;
		}

		ret = elcore50_kprobe_init(debugfs_pdentry,
					   kretprobes_max_active);
		if (ret) {
			use_kretprobes = false;
			pr_warn("elcore50: Can't enable kretprobes, returned code %d. Continue without it.\n",
				ret);
		}
	}

	return 0;
err_procfs_remove:
	proc_remove(proc_pdentry);
err_debugfs_remove:
	debugfs_remove_recursive(debugfs_pdentry);
err_class_destroy:
	class_destroy(elcore50_class);
	return ret;
}

static void __exit elcore50_exit(void)
{
	if (use_kretprobes && kretprobes_max_active > 0)
		elcore50_kprobe_exit();
	platform_driver_unregister(&elcore50_driver);
	proc_remove(proc_pdentry);
	debugfs_remove_recursive(debugfs_pdentry);
	class_destroy(elcore50_class);
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ELVEES ELcore-50 driver");

module_init(elcore50_init);
module_exit(elcore50_exit);
