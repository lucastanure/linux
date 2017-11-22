/*
 * clsic-syssrv.c -- CLSIC System Service
 *
 * Copyright 2017 CirrusLogic, Inc.
 *
 * Author: Simon Trimmer <simont@opensource.cirrus.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <sound/soc.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <linux/mfd/clsic/core.h>
#include <linux/mfd/clsic/message.h>
#include <linux/mfd/clsic/irq.h>
#include <linux/mfd/clsic/syssrv.h>
#include <linux/mfd/clsic/regmapsrv.h>
#include <linux/mfd/clsic/debugsrv.h>
#include <linux/mfd/clsic/voxsrv.h>
#include <linux/mfd/clsic/bootsrv.h>
#include "clsic-trace.h"

/*
 * This handler function will be called frequently by the incoming messages
 * context when a system service notification is received, many of the system
 * service notifications are concerned with the operation of the messaging
 * protocol and this handler calls back to the messaging layer to do the actual
 * named work.
 */
static int clsic_system_service_handler(struct clsic *clsic,
					struct clsic_service *handler,
					struct clsic_message *msg)
{
	enum clsic_sys_msg_id system_msgid;
	int ret = CLSIC_UNHANDLED;

	/* Make sure it is a notification message */
	if (clsic_get_cran_frommsg(msg) != CLSIC_CRAN_NTY) {
		clsic_dump_message(clsic, msg, "unhandled message");
		return ret;
	}
	system_msgid = clsic_get_messageid(msg);
	switch (system_msgid) {
	case CLSIC_SYS_MSG_N_RXDMA_STS:
		clsic_handle_message_rxdma_status(clsic, msg);
		ret = CLSIC_HANDLED;
		break;
	case CLSIC_SYS_MSG_N_INVAL_CMD:
		clsic_handle_message_invalid_cmd(clsic, msg);
		ret = CLSIC_HANDLED;
		break;
	case CLSIC_SYS_MSG_N_PANIC:
		clsic_dev_panic(clsic, msg);
		ret = CLSIC_HANDLED;
		break;
	default:
		clsic_err(clsic, "unrecognised message\n");
		clsic_dump_message(clsic, msg, "Unrecognised message");
	}
	return ret;
}

static void clsic_system_service_stop(struct clsic *clsic,
				      struct clsic_service *handler)
{
	clsic_dbg(clsic, "%p %d %d", handler, clsic->clsic_secproc_message_sent,
		  clsic->clsic_secproc_responded);

	/*
	 * All the other services will have shutdown before this function is
	 * called and the device should now be idle.
	 *
	 * The system service is responsible for making sure that the device
	 * can have it's power removed, if the ARM may be on try to shut it
	 * down.
	 */
	clsic_send_shutdown_cmd(clsic);

	if (handler->kcontrols != NULL) {
		clsic_deregister_codec_controls(clsic,
						handler->kcontrol_count,
						handler->kcontrols);
		handler->kcontrol_count = 0;
		handler->kcontrols = NULL;
	}

	if (handler->data != NULL) {
		kfree(handler->data);
		handler->data = NULL;
	}
}

/*
 * XXX Should be in a header (Shared between kernel and userspace)
 */
struct clsic_srv_info {
	uint8_t  inst;
	uint16_t type;
	uint32_t ver;
} __packed;

struct clsic_srvs_info {
	uint8_t count;
	struct clsic_srv_info info[CLSIC_SERVICE_COUNT];
} __packed;

/* Structure containing the System Service instance data */
struct clsic_syssrv_struct {
	struct clsic *clsic;

	struct clsic_service *srv;

	struct snd_kcontrol_new srvinfo_ctrl;
	char srvinfo_ctrl_name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	struct soc_bytes_ext srvinfo_ext;
};

static int sys_srv_info_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes_ext *bytes_ext =
		(struct soc_bytes_ext *) kcontrol->private_value;
	struct clsic_syssrv_struct *syssrv =
		container_of(bytes_ext, struct clsic_syssrv_struct,
			     srvinfo_ext);
	struct clsic *clsic = syssrv->clsic;
	struct clsic_srvs_info *srvs_info =
		(struct clsic_srvs_info *) ucontrol->value.bytes.data;
	int i;

	if (mutex_lock_interruptible(&clsic->service_lock))
		return -EINTR;

	/* Generate details of the services */
	for (i = 0; i <= CLSIC_SERVICE_MAX; i++) {
		if (clsic->service_handlers[i] != NULL) {
			srvs_info->count++;
			srvs_info->info[i].inst = i;
			srvs_info->info[i].type =
				clsic->service_handlers[i]->service_type;
			srvs_info->info[i].ver =
				clsic->service_handlers[i]->service_version;
		}
	}

	mutex_unlock(&clsic->service_lock);

	return 0;
}

int clsic_system_service_start(struct clsic *clsic,
			       struct clsic_service *handler)
{
	struct clsic_syssrv_struct *syssrv;

	/*
	 * In the reenumeration case the system service handler structure will
	 * be allocated, but the service info should be regenerated.
	 */
	if ((handler->stop == &clsic_system_service_stop) &&
	    (handler->data != NULL)) {
		syssrv = handler->data;
		return 0;
	}

	syssrv = kzalloc(sizeof(struct clsic_syssrv_struct), GFP_KERNEL);
	if (syssrv == NULL)
		return -ENOMEM;

	snprintf(syssrv->srvinfo_ctrl_name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN,
		 "Services Info");
	syssrv->srvinfo_ext.max = sizeof(struct clsic_srvs_info);

	syssrv->srvinfo_ctrl.name = syssrv->srvinfo_ctrl_name;
	syssrv->srvinfo_ctrl.info = snd_soc_bytes_info_ext;
	syssrv->srvinfo_ctrl.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	syssrv->srvinfo_ctrl.get = sys_srv_info_get;
	syssrv->srvinfo_ctrl.private_value =
		(unsigned long)(&(syssrv->srvinfo_ext));
	syssrv->srvinfo_ctrl.access = SNDRV_CTL_ELEM_ACCESS_READ |
		SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	syssrv->clsic = clsic;
	syssrv->srv = handler;

	handler->callback = &clsic_system_service_handler;
	handler->stop = &clsic_system_service_stop;
	handler->kcontrol_count = 1;
	handler->kcontrols = &(syssrv->srvinfo_ctrl);
	handler->data = syssrv;

	return clsic_register_codec_controls(clsic,
					     handler->kcontrol_count,
					     handler->kcontrols);
}

/*
 * Called by the core driver after receiving a boot done interrupt, enumerate
 * the services on a CLSIC device.
 */
int clsic_system_service_enumerate(struct clsic *clsic)
{
	union clsic_sys_msg msg_cmd;
	union clsic_sys_msg msg_rsp;
	uint8_t service_count = 0;
	int ret;
	uint8_t service_instance = 0;
	uint8_t services_found = 0;
	uint16_t service_type;
	uint32_t service_version;
	struct clsic_syssrv_struct *syssrv =
		clsic->service_handlers[CLSIC_SRV_INST_SYS]->data;

	if (syssrv == NULL) {
		clsic_err(clsic, "No system service data\n");
		return -EINVAL;
	}

	clsic_dbg(clsic, "=[ BEGINS ]===================\n");

	/*
	 * The "first touch" message that wakes the device may generate a
	 * bootloader notification so this message may fail message with
	 * CLSIC_MSG_INTERRUPTED.
	 *
	 * If the device is dead then this command may also timeout - in that
	 * case initiate recovery measures.
	 */
	memset(&msg_cmd, 0, CLSIC_FIXED_MSG_SZ);
	clsic_set_cran(&msg_cmd.cmd_sys_info.hdr.sbc, CLSIC_CRAN_CMD);
	clsic_set_bulk(&msg_cmd.cmd_sys_info.hdr.sbc, 0);
	clsic_set_srv_inst(&msg_cmd.cmd_sys_info.hdr.sbc,
			   CLSIC_SRV_INST_SYS);
	msg_cmd.cmd_sys_info.hdr.msgid = CLSIC_SYS_MSG_CR_SYS_INFO;
	ret = clsic_send_msg_sync(clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				  CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);

	if (ret != 0) {
		clsic_err(clsic, "sysinfo ret %d\n", ret);
		if (ret == -ETIMEDOUT) {
			/*
			 * First touch message timed out - restart the device
			 * in firmware update mode to attempt recovery
			 */
			clsic_fwupdate_reset(clsic);
		}
		return -EIO;
	}

	clsic_dbg(clsic, "Sysinfo ret 0x%x 0x%x 0x%x\n",
		  msg_rsp.rsp_sys_info.hdr.sbc,
		  msg_rsp.rsp_sys_info.hdr.msgid,
		  msg_rsp.rsp_sys_info.hdr.err);

	service_count = msg_rsp.rsp_sys_info.srv_count;

	clsic_dbg(clsic, "Sysinfo service count %d\n", service_count);

	/*
	 * The message size is stored in a byte, but there is only 5 bits of
	 * addressable services
	 */
	if (service_count > CLSIC_SERVICE_COUNT) {
		clsic_err(clsic, "Sysinfo response larger than max %d\n",
			  service_count);
		service_count = CLSIC_SERVICE_COUNT;
	}

	/* Enumerate services */
	for (service_instance = 0;
	     (services_found < service_count) &&
	     (service_instance < CLSIC_SERVICE_COUNT);
	     service_instance++) {
		clsic_dbg(clsic, "Examine instance %d (found count %d)",
			  service_instance, services_found);
		/* Read the service type */
		memset(&msg_cmd, 0, CLSIC_FIXED_MSG_SZ);
		clsic_set_cran(&msg_cmd.cmd_srv_info.hdr.sbc, CLSIC_CRAN_CMD);
		clsic_set_bulk(&msg_cmd.cmd_srv_info.hdr.sbc, 0);
		clsic_set_srv_inst(&msg_cmd.cmd_srv_info.hdr.sbc,
				   CLSIC_SRV_INST_SYS);
		msg_cmd.cmd_srv_info.hdr.msgid = CLSIC_SYS_MSG_CR_SRV_INFO;
		msg_cmd.cmd_srv_info.srv_inst = service_instance;
		ret = clsic_send_msg_sync(clsic,
				     (union t_clsic_generic_message *) &msg_cmd,
				     (union t_clsic_generic_message *) &msg_rsp,
				     CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				     CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);

		if (ret != 0) {
			/* XXX need to determine if this send message error was
			 * fatal
			 *
			 * If the command was cancelled due to a bootloader
			 * event then it should be considered fatal
			 */
			clsic_err(clsic,
				  "getserviceinfo %d: send_message %d\n",
				  service_instance, ret);
			continue;
		}

		/*
		 * Move on to examine the next service instance when
		 * getserviceinfo encounters an invalid service instance error
		 * code (this just means that the services are sparse)
		 */
		if (msg_rsp.rsp_srv_info.hdr.err == CLSIC_ERR_INVAL_SI) {
			clsic_dbg(clsic, "getserviceinfo %d: no service\n",
				  service_instance);
			continue;
		}
		services_found++;
		service_type = msg_rsp.rsp_srv_info.srv_type;
		service_version = msg_rsp.rsp_srv_info.srv_ver;

		clsic_dbg(clsic,
			  " Found service id %d type 0x%x version 0x%x (%d.%d.%d)",
			  service_instance, service_type, service_version,
			  (service_version & CLSIC_SVCVER_MAJ_MASK) >>
			  CLSIC_SVCVER_MAJ_SHIFT,
			  (service_version & CLSIC_SVCVER_MIN_MASK) >>
			  CLSIC_SVCVER_MIN_SHIFT,
			  (service_version & CLSIC_SVCVER_BLD_MASK) >>
			  CLSIC_SVCVER_BLD_SHIFT);

		switch (service_type) {
		case CLSIC_SRV_TYPE_SYS:
			clsic_info(clsic,
				   "System service fw version %d.%d.%d",
				   (service_version & CLSIC_SVCVER_MAJ_MASK) >>
				   CLSIC_SVCVER_MAJ_SHIFT,
				   (service_version & CLSIC_SVCVER_MIN_MASK) >>
				   CLSIC_SVCVER_MIN_SHIFT,
				   (service_version & CLSIC_SVCVER_BLD_MASK) >>
				   CLSIC_SVCVER_BLD_SHIFT);
			/* fallthrough */
		case CLSIC_SERVICE_TYPE_BOOTLOADER:
			/* preregistered handlers */
			clsic_dbg(clsic,
				  " Service %d is a standard service (type 0x%x)",
				  service_instance, service_type);

			if (clsic->service_handlers[service_instance] != NULL) {
				clsic->service_handlers[service_instance]
					->service_version = service_version;
			}
			break;
		case CLSIC_SERVICE_TYPE_DEBUG_EMU:
			clsic_register_service_handler(clsic,
						    service_instance,
						    service_type,
						    service_version,
						    clsic_debug_service_start);
			break;
		case CLSIC_SRV_TYPE_RAS:
			clsic_register_service_handler(clsic,
						    service_instance,
						    service_type,
						    service_version,
						    clsic_regmap_service_start);
			break;
		case CLSIC_SRV_TYPE_VOX:
			clsic_register_service_handler(clsic,
						    service_instance,
						    service_type,
						    service_version,
						    clsic_vox_service_start);
			break;
		default:
			/* unrecognised */
			clsic_err(clsic,
				  " Unrecognised service (%d: type 0x%x ver 0x%x)",
				  service_instance, service_type,
				  service_version);
			clsic_register_service_handler(clsic,
						       service_instance,
						       service_type,
						       service_version, NULL);
		}
	}

	clsic_dbg(clsic, "Enumerate found %d services (error: %d)",
		  services_found, ret);
	clsic_dbg(clsic, "=[ ENDS ]=====================");

	clsic_set_state(clsic, CLSIC_STATE_ACTIVE);

	return 0;
}

/*
 * This helper function is called when the device is being shutdown properly,
 * such as when the handset is powering off or rebooted.
 *
 * It is also used as part of the firmware update process where the service
 * enumeration decides that it has a newer firmware than is presently loaded
 * onto the device.
 */
int clsic_send_shutdown_cmd(struct clsic *clsic)
{
	union clsic_sys_msg msg_cmd;
	union clsic_sys_msg msg_rsp;
	int ret = 0;

	/*
	 * The only state when performing a shutdown is a sensible activity is
	 * when it is running (for power management purposes) or stopping (in
	 * preparation for driver unload).
	 */
	if ((clsic->state != CLSIC_STATE_ACTIVE) &&
	    (clsic->state != CLSIC_STATE_ENUMERATING) &&
	    (clsic->state != CLSIC_STATE_STOPPING)) {
		/*
		 * CLSIC_STATE_INACTIVE:
		 * When the chip is off then it would be crazy to wake it up to
		 * just shut it down.
		 *
		 * CLSIC_STATE_BOOTLOADER*:
		 * The bootloader does not support the shutdown message
		 *
		 * CLSIC_STATE_PANIC or CLSIC_STATE_LOST:
		 * If the board has failed then the shutdown message will
		 * timeout as there is nothing to receive and handle it
		 *
		 * CLSIC_STATE_DEBUGCONTROL_GRANTED:
		 * If debugcontrol is asserted then this shutdown command can
		 * not be sent over the bus (it's locked and we don't know what
		 * state the messaging protocol has been left in).
		 *
		 * CLSIC_STATE_DEBUGCONTROL_REQUESTED:
		 * Likewise, if debug control is in the process of being
		 * asserted then the message will not be sent either.
		 *
		 */
		clsic_info(clsic,
			   "state 0x%x (%s), skipping shutdown message\n",
			   clsic->state, clsic_state_to_string(clsic->state));
		return -EBUSY;
	}

	/*
	 * All the other services will have shutdown before this function is
	 * called and the device should now be idle.
	 *
	 * Or, the device is being powered off or rebooted and this is a catch
	 * saving state.
	 *
	 * The system service is responsible for making sure that the device
	 * can have it's power removed, if the ARM may be on try to shut it
	 * down.
	 */
	if (clsic->clsic_secproc_message_sent
	    || clsic->clsic_secproc_responded) {
		memset(&msg_cmd, 0, CLSIC_FIXED_MSG_SZ);
		clsic_set_cran(&msg_cmd.cmd_sp_shdn.hdr.sbc, CLSIC_CRAN_CMD);
		clsic_set_bulk(&msg_cmd.cmd_sp_shdn.hdr.sbc, 0);
		clsic_set_srv_inst(&msg_cmd.cmd_sp_shdn.hdr.sbc,
				   CLSIC_SRV_INST_SYS);
		msg_cmd.cmd_sp_shdn.hdr.msgid = CLSIC_SYS_MSG_CR_SP_SHDN;
		ret = clsic_send_msg_sync(clsic,
				     (union t_clsic_generic_message *) &msg_cmd,
				     (union t_clsic_generic_message *) &msg_rsp,
				     CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				     CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);

		clsic_info(clsic,
			   "Shutdown message returned 0x%x 0x%x: bitmap 0x%x\n",
			   ret,
			   msg_rsp.rsp_sp_shdn.hdr.err,
			   msg_rsp.rsp_sp_shdn.srvs_hold_wakelock);
		clsic_set_state(clsic, CLSIC_STATE_STOPPED);
	}
	return ret;
}
