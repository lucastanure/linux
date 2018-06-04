/*
 * regmapsrv.h -- CLSIC Register Access Service
 *
 * Copyright 2016-2018 Cirrus Logic
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CIRRUS_CLSIC_REGMAPSRV_H
#define CIRRUS_CLSIC_REGMAPSRV_H

struct clsic_regmapsrv_struct {
	struct clsic *clsic;
	uint8_t service_instance;

	struct regmap *regmap;
	struct mutex regmap_mutex;
};

int clsic_regmap_service_start(struct clsic *clsic,
			       struct clsic_service *handler);

int clsic_ras_reg_write(void *context, unsigned int reg, unsigned int val);
int clsic_ras_reg_read(void *context, unsigned int reg, unsigned int *val);

#endif
