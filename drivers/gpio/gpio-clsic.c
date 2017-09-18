/*
 * gpio-clsic.c -- CLSIC GPIO driver
 *
 * Copyright 2017 Cirrus Logic
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <linux/mfd/clsic/core.h>
#include <linux/mfd/clsic/regmapsrv.h>

#define CLSIC_NUM_GPIOS		28

struct clsic_gpio {
	struct clsic *clsic;
	struct regmap *regmap;
	struct gpio_chip gpio_chip;
};

static int clsic_of_read_uint_array(struct clsic *clsic,
				    const char *prop, unsigned int *dest,
				    int minlen, int maxlen)
{
	struct device_node *np = clsic->dev->of_node;
	struct property *tempprop;
	const __be32 *cur;
	u32 val;
	int n_elems, i, ret;

	n_elems = of_property_count_u32_elems(np, prop);
	if (n_elems < 0) {
		/* of_property_count_u32_elems uses -EINVAL to mean missing */
		if (n_elems == -EINVAL)
			ret = -ENOENT;
		else
			ret = n_elems;
		goto err;
	}
	if (n_elems < minlen) {
		ret = -EOVERFLOW;
		goto err;
	}
	if (n_elems == 0)
		return 0;

	i = 0;
	of_property_for_each_u32(np, prop, tempprop, cur, val) {
		if (i == maxlen)
			break;

		dest[i++] = val;
	}

	return i;

err:
	clsic_err(clsic, "DT property %s is malformed: %d\n", prop, ret);
	return ret;
}

static inline struct clsic_gpio *to_clsic_gpio(struct gpio_chip *chip)
{
	return container_of(chip, struct clsic_gpio, gpio_chip);
}

static int clsic_gpio_direction_in(struct gpio_chip *chip,
				   unsigned int offset)
{
	struct clsic_gpio *clsic_gpio = to_clsic_gpio(chip);

	return regmap_update_bits(clsic_gpio->regmap,
				  TACNA_GPIO1_CTRL1 + (4 * offset),
				  TACNA_GP1_DIR_MASK, TACNA_GP1_DIR);
}

static int clsic_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct clsic_gpio *clsic_gpio = to_clsic_gpio(chip);
	unsigned int val;
	int ret;

	ret = regmap_read(clsic_gpio->regmap, TACNA_GPIO_STATUS1, &val);
	if (ret < 0)
		return ret;

	if (val & (1 << offset))
		return 1;
	else
		return 0;
}

static int clsic_gpio_direction_out(struct gpio_chip *chip,
				    unsigned int offset, int value)
{
	struct clsic_gpio *clsic_gpio = to_clsic_gpio(chip);
	unsigned int level;

	if (value)
		level = TACNA_GP1_LVL;
	else
		level = 0;

	/* Clear DIR and set the level */
	return regmap_update_bits(clsic_gpio->regmap,
				  TACNA_GPIO1_CTRL1 + (4 * offset),
				  TACNA_GP1_DIR_MASK |
				  TACNA_GP1_LVL_MASK,
				  level);
}

static void clsic_gpio_set(struct gpio_chip *chip, unsigned int offset,
			   int value)
{
	struct clsic_gpio *clsic_gpio = to_clsic_gpio(chip);
	unsigned int level;
	int ret;

	if (value)
		level = TACNA_GP1_LVL;
	else
		level = 0;

	ret = regmap_update_bits(clsic_gpio->regmap,
				 TACNA_GPIO1_CTRL1 + (4 * offset),
				 TACNA_GP1_LVL_MASK, level);
	if (ret)
		dev_warn(chip->parent, "Failed to write register 0x%x: %d\n",
			 TACNA_GPIO1_CTRL1 + (4 * offset), ret);
}

static struct gpio_chip template_chip = {
	.label			= "clsic",
	.owner			= THIS_MODULE,
	.direction_input	= clsic_gpio_direction_in,
	.get			= clsic_gpio_get,
	.direction_output	= clsic_gpio_direction_out,
	.set			= clsic_gpio_set,
	.can_sleep		= true,
};

static int clsic_gpio_probe(struct platform_device *pdev)
{
	struct clsic *clsic = dev_get_drvdata(pdev->dev.parent);
	struct clsic_regmapsrv_struct *regmapsrv =
		dev_get_platdata(&pdev->dev);
	struct clsic_gpio *clsic_gpio;
	unsigned int gpion_control1[CLSIC_NUM_GPIOS];
	int ret;
	int i, n_elems;

	clsic_gpio = devm_kzalloc(&pdev->dev, sizeof(*clsic_gpio),
				  GFP_KERNEL);
	if (!clsic_gpio)
		return -ENOMEM;

	platform_set_drvdata(pdev, clsic_gpio);

	clsic_gpio->clsic = clsic;
	clsic_gpio->regmap = regmapsrv->regmap;
	clsic_gpio->gpio_chip = template_chip;
	clsic_gpio->gpio_chip.parent = &pdev->dev;
	clsic_gpio->gpio_chip.base = -1;

#if defined(CONFIG_OF_GPIO)
	clsic_gpio->gpio_chip.of_node = clsic->dev->of_node;
#endif

	switch (clsic->devid) {
	case CLSIC_SUPPORTED_ID_48AB50:
	case CLSIC_SUPPORTED_ID_48AC40:
		clsic_gpio->gpio_chip.ngpio = CLSIC_NUM_GPIOS;
		break;
	default:
		dev_err(&pdev->dev, "Unknown chip variant 0x%x\n",
			clsic->devid);
		return -EINVAL;
	}

	n_elems = clsic_of_read_uint_array(clsic, "cirrus,gpio-defaults",
					   gpion_control1, 0,
					   ARRAY_SIZE(gpion_control1));

	if (n_elems > 0) {
		for (i = 0; i < n_elems; i++) {
			regmap_write(clsic_gpio->regmap,
				     TACNA_GPIO1_CTRL1 + (i * 4),
				     gpion_control1[i]);
		}
	}

	ret = gpiochip_add(&clsic_gpio->gpio_chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not register gpiochip, %d\n", ret);
		return ret;
	}

	return 0;
}

static int clsic_gpio_remove(struct platform_device *pdev)
{
	struct clsic_gpio *clsic_gpio = platform_get_drvdata(pdev);

	gpiochip_remove(&clsic_gpio->gpio_chip);

	return 0;
}

static struct platform_driver clsic_gpio_driver = {
	.driver.name	= "clsic-gpio",
	.driver.owner	= THIS_MODULE,
	.probe		= clsic_gpio_probe,
	.remove		= clsic_gpio_remove,
};

module_platform_driver(clsic_gpio_driver);

MODULE_DESCRIPTION("GPIO interface for Cirrus Logic CLSIC codecs");
MODULE_AUTHOR("Lucas Tanure <tanureal@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:clsic-gpio");
