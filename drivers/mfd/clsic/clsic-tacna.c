/*
 * clsic-tacna.c - core MFD support for codec aspect of CLSIC devices
 *
 * Copyright 2017 Cirrus Logic
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/mfd/clsic/core.h>
#include <linux/mfd/clsic/regmapsrv.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tacna/core.h>
#include <linux/module.h>

#define CLSIC_32K_MCLK2		1

static struct mfd_cell clsic_tacna_devs[] = {
	{ .name = "clsic-codec", },
	/* { .name = "clsic-gpio", }, */
};

static int clsic_tacna_probe(struct platform_device *pdev)
{
	struct clsic *clsic = dev_get_drvdata(pdev->dev.parent);
	struct clsic_regmapsrv_struct *regmapsrv =
		dev_get_platdata(&pdev->dev);
	struct tacna *tacna;
	u32 devid;
	int ret = 0;

	ret = regmap_read(regmapsrv->regmap, CLSIC_DEVID, &devid);
	dev_dbg(&pdev->dev, "%s() regmap access test: devid 0x%x (%d)\n",
		__func__, devid, ret);

	/*
	 * Don't try to run this on the emulated platform, only on real
	 * hardware
	 */
	if (devid != CLSIC_SUPPORTED_ID_48AB50 &&
	    devid != CLSIC_SUPPORTED_ID_48AC40)
		return -EIO;

	tacna = devm_kzalloc(&pdev->dev, sizeof(struct tacna), GFP_KERNEL);
	if (!tacna)
		return -ENOMEM;

	tacna->type = CS48LX50; /* TODO: should read this? */
	tacna->dev = &pdev->dev;
	tacna->dev->of_node = clsic->dev->of_node;
	tacna->irq = 0; /*
			 * TODO: should set something sensible once IRQ support
			 * is added
			 */

	tacna->regmap = regmapsrv->regmap;

	dev_set_drvdata(tacna->dev, tacna);

	ret = regmap_update_bits(tacna->regmap,
				 TACNA_CLOCK32K,
				 TACNA_CLK_32K_EN_MASK | TACNA_CLK_32K_SRC_MASK,
				 TACNA_CLK_32K_EN | CLSIC_32K_MCLK2);
	if (ret) {
		dev_err(tacna->dev, "Failed to init 32k clock: %d\n", ret);
		return ret;
	}

	ret = mfd_add_devices(tacna->dev, PLATFORM_DEVID_NONE, clsic_tacna_devs,
			      ARRAY_SIZE(clsic_tacna_devs), NULL, 0, NULL);
	if (ret)
		dev_err(tacna->dev, "Failed to add subdevices: %d\n", ret);

	return ret;
}

static int clsic_tacna_remove(struct platform_device *pdev)
{
	mfd_remove_devices(&pdev->dev);

	return 0;
}

static struct platform_driver clsic_tacna_core_driver = {
	.driver = {
		.name = "clsic-tacna",
		.owner = THIS_MODULE,
	},
	.probe	= clsic_tacna_probe,
	.remove	= clsic_tacna_remove,
};

static int __init clsic_tacna_core_init(void)
{
	return platform_driver_register(&clsic_tacna_core_driver);
}

static void __exit clsic_tacna_core_exit(void)
{
	platform_driver_unregister(&clsic_tacna_core_driver);
}

module_init(clsic_tacna_core_init);
module_exit(clsic_tacna_core_exit);

MODULE_AUTHOR("Piotr Stankiewicz <piotrs@opensource.wolfsonmicro.com>");
MODULE_DESCRIPTION("CLSIC Tacna MFD core");
MODULE_LICENSE("GPL v2");
