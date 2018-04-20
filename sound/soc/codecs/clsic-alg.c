/*
 * clsic-alg.c -- ALSA SoC CLSIC ALGORITHM SERVICE
 *
 * Copyright 2018 CirrusLogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/regmap.h>

#include <linux/mfd/tacna/core.h>
#include <linux/mfd/tacna/registers.h>
#include "tacna.h"

#include <linux/mfd/clsic/core.h>
#include "../../../drivers/mfd/clsic/clsic-trace.h"
#include <linux/mfd/clsic/message.h>

#define CLSIC_ALG_MAX_BULK_SZ  (CLSIC_FIFO_TRANSACTION_MAX / BITS_PER_BYTE)

#define CLSIC_ALG_REG_BITS	32
#define CLSIC_ALG_REG_BYTES	(CLSIC_ALG_REG_BITS/BITS_PER_BYTE)
#define CLSIC_ALG_VAL_BITS	32
#define CLSIC_ALG_VAL_BYTES	(CLSIC_ALG_VAL_BITS/BITS_PER_BYTE)

/* Stride is the number of bytes per register address, typically this is 4  */
#define CLSIC_ALG_STRIDE	(CLSIC_ALG_REG_BITS/BITS_PER_BYTE)

#define ALGOSERV_BASEADDRESS        (0x20000000)

struct clsic_alg {
	struct clsic *clsic;

	/* Instance specific information about a service handler */
	struct clsic_service *service;

	/* SoC Audio Codec device */
	struct snd_soc_codec *codec;

	struct regmap *regmap;
	struct mutex regmap_mutex;
};

static int clsic_alg_simple_readregister(struct clsic_alg *alg,
					 uint32_t address, __be32 *value)
{
	union clsic_ras_msg msg_cmd;
	union clsic_ras_msg msg_rsp;
	int ret;

	clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
			   alg->service->service_instance,
			   CLSIC_RAS_MSG_CR_RDREG);

	msg_cmd.cmd_rdreg.addr = address;

	ret = clsic_send_msg_sync(alg->clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				  CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);

	/*
	 *  Clients to this function can't interpret detailed error codes so
	 *  map error to -EIO
	 */
	if (ret != 0) {
		clsic_dbg(alg->clsic, "0x%x ret %d\n", address, ret);
		ret = -EIO;
	} else if (msg_rsp.rsp_rdreg.hdr.err != 0) {
		clsic_dbg(alg->clsic, "addr: 0x%x status %d\n", address,
			  msg_rsp.rsp_rdreg.hdr.err);
		ret = -EIO;
	} else {
		/* The request succeeded */
		ret = 0;

		clsic_dbg(alg->clsic, "addr: 0x%x value: 0x%x status %d\n",
			  address,
			  msg_rsp.rsp_rdreg.value,
			  msg_rsp.rsp_rdreg.hdr.err);

		*value = cpu_to_be32(msg_rsp.rsp_rdreg.value);
	}

	trace_clsic_alg_simple_readregister(msg_cmd.cmd_rdreg.addr,
					    (unsigned int) *value, ret);

	return ret;
}

static int clsic_alg_simple_writeregister(struct clsic_alg *alg,
					  uint32_t address, __be32 value)
{
	struct clsic *clsic;
	union clsic_ras_msg msg_cmd;
	union clsic_ras_msg msg_rsp;
	int ret = 0;

	if (alg == NULL)
		return -EINVAL;

	clsic = alg->clsic;

	/* Format and send a message to the remote access service */
	clsic_init_message((union t_clsic_generic_message *)&msg_cmd,
			   alg->service->service_instance,
			   CLSIC_RAS_MSG_CR_WRREG);
	msg_cmd.cmd_wrreg.addr = address;
	msg_cmd.cmd_wrreg.value = value;

	ret = clsic_send_msg_sync(clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				  CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);
	/*
	 *  Clients to this function can't interpret detailed error codes so
	 *  map error to -EIO
	 */
	if (ret != 0) {
		clsic_dbg(clsic, "0x%x ret %d", address, ret);
		ret = -EIO;
	} else if (msg_rsp.rsp_wrreg.hdr.err != 0) {
		clsic_dbg(clsic, "addr: 0x%x status %d\n", address,
			  msg_rsp.rsp_wrreg.hdr.err);
		ret = -EIO;
	} else {
		/* The request succeeded */
		ret = 0;
	}

	trace_clsic_alg_simple_writeregister(msg_cmd.cmd_wrreg.addr,
					     msg_cmd.cmd_wrreg.value, ret);
	return ret;
}

static int clsic_alg_read(void *context, const void *reg_buf,
			  const size_t reg_size, void *val_buf,
			  const size_t val_size)
{
	struct clsic *clsic;
	struct clsic_alg *alg = context;
	u32 reg = be32_to_cpu(*(const __be32 *) reg_buf);
	int ret = 0;
	size_t i, j, frag_sz;
	union clsic_ras_msg msg_cmd;
	union clsic_ras_msg msg_rsp;

	if (alg == NULL)
		return -EINVAL;

	clsic = alg->clsic;

	if (val_size == CLSIC_ALG_VAL_BYTES)
		return clsic_alg_simple_readregister(alg, reg,
						     (__be32 *) val_buf);

	for (i = 0; i < val_size; i += CLSIC_ALG_MAX_BULK_SZ) {
		/* Format and send a message to the remote access service */
		clsic_init_message((union t_clsic_generic_message *)&msg_cmd,
				   alg->service->service_instance,
				   CLSIC_RAS_MSG_CR_RDREG_BULK);
		frag_sz = min(val_size - i, (size_t) CLSIC_ALG_MAX_BULK_SZ);
		msg_cmd.cmd_rdreg_bulk.addr =
			reg + ((i / CLSIC_ALG_REG_BYTES) * CLSIC_ALG_STRIDE);
		msg_cmd.cmd_rdreg_bulk.byte_count = frag_sz;

		ret = clsic_send_msg_sync(
				    clsic,
				    (union t_clsic_generic_message *) &msg_cmd,
				    (union t_clsic_generic_message *) &msg_rsp,
				    CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				    (uint8_t *)val_buf + i, frag_sz);

		trace_clsic_alg_read(msg_cmd.cmd_rdreg_bulk.addr,
				     msg_cmd.cmd_rdreg_bulk.byte_count, ret);
		/*
		 *  Clients to this function can't interpret detailed error
		 *  codes so map error to -EIO
		 */
		if (ret != 0) {
			clsic_dbg(clsic, "0x%x ret %d", reg, ret);
			return -EIO;
		} else if ((clsic_get_bulk_bit(msg_rsp.rsp_rdreg_bulk.hdr.sbc)
			    == 0) && (msg_rsp.rsp_rdreg_bulk.hdr.err != 0)) {
			clsic_dbg(clsic, "rsp addr: 0x%x status %d\n", reg,
				  msg_rsp.rsp_rdreg_bulk.hdr.err);
			return -EIO;
		} else if (msg_rsp.blkrsp_rdreg_bulk.hdr.err != 0) {
			clsic_dbg(clsic, "blkrsp addr: 0x%x status %d\n", reg,
				  msg_rsp.blkrsp_rdreg_bulk.hdr.err);
			return -EIO;
		}
		/* The request succeeded */
		ret = 0;

		/*
		 * The regmap bus is declared as BIG endian but all the
		 * accesses this service makes are CPU native so the value may
		 * need to be converted.
		 */
		for (j = 0; j < (frag_sz / CLSIC_ALG_VAL_BYTES); ++j)
			((__be32 *) val_buf)[i + j] =
				cpu_to_be32(((u32 *) val_buf)[i + j]);
	}

	return ret;
}

static int clsic_alg_write(void *context, const void *val_buf,
			   const size_t val_size)
{
	struct clsic_alg *alg = context;
	struct clsic *clsic;
	const __be32 *buf = val_buf;
	u32 addr = be32_to_cpu(buf[0]);
	int ret = 0;
	size_t i, payload_sz;
	size_t frag_sz;
	union clsic_ras_msg msg_cmd;
	union clsic_ras_msg msg_rsp;
	u32 *values;

	if (alg == NULL)
		return -EINVAL;

	clsic = alg->clsic;

	payload_sz = val_size - CLSIC_ALG_REG_BYTES;
	if ((val_size % CLSIC_ALG_STRIDE) != 0) {
		clsic_err(clsic,
			  "error: context %p val_buf %p, val_size %d",
			  context, val_buf, val_size);
		clsic_err(clsic, "0x%x 0x%x 0x%x ",
			  buf[CLSIC_FSM0], buf[CLSIC_FSM1], buf[CLSIC_FSM2]);
		return -EIO;
	}

	if (val_size == (CLSIC_ALG_VAL_BYTES + CLSIC_ALG_REG_BYTES))
		return clsic_alg_simple_writeregister(alg, addr,
						      be32_to_cpu(buf[1]));

	values = kzalloc(payload_sz, GFP_KERNEL);
	if (values == NULL)
		return -ENOMEM;

	for (i = 1; i < (val_size / CLSIC_ALG_VAL_BYTES); ++i)
		values[i - 1] = be32_to_cpu(buf[i]);

	for (i = 0; i < payload_sz; i += CLSIC_ALG_MAX_BULK_SZ) {
		/* Format and send a message to the remote access service */
		clsic_init_message((union t_clsic_generic_message *)&msg_cmd,
				   alg->service->service_instance,
				   CLSIC_RAS_MSG_CR_WRREG_BULK);
		frag_sz = min(payload_sz - i, (size_t) CLSIC_ALG_MAX_BULK_SZ);
		msg_cmd.blkcmd_wrreg_bulk.addr =
			addr + ((i / CLSIC_ALG_REG_BYTES) * CLSIC_ALG_STRIDE);
		msg_cmd.blkcmd_wrreg_bulk.hdr.bulk_sz = frag_sz;

		memset(&msg_rsp, 0, CLSIC_FIXED_MSG_SZ);
		ret = clsic_send_msg_sync(
				    clsic,
				    (union t_clsic_generic_message *) &msg_cmd,
				    (union t_clsic_generic_message *) &msg_rsp,
				    ((const u8 *) values) + i, frag_sz,
				    CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);

		trace_clsic_alg_write(msg_cmd.blkcmd_wrreg_bulk.addr,
				      msg_cmd.blkcmd_wrreg_bulk.hdr.bulk_sz,
				      ret);
		/*
		 *  Clients to this function can't interpret detailed error
		 *  codes so map error to -EIO
		 */
		if (ret != 0) {
			clsic_dbg(clsic, "0x%x ret %d", addr, ret);
			ret = -EIO;
			goto error;
		} else if (msg_rsp.rsp_wrreg_bulk.hdr.err != 0) {
			clsic_dbg(clsic, "addr: 0x%x status %d\n", addr,
				  msg_rsp.rsp_wrreg_bulk.hdr.err);
			ret = -EIO;
			goto error;
		}
		/* The request succeeded */
		ret = 0;
	}

error:
	kfree(values);
	return ret;
}

/*
 * This function is called when a single register write is performed on the
 * regmap, it translates the context back into a clsic_alg structure so the
 * request can be sent through the messaging layer and fulfilled
 */
int clsic_alg_reg_write(void *context, unsigned int reg, uint32_t val)
{
	struct clsic_alg *alg = context;

	return clsic_alg_simple_writeregister(alg, reg, val);
}

/*
 * This function is called when a single register read is performed on the
 * regmap, it translates the context back into a clsic_alg structure so the
 * request can be sent through the messaging layer and fulfilled
 */
int clsic_alg_reg_read(void *context, unsigned int reg, uint32_t *val)
{
	struct clsic_alg *alg = context;

	return clsic_alg_simple_readregister(alg, reg, (__be32 *) val);
}

static int clsic_alg_gather_write(void *context, const void *reg,
				  size_t reg_len, const void *val,
				  size_t val_len)
{
	return -ENOTSUPP;
}

/*
 * The Algorithm service exposes a big endian regmap bus, but when we send
 * requests we are cpu native.
 */
static struct regmap_bus regmap_bus_alg = {
	.reg_write = &clsic_alg_reg_write,
	.reg_read = &clsic_alg_reg_read,
	.read = &clsic_alg_read,
	.write = &clsic_alg_write,
	.gather_write = &clsic_alg_gather_write,

	.val_format_endian_default = REGMAP_ENDIAN_BIG,
};

/*
 * Implement own regmap locking in order to silence lockdep
 * recursive lock warning.
 */
static void clsic_alg_regmap_lock(void *context)
{
	struct clsic_alg *alg = context;

	mutex_lock(&alg->regmap_mutex);
}

static void clsic_alg_regmap_unlock(void *context)
{
	struct clsic_alg *alg = context;

	mutex_unlock(&alg->regmap_mutex);
}

bool clsic_alg_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x20000000 ... 0x2fffffff:
		return true;
	default:
		return false;
	}
}

/*
 * The regmap_config for the service is different to the one setup by the main
 * driver; as this is tunneling over the messaging protocol to access the
 * registers of the device the values can be cached.
 */
static struct regmap_config regmap_config_alg = {
	.reg_bits = CLSIC_ALG_REG_BITS,
	.val_bits = CLSIC_ALG_VAL_BITS,
	.reg_stride = CLSIC_ALG_STRIDE,

	.lock = &clsic_alg_regmap_lock,
	.unlock = &clsic_alg_regmap_unlock,

	.readable_reg = &clsic_alg_readable_register,
	.cache_type = REGCACHE_NONE,
	.max_register = 0x2fffffff,

};

static int clsic_alg_codec_probe(struct snd_soc_codec *codec)
{
	struct clsic_alg *alg = snd_soc_codec_get_drvdata(codec);
	struct clsic_service *handler = alg->service;

	dev_info(codec->dev, "%s() %p.\n", __func__, codec);

	alg->codec = codec;
	handler->data = (void *)alg;

	return 0;
}

static int clsic_alg_codec_remove(struct snd_soc_codec *codec)
{
	struct clsic_alg *alg = snd_soc_codec_get_drvdata(codec);

	dev_info(codec->dev, "%s() %p %p.\n", __func__, codec, alg);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_clsic_alg = {
	.probe = clsic_alg_codec_probe,
	.remove = clsic_alg_codec_remove,
};

static int clsic_alg_probe(struct platform_device *pdev)
{
	struct clsic *clsic = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct clsic_service *clsic_service = dev_get_platdata(dev);
	struct clsic_alg *alg;
	int ret;

	/* Allocate memory for device specific data */
	alg = devm_kzalloc(dev, sizeof(struct clsic_alg), GFP_KERNEL);
	if (alg == NULL)
		return -ENOMEM;

	/* Populate device specific data struct */
	alg->clsic = clsic;
	alg->service = clsic_service;
	mutex_init(&alg->regmap_mutex);
	regmap_config_alg.lock_arg = alg;

	/* Set device specific data */
	platform_set_drvdata(pdev, alg);

	alg->regmap = devm_regmap_init(dev, &regmap_bus_alg, alg,
				       &regmap_config_alg);
	if (IS_ERR(alg->regmap)) {
		ret = PTR_ERR(alg->regmap);
		dev_err(dev, "Failed to allocate register map: %d\n", ret);
		return ret;
	}

	/* Register codec with the ASoC core */
	ret = snd_soc_register_codec(dev, &soc_codec_clsic_alg, NULL, 0);
	if (ret < 0) {
		dev_err(dev, "Failed to register codec: %d.\n", ret);
		return ret;
	}

	dev_info(dev, "%s() Register: %p ret %d.\n", __func__, dev, ret);

	return ret;
}

static int clsic_alg_remove(struct platform_device *pdev)
{
	struct clsic_alg *alg = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "%s() dev %p priv %p.\n",
		 __func__, &pdev->dev, alg);

	snd_soc_unregister_codec(&pdev->dev);

	return 0;
}

static struct platform_driver clsic_alg_driver = {
	.driver = {
		.name = "clsic-alg",
	},
	.probe = clsic_alg_probe,
	.remove = clsic_alg_remove,
};

module_platform_driver(clsic_alg_driver);

MODULE_DESCRIPTION("ASoC Cirrus Logic CLSIC Algorithm Service");
MODULE_AUTHOR("Andrew Ford <andrew.ford@cirrus.com>");
MODULE_AUTHOR("Lucas Tanure <tanureal@opensource.cirrus.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:clsic-alg");
