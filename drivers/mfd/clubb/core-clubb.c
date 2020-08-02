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
#define DEBUG
#include <linux/of.h>
#include <linux/usb.h>
#include <linux/module.h>
#include <linux/mfd/core.h>
#include <linux/mfd/clubb/clubb.h>

static const struct mfd_cell clubb_devs[] = {
	{ .name = "clubb-i2s"},
	{ .name = "clubb-spi"},
	{ .name = "clubb-i2c", },
	{ .name = "clubb-gpio", },
};


int clubb_control_msg(struct clubb *clubb, __u8 request, __u8 requesttype, __u16 value, __u16 index, void *data, __u16 size)
{
	int ret = 0;
	mutex_lock(&clubb->mutex_ep0);
	ret = usb_control_msg(clubb->udev, usb_sndctrlpipe(clubb->udev, 0), request, requesttype, value, index, data, size, 1000);
	if (ret < 0)
		pr_err("clubb_control_msg %x %d", request, ret);
	mutex_unlock(&clubb->mutex_ep0);
	return ret;
}
EXPORT_SYMBOL_GPL(clubb_control_msg);

int clubb_control_msg2(struct clubb *clubb, __u8 request, __u8 requesttype, __u16 value, __u16 index, void *data, __u16 size)
{
	int ret = 0;
	ret = usb_control_msg(clubb->udev, usb_sndctrlpipe(clubb->udev, 0), request, requesttype, value, index, data, size, 1000);
	if (ret < 0)
		pr_err("clubb_control_msg %x %d", request, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(clubb_control_msg2);

static int clubb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct device_node *np;
	struct clubb *clubb;
	int ret;

	clubb = devm_kzalloc(&udev->dev, sizeof(struct clubb), GFP_KERNEL);
	if (!clubb)
		return -ENOMEM;

	mutex_init(&clubb->mutex_ep0);

	clubb->udev = udev;

	udev->dev.init_name = "Clubb Core";

	dev_set_drvdata(&udev->dev, clubb);

	np = of_find_compatible_node(NULL, NULL, "cirrus,clubb");
	if (np)
		udev->dev.of_node = np;
	of_node_put(np);

	ret = mfd_add_devices(&udev->dev, PLATFORM_DEVID_AUTO, clubb_devs, ARRAY_SIZE(clubb_devs), NULL, 0, NULL);
	if (ret)
		dev_err(&udev->dev, "Failed to add subdevices: %d\n", ret);
	return 0;
}

static void clubb_disconnect(struct usb_interface *intf)
{
	pr_err("CLUBB DISCONNECTED!");
}

static const struct usb_device_id clubb_id_table[] = {
	{ USB_DEVICE(0x04b4, 0x00f0) },
	{}
};
MODULE_DEVICE_TABLE(usb, clubb_id_table);

static struct usb_driver clubb_driver = {
	.name		= "Clubb_Core",
	.probe		= clubb_probe,
	.disconnect	= clubb_disconnect,
	.id_table	= clubb_id_table,
};

module_usb_driver(clubb_driver);

MODULE_AUTHOR("Lucas Tanure <tanureal@opensource.cirrus.com>");
MODULE_DESCRIPTION("Driver for Cirrus USB Bus Bridge");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:clubb-core");
