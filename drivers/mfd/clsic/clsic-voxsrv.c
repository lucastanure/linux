/*
 * clsic-voxsrv.c -- CLSIC Voice Service
 *
 * Copyright 2017 CirrusLogic, Inc.
 *
 * Author: Nikesh Oswal <Nikesh.Oswal@cirrus.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <sound/compress_driver.h>
#include <sound/soc.h>
#include <uapi/sound/compress_params.h>
#include <linux/circ_buf.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/mfd/clsic/core.h>
#include <linux/mfd/clsic/message.h>
#include <linux/mfd/clsic/irq.h>

#include "clsic-trace.h"

#define VOX_MAX_CMD_SZ		(4 * 1024)
#define VOX_MAX_RSP_SZ		(4 * 1024)
#define VOX_RSP_QUEUE_SZ	(100)
#define VOX_NTY_QUEUE_SZ	(100)

#define VOX_CMD_CTRL		(0)
#define VOX_RSP_CTRL		(1)
#define VOX_RSP_COUNT_CTRL	(2)
#define VOX_RSP_POP_CTRL	(3)
#define VOX_NTY_CTRL		(4)
#define VOX_NTY_COUNT_CTRL	(5)
#define VOX_NTY_POP_CTRL	(6)
#define VOX_INSTALL_PHRASE_CTRL	(7)
#define VOX_ALSA_CTRL_COUNT	(8)

#define PHRASE_VDT1		(0)
#define PHRASE_VDT2		(1)
#define PHRASE_UDT		(2)
#define PHRASE_SECURE		(3)
#define PHRASE_TI		(4)
#define PHRASE_COUNT		(5)

#define ROUNDUP_POWER2(_n, _m)	(((_n) + ((_m) - 1)) & ~((_m) - 1))

/* TODO: may require tuning */
#define VOX_ASR_MIN_FRAGMENT_SZ	0
#define VOX_ASR_MAX_FRAGMENT_SZ	307200
#define VOX_ASR_MIN_FRAGMENTS	4
#define VOX_ASR_MAX_FRAGMENTS	256

static const char *phrase_text[PHRASE_COUNT] = {
	[PHRASE_VDT1]	= "Vdt1",
	[PHRASE_VDT2]	= "Vdt2",
	[PHRASE_UDT]	= "Udt",
	[PHRASE_SECURE]	= "Secure",
	[PHRASE_TI]	= "Ti",
};

static struct {
	const char *file;
} phrase_files[PHRASE_COUNT] = {
	[PHRASE_VDT1]	= { .file = "bpb.p00" },
	[PHRASE_VDT2]	= { .file = "bpb.p01" },
	[PHRASE_UDT]	= { .file = "bpb.p02" },
	[PHRASE_SECURE]	= { .file = "bpb.p03" },
	[PHRASE_TI]	= { .file = "bpb.p04" },
};

struct clsic_asr_stream_buf {
	void *data;

	size_t read_idx;
	size_t write_idx;

	size_t size;
	size_t frag_sz;
};

struct clsic_asr_stream {
	struct clsic_asr_stream_buf buf;

	struct snd_compr_stream *stream;

	unsigned int block_sz;
	unsigned int copied_total;

	unsigned int sample_rate;

	bool error;

	struct task_struct *wait_for_trigger;
	struct completion trigger_heard;
};

struct clsic_vox {
	struct clsic *clsic;
	struct clsic_service *service;

	/* only one command can be being sent at any point */
	uint8_t *cmd;

	/* notification data structures */
	struct mutex ntylock;
	uint8_t *nty;
	uint32_t nty_write_head;
	uint32_t nty_read_head;
	uint32_t nty_count;

	/* Response data structures */
	struct mutex rsplock;
	struct clsic_message **rsp;
	uint32_t rsp_write_head;
	uint32_t rsp_read_head;
	uint32_t rsp_count;

	/* KControl data structures */
	struct snd_kcontrol_new ctrls[VOX_ALSA_CTRL_COUNT];
	char ctrls_name[VOX_ALSA_CTRL_COUNT][SNDRV_CTL_ELEM_ID_NAME_MAXLEN];

	struct soc_bytes_ext cmd_ext;
	struct soc_bytes_ext rsp_ext;
	struct soc_mixer_control rsp_count_mc;
	struct soc_bytes_ext nty_ext;
	struct soc_mixer_control nty_count_mc;
	struct soc_enum phr_inst_enum;

	/* The trigger detect callback */
	void (*trig_det_cb)(struct clsic *clsic, struct clsic_service *service);

	/* ASR data stream */
	struct clsic_asr_stream asr_stream;
};

static int vox_stub_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol);
static enum clsic_message_cb_ret clsic_vox_rsp_handler(struct clsic *clsic,
						     struct clsic_message *msg);
static int clsic_vox_nty_handler(struct clsic *clsic,
				 struct clsic_service *service,
				 struct clsic_message *msg);
static void clsic_vox_service_stop(struct clsic *clsic,
				   struct clsic_service *service);
static int vox_cmd_tlv_put(struct snd_kcontrol *kcontrol,
				    int op_flag,
				    unsigned int size,
				    unsigned int __user *tlv);

static int vox_rsp_count_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol);
static int vox_rsp_tlv_get(struct snd_kcontrol *kcontrol,
			   int op_flag,
			   unsigned int size,
			   unsigned int __user *tlv);
static int vox_rsp_pop(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol);
static int vox_nty_count_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol);
static int vox_nty_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol);
static int vox_nty_pop(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol);
static int vox_install_phrase(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol);

/* Note: this will not work if there was more than one vox service */
static struct clsic_vox *clsic_get_vox_from_core(struct clsic *clsic)
{
	uint32_t srvNum;

	for (srvNum = 0; srvNum < CLSIC_SERVICE_COUNT; srvNum++) {
		if ((clsic->service_handlers[srvNum] != NULL) &&
		    (clsic->service_handlers[srvNum]->service_type ==
		     CLSIC_SRV_TYPE_VOX)) {
			return (struct clsic_vox *)
				clsic->service_handlers[srvNum]->data;
		}
	}
	return NULL;
}

/*
 * XXX I think this callback implementation needs to be thought through a bit
 * more to close the logical loops as I'm not sure how this would work yet. The
 * obvious snags are:
 *
 * - the unknown caller would need to pass in the clsic structure reference and
 *   it's unclear how it would get that information
 * - if it didn't have the clsic structure then core holds a list of clsic
 *   devices, but again it is unclear which one it should be registering with
 * - the get vox_from_core assumes that there will only be one instance of vox,
 *   which is probably a safe bet but not a generic pattern
 * - I'd have though the callback would be a "void cb(void *..)" type of
 *   function and we'd need to stash a data pointer for the client to use
 */
int clsic_vox_reg_trigger_det_cb(struct clsic *clsic,
				 void (*trig_det_cb)(struct clsic *clsic,
						 struct clsic_service *service))
{
	struct clsic_vox *vox = clsic_get_vox_from_core(clsic);

	if (vox == NULL)
		return -ENODEV;

	mutex_lock(&vox->ntylock);
	vox->trig_det_cb = trig_det_cb;
	mutex_unlock(&vox->ntylock);

	return 0;
}
EXPORT_SYMBOL_GPL(clsic_vox_reg_trigger_det_cb);

int clsic_vox_reset_trigger_det_cb(struct clsic *clsic)
{
	struct clsic_vox *vox = clsic_get_vox_from_core(clsic);

	if (vox == NULL)
		return -ENODEV;

	mutex_lock(&vox->ntylock);
	vox->trig_det_cb = NULL;
	mutex_unlock(&vox->ntylock);

	return 0;
}
EXPORT_SYMBOL_GPL(clsic_vox_reset_trigger_det_cb);

int clsic_vox_service_start(struct clsic *clsic, struct clsic_service *service)
{
	struct clsic_vox *vox;

	/*
	 * In the reenumeration case the service handler structure may already
	 * be correctly configured as the core service infrastructure will call
	 * stop() on services if they change.
	 */
	if ((service->stop == &clsic_vox_service_stop) &&
	    (service->data != NULL)) {
		clsic_dbg(clsic, "%p reenumerating", service);
		/*
		 * Check the private data structure is correct
		 * vox = handler->data;
		 */
		return 0;
	}

	/* Not reenumerating, start vox for the first time */
	vox = kzalloc(sizeof(struct clsic_vox), GFP_KERNEL);
	if (vox == NULL)
		goto no_mem_vox;

	vox->cmd = kzalloc(VOX_MAX_CMD_SZ, GFP_DMA | GFP_KERNEL);
	if (vox->cmd == NULL)
		goto no_mem_cmd;

	vox->rsp = kzalloc(VOX_RSP_QUEUE_SZ *
			   sizeof(struct clsic_message *), GFP_KERNEL);
	if (vox->rsp == NULL)
		goto no_mem_rsp;

	vox->nty = kzalloc(VOX_NTY_QUEUE_SZ * CLSIC_FIXED_MSG_SZ,
			   GFP_KERNEL);
	if (vox->nty == NULL)
		goto no_mem_nty;

	mutex_init(&vox->ntylock);
	mutex_init(&vox->rsplock);

	snprintf(vox->ctrls_name[VOX_CMD_CTRL], SNDRV_CTL_ELEM_ID_NAME_MAXLEN,
		 "Service-%04x-%d Command", service->service_type,
		 service->service_instance);
	vox->cmd_ext.max = VOX_MAX_CMD_SZ;
	vox->ctrls[VOX_CMD_CTRL].name = vox->ctrls_name[VOX_CMD_CTRL];
	vox->ctrls[VOX_CMD_CTRL].info = snd_soc_bytes_info_ext;
	vox->ctrls[VOX_CMD_CTRL].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->ctrls[VOX_CMD_CTRL].tlv.c = vox_cmd_tlv_put;
	vox->ctrls[VOX_CMD_CTRL].private_value =
		(unsigned long)(&(vox->cmd_ext));
	vox->ctrls[VOX_CMD_CTRL].access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |
		SNDRV_CTL_ELEM_ACCESS_TLV_WRITE |
		SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK |
		SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	snprintf(vox->ctrls_name[VOX_RSP_CTRL], SNDRV_CTL_ELEM_ID_NAME_MAXLEN,
		 "Service-%04x-%d Response", service->service_type,
		 service->service_instance);
	vox->rsp_ext.max = VOX_MAX_RSP_SZ;
	vox->ctrls[VOX_RSP_CTRL].name = vox->ctrls_name[VOX_RSP_CTRL];
	vox->ctrls[VOX_RSP_CTRL].info = snd_soc_bytes_info_ext;
	vox->ctrls[VOX_RSP_CTRL].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->ctrls[VOX_RSP_CTRL].tlv.c = vox_rsp_tlv_get;
	vox->ctrls[VOX_RSP_CTRL].private_value =
		(unsigned long)(&(vox->rsp_ext));
	vox->ctrls[VOX_RSP_CTRL].access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |
		SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK |
		SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	snprintf(vox->ctrls_name[VOX_RSP_COUNT_CTRL],
		 SNDRV_CTL_ELEM_ID_NAME_MAXLEN,
		 "Service-%04x-%d Response Count", service->service_type,
		 service->service_instance);
	vox->rsp_count_mc.reg = vox->rsp_count_mc.rreg = 0;
	vox->rsp_count_mc.shift = vox->rsp_count_mc.rshift = 0;
	vox->rsp_count_mc.invert = vox->rsp_count_mc.autodisable = 0;
	vox->rsp_count_mc.min = 0;
	vox->rsp_count_mc.max = vox->rsp_count_mc.platform_max =
		VOX_RSP_QUEUE_SZ;
	vox->ctrls[VOX_RSP_COUNT_CTRL].name =
		vox->ctrls_name[VOX_RSP_COUNT_CTRL];
	vox->ctrls[VOX_RSP_COUNT_CTRL].info = snd_soc_info_volsw;
	vox->ctrls[VOX_RSP_COUNT_CTRL].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->ctrls[VOX_RSP_COUNT_CTRL].get = vox_rsp_count_get;
	vox->ctrls[VOX_RSP_COUNT_CTRL].private_value =
		(unsigned long)(&(vox->rsp_count_mc));
	vox->ctrls[VOX_RSP_COUNT_CTRL].access = SNDRV_CTL_ELEM_ACCESS_READ |
		SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	snprintf(vox->ctrls_name[VOX_RSP_POP_CTRL],
		 SNDRV_CTL_ELEM_ID_NAME_MAXLEN,
		 "Service-%04x-%d Response Pop", service->service_type,
		 service->service_instance);
	vox->ctrls[VOX_RSP_POP_CTRL].name =
		vox->ctrls_name[VOX_RSP_POP_CTRL];
	vox->ctrls[VOX_RSP_POP_CTRL].info = snd_soc_info_bool_ext;
	vox->ctrls[VOX_RSP_POP_CTRL].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->ctrls[VOX_RSP_POP_CTRL].put = vox_rsp_pop;
	vox->ctrls[VOX_RSP_POP_CTRL].get = vox_stub_get;
	vox->ctrls[VOX_RSP_POP_CTRL].private_value = (unsigned long)(vox);
	vox->ctrls[VOX_RSP_POP_CTRL].access = SNDRV_CTL_ELEM_ACCESS_READ |
		SNDRV_CTL_ELEM_ACCESS_WRITE | SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	snprintf(vox->ctrls_name[VOX_NTY_CTRL], SNDRV_CTL_ELEM_ID_NAME_MAXLEN,
		 "Service-%04x-%d Notification", service->service_type,
		 service->service_instance);
	vox->nty_ext.max = CLSIC_FIXED_MSG_SZ;
	vox->ctrls[VOX_NTY_CTRL].name = vox->ctrls_name[VOX_NTY_CTRL];
	vox->ctrls[VOX_NTY_CTRL].info = snd_soc_bytes_info_ext;
	vox->ctrls[VOX_NTY_CTRL].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->ctrls[VOX_NTY_CTRL].get = vox_nty_get;
	vox->ctrls[VOX_NTY_CTRL].private_value =
		(unsigned long)(&(vox->nty_ext));
	vox->ctrls[VOX_NTY_CTRL].access = SNDRV_CTL_ELEM_ACCESS_READ |
		SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	snprintf(vox->ctrls_name[VOX_NTY_COUNT_CTRL],
		 SNDRV_CTL_ELEM_ID_NAME_MAXLEN,
		 "Service-%04x-%d Notification Count", service->service_type,
		 service->service_instance);
	vox->nty_count_mc.reg = vox->nty_count_mc.rreg = 0;
	vox->nty_count_mc.shift = vox->nty_count_mc.rshift = 0;
	vox->nty_count_mc.invert = vox->nty_count_mc.autodisable = 0;
	vox->nty_count_mc.min = 0;
	vox->nty_count_mc.max = vox->nty_count_mc.platform_max =
		VOX_NTY_QUEUE_SZ;
	vox->ctrls[VOX_NTY_COUNT_CTRL].name =
		vox->ctrls_name[VOX_NTY_COUNT_CTRL];
	vox->ctrls[VOX_NTY_COUNT_CTRL].info = snd_soc_info_volsw;
	vox->ctrls[VOX_NTY_COUNT_CTRL].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->ctrls[VOX_NTY_COUNT_CTRL].get = vox_nty_count_get;
	vox->ctrls[VOX_NTY_COUNT_CTRL].private_value =
		(unsigned long)(&(vox->nty_count_mc));
	vox->ctrls[VOX_NTY_COUNT_CTRL].access = SNDRV_CTL_ELEM_ACCESS_READ |
		SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	snprintf(vox->ctrls_name[VOX_NTY_POP_CTRL],
		 SNDRV_CTL_ELEM_ID_NAME_MAXLEN,
		 "Service-%04x-%d Notification Pop", service->service_type,
		 service->service_instance);
	vox->ctrls[VOX_NTY_POP_CTRL].name =
		vox->ctrls_name[VOX_NTY_POP_CTRL];
	vox->ctrls[VOX_NTY_POP_CTRL].info = snd_soc_info_bool_ext;
	vox->ctrls[VOX_NTY_POP_CTRL].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->ctrls[VOX_NTY_POP_CTRL].put = vox_nty_pop;
	vox->ctrls[VOX_NTY_POP_CTRL].get = vox_stub_get;
	vox->ctrls[VOX_NTY_POP_CTRL].private_value = (unsigned long)(vox);
	vox->ctrls[VOX_NTY_POP_CTRL].access = SNDRV_CTL_ELEM_ACCESS_READ |
		SNDRV_CTL_ELEM_ACCESS_WRITE | SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	snprintf(vox->ctrls_name[VOX_INSTALL_PHRASE_CTRL],
		 SNDRV_CTL_ELEM_ID_NAME_MAXLEN,
		 "Service-%04x-%d Install Phrase", service->service_type,
		 service->service_instance);
	vox->phr_inst_enum.reg = vox->phr_inst_enum.shift_l =
		vox->phr_inst_enum.shift_r = 0;
	vox->phr_inst_enum.mask = 0;
	vox->phr_inst_enum.items = ARRAY_SIZE(phrase_text);
	vox->phr_inst_enum.texts = phrase_text;
	vox->ctrls[VOX_INSTALL_PHRASE_CTRL].name =
		vox->ctrls_name[VOX_INSTALL_PHRASE_CTRL];
	vox->ctrls[VOX_INSTALL_PHRASE_CTRL].info = snd_soc_info_enum_double;
	vox->ctrls[VOX_INSTALL_PHRASE_CTRL].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->ctrls[VOX_INSTALL_PHRASE_CTRL].put = vox_install_phrase;
	vox->ctrls[VOX_INSTALL_PHRASE_CTRL].get = vox_stub_get;
	vox->ctrls[VOX_INSTALL_PHRASE_CTRL].private_value =
		(unsigned long)(&(vox->phr_inst_enum));
	vox->ctrls[VOX_INSTALL_PHRASE_CTRL].access =
		SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_WRITE |
		SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	service->callback = &clsic_vox_nty_handler;
	service->stop = &clsic_vox_service_stop;
	service->kcontrol_count = VOX_ALSA_CTRL_COUNT;
	service->kcontrols = vox->ctrls;
	service->data = vox;
	vox->clsic = clsic;
	vox->service = service;

	init_completion(&vox->asr_stream.trigger_heard);

	return 0;

no_mem_nty:
	kfree(vox->rsp);
no_mem_rsp:
	kfree(vox->cmd);
no_mem_cmd:
	kfree(vox);
no_mem_vox:
	return -ENOMEM;
}

static void clsic_vox_service_stop(struct clsic *clsic,
				   struct clsic_service *service)
{
	struct clsic_vox *vox = service->data;

	if (vox) {
		kfree(vox->cmd);
		kfree(vox->rsp);
		kfree(vox->nty);
		kfree(vox);
	}
}

static bool clsic_vox_is_msg_allowed_over_alsa_ctl(uint8_t msgid)
{
	switch (msgid) {
	/* All modes */
	case CLSIC_VOX_MSG_CR_SET_MODE:
	case CLSIC_VOX_MSG_CR_GET_MODE:
	case CLSIC_VOX_MSG_CR_BARGE_IN_ENA:
	case CLSIC_VOX_MSG_CR_BARGE_IN_DIS:
	case CLSIC_VOX_MSG_CR_GET_DEBUG_INFO:
	/* Enrol modes */
	case CLSIC_VOX_MSG_CR_INSTALL_USER_BEGIN:
	case CLSIC_VOX_MSG_CR_REP_START:
	case CLSIC_VOX_MSG_CR_INSTALL_USER_COMPLETE:
	/* Listen modes */
	case CLSIC_VOX_MSG_CR_LISTEN_START:
	case CLSIC_VOX_MSG_CR_SET_TRGR_DETECT:
	/* Stream modes */
	case CLSIC_VOX_MSG_CR_GET_TRGR_INFO:
	case CLSIC_VOX_MSG_CR_GET_AVAIL_ASR_DATA:
	case CLSIC_VOX_MSG_CR_AUTH_USER:
	/* Manage modes */
	case CLSIC_VOX_MSG_CR_IS_PHRASE_INSTALLED:
	case CLSIC_VOX_MSG_CR_IS_USER_INSTALLED:
	case CLSIC_VOX_MSG_CR_REMOVE_PHRASE:
	case CLSIC_VOX_MSG_CR_REMOVE_USER:
	case CLSIC_VOX_MSG_CR_GET_AUTH_KEY:
		return true;
	/*
	 * Install Phrase is supported via a custom alsa control which would
	 * read the phrase bundle file in the driver and install it via the
	 * vox, userspace piping of the binary blob using a tlv control is not
	 * permitted as this file is huge approx 0.5MB
	 */
	case CLSIC_VOX_MSG_CR_INSTALL_PHRASE:
	/*
	 * Getting asr data via alsa ctrls is not permitted because a) The asr
	 * block data can be huge upto 75KB and b) the data path for asr is
	 * restricted via the alsa compressed
	 */
	case CLSIC_VOX_MSG_CRA_GET_ASR_BLOCK:
		return false;
	/* Unknow Vox Cmd */
	default:
		return false;
	}
}

static enum clsic_message_cb_ret clsic_vox_rsp_handler(struct clsic *clsic,
						      struct clsic_message *msg)
{
	struct clsic_vox *vox = clsic_get_vox_from_core(clsic);
	uint32_t next_write_head;

	/* check is rsp size is more than permitted */
	if (msg->bulk_rxbuf_maxsize > VOX_MAX_RSP_SZ - CLSIC_FIXED_MSG_SZ) {
		clsic_dump_message(clsic, msg, "Err:VoxRspSizeOutOfRange");

		/*
		 * XXX this will cause the client to hang forever, should fail
		 * it somehow
		 */

		return CLSIC_MSG_RELEASED;
	}

	/*
	 * put the rsp in the queue first check if enough space is avail in the
	 * rsp queue, else flush out the oldest rsp from the queue
	 */
	mutex_lock(&vox->rsplock);
	next_write_head = (vox->rsp_write_head + 1) % VOX_RSP_QUEUE_SZ;
	if (next_write_head == vox->rsp_read_head) {
		clsic_dump_message(clsic, msg, "Err:VoxRspFlushedOut");
		vox->rsp_read_head =
			(vox->rsp_read_head + 1) % VOX_RSP_QUEUE_SZ;
	}
	vox->rsp[vox->rsp_write_head] = msg;
	vox->rsp_write_head = next_write_head;
	if (vox->rsp_write_head >= vox->rsp_read_head)
		vox->rsp_count = (vox->rsp_write_head - vox->rsp_read_head);
	else
		vox->rsp_count = (VOX_RSP_QUEUE_SZ -
				  (vox->rsp_read_head - vox->rsp_write_head));
	mutex_unlock(&vox->rsplock);

	/*
	 * XXX Need to signal that the count has changed
	 *
	 * clsic_codec_control_changed(clsic, &vox->ctrls[VOX_RSP_COUNT_CTRL]);
	 */

	return CLSIC_MSG_RETAINED;
}

/*
 * Some of our test tools attempt to read the value of all of the card
 * controls, return a value of 0 so as not to disturb them.
 */
static int vox_stub_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static int vox_rsp_count_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *) kcontrol->private_value;
	struct clsic_vox *vox =
		container_of(mc, struct clsic_vox, rsp_count_mc);

	mutex_lock(&vox->rsplock);
	ucontrol->value.integer.value[0] = (long)vox->rsp_count;
	mutex_unlock(&vox->rsplock);

	return 0;
}

static int vox_rsp_tlv_get(struct snd_kcontrol *kcontrol,
			   int op_flag,
			   unsigned int size,
			   unsigned int __user *tlv)
{
	struct soc_bytes_ext *bytes_ext =
		(struct soc_bytes_ext *) kcontrol->private_value;
	struct clsic_vox *vox =
		container_of(bytes_ext, struct clsic_vox, rsp_ext);
	int ret = 0;
	struct clsic_message *rsp;

	if (op_flag != SNDRV_CTL_TLV_OP_READ) {
		clsic_err(vox->clsic, "Err:%s op_flag unexpected value of %d.\n",
			  __func__, op_flag);
	}

	mutex_lock(&vox->rsplock);

	if (vox->rsp_count == 0) {
		goto err_unlock;
	} else {
		rsp = vox->rsp[vox->rsp_read_head];
		if (size < CLSIC_FIXED_MSG_SZ + rsp->bulk_rxbuf_maxsize) {
			ret = -EINVAL;
			goto err_unlock;
		}

		if (copy_to_user(tlv, rsp->response.raw, CLSIC_FIXED_MSG_SZ)) {
			ret = -EFAULT;
			goto err_unlock;
		}

		if (rsp->bulk_rxbuf_maxsize > 0) {
			if (copy_to_user(tlv + (CLSIC_FIXED_MSG_SZ
						/ sizeof(unsigned int)),
					 rsp->bulk_rxbuf,
					 rsp->bulk_rxbuf_maxsize)) {
				ret = -EFAULT;
				goto err_unlock;
			}
		}
	}

err_unlock:
	mutex_unlock(&vox->rsplock);

	return ret;
}

static int vox_rsp_pop(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct clsic_vox *vox =
		(struct clsic_vox *) kcontrol->private_value;
	struct clsic_message *rsp;
	int ret = 0;

	if (ucontrol->value.integer.value[0] == 0)
		goto no_pop_exit;

	mutex_lock(&vox->rsplock);
	if (vox->rsp_count == 0) {
		ret = -EINVAL;
	} else {
		rsp = vox->rsp[vox->rsp_read_head];
		clsic_release_msg(vox->clsic, rsp);
		vox->rsp_read_head =
			(vox->rsp_read_head + 1) % VOX_RSP_QUEUE_SZ;
		if (vox->rsp_write_head >= vox->rsp_read_head)
			vox->rsp_count =
				(vox->rsp_write_head - vox->rsp_read_head);
		else
			vox->rsp_count = (VOX_RSP_QUEUE_SZ -
					  (vox->rsp_read_head -
					   vox->rsp_write_head));
	}
	mutex_unlock(&vox->rsplock);

no_pop_exit:
	clsic_dbg(vox->clsic, "rsp_count: %d\n", vox->rsp_count);
	return ret;
}

static int clsic_vox_nty_handler(struct clsic *clsic,
				 struct clsic_service *service,
				 struct clsic_message *msg)
{
	struct clsic_vox *vox = service->data;
	bool invoke_trigdet_cb = false;
	uint32_t next_write_head;
	uint8_t *nty;

	/* check if its a nty */
	if (clsic_get_cran_frommsg(msg) != CLSIC_CRAN_NTY)
		return CLSIC_UNHANDLED;

	/* must be a known nty */
	switch (clsic_get_messageid(msg)) {
	case CLSIC_VOX_MSG_N_REP_COMPLETE:
	case CLSIC_VOX_MSG_N_LISTEN_ERR:
	case CLSIC_VOX_MSG_N_NEW_AUTH_RESULT:
		break;
	case CLSIC_VOX_MSG_N_TRGR_DETECT:
		invoke_trigdet_cb = true;
		break;
	default:
		return CLSIC_UNHANDLED;
	}

	/*
	 * put the nty in the queue first check if enough space is avail in the
	 * nty queue, else flush out the oldest nty from the queue
	 */
	mutex_lock(&vox->ntylock);

	next_write_head = (vox->nty_write_head + 1) % VOX_NTY_QUEUE_SZ;
	if (next_write_head == vox->nty_read_head) {
		clsic_dump_message(clsic, msg, "Err:VoxNtyFlushedOut");
		vox->nty_read_head =
			(vox->nty_read_head + 1) % VOX_NTY_QUEUE_SZ;
	}

	nty = vox->nty + (vox->nty_write_head * CLSIC_FIXED_MSG_SZ);
	memcpy(nty, msg->fsm.raw, CLSIC_FIXED_MSG_SZ);

	vox->nty_write_head = next_write_head;
	if (vox->nty_write_head >= vox->nty_read_head)
		vox->nty_count = (vox->nty_write_head - vox->nty_read_head);
	else
		vox->nty_count = (VOX_NTY_QUEUE_SZ -
				  (vox->nty_read_head - vox->nty_write_head));

	if (invoke_trigdet_cb && (vox->trig_det_cb != NULL))
		(vox->trig_det_cb)(vox->clsic, vox->service);

	mutex_unlock(&vox->ntylock);

	/*
	 * XXX Need to signal that the count has changed
	 *
	 * clsic_codec_control_changed(clsic, &vox->ctrls[VOX_NTY_COUNT_CTRL]);
	 */

	return 0;
}

static int vox_nty_count_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *) kcontrol->private_value;
	struct clsic_vox *vox =
		container_of(mc, struct clsic_vox, nty_count_mc);

	mutex_lock(&vox->ntylock);
	ucontrol->value.integer.value[0] = (long)vox->nty_count;
	mutex_unlock(&vox->ntylock);

	return 0;
}

static int vox_nty_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes_ext *bytes_ext = (struct soc_bytes_ext *)
		kcontrol->private_value;
	struct clsic_vox *vox =
		container_of(bytes_ext, struct clsic_vox, nty_ext);
	int ret = 0;
	uint8_t *nty;
	uint8_t nty_nop[CLSIC_FIXED_MSG_SZ];

	mutex_lock(&vox->ntylock);
	if (vox->nty_count == 0) {
		memset(&nty_nop, 0, CLSIC_FIXED_MSG_SZ);
		memcpy(ucontrol->value.bytes.data, &nty_nop,
		       CLSIC_FIXED_MSG_SZ);
	} else {
		nty = vox->nty + (vox->nty_read_head * CLSIC_FIXED_MSG_SZ);
		memcpy(ucontrol->value.bytes.data, nty, CLSIC_FIXED_MSG_SZ);
		clsic_dbg(vox->clsic,
			  "(nty_count: %d) %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
			  vox->nty_count,
			  nty[CLSIC_FSM0], nty[CLSIC_FSM1],
			  nty[CLSIC_FSM2], nty[CLSIC_FSM3],
			  nty[CLSIC_FSM4], nty[CLSIC_FSM5],
			  nty[CLSIC_FSM6], nty[CLSIC_FSM7],
			  nty[CLSIC_FSM8], nty[CLSIC_FSM9],
			  nty[CLSIC_FSM10], nty[CLSIC_FSM11]);
	}
	mutex_unlock(&vox->ntylock);

	return ret;
}

static int vox_nty_pop(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct clsic_vox *vox = (struct clsic_vox *) kcontrol->private_value;
	int ret = 0;

	if (ucontrol->value.integer.value[0] == 0)
		goto no_pop_exit;

	mutex_lock(&vox->ntylock);
	if (vox->nty_count == 0) {
		ret = -EINVAL;
	} else {
		vox->nty_read_head =
			(vox->nty_read_head + 1) % VOX_NTY_QUEUE_SZ;
		if (vox->nty_write_head >= vox->nty_read_head)
			vox->nty_count =
				(vox->nty_write_head - vox->nty_read_head);
		else
			vox->nty_count = (VOX_NTY_QUEUE_SZ -
				    (vox->nty_read_head - vox->nty_write_head));
	}
	mutex_unlock(&vox->ntylock);

no_pop_exit:
	clsic_dbg(vox->clsic, "nty_count: %d\n", vox->nty_count);
	return ret;
}

static void vox_check_pm(struct clsic *clsic, struct clsic_vox *vox)
{
	union clsic_vox_msg *mode_msg = (union clsic_vox_msg *) vox->cmd;
	uint8_t service_instance = vox->service->service_instance;

	if (mode_msg->cmd_set_mode.mode == CLSIC_VOX_MODE_IDLE) {
		/* mark VOX idle */
		clsic_pm_service_mark(clsic, service_instance, false);

	} else {
		/*
		 * Handle special case of CLSIC_VOX_MODE_LISTEN
		 * where we want secure processor to be turned off
		 * while hardware is waiting for trigger (so powered)
		 *
		 * The mixer will have an established route that will hold the
		 * device power on.
		 */
		if (mode_msg->cmd_set_mode.mode == CLSIC_VOX_MODE_LISTEN)
			clsic_pm_service_mark(clsic, service_instance, false);
		else
			/* mark VOX busy */
			clsic_pm_service_mark(clsic, service_instance, true);
	}
}

static int vox_cmd_tlv_put(struct snd_kcontrol *kcontrol,
			   int op_flag,
			   unsigned int size,
			   unsigned int __user *tlv)
{
	struct soc_bytes_ext *bytes_ext =
		(struct soc_bytes_ext *) kcontrol->private_value;
	struct clsic_vox *vox =
		container_of(bytes_ext, struct clsic_vox, cmd_ext);
	struct clsic_cmd_hdr *cmdhdr;
	struct clsic_blkcmd_hdr *bulkcmdhdr;
	uint8_t *tx_bulk = NULL;
	size_t tx_bulk_sz = 0;
	int ret = 0;

	if (op_flag == SNDRV_CTL_TLV_OP_READ)
		return 0;

	if (op_flag != SNDRV_CTL_TLV_OP_WRITE) {
		clsic_err(vox->clsic, "Err:%s op_flag unexpected value of %d.\n",
			  __func__, op_flag);
	}

	/* Copy the cmd into vox buffer */
	if (copy_from_user(vox->cmd, tlv, size))
		return -EFAULT;

	cmdhdr = (struct clsic_cmd_hdr *) vox->cmd;
	bulkcmdhdr = (struct clsic_blkcmd_hdr *) vox->cmd;

	/* check if its a cmd */
	if (clsic_get_cran(cmdhdr->sbc) != CLSIC_CRAN_CMD)
		return -EINVAL;

	/* check if the msg is exposed to alsa controls */
	if (!clsic_vox_is_msg_allowed_over_alsa_ctl(cmdhdr->msgid)) {
		/* XXX TODO
		 * DUMP_ERR_VOX_MSG(vox->clsic,
		 * "Err:VoxCmdNotAllowedOverAlsaCtrl",
		 * ((uint8_t *) data));
		 */
		clsic_err(vox->clsic, "Err:VoxCmdNotAllowedOverAlsaCtrl\n");
		return -EINVAL;
	}

	/* check if the size is valid */
	if (clsic_get_bulk_bit(cmdhdr->sbc) != 0) {
		if ((size - CLSIC_FIXED_MSG_SZ) <
		    ROUNDUP_POWER2(bulkcmdhdr->bulk_sz, 4)) {
			return -EINVAL;
		}
		tx_bulk = vox->cmd + CLSIC_FIXED_MSG_SZ;
		tx_bulk_sz = ROUNDUP_POWER2(bulkcmdhdr->bulk_sz, 4);
	} else {
		if (size < CLSIC_FIXED_MSG_SZ)
			return -EINVAL;
	}

	/* Insert the vox service instance */
	clsic_set_srv_inst(vox->cmd, vox->service->service_instance);

	if (cmdhdr->msgid == CLSIC_VOX_MSG_CR_SET_MODE)
		vox_check_pm(vox->clsic, vox);

	/*
	 * Send the cmd (don't provide a rsp buf let the msg layer allocate the
	 * rsp buffer when a rsp is received)
	 */
	ret = clsic_send_msg_async(vox->clsic,
				   (union t_clsic_generic_message *) vox->cmd,
				   tx_bulk, tx_bulk_sz,
				   CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN,
				   0, clsic_vox_rsp_handler);

	if (ret == 0) {
		/* XXX TODO
		 * DUMP_INFO_VOX_MSG(vox->clsic, "VoxCmdSent", (vox->cmd));
		 */
		clsic_dbg(vox->clsic, "VoxCmdSent\n");
	} else {
		/* XXX TODO
		 * DUMP_ERR_VOX_MSG(vox->clsic, "Err:VoxCmdSendFailure",
		 *		 (vox->cmd));
		 */
		clsic_err(vox->clsic, "Err:VoxCmdSendFailure\n");
	}

	return ret;
}

static uint8_t vox_convert_to_clsic_phraseid(unsigned int phraseid)
{
	if (phraseid == PHRASE_VDT1)
		return CLSIC_VOX_PHRASE_VDT1;
	else if (phraseid == PHRASE_TI)
		return CLSIC_VOX_PHRASE_TI;
	else
		return (uint8_t)phraseid;
}

static int vox_install_phrase(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *e = (struct soc_enum *) kcontrol->private_value;
	struct clsic_vox *vox =
		container_of(e, struct clsic_vox, phr_inst_enum);
	unsigned int phraseid = ucontrol->value.enumerated.item[0];
	int ret = 0;
	const struct firmware *fw;
	union clsic_vox_msg voxcmd;
	union clsic_vox_msg voxrsp;

	if (phraseid >= PHRASE_COUNT)
		return -EINVAL;

	ret = request_firmware(&fw, phrase_files[phraseid].file,
			       vox->clsic->dev);
	if (ret) {
		clsic_err(vox->clsic, "Failed to request phrase file %s",
			  phrase_files[phraseid].file);
		return ret;
	}

	if (fw->size % 4) {
		clsic_err(vox->clsic,
			  "Firmware file %s, size %d, is not multiple of 4",
			  phrase_files[phraseid].file, fw->size);
		release_firmware(fw);
		return -EBADF;
	}

	clsic_init_message((union t_clsic_generic_message *)&voxcmd,
			   vox->service->service_instance,
			   CLSIC_VOX_MSG_CR_INSTALL_PHRASE);
	voxcmd.cmd_install_phrase.hdr.bulk_sz = fw->size;
	voxcmd.cmd_install_phrase.phraseid =
		vox_convert_to_clsic_phraseid(phraseid);

	ret = clsic_send_msg_sync(vox->clsic,
			       (union t_clsic_generic_message *) voxcmd.raw_msg,
			       (union t_clsic_generic_message *) voxrsp.raw_msg,
			       &(fw->data[0]), fw->size, NULL, 0);

	release_firmware(fw);

	if (ret)
		return ret;

	if (voxrsp.rsp_install_phrase.hdr.err != CLSIC_ERR_NONE) {
		clsic_err(vox->clsic, "Phrase installation error %d",
			  voxrsp.rsp_install_phrase.hdr.err);
		return voxrsp.rsp_install_phrase.hdr.err;
	}

	clsic_dbg(vox->clsic, "Successfully installed phrase %d",
		  vox_convert_to_clsic_phraseid(phraseid));

	return 0;
}

static const struct {
	u32 id;
	struct snd_codec_desc desc;
} clsic_asr_stream_caps = {
	.id = SND_AUDIOCODEC_PCM,
	.desc = {
		.max_ch = 2,
		.sample_rates = { 16000 },
		.num_sample_rates = 1,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
};

int clsic_vox_asr_stream_open(struct clsic *clsic,
			      struct snd_compr_stream *stream)
{
	/*
	 * find vox service handler
	 *
	 * FIXME: for now an assumption is made that there is only one vox
	 *        service, which may not be the case in the future; to solve
	 *        this each instance of the vox service should be represented
	 *        by its own codec device
	 */
	struct clsic_service *handler =
		clsic_find_first_service(clsic, CLSIC_SRV_TYPE_VOX);
	struct clsic_vox *vox;

	if (handler == NULL) {
		clsic_err(clsic, "ASR stream support requires VOX service.\n");
		return -ENXIO;
	}
	vox = handler->data;

	if (vox->asr_stream.stream) {
		clsic_err(clsic, "ASR stream already active.\n");
		return -EBUSY;
	}

	if (stream->direction != SND_COMPRESS_CAPTURE) {
		clsic_err(clsic, "Only capture is supported for ASR stream.\n");
		return -EINVAL;
	}

	vox->asr_stream.stream = stream;
	vox->asr_stream.error = false;
	vox->asr_stream.copied_total = 0;

	stream->runtime->private_data = &vox->asr_stream;

	trace_clsic_vox_asr_stream_open(stream->direction);

	return 0;
}
EXPORT_SYMBOL_GPL(clsic_vox_asr_stream_open);

int clsic_vox_asr_stream_free(struct snd_compr_stream *stream)
{
	struct clsic_asr_stream *asr_stream = stream->runtime->private_data;
	struct clsic_vox *vox =
		container_of(asr_stream, struct clsic_vox, asr_stream);

	trace_clsic_vox_asr_stream_free(stream->direction,
					asr_stream->copied_total);

	kfree(asr_stream->buf.data);
	asr_stream->buf.size = 0;
	asr_stream->buf.frag_sz = 0;
	asr_stream->buf.read_idx = 0;
	asr_stream->buf.write_idx = 0;

	asr_stream->copied_total = 0;
	asr_stream->stream = NULL;
	complete(&asr_stream->trigger_heard);
	vox->trig_det_cb = NULL;

	return 0;
}
EXPORT_SYMBOL_GPL(clsic_vox_asr_stream_free);

int clsic_vox_asr_stream_block_sz(u32 block_size)
{
	switch (block_size) {
	case 0:
		return CLSIC_VOX_ASR_BLK_SZ_0;
	case 192:
		return CLSIC_VOX_ASR_BLK_SZ_192;
	case 384:
		return CLSIC_VOX_ASR_BLK_SZ_384;
	case 960:
		return CLSIC_VOX_ASR_BLK_SZ_960;
	case 1920:
		return CLSIC_VOX_ASR_BLK_SZ_1920;
	case 4800:
		return CLSIC_VOX_ASR_BLK_SZ_4800;
	case 9600:
		return CLSIC_VOX_ASR_BLK_SZ_9600;
	default:
		break;
	}

	return -EINVAL;
}

#define PCM_S16_LE_BYTES_PER_SAMPLE 2

int clsic_vox_asr_stream_set_params(struct snd_compr_stream *stream,
				    struct snd_compr_params *params)
{
	struct clsic_asr_stream *asr_stream = stream->runtime->private_data;
	struct clsic_vox *vox =
		container_of(asr_stream, struct clsic_vox, asr_stream);
	struct clsic *clsic = vox->clsic;
	size_t size, frag_sz = params->buffer.fragment_size;
	int block_sz, i, frame_sz;
	bool params_ok = true;

	frame_sz = params->codec.ch_in * PCM_S16_LE_BYTES_PER_SAMPLE;
	if (frag_sz % frame_sz) {
		clsic_err(clsic,
			  "%u is not a supported ASR stream fragment size.\n",
			  params->buffer.fragment_size);
		return -EINVAL;
	}
	block_sz = clsic_vox_asr_stream_block_sz(frag_sz / frame_sz);
	if (block_sz < 0) {
		clsic_err(clsic,
			  "%u is not a supported ASR stream fragment size.\n",
			  params->buffer.fragment_size);
		return -EINVAL;
	}

	params_ok = params_ok && (clsic_asr_stream_caps.id == params->codec.id);
	params_ok = params_ok &&
		(clsic_asr_stream_caps.desc.max_ch == params->codec.ch_in);
	params_ok = params_ok && (clsic_asr_stream_caps.desc.formats &
		(1 << params->codec.format));
	for (i = 0; i < clsic_asr_stream_caps.desc.num_sample_rates; ++i)
		if (clsic_asr_stream_caps.desc.sample_rates[i] ==
				params->codec.sample_rate)
			break;
	params_ok = params_ok &&
		(i < clsic_asr_stream_caps.desc.num_sample_rates);

	if (!params_ok) {
		clsic_err(clsic,
			  "Invalid params id=%u, ch=%u,%u, rate=%u fmt=%u\n",
			  params->codec.id, params->codec.ch_in,
			  params->codec.ch_out, params->codec.sample_rate,
			  params->codec.format);
		return -EINVAL;
	}

	asr_stream->block_sz = block_sz;

	size = frag_sz * params->buffer.fragments;
	asr_stream->buf.data = kmalloc(size, GFP_KERNEL);
	if (!asr_stream->buf.data)
		return -ENOMEM;
	asr_stream->buf.size = size;

	trace_clsic_vox_asr_stream_set_params(params, size);

	return 0;
}
EXPORT_SYMBOL_GPL(clsic_vox_asr_stream_set_params);

int clsic_vox_asr_stream_get_params(struct snd_compr_stream *stream,
				    struct snd_codec *params)
{
	return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(clsic_vox_asr_stream_get_params);

static enum clsic_message_cb_ret clsic_vox_asr_stream_data_cb(
						      struct clsic *clsic,
						      struct clsic_message *msg)
{
	struct clsic_service *handler =
		clsic_find_first_service(clsic, CLSIC_SRV_TYPE_VOX);
	struct clsic_vox *vox = handler->data;
	struct clsic_asr_stream *asr_stream = &vox->asr_stream;
	union clsic_vox_msg *msg_rsp;
	size_t read_idx, write_idx;
	u32 payload_sz;

	if (!asr_stream->stream) {
		clsic_dbg(clsic, "ASR stream is no longer active.\n");
		return CLSIC_MSG_RELEASED;
	}

	msg_rsp = (union clsic_vox_msg *) &msg->response;
	if (!clsic_get_bulk_bit(msg_rsp->rsp_get_asr_block.hdr.sbc) &&
	    msg_rsp->rsp_get_asr_block.hdr.err != 0) {
		clsic_err(clsic,
			  "Device responded with error code: %d\n",
			  msg_rsp->rsp_get_asr_block.hdr.err);
		asr_stream->error = true;
		snd_compr_fragment_elapsed(asr_stream->stream);
		return CLSIC_MSG_RELEASED;
	} else if (msg_rsp->blkrsp_get_asr_block.hdr.err != 0) {
		clsic_err(clsic,
			  "Device responded with error code: %d\n",
			  msg_rsp->blkrsp_get_asr_block.hdr.err);
		asr_stream->error = true;
		snd_compr_fragment_elapsed(asr_stream->stream);
		return CLSIC_MSG_RELEASED;
	}

	write_idx = asr_stream->buf.write_idx;
	read_idx = READ_ONCE(asr_stream->buf.read_idx);
	payload_sz = msg_rsp->blkrsp_get_asr_block.hdr.bulk_sz;

	trace_clsic_vox_asr_stream_data_rcv_start(payload_sz, read_idx,
						  write_idx);

	if ((read_idx - (write_idx + 1)) % asr_stream->buf.size >=
	    asr_stream->buf.frag_sz) {
		/* extract data from the response to an intermediate buffer */
		memcpy((u8 *) asr_stream->buf.data + write_idx,
		       msg->bulk_rxbuf,
		       payload_sz);

		smp_store_release(&asr_stream->buf.write_idx,
				  (write_idx + payload_sz) %
				  (asr_stream->buf.size));

		asr_stream->copied_total += payload_sz;

		/* notify the compressed framework of available data */
		snd_compr_fragment_elapsed(asr_stream->stream);

		trace_clsic_vox_asr_stream_data_rcv_end(payload_sz,
						    asr_stream->buf.read_idx,
						    asr_stream->buf.write_idx);
	} else {
		clsic_err(clsic, "ASR stream overflow.\n");
		asr_stream->error = true;
		asr_stream->copied_total += payload_sz;
		snd_compr_fragment_elapsed(asr_stream->stream);
		return CLSIC_MSG_RELEASED;
	}

	return CLSIC_MSG_RELEASED;
}

static void clsic_vox_asr_stream_trig_det_cb(struct clsic *clsic,
					     struct clsic_service *service)
{
	struct clsic_vox *vox = service->data;
	struct clsic_asr_stream *asr_stream = &vox->asr_stream;

	trace_clsic_vox_trigger_heard(service->service_instance);

	if (asr_stream->stream)
		complete(&asr_stream->trigger_heard);
}

static int clsic_vox_asr_stream_wait_for_trigger(void *data)
{
	struct clsic_asr_stream *asr_stream = data;
	struct clsic_vox *vox =
		container_of(asr_stream, struct clsic_vox, asr_stream);
	struct clsic *clsic = vox->clsic;
	union clsic_vox_msg msg_cmd;
	int ret = 0;

	if (wait_for_completion_interruptible(&asr_stream->trigger_heard)) {
		clsic_dbg(clsic, "Wait for ASR stream trigger aborted.\n");

		if (asr_stream->stream) {
			/* force compressed fw to notice error */
			asr_stream->error = true;
			asr_stream->copied_total += 1;
			snd_compr_fragment_elapsed(asr_stream->stream);
		}
		return 0;
	}

	if (!asr_stream->stream)
		return 0;

	trace_clsic_vox_asr_stream_data_start(asr_stream->copied_total);

	/* queue up the first read */
	clsic_init_message((union t_clsic_generic_message *)&msg_cmd,
			   vox->service->service_instance,
			   CLSIC_VOX_MSG_CRA_GET_ASR_BLOCK);

	ret = clsic_send_msg_async(clsic,
				   (union t_clsic_generic_message *) &msg_cmd,
				   CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				   CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN,
				   0,
				   clsic_vox_asr_stream_data_cb);
	if (ret) {
		clsic_err(clsic, "Error sending msg: %d\n", ret);
		/* force compressed fw to notice error */
		asr_stream->error = true;
		asr_stream->copied_total += 1;
		snd_compr_fragment_elapsed(asr_stream->stream);
		return 0;
	}

	trace_clsic_vox_asr_stream_queue_read(asr_stream->copied_total);

	return 0;
}

int clsic_vox_asr_stream_trigger(struct snd_compr_stream *stream, int cmd)
{
	struct clsic_asr_stream *asr_stream = stream->runtime->private_data;
	struct clsic_vox *vox =
		container_of(asr_stream, struct clsic_vox, asr_stream);
	struct clsic *clsic = vox->clsic;
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		/* instruct the service to enter listen mode */
		clsic_init_message((union t_clsic_generic_message *)&msg_cmd,
				   vox->service->service_instance,
				   CLSIC_VOX_MSG_CR_SET_MODE);
		msg_cmd.cmd_set_mode.mode = CLSIC_VOX_MODE_IDLE;

		ret = clsic_send_msg_sync(
				     clsic,
				     (union t_clsic_generic_message *) &msg_cmd,
				     (union t_clsic_generic_message *) &msg_rsp,
				     CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				     CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);
		if (ret) {
			clsic_err(clsic, "Error sending msg: %d\n", ret);
			return -EIO;
		}
		if (msg_rsp.rsp_set_mode.hdr.err) {
			clsic_err(clsic,
				  "Failed to enter idle mode: %d\n",
				  msg_rsp.rsp_set_mode.hdr.err);
			return -EIO;
		}

		clsic_init_message((union t_clsic_generic_message *)&msg_cmd,
				   vox->service->service_instance,
				   CLSIC_VOX_MSG_CR_SET_MODE);
		msg_cmd.cmd_set_mode.mode = CLSIC_VOX_MODE_LISTEN;

		ret = clsic_send_msg_sync(
				     clsic,
				     (union t_clsic_generic_message *) &msg_cmd,
				     (union t_clsic_generic_message *) &msg_rsp,
				     CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				     CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);
		if (ret) {
			clsic_err(clsic, "Error sending msg: %d\n", ret);
			return -EIO;
		}
		if (msg_rsp.rsp_set_mode.hdr.err) {
			clsic_err(clsic,
				  "Failed to enter listen mode: %d\n",
				  msg_rsp.rsp_set_mode.hdr.err);
			return -EIO;
		}

		clsic_init_message((union t_clsic_generic_message *)&msg_cmd,
				   vox->service->service_instance,
				   CLSIC_VOX_MSG_CR_LISTEN_START);
		/* TODO: add handling for external trigger */
		msg_cmd.cmd_listen_start.trgr_domain =
			CLSIC_VOX_TRIG_DOMAIN_INTRNL;
		msg_cmd.cmd_listen_start.asr_blk_sz = asr_stream->block_sz;

		ret = clsic_send_msg_sync(
				     clsic,
				     (union t_clsic_generic_message *) &msg_cmd,
				     (union t_clsic_generic_message *) &msg_rsp,
				     CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				     CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);
		if (ret) {
			clsic_err(clsic, "Error sending msg: %d\n", ret);
			return -EIO;
		}
		if (msg_rsp.rsp_listen_start.hdr.err) {
			clsic_err(clsic,
				  "Failed to start listening: %d\n",
				  msg_rsp.rsp_listen_start.hdr.err);
			return -EIO;
		}

		trace_clsic_vox_asr_stream_listen(
					  msg_cmd.cmd_listen_start.trgr_domain);

		reinit_completion(&asr_stream->trigger_heard);

		asr_stream->wait_for_trigger =
			kthread_create(clsic_vox_asr_stream_wait_for_trigger,
				       asr_stream,
				       "clsic-vox-asr-wait-for-trigger");

		vox->trig_det_cb = clsic_vox_asr_stream_trig_det_cb;

		wake_up_process(asr_stream->wait_for_trigger);

		break;
	case SNDRV_PCM_TRIGGER_STOP:
		clsic_init_message((union t_clsic_generic_message *)&msg_cmd,
				   vox->service->service_instance,
				   CLSIC_VOX_MSG_CR_SET_MODE);
		msg_cmd.cmd_set_mode.mode = CLSIC_VOX_MODE_IDLE;

		ret = clsic_send_msg_sync(
				     clsic,
				     (union t_clsic_generic_message *) &msg_cmd,
				     (union t_clsic_generic_message *) &msg_rsp,
				     CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				     CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);
		if (ret) {
			clsic_err(clsic, "Error sending msg: %d\n", ret);
			return -EIO;
		}
		if (msg_rsp.rsp_set_mode.hdr.err) {
			clsic_err(clsic,
				  "Failed to enter idle mode: %d\n",
				  msg_rsp.rsp_set_mode.hdr.err);
			return -EIO;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(clsic_vox_asr_stream_trigger);

int clsic_vox_asr_stream_pointer(struct snd_compr_stream *stream,
				 struct snd_compr_tstamp *tstamp)
{
	struct clsic_asr_stream *asr_stream = stream->runtime->private_data;

	tstamp->copied_total = asr_stream->copied_total;
	tstamp->sampling_rate = asr_stream->sample_rate;

	return 0;
}
EXPORT_SYMBOL_GPL(clsic_vox_asr_stream_pointer);

int clsic_vox_asr_stream_copy(struct snd_compr_stream *stream, char __user *buf,
			      size_t count)
{
	struct clsic_asr_stream *asr_stream = stream->runtime->private_data;
	struct clsic_vox *vox =
		container_of(asr_stream, struct clsic_vox, asr_stream);
	struct clsic *clsic = vox->clsic;
	union clsic_vox_msg msg_cmd;
	size_t write_idx, read_idx;
	int ret = 0;

	if (asr_stream->error) {
		clsic_err(clsic, "ASR stream xrun.\n");
		return -EIO;
	}

	write_idx = smp_load_acquire(&asr_stream->buf.write_idx);
	read_idx = asr_stream->buf.read_idx;

	trace_clsic_vox_asr_stream_copy_start(count, read_idx, write_idx);

	if ((write_idx - read_idx) % asr_stream->buf.size >= count) {
		if (copy_to_user(buf, asr_stream->buf.data + read_idx,
				 count)) {
			clsic_err(clsic, "Failed to copy data to user.\n");
			return -EFAULT;
		}

		smp_store_release(&asr_stream->buf.read_idx,
				  (read_idx + count) %
				  (asr_stream->buf.size));

		trace_clsic_vox_asr_stream_copy_end(count,
						    asr_stream->buf.read_idx,
						    asr_stream->buf.write_idx);
	} else {
		/*
		 * underrun - should never happen as the stream will be
		 * signalled only when there is data available or the stream
		 * has overran
		 */
		clsic_err(clsic, "ASR stream underrun.\n");
		return -EIO;
	}

	/* queue up next read */
	clsic_init_message((union t_clsic_generic_message *)&msg_cmd,
			   vox->service->service_instance,
			   CLSIC_VOX_MSG_CRA_GET_ASR_BLOCK);

	ret = clsic_send_msg_async(clsic,
				   (union t_clsic_generic_message *) &msg_cmd,
				   CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				   CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN,
				   0,
				   clsic_vox_asr_stream_data_cb);
	if (ret) {
		clsic_err(clsic, "Error sending msg: %d\n", ret);
		return -EIO;
	}

	trace_clsic_vox_asr_stream_queue_read(asr_stream->copied_total);

	return count;
}
EXPORT_SYMBOL_GPL(clsic_vox_asr_stream_copy);

int clsic_vox_asr_stream_get_caps(struct snd_compr_stream *stream,
				  struct snd_compr_caps *caps)
{
	caps->codecs[0] = clsic_asr_stream_caps.id;
	caps->direction = SND_COMPRESS_CAPTURE;
	caps->min_fragment_size = VOX_ASR_MIN_FRAGMENT_SZ;
	caps->max_fragment_size = VOX_ASR_MAX_FRAGMENT_SZ;
	caps->min_fragments = VOX_ASR_MIN_FRAGMENTS;
	caps->max_fragments = VOX_ASR_MAX_FRAGMENTS;

	return 0;
}
EXPORT_SYMBOL_GPL(clsic_vox_asr_stream_get_caps);

int clsic_vox_asr_stream_get_codec_caps(struct snd_compr_stream *stream,
					struct snd_compr_codec_caps *codec)
{
	return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(clsic_vox_asr_stream_get_codec_caps);
