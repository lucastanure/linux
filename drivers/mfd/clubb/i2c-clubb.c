/*
 * Clubb GPIO and I2C master mode driver
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/i2c.h>
#include <linux/usb.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/platform_device.h>
#include <linux/mfd/clubb/clubb.h>

struct clubb_i2c {
	struct clubb *clubb;
	struct i2c_adapter adapter;
	uint16_t i2c_addr;
};

static inline int clubb_i2c_addr(struct clubb *clubb, uint8_t i2c_addr)
{
	return clubb_control_msg(clubb, I2C_ADDR, USB_DIR_OUT | USB_TYPE_VENDOR , i2c_addr, 0, NULL, 0);
}

static inline int clubb_i2c_read(struct clubb *clubb, uint16_t high_addr, uint16_t low_addr, void *data, uint16_t len)
{
	return clubb_control_msg(clubb, I2C_READ, USB_DIR_IN | USB_TYPE_VENDOR , high_addr, low_addr, data, len);
}

static inline int clubb_i2c_write(struct clubb *clubb, uint16_t high_addr, uint16_t low_addr, void *data, uint16_t len)
{
	return clubb_control_msg(clubb, I2C_WRITE, USB_DIR_OUT | USB_TYPE_VENDOR , high_addr, low_addr, data, len);
}

static int clubb_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[], int num)
{
	struct clubb_i2c *i2c_dev = i2c_get_adapdata(adap);
	uint16_t high_addr, low_addr;
	int ret;

	if (num == 2){
		if ( (!(msgs[0].flags & I2C_M_RD)) && (msgs[1].flags & I2C_M_RD) ){
			high_addr = ((uint16_t)(msgs[0].buf[0] << 8 )) | msgs[0].buf[1];
			low_addr  = ((uint16_t)(msgs[0].buf[2] << 8 )) | msgs[0].buf[3];
			if (i2c_dev->i2c_addr != msgs[0].addr) {
				clubb_i2c_addr(i2c_dev->clubb, msgs[0].addr << 1);
				i2c_dev->i2c_addr = msgs[0].addr;
			}
			ret = clubb_i2c_read(i2c_dev->clubb, high_addr, low_addr, msgs[1].buf, msgs[1].len);
			if (ret != msgs[1].len)
				return -EIO;
			return num;
		}
	} else {
		if (num == 1 && (!(msgs[0].flags & I2C_M_RD)) ) {
			high_addr = ((uint16_t)(msgs[0].buf[0] << 8 )) | msgs[0].buf[1];
			low_addr  = ((uint16_t)(msgs[0].buf[2] << 8 )) | msgs[0].buf[3];
			if (i2c_dev->i2c_addr != msgs[0].addr) {
				clubb_i2c_addr(i2c_dev->clubb, msgs[0].addr << 1);
				i2c_dev->i2c_addr = msgs[0].addr;
			}
			ret = clubb_i2c_write(i2c_dev->clubb, high_addr, low_addr, msgs[0].buf, msgs[0].len);
			if (ret != msgs[1].len)
				return -EIO;
			return num;

		}
	}

	pr_err("BAD I2C format for clubb");
	return -EIO;
}

static u32 clubb_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm clubb_i2c_algo = {
	.master_xfer	= clubb_i2c_xfer,
	.functionality	= clubb_i2c_func,
};

static int clubb_i2c_probe(struct platform_device *pdev)
{
	struct clubb *clubb;
	struct clubb_i2c *i2c_dev;
	struct i2c_adapter *adap;
	struct device_node *np;
	int ret;

	clubb = dev_get_drvdata(pdev->dev.parent);
	if (!clubb) {
		pr_info("fuck!!");
		return -EPROBE_DEFER;
	}

	i2c_dev = devm_kzalloc(&pdev->dev, sizeof(*i2c_dev), GFP_KERNEL);
	if (!i2c_dev)
		return -ENOMEM;
	adap = &i2c_dev->adapter;
	i2c_dev->clubb = clubb;

	adap->class = I2C_CLASS_DEPRECATED;
	strlcpy(adap->name, "Clubb I2C", sizeof(adap->name));
	adap->algo = &clubb_i2c_algo;
	adap->dev.parent = &pdev->dev;

	i2c_set_adapdata(adap, i2c_dev);

	np = of_find_compatible_node(NULL, NULL, "cirrus,clubb-i2c");
	if (np)
		adap->dev.of_node = np;
	of_node_put(np);

	ret = i2c_add_adapter(adap);
	if (ret)
		pr_err("Failed to i2c_add_adapter\n");

	return ret;
}

static int clubb_i2c_remove(struct platform_device *pdev)
{
	struct clubb_i2c *i2c_dev = platform_get_drvdata(pdev);

	i2c_del_adapter(&i2c_dev->adapter);

	return 0;
}

static struct platform_driver clubb_i2c_driver = {
	.probe		= clubb_i2c_probe,
	.remove		= clubb_i2c_remove,
	.driver		= {
		.name	= "clubb-i2c",
	},
};
module_platform_driver(clubb_i2c_driver);

MODULE_AUTHOR("Lucas Tanure <tanureal@opensource.cirrus.com>");
MODULE_DESCRIPTION("Driver for Cirrus I2C and GPIO USB Bridge");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:clubb-i2c");
