// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Cirrus Logic USB Bus Bridge
 *
 * Copyright 2020 Cirrus Logic, Inc.
 *
 * Author: Lucas Tanure <tanureal@opensource.cirrus.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */
#define DEBUG
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/list.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <linux/completion.h>
#include <linux/platform_device.h>
#include <linux/mfd/clubb/clubb.h>

#define DRV_NAME "clubb-i2s"

/*
 * Left and right are sent to separate endpoints so max period size
 * is 2* max USB packet size
 */
#define CLUBB_PERIOD_BYTES_MAX	1024	/* DMA transfer/period size */
#define CLUBB_PERIOD_BYTES_MIN	2	/* DMA transfer/period size */
#define CLUBB_PERIODS_MAX		100	/* Max periods in buffer */
#define CLUBB_PERIODS_MIN		1	/* Min periods in buffer */
#define CLUBB_BUFFER_BYTES_MAX	(CLUBB_PERIOD_BYTES_MAX * CLUBB_PERIODS_MAX)

#define CREATE_TRACE_POINTS
#include <trace/events/clubb.h>

struct urbs_pending {
	struct urb *l_urb;
	struct urb *r_urb;
	unsigned long size_bytes;
	struct clubb_i2s *priv;
	struct list_head node;
};

struct clubb_i2s {
	struct clubb *clubb;
	struct delayed_work send_worker;
	unsigned int hwptr_done;
	volatile int playing;
	unsigned long period_ptr;
	unsigned long period_size;
	struct completion l_completion;
	struct completion r_completion;
	struct snd_pcm_substream *sub;
	spinlock_t send_lock;
	spinlock_t reuse_lock;
	struct list_head pending_list;
	struct list_head reuse_list;
};

static inline void clubb_free_urb(struct urb *urb)
{
  usb_free_coherent(urb->dev, urb->transfer_buffer_length, urb->transfer_buffer, urb->transfer_dma);
  usb_free_urb(urb);
}

static void clubb_callback(struct urb *urb)
{
	struct urbs_pending *urbs = (struct urbs_pending *)urb->context;
	struct clubb_i2s *priv = urbs->priv;
	int status = urb->status;
	int period_elapsed = 0;
	unsigned long irq_flags;

	if (status && !(status == -ENOENT || status == -ECONNRESET || status == -ESHUTDOWN)) {
		pr_err("urb=%p bulk status: %d\n", urb, status);
	}

	spin_lock_irqsave(&priv->send_lock, irq_flags);

	priv->period_ptr += urb->transfer_buffer_length;
	if (priv->period_ptr >= priv->period_size) {
		priv->period_ptr -= priv->period_size;
		period_elapsed = 1;
	}

	/* hwptr_done calculation adapted from sound/usb/pcm.c */
	priv->hwptr_done += urb->transfer_buffer_length;
	if (priv->hwptr_done >= snd_pcm_lib_buffer_bytes(priv->sub))
		priv->hwptr_done -= snd_pcm_lib_buffer_bytes(priv->sub);

	spin_unlock_irqrestore(&priv->send_lock, irq_flags);

	if (urb == urbs->r_urb)
		complete(&priv->r_completion);
	else if (urb == urbs->l_urb)
		complete(&priv->l_completion);
	else
		pr_err("Received unexpected urb");

	if (period_elapsed && priv->playing)
		snd_pcm_period_elapsed(priv->sub);
}

static struct urbs_pending *clubb_create_pkg(struct clubb_i2s *priv, unsigned long bytes)
{
	struct usb_device *udev = priv->clubb->udev;
	struct urbs_pending *urbs;
	struct urb *l_urb, *r_urb;
	uint16_t *l_buf, *r_buf;

	urbs = kzalloc(sizeof(struct urbs_pending), GFP_ATOMIC);
	if (!urbs)
		return NULL;

	/* left channel */
	l_urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!l_urb)
		return NULL;
	l_buf = usb_alloc_coherent(udev, bytes/2, GFP_ATOMIC, &l_urb->transfer_dma);
	if (!l_buf) {
		usb_free_urb(l_urb);
		return NULL;
	}

	/* right channel */
	r_urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!r_urb) {
		clubb_free_urb(l_urb);
		return NULL;
	}
	r_buf = usb_alloc_coherent(udev, bytes/2, GFP_ATOMIC, &r_urb->transfer_dma);
	if (!r_buf) {
		usb_free_urb(r_urb);
		clubb_free_urb(l_urb);
		return NULL;
	}

	urbs->l_urb = l_urb;
	urbs->r_urb = r_urb;
	urbs->size_bytes = bytes;
	urbs->priv = priv;

	usb_fill_bulk_urb(l_urb, udev, usb_sndbulkpipe(udev, 1), l_buf, bytes/2, clubb_callback, urbs);
	l_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	usb_fill_bulk_urb(r_urb, udev, usb_sndbulkpipe(udev, 2), r_buf, bytes/2, clubb_callback, urbs);
	r_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	return urbs;
}

static int clubb_create_lr_urb(struct clubb_i2s *priv, struct snd_pcm_substream *sub,
			       uint16_t *buffer, unsigned long bytes)
{
	struct urbs_pending *urbs;
	uint16_t *l_buf, *r_buf;
	unsigned int sample, schedule_work = 0;

	spin_lock(&priv->reuse_lock);
	urbs = list_first_entry_or_null(&priv->reuse_list, struct urbs_pending, node);
	if (urbs != NULL) {
		if (urbs->size_bytes < bytes)
			goto create;
		else
			list_del(&urbs->node);

	} else {
create:
		urbs = clubb_create_pkg(priv, bytes);
		if (!urbs) {
			spin_unlock(&priv->reuse_lock);
			return -ENOMEM;
		}
	}
	spin_unlock(&priv->reuse_lock);

	urbs->l_urb->transfer_buffer_length = bytes/2;
	urbs->r_urb->transfer_buffer_length = bytes/2;
	l_buf = urbs->l_urb->transfer_buffer;
	r_buf = urbs->r_urb->transfer_buffer;

	for (sample = 0; sample < bytes/2; sample += 2) {
		l_buf[sample/2] = buffer[sample];
		r_buf[sample/2] = buffer[sample+1];
	}

	if (list_empty(&priv->pending_list) && priv->playing)
		schedule_work = 1;

	spin_lock(&priv->send_lock);
	list_add_tail(&urbs->node, &priv->pending_list);
	spin_unlock(&priv->send_lock);

	if (schedule_work)
		schedule_delayed_work(&priv->send_worker, 0);

	return 0;
}

static int clubb_i2s_copy(struct snd_pcm_substream *sub, int channel, unsigned long hwoff,
			  void __user *user_buf, unsigned long bytes)
{
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(sub->private_data, DRV_NAME);
	struct clubb_i2s *priv = snd_soc_component_get_drvdata(component);
	unsigned long writesize, pos;
	char *buffer;
	int ret;

	buffer = memdup_user(user_buf, bytes);
	if (IS_ERR(buffer))
		return PTR_ERR(buffer);

	pos = 0;
	while (bytes) {
		writesize = min_t(unsigned long, bytes, CLUBB_PERIOD_BYTES_MAX);
		ret = clubb_create_lr_urb(priv, sub, (uint16_t *)(&buffer[pos]), writesize);
		if (ret)
			goto free;
		pos += writesize;
		bytes -= writesize;
	}
free:
	kfree(buffer);
	return ret;
}

void clubb_urb_sender(struct work_struct *work)
{
	struct clubb_i2s *priv = container_of(work, struct clubb_i2s, send_worker.work);
	struct usb_device *udev = priv->clubb->udev;
	struct urbs_pending *to_send, *to_save;
	int retval = 0;

	spin_lock(&priv->send_lock);
	to_send = list_first_entry_or_null(&priv->pending_list, struct urbs_pending, node);
	spin_unlock(&priv->send_lock);

	while (priv->playing && to_send) {

		retval = usb_submit_urb(to_send->l_urb, GFP_ATOMIC);
		if (retval)
			dev_err(&udev->dev, "Failed submitting urb %d\n", retval);

		retval = usb_submit_urb(to_send->r_urb, GFP_ATOMIC);
		if (retval)
			dev_err(&udev->dev, "Failed submitting urb %d\n",  retval);

		if (wait_for_completion_timeout(&priv->l_completion, msecs_to_jiffies(5000)) == 0)
			dev_err(&udev->dev, "Left Urb timeout\n");

		if (wait_for_completion_timeout(&priv->r_completion, msecs_to_jiffies(5000)) == 0)
			dev_err(&udev->dev, "Right Urb timeout\n");

		to_save = to_send;

		spin_lock(&priv->send_lock);
		list_del(&to_send->node);
		to_send = list_first_entry_or_null(&priv->pending_list, struct urbs_pending, node);
		spin_unlock(&priv->send_lock);

		spin_lock(&priv->reuse_lock);
		list_add_tail(&to_save->node, &priv->reuse_list);
		spin_unlock(&priv->reuse_lock);
	}
}

int clubb_i2s_trigger(struct snd_pcm_substream *sub, int cmd, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(sub->private_data, DRV_NAME);
	struct clubb_i2s *priv = snd_soc_component_get_drvdata(component);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		priv->playing = 1;
		priv->hwptr_done = 0;
		schedule_delayed_work(&priv->send_worker, 0);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		priv->playing = 0;
		/*
		 * trigger is called in an atomic section so can't use
		 * cancel_delayed_work_sync()
		 */
		cancel_delayed_work(&priv->send_worker);
		break;
	default:
		break;
	}

	return 0;
}

/*
 * This function returns where the next available position is within
 * the DMA ring buffer, and so is always 0..(runtime->buffer_size-1).
 * It tells the upper layers how much more data our buffer can accept.
 */
static snd_pcm_uframes_t clubb_i2s_pointer(struct snd_pcm_substream *sub)
{
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(sub->private_data, DRV_NAME);
	struct clubb_i2s *priv = snd_soc_component_get_drvdata(component);
	struct snd_pcm_runtime *runtime = sub->runtime;
	unsigned long hwptr_done;
	unsigned long irq_flags;

	spin_lock_irqsave(&priv->send_lock, irq_flags);
	hwptr_done = priv->hwptr_done;
	spin_unlock_irqrestore(&priv->send_lock, irq_flags);

	return bytes_to_frames(runtime, hwptr_done);
}

static const struct snd_pcm_hardware clubb_pcm_hw = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED,
	.formats		= SNDRV_PCM_FMTBIT_S16,
	.rates			= SNDRV_PCM_RATE_48000,
	.rate_min		= 48000,
	.rate_max		= 48000,
	.channels_min		= 2,
	.channels_max		= 2,
	.buffer_bytes_max	= CLUBB_BUFFER_BYTES_MAX,
	.period_bytes_min	= CLUBB_PERIOD_BYTES_MIN,
	.period_bytes_max	= CLUBB_PERIOD_BYTES_MAX,
	.periods_min		= CLUBB_PERIODS_MIN,
	.periods_max		= CLUBB_PERIODS_MAX,
};

static int clubb_i2s_prepare(struct snd_pcm_substream *sub)
{
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(sub->private_data, DRV_NAME);
	struct clubb_i2s *priv = snd_soc_component_get_drvdata(component);
	unsigned long bufsize;

        bufsize = snd_pcm_lib_buffer_bytes(sub);
        priv->period_size = snd_pcm_lib_period_bytes(sub);
        priv->sub = sub;

        return 0;
}

static int clubb_pcm_open(struct snd_pcm_substream *sub)
{
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(sub->private_data, DRV_NAME);
	struct clubb_i2s *priv = snd_soc_component_get_drvdata(component);

	priv->playing = 0;
	snd_soc_set_runtime_hwparams(sub, &clubb_pcm_hw);
	INIT_DELAYED_WORK(&priv->send_worker, clubb_urb_sender);

	return 0;
}

static  int clubb_i2s_comp_probe(struct snd_soc_component *component)
{
	struct clubb *clubb = snd_soc_component_get_drvdata(component);
	struct clubb_i2s *priv;
	struct device_node *np;

	priv = devm_kzalloc(component->dev, sizeof(struct clubb_i2s), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	INIT_LIST_HEAD(&priv->pending_list);
	INIT_LIST_HEAD(&priv->reuse_list);
	priv->clubb = clubb;
	spin_lock_init(&priv->send_lock);
	spin_lock_init(&priv->reuse_lock);
	init_completion(&priv->l_completion);
	init_completion(&priv->r_completion);

	np = of_find_compatible_node(NULL, NULL, "cirrus,clubb-i2s");
	if (np)
		component->dev->of_node = np;
	of_node_put(np);

	snd_soc_component_set_drvdata(component, priv);

	return 0;
}


static struct snd_soc_dai_ops clubb_i2s_dai_ops = {
	.trigger	= &clubb_i2s_trigger,
};

static struct snd_soc_dai_driver clubb_i2s_dai[] = {
	{
		.name	= "clubb-i2s-sai1",
		.id = 1,
		.playback = {
			.channels_min = 2,
			.channels_max = 2,
			.rates =	SNDRV_PCM_RATE_48000,
			.rate_min =	48000,
			.rate_max =	48000,
			.formats =	SNDRV_PCM_FMTBIT_S16_LE
		},
		.ops = &clubb_i2s_dai_ops,
	},
};

static struct snd_pcm_ops clubb_i2s_pcm_ops = {
	.open		= &clubb_pcm_open,
	.copy_user	= &clubb_i2s_copy,
	.pointer	= &clubb_i2s_pointer,
	.prepare	= &clubb_i2s_prepare,
};

const struct snd_soc_component_driver clubb_i2s_component = {
	.name	= DRV_NAME,
	.ops	= &clubb_i2s_pcm_ops,
	.non_legacy_dai_naming	= 1,
	.probe = &clubb_i2s_comp_probe,
};

static int clubb_i2s_probe(struct platform_device *pdev)
{
	struct clubb *clubb;
	struct device_node *np;
	int ret = 0;

	clubb = dev_get_drvdata(pdev->dev.parent);
	if (!clubb)
		return -EPROBE_DEFER;

	platform_set_drvdata(pdev, clubb);

	np = of_find_compatible_node(NULL, NULL, "cirrus,clubb-i2s");
	if (np)
		pdev->dev.of_node = np;
	of_node_put(np);

	ret = devm_snd_soc_register_component(&pdev->dev, &clubb_i2s_component, clubb_i2s_dai,
					      ARRAY_SIZE(clubb_i2s_dai));
	if (ret)
		pr_err("Could not register DAI: %d\n", ret);

	return ret;
}

static int clubb_i2s_remove(struct platform_device *pdev)
{
	return 0;
}


static struct platform_driver clubb_i2s_driver = {
	.probe		= clubb_i2s_probe,
	.remove		= clubb_i2s_remove,
	.driver		= {
		.name	= DRV_NAME,
	},
};

module_platform_driver(clubb_i2s_driver);

MODULE_AUTHOR("Lucas Tanure <tanureal@opensource.cirrus.com>");
MODULE_DESCRIPTION("Driver for Cirrus I2S USB Bridge");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:clubb-i2s");
