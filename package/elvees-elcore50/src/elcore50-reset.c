// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2020 RnD Center "ELVEES", JSC
 */

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/reset.h>

#include "elcore50-reset.h"

int elcore50_reset(struct cluster_priv *drv_priv, bool off)
{
	int ret;

	if (drv_priv->resets == NULL) {
		// Platform does not support resets. We accept it.
		return 0;
	}

	ret = off ? reset_control_assert(drv_priv->resets) :
		    reset_control_deassert(drv_priv->resets);
	if (ret)
		dev_err(drv_priv->dev, "Failed to %s reset (%d)",
			off ? "assert" : "deassert", ret);

	return ret;
}

int elcore50_reset_init(struct cluster_priv *drv_priv)
{
	drv_priv->resets = devm_reset_control_array_get_shared(drv_priv->dev);
	if (IS_ERR(drv_priv->resets)) {
		dev_warn(drv_priv->dev, "Failed to initialize resets\n");
		drv_priv->resets = NULL;
	}

	return elcore50_reset(drv_priv, false);
}
