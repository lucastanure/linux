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

struct clubb_gpio {
	struct clubb *clubb;
	struct gpio_chip gc;

	bool irq_poll;
	struct delayed_work irq_poll_worker;
};

static void clubb_irq_poll_callback(struct work_struct *work)
{
	struct clubb_gpio *gpio = container_of(work, struct clubb_gpio, irq_poll_worker.work);
	uint8_t *buf;
	int irq;

	buf = kzalloc(4 * sizeof(uint8_t), GFP_KERNEL);
	if (!buf)
		return;

	while (gpio->irq_poll) {
		memset(buf, 0 , 4);
		clubb_control_msg(gpio->clubb, IRQ_READ , USB_DIR_IN | USB_TYPE_VENDOR,  0,  0, buf, 4);
		if (buf[0]) {
			irq = gpio->gc.to_irq(&gpio->gc, 0);
			handle_nested_irq(irq);
		}
		if (buf[1]) {
			irq = gpio->gc.to_irq(&gpio->gc, 1);
			handle_nested_irq(irq);
		}
		if (buf[2]) {
			irq = gpio->gc.to_irq(&gpio->gc, 2);
			handle_nested_irq(irq);
		}
		if (buf[3]) {
			irq = gpio->gc.to_irq(&gpio->gc, 3);
			handle_nested_irq(irq);
		}
		msleep(10);
	}

	kfree(buf);
}

static unsigned int clubb_irq_startup(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct clubb_gpio *gpio = gpiochip_get_data(gc);

	INIT_DELAYED_WORK(&gpio->irq_poll_worker, clubb_irq_poll_callback);

	gpio->irq_poll = true;

	schedule_delayed_work(&gpio->irq_poll_worker, 0);

	return 0;
}

static void clubb_irq_shutdown(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct clubb_gpio *gpio = gpiochip_get_data(gc);

	gpio->irq_poll = false;

	cancel_delayed_work_sync(&gpio->irq_poll_worker);
}

static void clubb_irq_ack(struct irq_data *d)
{
}

static void clubb_irq_mask(struct irq_data *d)
{
}

static void clubb_irq_unmask(struct irq_data *d)
{
}

static int clubb_irq_type(struct irq_data *d, unsigned int type)
{
	return 0;
}


static int clubb_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	return 0;
}

static int clubb_gpio_direction_output(struct gpio_chip *chip, unsigned offset, int value)
{
	return 0;
}

static void clubb_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct clubb_gpio *gpio = gpiochip_get_data(chip);

	clubb_control_msg(gpio->clubb, GPIO_SET, USB_DIR_OUT | USB_TYPE_VENDOR , offset, value, NULL, 0);
}

static int clubb_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	return 1;
}

static int clubb_gpio_probe(struct platform_device *pdev)
{
	struct clubb *clubb;
	struct clubb_gpio *gpio;
	struct device_node *np;
	struct gpio_chip *gc;
	struct irq_chip *irqchip;
	int ret;

	clubb = dev_get_drvdata(pdev->dev.parent);
	if (!clubb){
		pr_info("fuck clubb_gpio_probe!!");
		return -EPROBE_DEFER;
	}

	gpio = devm_kzalloc(&pdev->dev, sizeof(struct clubb_gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;
	gc = &gpio->gc;
	gpio->clubb = clubb;

	gc->label		= "clubb_gpio";
	gc->direction_input	= clubb_gpio_direction_input;
	gc->direction_output	= clubb_gpio_direction_output;
	gc->set			= clubb_gpio_set;
	gc->get			= clubb_gpio_get;
	gc->base		= -1;
	gc->ngpio		= 4;
	gc->can_sleep		= 1;
	gc->parent		= &pdev->dev;

	np = of_find_compatible_node(NULL, NULL, "cirrus,clubb-gpio");
	if (np)
		gc->parent->of_node = np;
	of_node_put(np);

	ret = gpiochip_add_data(gc, gpio);
	if (ret < 0)
		pr_err("error registering gpio chip\n");


	irqchip = devm_kzalloc(&pdev->dev, sizeof(struct irq_chip), GFP_KERNEL);
	if (!irqchip)
		return -ENOMEM;

	irqchip->name = "clubb-irq";
	irqchip->irq_startup = clubb_irq_startup;
	irqchip->irq_shutdown = clubb_irq_shutdown;
	irqchip->irq_ack = clubb_irq_ack;
	irqchip->irq_mask = clubb_irq_mask;
	irqchip->irq_unmask = clubb_irq_unmask;
	irqchip->irq_set_type = clubb_irq_type;

	ret = gpiochip_irqchip_add(gc, irqchip, 0, handle_simple_irq, IRQ_TYPE_NONE);
	if (ret)
		pr_err("failed to add IRQ chip\n");
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
