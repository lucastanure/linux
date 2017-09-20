/*
 * debugsrv.h -- CLSIC Debug Service
 *
 * Copyright 2017 CirrusLogic, Inc.
 *
 * Author: Simon Trimmer <simont@opensource.cirrus.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CIRRUS_CLSIC_DEBUGSRV_H
#define CIRRUS_CLSIC_DEBUGSRV_H

int clsic_debug_service_start(struct clsic *clsic,
			      struct clsic_service *handler);
#endif
