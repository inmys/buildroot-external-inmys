// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2022 RnD Center "ELVEES", JSC
 */

#include <linux/pm_opp.h>

#include "elcore50-pm.h"
#include "elcore50-reset.h"

#define KHZ 1000

/* RUNTIME POWER MANAGEMENT FUNCTIONS */

void elcore50_clock_disable(struct cluster_priv *drv_priv)
{
	int i;

	for (i = 0; i < drv_priv->clock_count; ++i)
		clk_disable(drv_priv->clocks[i]);
}

static int elcore50_clock_enable(struct cluster_priv *drv_priv)
{
	int ret, i;

	for (i = 0; i < drv_priv->clock_count; ++i) {
		ret = clk_enable(drv_priv->clocks[i]);
		if (ret < 0) {
			dev_err(drv_priv->dev, "could not enable %d clock\n",
				i);
			goto err_enable_clk;
		}
	}

	return 0;
err_enable_clk:
	while (--i >= 0)
		clk_disable(drv_priv->clocks[i]);
	return ret;
}

static int elcore50_runtime_suspend(struct device *dev)
{
	struct cluster_priv *drv_priv = dev_get_drvdata(dev);

#ifndef ELCORE50_NO_IRQS
	elcore50_disable_irqs(drv_priv);
#endif

	elcore50_clock_disable(drv_priv);

	devfreq_suspend_device(drv_priv->devfreq);

	// resets is shared - so there are internal
	// assert/deassert counters in kernel.
	// Actual reset perfroms only if counter == 0.
	// Call assert here to keep this counters in
	// proper state, for later resume deassert call.
	elcore50_reset(drv_priv, true);
	return 0;
}

static int elcore50_runtime_resume(struct device *dev)
{
	struct cluster_priv *drv_priv = dev_get_drvdata(dev);
	int ret;

	elcore50_reset(drv_priv, false);

	ret = elcore50_clock_enable(drv_priv);
	if (ret)
		return ret;

	devfreq_resume_device(drv_priv->devfreq);
#ifndef ELCORE50_NO_IRQS
	elcore50_enable_irqs(drv_priv);
#endif

	return 0;
}

const struct dev_pm_ops elcore50_pm_ops = { SET_RUNTIME_PM_OPS(
	elcore50_runtime_suspend, elcore50_runtime_resume, NULL) };

/* END POWER MANAGEMENT FUNCTIONS */

/* FREQUENCY SCALING FUNCTIONS */

static int elcore50_freq_target(struct device *dev, unsigned long *freq,
				u32 flags)
{
	struct cluster_priv *drv_priv = dev_get_drvdata(dev);
	struct dev_pm_opp *opp;
	int i = drv_priv->devfreq_clk_idx, ret;

	opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(opp))
		return PTR_ERR(opp);

	ret = clk_set_rate(drv_priv->clocks[i], *freq);
	dev_pm_opp_put(opp);
	if (ret)
		return ret;

	drv_priv->devfreq_clk_last_value = *freq;
	return 0;
}

static int elcore50_get_dev_status(struct device *dev,
				   struct devfreq_dev_status *status)
{
	struct cluster_priv *drv_priv = dev_get_drvdata(dev);
	struct devfreq_priv *prev_status = &drv_priv->devfreq_priv;
	struct elcore50_core *core;
	u64 tmp_val, ticks = 0;
	u32 freq, busy;
	ktime_t time;
	int i = drv_priv->devfreq_clk_idx, j;

	freq = ((u32)status->current_frequency) / KHZ;
	status->current_frequency = clk_get_rate(drv_priv->clocks[i]);

	// Increment usage counter to protect from device suspending
	pm_runtime_get_noresume(drv_priv->dev);
	if (pm_runtime_active(drv_priv->dev)) {
		for (j = 0; j < drv_priv->ncores; ++j) {
			core = &drv_priv->cores[j];
			// Find the maximum loaded core of cluster
			tmp_val = elcore50_read64(core, DSP_TIC_CNTR);
			ticks = max(ticks, tmp_val);
		}
	}
	pm_runtime_put(drv_priv->dev);

	busy = freq ? ticks / freq : 0;
	if (busy)
		status->busy_time = busy - prev_status->busy_time;
	else
		status->busy_time = 0;

	time = ktime_get();
	status->total_time = ktime_ms_delta(time, prev_status->total_time);

	prev_status->busy_time = busy;
	prev_status->total_time = time;
	return 0;
}

static int elcore50_get_cur_freq(struct device *dev, unsigned long *freq)
{
	struct cluster_priv *drv_priv = dev_get_drvdata(dev);
	int i = drv_priv->devfreq_clk_idx;

	*freq = clk_get_rate(drv_priv->clocks[i]);
	return 0;
}

static struct devfreq_dev_profile devfreq_profile = {
	.polling_ms = 10,
	.target = elcore50_freq_target,
	.get_dev_status = elcore50_get_dev_status,
	.get_cur_freq = elcore50_get_cur_freq,
};

static int elcore50_devfreq_init(struct device *dev)
{
	struct cluster_priv *drv_priv = dev_get_drvdata(dev);
	int i = drv_priv->devfreq_clk_idx;
	int ret;

	ret = dev_pm_opp_of_add_table(dev);
	if (ret) {
		dev_warn(dev,
			 "Device do not support Device frequency scaling\n");
		drv_priv->devfreq = NULL;
		return 0;
	}

	devfreq_profile.initial_freq = clk_get_rate(drv_priv->clocks[i]);
	drv_priv->devfreq =
		devfreq_add_device(dev, &devfreq_profile, "performance", NULL);
	if (IS_ERR(drv_priv->devfreq)) {
		dev_pm_opp_of_remove_table(dev);
		return dev_err_probe(dev, PTR_ERR(drv_priv->devfreq),
				     "Failed to add devfreq device\n");
	}
	return 0;
}

static void elcore50_devfreq_remove(struct cluster_priv *drv_priv)
{
	if (IS_ERR_OR_NULL(drv_priv->devfreq))
		return;

	devfreq_remove_device(drv_priv->devfreq);
	dev_pm_opp_of_remove_table(drv_priv->dev);
}

/* END FREQUENCY SCALING FUNCTIONS */

/* CLOCK SETUP FUNCTIONS */

int elcore50_clock_init(struct cluster_priv *drv_priv)
{
	struct device_node *np = drv_priv->dev->of_node;
	int i, ret;

	drv_priv->clock_count = of_clk_get_parent_count(np);
	if (!drv_priv->clock_count)
		return 0;

	drv_priv->clocks = devm_kcalloc(drv_priv->dev, drv_priv->clock_count,
					sizeof(struct clk *), GFP_KERNEL);
	if (!drv_priv->clocks)
		return -ENOMEM;

	for (i = 0; i < drv_priv->clock_count; ++i) {
		drv_priv->clocks[i] = of_clk_get(np, i);
		if (IS_ERR(drv_priv->clocks[i])) {
			ret = PTR_ERR(drv_priv->clocks[i]);
			drv_priv->clocks[i] = NULL;
			dev_err_probe(drv_priv->dev, ret,
				      "Failed to get %d clock\n", i);
			goto err_clocks;
		}
		ret = clk_prepare_enable(drv_priv->clocks[i]);
		if (ret) {
			clk_put(drv_priv->clocks[i]);
			drv_priv->clocks[i] = NULL;
			dev_err_probe(drv_priv->dev, ret,
				      "Failed to enable %d clock\n", i);
			goto err_clocks;
		}
	}

#ifdef ELCORE50_MCOM03
	ret = of_property_match_string(np, "clock-names", "main_clk");
	if (ret < 0) {
		dev_err_probe(drv_priv->dev, ret, "Failed to parse main_clk\n");
		goto err_clocks;
	}
#else
	ret = of_property_match_string(np, "clock-names", "clkf");
	if (ret < 0) {
		dev_err_probe(drv_priv->dev, ret, "Failed to parse clkf\n");
		goto err_clocks;
	}
#endif
	drv_priv->devfreq_clk_idx = ret;

	/* TODO: Clarify the need of spin_lock.
	 * Without spin_lock sometimes observed incomplete opp table reading.
	 * Maybe this is due to the both of elcore50 nodes have link to the
	 * single opp_table node in device tree.
	 */
	mutex_lock(&elcore50_devfreq_lock);
	ret = elcore50_devfreq_init(drv_priv->dev);
	mutex_unlock(&elcore50_devfreq_lock);
	if (ret)
		goto err_clocks;

	return 0;

err_clocks:
	for (i = 0; i < drv_priv->clock_count; i++) {
		if (drv_priv->clocks[i]) {
			clk_disable_unprepare(drv_priv->clocks[i]);
			clk_put(drv_priv->clocks[i]);
		}
	}
	return ret;
}

void elcore50_clock_destroy(struct cluster_priv *drv_priv)
{
	int i;

	if (!drv_priv->clocks)
		return;

	if (pm_runtime_enabled(drv_priv->dev))
		pm_runtime_disable(drv_priv->dev);

	if (pm_runtime_active(drv_priv->dev))
		pm_runtime_suspend(drv_priv->dev);

	for (i = 0; i < drv_priv->clock_count; ++i) {
		if (drv_priv->clocks[i]) {
			// Clock disabled inside suspend callback
			clk_unprepare(drv_priv->clocks[i]);
			clk_put(drv_priv->clocks[i]);
		}
	}

	mutex_lock(&elcore50_devfreq_lock);
	elcore50_devfreq_remove(drv_priv);
	mutex_unlock(&elcore50_devfreq_lock);

	devm_kfree(drv_priv->dev, drv_priv->clocks);
}

/* END CLOCK SETUP FUNCTIONS */
