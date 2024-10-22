/*
 * cs48l32.c  --  ALSA SoC Audio driver for CS48L32 codecs
 *
 * Copyright 2018 Cirrus Logic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/completion.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include <linux/irqchip/irq-tacna.h>

#include <linux/mfd/tacna/core.h>
#include <linux/mfd/tacna/registers.h>

#include "tacna.h"
#include "wm_adsp.h"

#define DRV_NAME "cs48l32-codec"

#define CS48L32_N_AUXPDM 2
#define CS48L32_N_FLL 1
#define CS48L32_NUM_DSP 1
#define CS48L32_DSP_N_RX_CHANNELS 8
#define CS48L32_DSP_N_TX_CHANNELS 8

struct cs48l32 {
	struct tacna_priv core;
	struct tacna_fll fll;
};

static const struct wm_adsp_region cs48l32_dsp1_regions[] = {
	{ .type = WMFW_HALO_PM_PACKED, .base = 0x3800000 },
	{ .type = WMFW_HALO_XM_PACKED, .base = 0x2000000 },
	{ .type = WMFW_ADSP2_XM, .base = 0x2800000 },
	{ .type = WMFW_HALO_YM_PACKED, .base = 0x2C00000 },
	{ .type = WMFW_ADSP2_YM, .base = 0x3400000 },
};

static const unsigned int cs48l32_dsp1_sram_ext_regs[] = {
	TACNA_DSP1_XM_SRAM_IBUS_SETUP_1,
	TACNA_DSP1_XM_SRAM_IBUS_SETUP_2,
	TACNA_DSP1_XM_SRAM_IBUS_SETUP_3,
	TACNA_DSP1_XM_SRAM_IBUS_SETUP_4,
	TACNA_DSP1_XM_SRAM_IBUS_SETUP_5,
	TACNA_DSP1_XM_SRAM_IBUS_SETUP_6,
	TACNA_DSP1_XM_SRAM_IBUS_SETUP_7,
	CS48L32_DSP1_XM_SRAM_IBUS_SETUP_8,
	CS48L32_DSP1_XM_SRAM_IBUS_SETUP_9,
	CS48L32_DSP1_XM_SRAM_IBUS_SETUP_10,
	CS48L32_DSP1_XM_SRAM_IBUS_SETUP_11,
	CS48L32_DSP1_XM_SRAM_IBUS_SETUP_12,
	CS48L32_DSP1_XM_SRAM_IBUS_SETUP_13,
	CS48L32_DSP1_XM_SRAM_IBUS_SETUP_14,
	CS48L32_DSP1_XM_SRAM_IBUS_SETUP_15,
	CS48L32_DSP1_XM_SRAM_IBUS_SETUP_16,
	CS48L32_DSP1_XM_SRAM_IBUS_SETUP_17,
	CS48L32_DSP1_XM_SRAM_IBUS_SETUP_18,
	CS48L32_DSP1_XM_SRAM_IBUS_SETUP_19,
	CS48L32_DSP1_XM_SRAM_IBUS_SETUP_20,
	CS48L32_DSP1_XM_SRAM_IBUS_SETUP_21,
	CS48L32_DSP1_XM_SRAM_IBUS_SETUP_22,
	CS48L32_DSP1_XM_SRAM_IBUS_SETUP_23,
	CS48L32_DSP1_XM_SRAM_IBUS_SETUP_24,
	CS48L32_DSP1_YM_SRAM_IBUS_SETUP_1,
	CS48L32_DSP1_YM_SRAM_IBUS_SETUP_2,
	CS48L32_DSP1_YM_SRAM_IBUS_SETUP_3,
	CS48L32_DSP1_YM_SRAM_IBUS_SETUP_4,
	CS48L32_DSP1_YM_SRAM_IBUS_SETUP_5,
	CS48L32_DSP1_YM_SRAM_IBUS_SETUP_6,
	CS48L32_DSP1_YM_SRAM_IBUS_SETUP_7,
	CS48L32_DSP1_YM_SRAM_IBUS_SETUP_8,
	CS48L32_DSP1_PM_SRAM_IBUS_SETUP_1,
	CS48L32_DSP1_PM_SRAM_IBUS_SETUP_2,
	CS48L32_DSP1_PM_SRAM_IBUS_SETUP_3,
	CS48L32_DSP1_PM_SRAM_IBUS_SETUP_4,
	CS48L32_DSP1_PM_SRAM_IBUS_SETUP_5,
	CS48L32_DSP1_PM_SRAM_IBUS_SETUP_6,
	CS48L32_DSP1_PM_SRAM_IBUS_SETUP_7,
};

static const unsigned int cs48l32_dsp1_sram_pwd_regs[] = {
	TACNA_DSP1_XM_SRAM_IBUS_SETUP_0,
	CS48L32_DSP1_YM_SRAM_IBUS_SETUP_0,
	CS48L32_DSP1_PM_SRAM_IBUS_SETUP_0
};

static const struct tacna_dsp_power_regs cs48l32_dsp_sram_regs = {
	.ext = cs48l32_dsp1_sram_ext_regs,
	.n_ext = ARRAY_SIZE(cs48l32_dsp1_sram_ext_regs),
	.pwd = cs48l32_dsp1_sram_pwd_regs,
	.n_pwd = ARRAY_SIZE(cs48l32_dsp1_sram_pwd_regs),
};

static const char * const cs48l32_auxpdm_in_texts[] = {
	"Analog",
	"IN1 Digital",
	"IN2 Digital",
};

static SOC_ENUM_SINGLE_DECL(cs48l32_auxpdm1_in,
			    TACNA_AUXPDM_CTRL2,
			    TACNA_AUXPDMDAT1_SRC_SHIFT,
			    cs48l32_auxpdm_in_texts);

static SOC_ENUM_SINGLE_DECL(cs48l32_auxpdm2_in,
			    TACNA_AUXPDM_CTRL2,
			    TACNA_AUXPDMDAT2_SRC_SHIFT,
			    cs48l32_auxpdm_in_texts);

static const struct snd_kcontrol_new cs48l32_auxpdm_inmux[] = {
	SOC_DAPM_ENUM("AUXPDM1 Input", cs48l32_auxpdm1_in),
	SOC_DAPM_ENUM("AUXPDM2 Input", cs48l32_auxpdm2_in),
};

static const unsigned int cs48l32_auxpdm_analog_in_val[] = {
	0x0, 0x1,
};

static const struct soc_enum cs48l32_auxpdm_analog_inmux_enum[] = {
	SOC_VALUE_ENUM_SINGLE(TACNA_AUXPDM1_CONTROL1,
			      TACNA_AUXPDM1_SRC_SHIFT,
			      TACNA_AUXPDM1_SRC_MASK >> TACNA_AUXPDM1_SRC_SHIFT,
			      ARRAY_SIZE(cs48l32_auxpdm_analog_in_val),
			      tacna_auxpdm_in_texts,
			      cs48l32_auxpdm_analog_in_val),
	SOC_VALUE_ENUM_SINGLE(TACNA_AUXPDM2_CONTROL1,
			      TACNA_AUXPDM2_SRC_SHIFT,
			      TACNA_AUXPDM2_SRC_MASK >> TACNA_AUXPDM2_SRC_SHIFT,
			      ARRAY_SIZE(cs48l32_auxpdm_analog_in_val),
			      tacna_auxpdm_in_texts,
			      cs48l32_auxpdm_analog_in_val),
};

static const struct snd_kcontrol_new cs48l32_auxpdm_analog_inmux[] = {
	SOC_DAPM_ENUM("AUXPDM1 Analog Input",
		      cs48l32_auxpdm_analog_inmux_enum[0]),
	SOC_DAPM_ENUM("AUXPDM2 Analog Input",
		      cs48l32_auxpdm_analog_inmux_enum[1]),
};

static const unsigned int cs48l32_us_freq_val[] = {
	0x2, 0x3,
};

static const struct soc_enum cs48l32_us_freq[] = {
	SOC_VALUE_ENUM_SINGLE(TACNA_US1_CONTROL,
			      TACNA_US1_FREQ_SHIFT,
			      TACNA_US1_FREQ_MASK >> TACNA_US1_FREQ_SHIFT,
			      ARRAY_SIZE(cs48l32_us_freq_val),
			      &tacna_us_freq_texts[2],
			      cs48l32_us_freq_val),
	SOC_VALUE_ENUM_SINGLE(TACNA_US2_CONTROL,
			      TACNA_US2_FREQ_SHIFT,
			      TACNA_US2_FREQ_MASK >> TACNA_US2_FREQ_SHIFT,
			      ARRAY_SIZE(cs48l32_us_freq_val),
			      &tacna_us_freq_texts[2],
			      cs48l32_us_freq_val),
};

static const unsigned int cs48l32_us_in_val[] = {
	0x0, 0x1, 0x2, 0x3,
};

static const struct soc_enum cs48l32_us_inmux_enum[] = {
	SOC_VALUE_ENUM_SINGLE(TACNA_US1_CONTROL,
			      TACNA_US1_SRC_SHIFT,
			      TACNA_US1_SRC_MASK >> TACNA_US1_SRC_SHIFT,
			      ARRAY_SIZE(cs48l32_us_in_val),
			      tacna_us_in_texts,
			      cs48l32_us_in_val),
	SOC_VALUE_ENUM_SINGLE(TACNA_US2_CONTROL,
			      TACNA_US2_SRC_SHIFT,
			      TACNA_US2_SRC_MASK >> TACNA_US2_SRC_SHIFT,
			      ARRAY_SIZE(cs48l32_us_in_val),
			      tacna_us_in_texts,
			      cs48l32_us_in_val),
};

static const struct snd_kcontrol_new cs48l32_us_inmux[2] = {
	SOC_DAPM_ENUM("Ultrasonic 1 Input", cs48l32_us_inmux_enum[0]),
	SOC_DAPM_ENUM("Ultrasonic 2 Input", cs48l32_us_inmux_enum[1]),
};

static const char * const cs48l32_us_det_lpf_cut_texts[] = {
	"1722Hz",
	"833Hz",
	"408Hz",
	"203Hz",
};

static const struct soc_enum cs48l32_us_det_lpf_cut[] = {
	SOC_ENUM_SINGLE(TACNA_US1_DET_CONTROL,
			TACNA_US1_DET_LPF_CUT_SHIFT,
			ARRAY_SIZE(cs48l32_us_det_lpf_cut_texts),
			cs48l32_us_det_lpf_cut_texts),
	SOC_ENUM_SINGLE(TACNA_US2_DET_CONTROL,
			TACNA_US2_DET_LPF_CUT_SHIFT,
			ARRAY_SIZE(cs48l32_us_det_lpf_cut_texts),
			cs48l32_us_det_lpf_cut_texts),
};

static const char * const cs48l32_us_det_dcy_texts[] = {
	"0 ms",
	"0.79 ms",
	"1.58 ms",
	"3.16 ms",
	"6.33 ms",
	"12.67 ms",
	"25.34 ms",
	"50.69 ms",
};

static const struct soc_enum cs48l32_us_det_dcy[] = {
	SOC_ENUM_SINGLE(TACNA_US1_DET_CONTROL,
			TACNA_US1_DET_DCY_SHIFT,
			ARRAY_SIZE(cs48l32_us_det_dcy_texts),
			cs48l32_us_det_dcy_texts),
	SOC_ENUM_SINGLE(TACNA_US2_DET_CONTROL,
			TACNA_US2_DET_DCY_SHIFT,
			ARRAY_SIZE(cs48l32_us_det_dcy_texts),
			cs48l32_us_det_dcy_texts),
};

static int cs48l32_dmode_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *comp =
		snd_soc_dapm_kcontrol_component(kcontrol);
	struct snd_soc_dapm_context *dapm =
		snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct soc_enum *e = (struct soc_enum *) kcontrol->private_value;
	unsigned int mode;
	int ret, result;

	mode = ucontrol->value.enumerated.item[0];
	switch (mode) {
	case 0:
		ret = snd_soc_component_update_bits(dapm->component,
						TACNA_ADC1L_ANA_CONTROL1,
						TACNA_ADC1L_INT_ENA_FRC_MASK,
						TACNA_ADC1L_INT_ENA_FRC_MASK);
		if (ret < 0) {
			dev_err(comp->dev,
				"Failed to set ADC1L_INT_ENA_FRC: %d\n", ret);
			return ret;
		}

		ret = snd_soc_component_update_bits(dapm->component,
						TACNA_ADC1R_ANA_CONTROL1,
						TACNA_ADC1R_INT_ENA_FRC_MASK,
						TACNA_ADC1R_INT_ENA_FRC_MASK);
		if (ret < 0) {
			dev_err(comp->dev,
				"Failed to set ADC1R_INT_ENA_FRC: %d\n", ret);
			return ret;
		}

		result = snd_soc_component_update_bits(dapm->component,
						       e->reg,
						       TACNA_IN1_MODE_MASK,
						       0);
		if (result < 0) {
			dev_err(comp->dev,
				"Failed to set input mode: %d\n", result);
			return result;
		}

		usleep_range(200, 300);

		ret = snd_soc_component_update_bits(dapm->component,
						TACNA_ADC1L_ANA_CONTROL1,
						TACNA_ADC1L_INT_ENA_FRC_MASK,
						0);
		if (ret < 0) {
			dev_err(comp->dev,
				"Failed to clear ADC1L_INT_ENA_FRC: %d\n", ret);
			return ret;
		}

		ret = snd_soc_component_update_bits(dapm->component,
						TACNA_ADC1R_ANA_CONTROL1,
						TACNA_ADC1R_INT_ENA_FRC_MASK,
						0);
		if (ret < 0) {
			dev_err(comp->dev,
				"Failed to clear ADC1R_INT_ENA_FRC: %d\n", ret);
			return ret;
		}

		if (result)
			return snd_soc_dapm_mux_update_power(dapm, kcontrol,
							     mode, e, NULL);
		else
			return 0;
		break;
	case 1:
		return snd_soc_dapm_put_enum_double(kcontrol, ucontrol);
	default:
		return -EINVAL;
	}
}

static SOC_ENUM_SINGLE_DECL(cs48l32_in1dmode_enum,
			    TACNA_INPUT1_CONTROL1,
			    TACNA_IN1_MODE_SHIFT,
			    tacna_dmode_texts);

static const struct snd_kcontrol_new cs48l32_dmode_mux[] = {
	SOC_DAPM_ENUM_EXT("IN1 Mode", cs48l32_in1dmode_enum,
			  snd_soc_dapm_get_enum_double, cs48l32_dmode_put),
};

static int cs48l32_in_ev(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol,
			 int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		switch (w->shift) {
		case TACNA_IN1L_EN_SHIFT:
			snd_soc_component_update_bits(comp,
						TACNA_ADC1L_ANA_CONTROL1,
						TACNA_ADC1L_INT_ENA_FRC_MASK,
						TACNA_ADC1L_INT_ENA_FRC_MASK);
			break;
		case TACNA_IN1R_EN_SHIFT:
			snd_soc_component_update_bits(comp,
						TACNA_ADC1R_ANA_CONTROL1,
						TACNA_ADC1R_INT_ENA_FRC_MASK,
						TACNA_ADC1R_INT_ENA_FRC_MASK);
			break;
		default:
			dev_err(comp->dev, "Enabling unknown input channel\n");
			break;
		}
		break;
	case SND_SOC_DAPM_POST_PMU:
		usleep_range(200, 300);

		switch (w->shift) {
		case TACNA_IN1L_EN_SHIFT:
			snd_soc_component_update_bits(comp,
						TACNA_ADC1L_ANA_CONTROL1,
						TACNA_ADC1L_INT_ENA_FRC_MASK,
						0);
			break;
		case TACNA_IN1R_EN_SHIFT:
			snd_soc_component_update_bits(comp,
						TACNA_ADC1R_ANA_CONTROL1,
						TACNA_ADC1R_INT_ENA_FRC_MASK,
						0);
			break;

		default:
			dev_err(comp->dev, "Disabling unknown input channel\n");
			break;
		}
	default:
		break;
	}

	return tacna_in_ev(w, kcontrol, event);
}

static const struct snd_kcontrol_new cs48l32_snd_controls[] = {
SOC_ENUM("IN1 OSR", tacna_in_dmic_osr[0]),
SOC_ENUM("IN2 OSR", tacna_in_dmic_osr[1]),

SOC_SINGLE_RANGE_TLV("IN1L Volume", TACNA_IN1L_CONTROL2,
		     TACNA_IN1L_PGA_VOL_SHIFT, 0x40, 0x5f, 0, tacna_ana_tlv),
SOC_SINGLE_RANGE_TLV("IN1R Volume", TACNA_IN1R_CONTROL2,
		     TACNA_IN1R_PGA_VOL_SHIFT, 0x40, 0x5f, 0, tacna_ana_tlv),

SOC_ENUM("IN HPF Cutoff Frequency", tacna_in_hpf_cut_enum),

SOC_SINGLE_EXT("IN1L LP Switch", TACNA_IN1L_CONTROL1, TACNA_IN1L_LP_MODE_SHIFT,
	       1, 0, snd_soc_get_volsw, tacna_low_power_mode_put),
SOC_SINGLE_EXT("IN1R LP Switch", TACNA_IN1R_CONTROL1, TACNA_IN1R_LP_MODE_SHIFT,
	       1, 0, snd_soc_get_volsw, tacna_low_power_mode_put),

SOC_SINGLE("IN1L HPF Switch", TACNA_IN1L_CONTROL1, TACNA_IN1L_HPF_SHIFT, 1, 0),
SOC_SINGLE("IN1R HPF Switch", TACNA_IN1R_CONTROL1, TACNA_IN1R_HPF_SHIFT, 1, 0),
SOC_SINGLE("IN2L HPF Switch", TACNA_IN2L_CONTROL1, TACNA_IN2L_HPF_SHIFT, 1, 0),
SOC_SINGLE("IN2R HPF Switch", TACNA_IN2R_CONTROL1, TACNA_IN2R_HPF_SHIFT, 1, 0),

SOC_SINGLE_EXT_TLV("IN1L Digital Volume", TACNA_IN1L_CONTROL2,
		   TACNA_IN1L_VOL_SHIFT, 0xbf, 0, snd_soc_get_volsw,
		   tacna_in_put_volsw, tacna_digital_tlv),
SOC_SINGLE_EXT_TLV("IN1R Digital Volume", TACNA_IN1R_CONTROL2,
		   TACNA_IN1R_VOL_SHIFT, 0xbf, 0, snd_soc_get_volsw,
		   tacna_in_put_volsw, tacna_digital_tlv),
SOC_SINGLE_EXT_TLV("IN2L Digital Volume", TACNA_IN2L_CONTROL2,
		   TACNA_IN2L_VOL_SHIFT, 0xbf, 0, snd_soc_get_volsw,
		   tacna_in_put_volsw, tacna_digital_tlv),
SOC_SINGLE_EXT_TLV("IN2R Digital Volume", TACNA_IN2R_CONTROL2,
		   TACNA_IN2R_VOL_SHIFT, 0xbf, 0, snd_soc_get_volsw,
		   tacna_in_put_volsw, tacna_digital_tlv),

SOC_ENUM("Input Ramp Up", tacna_in_vi_ramp),
SOC_ENUM("Input Ramp Down", tacna_in_vd_ramp),

TACNA_RATE_ENUM("Ultrasonic 1 Rate", tacna_us_output_rate[0]),
TACNA_RATE_ENUM("Ultrasonic 2 Rate", tacna_us_output_rate[1]),

SOC_ENUM("Ultrasonic 1 Freq", cs48l32_us_freq[0]),
SOC_ENUM("Ultrasonic 2 Freq", cs48l32_us_freq[1]),

SOC_SINGLE_TLV("Ultrasonic 1 Volume", TACNA_US1_CONTROL, TACNA_US1_GAIN_SHIFT,
	       3, 0, tacna_us_tlv),
SOC_SINGLE_TLV("Ultrasonic 2 Volume", TACNA_US2_CONTROL, TACNA_US2_GAIN_SHIFT,
	       3, 0, tacna_us_tlv),

SOC_ENUM("Ultrasonic 1 Activity Detect Threshold", tacna_us_det_thr[0]),
SOC_ENUM("Ultrasonic 2 Activity Detect Threshold", tacna_us_det_thr[1]),

SOC_ENUM("Ultrasonic 1 Activity Detect Pulse Length", tacna_us_det_num[0]),
SOC_ENUM("Ultrasonic 2 Activity Detect Pulse Length", tacna_us_det_num[1]),

SOC_ENUM("Ultrasonic 1 Activity Detect Hold", tacna_us_det_hold[0]),
SOC_ENUM("Ultrasonic 2 Activity Detect Hold", tacna_us_det_hold[1]),

SOC_ENUM("Ultrasonic 1 Activity Detect Decay", cs48l32_us_det_dcy[0]),
SOC_ENUM("Ultrasonic 2 Activity Detect Decay", cs48l32_us_det_dcy[1]),

SOC_SINGLE("Ultrasonic 1 Activity Detect LPF Switch",
	   TACNA_US1_DET_CONTROL, TACNA_US1_DET_LPF_SHIFT, 1, 0),
SOC_SINGLE("Ultrasonic 2 Activity Detect LPF Switch",
	   TACNA_US2_DET_CONTROL, TACNA_US2_DET_LPF_SHIFT, 1, 0),

SOC_ENUM("Ultrasonic 1 Activity Detect LPF Cut-off", cs48l32_us_det_lpf_cut[0]),
SOC_ENUM("Ultrasonic 2 Activity Detect LPF Cut-off", cs48l32_us_det_lpf_cut[1]),

TACNA_MIXER_CONTROLS("EQ1", TACNA_EQ1_INPUT1),
TACNA_MIXER_CONTROLS("EQ2", TACNA_EQ2_INPUT1),
TACNA_MIXER_CONTROLS("EQ3", TACNA_EQ3_INPUT1),
TACNA_MIXER_CONTROLS("EQ4", TACNA_EQ4_INPUT1),

SOC_ENUM_EXT("EQ1 Mode", tacna_eq_mode[0], tacna_eq_mode_get,
	     tacna_eq_mode_put),

TACNA_EQ_COEFF_CONTROLS(EQ1),

SOC_SINGLE_TLV("EQ1 B1 Volume", TACNA_EQ1_GAIN1, TACNA_EQ1_B1_GAIN_SHIFT,
	       24, 0, tacna_eq_tlv),
SOC_SINGLE_TLV("EQ1 B2 Volume", TACNA_EQ1_GAIN1, TACNA_EQ1_B2_GAIN_SHIFT,
	       24, 0, tacna_eq_tlv),
SOC_SINGLE_TLV("EQ1 B3 Volume", TACNA_EQ1_GAIN1, TACNA_EQ1_B3_GAIN_SHIFT,
	       24, 0, tacna_eq_tlv),
SOC_SINGLE_TLV("EQ1 B4 Volume", TACNA_EQ1_GAIN1, TACNA_EQ1_B4_GAIN_SHIFT,
	       24, 0, tacna_eq_tlv),
SOC_SINGLE_TLV("EQ1 B5 Volume", TACNA_EQ1_GAIN2, TACNA_EQ1_B5_GAIN_SHIFT,
	       24, 0, tacna_eq_tlv),

SOC_ENUM_EXT("EQ2 Mode", tacna_eq_mode[1], tacna_eq_mode_get,
	     tacna_eq_mode_put),
TACNA_EQ_COEFF_CONTROLS(EQ2),
SOC_SINGLE_TLV("EQ2 B1 Volume", TACNA_EQ2_GAIN1, TACNA_EQ2_B1_GAIN_SHIFT,
	       24, 0, tacna_eq_tlv),
SOC_SINGLE_TLV("EQ2 B2 Volume", TACNA_EQ2_GAIN1, TACNA_EQ2_B2_GAIN_SHIFT,
	       24, 0, tacna_eq_tlv),
SOC_SINGLE_TLV("EQ2 B3 Volume", TACNA_EQ2_GAIN1, TACNA_EQ2_B3_GAIN_SHIFT,
	       24, 0, tacna_eq_tlv),
SOC_SINGLE_TLV("EQ2 B4 Volume", TACNA_EQ2_GAIN1, TACNA_EQ2_B4_GAIN_SHIFT,
	       24, 0, tacna_eq_tlv),
SOC_SINGLE_TLV("EQ2 B5 Volume", TACNA_EQ2_GAIN2, TACNA_EQ2_B5_GAIN_SHIFT,
	       24, 0, tacna_eq_tlv),

SOC_ENUM_EXT("EQ3 Mode", tacna_eq_mode[2], tacna_eq_mode_get,
	     tacna_eq_mode_put),
TACNA_EQ_COEFF_CONTROLS(EQ3),
SOC_SINGLE_TLV("EQ3 B1 Volume", TACNA_EQ3_GAIN1, TACNA_EQ3_B1_GAIN_SHIFT,
	       24, 0, tacna_eq_tlv),
SOC_SINGLE_TLV("EQ3 B2 Volume", TACNA_EQ3_GAIN1, TACNA_EQ3_B2_GAIN_SHIFT,
	       24, 0, tacna_eq_tlv),
SOC_SINGLE_TLV("EQ3 B3 Volume", TACNA_EQ3_GAIN1, TACNA_EQ3_B3_GAIN_SHIFT,
	       24, 0, tacna_eq_tlv),
SOC_SINGLE_TLV("EQ3 B4 Volume", TACNA_EQ3_GAIN1, TACNA_EQ3_B4_GAIN_SHIFT,
	       24, 0, tacna_eq_tlv),
SOC_SINGLE_TLV("EQ3 B5 Volume", TACNA_EQ3_GAIN2, TACNA_EQ3_B5_GAIN_SHIFT,
	       24, 0, tacna_eq_tlv),

SOC_ENUM_EXT("EQ4 Mode", tacna_eq_mode[3], tacna_eq_mode_get,
	     tacna_eq_mode_put),
TACNA_EQ_COEFF_CONTROLS(EQ4),
SOC_SINGLE_TLV("EQ4 B1 Volume", TACNA_EQ4_GAIN1, TACNA_EQ4_B1_GAIN_SHIFT,
	       24, 0, tacna_eq_tlv),
SOC_SINGLE_TLV("EQ4 B2 Volume", TACNA_EQ4_GAIN1, TACNA_EQ4_B2_GAIN_SHIFT,
	       24, 0, tacna_eq_tlv),
SOC_SINGLE_TLV("EQ4 B3 Volume", TACNA_EQ4_GAIN1, TACNA_EQ4_B3_GAIN_SHIFT,
	       24, 0, tacna_eq_tlv),
SOC_SINGLE_TLV("EQ4 B4 Volume", TACNA_EQ4_GAIN1, TACNA_EQ4_B4_GAIN_SHIFT,
	       24, 0, tacna_eq_tlv),
SOC_SINGLE_TLV("EQ4 B5 Volume", TACNA_EQ4_GAIN2, TACNA_EQ4_B5_GAIN_SHIFT,
	       24, 0, tacna_eq_tlv),

TACNA_MIXER_CONTROLS("DRC1L", TACNA_DRC1L_INPUT1),
TACNA_MIXER_CONTROLS("DRC1R", TACNA_DRC1R_INPUT1),
TACNA_MIXER_CONTROLS("DRC2L", TACNA_DRC2L_INPUT1),
TACNA_MIXER_CONTROLS("DRC2R", TACNA_DRC2R_INPUT1),

SND_SOC_BYTES_MASK("DRC1 Coefficients", TACNA_DRC1_CONTROL1, 4,
		   TACNA_DRC1R_EN | TACNA_DRC1L_EN),
SND_SOC_BYTES_MASK("DRC2 Coefficients", TACNA_DRC2_CONTROL1, 4,
		   TACNA_DRC2R_EN | TACNA_DRC2L_EN),

TACNA_MIXER_CONTROLS("LHPF1", TACNA_LHPF1_INPUT1),
TACNA_MIXER_CONTROLS("LHPF2", TACNA_LHPF2_INPUT1),
TACNA_MIXER_CONTROLS("LHPF3", TACNA_LHPF3_INPUT1),
TACNA_MIXER_CONTROLS("LHPF4", TACNA_LHPF4_INPUT1),

TACNA_LHPF_CONTROL("LHPF1 Coefficients", TACNA_LHPF1_COEFF),
TACNA_LHPF_CONTROL("LHPF2 Coefficients", TACNA_LHPF2_COEFF),
TACNA_LHPF_CONTROL("LHPF3 Coefficients", TACNA_LHPF3_COEFF),
TACNA_LHPF_CONTROL("LHPF4 Coefficients", TACNA_LHPF4_COEFF),

SOC_ENUM("LHPF1 Mode", tacna_lhpf1_mode),
SOC_ENUM("LHPF2 Mode", tacna_lhpf2_mode),
SOC_ENUM("LHPF3 Mode", tacna_lhpf3_mode),
SOC_ENUM("LHPF4 Mode", tacna_lhpf4_mode),

TACNA_RATE_CONTROL("Sample Rate 1", 1),
TACNA_RATE_CONTROL("Sample Rate 2", 2),
TACNA_RATE_CONTROL("Sample Rate 3", 3),
TACNA_RATE_CONTROL("Sample Rate 4", 4),

TACNA_RATE_ENUM("FX Rate", tacna_fx_rate),

TACNA_RATE_ENUM("ISRC1 FSL", tacna_isrc_fsl[0]),
TACNA_RATE_ENUM("ISRC2 FSL", tacna_isrc_fsl[1]),
TACNA_RATE_ENUM("ISRC3 FSL", tacna_isrc_fsl[2]),
TACNA_RATE_ENUM("ISRC1 FSH", tacna_isrc_fsh[0]),
TACNA_RATE_ENUM("ISRC2 FSH", tacna_isrc_fsh[1]),
TACNA_RATE_ENUM("ISRC3 FSH", tacna_isrc_fsh[2]),

SOC_ENUM("AUXPDM1 Rate", tacna_auxpdm1_freq),
SOC_ENUM("AUXPDM2 Rate", tacna_auxpdm2_freq),

SOC_ENUM_EXT("IN1L Rate", tacna_input_rate[0],
	     snd_soc_get_enum_double, tacna_in_rate_put),
SOC_ENUM_EXT("IN1R Rate", tacna_input_rate[1],
	     snd_soc_get_enum_double, tacna_in_rate_put),
SOC_ENUM_EXT("IN2L Rate", tacna_input_rate[2],
	     snd_soc_get_enum_double, tacna_in_rate_put),
SOC_ENUM_EXT("IN2R Rate", tacna_input_rate[3],
	     snd_soc_get_enum_double, tacna_in_rate_put),

SOC_SINGLE_TLV("Noise Generator Volume", TACNA_COMFORT_NOISE_GENERATOR,
	       TACNA_NOISE_GEN_GAIN_SHIFT, 0x12, 0, tacna_noise_tlv),

TACNA_MIXER_CONTROLS("ASP1TX1", TACNA_ASP1TX1_INPUT1),
TACNA_MIXER_CONTROLS("ASP1TX2", TACNA_ASP1TX2_INPUT1),
TACNA_MIXER_CONTROLS("ASP1TX3", TACNA_ASP1TX3_INPUT1),
TACNA_MIXER_CONTROLS("ASP1TX4", TACNA_ASP1TX4_INPUT1),
TACNA_MIXER_CONTROLS("ASP1TX5", TACNA_ASP1TX5_INPUT1),
TACNA_MIXER_CONTROLS("ASP1TX6", TACNA_ASP1TX6_INPUT1),
TACNA_MIXER_CONTROLS("ASP1TX7", TACNA_ASP1TX7_INPUT1),
TACNA_MIXER_CONTROLS("ASP1TX8", TACNA_ASP1TX8_INPUT1),

TACNA_MIXER_CONTROLS("ASP2TX1", TACNA_ASP2TX1_INPUT1),
TACNA_MIXER_CONTROLS("ASP2TX2", TACNA_ASP2TX2_INPUT1),
TACNA_MIXER_CONTROLS("ASP2TX3", TACNA_ASP2TX3_INPUT1),
TACNA_MIXER_CONTROLS("ASP2TX4", TACNA_ASP2TX4_INPUT1),

WM_ADSP2_PRELOAD_SWITCH("DSP1", 1),

TACNA_MIXER_CONTROLS("DSP1RX1", TACNA_DSP1RX1_INPUT1),
TACNA_MIXER_CONTROLS("DSP1RX2", TACNA_DSP1RX2_INPUT1),
TACNA_MIXER_CONTROLS("DSP1RX3", TACNA_DSP1RX3_INPUT1),
TACNA_MIXER_CONTROLS("DSP1RX4", TACNA_DSP1RX4_INPUT1),
TACNA_MIXER_CONTROLS("DSP1RX5", TACNA_DSP1RX5_INPUT1),
TACNA_MIXER_CONTROLS("DSP1RX6", TACNA_DSP1RX6_INPUT1),
TACNA_MIXER_CONTROLS("DSP1RX7", TACNA_DSP1RX7_INPUT1),
TACNA_MIXER_CONTROLS("DSP1RX8", TACNA_DSP1RX8_INPUT1),

WM_ADSP_FW_CONTROL("DSP1", 0),
};

TACNA_MIXER_ENUMS(EQ1, TACNA_EQ1_INPUT1);
TACNA_MIXER_ENUMS(EQ2, TACNA_EQ2_INPUT1);
TACNA_MIXER_ENUMS(EQ3, TACNA_EQ3_INPUT1);
TACNA_MIXER_ENUMS(EQ4, TACNA_EQ4_INPUT1);

TACNA_MIXER_ENUMS(DRC1L, TACNA_DRC1L_INPUT1);
TACNA_MIXER_ENUMS(DRC1R, TACNA_DRC1R_INPUT1);
TACNA_MIXER_ENUMS(DRC2L, TACNA_DRC2L_INPUT1);
TACNA_MIXER_ENUMS(DRC2R, TACNA_DRC2R_INPUT1);

TACNA_MIXER_ENUMS(LHPF1, TACNA_LHPF1_INPUT1);
TACNA_MIXER_ENUMS(LHPF2, TACNA_LHPF2_INPUT1);
TACNA_MIXER_ENUMS(LHPF3, TACNA_LHPF3_INPUT1);
TACNA_MIXER_ENUMS(LHPF4, TACNA_LHPF4_INPUT1);

TACNA_MIXER_ENUMS(ASP1TX1, TACNA_ASP1TX1_INPUT1);
TACNA_MIXER_ENUMS(ASP1TX2, TACNA_ASP1TX2_INPUT1);
TACNA_MIXER_ENUMS(ASP1TX3, TACNA_ASP1TX3_INPUT1);
TACNA_MIXER_ENUMS(ASP1TX4, TACNA_ASP1TX4_INPUT1);
TACNA_MIXER_ENUMS(ASP1TX5, TACNA_ASP1TX5_INPUT1);
TACNA_MIXER_ENUMS(ASP1TX6, TACNA_ASP1TX6_INPUT1);
TACNA_MIXER_ENUMS(ASP1TX7, TACNA_ASP1TX7_INPUT1);
TACNA_MIXER_ENUMS(ASP1TX8, TACNA_ASP1TX8_INPUT1);

TACNA_MIXER_ENUMS(ASP2TX1, TACNA_ASP2TX1_INPUT1);
TACNA_MIXER_ENUMS(ASP2TX2, TACNA_ASP2TX2_INPUT1);
TACNA_MIXER_ENUMS(ASP2TX3, TACNA_ASP2TX3_INPUT1);
TACNA_MIXER_ENUMS(ASP2TX4, TACNA_ASP2TX4_INPUT1);

TACNA_MUX_ENUMS(ISRC1INT1, TACNA_ISRC1INT1_INPUT1);
TACNA_MUX_ENUMS(ISRC1INT2, TACNA_ISRC1INT2_INPUT1);
TACNA_MUX_ENUMS(ISRC1INT3, TACNA_ISRC1INT3_INPUT1);
TACNA_MUX_ENUMS(ISRC1INT4, TACNA_ISRC1INT4_INPUT1);

TACNA_MUX_ENUMS(ISRC1DEC1, TACNA_ISRC1DEC1_INPUT1);
TACNA_MUX_ENUMS(ISRC1DEC2, TACNA_ISRC1DEC2_INPUT1);
TACNA_MUX_ENUMS(ISRC1DEC3, TACNA_ISRC1DEC3_INPUT1);
TACNA_MUX_ENUMS(ISRC1DEC4, TACNA_ISRC1DEC4_INPUT1);

TACNA_MUX_ENUMS(ISRC2INT1, TACNA_ISRC2INT1_INPUT1);
TACNA_MUX_ENUMS(ISRC2INT2, TACNA_ISRC2INT2_INPUT1);

TACNA_MUX_ENUMS(ISRC2DEC1, TACNA_ISRC2DEC1_INPUT1);
TACNA_MUX_ENUMS(ISRC2DEC2, TACNA_ISRC2DEC2_INPUT1);

TACNA_MUX_ENUMS(ISRC3INT1, TACNA_ISRC3INT1_INPUT1);
TACNA_MUX_ENUMS(ISRC3INT2, TACNA_ISRC3INT2_INPUT1);

TACNA_MUX_ENUMS(ISRC3DEC1, TACNA_ISRC3DEC1_INPUT1);
TACNA_MUX_ENUMS(ISRC3DEC2, TACNA_ISRC3DEC2_INPUT1);

TACNA_MIXER_ENUMS(DSP1RX1, TACNA_DSP1RX1_INPUT1);
TACNA_MIXER_ENUMS(DSP1RX2, TACNA_DSP1RX2_INPUT1);
TACNA_MIXER_ENUMS(DSP1RX3, TACNA_DSP1RX3_INPUT1);
TACNA_MIXER_ENUMS(DSP1RX4, TACNA_DSP1RX4_INPUT1);
TACNA_MIXER_ENUMS(DSP1RX5, TACNA_DSP1RX5_INPUT1);
TACNA_MIXER_ENUMS(DSP1RX6, TACNA_DSP1RX6_INPUT1);
TACNA_MIXER_ENUMS(DSP1RX7, TACNA_DSP1RX7_INPUT1);
TACNA_MIXER_ENUMS(DSP1RX8, TACNA_DSP1RX8_INPUT1);

static int cs48l32_dsp_mem_ev(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *comp = snd_soc_dapm_to_component(w->dapm);
	struct tacna_priv *priv = snd_soc_component_get_drvdata(comp);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		return tacna_dsp_memory_enable(priv, &cs48l32_dsp_sram_regs);
	case SND_SOC_DAPM_PRE_PMD:
		tacna_dsp_memory_disable(priv, &cs48l32_dsp_sram_regs);
		return 0;
	default:
		return 0;
	}
}

static int cs48l32_dsp_freq_ev(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol, int event)
{
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		return tacna_dsp_freq_update(w, TACNA_SYSTEM_CLOCK2,
					     TACNA_SYSTEM_CLOCK1);
	default:
		return 0;
	}
}

static const struct snd_soc_dapm_widget cs48l32_dapm_widgets[] = {
SND_SOC_DAPM_SUPPLY("SYSCLK", TACNA_SYSTEM_CLOCK1, TACNA_SYSCLK_EN_SHIFT,
		    0, tacna_sysclk_ev,
		    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
SND_SOC_DAPM_SUPPLY("OPCLK", TACNA_OUTPUT_SYS_CLK, TACNA_OPCLK_EN_SHIFT,
		    0, NULL, 0),

SND_SOC_DAPM_REGULATOR_SUPPLY("VDD1_CP", 20, 0),
SND_SOC_DAPM_REGULATOR_SUPPLY("VOUT_MIC", 0, SND_SOC_DAPM_REGULATOR_BYPASS),

SND_SOC_DAPM_SUPPLY("MICBIAS1", TACNA_MICBIAS_CTRL1, TACNA_MICB1_EN_SHIFT,
		    0, NULL, 0),

SND_SOC_DAPM_SUPPLY("MICBIAS1A", TACNA_MICBIAS_CTRL5, TACNA_MICB1A_EN_SHIFT,
		    0, NULL, 0),
SND_SOC_DAPM_SUPPLY("MICBIAS1B", TACNA_MICBIAS_CTRL5, TACNA_MICB1B_EN_SHIFT,
		    0, NULL, 0),
SND_SOC_DAPM_SUPPLY("MICBIAS1C", TACNA_MICBIAS_CTRL5, TACNA_MICB1C_EN_SHIFT,
		    0, NULL, 0),

SND_SOC_DAPM_SUPPLY("DSP1MEM", SND_SOC_NOPM, 0, 0, cs48l32_dsp_mem_ev,
		    SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

TACNA_DSP_FREQ_WIDGET_EV("DSP1", 0, cs48l32_dsp_freq_ev),

SND_SOC_DAPM_SIGGEN("TONE"),
SND_SOC_DAPM_SIGGEN("NOISE"),

SND_SOC_DAPM_INPUT("IN1LN_1"),
SND_SOC_DAPM_INPUT("IN1LN_2"),
SND_SOC_DAPM_INPUT("IN1LP_1"),
SND_SOC_DAPM_INPUT("IN1LP_2"),
SND_SOC_DAPM_INPUT("IN1RN_1"),
SND_SOC_DAPM_INPUT("IN1RN_2"),
SND_SOC_DAPM_INPUT("IN1RP_1"),
SND_SOC_DAPM_INPUT("IN1RP_2"),
SND_SOC_DAPM_INPUT("IN1_PDMCLK"),
SND_SOC_DAPM_INPUT("IN1_PDMDATA"),

SND_SOC_DAPM_INPUT("IN2_PDMCLK"),
SND_SOC_DAPM_INPUT("IN2_PDMDATA"),

SND_SOC_DAPM_MUX("Ultrasonic 1 Input", SND_SOC_NOPM,
		 0, 0, &cs48l32_us_inmux[0]),
SND_SOC_DAPM_MUX("Ultrasonic 2 Input", SND_SOC_NOPM,
		 0, 0, &cs48l32_us_inmux[1]),

SND_SOC_DAPM_OUTPUT("DRC1 Signal Activity"),
SND_SOC_DAPM_OUTPUT("DRC2 Signal Activity"),

SND_SOC_DAPM_OUTPUT("DSP Trigger Out"),

SND_SOC_DAPM_MUX("IN1L Mux", SND_SOC_NOPM, 0, 0, &tacna_inmux[0]),
SND_SOC_DAPM_MUX("IN1R Mux", SND_SOC_NOPM, 0, 0, &tacna_inmux[1]),

SND_SOC_DAPM_MUX("IN1L Mode", SND_SOC_NOPM, 0, 0, &cs48l32_dmode_mux[0]),
SND_SOC_DAPM_MUX("IN1R Mode", SND_SOC_NOPM, 0, 0, &cs48l32_dmode_mux[0]),

SND_SOC_DAPM_AIF_OUT("ASP1TX1", NULL, 0, TACNA_ASP1_ENABLES1,
		     TACNA_ASP1_TX1_EN_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("ASP1TX2", NULL, 0, TACNA_ASP1_ENABLES1,
		     TACNA_ASP1_TX2_EN_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("ASP1TX3", NULL, 0, TACNA_ASP1_ENABLES1,
		     TACNA_ASP1_TX3_EN_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("ASP1TX4", NULL, 0, TACNA_ASP1_ENABLES1,
		     TACNA_ASP1_TX4_EN_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("ASP1TX5", NULL, 0, TACNA_ASP1_ENABLES1,
		     TACNA_ASP1_TX5_EN_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("ASP1TX6", NULL, 0, TACNA_ASP1_ENABLES1,
		     TACNA_ASP1_TX6_EN_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("ASP1TX7", NULL, 0, TACNA_ASP1_ENABLES1,
		     TACNA_ASP1_TX7_EN_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("ASP1TX8", NULL, 0, TACNA_ASP1_ENABLES1,
		     TACNA_ASP1_TX8_EN_SHIFT, 0),

SND_SOC_DAPM_AIF_OUT("ASP2TX1", NULL, 0, TACNA_ASP2_ENABLES1,
		     TACNA_ASP2_TX1_EN_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("ASP2TX2", NULL, 0, TACNA_ASP2_ENABLES1,
		     TACNA_ASP2_TX2_EN_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("ASP2TX3", NULL, 0, TACNA_ASP2_ENABLES1,
		     TACNA_ASP2_TX3_EN_SHIFT, 0),
SND_SOC_DAPM_AIF_OUT("ASP2TX4", NULL, 0, TACNA_ASP2_ENABLES1,
		     TACNA_ASP2_TX4_EN_SHIFT, 0),

SND_SOC_DAPM_SWITCH("AUXPDM1 Output", TACNA_AUXPDM_CONTROL1,
		    TACNA_AUXPDM1_EN_SHIFT, 0, &tacna_auxpdm_switch[0]),
SND_SOC_DAPM_SWITCH("AUXPDM2 Output", TACNA_AUXPDM_CONTROL1,
		    TACNA_AUXPDM2_EN_SHIFT, 0, &tacna_auxpdm_switch[1]),

SND_SOC_DAPM_MUX("AUXPDM1 Input", SND_SOC_NOPM, 0, 0,
		 &cs48l32_auxpdm_inmux[0]),
SND_SOC_DAPM_MUX("AUXPDM2 Input", SND_SOC_NOPM, 0, 0,
		 &cs48l32_auxpdm_inmux[1]),

SND_SOC_DAPM_MUX("AUXPDM1 Analog Input", SND_SOC_NOPM, 0, 0,
		 &cs48l32_auxpdm_analog_inmux[0]),
SND_SOC_DAPM_MUX("AUXPDM2 Analog Input", SND_SOC_NOPM, 0, 0,
		 &cs48l32_auxpdm_analog_inmux[1]),

SND_SOC_DAPM_SWITCH("Ultrasonic 1 Activity Detect", TACNA_US_CONTROL,
		    TACNA_US1_DET_EN_SHIFT, 0, &tacna_us_switch[0]),
SND_SOC_DAPM_SWITCH("Ultrasonic 2 Activity Detect", TACNA_US_CONTROL,
		    TACNA_US2_DET_EN_SHIFT, 0, &tacna_us_switch[1]),

/* mux_in widgets : arranged in the order of sources
 * specified in TACNA_MIXER_INPUT_ROUTES
 */

SND_SOC_DAPM_PGA("Tone Generator 1", TACNA_TONE_GENERATOR1,
		 TACNA_TONE1_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("Tone Generator 2", TACNA_TONE_GENERATOR1,
		 TACNA_TONE2_EN_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("Noise Generator", TACNA_COMFORT_NOISE_GENERATOR,
		 TACNA_NOISE_GEN_EN_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA_E("IN1L PGA", TACNA_INPUT_CONTROL, TACNA_IN1L_EN_SHIFT,
		   0, NULL, 0, cs48l32_in_ev,
		   SND_SOC_DAPM_PRE_PMD |
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("IN1R PGA", TACNA_INPUT_CONTROL, TACNA_IN1R_EN_SHIFT,
		   0, NULL, 0, cs48l32_in_ev,
		   SND_SOC_DAPM_PRE_PMD |
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("IN2L PGA", TACNA_INPUT_CONTROL, TACNA_IN2L_EN_SHIFT,
		   0, NULL, 0, tacna_in_ev,
		   SND_SOC_DAPM_PRE_PMD |
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("IN2R PGA", TACNA_INPUT_CONTROL, TACNA_IN2R_EN_SHIFT,
		   0, NULL, 0, tacna_in_ev,
		   SND_SOC_DAPM_PRE_PMD |
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU),

SND_SOC_DAPM_AIF_IN("ASP1RX1", NULL, 0, TACNA_ASP1_ENABLES1,
		    TACNA_ASP1_RX1_EN_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("ASP1RX2", NULL, 0, TACNA_ASP1_ENABLES1,
		    TACNA_ASP1_RX2_EN_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("ASP1RX3", NULL, 0, TACNA_ASP1_ENABLES1,
		    TACNA_ASP1_RX3_EN_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("ASP1RX4", NULL, 0, TACNA_ASP1_ENABLES1,
		    TACNA_ASP1_RX4_EN_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("ASP1RX5", NULL, 0, TACNA_ASP1_ENABLES1,
		    TACNA_ASP1_RX5_EN_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("ASP1RX6", NULL, 0, TACNA_ASP1_ENABLES1,
		    TACNA_ASP1_RX6_EN_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("ASP1RX7", NULL, 0, TACNA_ASP1_ENABLES1,
		    TACNA_ASP1_RX7_EN_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("ASP1RX8", NULL, 0, TACNA_ASP1_ENABLES1,
		    TACNA_ASP1_RX8_EN_SHIFT, 0),

SND_SOC_DAPM_AIF_IN("ASP2RX1", NULL, 0, TACNA_ASP2_ENABLES1,
		    TACNA_ASP2_RX1_EN_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("ASP2RX2", NULL, 0, TACNA_ASP2_ENABLES1,
		    TACNA_ASP2_RX2_EN_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("ASP2RX3", NULL, 0, TACNA_ASP2_ENABLES1,
		    TACNA_ASP2_RX3_EN_SHIFT, 0),
SND_SOC_DAPM_AIF_IN("ASP2RX4", NULL, 0, TACNA_ASP2_ENABLES1,
		    TACNA_ASP2_RX4_EN_SHIFT, 0),

SND_SOC_DAPM_PGA("ISRC1DEC1", TACNA_ISRC1_CONTROL2,
		 TACNA_ISRC1_DEC1_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC1DEC2", TACNA_ISRC1_CONTROL2,
		 TACNA_ISRC1_DEC2_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC1DEC3", TACNA_ISRC1_CONTROL2,
		 TACNA_ISRC1_DEC3_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC1DEC4", TACNA_ISRC1_CONTROL2,
		 TACNA_ISRC1_DEC4_EN_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("ISRC1INT1", TACNA_ISRC1_CONTROL2,
		 TACNA_ISRC1_INT1_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC1INT2", TACNA_ISRC1_CONTROL2,
		 TACNA_ISRC1_INT2_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC1INT3", TACNA_ISRC1_CONTROL2,
		 TACNA_ISRC1_INT3_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC1INT4", TACNA_ISRC1_CONTROL2,
		 TACNA_ISRC1_INT4_EN_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("ISRC2DEC1", TACNA_ISRC2_CONTROL2,
		 TACNA_ISRC2_DEC1_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC2DEC2", TACNA_ISRC2_CONTROL2,
		 TACNA_ISRC2_DEC2_EN_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("ISRC2INT1", TACNA_ISRC2_CONTROL2,
		 TACNA_ISRC2_INT1_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC2INT2", TACNA_ISRC2_CONTROL2,
		 TACNA_ISRC2_INT2_EN_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("ISRC3DEC1", TACNA_ISRC3_CONTROL2,
		 TACNA_ISRC3_DEC1_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC3DEC2", TACNA_ISRC3_CONTROL2,
		 TACNA_ISRC3_DEC2_EN_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA("ISRC3INT1", TACNA_ISRC3_CONTROL2,
		 TACNA_ISRC3_INT1_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("ISRC3INT2", TACNA_ISRC3_CONTROL2,
		 TACNA_ISRC3_INT2_EN_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_PGA_E("EQ1", TACNA_EQ_CONTROL1, TACNA_EQ1_EN_SHIFT, 0, NULL, 0,
		   tacna_eq_ev, SND_SOC_DAPM_PRE_PMU),
SND_SOC_DAPM_PGA_E("EQ2", TACNA_EQ_CONTROL1, TACNA_EQ2_EN_SHIFT, 0, NULL, 0,
		   tacna_eq_ev, SND_SOC_DAPM_PRE_PMU),
SND_SOC_DAPM_PGA_E("EQ3", TACNA_EQ_CONTROL1, TACNA_EQ3_EN_SHIFT, 0, NULL, 0,
		   tacna_eq_ev, SND_SOC_DAPM_PRE_PMU),
SND_SOC_DAPM_PGA_E("EQ4", TACNA_EQ_CONTROL1, TACNA_EQ4_EN_SHIFT, 0, NULL, 0,
		   tacna_eq_ev, SND_SOC_DAPM_PRE_PMU),

SND_SOC_DAPM_PGA("DRC1L", TACNA_DRC1_CONTROL1, TACNA_DRC1L_EN_SHIFT, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("DRC1R", TACNA_DRC1_CONTROL1, TACNA_DRC1R_EN_SHIFT, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("DRC2L", TACNA_DRC2_CONTROL1, TACNA_DRC2L_EN_SHIFT, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("DRC2R", TACNA_DRC2_CONTROL1, TACNA_DRC2R_EN_SHIFT, 0,
		 NULL, 0),

SND_SOC_DAPM_PGA("LHPF1", TACNA_LHPF_CONTROL1, TACNA_LHPF1_EN_SHIFT, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("LHPF2", TACNA_LHPF_CONTROL1, TACNA_LHPF2_EN_SHIFT, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("LHPF3", TACNA_LHPF_CONTROL1, TACNA_LHPF3_EN_SHIFT, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("LHPF4", TACNA_LHPF_CONTROL1, TACNA_LHPF4_EN_SHIFT, 0,
		 NULL, 0),

SND_SOC_DAPM_PGA("Ultrasonic 1", TACNA_US_CONTROL,
		 TACNA_US1_EN_SHIFT, 0, NULL, 0),
SND_SOC_DAPM_PGA("Ultrasonic 2", TACNA_US_CONTROL,
		 TACNA_US2_EN_SHIFT, 0, NULL, 0),

WM_HALO("DSP1", 0, wm_adsp_early_event),

/* end of ordered widget list */

TACNA_MIXER_WIDGETS(EQ1, "EQ1"),
TACNA_MIXER_WIDGETS(EQ2, "EQ2"),
TACNA_MIXER_WIDGETS(EQ3, "EQ3"),
TACNA_MIXER_WIDGETS(EQ4, "EQ4"),

TACNA_MIXER_WIDGETS(DRC1L, "DRC1L"),
TACNA_MIXER_WIDGETS(DRC1R, "DRC1R"),
TACNA_MIXER_WIDGETS(DRC2L, "DRC2L"),
TACNA_MIXER_WIDGETS(DRC2R, "DRC2R"),

SND_SOC_DAPM_SWITCH("DRC1 Activity Output", SND_SOC_NOPM, 0, 0,
		    &tacna_drc_activity_output_mux[0]),
SND_SOC_DAPM_SWITCH("DRC2 Activity Output", SND_SOC_NOPM, 0, 0,
		    &tacna_drc_activity_output_mux[1]),

TACNA_MIXER_WIDGETS(LHPF1, "LHPF1"),
TACNA_MIXER_WIDGETS(LHPF2, "LHPF2"),
TACNA_MIXER_WIDGETS(LHPF3, "LHPF3"),
TACNA_MIXER_WIDGETS(LHPF4, "LHPF4"),

TACNA_MIXER_WIDGETS(ASP1TX1, "ASP1TX1"),
TACNA_MIXER_WIDGETS(ASP1TX2, "ASP1TX2"),
TACNA_MIXER_WIDGETS(ASP1TX3, "ASP1TX3"),
TACNA_MIXER_WIDGETS(ASP1TX4, "ASP1TX4"),
TACNA_MIXER_WIDGETS(ASP1TX5, "ASP1TX5"),
TACNA_MIXER_WIDGETS(ASP1TX6, "ASP1TX6"),
TACNA_MIXER_WIDGETS(ASP1TX7, "ASP1TX7"),
TACNA_MIXER_WIDGETS(ASP1TX8, "ASP1TX8"),

TACNA_MIXER_WIDGETS(ASP2TX1, "ASP2TX1"),
TACNA_MIXER_WIDGETS(ASP2TX2, "ASP2TX2"),
TACNA_MIXER_WIDGETS(ASP2TX3, "ASP2TX3"),
TACNA_MIXER_WIDGETS(ASP2TX4, "ASP2TX4"),

TACNA_MUX_WIDGETS(ISRC1DEC1, "ISRC1DEC1"),
TACNA_MUX_WIDGETS(ISRC1DEC2, "ISRC1DEC2"),
TACNA_MUX_WIDGETS(ISRC1DEC3, "ISRC1DEC3"),
TACNA_MUX_WIDGETS(ISRC1DEC4, "ISRC1DEC4"),

TACNA_MUX_WIDGETS(ISRC1INT1, "ISRC1INT1"),
TACNA_MUX_WIDGETS(ISRC1INT2, "ISRC1INT2"),
TACNA_MUX_WIDGETS(ISRC1INT3, "ISRC1INT3"),
TACNA_MUX_WIDGETS(ISRC1INT4, "ISRC1INT4"),

TACNA_MUX_WIDGETS(ISRC2DEC1, "ISRC2DEC1"),
TACNA_MUX_WIDGETS(ISRC2DEC2, "ISRC2DEC2"),

TACNA_MUX_WIDGETS(ISRC2INT1, "ISRC2INT1"),
TACNA_MUX_WIDGETS(ISRC2INT2, "ISRC2INT2"),

TACNA_MUX_WIDGETS(ISRC3DEC1, "ISRC3DEC1"),
TACNA_MUX_WIDGETS(ISRC3DEC2, "ISRC3DEC2"),

TACNA_MUX_WIDGETS(ISRC3INT1, "ISRC3INT1"),
TACNA_MUX_WIDGETS(ISRC3INT2, "ISRC3INT2"),

TACNA_MIXER_WIDGETS(DSP1RX1, "DSP1RX1"),
TACNA_MIXER_WIDGETS(DSP1RX2, "DSP1RX2"),
TACNA_MIXER_WIDGETS(DSP1RX3, "DSP1RX3"),
TACNA_MIXER_WIDGETS(DSP1RX4, "DSP1RX4"),
TACNA_MIXER_WIDGETS(DSP1RX5, "DSP1RX5"),
TACNA_MIXER_WIDGETS(DSP1RX6, "DSP1RX6"),
TACNA_MIXER_WIDGETS(DSP1RX7, "DSP1RX7"),
TACNA_MIXER_WIDGETS(DSP1RX8, "DSP1RX8"),

SND_SOC_DAPM_SWITCH("DSP1 Trigger Output", SND_SOC_NOPM, 0, 0,
		    &tacna_dsp_trigger_output_mux[0]),

SND_SOC_DAPM_OUTPUT("AUXPDM1_CLK"),
SND_SOC_DAPM_OUTPUT("AUXPDM1_DOUT"),
SND_SOC_DAPM_OUTPUT("AUXPDM2_CLK"),
SND_SOC_DAPM_OUTPUT("AUXPDM2_DOUT"),

SND_SOC_DAPM_OUTPUT("MICSUPP"),

SND_SOC_DAPM_OUTPUT("Ultrasonic Dummy Output"),
};

#define TACNA_MIXER_INPUT_ROUTES(name) \
	{ name, "Tone Generator 1", "Tone Generator 1" }, \
	{ name, "Tone Generator 2", "Tone Generator 2" }, \
	{ name, "Noise Generator", "Noise Generator" }, \
	{ name, "IN1L", "IN1L PGA" }, \
	{ name, "IN1R", "IN1R PGA" }, \
	{ name, "IN2L", "IN2L PGA" }, \
	{ name, "IN2R", "IN2R PGA" }, \
	{ name, "ASP1RX1", "ASP1RX1" }, \
	{ name, "ASP1RX2", "ASP1RX2" }, \
	{ name, "ASP1RX3", "ASP1RX3" }, \
	{ name, "ASP1RX4", "ASP1RX4" }, \
	{ name, "ASP1RX5", "ASP1RX5" }, \
	{ name, "ASP1RX6", "ASP1RX6" }, \
	{ name, "ASP1RX7", "ASP1RX7" }, \
	{ name, "ASP1RX8", "ASP1RX8" }, \
	{ name, "ASP2RX1", "ASP2RX1" }, \
	{ name, "ASP2RX2", "ASP2RX2" }, \
	{ name, "ASP2RX3", "ASP2RX3" }, \
	{ name, "ASP2RX4", "ASP2RX4" }, \
	{ name, "ISRC1DEC1", "ISRC1DEC1" }, \
	{ name, "ISRC1DEC2", "ISRC1DEC2" }, \
	{ name, "ISRC1DEC3", "ISRC1DEC3" }, \
	{ name, "ISRC1DEC4", "ISRC1DEC4" }, \
	{ name, "ISRC1INT1", "ISRC1INT1" }, \
	{ name, "ISRC1INT2", "ISRC1INT2" }, \
	{ name, "ISRC1INT3", "ISRC1INT3" }, \
	{ name, "ISRC1INT4", "ISRC1INT4" }, \
	{ name, "ISRC2DEC1", "ISRC2DEC1" }, \
	{ name, "ISRC2DEC2", "ISRC2DEC2" }, \
	{ name, "ISRC2INT1", "ISRC2INT1" }, \
	{ name, "ISRC2INT2", "ISRC2INT2" }, \
	{ name, "ISRC3DEC1", "ISRC3DEC1" }, \
	{ name, "ISRC3DEC2", "ISRC3DEC2" }, \
	{ name, "ISRC3INT1", "ISRC3INT1" }, \
	{ name, "ISRC3INT2", "ISRC3INT2" }, \
	{ name, "EQ1", "EQ1" }, \
	{ name, "EQ2", "EQ2" }, \
	{ name, "EQ3", "EQ3" }, \
	{ name, "EQ4", "EQ4" }, \
	{ name, "DRC1L", "DRC1L" }, \
	{ name, "DRC1R", "DRC1R" }, \
	{ name, "DRC2L", "DRC2L" }, \
	{ name, "DRC2R", "DRC2R" }, \
	{ name, "LHPF1", "LHPF1" }, \
	{ name, "LHPF2", "LHPF2" }, \
	{ name, "LHPF3", "LHPF3" }, \
	{ name, "LHPF4", "LHPF4" }, \
	{ name, "Ultrasonic 1", "Ultrasonic 1" }, \
	{ name, "Ultrasonic 2", "Ultrasonic 2" }, \
	{ name, "DSP1.1", "DSP1" }, \
	{ name, "DSP1.2", "DSP1" }, \
	{ name, "DSP1.3", "DSP1" }, \
	{ name, "DSP1.4", "DSP1" }, \
	{ name, "DSP1.5", "DSP1" }, \
	{ name, "DSP1.6", "DSP1" }, \
	{ name, "DSP1.7", "DSP1" }, \
	{ name, "DSP1.8", "DSP1" }

static const struct snd_soc_dapm_route cs48l32_dapm_routes[] = {
	{ "OPCLK", NULL, "SYSCLK" },

	{ "IN1LN_1", NULL, "SYSCLK" },
	{ "IN1LN_2", NULL, "SYSCLK" },
	{ "IN1LP_1", NULL, "SYSCLK" },
	{ "IN1LP_2", NULL, "SYSCLK" },
	{ "IN1RN_1", NULL, "SYSCLK" },
	{ "IN1RN_2", NULL, "SYSCLK" },
	{ "IN1RP_1", NULL, "SYSCLK" },
	{ "IN1RP_2", NULL, "SYSCLK" },

	{ "IN1_PDMCLK", NULL, "SYSCLK" },
	{ "IN1_PDMDATA", NULL, "SYSCLK" },
	{ "IN2_PDMCLK", NULL, "SYSCLK" },
	{ "IN2_PDMDATA", NULL, "SYSCLK" },

	{ "DSP1 Preloader", NULL, "DSP1MEM" },
	{ "DSP1", NULL, "DSP1FREQ" },

	{ "Audio Trace DSP", NULL, "DSP1" },
	{ "Voice Ctrl DSP", NULL, "DSP1" },
	{ "Voice Ctrl 2 DSP", NULL, "DSP1" },
	{ "Voice Ctrl 3 DSP", NULL, "DSP1" },
	{ "Text Log DSP", NULL, "DSP1" },

	{ "MICBIAS1", NULL, "VOUT_MIC" },

	{ "MICBIAS1A", NULL, "MICBIAS1" },
	{ "MICBIAS1B", NULL, "MICBIAS1" },
	{ "MICBIAS1C", NULL, "MICBIAS1" },

	{ "Tone Generator 1", NULL, "SYSCLK" },
	{ "Tone Generator 2", NULL, "SYSCLK" },
	{ "Noise Generator", NULL, "SYSCLK" },

	{ "Tone Generator 1", NULL, "TONE" },
	{ "Tone Generator 2", NULL, "TONE" },
	{ "Noise Generator", NULL, "NOISE" },

	{ "ASP1 Capture", NULL, "ASP1TX1" },
	{ "ASP1 Capture", NULL, "ASP1TX2" },
	{ "ASP1 Capture", NULL, "ASP1TX3" },
	{ "ASP1 Capture", NULL, "ASP1TX4" },
	{ "ASP1 Capture", NULL, "ASP1TX5" },
	{ "ASP1 Capture", NULL, "ASP1TX6" },
	{ "ASP1 Capture", NULL, "ASP1TX7" },
	{ "ASP1 Capture", NULL, "ASP1TX8" },

	{ "ASP1RX1", NULL, "ASP1 Playback" },
	{ "ASP1RX2", NULL, "ASP1 Playback" },
	{ "ASP1RX3", NULL, "ASP1 Playback" },
	{ "ASP1RX4", NULL, "ASP1 Playback" },
	{ "ASP1RX5", NULL, "ASP1 Playback" },
	{ "ASP1RX6", NULL, "ASP1 Playback" },
	{ "ASP1RX7", NULL, "ASP1 Playback" },
	{ "ASP1RX8", NULL, "ASP1 Playback" },

	{ "ASP2 Capture", NULL, "ASP2TX1" },
	{ "ASP2 Capture", NULL, "ASP2TX2" },
	{ "ASP2 Capture", NULL, "ASP2TX3" },
	{ "ASP2 Capture", NULL, "ASP2TX4" },

	{ "ASP2RX1", NULL, "ASP2 Playback" },
	{ "ASP2RX2", NULL, "ASP2 Playback" },
	{ "ASP2RX3", NULL, "ASP2 Playback" },
	{ "ASP2RX4", NULL, "ASP2 Playback" },

	{ "ASP1 Playback", NULL, "SYSCLK" },
	{ "ASP2 Playback", NULL, "SYSCLK" },

	{ "ASP1 Capture", NULL, "SYSCLK" },
	{ "ASP2 Capture", NULL, "SYSCLK" },

	{ "IN1L Mux", "Analog 1", "IN1LN_1" },
	{ "IN1L Mux", "Analog 2", "IN1LN_2" },
	{ "IN1L Mux", "Analog 1", "IN1LP_1" },
	{ "IN1L Mux", "Analog 2", "IN1LP_2" },
	{ "IN1R Mux", "Analog 1", "IN1RN_1" },
	{ "IN1R Mux", "Analog 2", "IN1RN_2" },
	{ "IN1R Mux", "Analog 1", "IN1RP_1" },
	{ "IN1R Mux", "Analog 2", "IN1RP_2" },

	{ "IN1L PGA", NULL, "IN1L Mode" },
	{ "IN1R PGA", NULL, "IN1R Mode" },

	{ "IN1L Mode", "Analog", "IN1L Mux" },
	{ "IN1R Mode", "Analog", "IN1R Mux" },

	{ "IN1L Mode", "Digital", "IN1_PDMCLK" },
	{ "IN1L Mode", "Digital", "IN1_PDMDATA" },
	{ "IN1R Mode", "Digital", "IN1_PDMCLK" },
	{ "IN1R Mode", "Digital", "IN1_PDMDATA" },

	{ "IN1L PGA", NULL, "VOUT_MIC" },
	{ "IN1R PGA", NULL, "VOUT_MIC" },

	{ "IN2L PGA", NULL, "IN2_PDMCLK" },
	{ "IN2R PGA", NULL, "IN2_PDMCLK" },
	{ "IN2L PGA", NULL, "IN2_PDMDATA" },
	{ "IN2R PGA", NULL, "IN2_PDMDATA" },

	{ "IN2L PGA", NULL, "VOUT_MIC" },
	{ "IN2R PGA", NULL, "VOUT_MIC" },

	{ "Ultrasonic 1", NULL, "Ultrasonic 1 Input" },
	{ "Ultrasonic 2", NULL, "Ultrasonic 2 Input" },

	{ "Ultrasonic 1 Input", "IN1L", "IN1L PGA" },
	{ "Ultrasonic 1 Input", "IN1R", "IN1R PGA" },
	{ "Ultrasonic 1 Input", "IN2L", "IN2L PGA" },
	{ "Ultrasonic 1 Input", "IN2R", "IN2R PGA" },

	{ "Ultrasonic 2 Input", "IN1L", "IN1L PGA" },
	{ "Ultrasonic 2 Input", "IN1R", "IN1R PGA" },
	{ "Ultrasonic 2 Input", "IN2L", "IN2L PGA" },
	{ "Ultrasonic 2 Input", "IN2R", "IN2R PGA" },

	{ "Ultrasonic 1 Activity Detect", "Switch", "Ultrasonic 1 Input" },
	{ "Ultrasonic 2 Activity Detect", "Switch", "Ultrasonic 2 Input" },

	{ "Ultrasonic Dummy Output", NULL, "Ultrasonic 1 Activity Detect" },
	{ "Ultrasonic Dummy Output", NULL, "Ultrasonic 2 Activity Detect" },

	TACNA_MIXER_ROUTES("ASP1TX1", "ASP1TX1"),
	TACNA_MIXER_ROUTES("ASP1TX2", "ASP1TX2"),
	TACNA_MIXER_ROUTES("ASP1TX3", "ASP1TX3"),
	TACNA_MIXER_ROUTES("ASP1TX4", "ASP1TX4"),
	TACNA_MIXER_ROUTES("ASP1TX5", "ASP1TX5"),
	TACNA_MIXER_ROUTES("ASP1TX6", "ASP1TX6"),
	TACNA_MIXER_ROUTES("ASP1TX7", "ASP1TX7"),
	TACNA_MIXER_ROUTES("ASP1TX8", "ASP1TX8"),

	TACNA_MIXER_ROUTES("ASP2TX1", "ASP2TX1"),
	TACNA_MIXER_ROUTES("ASP2TX2", "ASP2TX2"),
	TACNA_MIXER_ROUTES("ASP2TX3", "ASP2TX3"),
	TACNA_MIXER_ROUTES("ASP2TX4", "ASP2TX4"),

	TACNA_MIXER_ROUTES("EQ1", "EQ1"),
	TACNA_MIXER_ROUTES("EQ2", "EQ2"),
	TACNA_MIXER_ROUTES("EQ3", "EQ3"),
	TACNA_MIXER_ROUTES("EQ4", "EQ4"),

	TACNA_MIXER_ROUTES("DRC1L", "DRC1L"),
	TACNA_MIXER_ROUTES("DRC1R", "DRC1R"),
	TACNA_MIXER_ROUTES("DRC2L", "DRC2L"),
	TACNA_MIXER_ROUTES("DRC2R", "DRC2R"),

	TACNA_MIXER_ROUTES("LHPF1", "LHPF1"),
	TACNA_MIXER_ROUTES("LHPF2", "LHPF2"),
	TACNA_MIXER_ROUTES("LHPF3", "LHPF3"),
	TACNA_MIXER_ROUTES("LHPF4", "LHPF4"),

	TACNA_MUX_ROUTES("ISRC1INT1", "ISRC1INT1"),
	TACNA_MUX_ROUTES("ISRC1INT2", "ISRC1INT2"),
	TACNA_MUX_ROUTES("ISRC1INT3", "ISRC1INT3"),
	TACNA_MUX_ROUTES("ISRC1INT4", "ISRC1INT4"),

	TACNA_MUX_ROUTES("ISRC1DEC1", "ISRC1DEC1"),
	TACNA_MUX_ROUTES("ISRC1DEC2", "ISRC1DEC2"),
	TACNA_MUX_ROUTES("ISRC1DEC3", "ISRC1DEC3"),
	TACNA_MUX_ROUTES("ISRC1DEC4", "ISRC1DEC4"),

	TACNA_MUX_ROUTES("ISRC2INT1", "ISRC2INT1"),
	TACNA_MUX_ROUTES("ISRC2INT2", "ISRC2INT2"),

	TACNA_MUX_ROUTES("ISRC2DEC1", "ISRC2DEC1"),
	TACNA_MUX_ROUTES("ISRC2DEC2", "ISRC2DEC2"),

	TACNA_MUX_ROUTES("ISRC3INT1", "ISRC3INT1"),
	TACNA_MUX_ROUTES("ISRC3INT2", "ISRC3INT2"),

	TACNA_MUX_ROUTES("ISRC3DEC1", "ISRC3DEC1"),
	TACNA_MUX_ROUTES("ISRC3DEC2", "ISRC3DEC2"),

	TACNA_DSP_ROUTES_1_8_SYSCLK("DSP1"),

	{ "DSP Trigger Out", NULL, "DSP1 Trigger Output" },

	{ "DSP1 Trigger Output", "Switch", "DSP1" },

	{ "AUXPDM1 Analog Input", "IN1L", "IN1L PGA" },
	{ "AUXPDM1 Analog Input", "IN1R", "IN1R PGA" },

	{ "AUXPDM2 Analog Input", "IN1L", "IN1L PGA" },
	{ "AUXPDM2 Analog Input", "IN1R", "IN1R PGA" },

	{ "AUXPDM1 Input", "Analog", "AUXPDM1 Analog Input" },
	{ "AUXPDM1 Input", "IN1 Digital", "IN1L PGA" },
	{ "AUXPDM1 Input", "IN1 Digital", "IN1R PGA" },
	{ "AUXPDM1 Input", "IN2 Digital", "IN2L PGA" },
	{ "AUXPDM1 Input", "IN2 Digital", "IN2R PGA" },

	{ "AUXPDM2 Input", "Analog", "AUXPDM2 Analog Input" },
	{ "AUXPDM2 Input", "IN1 Digital", "IN1L PGA" },
	{ "AUXPDM2 Input", "IN1 Digital", "IN1R PGA" },
	{ "AUXPDM2 Input", "IN2 Digital", "IN2L PGA" },
	{ "AUXPDM2 Input", "IN2 Digital", "IN2R PGA" },

	{ "AUXPDM1 Output", "Switch", "AUXPDM1 Input" },
	{ "AUXPDM1_CLK", NULL, "AUXPDM1 Output" },
	{ "AUXPDM1_DOUT", NULL, "AUXPDM1 Output" },

	{ "AUXPDM2 Output", "Switch", "AUXPDM2 Input" },
	{ "AUXPDM2_CLK", NULL, "AUXPDM2 Output" },
	{ "AUXPDM2_DOUT", NULL, "AUXPDM2 Output" },

	{ "MICSUPP", NULL, "SYSCLK" },

	{ "DRC1 Signal Activity", NULL, "DRC1 Activity Output" },
	{ "DRC2 Signal Activity", NULL, "DRC2 Activity Output" },
	{ "DRC1 Activity Output", "Switch", "DRC1L" },
	{ "DRC1 Activity Output", "Switch", "DRC1R" },
	{ "DRC2 Activity Output", "Switch", "DRC2L" },
	{ "DRC2 Activity Output", "Switch", "DRC2R" },
};

static struct snd_soc_dai_driver cs48l32_dai[] = {
	{
		.name = "cs48l32-asp1",
		.id = 1,
		.base = TACNA_ASP1_ENABLES1,
		.playback = {
			.stream_name = "ASP1 Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = TACNA_RATES,
			.formats = TACNA_FORMATS,
		},
		.capture = {
			.stream_name = "ASP1 Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = TACNA_RATES,
			.formats = TACNA_FORMATS,
		 },
		.ops = &tacna_dai_ops,
		.symmetric_rates = 1,
		.symmetric_samplebits = 1,
	},
	{
		.name = "cs48l32-asp2",
		.id = 2,
		.base = TACNA_ASP2_ENABLES1,
		.playback = {
			.stream_name = "ASP2 Playback",
			.channels_min = 1,
			.channels_max = 4,
			.rates = TACNA_RATES,
			.formats = TACNA_FORMATS,
		},
		.capture = {
			.stream_name = "ASP2 Capture",
			.channels_min = 1,
			.channels_max = 4,
			.rates = TACNA_RATES,
			.formats = TACNA_FORMATS,
		 },
		.ops = &tacna_dai_ops,
		.symmetric_rates = 1,
		.symmetric_samplebits = 1,
	},
	{
		.name = "cs48l32-cpu-trace",
		.capture = {
			.stream_name = "Audio Trace CPU",
			.channels_min = 1,
			.channels_max = 8,
			.rates = TACNA_RATES,
			.formats = TACNA_FORMATS,
		},
		.compress_new = &snd_soc_new_compress,
	},
	{
		.name = "cs48l32-dsp-trace",
		.capture = {
			.stream_name = "Audio Trace DSP",
			.channels_min = 1,
			.channels_max = 8,
			.rates = TACNA_RATES,
			.formats = TACNA_FORMATS,
		},
	},
	{
		.name = "cs48l32-cpu-voicectrl",
		.capture = {
			.stream_name = "Voice Ctrl CPU",
			.channels_min = 1,
			.channels_max = 8,
			.rates = TACNA_RATES,
			.formats = TACNA_FORMATS,
		},
		.compress_new = &snd_soc_new_compress,
	},
	{
		.name = "cs48l32-dsp-voicectrl",
		.capture = {
			.stream_name = "Voice Ctrl DSP",
			.channels_min = 1,
			.channels_max = 8,
			.rates = TACNA_RATES,
			.formats = TACNA_FORMATS,
		},
	},
	{
		.name = "cs48l32-cpu-voicectrl2",
		.capture = {
			.stream_name = "Voice Ctrl 2 CPU",
			.channels_min = 1,
			.channels_max = 8,
			.rates = TACNA_RATES,
			.formats = TACNA_FORMATS,
		},
		.compress_new = &snd_soc_new_compress,
	},
	{
		.name = "cs48l32-dsp-voicectrl2",
		.capture = {
			.stream_name = "Voice Ctrl 2 DSP",
			.channels_min = 1,
			.channels_max = 8,
			.rates = TACNA_RATES,
			.formats = TACNA_FORMATS,
		},
	},
	{
		.name = "cs48l32-cpu-voicectrl3",
		.capture = {
			.stream_name = "Voice Ctrl 3 CPU",
			.channels_min = 1,
			.channels_max = 8,
			.rates = TACNA_RATES,
			.formats = TACNA_FORMATS,
		},
		.compress_new = &snd_soc_new_compress,
	},
	{
		.name = "cs48l32-dsp-voicectrl3",
		.capture = {
			.stream_name = "Voice Ctrl 3 DSP",
			.channels_min = 1,
			.channels_max = 8,
			.rates = TACNA_RATES,
			.formats = TACNA_FORMATS,
		},
	},
	{
		.name = "cs48l32-cpu-textlog",
		.capture = {
			.stream_name = "Text Log CPU",
			.channels_min = 1,
			.channels_max = 8,
			.rates = TACNA_RATES,
			.formats = TACNA_FORMATS,
		},
		.compress_new = &snd_soc_new_compress,
	},
	{
		.name = "cs48l32-dsp-textlog",
		.capture = {
			.stream_name = "Text Log DSP",
			.channels_min = 1,
			.channels_max = 8,
			.rates = TACNA_RATES,
			.formats = TACNA_FORMATS,
		},
	},
};

static int cs48l32_compr_open(struct snd_compr_stream *stream)
{
	struct snd_soc_pcm_runtime *rtd = stream->private_data;
	struct snd_soc_component *comp = snd_soc_rtdcom_lookup(rtd, DRV_NAME);
	struct cs48l32 *cs48l32 = snd_soc_component_get_drvdata(comp);
	struct tacna_priv *priv = &cs48l32->core;

	if (strcmp(rtd->codec_dai->name, "cs48l32-dsp-trace") &&
	    strcmp(rtd->codec_dai->name, "cs48l32-dsp-voicectrl") &&
	    strcmp(rtd->codec_dai->name, "cs48l32-dsp-voicectrl2") &&
	    strcmp(rtd->codec_dai->name, "cs48l32-dsp-voicectrl3") &&
	    strcmp(rtd->codec_dai->name, "cs48l32-dsp-textlog")) {
		dev_err(priv->dev,
			"No suitable compressed stream for DAI '%s'\n",
			rtd->codec_dai->name);
		return -EINVAL;
	}

	return wm_adsp_compr_open(&priv->dsp[0], stream);
}

static irqreturn_t cs48l32_dsp1_irq(int irq, void *data)
{
	struct cs48l32 *cs48l32 = data;
	struct tacna_priv *priv = &cs48l32->core;
	int ret;

	ret = wm_adsp_compr_handle_irq(&priv->dsp[0]);
	if (ret == -ENODEV) {
		dev_err(priv->dev, "Spurious compressed data IRQ\n");
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

static int cs48l32_component_probe(struct snd_soc_component *comp)
{
	struct cs48l32 *cs48l32 = snd_soc_component_get_drvdata(comp);
	struct tacna *tacna = cs48l32->core.tacna;
	int ret;

	tacna->dapm = snd_soc_component_get_dapm(comp);
	snd_soc_component_init_regmap(comp, tacna->regmap);

	ret = tacna_init_inputs(comp);
	if (ret)
		return ret;

	ret = tacna_init_auxpdm(comp, CS48L32_N_AUXPDM);
	if (ret)
		return ret;

	ret = tacna_init_eq(&cs48l32->core);
	if (ret)
		return ret;

	ret = tacna_dsp_add_component_controls(comp, CS48L32_NUM_DSP);
	if (ret)
		return ret;

	wm_adsp2_component_probe(&cs48l32->core.dsp[0], comp);

	return 0;
}

static void cs48l32_component_remove(struct snd_soc_component *comp)
{
	struct cs48l32 *cs48l32 = snd_soc_component_get_drvdata(comp);
	struct tacna *tacna = cs48l32->core.tacna;

	wm_adsp2_component_remove(&cs48l32->core.dsp[0], comp);

	tacna->dapm = NULL;
}

static int cs48l32_set_fll(struct snd_soc_component *comp, int fll_id, int source,
			   unsigned int fref, unsigned int fout)
{
	struct cs48l32 *cs48l32 = snd_soc_component_get_drvdata(comp);

	switch (fll_id) {
	case TACNA_FLL1_REFCLK:
		break;
	default:
		return -EINVAL;
	}

	return tacna_fllhj_set_refclk(&cs48l32->fll, source, fref, fout);
}

static const struct snd_compr_ops cs48l32_compr_ops = {
	.open = &cs48l32_compr_open,
	.free = &wm_adsp_compr_free,
	.set_params = &wm_adsp_compr_set_params,
	.get_caps = &wm_adsp_compr_get_caps,
	.trigger = &wm_adsp_compr_trigger,
	.pointer = &wm_adsp_compr_pointer,
	.copy = &wm_adsp_compr_copy,
};

static const struct snd_soc_component_driver soc_component_dev_cs48l32 = {
	.probe = &cs48l32_component_probe,
	.remove = &cs48l32_component_remove,
	.compr_ops = &cs48l32_compr_ops,

	.idle_bias_on = false,
	.name		= DRV_NAME,

	.set_sysclk = &tacna_set_sysclk,
	.set_pll = &cs48l32_set_fll,

	.controls = cs48l32_snd_controls,
	.num_controls = ARRAY_SIZE(cs48l32_snd_controls),
	.dapm_widgets = cs48l32_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cs48l32_dapm_widgets),
	.dapm_routes = cs48l32_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(cs48l32_dapm_routes),
};

static int cs48l32_probe(struct platform_device *pdev)
{
	struct tacna *tacna = dev_get_drvdata(pdev->dev.parent);
	struct cs48l32 *cs48l32;
	struct wm_adsp *dsp;
	int i, ret;

	BUILD_BUG_ON(ARRAY_SIZE(cs48l32_dai) > TACNA_MAX_DAI);

	/* quick exit if tacna irqchip driver hasn't completed probe */
	if (!tacna->irq_dev) {
		dev_dbg(&pdev->dev, "irqchip driver not ready\n");
		return -EPROBE_DEFER;
	}

	cs48l32 = devm_kzalloc(&pdev->dev, sizeof(struct cs48l32), GFP_KERNEL);
	if (!cs48l32)
		return -ENOMEM;

	platform_set_drvdata(pdev, cs48l32);
	pdev->dev.of_node = of_node_get(tacna->dev->of_node);

	cs48l32->core.tacna = tacna;
	cs48l32->core.dev = &pdev->dev;
	cs48l32->core.num_inputs = 2;
	cs48l32->core.max_analogue_inputs = 1;
	cs48l32->core.max_pdm_sup = 2;
	cs48l32->core.in_vu_reg = TACNA_INPUT_CONTROL3;

	ret = tacna_core_init(&cs48l32->core);
	if (ret)
		return ret;

	ret = tacna_request_irq(tacna, TACNA_IRQ_US1_ACT_DET_RISE,
				"Ultrasonic 1 activity",
				 tacna_us1_activity, tacna);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to get Ultrasonic 1 IRQ: %d\n",
			ret);
		goto error_us1_irq;
	}

	ret = tacna_request_irq(tacna, TACNA_IRQ_US2_ACT_DET_RISE,
				"Ultrasonic 2 activity",
				 tacna_us2_activity, tacna);
	if (ret != 0) {
		tacna_free_irq(tacna, TACNA_IRQ_US1_ACT_DET_RISE, tacna);
		dev_err(&pdev->dev, "Failed to get Ultrasonic 2 IRQ: %d\n",
			ret);
		goto error_us2_irq;
	}

	ret = tacna_request_irq(tacna, TACNA_IRQ_DSP1_IRQ0,
				"DSP1 Buffer IRQ", cs48l32_dsp1_irq,
				cs48l32);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to request DSP1_IRQ0: %d\n", ret);
		goto error_dsp1_irq;
	}

	ret = tacna_set_irq_wake(tacna, TACNA_IRQ_DSP1_IRQ0, 1);
	if(ret)
		dev_warn(&pdev->dev, "Failed to set DSP IRQ wake: %d\n", ret);

	dsp = &cs48l32->core.dsp[0];
	dsp->part = "cs48l32";
	dsp->num = 1;
	dsp->type = WMFW_HALO;
	dsp->rev = 0;
	dsp->dev = tacna->dev;
	dsp->regmap = tacna->dsp_regmap[0];

	dsp->base = TACNA_DSP1_CLOCK_FREQ;
	dsp->base_sysinfo = TACNA_DSP1_SYS_INFO_ID;

	dsp->mem = cs48l32_dsp1_regions;
	dsp->num_mems = ARRAY_SIZE(cs48l32_dsp1_regions);

	dsp->n_rx_channels = CS48L32_DSP_N_RX_CHANNELS;
	dsp->n_tx_channels = CS48L32_DSP_N_TX_CHANNELS;

	ret = wm_halo_init(dsp, &cs48l32->core.rate_lock);
	if (ret != 0)
		goto error_core;

	ret = tacna_request_irq(tacna, TACNA_IRQ_DSP1_MPU_ERR,
				"DSP1 MPU", wm_halo_bus_error,
				&cs48l32->core.dsp[0]);
	if (ret) {
		dev_warn(&pdev->dev, "Failed to get DSP1 MPU IRQ: %d\n", ret);
		goto error_dsp;
	}

	ret = tacna_request_irq(tacna, TACNA_IRQ_DSP1_WDT_EXPIRE,
				"DSP1 WDT", wm_halo_wdt_expire,
				&cs48l32->core.dsp[0]);
	if (ret) {
		dev_warn(&pdev->dev, "Failed to get DSP1 WDT IRQ: %d\n", ret);
		goto error_mpu_irq1;
	}

	cs48l32->fll.tacna_priv = &cs48l32->core;
	cs48l32->fll.id = 1;
	cs48l32->fll.base = TACNA_FLL1_CONTROL1;
	cs48l32->fll.sts_addr = TACNA_IRQ1_STS_6;
	cs48l32->fll.sts_mask = TACNA_FLL1_LOCK_STS1_MASK;
	cs48l32->fll.has_lp = 1;
	tacna_init_fll(&cs48l32->fll);

	for (i = 0; i < ARRAY_SIZE(cs48l32_dai); i++)
		tacna_init_dai(&cs48l32->core, i);

	pm_runtime_enable(&pdev->dev);
	pm_runtime_idle(&pdev->dev);

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &soc_component_dev_cs48l32,
					      cs48l32_dai,
					      ARRAY_SIZE(cs48l32_dai));
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register component: %d\n", ret);
		goto error_wdt_irq1;
	}

	return ret;
error_wdt_irq1:
	tacna_free_irq(tacna, TACNA_IRQ_DSP1_WDT_EXPIRE,
		       &cs48l32->core.dsp[0]);
error_mpu_irq1:
	tacna_free_irq(tacna, TACNA_IRQ_DSP1_MPU_ERR, &cs48l32->core.dsp[0]);
error_dsp:
	wm_adsp2_remove(&cs48l32->core.dsp[0]);
error_core:
	tacna_set_irq_wake(tacna, TACNA_IRQ_DSP1_IRQ0, 0);
	tacna_free_irq(tacna, TACNA_IRQ_DSP1_IRQ0, cs48l32);
error_dsp1_irq:
	tacna_free_irq(tacna, TACNA_IRQ_US2_ACT_DET_RISE, tacna);
error_us2_irq:
	tacna_free_irq(tacna, TACNA_IRQ_US1_ACT_DET_RISE, tacna);
error_us1_irq:
	tacna_core_destroy(&cs48l32->core);

	return ret;
}

static int cs48l32_remove(struct platform_device *pdev)
{
	struct cs48l32 *cs48l32 = platform_get_drvdata(pdev);
	struct tacna *tacna = cs48l32->core.tacna;

	pm_runtime_disable(&pdev->dev);

	tacna_free_irq(tacna, TACNA_IRQ_US1_ACT_DET_RISE, tacna);
	tacna_free_irq(tacna, TACNA_IRQ_US2_ACT_DET_RISE, tacna);

	tacna_free_irq(tacna, TACNA_IRQ_DSP1_WDT_EXPIRE,
		       &cs48l32->core.dsp[0]);
	tacna_free_irq(tacna, TACNA_IRQ_DSP1_MPU_ERR, &cs48l32->core.dsp[0]);

	tacna_set_irq_wake(tacna, TACNA_IRQ_DSP1_IRQ0, 0);
	tacna_free_irq(tacna, TACNA_IRQ_DSP1_IRQ0, cs48l32);

	wm_adsp2_remove(&cs48l32->core.dsp[0]);

	tacna_core_destroy(&cs48l32->core);

	return 0;
}

static struct platform_driver cs48l32_component_driver = {
	.driver = {
		.name = "cs48l32-codec",
		.owner = THIS_MODULE,
	},
	.probe = &cs48l32_probe,
	.remove = &cs48l32_remove,
};

module_platform_driver(cs48l32_component_driver);

MODULE_DESCRIPTION("ASoC CS48L32 driver");
MODULE_AUTHOR("Stuart Henderson <stuarth@opensource.cirrus.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:cs48l32-codec");
