/*
 * clubb.h -- CLUBB core definitions
 *
 * Copyright (C) 2020 Cirrus Logic, Inc. and Cirrus Logic International Semiconductor Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

struct clubb {
	struct usb_device *udev;
	struct mutex mutex_ep0;
};

int clubb_control_msg(struct clubb *clubb, __u8 request, __u8 requesttype, __u16 value, __u16 index, void *data, __u16 size);
