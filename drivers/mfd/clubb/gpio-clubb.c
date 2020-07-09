/*
 * Clubb GPIO and I2C master mode driver
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/i2c.h>
#include <linux/usb.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/platform_device.h>
#include <linux/mfd/clubb/clubb.h>

#define I2C_WRITE	(0xBA)
#define I2C_READ	(0xBB)
#define I2C_ADDR	(0xB0)
#define IRQ_READ	(0xAA)
#define GPIO_SET	(0xAE)

struct clubb_gpio {
	struct clubb *clubb;
	struct gpio_chip gc;
};

static int clubb_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{

	pr_info("clubb_gpio_direction_input %d\n", offset);
	return 0;
}

static int clubb_gpio_direction_output(struct gpio_chip *chip, unsigned offset, int value)
{

	pr_info("clubb_gpio_direction_output %d %d \n", offset, value);
	return 0;
}

static void clubb_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct clubb_gpio *gpio = gpiochip_get_data(chip);

	pr_info("clubb_gpio_set %d %d \n", offset, value);
	clubb_control_msg(gpio->clubb, GPIO_SET, USB_DIR_OUT | USB_TYPE_VENDOR , offset, value, NULL, 0);
}

static int clubb_gpio_get(struct gpio_chip *chip, unsigned int offset)
{

	pr_info("clubb_gpio_get %d\n", offset);
	return 1;
}

static int clubb_gpio_probe(struct platform_device *pdev)
{
	struct clubb *clubb;
	struct clubb_gpio *gpio;
	struct device_node *np;
	struct gpio_chip *gc;
	int ret;

	clubb = dev_get_drvdata(pdev->dev.parent);
	if (!clubb)
		return -EPROBE_DEFER;

	gpio = devm_kzalloc(&pdev->dev, sizeof(struct clubb_gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;
	gc = &gpio->gc;
	gpio->clubb = clubb;

	np = of_find_compatible_node(NULL, NULL, "cirrus,clubb-gpio");
	if (np)
		gc->of_node = np;
	of_node_put(np);

	gc->label		= "clubb_gpio";
	gc->direction_input	= clubb_gpio_direction_input;
	gc->direction_output	= clubb_gpio_direction_output;
	gc->set			= clubb_gpio_set;
	gc->get			= clubb_gpio_get;
	gc->base		= -1;
	gc->ngpio		= 2;
	gc->can_sleep		= 1;
	gc->parent		= &pdev->dev;

	ret = gpiochip_add_data(gc, gpio);
	if (ret < 0)
		pr_err("error registering gpio chip\n");
	pr_info("end gpiochip_add_data");

	return ret;
}

static int clubb_gpio_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver clubb_gpio_driver = {
	.probe		= clubb_gpio_probe,
	.remove		= clubb_gpio_remove,
	.driver		= {
		.name	= "clubb-gpio",
	},
};
module_platform_driver(clubb_gpio_driver);

MODULE_AUTHOR("Lucas Tanure <tanureal@opensource.cirrus.com>");
MODULE_DESCRIPTION("Driver for Cirrus I2C and GPIO USB Bridge");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:clubb-gpio");
