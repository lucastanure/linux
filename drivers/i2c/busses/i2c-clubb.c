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
#include <linux/debugfs.h>

/* USB vendor request to write to I2C EEPROM connected. The EEPROM page size is
 * fixed to 64 bytes. The I2C EEPROM address is provided in the value field. The
 * memory address to start writing is provided in the index field of the request.
 * The maximum allowed request length is 4KB. */
#define I2C_WRITE              (0xBA)

/* USB vendor request to read from I2C EEPROM connected. The EEPROM page size is
 * fixed to 64 bytes. The I2C EEPROM address is provided in the value field. The
 * memory address to start reading from is provided in the index field of the
 * request. The maximum allowed request length is 4KB. */
#define I2C_READ               (0xBB)

#define I2C_ADDR		(0xB0)
#define   IRQ_READ             (0xAA)

#define PRINCE_LFT	0x80

struct clubb_i2c_dev {
	struct device *dev;
	struct usb_device *udev;
	struct i2c_adapter adapter;
	uint16_t i2c_addr;
	struct dentry *debugfs_root;
};

static inline int clubb_i2c_addr(struct usb_device *udev, uint8_t i2c_addr)
{
	return usb_control_msg(udev, usb_sndctrlpipe(udev, 0), I2C_ADDR, USB_DIR_OUT | USB_TYPE_VENDOR , i2c_addr, 0, NULL, 0, 5000);
}

static inline int clubb_i2c_read(struct usb_device *udev, uint16_t high_addr, uint16_t low_addr, void *data, uint16_t len)
{
	return usb_control_msg(udev, usb_sndctrlpipe(udev, 0), I2C_READ, USB_DIR_IN | USB_TYPE_VENDOR , high_addr, low_addr, data, len, 1000);
}

static inline int clubb_i2c_write(struct usb_device *udev, uint16_t high_addr, uint16_t low_addr, void *data, uint16_t len)
{
	return usb_control_msg(udev, usb_sndctrlpipe(udev, 0), I2C_WRITE, USB_DIR_OUT | USB_TYPE_VENDOR , high_addr, low_addr, data, len, 5000);
}

static int clubb_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[], int num)
{
	struct clubb_i2c_dev *i2c_dev = i2c_get_adapdata(adap);
	uint16_t high_addr, low_addr;
	int ret;

	if (num == 2){
		if ( (!(msgs[0].flags & I2C_M_RD)) && (msgs[1].flags & I2C_M_RD) ){
			high_addr = ((uint16_t)(msgs[0].buf[0] << 8 )) | msgs[0].buf[1];
			low_addr  = ((uint16_t)(msgs[0].buf[2] << 8 )) | msgs[0].buf[3];
			if (i2c_dev->i2c_addr != msgs[0].addr) {
				clubb_i2c_addr(i2c_dev->udev, msgs[0].addr << 1);
				i2c_dev->i2c_addr = msgs[0].addr;
			}
			ret = clubb_i2c_read(i2c_dev->udev, high_addr, low_addr, msgs[1].buf, msgs[1].len);
			if (ret != msgs[1].len)
				return -EIO;
			return num;
		}
	} else {
		if (num == 1 && (!(msgs[0].flags & I2C_M_RD)) ) {
			high_addr = ((uint16_t)(msgs[0].buf[0] << 8 )) | msgs[0].buf[1];
			low_addr  = ((uint16_t)(msgs[0].buf[2] << 8 )) | msgs[0].buf[3];
			if (i2c_dev->i2c_addr != msgs[0].addr) {
				clubb_i2c_addr(i2c_dev->udev, msgs[0].addr << 1);
				i2c_dev->i2c_addr = msgs[0].addr;
			}
			ret = clubb_i2c_write(i2c_dev->udev, high_addr, low_addr, msgs[0].buf, msgs[0].len);
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

static const struct i2c_algorithm blubb_i2c_algo = {
	.master_xfer	= clubb_i2c_xfer,
	.functionality	= clubb_i2c_func,
};

static void clubb_i2c_callback(struct urb *urb)
{
	struct clubb_i2c_dev *i2c_dev = (struct clubb_i2c_dev *)urb->context;
	struct usb_device *udev = i2c_dev->udev;
	int status = urb->status;
	uint8_t *buf = urb->transfer_buffer;

	if (status && !(status == -ENOENT || status == -ECONNRESET || status == -ESHUTDOWN)) {
		dev_err(&udev->dev, "urb=%p bulk status: %d\n", urb, status);
		return;
	}

	if (buf[0] != 0) {
	    pr_info(" IRQ %d", buf[0]);
	}

	usb_submit_urb(urb, GFP_ATOMIC);

}
#define USB_PKT_LEN 4
static ssize_t clubb_file(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct clubb_i2c_dev *i2c_dev = file_inode(file)->i_private;
	struct urb *urb;
	uint8_t *buf;
	int retval;
	struct usb_ctrlrequest *dr;

	pr_info("clubb_file \n");

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		pr_err("usb_alloc_urb err");
	buf = usb_alloc_coherent(i2c_dev->udev, USB_PKT_LEN, GFP_KERNEL, &urb->transfer_dma);
	if (!buf) {
		usb_free_urb(urb);
		pr_err("usb_alloc_coherent err");
	}

	dr = kmalloc(sizeof(*dr), GFP_KERNEL);
	if (!dr) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	dr->bRequestType = USB_DIR_IN | USB_TYPE_VENDOR;
	dr->bRequest     = IRQ_READ;
	dr->wIndex       = 0;
	dr->wValue       = 0;
	dr->wLength      = __cpu_to_le16(USB_PKT_LEN);

	usb_fill_control_urb(urb, i2c_dev->udev, usb_sndctrlpipe(i2c_dev->udev, 0), (void *) dr, buf, 4, clubb_i2c_callback, i2c_dev);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		pr_err("usb_submit_urb %d\n", retval);

	return 0;
}

static const struct file_operations clubb_fops = {
	.read = &clubb_file,
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
	i2c_dev->udev = udev;

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

	i2c_dev->debugfs_root = debugfs_create_dir("clubb", NULL);
	if (i2c_dev->debugfs_root == NULL) {
		pr_err("Failed to create debugfs dir\n");
	}

	debugfs_create_file("exec", 0660, i2c_dev->debugfs_root, i2c_dev, &clubb_fops);

	return i2c_add_adapter(adap);
}

static void clubb_i2c_disconnect(struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct clubb_i2c_dev *i2c_dev = dev_get_drvdata(&udev->dev);

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
