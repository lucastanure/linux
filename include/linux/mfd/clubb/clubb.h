/*
 * clubb.h -- CLUBB core definitions
 *
 * Copyright (C) 2020 Cirrus Logic, Inc. and Cirrus Logic International Semiconductor Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define I2C_WRITE	(0xBA)
#define I2C_READ	(0xBB)
#define I2C_ADDR	(0xB0)

#define GPIO_SET	(0xAE)
#define IRQ_READ	(0xAA)

#define SPI_WRITE	(0xC2)
#define SPI_READ	(0xC3)
#define SPI_CS		(0xCC)

struct clubb {
	struct usb_device *udev;
	struct mutex mutex_ep0;
};

int clubb_control_msg(struct clubb *clubb, __u8 request, __u8 requesttype, __u16 value, __u16 index, void *data, __u16 size);
int clubb_control_msg2(struct clubb *clubb, __u8 request, __u8 requesttype, __u16 value, __u16 index, void *data, __u16 size);
