// SPDX-License-Identifier: GPL-2.0
/*
 * Bootloader Driver for CYUSB3KIT-003 EZ-USB FX3 SuperSpeed Explorer Kit
 *
 * Copyright 2020 Cirrus Logic, Inc.
 *
 * Author: Lucas Tanure <tanureal@opensource.cirrus.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * This driver was based on download_fx3.cpp from Cypress FX3 SDK
 * download_fx3.cpp description : Downloads FX3 firmware to RAM, I2C EEPROM or
 * SPI Flash.
 *
 */
#define DEBUG
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/slab.h>

#define VENDORCMD_TIMEOUT	(5000)
// Max. size of data that can be written through one vendor command.
#define MAX_WRITE_SIZE		(2 * 1024)

#define FW_NAME "cyusb3kit-003.bin"

#define cyboot_msg(udev, req, addr, buf, size) \
	usb_control_msg(udev, usb_sndctrlpipe(udev, 0), req, \
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, \
			addr & 0xFFFF, addr >> 16, buf, size, VENDORCMD_TIMEOUT)

struct fx3_sector {
	u32 dLength;
	u32 dAddress;
	u32 dData[0];
} __packed;

struct fx3_img {
	char wSignature[2];
	u8 bImageCTL;
	u8 bImageType;
	u32 sectors[0];
} __packed;

static int cyboot_ram_write(struct usb_device *udev,
			    const struct firmware *firmware)
{
	struct fx3_sector *sec;
	struct fx3_img *img;
	size_t index, size, len;
	u8 *ptr;
	u32 addr;
	char *buf;
	int ret;

	img = (struct fx3_img *)firmware->data;
	ptr = (u8 *)img->sectors;

	while (ptr < firmware->data + firmware->size) {
		sec = (struct fx3_sector *)ptr;
		if (sec->dLength == 0) {
			ret = cyboot_msg(udev, 0xA0, sec->dAddress, NULL, 0);
			if (ret < 0)
				return ret;
			break;
		}

		index = 0;
		addr = sec->dAddress;
		len = sec->dLength * sizeof(sec->dData[0]);
		buf = kmalloc(len, GFP_DMA);
		buf = memcpy(buf, sec->dData, len);
		while (len > 0) {
			size = (len > MAX_WRITE_SIZE) ? MAX_WRITE_SIZE : len;
			ret = cyboot_msg(udev, 0xA0, addr, &buf[index], size);
			if (ret != size) {
				ret = (ret < 0) ? ret : -EIO;
				return ret;
			}
			addr += size;
			index += size;
			len -= size;
		}
		kfree(buf);

		ptr += sizeof(sec->dLength) + sizeof(sec->dAddress) +
		       sec->dLength * sizeof(sec->dData[0]);
	}

	return 0;
}

static int cyboot_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	const struct firmware *firmware;
	int ret;

	dev_dbg(&intf->dev, "Start to Write to RAM\n");

	ret = request_firmware(&firmware, FW_NAME, &udev->dev);
	if (ret) {
		dev_err(&udev->dev, "request_firmware '%s' failed: %d\n",
			     FW_NAME, ret);
		return ret;
	}

	ret = cyboot_ram_write(udev, firmware);
	if (ret)
		dev_err(&udev->dev, "Fail to flash FW to RAM %d", ret);

	release_firmware(firmware);
	return ret;
}

static void cyboot_disconnect(struct usb_interface *intf)
{
	dev_dbg(&intf->dev, "%s\n", __func__);
}

static const struct usb_device_id cyboot_id_table[] = {
	{ USB_DEVICE(0x04b4, 0x00f3) }, /* cyusb3kit-003 */
	{}
};
MODULE_DEVICE_TABLE(usb, cyboot_id_table);

static struct usb_driver cyboot_driver = {
	.name		= "CYUSB3KIT-003 Bootloader Driver",
	.probe		= cyboot_probe,
	.disconnect	= cyboot_disconnect,
	.id_table	= cyboot_id_table,
};

module_usb_driver(cyboot_driver);

MODULE_AUTHOR("Lucas Tanure <tanureal@opensource.cirrus.com>");
MODULE_DESCRIPTION("Bootloader Driver for CYUSB3KIT-003 Explorer Kit");
MODULE_LICENSE("GPL v2");
MODULE_FIRMWARE(FW_NAME);
