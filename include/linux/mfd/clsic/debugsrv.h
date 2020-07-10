/*
 * debugsrv.h -- CLSIC Debug Service
 *
 * Copyright (C) 2015-2018 Cirrus Logic, Inc. and
 *			   Cirrus Logic International Semiconductor Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CIRRUS_CLSIC_DEBUGSRV_H
#define CIRRUS_CLSIC_DEBUGSRV_H

#include <linux/mfd/clsic/clsicmessagedefinesdiagnostics_DBG.h>

int clsic_debug_service_start(struct clsic *clsic,
			      struct clsic_service *handler);
#endif
