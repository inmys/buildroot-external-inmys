// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2018-2020 RnD Center "ELVEES", JSC
 */

#include <linux/debugfs.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>

#include "elcore50-pm.h"
#include "elcore50-reset.h"

struct elcore50_device_info elcore50_platform_info;
spinlock_t elcore50_platform_lock;
static bool platform_info_initialized;

#define IRQ_WAS_REQUESTED (1u << 31)

#ifndef ELCORE50_NO_IRQS
int elcore50_enable_irqs(struct cluster_priv *priv)
{
	int i, ret;

	for (i = 0; i < priv->nirqs * priv->ncores; i++) {
		ret = devm_request_irq(priv->dev, priv->irqs[i], elcore50_irq,
				       IRQF_SHARED, "elcore50",
				       &priv->cores[i / priv->nirqs]);

		if (ret) {
			dev_err(priv->dev,
				"Failed to request IRQ #%d, (errno:%d)\n", i,
				ret);
			goto err_irq;
		}

		priv->irqs[i] |= IRQ_WAS_REQUESTED;
	}
	return 0;

err_irq:
	while (i > 0) {
		i--;
		priv->irqs[i] &= ~IRQ_WAS_REQUESTED;
		devm_free_irq(priv->dev, priv->irqs[i],
			      &priv->cores[i / priv->nirqs]);
	}
	return ret;
}

void elcore50_disable_irqs(struct cluster_priv *priv)
{
	int i, irq, *val;

	for (i = 0; i < priv->ncores; i++) {
		for (irq = 0; irq < priv->nirqs; ++irq) {
			val = &priv->irqs[i * priv->nirqs + irq];
			if (*val == 0 || ((*val & IRQ_WAS_REQUESTED) == 0))
				break;

			*val &= ~IRQ_WAS_REQUESTED;
			devm_free_irq(priv->dev, *val, &priv->cores[i]);
		}
	}
}

static void elcore50_free_irqs(struct cluster_priv *drv_priv)
{
	elcore50_disable_irqs(drv_priv);
	devm_kfree(drv_priv->dev, drv_priv->irqs);
}
#endif

int elcore50_probe(struct platform_device *pdev)
{
	struct cluster_priv *drv_priv;
	int ret, ncores;
#ifndef ELCORE50_MCOM03
	uint32_t tmp_read;
#endif
#ifndef ELCORE50_NO_IRQS
	int i;
#endif
	struct resource *res;

	drv_priv = devm_kzalloc(&pdev->dev, sizeof(struct cluster_priv),
				GFP_KERNEL);
	if (!drv_priv)
		return -ENOMEM;
	drv_priv->dev = &pdev->dev;

	ret = elcore50_reset_init(drv_priv);
	if (ret)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENOENT;
		dev_err_probe(&pdev->dev, ret, "Failed to get registers\n");
		goto reset_deinit;
	}

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err_probe(&pdev->dev, ret, "Failed to set DMAMASK\n");
		goto reset_deinit;
	}
	// Prevent splitting scatter-gather elements by dma_map_sg()
	dma_set_max_seg_size(&pdev->dev, UINT_MAX);

	drv_priv->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(drv_priv->regs)) {
		ret = PTR_ERR(drv_priv->regs);
		dev_err_probe(&pdev->dev, ret, "Failed to map registers\n");
		goto reset_deinit;
	}
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		ret = -ENOENT;
		dev_err_probe(&pdev->dev, ret, "Failed to get memory\n");
		goto reset_deinit;
	}

	drv_priv->mem = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(drv_priv->mem)) {
		ret = PTR_ERR(drv_priv->mem);
		dev_err_probe(&pdev->dev, ret, "Failed to map registers\n");
		goto reset_deinit;
	}

	platform_set_drvdata(pdev, drv_priv);

	ret = elcore50_clock_init(drv_priv);
	if (ret)
		goto err_clock;

#ifdef ELCORE50_MCOM03
	ncores = 1;
#else
	tmp_read = ioread32(drv_priv->regs + DSP_IDR);
	ncores = (tmp_read & IDR_COCNT_MASK) >> IDR_COCNT_OFFT;
	WARN_ON((tmp_read & IDR_VER_MASK) >> IDR_VER_OFFT != 0x32);
	WARN_ON((tmp_read & IDR_REV_MASK) >> IDR_REV_OFFT != 0x66);
#endif
	ret = elcore50_cores_init(drv_priv, ncores);
	if (ret)
		goto err_clock;
	spin_lock(&elcore50_platform_lock);
	if (!platform_info_initialized) {
		elcore50_get_platform_info(&drv_priv->cores[0],
					   &elcore50_platform_info);
		platform_info_initialized = true;
	}
	spin_unlock(&elcore50_platform_lock);
#ifndef ELCORE50_NO_IRQS
	{
		int total_nirqs = of_property_count_elems_of_size(
			drv_priv->dev->of_node, "interrupts", sizeof(int));

		drv_priv->nirqs = total_nirqs / ncores;
		drv_priv->irqs = devm_kcalloc(&pdev->dev, total_nirqs,
					      sizeof(unsigned int), GFP_KERNEL);
		if (!drv_priv->irqs) {
			ret = -ENOMEM;
			goto err_device;
		}
	}
	for (i = 0; i < drv_priv->nirqs * ncores; i++) {
		drv_priv->irqs[i] = platform_get_irq(pdev, i);
		if (drv_priv->irqs[i] < 0) {
			ret = drv_priv->irqs[i];
			dev_err_probe(&pdev->dev, ret,
				      "Failed to get interrupt\n");
			goto err_device;
		}
	}
#endif
	pm_runtime_set_active(drv_priv->dev);
	pm_runtime_enable(drv_priv->dev);
	pm_runtime_suspend(drv_priv->dev);

	dev_info(drv_priv->dev, "%d ELcore-50 cores initialized\n", ncores);
	return 0;
err_device:
	elcore50_cores_destroy(drv_priv);
err_clock:
	// Here pm_runtime_suspend() still not performed so we need clocks to
	// be disabled manually.
	elcore50_clock_disable(drv_priv);
	elcore50_clock_destroy(drv_priv);
reset_deinit:
	elcore50_reset(drv_priv, true);
	return dev_err_probe(&pdev->dev, ret, "elcore50 init failed\n");
}

int elcore50_remove(struct platform_device *pdev)
{
	struct cluster_priv *drv_priv = platform_get_drvdata(pdev);

#ifndef ELCORE50_NO_IRQS
	elcore50_free_irqs(drv_priv);
#endif
	elcore50_cores_destroy(drv_priv);
	elcore50_clock_destroy(drv_priv);
	return 0;
}
