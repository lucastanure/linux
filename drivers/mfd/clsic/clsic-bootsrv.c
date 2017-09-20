/*
 * clsic-bootsrv.c -- CLSIC Bootloader Service
 *
 * Copyright 2017 CirrusLogic, Inc.
 *
 * Author: Simon Trimmer <simont@opensource.cirrus.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/firmware.h>

#include <linux/mfd/clsic/core.h>
#include <linux/mfd/clsic/message.h>
#include <linux/mfd/clsic/irq.h>
#include <linux/mfd/clsic/bootsrv.h>
#include <linux/mfd/clsic/syssrv.h>

/*
 * The way this bootloader service works is that it has two entry points
 *
 * The first is the handler that the messaging layer calls when it receives
 * notifications from the bootloader - we would expect a notification if the
 * device is reset with the fw_update bits set or if the device encounters
 * corrupted flash. The result of this call will be the overall driver state
 * transitioning to one of the bootloader states and the driver maintenance
 * thread being scheduled, this will then call the next entry point, the state
 * handler.
 *
 * The second entry point is the state handler; this is called by the driver
 * maintenance thread when it encounters the driver state within the bootloader
 * range. This handler provides a response to a notification, for instance if
 * the device requests a customer key then the handler will send it.
 *
 * It is expected that the bootloader will send a series of notifications, in
 * the form "give me X ... (driver satisfies request by sending X) ... give me
 * Y ... (driver satisfies request by sending Y)" and when the final bootloader
 * state is encountered then the driver is set back to the INACTIVE state and
 * the maintenance thread is scheduled again - this will cause the device to be
 * reset and enumerated, resulting in the combined system entering the ACTIVE
 * state.
 *
 * The bootloader may also send notifications when it cannot boot the device
 * from flash. The driver responds to these requests by initiating a firmware
 * update reset - the bootloader will then cycle through the normal firmware
 * download message exchange that should rewrite the flash on the device and
 * restore it to a working state.
 */

/*
 * These are the standard firmware filenames, usually stored in /lib/firmware/
 */
static const char CLSIC_FWFILE_MAB[] = "clsic-mab.bin";
static const char CLSIC_FWFILE_CPK[] = "clsic-cpk.bin";
static const char CLSIC_FWFILE_FWU[] = "clsic-fwu.bin";

/*
 * Constants describing datafile structures - the Linux host is only interested
 * in a few fields in the header of the firmware file, the remaining portions
 * of the structure are obscured.
 */
#define SIZEOF_PADDING_IN_BYTES		76
#define SIZEOF_PADDING2_IN_BYTES	12

struct clsic_fwheader {
	uint8_t padding[SIZEOF_PADDING_IN_BYTES];
	uint32_t magic;
	uint32_t type;
	uint8_t padding2[SIZEOF_PADDING2_IN_BYTES];
	uint32_t version; /* iReleaseVersion */
} PACKED;

#define CLSIC_FWMAGIC               0x42554c43UL
#define CLSIC_FWMAGIC_WIPE          0x45504957UL

/* The firmware type magic numbers for different files */
#define CLSIC_FWTYPE_KGN            0x204e474bUL
#define CLSIC_FWTYPE_FWU            0x20555746UL
#define CLSIC_FWTYPE_CPK            0x204b5043UL
#define CLSIC_FWTYPE_MAB            0x2042414dUL
#define CLSIC_FWTYPE_CAB            0x20424143UL
#define CLSIC_FWTYPE_BPB            0x20425042UL
#define CLSIC_FWTYPE_FAK            0x204b4146UL

/* Strings used for describing firmware types */
static const char CLSIC_KGN[] = "KGN";
static const char CLSIC_FWU[] = "FWU";
static const char CLSIC_CPK[] = "CPK";
static const char CLSIC_MAB[] = "MAB";
static const char CLSIC_CAB[] = "CAB";
static const char CLSIC_BPB[] = "BPB";
static const char CLSIC_FAK[] = "FAK";

/*
 * Utility function to convert between an integer file type and a three letter
 * string representation for use in messages.
 */
static const char *clsic_fwtype2string(uint32_t type)
{
	const char *ret_string;

	/* recognised firmware types */
	switch (type) {
	case CLSIC_FWTYPE_KGN:
		ret_string = CLSIC_KGN;
		break;
	case CLSIC_FWTYPE_FWU:
		ret_string = CLSIC_FWU;
		break;
	case CLSIC_FWTYPE_CPK:
		ret_string = CLSIC_CPK;
		break;
	case CLSIC_FWTYPE_MAB:
		ret_string = CLSIC_MAB;
		break;
	case CLSIC_FWTYPE_CAB:
		ret_string = CLSIC_CAB;
		break;
	case CLSIC_FWTYPE_BPB:
		ret_string = CLSIC_BPB;
		break;
	case CLSIC_FWTYPE_FAK:
		ret_string = CLSIC_FAK;
		break;
	default:
		/* unrecognised */
		ret_string = NULL;
	}
	return ret_string;
}

static inline const char *clsic_bootsrv_err_to_string(uint8_t err)
{
	switch (err) {
	case CLSIC_ERR_NONE:
		return "Success";
	case CLSIC_ERR_BL_AUTH_FAILED:
		return "Authentication failed";
	case CLSIC_ERR_BL_INVAL_VERSION:
		return "Invalid version";
	case CLSIC_ERR_BL_FLASH_WRITE_FAILED:
		return "Flash write failed";
	case CLSIC_ERR_BL_ARB_CHECK_FAILED:
		return "ARB check failed";
	case CLSIC_ERR_BL_CLUB_TOO_LARGE:
		return "CLUB tool large";
	case CLSIC_ERR_BL_IMG_NAME_CLASH:
		return "Image name clash";
	case CLSIC_ERR_BL_CAB_NOT_1ST_IN_MAB:
		return "CAB not 1st in MAB";
	case CLSIC_ERR_BL_TOO_MANY_IMGS:
		return "Too many images";
	case CLSIC_ERR_BL_NO_MIN_SET_IN_MAB:
		return "Too few images";
	case CLSIC_ERR_BL_FLASH_ERASE_FAILED:
		return "Flash erase failed";
	case CLSIC_ERR_BL_FLASH_READ_FAILED:
		return "Flash read failed";
	case CLSIC_ERR_BL_NBS2_NOT_1ST_IN_CAB:
		return "NBS2 not 1st in CAB";
	case CLSIC_ERR_BL_OSAPP_NOT_2ND_IN_CAB:
		return "OSAPP not 2nd in CAB";
	default:
		return "Unknown";
	}
}

/*
 * Utility function, check that the magic number and the file type in the given
 * header are valid. This function doesn't reopen the firmware file - the
 * filename is just used for the logged message.
 */
static const int clsic_bootsrv_fwheader_check(struct clsic *clsic,
					      const char *filename,
					      struct clsic_fwheader *hdr)
{
	int ret;

	/* Perform a basic sanity check on magic and type */
	if (hdr->magic != CLSIC_FWMAGIC) {
		clsic_err(clsic, "Firmware file %s wrong magic 0x%x\n",
			  filename, hdr->magic);
		ret = -EINVAL;
	} else if (clsic_fwtype2string(hdr->type) == NULL) {
		clsic_err(clsic, "Firmware file %s unknown type 0x%x\n",
			  filename, hdr->type);
		ret = -EINVAL;
	} else {
		ret = 0;
	}

	return ret;
}

/*
 * For a given filename, safely read from the file to populate header
 * structure.  The header can then be used to check it is the expected kind of
 * firmware file and the version of the file.
 */
static const int clsic_bootsrv_fwfile_info(struct clsic *clsic,
					   const char *filename,
					   struct clsic_fwheader *hdr)
{
	const struct firmware *firmware;
	int ret;

	ret = request_firmware(&firmware, filename, clsic->dev);
	if (ret != 0) {
		clsic_info(clsic, "request_firmware %s failed %d\n",
			   filename, ret);
		return ret;
	}

	/*
	 * This driver has a minimal file header structure that contains only
	 * what it needs, if the file is smaller than that it can't be a real
	 * firmware file.
	 */
	if (firmware->size < sizeof(struct clsic_fwheader)) {
		clsic_info(clsic, "Firmware file %s too small %d\n",
			   filename, firmware->size);
		ret = -EINVAL;
		goto release_exit;
	}

	memcpy(hdr, firmware->data, sizeof(struct clsic_fwheader));

	/* Finally sanity check the file's magic numbers */
	ret = clsic_bootsrv_fwheader_check(clsic, filename, hdr);

release_exit:
	release_firmware(firmware);
	return ret;
}

/*
 * Utility function for the system service. For a given firmware filename,
 * safely interrogate the header and return the version within.
 *
 * To prevent an overlap of ranges in this function if an error is encountered
 * the version returned is 0. This should mean that if the device has valid
 * firmware then the firmware update process will not be started if an error is
 * encountered.
 *
 * Traditionally the top bit is used to indicate returned value is an error
 * codes but that bit is used in the major version.
 */
static const uint32_t clsic_bootsrv_file_version(struct clsic *clsic,
						 const char *filename)
{
	struct clsic_fwheader hdr;
	int ret;

	ret = clsic_bootsrv_fwfile_info(clsic, filename, &hdr);
	if (ret != 0)
		return 0;

	clsic_dbg(clsic, "%s: %s 0x%x (%d.%d.%d)\n",
		  filename, clsic_fwtype2string(hdr.type), hdr.version,
		  (hdr.version & CLSIC_SVCVER_MAJ_MASK) >>
		  CLSIC_SVCVER_MAJ_SHIFT,
		  (hdr.version & CLSIC_SVCVER_MIN_MASK) >>
		  CLSIC_SVCVER_MIN_SHIFT,
		  (hdr.version & CLSIC_SVCVER_BLD_MASK) >>
		  CLSIC_SVCVER_BLD_SHIFT);

	return hdr.version;
}

/*
 * Transmits the contents of the given filename as bulk data payload to the
 * bootloader with the given message id.
 *
 * Performs basic sanity check on the file header to make sure it is valid and
 * matches the expected type.
 */
static int clsic_bootsrv_sendfile(struct clsic *clsic,
				  const char *filename,
				  uint32_t type, uint32_t msgid,
				  union clsic_bl_msg *msg_rsp)
{
	const struct firmware *firmware;
	int ret;
	union t_clsic_generic_message msg_cmd;
	struct clsic_fwheader *hdr;
	uint8_t err;

	ret = request_firmware(&firmware, filename, clsic->dev);
	if (ret != 0) {
		clsic_info(clsic, "request_firmware %d\n", ret);
		return ret;
	}

	clsic_info(clsic, "%s len: %d (%%4 = %d)\n", filename,
		   firmware->size, firmware->size % sizeof(uint32_t));

	if (firmware->size < sizeof(struct clsic_fwheader)) {
		clsic_info(clsic, "Firmware file too small\n");
		release_firmware(firmware);
		return -EINVAL;
	}

	hdr = (struct clsic_fwheader *)firmware->data;

	/* Sanity check the file's magic numbers */
	if (clsic_bootsrv_fwheader_check(clsic, filename, hdr) != 0)
		goto release_exit;

	if (hdr->type != type) {
		clsic_err(clsic,
			  "Wrong file type: expected 0x%x, file 0x%x\n",
			  type, hdr->type);
		goto release_exit;
	}

	/* Finally send the file as the bulk data payload of the given msgid */
	memset(&msg_cmd, 0, CLSIC_FIXED_MSG_SZ);

	clsic_set_cran(&msg_cmd.bulk_cmd.hdr.sbc, CLSIC_CRAN_CMD);
	clsic_set_srv_inst(&msg_cmd.bulk_cmd.hdr.sbc,
			   CLSIC_SRV_INST_BLD);
	msg_cmd.bulk_cmd.hdr.msgid = msgid;
	clsic_set_bulk(&msg_cmd.bulk_cmd.hdr.sbc, 1);
	msg_cmd.bulk_cmd.hdr.bulk_sz = firmware->size;
	ret = clsic_send_msg_sync(clsic, &msg_cmd,
				  (union t_clsic_generic_message *)msg_rsp,
				  firmware->data, firmware->size,
				  CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);

	if (ret != 0) {
		clsic_info(clsic, "Failed to send: %d\n", ret);
		ret = -EIO;
	}

	if (msg_rsp->rsp_set_mab.hdr.err != 0) {
		err = msg_rsp->rsp_set_mab.hdr.err;
		clsic_info(clsic, "Response error_code 0x%x : '%s'\n",
			   err, clsic_bootsrv_err_to_string(err));
		ret = -EIO;
	}

release_exit:
	release_firmware(firmware);
	return ret;
}

/*
 * Called by the messaging layer in response to receiving a NOTIFICATION
 * message
 */
static int clsic_bootsrv_msghandler(struct clsic *clsic,
				    struct clsic_service *handler,
				    struct clsic_message *msg)
{
	uint8_t msgid = clsic_get_messageid(msg);

	/*
	 * Most of the notifications result in the driver setting state to
	 * indicate that it should send a file to the bootloader service in the
	 * maintenance thread context.
	 *
	 * This function cannot send the response message directly because this
	 * context is used to progress all notifications; as sending files uses
	 * bulk messaging and that involves a system service notification if we
	 * blocked this context the messaging layer would deadlock.
	 */
	switch (msgid) {
	case CLSIC_BL_MSG_N_REQ_FWU:
		clsic_dbg(clsic, "Request FWU bundle\n");
		clsic_set_state(clsic, CLSIC_STATE_BOOTLOADER_FWU);
		break;
	case CLSIC_BL_MSG_N_REQ_CPK:
		clsic_dbg(clsic, "Request CPK bundle\n");
		clsic_set_state(clsic, CLSIC_STATE_BOOTLOADER_CPK);
		break;
	case CLSIC_BL_MSG_N_REQ_MAB:
		clsic_dbg(clsic, "Request MAB bundle\n");
		clsic_set_state(clsic, CLSIC_STATE_BOOTLOADER_MAB);
		break;
	case CLSIC_BL_MSG_N_NO_BOOTABLE_COMP:
	case CLSIC_BL_MSG_N_FAILED_FLASH_AUTH:
	case CLSIC_BL_MSG_N_FLASH_CORRUPTED:
		clsic_dbg(clsic, "CSLIC boot fail %d\n", msgid);
		clsic_set_state(clsic, CLSIC_STATE_BOOTLOADER_BEGIN);
		mutex_lock(&clsic->message_lock);
		clsic_purge_message_queues(clsic);
		mutex_unlock(&clsic->message_lock);
		break;
	default:
		clsic_dump_message(clsic, msg, "clsic_bootsrv_msghandler");
		return CLSIC_UNHANDLED;
	}

	schedule_work(&clsic->maintenance_handler);
	return CLSIC_HANDLED;
}

/*
 * Called by the maintenance thread to progress bootloader states
 *
 * The majority of the states in the handler are for sending files to the
 * bootloader after receiving a notification.
 */
void clsic_bootsrv_state_handler(struct clsic *clsic)
{
	int ret = 0;
	union clsic_bl_msg msg_rsp;

	switch (clsic->state) {
	case CLSIC_STATE_BOOTLOADER_BEGIN:
		/*
		 * This state handles the case where the bootloader notifies
		 * the host about a flash boot failure and the driver responds
		 * by just resetting the device in firmware update mode, we'd
		 * expect the bootloader to respond with a notification
		 * requesting the FWU package which will progress the system
		 * through the states.
		 */
		clsic_info(clsic, "Bootloader starting firmware update\n");
		clsic_fwupdate_reset(clsic);
		break;
	case CLSIC_STATE_BOOTLOADER_FWU:
		clsic_set_state(clsic, CLSIC_STATE_BOOTLOADER_WFR);
		ret = clsic_bootsrv_sendfile(clsic,
					     CLSIC_FWFILE_FWU,
					     CLSIC_FWTYPE_FWU,
					     CLSIC_BL_MSG_CR_SET_FWU,
					     &msg_rsp);
		break;
	case CLSIC_STATE_BOOTLOADER_CPK:
		clsic_set_state(clsic, CLSIC_STATE_BOOTLOADER_WFR);
		ret = clsic_bootsrv_sendfile(clsic,
					     CLSIC_FWFILE_CPK,
					     CLSIC_FWTYPE_CPK,
					     CLSIC_BL_MSG_CR_SET_CPK,
					     &msg_rsp);
		break;
	case CLSIC_STATE_BOOTLOADER_MAB:
		clsic_set_state(clsic, CLSIC_STATE_BOOTLOADER_WFR);
		ret = clsic_bootsrv_sendfile(clsic,
					     CLSIC_FWFILE_MAB,
					     CLSIC_FWTYPE_MAB,
					     CLSIC_BL_MSG_CR_SET_MAB,
					     &msg_rsp);
		if (ret == 0) {
			/*
			 * Successfully downloading the MAB is normally the end
			 * of the bootloader exchange.
			 */
			if (msg_rsp.rsp_set_mab.flags &
			    CLSIC_BL_RESET_NOT_REQUIRED)
				clsic_set_state(clsic, CLSIC_STATE_ENUMERATING);
			else
				clsic_set_state(clsic, CLSIC_STATE_INACTIVE);
			schedule_work(&clsic->maintenance_handler);
		}
		break;
	case CLSIC_STATE_BOOTLOADER_WFR:
		/*
		 * The bootloader sets itself to the waiting for response (WFR)
		 * state before issuing a command so that if the if the
		 * maintenance thread reruns it'll dump out progress
		 * information rather than attempting to resend a command
		 * message with bulk data.
		 */
		clsic_err(clsic, "Bootloader waiting for response\n");
		break;
	default:
		/*
		 * Entering this case indicates that there is a state
		 * notification race and that between the messaging handler
		 * identifying the state as being a bootloader state and
		 * processing it something else has changed the state. This
		 * could be because of a device panic.
		 *
		 * As there is no clear recovery path attempt to dump the
		 * bootloader progress values and set the overall driver state
		 * to LOST so the driver ceases driver communication.
		 */
		clsic_err(clsic, "Unrecognised: %d\n", clsic->state);
		ret = -EINVAL;
	}

	if (ret != 0)
		clsic_set_state(clsic, CLSIC_STATE_LOST);
}

#ifdef CONFIG_DEBUG_FS
/*
 * NOTE: The debugfs mechanism to trigger the firmware update is a test
 * interface, it is not intended to be be used in a product as OS software may
 * have built state on top of the driver interfaces.
 */
static int clsic_fwupdate_write(void *data, u64 val)
{
	struct clsic *clsic = data;
	int ret = -EINVAL;

	/*
	 * Only allow firmware update from the initial cold and from the
	 * regular enumerated driver states.
	 *
	 * Attempt to park the device by sending a shutdown message before
	 * initiating device reset.
	 */
	if ((clsic->state == CLSIC_STATE_ACTIVE) ||
	    (clsic->state == CLSIC_STATE_INACTIVE)) {
		clsic_send_shutdown_cmd(clsic);
		ret = clsic_fwupdate_reset(clsic);
	}

	return ret;
}

DEFINE_SIMPLE_ATTRIBUTE(clsic_fwupdate_fops, NULL,
			clsic_fwupdate_write, "%llu\n");
#endif

static ssize_t clsic_show_file_fw_version(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct clsic *clsic = dev_get_drvdata(dev);
	uint32_t file_version = clsic_bootsrv_file_version(clsic,
							   CLSIC_FWFILE_MAB);

	return snprintf(buf, PAGE_SIZE, "%d.%d.%d\n",
		       (file_version & CLSIC_SVCVER_MAJ_MASK) >>
		       CLSIC_SVCVER_MAJ_SHIFT,
		       (file_version & CLSIC_SVCVER_MIN_MASK) >>
		       CLSIC_SVCVER_MIN_SHIFT,
		       (file_version & CLSIC_SVCVER_BLD_MASK) >>
		       CLSIC_SVCVER_BLD_SHIFT);
}
static DEVICE_ATTR(file_fw_version, S_IRUGO, clsic_show_file_fw_version, NULL);

static ssize_t clsic_store_device_fw_version(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct clsic *clsic = dev_get_drvdata(dev);

	if (!strncmp(buf, "update", strlen("update"))) {
		clsic_send_shutdown_cmd(clsic);
		clsic_fwupdate_reset(clsic);
	}

	return count;
}

static ssize_t clsic_show_device_fw_version(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct clsic *clsic = dev_get_drvdata(dev);
	uint32_t device_version =
		clsic->service_handlers[CLSIC_SRV_INST_SYS]->service_version;

	return snprintf(buf, PAGE_SIZE, "%d.%d.%d\n",
		       (device_version & CLSIC_SVCVER_MAJ_MASK) >>
		       CLSIC_SVCVER_MAJ_SHIFT,
		       (device_version & CLSIC_SVCVER_MIN_MASK) >>
		       CLSIC_SVCVER_MIN_SHIFT,
		       (device_version & CLSIC_SVCVER_BLD_MASK) >>
		       CLSIC_SVCVER_BLD_SHIFT);
}
static DEVICE_ATTR(device_fw_version, S_IRUGO | S_IWUSR,
		   clsic_show_device_fw_version, clsic_store_device_fw_version);

static void clsic_bootsrv_service_stop(struct clsic *clsic,
				      struct clsic_service *handler)
{
	device_remove_file(clsic->dev, &dev_attr_device_fw_version);
	device_remove_file(clsic->dev, &dev_attr_file_fw_version);
}

int clsic_bootsrv_service_start(struct clsic *clsic,
				struct clsic_service *handler)
{
	int ret = 0;

	handler->callback = &clsic_bootsrv_msghandler;
	handler->stop = &clsic_bootsrv_service_stop;

	device_create_file(clsic->dev, &dev_attr_device_fw_version);
	device_create_file(clsic->dev, &dev_attr_file_fw_version);

#ifdef CONFIG_DEBUG_FS
	debugfs_create_file("triggerfwupdate",
			    S_IWUSR | S_IWGRP,
			    clsic->debugfs_root,
			    clsic, &clsic_fwupdate_fops);
#endif

	return ret;
}
