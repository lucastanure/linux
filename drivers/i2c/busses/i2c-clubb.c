/*
 * Clubb I2C master mode driver
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
#include <linux/slab.h>


struct clubb_i2c_dev {
	struct device *dev;
	struct i2c_adapter adapter;
};

static int clubb_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[], int num)
{
	return -EIO;
}

static u32 clubb_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm blubb_i2c_algo = {
	.master_xfer	= clubb_i2c_xfer,
	.functionality	= clubb_i2c_func,
};


static int clubb_i2c_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct clubb_i2c_dev *i2c_dev;
	struct i2c_adapter *adap;
	struct device_node *np;

	i2c_dev = devm_kzalloc(&udev->dev, sizeof(*i2c_dev), GFP_KERNEL);
	if (!i2c_dev)
		return -ENOMEM;
	dev_set_drvdata(&udev->dev, i2c_dev);
	i2c_dev->dev = &udev->dev;
	adap = &i2c_dev->adapter;

	i2c_set_adapdata(adap, i2c_dev);
	adap->class = I2C_CLASS_DEPRECATED;
	strlcpy(adap->name, "Clubb I2C adapter", sizeof(adap->name));
	adap->algo = &blubb_i2c_algo;
	adap->dev.parent = &udev->dev;


	np = of_find_compatible_node(NULL, NULL, "cirrus,clubb-i2c");
	if (np) {
		udev->dev.of_node = np;
		adap->dev.of_node = np;
	}

	return i2c_add_adapter(adap);
}

static void clubb_i2c_disconnect(struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct clubb_i2c_dev *i2c_dev = dev_get_drvdata(&udev->dev);

	dev_dbg(i2c_dev->dev, "%s\n", __func__);
	i2c_del_adapter(&i2c_dev->adapter);

}

static const struct usb_device_id clubb_i2c_id_table[] = {
	{ USB_DEVICE(0x04b4, 0x00f0) },
	{}
};
MODULE_DEVICE_TABLE(usb, clubb_i2c_id_table);

static struct usb_driver clubb_driver = {
	.name		= "Clubb_I2C",
	.probe		= clubb_i2c_probe,
	.disconnect	= clubb_i2c_disconnect,
	.id_table	= clubb_i2c_id_table,
};

module_usb_driver(clubb_driver);

MODULE_AUTHOR("Lucas Tanure <tanureal@opensource.cirrus.com>");
MODULE_DESCRIPTION("Driver for Cirrus USB Bus Bridge");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:i2c-clubb");
