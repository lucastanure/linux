# SPDX-License-Identifier: GPL-2.0
# EP93xx Platform Support
snd-soc-ep93xx-objs				:= ep93xx-pcm.o
snd-soc-ep93xx-i2s-objs	 			:= ep93xx-i2s.o
snd-soc-ep93xx-ac97-objs 			:= ep93xx-ac97.o

obj-$(CONFIG_SND_EP93XX_SOC)			+= snd-soc-ep93xx.o
obj-$(CONFIG_SND_EP93XX_SOC_I2S)		+= snd-soc-ep93xx-i2s.o
obj-$(CONFIG_SND_EP93XX_SOC_AC97)		+= snd-soc-ep93xx-ac97.o

# EP93XX Machine Support
snd-soc-snappercl15-objs			:= snappercl15.o
snd-soc-simone-objs				:= simone.o
snd-soc-edb93xx-objs				:= edb93xx.o

obj-$(CONFIG_SND_EP93XX_SOC_SNAPPERCL15)	+= snd-soc-snappercl15.o
obj-$(CONFIG_SND_EP93XX_SOC_SIMONE)		+= snd-soc-simone.o
obj-$(CONFIG_SND_EP93XX_SOC_EDB93XX)		+= snd-soc-edb93xx.o

# Clubb SoundCard Support
snd-soc-clubb-soundcard-objs			:= clubb-soundcard.o
obj-$(CONFIG_SND_SOC_CLUBB_SOUNDCARD)		+= snd-soc-clubb-soundcard.o

snd-soc-clubb-simple-soundcard-objs			:= clubb-simple-soundcard.o
obj-$(CONFIG_SND_SOC_CLUBB_SIMPLE_SOUNDCARD)	+= snd-soc-clubb-simple-soundcard.o

# Cirrus Quartet SoundCard Support
snd-soc-quartet-simplified-soundcard-objs	:= quartet-simplified-soundcard.o
obj-$(CONFIG_SND_SOC_QUARTET_SIMPLE_SOUNDCARD)	+= snd-soc-quartet-simplified-soundcard.o
