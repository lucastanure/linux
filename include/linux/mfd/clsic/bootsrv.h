/*
 * bootsrv.h -- CLSIC Bootloader Service
 *
 * Copyright 2017 CirrusLogic, Inc.
 *
 * Author: Simon Trimmer <simont@opensource.cirrus.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CIRRUS_CLSIC_BOOTSRV_H
#define CIRRUS_CLSIC_BOOTSRV_H

void clsic_bootsrv_state_handler(struct clsic *clsic);

int clsic_bootsrv_service_start(struct clsic *clsic,
				struct clsic_service *handler);
#endif
