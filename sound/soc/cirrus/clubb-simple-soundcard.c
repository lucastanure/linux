/*
 *  Machine Driver for Cirrus Quartet SoundCard - Simplified implementation
 *
 *  Copyright 2018 Cirrus Logic
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <sound/soc.h>
#include <linux/module.h>
#include <sound/pcm_params.h>

static struct snd_soc_dai_link clubb_dai[] = {
	{
		.name = "cpu-codec1",
		.stream_name = "cpu-codec1",
		.cpu_name = "clubb-i2s",
		.cpu_dai_name = "clubb-i2s-sai1",
		.codec_name = "pcm5102a-codec",
		.codec_dai_name = "pcm5102a-hifi",
		.dai_fmt = SND_SOC_DAIFMT_I2S |
			   SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBM_CFM,
	},
};

static struct snd_soc_card clubb_sndcard = {
	.name			= "Clubb-SoundCard",
	.long_name		= "Cirrus Clubb SoundCard",
	.dai_link		= clubb_dai,
	.num_links		= ARRAY_SIZE(clubb_dai),
};

static int clubb_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *i2s_node;
	struct snd_soc_card *card = &clubb_sndcard;

	card->dev = &pdev->dev;
	dev_info(card->dev, "Clubb SoundCard\n");

	ret = devm_snd_soc_register_card(card->dev, card);
	if (ret && ret != -EPROBE_DEFER)
		dev_err(card->dev, "Fail to register %s:%d\n", card->name, ret);

	return ret;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id snd_clubb_of_match[] = {
	{ .compatible = "cirrus,clubb-soundcard", },
	{},
};
MODULE_DEVICE_TABLE(of, snd_clubb_of_match);
#endif /* CONFIG_OF */

static struct platform_driver snd_clubb_soundcard_driver = {
	.driver		= {
		.name	= "clubb-soundcard",
		.of_match_table = of_match_ptr(snd_clubb_of_match),
	},
	.probe		= clubb_probe,
};

module_platform_driver(snd_clubb_soundcard_driver);

MODULE_DESCRIPTION("ASoC driver for Cirrus Quartet Simplified Soundcard");
MODULE_AUTHOR("Andrew Ford <andrew.ford@opensource.cirrus.com>");
MODULE_AUTHOR("Lucas Tanure <tanureal@opensource.cirrus.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:cirrus-clubb-soundcard");
