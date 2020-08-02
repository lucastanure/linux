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

#include <linux/of.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/platform_device.h>
#include <linux/mfd/clubb/clubb.h>

struct clubb_spi {
	struct clubb *clubb;
	struct spi_master *master;
};

static int clubb_spi_setup(struct spi_device *spi_dev)
{
	//struct clubb_spi *spi = spi_master_get_devdata(spi_dev->master);
	if (spi_dev->chip_select > 1) {
		pr_err("setup: only one native chip-selects are supported\n");
		return -EINVAL;
	}
	return 0;
}

static void clubb_spi_set_cs(struct spi_device *spi, bool gpio_level)
{
	struct clubb_spi *spi_dev = spi_master_get_devdata(spi->master);
	struct clubb *clubb = spi_dev->clubb;

	if (gpio_level) {
		mutex_unlock(&clubb->mutex_ep0);
	}
	else {
		mutex_lock(&clubb->mutex_ep0);
	}

	clubb_control_msg2(spi_dev->clubb, SPI_CS, USB_DIR_OUT | USB_TYPE_VENDOR , gpio_level, 0, NULL, 0);
}

static int clubb_spi_transfer_one(struct spi_master *master, struct spi_device *spi, struct spi_transfer *tfr)
{
	struct clubb_spi *spi_dev = spi_master_get_devdata(master);
	struct clubb *clubb = spi_dev->clubb;
	unsigned transfer_size = tfr->len;
	unsigned current_size;
	uint8_t *buf;
	//pr_info("SPI Transfer Len %d %s %s", tfr->len, tfr->tx_buf != NULL ? "OUT" : "", tfr->rx_buf != NULL ? "IN" : "");

	if (tfr->tx_buf != NULL) {
		buf = (uint8_t*)tfr->tx_buf;
		while(transfer_size != 0){
			if (transfer_size <= 4096)
				current_size = transfer_size;
			else
				current_size = 4096;

			clubb_control_msg2(spi_dev->clubb, SPI_WRITE, USB_DIR_OUT | USB_TYPE_VENDOR, 0, 0, buf, current_size);
			transfer_size -= current_size;
			buf = &buf[current_size];
		}
	}
	if (tfr->rx_buf != NULL)
		clubb_control_msg2(spi_dev->clubb, SPI_READ, USB_DIR_IN | USB_TYPE_VENDOR, 0, 0, tfr->rx_buf, tfr->len);

	return 0;
}

static void clubb_spi_handle_err(struct spi_master *master, struct spi_message *msg)
{
	pr_info("%s", __FUNCTION__);
}

static int clubb_spi_prepare_message(struct spi_master *master, struct spi_message *msg)
{

	return 0;
}

static int clubb_spi_probe(struct platform_device *pdev)
{
	struct clubb *clubb;
	struct clubb_spi *spi_dev;
	struct device_node *np;
	struct spi_master *master;
	int ret;

	clubb = dev_get_drvdata(pdev->dev.parent);
	if (!clubb) {
		pr_info("fuck clubb_spi_probe!!");
		return -EPROBE_DEFER;
	}

	master = spi_alloc_master(&pdev->dev, sizeof(struct clubb_spi));
	if (!master) {
		pr_err("spi_alloc_master() failed\n");
		return -ENOMEM;
	}

	master->mode_bits = (SPI_CPOL|SPI_CPHA|SPI_CS_HIGH);
	master->bits_per_word_mask = SPI_BPW_MASK(8);
	master->num_chipselect = 1;
	master->setup = clubb_spi_setup;
	master->set_cs = clubb_spi_set_cs;
	master->transfer_one = clubb_spi_transfer_one;
	master->handle_err = clubb_spi_handle_err;
	master->prepare_message = clubb_spi_prepare_message;

	spi_dev = spi_master_get_devdata(master);
	spi_dev->master = master;
	spi_dev->clubb = clubb;

	np = of_find_compatible_node(NULL, NULL, "cirrus,clubb-spi");
	if (np)
		master->dev.of_node = np;
	of_node_put(np);

	ret = devm_spi_register_master(&pdev->dev, master);
	if (ret)
		pr_err("could not register SPI master: %d\n", ret);
	return ret;
}


static struct platform_driver clubb_spi_driver = {
	.probe		= clubb_spi_probe,
	.driver		= {
		.name	= "clubb-spi",
	},
};
module_platform_driver(clubb_spi_driver);

MODULE_AUTHOR("Lucas Tanure <tanureal@opensource.cirrus.com>");
MODULE_DESCRIPTION("Driver for Cirrus SPI USB Bridge");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:clubb-spi");
