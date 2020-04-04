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

#define CLUBB_PERIOD_BYTES_MAX	24000	/* DMA transfer/period size */
#define CLUBB_PERIOD_BYTES_MIN	256	/* DMA transfer/period size */
#define CLUBB_PERIODS_MAX		6000	/* Max periods in buffer */
#define CLUBB_PERIODS_MIN		4	/* Min periods in buffer */
#define CLUBB_BUFFER_BYTES_MAX	(CLUBB_PERIOD_BYTES_MAX * CLUBB_PERIODS_MAX)

#define CREATE_TRACE_POINTS
#include <trace/events/clubb.h>

struct urbs_pending {
	unsigned long id;
	unsigned long sub_id;
	struct urb *l_urb;
	struct urb *r_urb;
	struct list_head node;
};

struct clubb_data {
	struct usb_device *udev;
	struct delayed_work send_worker;
	unsigned int hwptr_done;
	int playing;
	unsigned long pkg_id;
	struct mutex lock;
	struct list_head pending_list;
};

struct clubb_data *clubb_priv;

static void clubb_bulk_callback(struct urb *urb)
{
	struct usb_device *udev = clubb_priv->udev;
	int status = urb->status;
	struct urbs_pending *urbs = (struct urbs_pending *)urb->context;

	if (status && !(status == -ENOENT || status == -ECONNRESET || status == -ESHUTDOWN)) {
		dev_err(&udev->dev, "urb=%p bulk status: %d\n", urb, status);
		trace_clubb("Error sending urb!!!!!!!!!!!!!!");
	}
	trace_clubb_2(__func__, "urbs->id", urbs->id, " urbs->sub_id ", urbs->sub_id);

	//usb_free_coherent(urb->dev, urb->transfer_buffer_length, urb->transfer_buffer, urb->transfer_dma);
	//usb_free_urb(urb);

}

static int clubb_create_lr_urb(uint16_t *buffer, unsigned long bytes, unsigned long sub_id)
{
	struct usb_device *udev = clubb_priv->udev;
	struct urbs_pending *urbs;
	struct urb *l_urb, *r_urb;
	uint16_t *l_buf, *r_buf;
	unsigned int sample;

	urbs = kzalloc(sizeof(struct urbs_pending), GFP_KERNEL);
	if (!urbs)
		return -ENOMEM;

	/* left channel */
	l_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!l_urb)
		return -ENOMEM;
	l_buf = usb_alloc_coherent(udev, bytes/2, GFP_KERNEL, &l_urb->transfer_dma);
	if (!l_buf) {
		usb_free_urb(l_urb);
		return -ENOMEM;
	}

	/* right channel */
	r_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!r_urb)
		return -ENOMEM;
	r_buf = usb_alloc_coherent(udev, bytes/2, GFP_KERNEL, &r_urb->transfer_dma);
	if (!r_buf) {
		usb_free_urb(r_urb);
		return -ENOMEM;
	}

	for (sample = 0; sample < bytes/2; sample += 2) {
		l_buf[sample/2] = buffer[sample];
		r_buf[sample/2] = buffer[sample+1];
	}
	urbs->l_urb = l_urb;
	urbs->r_urb = r_urb;
	urbs->id = clubb_priv->pkg_id;
	urbs->sub_id = sub_id;

	usb_fill_bulk_urb(l_urb, udev, usb_sndbulkpipe(udev, 1), l_buf, bytes/2, clubb_bulk_callback, urbs);
	usb_fill_bulk_urb(r_urb, udev, usb_sndbulkpipe(udev, 2), r_buf, bytes/2, clubb_bulk_callback, urbs);

	list_add_tail(&urbs->node, &clubb_priv->pending_list);

	if (clubb_priv->playing && !list_empty(&clubb_priv->pending_list))
		schedule_delayed_work(&clubb_priv->send_worker, msecs_to_jiffies(1));

	trace_clubb_3(__func__, "urbs->id", urbs->id, "urbs->sub_id", urbs->sub_id, "bytes", bytes);
	return 0;
}

static int clubb_i2s_copy(struct snd_pcm_substream *substream, int channel, unsigned long hwoff,
			  void __user *user_buf, unsigned long bytes)
{
	unsigned long writesize, pos, sub_id;
	char *buffer;
	int ret;

	trace_clubb(__func__);

	buffer = kmalloc(bytes, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;
	if (copy_from_user(buffer, user_buf, bytes))
		return -EFAULT;

	pos = 0;
	sub_id = 0;
	while (bytes) {
		writesize = min_t(unsigned long, bytes, 1024);
		ret = clubb_create_lr_urb((uint16_t *)(&buffer[pos]), writesize, sub_id);
		if (ret)
			return ret;
		pos += writesize;
		bytes -= writesize;
		sub_id++;
	}
	kfree(buffer);
	clubb_priv->pkg_id++;
	return 0;
}

void clubb_urb_sender(struct work_struct *data)
{
	struct urbs_pending *to_send;
	int retval = 0;

	to_send = list_first_entry_or_null(&clubb_priv->pending_list, struct urbs_pending, node);
	if (to_send) {
		trace_clubb_2(__func__, "to_send->id", to_send->id, "to_send->sub_id", to_send->sub_id);

		retval = usb_submit_urb(to_send->l_urb, GFP_KERNEL);
		if (retval) {
			pr_err("%s l_urb failed submitting write urb, error %d\n", __func__, retval);
			return;
		}

		retval = usb_submit_urb(to_send->r_urb, GFP_KERNEL);
		if (retval) {
			pr_err("%s r_urb failed submitting write urb, error %d\n", __func__, retval);
			return;
		}

		list_del(&to_send->node);

		mutex_lock(&clubb_priv->lock);
		clubb_priv->hwptr_done += to_send->r_urb->transfer_buffer_length;
		clubb_priv->hwptr_done += to_send->l_urb->transfer_buffer_length;
		//if (clubb_priv->hwptr_done >= 24000)//???????
	//		clubb_priv->hwptr_done -= 24000;
		mutex_unlock(&clubb_priv->lock);
		trace_clubb_1(__func__, "clubb_priv->hwptr_done", clubb_priv->hwptr_done);

	}
	if (clubb_priv->playing && !list_empty(&clubb_priv->pending_list))
		schedule_delayed_work(&clubb_priv->send_worker, msecs_to_jiffies(1));
}

int clubb_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
		      struct snd_soc_dai *dai)
{
	struct urbs_pending *to_send;
	struct urb *l_urb, *r_urb;
	int retval;

	trace_clubb(__func__);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		clubb_priv->playing = 1;
		clubb_priv->hwptr_done = 0;
		to_send = list_first_entry_or_null(&clubb_priv->pending_list, struct urbs_pending, node);
		if (!to_send)
			break;

		trace_clubb_2(__func__, "to_send->id", to_send->id, "to_send->sub_id", to_send->sub_id);

		l_urb = to_send->l_urb;
		retval = usb_submit_urb(to_send->l_urb, GFP_KERNEL);
		if (retval) {
			pr_err("%s l_urb failed submitting write urb, error %d\n", __func__, retval);
			usb_free_coherent(l_urb->dev, l_urb->transfer_buffer_length, l_urb->transfer_buffer, l_urb->transfer_dma);
			usb_free_urb(l_urb);
			return retval;
		}

		r_urb = to_send->r_urb;
		retval = usb_submit_urb(r_urb, GFP_KERNEL);
		if (retval) {
			pr_err("%s r_urb failed submitting write urb, error %d\n", __func__, retval);
			usb_free_coherent(r_urb->dev, r_urb->transfer_buffer_length, r_urb->transfer_buffer, r_urb->transfer_dma);
			usb_free_urb(r_urb);
			return retval;
		}
		list_del(&to_send->node);

		mutex_lock(&clubb_priv->lock);
		clubb_priv->hwptr_done += to_send->r_urb->transfer_buffer_length;
		clubb_priv->hwptr_done += to_send->l_urb->transfer_buffer_length;
		//if (clubb_priv->hwptr_done >= 24000)//???????
		//	clubb_priv->hwptr_done -= 24000;
		mutex_unlock(&clubb_priv->lock);
		trace_clubb_1(__func__, "clubb_priv->hwptr_done", clubb_priv->hwptr_done);

		schedule_delayed_work(&clubb_priv->send_worker, msecs_to_jiffies(1));
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		clubb_priv->playing = 0;
		trace_clubb_0(__func__, "SNDRV_PCM_TRIGGER_STOP");
		break;
	default:
		break;
	}

	return 0;
}

static snd_pcm_uframes_t clubb_i2s_pointer(struct snd_pcm_substream *subs)
{
	struct snd_pcm_runtime *runtime = subs->runtime;

	trace_clubb_1(__func__, "hwptr_done", clubb_priv->hwptr_done);
	//mutex_lock(&clubb_priv->lock);
	//if (clubb_priv->hwptr_done >= 24000)//???????
	//	clubb_priv->hwptr_done -= 24000;
	//mutex_unlock(&clubb_priv->lock);

	return bytes_to_frames(runtime, clubb_priv->hwptr_done);
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

static int clubb_pcm_open(struct snd_pcm_substream *substream)
{
	trace_clubb(__func__);
	clubb_priv->pkg_id = 0;
	clubb_priv->playing = 0;

	snd_soc_set_runtime_hwparams(substream, &clubb_pcm_hw);

	INIT_DELAYED_WORK(&clubb_priv->send_worker, clubb_urb_sender);

	return 0;
}

static int clubb_i2s_probe(struct snd_soc_dai *dai)
{
	trace_clubb(__func__);
	return 0;
}

static struct snd_soc_dai_ops clubb_i2s_dai_ops = {
	.trigger	= clubb_i2s_trigger,
};

static struct snd_soc_dai_driver clubb_i2s_dai[] = {
	{
		.name	= "clubb-i2s-sai1",
		.probe	= clubb_i2s_probe,
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
	.open		= clubb_pcm_open,
	.copy_user	= &clubb_i2s_copy,
	.pointer	= &clubb_i2s_pointer,
};

const struct snd_soc_component_driver clubb_i2s_component = {
	.name	= "clubb-i2s-comp",
	.ops	= &clubb_i2s_pcm_ops,
	.non_legacy_dai_naming	= 1,
};

static int clubb_usb_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	int ret = 0;
	struct usb_device *udev = interface_to_usbdev(intf);

	clubb_priv = kzalloc(sizeof(struct clubb_data), GFP_KERNEL);
	if (!clubb_priv)
		return -ENOMEM;

	dev_dbg(&intf->dev, "%s\n", __func__);

	INIT_LIST_HEAD(&clubb_priv->pending_list);
	clubb_priv->udev = udev;
	clubb_priv->udev->dev.init_name = "clubb-i2s";
	mutex_init(&clubb_priv->lock);

	ret = devm_snd_soc_register_component(&udev->dev, &clubb_i2s_component, clubb_i2s_dai,
					      ARRAY_SIZE(clubb_i2s_dai));
	if (ret)
		dev_err(&intf->dev, "Could not register DAI: %d\n", ret);

	return ret;
}

static void clubb_usb_disconnect(struct usb_interface *intf)
{
	dev_dbg(&intf->dev, "%s\n", __func__);
}

static const struct usb_device_id clubb_usb_id_table[] = {
	{ USB_DEVICE(0x0429, 0x00f0) },
	{}
};
MODULE_DEVICE_TABLE(usb, clubb_usb_id_table);

static struct usb_driver clubb_driver = {
	.name		= "Clubb_I2S",
	.probe		= clubb_usb_probe,
	.disconnect	= clubb_usb_disconnect,
	.id_table	= clubb_usb_id_table,
};

module_usb_driver(clubb_driver);

MODULE_AUTHOR("Lucas Tanure <tanureal@opensource.cirrus.com>");
MODULE_DESCRIPTION("Driver for Cirrus USB Bus Bridge");
MODULE_LICENSE("GPL v2");
