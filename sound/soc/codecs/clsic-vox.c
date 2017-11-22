/*
 * clsic-vox.c -- ALSA SoC CLSIC VOX
 *
 * Copyright 2017 CirrusLogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>

#include <sound/core.h>
#include <sound/compress_driver.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include <linux/mfd/tacna/core.h>
#include <linux/mfd/tacna/registers.h>
#include "tacna.h"

#include <linux/mfd/clsic/core.h>
#include <linux/mfd/clsic/message.h>
#include <linux/mfd/clsic/voxsrv.h>

struct clsic_vox {
	struct clsic *clsic;
	struct snd_soc_codec *codec;
};

static const struct snd_kcontrol_new clsic_vox_snd_controls[] = {
	/* TODO controls get inserted here */
};

static int clsic_vox_codec_probe(struct snd_soc_codec *codec)
{
	struct clsic_vox *clsic_vox = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	dev_info(codec->dev, "%s() %p.\n", __func__, codec);

	clsic_vox->codec = codec;

	return ret;
}

static int clsic_vox_codec_remove(struct snd_soc_codec *codec)
{
	struct clsic_vox *clsic_vox = snd_soc_codec_get_drvdata(codec);

	dev_info(codec->dev, "%s() %p %p.\n", __func__, codec, clsic_vox);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_clsic_vox = {
	.probe = clsic_vox_codec_probe,
	.remove = clsic_vox_codec_remove,

	.controls = clsic_vox_snd_controls,
	.num_controls = ARRAY_SIZE(clsic_vox_snd_controls),
};

static int clsic_vox_probe(struct platform_device *pdev)
{
	struct clsic *clsic = dev_get_drvdata(pdev->dev.parent);
	struct clsic_service *vox_service = dev_get_platdata(&pdev->dev);
	struct clsic_vox *clsic_vox;
	int ret;
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;

	dev_info(&pdev->dev, "%s() service %p.\n", __func__,
		 vox_service);

	dev_info(&pdev->dev, "%s() clsic %p.\n", __func__,
		 clsic);

	clsic_vox = devm_kzalloc(&pdev->dev, sizeof(struct clsic_vox),
				 GFP_KERNEL);
	if (clsic_vox == NULL)
		return -ENOMEM;

	/*
	 * share of_node with the clsic device
	 *
	 * TODO: may be sensible to have the codec as a sub-node of the clsic
	 * device in device tree
	 */
	pdev->dev.of_node = clsic->dev->of_node;

	clsic_vox->clsic = clsic;

	platform_set_drvdata(pdev, clsic_vox);
#if 0
	pm_runtime_enable(&pdev->dev);
	pm_runtime_idle(&pdev->dev);
#endif

	ret = snd_soc_register_codec(&pdev->dev, &soc_codec_dev_clsic_vox,
				     NULL, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register codec: %d.\n", ret);
	}

	dev_info(&pdev->dev, "%s() Register: %p ret %d.\n", __func__,
		 &pdev->dev, ret);

	if (ret == 0) {
		dev_info(&pdev->dev, "%s() test sending idle message.\n",
			 __func__);

		clsic_init_message((union t_clsic_generic_message *)&msg_cmd,
				   vox_service->service_instance,
				   CLSIC_VOX_MSG_CR_SET_MODE);
		msg_cmd.cmd_set_mode.mode = CLSIC_VOX_MODE_IDLE;

		ret = clsic_send_msg_sync(clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				  CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);

		dev_info(&pdev->dev, "%s() idle message %d %d.\n",
			 __func__, ret, msg_rsp.rsp_set_mode.hdr.err);

		if (ret) {
			clsic_err(clsic, "Error sending msg: %d.\n", ret);
			return -EIO;
		}
		if (msg_rsp.rsp_set_mode.hdr.err) {
			clsic_err(clsic,
				  "Failed to enter idle mode: %d.\n",
				  msg_rsp.rsp_set_mode.hdr.err);
			return -EIO;
		}
	}

	return ret;
}

static int clsic_vox_remove(struct platform_device *pdev)
{
	struct clsic_vox *clsic_vox = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "%s() dev %p priv %p.\n",
		 __func__, &pdev->dev, clsic_vox);

	snd_soc_unregister_codec(&pdev->dev);

#if 0
	pm_runtime_disable(&pdev->dev);
#endif

	return 0;
}

static struct platform_driver clsic_vox_driver = {
	.driver = {
		.name = "clsic-vox",
		.owner = THIS_MODULE,
	},
	.probe = clsic_vox_probe,
	.remove = clsic_vox_remove,
};

module_platform_driver(clsic_vox_driver);

MODULE_DESCRIPTION("ASoC Cirrus Logic CLSIC VOX codec");
MODULE_AUTHOR("Piotr Stankiewicz <piotrs@opensource.wolfsonmicro.com>");
MODULE_AUTHOR("Ralph Clark <ralph.clark@cirrus.com>");
MODULE_AUTHOR("Simon Trimmer <simont@opensource.cirrus.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:clsic-vox");
