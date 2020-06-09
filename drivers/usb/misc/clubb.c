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

#define DRV_NAME "clubb-i2s-comp"

/*
 * Left and right are sent to separate endpoints so max period size
 * is 2* max USB packet size
 */
#define CLUBB_PERIOD_BYTES_MAX	1024	/* DMA transfer/period size */
#define CLUBB_PERIOD_BYTES_MIN	2	/* DMA transfer/period size */
#define CLUBB_PERIODS_MAX		6000	/* Max periods in buffer */
#define CLUBB_PERIODS_MIN		1	/* Min periods in buffer */
#define CLUBB_BUFFER_BYTES_MAX	(CLUBB_PERIOD_BYTES_MAX * CLUBB_PERIODS_MAX)

#define CREATE_TRACE_POINTS
#include <trace/events/clubb.h>

struct urbs_pending {
	unsigned long id;
	unsigned long sub_id;
	struct urb *l_urb;
	struct urb *r_urb;
	unsigned long end_jiffies;
	int urbs_in_transmission;
	struct clubb_data *priv;
	struct snd_pcm_substream *sub;
	struct list_head node;
};

struct clubb_data {
	struct usb_device *udev;
	struct delayed_work send_worker;
	unsigned int hwptr_done;
	volatile int playing;
	unsigned long pkg_id;
	unsigned long jiffies_per_period;
	unsigned long period_ptr;
	unsigned long period_size;
	unsigned long urbs_sent_ok;
	spinlock_t lock;
	struct list_head pending_list;
};

static void clubb_bulk_callback(struct urb *urb)
{
	struct urbs_pending *urbs = (struct urbs_pending *)urb->context;
	struct clubb_data *priv = urbs->priv;
	struct usb_device *udev = priv->udev;
	int status = urb->status;
	int urbs_remaining;
	unsigned long delay, jiffies_now;
	int period_elapsed = 0;
	unsigned long irq_flags;

//	dev_info(&udev->dev, "clubb_bulk_callback: %s urb=%p bulk status: %d\n",
//			urb == urbs->l_urb ? "left" : "right",
//			urb, status);

	if (status && !(status == -ENOENT || status == -ECONNRESET || status == -ESHUTDOWN)) {
		dev_err(&udev->dev, "urb=%p bulk status: %d (ok count=%lu)\n",
			urb, status, priv->urbs_sent_ok);
		trace_clubb("Error sending urb!!!!!!!!!!!!!!");
	}
	trace_clubb_2(__func__, "urbs->id", urbs->id, " urbs->sub_id ", urbs->sub_id);

	spin_lock_irqsave(&priv->lock, irq_flags);
	++priv->urbs_sent_ok;
	urbs_remaining = --urbs->urbs_in_transmission;
	priv->period_ptr += urb->transfer_buffer_length;
	if (priv->period_ptr >= priv->period_size) {
		priv->period_ptr -= priv->period_size;
		period_elapsed = 1;
	}
	spin_unlock_irqrestore(&priv->lock, irq_flags);

//	dev_info(&udev->dev, "clubb_bulk_callback: txlen=%u new period_ptr=%lu\n",
//			urb->transfer_buffer_length, priv->period_ptr);

	usb_free_coherent(urb->dev, urb->transfer_buffer_length, urb->transfer_buffer, urb->transfer_dma);
	usb_free_urb(urb);
	urb = NULL;

	if (!priv->playing)
		return;

	/*
	 * More URBs to be sent in this period?
	 * A period can't elapse until we've sent all URBs in the period.
	 */
	if (urbs_remaining > 0)
		return;

	if (period_elapsed) {
//		dev_info(&udev->dev, "period\n");
		snd_pcm_period_elapsed(urbs->sub);
	}

	/*
	 * urbs->end_jiffes isn't changed after it was created so
	 * we don't need to lock to read it.
	 * Take a snapshot of jiffes so that we are calculating using
	 * the same value that we tested in the if().
	 */
	jiffies_now = jiffies;
/*	if (time_before(urbs->end_jiffies, jiffies_now))
		delay = 0; // next packet already overdue
	else
		delay = urbs->end_jiffies - jiffies_now;
*/
	delay = jiffies_now + priv->jiffies_per_period;

//	dev_info(&udev->dev, "rq %lu\n", delay);
	schedule_delayed_work(&priv->send_worker, delay);

	kfree(urbs);
}

static int clubb_create_lr_urb(struct clubb_data *priv, struct snd_pcm_substream *sub,
			       uint16_t *buffer, unsigned long bytes, unsigned long sub_id)
{
	struct usb_device *udev = priv->udev;
	struct urbs_pending *urbs;
	struct urb *l_urb, *r_urb;
	uint16_t *l_buf, *r_buf;
	unsigned int sample/*, schedule_work = 0*/;

//	dev_info(&priv->udev->dev, "create_lr_urb\n");

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
	urbs->urbs_in_transmission = 2;
	urbs->id = priv->pkg_id;
	urbs->sub_id = sub_id;
	urbs->priv = priv;
	urbs->sub = sub;

	usb_fill_bulk_urb(l_urb, udev, usb_sndbulkpipe(udev, 1), l_buf, bytes/2, clubb_bulk_callback, urbs);
	l_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	usb_fill_bulk_urb(r_urb, udev, usb_sndbulkpipe(udev, 2), r_buf, bytes/2, clubb_bulk_callback, urbs);
	r_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	spin_lock(&priv->lock);
	list_add_tail(&urbs->node, &priv->pending_list);

	/* hwptr_done calculation adapted from sound/usb/pcm.c */
	priv->hwptr_done += bytes;
	if (priv->hwptr_done >= snd_pcm_lib_buffer_bytes(sub))
		priv->hwptr_done -= snd_pcm_lib_buffer_bytes(sub);
	spin_unlock(&priv->lock);

	trace_clubb_3(__func__, "urbs->id", urbs->id, "urbs->sub_id", urbs->sub_id, "bytes", bytes);

	return 0;
}

static int clubb_i2s_copy(struct snd_pcm_substream *sub, int channel, unsigned long hwoff,
			  void __user *user_buf, unsigned long bytes)
{
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(sub->private_data, DRV_NAME);
	struct clubb_data *priv = snd_soc_component_get_drvdata(component);
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
		writesize = min_t(unsigned long, bytes, CLUBB_PERIOD_BYTES_MAX);
		ret = clubb_create_lr_urb(priv, sub, (uint16_t *)(&buffer[pos]), writesize, sub_id);
		if (ret)
			return ret;
		pos += writesize;
		bytes -= writesize;
		sub_id++;
	}
	kfree(buffer);
	priv->pkg_id++;
	return 0;
}

void clubb_urb_sender(struct work_struct *work)
{
	struct clubb_data *priv = container_of(work, struct clubb_data, send_worker.work);
	struct urbs_pending *to_send;
//	unsigned long bytes_sent;
	unsigned long jiffies_delay;
	int retval = 0;

	if (!priv->playing)
		return;

	spin_lock(&priv->lock);
	to_send = list_first_entry_or_null(&priv->pending_list, struct urbs_pending, node);
	if (to_send)
		list_del(&to_send->node);
	spin_unlock(&priv->lock);

	if (!to_send) {
		/*
		 * Underflow: really we should sent a silent period here, but
		 * for now just requeue after a period interval.
		 */
		dev_warn(&priv->udev->dev, "underflow\n");
		schedule_delayed_work(&priv->send_worker,
							  priv->jiffies_per_period);
		return;
	} else {
		trace_clubb_2(__func__, "to_send->id", to_send->id, "to_send->sub_id", to_send->sub_id);

//		bytes_sent = to_send->l_urb->transfer_buffer_length +
//					 to_send->r_urb->transfer_buffer_length;
		jiffies_delay = priv->jiffies_per_period;

		/*
		 * end_jiffies must be valid when the callback runs
		 * so must be set before submitting any URBs
		 */
		to_send->end_jiffies = jiffies + jiffies_delay;

//		dev_info(&priv->udev->dev, "+sub l\n");
		retval = usb_submit_urb(to_send->l_urb, GFP_ATOMIC);
//		dev_info(&priv->udev->dev, "-sub l\n");
		if (retval) {
			dev_err(&priv->udev->dev,
				    "%s l_urb failed submitting write urb, error %d\n", __func__, retval);
			return;
		}

//		dev_info(&priv->udev->dev, "+sub r\n");
		retval = usb_submit_urb(to_send->r_urb, GFP_ATOMIC);
//		dev_info(&priv->udev->dev, "-sub r\n");
		if (retval) {
			dev_err(&priv->udev->dev,
					"%s r_urb failed submitting write urb, error %d\n", __func__, retval);
			return;
		}
	}
}

int clubb_i2s_trigger(struct snd_pcm_substream *sub, int cmd, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(sub->private_data, DRV_NAME);
	struct clubb_data *priv = snd_soc_component_get_drvdata(component);

	trace_clubb(__func__);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
//		dev_info(&priv->udev->dev, "TRIGGER_START\n");
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
		trace_clubb_0(__func__, "SNDRV_PCM_TRIGGER_STOP");
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
	struct clubb_data *priv = snd_soc_component_get_drvdata(component);
	struct snd_pcm_runtime *runtime = sub->runtime;
	unsigned long hwptr_done;
	unsigned long irq_flags;

	spin_lock_irqsave(&priv->lock, irq_flags);
	hwptr_done = priv->hwptr_done;
	spin_unlock_irqrestore(&priv->lock, irq_flags);

//	dev_info(&priv->udev->dev, "hwptr_done=%lu\n", hwptr_done);
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
	struct clubb_data *priv = snd_soc_component_get_drvdata(component);
	unsigned long bufsize;

        bufsize = snd_pcm_lib_buffer_bytes(sub);
        priv->period_size = snd_pcm_lib_period_bytes(sub);

        dev_info(&priv->udev->dev,
		 "%s (buf_size %lu) (period_size %lu)\n",
		 __func__, bufsize, priv->period_size);

        return 0;
}

static int clubb_pcm_open(struct snd_pcm_substream *sub)
{
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(sub->private_data, DRV_NAME);
	struct clubb_data *priv = snd_soc_component_get_drvdata(component);

	trace_clubb(__func__);
	priv->pkg_id = 0;
	priv->playing = 0;

	snd_soc_set_runtime_hwparams(sub, &clubb_pcm_hw);

	INIT_DELAYED_WORK(&priv->send_worker, clubb_urb_sender);

	return 0;
}

static int clubb_i2s_hw_params(struct snd_pcm_substream *sub, struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(sub->private_data, DRV_NAME);
	struct clubb_data *priv = snd_soc_component_get_drvdata(component);
	unsigned long frames_per_second = params_rate(params);
	unsigned long frames_per_period = params_period_size(params);
	unsigned long jiffies_per_second = msecs_to_jiffies(1000);
	priv->jiffies_per_period = (jiffies_per_second * frames_per_period) / frames_per_second;

	dev_info(&priv->udev->dev, "jiffies_per_second %lu\n", jiffies_per_second);
	return 0;
}

static struct snd_soc_dai_ops clubb_i2s_dai_ops = {
	.trigger	= &clubb_i2s_trigger,
	.hw_params	= &clubb_i2s_hw_params,
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
};

static int clubb_usb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct clubb_data *priv;
	int ret = 0;

	priv = devm_kzalloc(&udev->dev, sizeof(struct clubb_data), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_dbg(&intf->dev, "%s\n", __func__);

	INIT_LIST_HEAD(&priv->pending_list);
	priv->udev = udev;
	priv->udev->dev.init_name = "clubb-i2s";
	spin_lock_init(&priv->lock);

	dev_set_drvdata(&udev->dev, priv);

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
