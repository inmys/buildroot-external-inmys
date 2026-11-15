#ifndef _SENSOR_PHY_H_
#define _SENSOR_PHY_H_

#include <linux/ioctl.h>
#include <linux/types.h>

typedef enum {
	SENSOR_IF_HISPI = 1,
	SENSOR_IF_LVDS = 2,
	SENSOR_IF_GPIO = 4,
	SENSOR_IF_MIPI = 8,
} sensor_if_type;

struct sensor_phy_params {
	sensor_if_type type;
	__u8 enable;
	__u8 clk_en;
	__u8 pixel_res;
	__u8 mipi_lanes;
	__u8 mipi_hsfreqrange;
	__u16 mipi_osc_freq;
	__u8 num_phys;
	__u8 alt_mode;
	__u8 filler_en;
	__u8 lsb_first;
	__u32 frame_width;
	__u32 frame_height;
	__u32 clk_freq;
	__u8 reset_gpio;
	__u8 enable_mux;
};

struct sensor_phy_status {
	__u32 mipi_status;
	__u16 cont_data;
};

#define DEV_STR "sensor_phy"

#define IOC_SENSOR_PHY_CONFIG		_IOR(0xf0, 0, struct sensor_phy_params)
#define IOC_SENSOR_PHY_CLK_CONFIG	_IOR(0xf0, 1, struct sensor_phy_params)
#define IOC_SENSOR_PHY_STATUS		_IOW(0xf0, 2, struct sensor_phy_status)
#define IOC_SENSOR_PHY_RESET_GPIO	_IOR(0xf0, 3, struct sensor_phy_params)
#define IOC_SENSOR_PHY_MUX_CONFIG	_IOR(0xf0, 4, struct sensor_phy_params)

#endif // _SENSOR_PHY_H_
