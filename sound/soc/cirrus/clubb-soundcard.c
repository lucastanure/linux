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
#include "../codecs/tacna.h"
#include "../codecs/cs35l41.h"

enum DAI_ID { CODEC_DAI, LEFT_AMP_DAI, RIGHT_AMP_DAI};


#define BITS		32
#define CHANNELS	2
#define AUDIO_RATE	48000
#define MCLK1_RATE	24576000
#define FLLOUT_RATE	49152000
#define ASP_BCLK	3072000 //1536000
#define SYSCLK_RATE	98304000
#define AMPCLK_RATE	(AUDIO_RATE * CHANNELS * BITS)

static int clubb_set_bias_level(struct snd_soc_card *card,
				  struct snd_soc_dapm_context *dapm,
				  enum snd_soc_bias_level level)
{
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai *cdc_dai;
	int ret = 0;

	rtd = snd_soc_get_pcm_runtime(card, card->dai_link[CODEC_DAI].name);
	cdc_dai = rtd->codec_dai;

	if (dapm->dev != cdc_dai->dev)
		return 0;

	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		if (dapm->bias_level != SND_SOC_BIAS_STANDBY)
			break;
		ret = snd_soc_component_set_pll(cdc_dai->component, TACNA_CLK_SYSCLK_1,
					    TACNA_FLL_SRC_MCLK1,
					    MCLK1_RATE,
					    FLLOUT_RATE);
		if (ret < 0)
			pr_err("Failed to start FLL: %d\n", ret);
		break;
	default:
		break;
	}
	return ret;
}

static int clubb_set_bias_level_post(struct snd_soc_card *card, struct snd_soc_dapm_context *dapm,
				     enum snd_soc_bias_level level)
{
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai *cdc_dai;
	int ret;

	rtd = snd_soc_get_pcm_runtime(card, card->dai_link[CODEC_DAI].name);
	cdc_dai = rtd->codec_dai;

	if (dapm->dev != cdc_dai->dev)
		return 0;

	switch (level) {
	case SND_SOC_BIAS_STANDBY:
		ret = snd_soc_component_set_pll(cdc_dai->component, TACNA_CLK_SYSCLK_1, 0, 0, 0);
		if (ret < 0) {
			pr_err("Failed to stop FLL: %d\n", ret);
			return ret;
		}
		break;
	default:
		break;
	}
	return 0;
}

static int clubb_amp_late_probe(struct snd_soc_card *card, unsigned int amp)
{
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai *asp_dai;
	struct snd_soc_component *comp;
	int ret;

	rtd = snd_soc_get_pcm_runtime(card, card->dai_link[amp].name);
	asp_dai = rtd->codec_dai;
	comp = asp_dai->component;

	/* CLKID has a hardcoded source */
	ret = snd_soc_component_set_sysclk(comp, 0, 0, AMPCLK_RATE, SND_SOC_CLOCK_IN);
	if (ret != 0) {
		dev_err(comp->dev, "Failed to set amp SYSCLK: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(asp_dai, 0, AMPCLK_RATE, SND_SOC_CLOCK_IN);
	if (ret != 0) {
		dev_err(card->dev, "Failed to set %s clock: %d\n",
				   asp_dai->name, ret);
		return ret;
	}

	return 0;
}

static int clubb_late_probe(struct snd_soc_card *card)
{
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai *asp_dai;
	struct snd_soc_component *comp;
	int ret;

	rtd = snd_soc_get_pcm_runtime(card, card->dai_link[CODEC_DAI].name);
	asp_dai = rtd->codec_dai;
	comp = asp_dai->component;

	ret = snd_soc_component_set_sysclk(comp, TACNA_CLK_SYSCLK_1, TACNA_CLK_SRC_FLL1, SYSCLK_RATE,
					   SND_SOC_CLOCK_IN);
	if (ret != 0) {
		dev_err(comp->dev, "Failed to set SYSCLK: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(asp_dai, TACNA_CLK_SYSCLK_1, 0, 0);
	if (ret != 0) {
		dev_err(card->dev, "Failed to set %s clock: %d\n",
				   asp_dai->name, ret);
		return ret;
	}
	/* Configure clock for AMPS */
	ret = clubb_amp_late_probe(card, LEFT_AMP_DAI);
	if (ret != 0) {
		dev_err(card->dev, "Failed to config Left Amp.\n");
		return ret;
	}

	ret = clubb_amp_late_probe(card, RIGHT_AMP_DAI);
	if (ret != 0) {
		dev_err(card->dev, "Failed to config Right Amp.\n");
		return ret;
	}

	return 0;
}

static struct snd_soc_codec_conf clubb_codec_conf[] = {
	{
		.dev_name = "cs35l41.7-0040",
		.name_prefix = "Left_AMP",
	},
	{
		.dev_name = "cs35l41.7-0041",
		.name_prefix = "Right_AMP",
	},
};

static const struct snd_soc_pcm_stream cs35l41_params = {
	/*It's supports 24 bits, but the bus is 32 bits*/
	.formats = SNDRV_PCM_FMTBIT_S24_LE,
	.rate_min = AUDIO_RATE,
	.rate_max = AUDIO_RATE,
	.channels_min = CHANNELS,
	.channels_max = CHANNELS,
};

static struct snd_soc_dai_link clubb_dai[] = {
	{
		.name = "cpu-codec1",
		.stream_name = "cpu-codec1",
		.cpu_dai_name = "clubb-i2s-sai1",
		.codec_dai_name = "clsic-asp1",
		.codec_name = "clsic-codec",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS,
	},
	{
		.name = "codec-left-amp",
		.stream_name = "codec-left-amp",
		.cpu_dai_name = "clsic-asp4",
		.codec_dai_name = "cs35l41.7-0040",
		.codec_name = "cs35l41.7-0040",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS,
		.params = &cs35l41_params,
	},
	{
		.name = "codec-right-amp",
		.stream_name = "codec-right-amp",
		.cpu_dai_name = "clsic-asp4",
		.codec_dai_name = "cs35l41.7-0041",
		.codec_name = "cs35l41.7-0041",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS,
		.params = &cs35l41_params,
	},
};

static struct snd_soc_card clubb_sndcard = {
	.name			= "Clubb-SoundCard",
	.long_name		= "Cirrus Clubb SoundCard",
	.dai_link		= clubb_dai,
	.num_links		= ARRAY_SIZE(clubb_dai),

	.codec_conf		= clubb_codec_conf,
	.num_configs		= ARRAY_SIZE(clubb_codec_conf),
	.late_probe		= clubb_late_probe,

	.set_bias_level		= clubb_set_bias_level,
	.set_bias_level_post	= clubb_set_bias_level_post,
};

static int clubb_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *i2s_node;
	struct snd_soc_card *card = &clubb_sndcard;

	card->dev = &pdev->dev;
	dev_info(card->dev, "Clubb SoundCard\n");

        i2s_node = of_parse_phandle(pdev->dev.of_node, "i2s-controller", 0);
        if (!i2s_node) {
                dev_err(&pdev->dev, "i2s-controller missing in DT\n");
                return -ENODEV;
        }

        clubb_dai[CODEC_DAI].cpu_of_node = i2s_node;
        clubb_dai[CODEC_DAI].platform_of_node = i2s_node;

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

MODULE_DESCRIPTION("ASoC driver for Cirrus Clubb Soundcard");
MODULE_AUTHOR("Lucas Tanure <tanureal@opensource.cirrus.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:cirrus-clubb-soundcard");
