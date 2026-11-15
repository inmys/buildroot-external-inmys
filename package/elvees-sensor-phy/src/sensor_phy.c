#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>
#include <linux/version.h>

#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "sensor_phy.h"
#include "sensor_phy_regs.h"

#define BITMASK(num)	((1 << (num)) - 1)

static bool device_detected = false;
static bool driver_registered = false;

static struct class *sensor_phy_class; /* char device class */

struct sensor_phy {
	u8 enabled_phy;
	u8 enabled_ref_clk;

	struct regmap *media_regs_base;

	struct cdev cdev;
	struct class *class;
	struct device *dev;
	dev_t dev_str;
	int devnum;
	int sensor_major;
	struct clk *mipi_rx_cfg_clk;
	struct clk *cmos_clk;
};

struct test_data {
	u8 addr_msb;
	u8 addr_lsb;
	u8 wdata;
};

struct test_data test_data_seq[] = {
	{0x00, 0xe2, 0xb6},	// ddl_osc_freq_target_ovr_rw[7:0] value according to data rate
	{0x00, 0xe3, 0x01},	// ddl_osc_freq_target_ovr_rw[11:8] value according to data rate
	{0x00, 0xe4, 0x21},	// ddl_osc_freq_target_ovr_en_rw = 1'b1 and counter_for_des_en_bypass_rw = 0x2
	{0x00, 0xe5, 0x01},	// counter_for_des_en_bypass_rw = 1'b1
};

static int sensor_phy_probe(struct platform_device *ofdev);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
static void sensor_phy_remove(struct platform_device *ofdev);
#else
static int sensor_phy_remove(struct platform_device *ofdev);
#endif
static int sensor_phy_pm_runtime_suspend(struct device *dev);
static int sensor_phy_pm_runtime_resume(struct device *dev);
void sensor_phy_field_set(struct regmap *reg_base, u32 offset, u32 mask, u32 shift, u32 value);

static const struct dev_pm_ops sensor_phy_pm_ops = {
	SET_RUNTIME_PM_OPS(sensor_phy_pm_runtime_suspend,
			   sensor_phy_pm_runtime_resume,
			   NULL)
};

#ifdef CONFIG_OF
static const struct of_device_id of_ids[] = {
	{.compatible = "elvees,mcom03-dw-mipi-dphy"},
	{}
};

MODULE_DEVICE_TABLE(of, of_ids);
#endif

static struct platform_driver local_driver = {
	.probe = sensor_phy_probe,
	.remove = sensor_phy_remove,
	.driver = {.name = "elvees,mcom03-dw-mipi-dphy",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(of_ids),
		   .pm = &sensor_phy_pm_ops,
		   },
};

void sensor_phy_field_set(struct regmap *reg_base, u32 offset, u32 mask, u32 shift, u32 value)
{
	unsigned int regval = 0;

	regmap_read(reg_base, offset, &regval);
	regval &= ~mask;
	regval |= value << shift;
	regmap_write(reg_base, offset, regval);
}

static int sensor_phy_status(struct sensor_phy *phy, struct sensor_phy_status *status)
{
	struct regmap *reg_base = phy->media_regs_base;
	unsigned int regval = 0;
	u32 mipi_offset;
	int ret = 0;

	if (phy->devnum == 0)
		mipi_offset = MIPI0_REGS_OFFSET;
	else
		mipi_offset = MIPI1_REGS_OFFSET;

	regmap_read(reg_base, mipi_offset + MIPI0_RX0_STATUS_OFFSET, &regval);
	status->mipi_status = regval;

	regmap_read(reg_base, mipi_offset + MIPI0_RX0_TEST_DATA_OFFSET, &regval);
	status->cont_data =
	    (regval & MIPI_RX0_TEST_CTRL_CR_CONT_DATA_MASK) >>
	    MIPI_RX0_TEST_CTRL_CR_CONT_DATA_SHIFT;

	return ret;
}

static int sensor_phy_config(struct sensor_phy *phy, struct sensor_phy_params *params)
{
	struct regmap *reg_base = phy->media_regs_base;
	unsigned int shutdown_mask, rstz_mask;
	unsigned int shutdown_shift, rstz_shift;
	u32 mipi_offset;
	u8 mipi_lanes_en = 0, hsfreqrange;
	u8 addr_lsb, addr_msb, wdata;
	int ret = 0;
	int i;

	if (phy->devnum == 0) {
		shutdown_mask = MIPI_RX_RST_CTRL_CR_MIPI0_SHUTDOWNZ_MASK;
		shutdown_shift = MIPI_RX_RST_CTRL_CR_MIPI0_SHUTDOWNZ_SHIFT;
		rstz_mask = MIPI_RX_RST_CTRL_CR_MIPI0_RSTZ_MASK;
		rstz_shift = MIPI_RX_RST_CTRL_CR_MIPI0_RSTZ_SHIFT;
		mipi_offset = MIPI0_REGS_OFFSET;
	} else {
		shutdown_mask = MIPI_RX_RST_CTRL_CR_MIPI1_SHUTDOWNZ_MASK;
		shutdown_shift = MIPI_RX_RST_CTRL_CR_MIPI1_SHUTDOWNZ_SHIFT;
		rstz_mask = MIPI_RX_RST_CTRL_CR_MIPI1_RSTZ_MASK;
		rstz_shift = MIPI_RX_RST_CTRL_CR_MIPI1_RSTZ_SHIFT;
		mipi_offset = MIPI1_REGS_OFFSET;
	}

	switch (params->type) {
	case SENSOR_IF_MIPI:
		/* Set MIPI DPHY0 shutdown to active state */
		sensor_phy_field_set(reg_base, MIPI_RX_RST_CTRL_OFFSET,
				     shutdown_mask, shutdown_shift, 0);

		/* Set MIPI DPHY0 reset to active state */
		sensor_phy_field_set(reg_base, MIPI_RX_RST_CTRL_OFFSET, rstz_mask, rstz_shift, 0);

		/* Set MIPI DPHY0 test registers reset to active state */
		sensor_phy_field_set(reg_base,
				     mipi_offset + MIPI0_RX0_TSTREG_CTRL_OFFSET,
				     MIPI_RX0_TSTREG_CTRL_CR_TESTCLR_MASK,
				     MIPI_RX0_TSTREG_CTRL_CR_TESTCLR_SHIFT, 0x1);
		ndelay(15);

		if (params->enable) {
			/* Set MIPI DPHY0 test registers reset to active state */
			sensor_phy_field_set(reg_base,
					     mipi_offset + MIPI0_RX0_TSTREG_CTRL_OFFSET,
					     MIPI_RX0_TSTREG_CTRL_CR_TESTCLR_MASK,
					     MIPI_RX0_TSTREG_CTRL_CR_TESTCLR_SHIFT, 0x0);

			/* Set clock lane enable (MIPI_RX0_CTRL) */
			sensor_phy_field_set(reg_base,
					     mipi_offset + MIPI0_RX0_CTRL_OFFSET,
					     MIPI_RX0_CTRL_CR_PHY_ENABLECLK_MASK,
					     MIPI_RX0_CTRL_CR_PHY_ENABLECLK_SHIFT, 0x1);

			/* PLL clock enable (MIPI_RX0_TEST_CTRL) */
			sensor_phy_field_set(reg_base,
					     mipi_offset + MIPI0_RX0_TEST_CTRL_OFFSET,
					     MIPI_RX0_TEST_CTRL_CR_PLL_CLK_SEL_MASK,
					     MIPI_RX0_TEST_CTRL_CR_PLL_CLK_SEL_SHIFT, 0x1);

			/* Set clocks frequency (MIPI_RX0_CLK_PARAM: Fcfg_clk = 27 MHz,
			 * Bit rate from params->mipi_hsfreqrange;
			 */
			hsfreqrange = params->mipi_hsfreqrange;
			sensor_phy_field_set(reg_base,
					     mipi_offset + MIPI0_RX0_CLK_PARAM_OFFSET,
					     MIPI_RX0_CLK_PARAM_CR_CFG_CLK_FREQ_RANGE_MASK,
					     MIPI_RX0_CLK_PARAM_CR_CFG_CLK_FREQ_RANGE_SHIFT, 0x28);
			sensor_phy_field_set(reg_base,
					     mipi_offset + MIPI0_RX0_CLK_PARAM_OFFSET,
					     MIPI_RX0_CLK_PARAM_CR_HS_FREQ_RANGE_MASK,
					     MIPI_RX0_CLK_PARAM_CR_HS_FREQ_RANGE_SHIFT,
					     hsfreqrange);

			/* Set data lanes enable (MIPI_RX0_CTRL) */
			mipi_lanes_en = params->enable ? BITMASK(params->mipi_lanes) : 0;
			sensor_phy_field_set(reg_base,
					     mipi_offset + MIPI0_RX0_CTRL_OFFSET,
					     MIPI_RX0_CTRL_CR_PHY_ENABLE_RX_LANE0_MASK,
					     MIPI_RX0_CTRL_CR_PHY_ENABLE_RX_LANE0_SHIFT,
					     mipi_lanes_en);

			/* Configuration using test registers */
			for (i = 0; i < 4; i++) {
				/* This programming sequence is from Synopsys testbench example */
				addr_msb = test_data_seq[i].addr_msb;
				addr_lsb = test_data_seq[i].addr_lsb;
				wdata = test_data_seq[i].wdata;
				if (params->mipi_osc_freq != 0) {
					if (addr_lsb == 0xe2)
						wdata = params->mipi_osc_freq & 0xff;
					if (addr_lsb == 0xe3)
						wdata = (params->mipi_osc_freq >> 8) & 0xff;
				}
				dev_dbg(phy->dev, "Reg address = 0x%02x%02x, wdata = 0x%02x\n",
				       addr_msb, addr_lsb, wdata);

				/* Step 1: set MSB of address   */
				/* MIPI_RX0_TSTREG_DATA: TSTEN=0 (write) */
				sensor_phy_field_set(reg_base,
						     mipi_offset + MIPI0_RX0_TSTREG_DATA_OFFSET,
						     MIPI_RX0_TSTREG_DATA_CR_TESTEN_MASK,
						     MIPI_RX0_TSTREG_DATA_CR_TESTEN_SHIFT, 0x0);

				/* MIPI_RX0_TSTREG_CTRL: TESTCLK=0 */
				sensor_phy_field_set(reg_base,
						     mipi_offset + MIPI0_RX0_TSTREG_CTRL_OFFSET,
						     MIPI_RX0_TSTREG_CTRL_CR_TESTCLK_MASK,
						     MIPI_RX0_TSTREG_CTRL_CR_TESTCLK_SHIFT, 0x0);

				/* MIPI_RX0_TSTREG_DATA: TSTEN=1 (read) */
				sensor_phy_field_set(reg_base,
						     mipi_offset + MIPI0_RX0_TSTREG_DATA_OFFSET,
						     MIPI_RX0_TSTREG_DATA_CR_TESTEN_MASK,
						     MIPI_RX0_TSTREG_DATA_CR_TESTEN_SHIFT, 0x1);

				/* MIPI_RX0_TSTREG_CTRL: TESTCLK=1 */
				sensor_phy_field_set(reg_base,
						     mipi_offset + MIPI0_RX0_TSTREG_CTRL_OFFSET,
						     MIPI_RX0_TSTREG_CTRL_CR_TESTCLK_MASK,
						     MIPI_RX0_TSTREG_CTRL_CR_TESTCLK_SHIFT, 0x1);

				/* MIPI_RX0_TSTREG_DATA: TSTDIN=0 */
				sensor_phy_field_set(reg_base,
						     mipi_offset + MIPI0_RX0_TSTREG_DATA_OFFSET,
						     MIPI_RX0_TSTREG_DATA_CR_TESTDIN_MASK,
						     MIPI_RX0_TSTREG_DATA_CR_TESTDIN_SHIFT, 0x00);

				/* MIPI_RX0_TSTREG_CTRL: TESTCLK=0 */
				sensor_phy_field_set(reg_base,
						     mipi_offset + MIPI0_RX0_TSTREG_CTRL_OFFSET,
						     MIPI_RX0_TSTREG_CTRL_CR_TESTCLK_MASK,
						     MIPI_RX0_TSTREG_CTRL_CR_TESTCLK_SHIFT, 0x0);

				/* MIPI_RX0_TSTREG_DATA: TSTEN=0 (write) */
				sensor_phy_field_set(reg_base,
						     mipi_offset + MIPI0_RX0_TSTREG_DATA_OFFSET,
						     MIPI_RX0_TSTREG_DATA_CR_TESTEN_MASK,
						     MIPI_RX0_TSTREG_DATA_CR_TESTEN_SHIFT, 0x0);

				/* MIPI_RX0_TSTREG_DATA: TSTDIN=addr_msb */
				sensor_phy_field_set(reg_base,
						     mipi_offset + MIPI0_RX0_TSTREG_DATA_OFFSET,
						     MIPI_RX0_TSTREG_DATA_CR_TESTDIN_MASK,
						     MIPI_RX0_TSTREG_DATA_CR_TESTDIN_SHIFT,
						     addr_msb);

				/* MIPI_RX0_TSTREG_CTRL: TESTCLK=1 */
				sensor_phy_field_set(reg_base,
						     mipi_offset + MIPI0_RX0_TSTREG_CTRL_OFFSET,
						     MIPI_RX0_TSTREG_CTRL_CR_TESTCLK_MASK,
						     MIPI_RX0_TSTREG_CTRL_CR_TESTCLK_SHIFT, 0x1);

				/* Step 2: set LSB of address */
				/* MIPI_RX0_TSTREG_CTRL: TESTCLK=0 */
				sensor_phy_field_set(reg_base,
						     mipi_offset + MIPI0_RX0_TSTREG_CTRL_OFFSET,
						     MIPI_RX0_TSTREG_CTRL_CR_TESTCLK_MASK,
						     MIPI_RX0_TSTREG_CTRL_CR_TESTCLK_SHIFT, 0x0);

				/* MIPI_RX0_TSTREG_DATA: TSTEN=1 (read) */
				sensor_phy_field_set(reg_base,
						     mipi_offset + MIPI0_RX0_TSTREG_DATA_OFFSET,
						     MIPI_RX0_TSTREG_DATA_CR_TESTEN_MASK,
						     MIPI_RX0_TSTREG_DATA_CR_TESTEN_SHIFT, 0x1);

				/* MIPI_RX0_TSTREG_CTRL: TESTCLK=1 */
				sensor_phy_field_set(reg_base,
						     mipi_offset + MIPI0_RX0_TSTREG_CTRL_OFFSET,
						     MIPI_RX0_TSTREG_CTRL_CR_TESTCLK_MASK,
						     MIPI_RX0_TSTREG_CTRL_CR_TESTCLK_SHIFT, 0x1);

				/* MIPI_RX0_TSTREG_DATA: TSTDIN=addr_lsb */
				sensor_phy_field_set(reg_base,
						     mipi_offset + MIPI0_RX0_TSTREG_DATA_OFFSET,
						     MIPI_RX0_TSTREG_DATA_CR_TESTDIN_MASK,
						     MIPI_RX0_TSTREG_DATA_CR_TESTDIN_SHIFT,
						     addr_lsb);

				/* MIPI_RX0_TSTREG_CTRL: TESTCLK=0 */
				sensor_phy_field_set(reg_base,
						     mipi_offset + MIPI0_RX0_TSTREG_CTRL_OFFSET,
						     MIPI_RX0_TSTREG_CTRL_CR_TESTCLK_MASK,
						     MIPI_RX0_TSTREG_CTRL_CR_TESTCLK_SHIFT, 0x0);

				/* MIPI_RX0_TSTREG_DATA: TSTEN=0 (write) */
				sensor_phy_field_set(reg_base,
						     mipi_offset + MIPI0_RX0_TSTREG_DATA_OFFSET,
						     MIPI_RX0_TSTREG_DATA_CR_TESTEN_MASK,
						     MIPI_RX0_TSTREG_DATA_CR_TESTEN_SHIFT, 0x0);

				/* Step 3: write data */
				/* MIPI_RX0_TSTREG_DATA: TSTDIN = wdata */
				sensor_phy_field_set(reg_base,
						     mipi_offset + MIPI0_RX0_TSTREG_DATA_OFFSET,
						     MIPI_RX0_TSTREG_DATA_CR_TESTDIN_MASK,
						     MIPI_RX0_TSTREG_DATA_CR_TESTDIN_SHIFT, wdata);

				/* MIPI_RX0_TSTREG_CTRL: TESTCLK=1 */
				sensor_phy_field_set(reg_base,
						     mipi_offset + MIPI0_RX0_TSTREG_CTRL_OFFSET,
						     MIPI_RX0_TSTREG_CTRL_CR_TESTCLK_MASK,
						     MIPI_RX0_TSTREG_CTRL_CR_TESTCLK_SHIFT, 0x1);

				/* MIPI_RX0_TSTREG_CTRL: TESTCLK=0 */
				sensor_phy_field_set(reg_base,
						     mipi_offset + MIPI0_RX0_TSTREG_CTRL_OFFSET,
						     MIPI_RX0_TSTREG_CTRL_CR_TESTCLK_MASK,
						     MIPI_RX0_TSTREG_CTRL_CR_TESTCLK_SHIFT, 0x0);
			}

			ndelay(15);

			/* Set MIPI DPHY0 shutdown to inactive state */
			sensor_phy_field_set(reg_base, MIPI_RX_RST_CTRL_OFFSET,
					     shutdown_mask, shutdown_shift, 0x1);

			ndelay(5);

			/* Set MIPI DPHY0 reset to inactive state */
			sensor_phy_field_set(reg_base, MIPI_RX_RST_CTRL_OFFSET,
					     rstz_mask, rstz_shift, 0x1);
		}

		break;

	default:
		dev_err(phy->dev, "Unknown sensor type\n");
		ret = -EFAULT;
	}

	if (ret == 0) {
		if (params->enable)
			phy->enabled_phy |= params->type;
		else
			phy->enabled_phy &= ~params->type;
	}

	return ret;
}

static long sensor_phy_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct sensor_phy *phy = (struct sensor_phy *)filp->private_data;
	union {
		struct sensor_phy_params params;
		struct sensor_phy_status status;
	} args;
	int ret = 0;

	switch (ioctl) {
	case IOC_SENSOR_PHY_CONFIG:
		if (copy_from_user(&args.params, argp, sizeof(args.params)))
			return -EFAULT;
		ret = sensor_phy_config(phy, &args.params);
		break;
	case IOC_SENSOR_PHY_STATUS:
		ret = sensor_phy_status(phy, &args.status);
		if (copy_to_user(argp, &args.status, sizeof(args.status)))
			return -EFAULT;
		break;
	default:
		dev_err(phy->dev, "Unknown ioctl value %d\n", ioctl);
		ret = -ENOTTY;
	}

	return ret;
}

static int sensor_phy_open(struct inode *inode, struct file *filp)
{
	struct sensor_phy *phy;

	phy = container_of(inode->i_cdev, struct sensor_phy, cdev);
	if (!phy) {
		pr_err("%s: Failed to get pointer to phy structure\n", DEV_STR);
		return -EINVAL;
	}

	filp->private_data = (void *)phy;

	pm_runtime_get_sync(phy->dev);

	return 0;
}

static int sensor_phy_release(struct inode *inode, struct file *filp)
{
	struct sensor_phy *phy;

	phy = container_of(inode->i_cdev, struct sensor_phy, cdev);
	if (!phy) {
		pr_err("%s: Failed to get pointer to phy structure\n", DEV_STR);
		return -EINVAL;
	}

	pm_runtime_put(phy->dev);

	return 0;
}

static struct file_operations sensor_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = sensor_phy_ioctl,
	.open = sensor_phy_open,
	.release = sensor_phy_release,
};
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
static void sensor_phy_remove(struct platform_device *ofdev)
#else
static int sensor_phy_remove(struct platform_device *ofdev)
#endif
{
	struct sensor_phy *sensor_dev = platform_get_drvdata(ofdev);

	device_destroy(sensor_phy_class, MKDEV(sensor_dev->sensor_major, 0));
	unregister_chrdev_region(sensor_dev->dev_str, 1);

	pm_runtime_disable(sensor_dev->dev);

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 13, 0)
	return 0;
#endif
}

static int sensor_phy_probe(struct platform_device *ofdev)
{
	int ret;
	struct device *dev = &ofdev->dev;
	struct sensor_phy *sensor_dev;
	char dev_name[20];
	char clock_name[20];
	int port;

	/* Clock names encode the physical receiver. Probe order is not an ID:
	 * CSI1 may be the only enabled receiver (NMS-SM-EVM X12).
	 */
	if (of_property_match_string(dev->of_node, "clock-names",
				     "mipi_rx0_cfg_clk") >= 0)
		port = 0;
	else if (of_property_match_string(dev->of_node, "clock-names",
					  "mipi_rx1_cfg_clk") >= 0)
		port = 1;
	else
		return dev_err_probe(dev, -EINVAL, "Missing MIPI RX clock name\n");

	sensor_dev = devm_kzalloc(&ofdev->dev, sizeof(*sensor_dev), GFP_KERNEL);
	if (!sensor_dev)
		return -ENOMEM;

	sensor_dev->media_regs_base = syscon_regmap_lookup_by_phandle(dev->of_node, "elvees,urb");

	if (IS_ERR(sensor_dev->media_regs_base)) {
		dev_err(dev, "Failed to get media registers base\n");
		return PTR_ERR(sensor_dev->media_regs_base);
	}

	sprintf(clock_name, "%s%d%s", "mipi_rx", port, "_cfg_clk");
	sensor_dev->mipi_rx_cfg_clk = devm_clk_get(&ofdev->dev, clock_name);
	if (IS_ERR(sensor_dev->mipi_rx_cfg_clk))
		return PTR_ERR(sensor_dev->mipi_rx_cfg_clk);

	sprintf(clock_name, "%s%d%s", "cmos", port, "_clk");
	sensor_dev->cmos_clk = devm_clk_get(&ofdev->dev, clock_name);
	if (IS_ERR(sensor_dev->cmos_clk))
		dev_warn(dev, "Warning: no cmos_clk for this sensor_phy\n");

	device_detected = true;

	sprintf(dev_name, "%s%d", DEV_STR, port);

	ret = alloc_chrdev_region(&sensor_dev->dev_str, 0, 1, dev_name);
	sensor_dev->sensor_major = MAJOR(sensor_dev->dev_str);
	dev_dbg(dev, "Chrdev ret=%d, MAJ=%d\n", ret, sensor_dev->sensor_major);
	if (ret < 0) {
		dev_err(dev, "Failed to alloc chrdev region\n");
		return ret;
	}
	dev_dbg(dev, "alloc_chrdev_region ret %d, assigned MAJ=%d",
	        ret, sensor_dev->sensor_major);

	cdev_init(&sensor_dev->cdev, &sensor_fops);
	sensor_dev->cdev.owner = THIS_MODULE;
	sensor_dev->cdev.ops = &sensor_fops;
	ret = cdev_add(&sensor_dev->cdev, sensor_dev->dev_str, 1);

	dev = device_create(sensor_phy_class, NULL, MKDEV(sensor_dev->sensor_major, 0), NULL,
				dev_name);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		dev_err(dev, "Failed to create device node\n");
		goto out_err_free_chrdev_region;
	}

	if (ret) {
		dev_err(dev, "Failed to register cdev, MAJ%d, ret%d\n",
		       sensor_dev->sensor_major, ret);
		goto out_err_free_chrdev_region;
	} else {
		dev_info(dev, "Successfully registered cdev MAJ %d, MIN 0\n",
		       sensor_dev->sensor_major);
	}

	platform_set_drvdata(ofdev, sensor_dev);
	sensor_dev->dev = &ofdev->dev;

	sensor_dev->devnum = port;

	pm_runtime_enable(sensor_dev->dev);
	pm_runtime_get_sync(sensor_dev->dev);

	pm_runtime_put(sensor_dev->dev);

	return 0;

out_err_free_chrdev_region:
	unregister_chrdev_region(sensor_dev->dev_str, 1);
	return ret;
}

static int sensor_phy_register(void)
{
	int ret;

	if (driver_registered) {
		pr_err("%s: sensor device already detected\n", DEV_STR);
		return -EINVAL;
	}

	driver_registered = true;

	ret = platform_driver_register(&local_driver);
	if (ret != 0) {
		ret = -ENODEV;
		pr_err("%s: platform_driver_register failed\n", DEV_STR);
		goto failure_register;
	}

	if (device_detected != true) {
		pr_err("%s: device_detect failed\n", DEV_STR);
		ret = -ENODEV;
		goto failure_detect;
	}

	return 0;

failure_detect:
	platform_driver_unregister(&local_driver);
failure_register:
	driver_registered = false;
	return ret;
}

static int sensor_phy_deregister(void)
{
	pr_debug("%s: unregister driver\n", DEV_STR);
	if (driver_registered)
		platform_driver_unregister(&local_driver);
	driver_registered = false;

	return 0;
}

static int sensor_phy_pm_runtime_suspend(struct device *dev)
{
	struct sensor_phy *sensor_dev = dev_get_drvdata(dev);

	if (!IS_ERR(sensor_dev->cmos_clk))
		clk_disable_unprepare(sensor_dev->cmos_clk);

	if (!IS_ERR(sensor_dev->mipi_rx_cfg_clk))
		clk_disable_unprepare(sensor_dev->mipi_rx_cfg_clk);

	return 0;
}

static int sensor_phy_pm_runtime_resume(struct device *dev)
{
	struct sensor_phy *sensor_dev = dev_get_drvdata(dev);
	int ret = 0;

	if (!IS_ERR(sensor_dev->mipi_rx_cfg_clk)) {
		ret = clk_prepare_enable(sensor_dev->mipi_rx_cfg_clk);
		if (ret) {
			dev_err(sensor_dev->dev, "Failed to enable cfg_clk clock\n");
			goto end_resume;
		}
	}

	if (!IS_ERR(sensor_dev->cmos_clk)) {
		ret = clk_prepare_enable(sensor_dev->cmos_clk);
		if (ret) {
			dev_err(sensor_dev->dev, "Failed to enable cmos_clk clock\n");
			goto end_resume;
		}
	}

end_resume:
	return ret;
}

static int __init sensor_phy_init(void)
{
	int ret = 0;
	pr_info("%s: driver initialization\n", DEV_STR);

	sensor_phy_class = class_create("elvees,mcom03-dw-mipi-dphy");

	if (IS_ERR(sensor_phy_class))
		return PTR_ERR(sensor_phy_class);

	ret = sensor_phy_register();
	if (ret) {
		pr_err("%s: failed to register the device driver!\n", DEV_STR);
		class_destroy(sensor_phy_class);
		return ret;
	}

	return ret;
}

static void __exit sensor_phy_exit(void)
{
	sensor_phy_deregister();
	class_destroy(sensor_phy_class);
}

MODULE_AUTHOR("Patrick Conway <paddy.conway@imgtec.com>");
MODULE_LICENSE("GPL");

module_init(sensor_phy_init);
module_exit(sensor_phy_exit);
