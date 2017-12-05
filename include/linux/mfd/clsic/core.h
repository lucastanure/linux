/*
 * core.h -- CLSIC core definitions
 *
 * Copyright 2017 CirrusLogic, Inc.
 *
 * Author: Simon Trimmer <simont@opensource.cirrus.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CIRRUS_CLSIC_CORE_H
#define CIRRUS_CLSIC_CORE_H
#include <linux/mfd/core.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/regmap.h>

#ifndef PACKED
#define PACKED __packed
#endif
#include <linux/mfd/clsic/clsicmessagedefines.h>

/*
 * XXX enable DEBUG  in the kernel headers - in particular this globally
 * enables clsic_dbg() messages
 */
#define DEBUG

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/semaphore.h>
#include <linux/kconfig.h>
#include <linux/regmap.h>
#include <linux/notifier.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <linux/mfd/clsic/registers.h>

#define clsic_dbg(_clsic, fmt, ...) \
	dev_dbg(_clsic->dev, "%s() " fmt, __func__, ##__VA_ARGS__)
#define clsic_info(_clsic, fmt, ...) \
	dev_info(_clsic->dev, "%s() " fmt, __func__, ##__VA_ARGS__)
#define clsic_warn(_clsic, fmt, ...) \
	dev_warn(_clsic->dev, "%s() " fmt, __func__, ##__VA_ARGS__)
#define clsic_err(_clsic, fmt, ...) \
	dev_err(_clsic->dev, "%s() " fmt, __func__, ##__VA_ARGS__)

extern const struct of_device_id clsic_of_match[];

#define CLSIC_SUPPORTED_ID_48AB50		0x48AB50
#define CLSIC_SUPPORTED_ID_EMULATED_CODEC	0xF48AB50
#define CLSIC_SUPPORTED_ID_48AC40		0x48AC40

#define CLSIC_SERVICE_TYPE_DEBUG_EMU		0x1E
#define CLSIC_SERVICE_TYPE_BOOTLOADER	0x1F

#define CLSIC_SERVICE_COUNT			32	/* 0 to 31 */
#define CLSIC_SERVICE_MAX			(CLSIC_SERVICE_COUNT - 1)

#define CLSIC_MAX_CORE_SUPPLIES			2

enum clsic_states {
	CLSIC_STATE_INACTIVE,
	CLSIC_STATE_STARTING,
	CLSIC_STATE_ENUMERATING,
	CLSIC_STATE_ACTIVE,
	CLSIC_STATE_STOPPING,
	CLSIC_STATE_STOPPED,
	CLSIC_STATE_BOOTLOADER_BEGIN,
	CLSIC_STATE_BOOTLOADER_FWU,
	CLSIC_STATE_BOOTLOADER_CPK,
	CLSIC_STATE_BOOTLOADER_MAB,
	CLSIC_STATE_BOOTLOADER_WFR,
	CLSIC_STATE_PANIC,
	CLSIC_STATE_LOST,
	CLSIC_STATE_DEBUGCONTROL_REQUESTED,
	CLSIC_STATE_DEBUGCONTROL_GRANTED,
};

static inline const char *clsic_state_to_string(enum clsic_states state)
{
	switch (state) {
	case CLSIC_STATE_INACTIVE:
		return "INACTIVE";
	case CLSIC_STATE_STARTING:
		return "STARTING";
	case CLSIC_STATE_ENUMERATING:
		return "ENUMERATING";
	case CLSIC_STATE_STOPPING:
		return "STOPPING";
	case CLSIC_STATE_STOPPED:
		return "STOPPED";
	case CLSIC_STATE_ACTIVE:
		return "ACTIVE";
	case CLSIC_STATE_BOOTLOADER_BEGIN:
		return "BOOTLOADER_BEGIN";
	case CLSIC_STATE_BOOTLOADER_FWU:
		return "BOOTLOADER_FWU";
	case CLSIC_STATE_BOOTLOADER_CPK:
		return "BOOTLOADER_CPK";
	case CLSIC_STATE_BOOTLOADER_MAB:
		return "BOOTLOADER_MAB";
	case CLSIC_STATE_BOOTLOADER_WFR:
		return "BOOTLOADER_WFR";
	case CLSIC_STATE_PANIC:
		return "PANIC";
	case CLSIC_STATE_LOST:
		return "LOST";
	case CLSIC_STATE_DEBUGCONTROL_REQUESTED:
		return "DEBUGCONTROL_REQUESTED";
	case CLSIC_STATE_DEBUGCONTROL_GRANTED:
		return "DEBUGCONTROL_GRANTED";
	default:
		return "UNKNOWN";
	}
}

struct clsic_panic {
	union t_clsic_generic_message msg;
	struct clsic_debug_info di;
};

#ifdef CONFIG_DEBUG_FS
enum clsic_simirq_state {
	CLSIC_SIMIRQ_STATE_DEASSERTED = 0,
	CLSIC_SIMIRQ_STATE_ASSERTED = 1
};
#endif

struct clsic {
	struct regmap *regmap;

	struct device *dev;
	uint32_t devid;
	int irq;

	uint8_t instance; /* instance number */
	enum clsic_states state;

	struct notifier_block clsic_shutdown_notifier;

	/*
	 * Location of the FIFO TX register
	 *
	 * Set to one of:
	 * CLSIC_SCP_TX_SPI
	 * CLSIC_SCP_TX_SLIMBUS
	 * CLSIC_SCP_TX_SOUNDWIRE
	 */
	uint32_t fifo_tx;

	/*
	 * This handler takes over the booting and enumeration of the system
	 * and servicing requests from the device bootloader.
	 *
	 * It has a brief lifespan and uses the shared workqueue
	 */
	struct work_struct maintenance_handler;

	/* The message layer has it's own workqueue as it is long lived */
	struct workqueue_struct *message_worker_queue;
	struct work_struct message_work;
	struct timer_list workerthread_timer;

	/*
	 * Number of times the worker thread had nothing to do on this message.
	 * This value is updated AFTER the timer runs and is measured in
	 * seconds.
	 */
	uint8_t timeout_counter;

	/*
	 * Informational counters indicating how many messages have been sent
	 * and received on the message bus
	 */
	uint32_t messages_sent;
	uint32_t messages_received;

	/* A message has been sent to the secure processor */
	bool clsic_msgproc_message_sent;
	/* The secure processor has responded and is certainly on */
	bool clsic_msgproc_responded;

	/* To be held whilst manipulating message queues */
	struct mutex message_lock;

	/*
	 * To be held whilst manipulating services and calling service handler
	 */
	struct mutex service_lock;

	/* Slab cache of messages */
	struct kmem_cache *message_cache;

	/*
	 * Single pointer to the message currently blocking the bus,
	 * if this is NULL then the bus is available
	 */
	struct clsic_message *current_msg;
	/* List of messages that are blocked waiting to send */
	struct list_head waiting_to_send;
	/* List of messages sent and/or ack'd but not completed */
	struct list_head waiting_for_response;
	/* List of messages completed but not released */
	struct list_head completed_messages;

	/* Array of pointers to service handlers */
	struct clsic_service *service_handlers[CLSIC_SERVICE_COUNT];

	/* Notifier typically used to signal the codec */
	struct blocking_notifier_head notifier;

	/* Pre-allocated area for a panic message and debug info payload */
	struct clsic_panic last_panic;

#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_root;

	/* Debugcontrol members protected by message_lock */
	struct completion *debugcontrol_completion;

	/* Simulated IRQ
	 * Represent interrupt enabled status and asserted state
	 * Supporting timer and work structure.
	 */
	bool simirq_enabled;
	enum clsic_simirq_state simirq_state;
	struct work_struct simirq_work;
	struct timer_list simirq_timer;
#endif

	struct gpio_desc *reset_gpio;

	/* PM related members */
	int num_core_supplies;
	struct regulator_bulk_data core_supplies[CLSIC_MAX_CORE_SUPPLIES];
	struct regulator *vdd_d;
	struct notifier_block vdd_d_notifier;
	bool vdd_d_powered_off;
};

int clsic_dev_init(struct clsic *clsic);
int clsic_dev_exit(struct clsic *clsic);
int clsic_fwupdate_reset(struct clsic *clsic);
int clsic_soft_reset(struct clsic *clsic);
void clsic_dev_panic(struct clsic *clsic, struct clsic_message *msg);
void clsic_maintenance(struct work_struct *data);

/*
 * This controls callback data structure is used to communicate an array of new
 * kcontrols with the codec through the notifier interface
 */
struct clsic_controls_cb_data {
	uint8_t kcontrol_count;
	struct snd_kcontrol_new *kcontrols;
};

/* Notifier events */
enum clsic_notifications {
	CLSIC_NOTIFY_ADD_KCONTROLS,
	CLSIC_NOTIFY_REMOVE_KCONTROLS,
};

int clsic_register_notifier(struct clsic *clsic, struct notifier_block *nb);
int clsic_deregister_notifier(struct clsic *clsic, struct notifier_block *nb);

int clsic_register_codec_controls(struct clsic *clsic,
				  uint8_t kcontrol_count,
				  struct snd_kcontrol_new *kcontrols);
int clsic_deregister_codec_controls(struct clsic *clsic,
				    uint8_t kcontrol_count,
				    struct snd_kcontrol_new *kcontrols);

/*
 * This service struct contains instance specific information about a service
 * handler.
 *
 * It is allocated by a service and passed during register_service_handler
 */
struct clsic_service {
	int (*callback)(struct clsic *clsic,
			struct clsic_service *handler,
			struct clsic_message *msg);

	void (*stop)(struct clsic *clsic, struct clsic_service *handler);

	uint8_t service_instance;
	uint16_t service_type;
	uint32_t service_version;

	uint8_t kcontrol_count;
	struct snd_kcontrol_new *kcontrols;

	/* A pointer the handler can use to stash instance specific stuff */
	void *data;
};

int clsic_register_service_handler(struct clsic *clsic,
				   uint8_t service_instance,
				   uint16_t service_type,
				   uint32_t service_version,
				   int (*start)(struct clsic *clsic,
						struct clsic_service *handler));

int clsic_deregister_service_handler(struct clsic *clsic,
				     struct clsic_service *handler);

void clsic_init_debugfs(struct clsic *clsic);
void clsic_deinit_debugfs(struct clsic *clsic);

void clsic_set_state(struct clsic *clsic, const enum clsic_states newstate);

/* in Tables */
bool clsic_readable_register(struct device *dev, unsigned int reg);
bool clsic_volatile_register(struct device *dev, unsigned int reg);
extern const struct reg_default clsic_reg_defaults[];

/*
 * Locates the first service handler instance for a service of the given type.
 */
static inline struct clsic_service *clsic_find_first_service(
							    struct clsic *clsic,
							    u16 service_type)
{
	int i;

	for (i = 0; i < CLSIC_SERVICE_COUNT; ++i)
		if (clsic->service_handlers[i] &&
		    clsic->service_handlers[i]->service_type == service_type)
			return clsic->service_handlers[i];

	return NULL;
}
#endif
