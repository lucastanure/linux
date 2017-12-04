/*
 * clsic-spi.c -- CLSIC SPI bus interface
 *
 * Copyright 2017 CirrusLogic, Inc.
 *
 * Author: Simon Trimmer <simont@opensource.cirrus.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <linux/mfd/clsic/core.h>

/*
 * This bus driver is the root of the clsic driver tree.
 *
 * The driver communicates with the clsic device through a very limited
 * register map, basically consisting of a TX/RX FIFO and status registers.
 * Messages are sent to firmware running on the device via the FIFO and the
 * firmware performs activities on our behalf as a proxy.
 *
 * For this reason the regmap configuration is extremely limited and cacheless.
 */
static const struct regmap_config clsic_spi_regmap = {
	.name = "clsic",
	.reg_bits = 32,
	.pad_bits = 16,
	.val_bits = 32,
	.reg_stride = 4,

	.max_register = CLSIC_TOP_REGISTER,
	.readable_reg = &clsic_readable_register,
	.volatile_reg = &clsic_volatile_register,
	.cache_type = REGCACHE_NONE,
};

/*
 * The probe function starts the process of driver support - it takes the
 * device information passed in and uses it to configure the main clsic
 * structure.
 *
 * The driver from then on uses abstract concepts to interact with the device
 * (the provided regmap, the abstract irq number and a fifo_tx address.
 */
static int clsic_spi_probe(struct spi_device *spi)
{
	struct clsic *clsic;
	int ret;

	clsic = devm_kzalloc(&spi->dev, sizeof(*clsic), GFP_KERNEL);
	if (clsic == NULL)
		return -ENOMEM;

	clsic->regmap = devm_regmap_init_spi(spi, &clsic_spi_regmap);
	if (IS_ERR(clsic->regmap)) {
		ret = PTR_ERR(clsic->regmap);
		dev_err(&spi->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	clsic->dev = &spi->dev;
	clsic->irq = spi->irq;

	/*
	 * Assign the location of the FIFO TX register for this bus type
	 * SPI is using a prefetching serial control port.
	 */
	clsic->fifo_tx = CLSIC_FIFO1_TX_SPI;

	return clsic_dev_init(clsic);
}

static int clsic_spi_remove(struct spi_device *spi)
{
	struct clsic *clsic = spi_get_drvdata(spi);

	clsic_dev_exit(clsic);

	return 0;
}

/*
 * XXX The "_broken" suffix is there to prevent the module from autoloading on
 * platform boot, that will be removed later in the project.
 */
static const struct spi_device_id clsic_spi_ids[] = {
	{ "clsic_broken", 1 },
	{ },
};

MODULE_DEVICE_TABLE(spi, clsic_spi_ids);

static struct spi_driver clsic_spi_driver = {
	.driver = {
		.name = "clsic",
		.owner = THIS_MODULE,
		.pm = &clsic_pm_ops,
		.of_match_table = of_match_ptr(clsic_of_match),
	},
	.probe = &clsic_spi_probe,
	.remove = &clsic_spi_remove,
	.id_table = clsic_spi_ids,
};

module_spi_driver(clsic_spi_driver);

MODULE_DESCRIPTION("CLSIC SPI bus interface");
MODULE_AUTHOR("Simon Trimmer <simont@opensource.cirrus.com>");
MODULE_LICENSE("GPL v2");
