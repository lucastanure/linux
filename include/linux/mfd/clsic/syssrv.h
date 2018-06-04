/*
 * syssrv.h -- CLSIC System Service
 *
 * Copyright 2016-2018 Cirrus Logic
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CIRRUS_CLSIC_SYSSRV_H
#define CIRRUS_CLSIC_SYSSRV_H

#define CLSIC_SVCVER_MAJ_SHIFT	24
#define CLSIC_SVCVER_MAJ_MASK	0xFF000000

#define CLSIC_SVCVER_MIN_SHIFT	16
#define CLSIC_SVCVER_MIN_MASK	0x00FF0000

#define CLSIC_SVCVER_BLD_SHIFT	0
#define CLSIC_SVCVER_BLD_MASK	0x0000FFFF

int clsic_system_service_start(struct clsic *clsic,
			       struct clsic_service *handler);

int clsic_system_service_enumerate(struct clsic *clsic);
int clsic_send_shutdown_cmd(struct clsic *clsic);

int clsic_system_service_ioctl(struct clsic *clsic, enum clsic_sys_ioctl ioctl);

#endif
