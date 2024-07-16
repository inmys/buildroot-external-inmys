/*
 * An I2C driver for Beilin BL5372 RTC
 */

#include <linux/i2c.h>
#include <linux/bcd.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of.h>

#define DEG 0

#define DRV_VERSION "0.0.1"

#define TIME24 1
#define RS5C_ADDR(R)		(((R) << 4) | 0)
#define RS5C372_REG_SECS	0
#define RS5C372_REG_MINS	1
#define RS5C372_REG_HOURS	2
#define RS5C372_REG_WDAY	3
#define RS5C372_REG_DAY		4
#define RS5C372_REG_MONTH	5
#define RS5C372_REG_YEAR	6
#define RS5C372_REG_TRIM	7
#define RS5C_REG_ALARM_A_MIN	8			/* or ALARM_W */
#define RS5C_REG_ALARM_A_HOURS	9
#define RS5C_REG_ALARM_A_WDAY	10

#define RS5C_REG_ALARM_B_MIN	11			/* or ALARM_D */
#define RS5C_REG_ALARM_B_HOURS	12
#define RS5C_REG_ALARM_B_WDAY	13			/* (ALARM_B only) */
#define RS5C_REG_CTRL1		14
#define RS5C_REG_CTRL2		15
#define DEVICE_ADDR        0x32	//0x5d



#if 0
//11 ---> 0x11
static unsigned char bin2bcd(unsigned  val)
{
	return ((val / 10) << 4) + val % 10;
}
//0x11---> 11
static unsigned bcd2bin(unsigned char val)
{
	return (val & 0x0f) + (val >> 4) * 10;
}
#endif

static unsigned rs5c_reg2hr(unsigned reg)
{
#if TIME24
	return bcd2bin(reg & 0x3f);
#else
	unsigned	hour;
	hour = bcd2bin(reg & 0x1f);
	if (hour == 12)
		hour = 0;
	if (reg & 0x20)
		hour += 12;
	return hour;
#endif
}

static unsigned rs5c_hr2reg(unsigned hour)
{

#if TIME24
	return bin2bcd(hour);

#else
	if (hour > 12)
		return 0x20 | bin2bcd(hour - 12);
	if (hour == 12)
		return 0x20 | bin2bcd(12);
	if (hour == 0)
		return bin2bcd(12);
	return bin2bcd(hour);
#endif
}

//-----------------------------------------------
static struct i2c_driver bl5372_driver;

struct bl5372 {
	struct rtc_device *rtc;
	struct device *dev;
	int irq;
 	/*
	unsigned char sec;
	unsigned char min;
	unsigned char hour;
	unsigned char week;
	unsigned char day;
	unsigned char month;
	unsigned int year;
	*/
};


#define UNUSED(x) (void)(x)

static int bl5372_get_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	struct bl5372 *bl5372 = i2c_get_clientdata(client);
	unsigned char buf[7] = { RS5C_ADDR(RS5C372_REG_SECS) };
	struct i2c_msg msgs[] = {
		{/* setup read ptr */
			.addr = client->addr,
			.flags = 0,/* write */
			.len = 1,
			.buf = buf
		},
		{/* read the sec,min,hour,week,day,month,year */
			.addr = client->addr,
			.flags = I2C_M_RD,/* read */
			.len = 7,
			.buf = buf
		},
	};


	struct i2c_msg msgs2[] = {
		{/* setup read  */
			.addr = client->addr,
			.len = 1,
			.buf = buf
		},
		{/* read is_24hour */
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = buf
		},
	};

	UNUSED(bl5372);

	//int __i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
	//@num: Number of messages to be executed.
	//There are two messages here, the size of msgs[]
	/* read registers */
	if ((i2c_transfer(client->adapter, msgs, 2)) != 2) {
		dev_err(&client->dev, "%s: read error\n", __func__);
		return -EIO;
	}


	tm->tm_sec = bcd2bin(buf[0] & 0x7f);
	tm->tm_min = bcd2bin(buf[1] & 0x7f);
	tm->tm_hour = rs5c_reg2hr(buf[2]);
	tm->tm_mday = bcd2bin(buf[4] & 0x7f);;
	tm->tm_wday = bcd2bin(buf[3] & 0x7f);
	tm->tm_mon = rs5c_reg2hr(buf[5])-1;
	tm->tm_year = bcd2bin(buf[6] & 0x7f)+100;

	//------------------------------------
	buf[0]= RS5C_ADDR(RS5C_REG_CTRL2);

	/* read registers */
	if ((i2c_transfer(client->adapter, msgs2, 2)) != 2) {
		dev_err(&client->dev, "%s: read error\n", __func__);
		return -EIO;
	}

	if(buf[0]&0x20)
	{
		
		tm->tm_hour= (tm->tm_hour<24)? (tm->tm_hour):(24-tm->tm_hour);
	}
	else
	{
		tm->tm_hour=(tm->tm_hour<24-8)? (tm->tm_hour+8):(tm->tm_hour+8-24);
	}


	/* the clock can give out invalid datetime, but we cannot return
	 * -EINVAL otherwise hwclock will refuse to set the time on bootup.
	 */
	if (rtc_valid_tm(tm) < 0)
		dev_err(&client->dev, "retrieved date/time is not valid.\n");

	return 0;
}

static int bl5372_set_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	struct bl5372 *bl5372 = i2c_get_clientdata(client);
	int err;
	unsigned char buf[7];

//------------------------------------
	struct i2c_msg msgs2[] = {
		{/* setup read  */
			.addr = client->addr,
			.len = 1,
			.buf = buf
		},
		{/* read is_24hour */
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = buf
		},
	};


	buf[0]= RS5C_ADDR(RS5C_REG_CTRL2);

	UNUSED(bl5372);



	/* read registers */
	if ((i2c_transfer(client->adapter, msgs2, 2)) != 2) {
		dev_err(&client->dev, "%s: read error\n", __func__);
		return -EIO;
	}


	if((buf[0]&0x20)== 0)
	{

		buf[0] |= (1<<5); 
		err = i2c_master_send(client, buf, 1);
	}
//------------------------
	/* hours, minutes and seconds */
	buf[0] = bin2bcd(tm->tm_sec);
	buf[1] = bin2bcd(tm->tm_min);
	buf[2] = rs5c_hr2reg(tm->tm_hour);
	buf[3] = bin2bcd(tm->tm_wday & 0x07); //week 0~6
	buf[4] = bin2bcd(tm->tm_mday);
	buf[5] = bin2bcd(tm->tm_mon)+1;// 0~11
	tm->tm_year -= 100;
	buf[6] = bin2bcd(tm->tm_year % 100);// start at 1900  2018=>118


  err = i2c_smbus_write_byte_data(client, RS5C_ADDR(RS5C372_REG_SECS),   buf[0]);
 	i2c_smbus_write_byte_data(client, RS5C_ADDR(RS5C372_REG_MINS) ,  buf[1]);
	i2c_smbus_write_byte_data(client, RS5C_ADDR(RS5C372_REG_HOURS) , buf[2]);
	i2c_smbus_write_byte_data(client, RS5C_ADDR(RS5C372_REG_WDAY) ,  buf[3]);
	i2c_smbus_write_byte_data(client, RS5C_ADDR(RS5C372_REG_DAY) ,   buf[4]);
	i2c_smbus_write_byte_data(client, RS5C_ADDR(RS5C372_REG_MONTH) , buf[5]);
	i2c_smbus_write_byte_data(client, RS5C_ADDR(RS5C372_REG_YEAR) ,  buf[6]);

	return 0;
}

#ifdef CONFIG_RTC_INTF_DEV
static int bl5372_rtc_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	struct bl5372 *bl5372 = i2c_get_clientdata(to_i2c_client(dev));
	struct rtc_time tm;
	UNUSED(bl5372);

	switch (cmd) {
	case RTC_RD_TIME:
		//bl5372_get_datetime(to_i2c_client(dev), &tm);
		return 0;
	case RTC_SET_TIME:
		if (copy_from_user(&tm, (const void *)arg, sizeof(tm)))
                        return -EFAULT;

		bl5372_set_datetime(to_i2c_client(dev), &tm);
		return 0;
	default:
		return -ENOIOCTLCMD;
	}
 
}
#else
#define bl5372_rtc_ioctl NULL
#endif

static int bl5372_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	return bl5372_get_datetime(to_i2c_client(dev), tm);
}

static int bl5372_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	return bl5372_set_datetime(to_i2c_client(dev), tm);
}

static int bl5372_rtc_getalarm(struct device *dev, struct rtc_wkalrm *wkalrm)
{
	struct bl5372 *bl5372 = i2c_get_clientdata(to_i2c_client(dev));
	UNUSED(bl5372);
	return 0;
}

static int bl5372_rtc_setalarm(struct device *dev, struct rtc_wkalrm *wkalrm)
{
	struct bl5372 *bl5372 = i2c_get_clientdata(to_i2c_client(dev));
	UNUSED(bl5372);
	return 0;
}

static int bl5372_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
        //struct bl5372 *bl5372 = dev_get_drvdata(dev);
	struct bl5372 *bl5372 = i2c_get_clientdata(to_i2c_client(dev));
	UNUSED(bl5372);
	return 0;
}

static const struct rtc_class_ops bl5372_rtc_ops = {
	.ioctl		= bl5372_rtc_ioctl,
	.read_time	= bl5372_rtc_read_time,
	.set_time	= bl5372_rtc_set_time,
	.read_alarm             = bl5372_rtc_getalarm,
        .set_alarm              = bl5372_rtc_setalarm,
        .alarm_irq_enable       = bl5372_rtc_alarm_irq_enable
};

static int bl5372_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{

	struct bl5372 *bl5372;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
	{
		return -ENODEV;
	}
	bl5372 = devm_kzalloc(&client->dev, sizeof(struct bl5372),
				GFP_KERNEL);
	if (!bl5372)
	{
		return -ENOMEM;
	}
	device_init_wakeup(&client->dev, 1);

	i2c_set_clientdata(client, bl5372);

	bl5372->rtc = devm_rtc_device_register(&client->dev,
				bl5372_driver.driver.name,
				&bl5372_rtc_ops, THIS_MODULE);

	if (IS_ERR(bl5372->rtc))
	{

		return PTR_ERR(bl5372->rtc);
	}

	return 0;
}

static int bl5372_remove(struct i2c_client *client)
{

	return 0;
}

static const struct i2c_device_id bl5372_id[] = {
	{ "bl5372", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bl5372_id);

#ifdef CONFIG_OF
static const struct of_device_id bl5372_of_match[] = {
	{ .compatible = "beilin,bl5372" },
	{}
};
MODULE_DEVICE_TABLE(of, bl5372_of_match);
#endif

static struct i2c_driver bl5372_driver = {
	.driver		= {
		.name	= "rtc-bl5372",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(bl5372_of_match),
	},
	.probe		= bl5372_probe,
	.remove		= bl5372_remove,
	.id_table	= bl5372_id,
};

module_i2c_driver(bl5372_driver);

MODULE_AUTHOR("Zhengweiqing <1548889230@qq.com>");
MODULE_DESCRIPTION("Beilin BL5372 RTC driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

