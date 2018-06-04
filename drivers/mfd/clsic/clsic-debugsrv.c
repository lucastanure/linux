/*
 * clsic-debugsrv.c -- CLSIC Debug Service
 *
 * Copyright 2016-2018 Cirrus Logic
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/mfd/clsic/core.h>
#include <linux/mfd/clsic/message.h>
#include <linux/mfd/clsic/irq.h>
#include <linux/mfd/clsic/debugsrv.h>

/*
 * This is not a real CLSIC service and is being used in development as a
 * method of testing functionality within the messaging layer.
 *
 * This service is designed to receive a few different notification messages,
 * one subset indicates the state of the simulated service as being ACTIVE or
 * IDLE and the other subset is used to exercise standard service
 * notifications.
 *
 * The ACTIVE and IDLE states are used to test that a service handler can send
 * messages to the device as it is being shutdown; an emulated codec test
 * injects a message to the simulated service that results in a notification to
 * this service handler indicating it has entered the ACTIVE state.  Then, when
 * the stop() callback is made this ACTIVE state triggers a message to be
 * issued to the simulated service. In addition to the regular response message
 * the simulated service responds with an IDLE notification.
 *
 * The other standard notifications exercise different message payload
 * combinations.
 */

#define CLSIC_DEBUGSRV_STATE_IDLE		0
#define CLSIC_DEBUGSRV_STATE_ACTIVE		1

/* Emulated codec test scenario 023 */
#define CLSIC_DEBUGSRV_CMD_DEACTIVATE		0
#define CLSIC_DEBUGSRV_CMD_ACTIVATE		1
#define CLSIC_DEBUGSRV_NOTIF_IDLE		0
#define CLSIC_DEBUGSRV_NOTIF_ACTIVE		1

/* message ids match emulated codec test scenario numbers */
#define CLSIC_DEBUGSRV_NOTIF_HANDLED		44
#define CLSIC_DEBUGSRV_NOTIF_UNHANDLED		45
#define CLSIC_DEBUGSRV_NOTIF_HANDLED_SHORT	46
#define CLSIC_DEBUGSRV_NOTIF_HANDLED_LONG	47
#define CLSIC_DEBUGSRV_NOTIF_HANDLED_WRONGDATA	48

struct clsic_debugsrv_struct {
	uint8_t state;
};

static void clsic_debug_service_stop(struct clsic *clsic,
				     struct clsic_service *handler)
{
	struct clsic_debugsrv_struct *debugsrv_struct = handler->data;
	union t_clsic_generic_message msg_cmd;
	union t_clsic_generic_message msg_rsp;
	int ret = 0;

	if (debugsrv_struct == NULL)
		return;

	/* debug service had started, tidy up before stopping */
	clsic_dbg(clsic, "State %d\n", debugsrv_struct->state);

	/*
	 * Fake sending a service shutdown message - this is testing that
	 * services can send messages in their stop() functions.
	 */
	if (debugsrv_struct->state != CLSIC_DEBUGSRV_STATE_IDLE) {
		clsic_init_message((union t_clsic_generic_message *)&msg_cmd,
				   handler->service_instance,
				   CLSIC_DEBUGSRV_CMD_DEACTIVATE);

		ret = clsic_send_msg_sync(clsic, &msg_cmd, &msg_rsp,
					  CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
					  CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);

		/* The above should block and state should now be IDLE */
		if (debugsrv_struct->state != CLSIC_DEBUGSRV_STATE_IDLE) {
			clsic_info(clsic,
				  "deactivate message: %d state now: %d\n",
				  ret, debugsrv_struct->state);
		} else {
			clsic_dbg(clsic,
				  "deactivate message: %d state now: %d\n",
				  ret, debugsrv_struct->state);
		}
	}

	handler->data = NULL;
	kfree(debugsrv_struct);
}

#define BULK_SZ_EXPECTED 16
static void clsic_debug_service_handle_bulk_notif(struct clsic *clsic,
					       struct clsic_message *msg)
{
	uint32_t bulk_sz;
	uint8_t bulk_data[BULK_SZ_EXPECTED];
	int ret;
	const unsigned char bulk_data_expected[] = {
		0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
		0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10 };

	if (clsic_get_bulkbit(msg)) {
		bulk_sz = clsic_get_bulk_sz(&msg->fsm);

		clsic_dbg(clsic,
			  "Expected bulk size: %d. FSM stated size: %d\n",
			  BULK_SZ_EXPECTED, bulk_sz);

		/* Drain bulk data from txfifo */
		ret = clsic_fifo_readbulk_payload(clsic, msg,
						  bulk_data,
						  BULK_SZ_EXPECTED);
		clsic_dbg(clsic, "clsic_fifo_readbulk_payload() returns %d\n",
			  ret);

		if (ret == BULK_SZ_EXPECTED)
			if (memcmp(bulk_data, bulk_data_expected,
				  BULK_SZ_EXPECTED) != 0)
				clsic_dbg(clsic, "Bulk data mismatch\n");
	}
}

static int clsic_debug_service_handler(struct clsic *clsic,
				       struct clsic_service *handler,
				       struct clsic_message *msg)
{
	struct clsic_debugsrv_struct *debugsrv_struct = handler->data;
	uint8_t msgid = clsic_get_messageid(msg);
	int ret = CLSIC_UNHANDLED;

	switch (msgid) {
	case CLSIC_DEBUGSRV_NOTIF_ACTIVE:
		clsic_dbg(clsic, "testing : service active\n");
		/* now in active state */
		debugsrv_struct->state = CLSIC_DEBUGSRV_STATE_ACTIVE;
		ret = CLSIC_HANDLED;
		break;
	case CLSIC_DEBUGSRV_NOTIF_IDLE:
		clsic_dbg(clsic, "testing : service idle\n");
		/* now in idle state */
		debugsrv_struct->state = CLSIC_DEBUGSRV_STATE_IDLE;
		ret = CLSIC_HANDLED;
		break;
	case CLSIC_DEBUGSRV_NOTIF_HANDLED:
	case CLSIC_DEBUGSRV_NOTIF_HANDLED_SHORT:
	case CLSIC_DEBUGSRV_NOTIF_HANDLED_LONG:
	case CLSIC_DEBUGSRV_NOTIF_HANDLED_WRONGDATA:
		clsic_dbg(clsic, "testing : notif handled\n");
		clsic_debug_service_handle_bulk_notif(clsic, msg);
		ret = CLSIC_HANDLED;
		break;
	case CLSIC_DEBUGSRV_NOTIF_UNHANDLED:
		clsic_dbg(clsic, "testing : notif unhandled\n");
		ret = CLSIC_UNHANDLED;
		break;
	default:
		clsic_dbg(clsic, "testing : notif default\n");
		break;
	}

	return ret;
}

/*
 * May be called more than once
 *
 * Only allocate and populate the data structure the first time, but reset the
 * state to idle every time.
 */
int clsic_debug_service_start(struct clsic *clsic,
			      struct clsic_service *handler)
{
	struct clsic_debugsrv_struct *debugsrv_struct = handler->data;

	if (debugsrv_struct == NULL) {
		clsic_dbg(clsic, "Service starting\n");
		debugsrv_struct = kzalloc(sizeof(*debugsrv_struct), GFP_KERNEL);
		if (debugsrv_struct == NULL)
			return -ENOMEM;
		handler->data = debugsrv_struct;
		handler->callback = &clsic_debug_service_handler;
		handler->stop = &clsic_debug_service_stop;
		debugsrv_struct->state = CLSIC_DEBUGSRV_STATE_IDLE;
	} else {
		clsic_dbg(clsic, "Service already started (State: %d)\n",
			  debugsrv_struct->state);
	}

	return 0;
}
