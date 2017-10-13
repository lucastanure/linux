/*
 * clsic-core.c -- CLSIC core driver initialisation
 *
 * Copyright 2017 CirrusLogic, Inc.
 *
 * Author: Simon Trimmer <simont@opensource.cirrus.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

#include <linux/mfd/clsic/core.h>
#include "clsic-trace.h"
#include <linux/mfd/clsic/message.h>
#include <linux/mfd/clsic/irq.h>
#include <linux/mfd/clsic/bootsrv.h>
#include <linux/mfd/clsic/syssrv.h>
#include <linux/mfd/clsic/regmapsrv.h>

static void clsic_init_sysfs(struct clsic *clsic);
static void clsic_deinit_sysfs(struct clsic *clsic);

#ifdef CONFIG_OF
const struct of_device_id clsic_of_match[] = {
	{ .compatible = "cirrus,clsic" },
	{},
};
EXPORT_SYMBOL_GPL(clsic_of_match);
#endif

static const char * const clsic_core_supplies[] = {
	"VDD_A",
	"VDD_IO1",
};

static bool clsic_bootonload = true;
module_param(clsic_bootonload, bool, 0);
MODULE_PARM_DESC(clsic_bootonload,
		 "Whether to boot the device when the module is loaded");

#define CLSIC_POST_RESET_DELAY	500

static atomic_t clsic_instances_count = ATOMIC_INIT(0);

static void clsic_enable_hard_reset(struct clsic *clsic)
{
	if (clsic->reset_gpio)
		gpiod_set_value_cansleep(clsic->reset_gpio, 0);
}

static void clsic_disable_hard_reset(struct clsic *clsic)
{
	if (clsic->reset_gpio) {
		gpiod_set_value_cansleep(clsic->reset_gpio, 1);
		usleep_range(1000, 2000);
	}
}

/* Backported from 4.9 kernel regmap_read_poll_timeout, taken from tacna */
#define clsic_read_poll_timeout(map, addr, val, cond, sleep_us, timeout_us) \
({ \
	ktime_t timeout = ktime_add_us(ktime_get(), timeout_us); \
	int pollret; \
	might_sleep_if(sleep_us); \
	for (;;) { \
		pollret = regmap_read((map), (addr), &(val)); \
		if (pollret) \
			break; \
		if (cond) \
			break; \
		if (timeout_us && ktime_compare(ktime_get(), timeout) > 0) { \
			pollret = regmap_read((map), (addr), &(val)); \
			break; \
		} \
		if (sleep_us) \
			usleep_range((sleep_us >> 2) + 1, sleep_us); \
	} \
	pollret ?: ((cond) ? 0 : -ETIMEDOUT); \
})

/*
 * NOTE: These are quite large timeouts whilst we are in development
 */
#define CLSIC_BOOT_POLL_MICROSECONDS    5000
#define CLSIC_BOOT_TIMEOUT_MICROSECONDS 2000000

static bool clsic_wait_for_boot_done(struct clsic *clsic)
{
	unsigned int val;
	int ret;

	ret = clsic_read_poll_timeout(clsic->regmap, TACNA_IRQ1_EINT2, val,
				      (val & TACNA_BOOT_DONE_EINT1_MASK),
				      CLSIC_BOOT_POLL_MICROSECONDS,
				      CLSIC_BOOT_TIMEOUT_MICROSECONDS);
	if (ret) {
		clsic_err(clsic, "Failed to get BOOT_DONE: %d\n", ret);
		return false;
	}

	return true;
}

static bool clsic_supported_devid(struct clsic *clsic)
{
	int ret = 0;
	unsigned int revid;
	unsigned int fabid;
	unsigned int relid;
	unsigned int otpid;

	/*
	 * When devid is 0 read from the device and print the other IDs to aid
	 * investigations.
	 */
	if (clsic->devid == 0) {
		ret = regmap_read(clsic->regmap, TACNA_DEVID, &clsic->devid);
		if (ret)
			clsic_warn(clsic, "Failed to read ID register: %d\n",
				   ret);

		regmap_read(clsic->regmap, TACNA_REVID, &revid);
		revid &= (TACNA_AREVID_MASK | TACNA_MTLREVID_MASK);
		regmap_read(clsic->regmap, TACNA_FABID, &fabid);
		fabid &= TACNA_FABID_MASK;
		regmap_read(clsic->regmap, TACNA_RELID, &relid);
		relid &= TACNA_RELID_MASK;
		regmap_read(clsic->regmap, TACNA_OTPID, &otpid);
		otpid &= TACNA_OTPID_MASK;

		clsic_info(clsic,
			   "DEVID 0x%x, REVID 0x%x, FABID 0x%x, RELID 0x%x, OTPID 0x%x\n",
			   clsic->devid, revid, fabid, relid, otpid);
	}

	switch (clsic->devid) {
	case CLSIC_SUPPORTED_ID_48AB50:
	case CLSIC_SUPPORTED_ID_EMULATED_CODEC:
	case CLSIC_SUPPORTED_ID_48AC40:
		return true;
	default:
		return false;
	}
}

static int clsic_shutdown_notifier_cb(struct notifier_block *this,
				      unsigned long code, void *data)
{
	struct clsic *clsic = container_of(this, struct clsic,
					   clsic_shutdown_notifier);

	pr_devel("clsic_shutdown_notifier_cb() clsic %p code %ld\n",
		 clsic, code);

	if ((code == SYS_DOWN) || (code == SYS_HALT)) {
		/* signal the device is shutting down - halt the CLSIC device */
		clsic_send_shutdown_cmd(clsic);

	}

	return NOTIFY_DONE;
}

static int clsic_register_reboot_notifier(struct clsic *clsic)
{
	clsic->clsic_shutdown_notifier.notifier_call =
		&clsic_shutdown_notifier_cb;
	BLOCKING_INIT_NOTIFIER_HEAD(&clsic->notifier);

	clsic->instance = atomic_inc_return(&clsic_instances_count);

	return register_reboot_notifier(&clsic->clsic_shutdown_notifier);
}

static int clsic_unregister_reboot_notifier(struct clsic *clsic)
{
	return unregister_reboot_notifier(&clsic->clsic_shutdown_notifier);
}

static int clsic_vdd_d_notify(struct notifier_block *nb,
			      unsigned long action, void *data)
{
	struct clsic *clsic = container_of(nb, struct clsic,
					   vdd_d_notifier);

	dev_dbg(clsic->dev, "VDD_D notify %lx\n", action);

	if (action & REGULATOR_EVENT_DISABLE)
		clsic->vdd_d_powered_off = true;

	return NOTIFY_DONE;
}

static void clsic_regulators_deregister_disable(struct clsic *clsic)
{
	regulator_disable(clsic->vdd_d);
	regulator_bulk_disable(clsic->num_core_supplies, clsic->core_supplies);
	regulator_unregister_notifier(clsic->vdd_d, &clsic->vdd_d_notifier);
	regulator_put(clsic->vdd_d);
}

static int clsic_regulators_register_enable(struct clsic *clsic)
{
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(clsic_core_supplies); i++)
		clsic->core_supplies[i].supply = clsic_core_supplies[i];
	clsic->num_core_supplies = ARRAY_SIZE(clsic_core_supplies);

	ret = devm_regulator_bulk_get(clsic->dev, clsic->num_core_supplies,
				      clsic->core_supplies);
	if (ret) {
		clsic_err(clsic, "Failed to request core supplies: %d\n", ret);
		return ret;
	}

	clsic->vdd_d = regulator_get(clsic->dev, "VDD_D");
	if (IS_ERR(clsic->vdd_d)) {
		ret = PTR_ERR(clsic->vdd_d);
		clsic_err(clsic, "Failed to request VDD_D: %d\n", ret);

		/*
		 * since devres_* version is used to get core regulators
		 * no need for explicit put for them
		 */
		return ret;
	}

	clsic->vdd_d_powered_off = false;
	clsic->vdd_d_notifier.notifier_call = clsic_vdd_d_notify;
	ret = regulator_register_notifier(clsic->vdd_d,
					  &clsic->vdd_d_notifier);
	if (ret) {
		clsic_err(clsic, "Failed to register VDD_D notifier %d\n", ret);
		goto vdd_d_notifier_failed;
	}

	ret = regulator_bulk_enable(clsic->num_core_supplies,
				    clsic->core_supplies);
	if (ret) {
		clsic_err(clsic, "Failed to enable core supplies: %d\n", ret);
		goto core_enable_failed;
	}

	ret = regulator_enable(clsic->vdd_d);
	if (ret) {
		clsic_err(clsic, "Failed to enable VDD_D: %d\n", ret);
		goto vdd_enable_failed;
	}

	return 0;

vdd_enable_failed:
	regulator_bulk_disable(clsic->num_core_supplies, clsic->core_supplies);
core_enable_failed:
	regulator_unregister_notifier(clsic->vdd_d, &clsic->vdd_d_notifier);
vdd_d_notifier_failed:
	regulator_put(clsic->vdd_d);

	return ret;
}

/*
 * Simple function to assign a new state and issue a matching
 * trace event.
 */
void clsic_set_state(struct clsic *clsic, const enum clsic_states newstate)
{
	enum clsic_states state_from = clsic->state;

	clsic->state = newstate;
	trace_clsic_statechange(state_from, newstate);
}

int clsic_dev_init(struct clsic *clsic)
{
	int ret = 0;

	clsic_info(clsic, "%p (bootonload: %d)\n", clsic, clsic_bootonload);

	dev_set_drvdata(clsic->dev, clsic);

	clsic_set_state(clsic, CLSIC_STATE_INACTIVE);

	ret = clsic_regulators_register_enable(clsic);
	if (ret != 0) {
		clsic_err(clsic, "Regulator register failed=%d", ret);
		return ret;
	}

	clsic->reset_gpio = devm_gpiod_get(clsic->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(clsic->reset_gpio)) {
		ret = PTR_ERR(clsic->reset_gpio);
		clsic_err(clsic,
			  "DT property reset-gpio is missing or malformed %d\n",
			  ret);
		clsic->reset_gpio = NULL;
	}

	if (clsic->reset_gpio == NULL) {
		clsic_warn(clsic,
			   "Running without reset GPIO is not recommended\n");
		clsic_soft_reset(clsic);
	} else {
		clsic_enable_hard_reset(clsic);
		msleep(CLSIC_POST_RESET_DELAY);
		clsic_disable_hard_reset(clsic);

		clsic_wait_for_boot_done(clsic);
	}

	if (!clsic_supported_devid(clsic)) {
		clsic_err(clsic, "Unknown device ID: %x\n", clsic->devid);
		ret = -EINVAL;
		goto err_reset;
	}

	INIT_WORK(&clsic->maintenance_handler, clsic_maintenance);

	clsic_init_sysfs(clsic);

	clsic_init_debugfs(clsic);

	ret = clsic_setup_message_interface(clsic);
	if (ret != 0)
		goto messaging_failed;

	ret = clsic_register_reboot_notifier(clsic);
	if (ret != 0) {
		clsic_err(clsic, "Register reboot notifier ret=%d", ret);
		goto notifier_failed;
	}

	/* The irq starts disabled */
	ret = clsic_irq_init(clsic);
	if (ret != 0)
		goto irq_failed;

	mutex_init(&clsic->service_lock);

	/*
	 * We expect these services to be on all devices in this family; during
	 * POR bootup the driver will receive a notification from either the
	 * bootloader if there is an issue or from the system service announcing
	 * message protocol availability.
	 *
	 * Preregister these two service handlers so that if a notification
	 * arrives during boot it can be suitably handled.
	 */
	ret = clsic_register_service_handler(clsic,
					     CLSIC_SRV_INST_SYS,
					     CLSIC_SRV_TYPE_SYS,
					     0, clsic_system_service_start);
	if (ret != 0)
		goto system_service_start_failed;

	ret = clsic_register_service_handler(clsic,
					     CLSIC_SRV_INST_BLD,
					     CLSIC_SERVICE_TYPE_BOOTLOADER,
					     0, clsic_bootsrv_service_start);
	if (ret != 0)
		goto bootloader_service_start_failed;

	if (clsic_bootonload)
		clsic_soft_reset(clsic);

	clsic_irq_enable(clsic);

	/*
	 * At this point the device is NOT fully setup - initialisation will
	 * continue after the device raises an interrupt.
	 */

	return 0;

	/* If errors are encountered, tidy up and deallocate as appropriate */
bootloader_service_start_failed:
	clsic->service_handlers[CLSIC_SRV_INST_SYS]->stop(
			clsic,
			clsic->service_handlers[CLSIC_SRV_INST_SYS]);
	clsic_deregister_service_handler(clsic,
			clsic->service_handlers[CLSIC_SRV_INST_SYS]);
	kfree(clsic->service_handlers[CLSIC_SRV_INST_SYS]);
system_service_start_failed:
	clsic_irq_exit(clsic);
irq_failed:
notifier_failed:
	clsic_unregister_reboot_notifier(clsic);
	clsic_shutdown_message_interface(clsic);
messaging_failed:
	clsic_deinit_debugfs(clsic);
	clsic_deinit_sysfs(clsic);

err_reset:
	clsic_enable_hard_reset(clsic);

	return ret;
}
EXPORT_SYMBOL_GPL(clsic_dev_init);

int clsic_fwupdate_reset(struct clsic *clsic)
{
	int ret = 0;

	ret = regmap_update_bits(clsic->regmap, CLSIC_FW_UPDATE_REG,
				 CLSIC_FW_UPDATE_BIT, CLSIC_FW_UPDATE_BIT);
	if (ret == 0)
		ret = clsic_soft_reset(clsic);

	return ret;
}

int clsic_soft_reset(struct clsic *clsic)
{
	int ret = 0;

	clsic_info(clsic, "%p\n", clsic);

	clsic_irq_disable(clsic);

	/* Initiate chip software reset */
	regmap_write(clsic->regmap, TACNA_SFT_RESET, CLSIC_SOFTWARE_RESET_CODE);

	msleep(CLSIC_POST_RESET_DELAY);

	/* Wait for boot done */
	clsic_wait_for_boot_done(clsic);

	clsic_irq_enable(clsic);
	return ret;
}

/*
 * Called when the device has informed the system service of a panic or other
 * fatal error.
 */
void clsic_dev_panic(struct clsic *clsic, struct clsic_message *msg)
{
	int ret;

	trace_clsic_dev_panic(clsic->state);
	clsic_dump_message(clsic, msg, "clsic_dev_panic() Panic Notification");
	memcpy(&clsic->last_panic.msg, &msg->fsm, CLSIC_FIXED_MSG_SZ);

	ret = clsic_fifo_readbulk_payload(clsic, msg, (uint8_t *)
					  &clsic->last_panic.di,
					  sizeof(clsic->last_panic.di));

	clsic_info(clsic, "ret: %d version: %d encrypted: %d\n",
		   ret,
		   clsic->last_panic.di.version,
		   clsic->last_panic.di.encrypted);

	clsic_set_state(clsic, CLSIC_STATE_PANIC);

	mutex_lock(&clsic->message_lock);
	clsic_purge_message_queues(clsic);
	mutex_unlock(&clsic->message_lock);

	/*
	 * If the device panics don't attempt to recover it automatically,
	 * the user will need to reboot or trigger a device reset
	 * schedule_work(&clsic->maintenance_handler);
	 */
}

/*
 * The driver maintenance thread used for progressing state - the kernel init
 * context can't be used as it would block kernel boot and the messaging thread
 * can't be used as that thread is required to progress messages.
 *
 * The main tasks that this thread progresses are the main system reset and
 * service enumeration task and sending the bootloader any data it requires to
 * start or upgrade the device.
 *
 * If the device state is inactive then the driver is in a first touch
 * situation, reset then enumerate the device.
 *
 * If the state is panic then the driver will stay halted
 *
 * If the device is starting or active then do nothing as no work is required,
 * either it is already running or the driver will receive a further
 * notification indicating what action is required.
 *
 * If the device is in one of the bootloader states then call the bootloader
 * service handler to progress the system booting.  The bootloader end state
 * signals that the bootloader service has successfully downloaded software to
 * the device.  This is separated out into a different logical state as at this
 * point some devices will be reset whilst on others the driver should attempt
 * service enumeration.
 */
void clsic_maintenance(struct work_struct *data)
{
	struct clsic *clsic = container_of(data, struct clsic,
					   maintenance_handler);

	switch (clsic->state) {
	case CLSIC_STATE_INACTIVE:
		clsic_soft_reset(clsic);
		break;
	case CLSIC_STATE_ENUMERATING:
		clsic_system_service_enumerate(clsic);
		break;
	case CLSIC_STATE_BOOTLOADER_BEGIN ... CLSIC_STATE_BOOTLOADER_WFR:
		clsic_bootsrv_state_handler(clsic);
		break;
	case CLSIC_STATE_STARTING:
	case CLSIC_STATE_STOPPING:
	case CLSIC_STATE_STOPPED:
	case CLSIC_STATE_ACTIVE:
		break;
	case CLSIC_STATE_PANIC:
		clsic_info(clsic, "Device has sent a panic notification\n");
		break;
	case CLSIC_STATE_LOST:
		clsic_info(clsic, "Device failed to start\n");
		break;
	default:
		clsic_info(clsic, "Defaulted: %d\n", clsic->state);
	}
}

int clsic_dev_exit(struct clsic *clsic)
{
	int i;

	clsic_info(clsic, "%p\n", clsic);

	if (clsic->state == CLSIC_STATE_ACTIVE)
		clsic_set_state(clsic, CLSIC_STATE_STOPPING);

	/* If it's still booting, cancel that work */
	mutex_lock(&clsic->message_lock);
	clsic_purge_message_queues(clsic);
	mutex_unlock(&clsic->message_lock);
	cancel_work_sync(&clsic->maintenance_handler);

	/*
	 * If any of the services registered child devices this will call their
	 * remove callback. This is being done before shutting down the service
	 * handlers because child mfd drivers may require service functionality
	 * to shutdown cleanly, such as the register access service.
	 */
	mfd_remove_devices(clsic->dev);

	clsic_unregister_reboot_notifier(clsic);

	/*
	 * To safely shutdown the device this driver will need to transition
	 * the device's state machine to idle and then issue a shutdown
	 * command, after which device power can be removed.
	 *
	 * Give all the service handlers a chance to tidy themselves up, they
	 * can send more messages to the device to tidy the services up.  On
	 * return they are expected to have deregistered and released all their
	 * resources. When all services have been shutdown the device should be
	 * in an idle state and be ready to be shutdown.
	 *
	 * The ordering of shutdown is important, service instance 0 is the
	 * system service that is used in some bulk transfers as well as error
	 * handling and will issue the shutdown command.
	 *
	 * As that service should be done last, shut them down in reverse order.
	 */
	for (i = CLSIC_SERVICE_MAX; i >= 0; i--) {
		if (clsic->service_handlers[i] != NULL) {
			clsic_dbg(clsic, "Stopping %d: %pF\n",
				  i, clsic->service_handlers[i]->stop);
			/* a stop() callback on handlers is optional */
			if (clsic->service_handlers[i]->stop != NULL)
				clsic->service_handlers[i]->stop(clsic,
						    clsic->service_handlers[i]);

			clsic_deregister_service_handler(clsic,
						    clsic->service_handlers[i]);

			kfree(clsic->service_handlers[i]);
		}
	}

	clsic_irq_exit(clsic);

	clsic_deinit_debugfs(clsic);
	clsic_deinit_sysfs(clsic);

	clsic_shutdown_message_interface(clsic);

	clsic_regulators_deregister_disable(clsic);

	clsic_enable_hard_reset(clsic);

	return 0;
}
EXPORT_SYMBOL_GPL(clsic_dev_exit);

static int clsic_noservice_handler(struct clsic *clsic,
				   struct clsic_service *handler,
				   struct clsic_message *msg)
{
	clsic_dump_message(clsic, msg, "unhandled message");

	return CLSIC_UNHANDLED;
}

/* Register as a handler for a service ID */
int clsic_register_service_handler(struct clsic *clsic,
				   uint8_t service_instance,
				   uint16_t service_type,
				   uint32_t service_version,
				   int (*start)(struct clsic *clsic,
						struct clsic_service *handler))
{
	struct clsic_service *tmp_handler;
	int ret = 0;

	clsic_dbg(clsic, "%p %d: %pF\n", clsic, service_instance, start);

	if (service_instance > CLSIC_SERVICE_MAX) {
		clsic_err(clsic, "%p:%d out of range\n", start,
			  service_instance);
		return -EINVAL;
	}

	mutex_lock(&clsic->service_lock);
	if (clsic->service_handlers[service_instance] != NULL) {
		clsic_dbg(clsic, "%d pre-registered %p\n", service_instance,
			  start);

		/*
		 * Check the service type matches, if not call stop and
		 * repopulate as a new handler.
		 */
		tmp_handler = clsic->service_handlers[service_instance];
		if ((tmp_handler->service_instance != service_instance) ||
		    (tmp_handler->service_type != service_type)) {
			clsic_err(clsic,
				  "handler different: instance %d:%d type 0x%x:0x%x\n",
				  service_instance,
				  tmp_handler->service_instance,
				  service_type, tmp_handler->service_type);

			tmp_handler->stop(clsic, tmp_handler);

			tmp_handler->service_instance = service_instance;
			tmp_handler->service_type = service_type;
		}
	} else {
		tmp_handler = kzalloc(sizeof(*tmp_handler), GFP_KERNEL);
		if (tmp_handler == NULL) {
			ret = -ENOMEM;
			goto reterror;
		}

		tmp_handler->service_instance = service_instance;
		tmp_handler->service_type = service_type;
		tmp_handler->callback = &clsic_noservice_handler;
		clsic->service_handlers[service_instance] = tmp_handler;
	}
	tmp_handler->service_version = service_version;
	mutex_unlock(&clsic->service_lock);

	if (start != NULL)
		ret = (start) (clsic,
			       clsic->service_handlers[service_instance]);

reterror:
	return ret;
}
EXPORT_SYMBOL_GPL(clsic_register_service_handler);

/*
 * Deregister a service handler - this expects to be called with the same
 * structure that was originally registered
 */
int clsic_deregister_service_handler(struct clsic *clsic,
				     struct clsic_service *handler)
{
	int ret = 0;
	uint8_t servinst = handler->service_instance;

	clsic_dbg(clsic, "%p %d: %pF\n", clsic, servinst, handler->callback);

	if (servinst > CLSIC_SERVICE_MAX) {
		clsic_err(clsic, "%p:%d out of range\n", handler, servinst);
		return -EINVAL;
	}

	mutex_lock(&clsic->service_lock);

	if (clsic->service_handlers[servinst] == NULL) {
		clsic_err(clsic, "%d not registered %p\n", servinst, handler);
		ret = -EINVAL;
	} else if (clsic->service_handlers[servinst] != handler) {
		clsic_err(clsic, "%d not matched %p != %p\n", servinst,
			  handler, clsic->service_handlers[servinst]);
		ret = -EINVAL;
	} else {
		clsic->service_handlers[servinst] = NULL;
	}

	mutex_unlock(&clsic->service_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(clsic_deregister_service_handler);

/*
 * Typically called by the codec driver to register a callback that enables the
 * core driver to pass structures of codec controls.
 */
int clsic_register_notifier(struct clsic *clsic, struct notifier_block *nb)
{
	int ret = 0;
	int i;

	clsic_info(clsic, "clsic: %p data: %p\n", clsic, nb);

	clsic_info(clsic, "clsic: %p data: %p cb: %pF\n",
		   clsic, nb, nb->notifier_call);

	ret = blocking_notifier_chain_register(&clsic->notifier, nb);
	if (ret != 0)
		return ret;

	/*
	 * For each service, if they have registered controls before the codec
	 * registers the callback then register them with the codec
	 */
	for (i = 0; i <= CLSIC_SERVICE_MAX; i++) {
		if ((clsic->service_handlers[i] != NULL) &&
		    (clsic->service_handlers[i]->kcontrols != NULL)) {
			clsic_register_codec_controls(clsic,
				     clsic->service_handlers[i]->kcontrol_count,
				     clsic->service_handlers[i]->kcontrols);
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(clsic_register_notifier);

/*
 * Typically called by the codec driver to remove it's callback handler.
 */
int clsic_deregister_notifier(struct clsic *clsic, struct notifier_block *nb)
{
	clsic_info(clsic, "clsic: %p data: %p\n", clsic, nb);

	clsic_info(clsic, "clsic: %p data: %p fn: %pF\n",
		   clsic, nb, nb->notifier_call);

	return blocking_notifier_chain_unregister(&clsic->notifier, nb);
}
EXPORT_SYMBOL_GPL(clsic_deregister_notifier);

/*
 * This function passes a service controls structure over to the codec so they
 * can be added.
 *
 * This is currently being invoked by service handlers when they add controls
 * but it could be performed by the service infrastructure after the service
 * start() function returns.
 */
int clsic_register_codec_controls(struct clsic *clsic,
				  uint8_t kcontrol_count,
				  struct snd_kcontrol_new *kcontrols)
{
	struct clsic_controls_cb_data cbdata;

	clsic_info(clsic, "%d controls: %p\n", kcontrol_count, kcontrols);

	cbdata.kcontrol_count = kcontrol_count;
	cbdata.kcontrols = kcontrols;

	return blocking_notifier_call_chain(&clsic->notifier,
					    CLSIC_NOTIFY_ADD_KCONTROLS,
					    &cbdata);
}

/*
 * This function passes a service controls structure over to the codec so they
 * can be removed or deactivated.
 *
 * This is somewhat wishful thinking as I don't think it is possible to remove
 * a control, though it could be possible for the controls to have their type
 * or function callbacks changed to be benign.
 *
 * This is currently being invoked by service handlers in their stop() routine
 * but it could be performed by the service infrastructure before the service
 * stop() function is called.
 */

int clsic_deregister_codec_controls(struct clsic *clsic,
				    uint8_t kcontrol_count,
				    struct snd_kcontrol_new *kcontrols)
{
	struct clsic_controls_cb_data cbdata;

	clsic_info(clsic, "%d controls: %p\n", kcontrol_count, kcontrols);

	cbdata.kcontrol_count = kcontrol_count;
	cbdata.kcontrols = kcontrols;

	return blocking_notifier_call_chain(&clsic->notifier,
					    CLSIC_NOTIFY_REMOVE_KCONTROLS,
					    &cbdata);
}

#ifdef CONFIG_DEBUG_FS

/*
 * This method is just so we can trigger the enumeration process that would
 * normally occur when the device raises a bootdone interrupt
 */
static int clsic_bootdone_write(void *data, u64 val)
{
	struct clsic *clsic = data;

	schedule_work(&clsic->maintenance_handler);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(clsic_bootdone_fops, NULL,
			clsic_bootdone_write, "%llu\n");

static ssize_t clsic_services_read_file(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct clsic *clsic = file_inode(file)->i_private;
	char *buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	ssize_t len;
	ssize_t ret = 0;
	int i;

	if (buf == NULL)
		return -ENOMEM;

	len = snprintf(buf + ret, PAGE_SIZE - ret,
		       "Registered service handlers:\n");
	if (len >= 0)
		ret += len;
	if (ret > PAGE_SIZE)
		ret = PAGE_SIZE;

	if (mutex_lock_interruptible(&clsic->service_lock)) {
		kfree(buf);
		return -EINTR;
	}

	for (i = 0; i <= CLSIC_SERVICE_MAX; i++) {
		if (clsic->service_handlers[i] == NULL)
			len = snprintf(buf + ret, PAGE_SIZE - ret,
				       "%2d: no handler registered\n", i);
		else
			len = snprintf(buf + ret, PAGE_SIZE - ret,
				    "%2d: 0x%04X 0x%08X %pF\n",
				    i,
				    clsic->service_handlers[i]->service_type,
				    clsic->service_handlers[i]->service_version,
				    clsic->service_handlers[i]->callback);

		if (len >= 0)
			ret += len;
		if (ret > PAGE_SIZE) {
			ret = PAGE_SIZE;
			break;
		}
	}

	mutex_unlock(&clsic->service_lock);
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	kfree(buf);

	return ret;
}

static const struct file_operations clsic_services_fops = {
	.read = &clsic_services_read_file,
	.llseek = &default_llseek,
};

static ssize_t clsic_state_panic_file(struct file *file,
				      char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct clsic *clsic = file_inode(file)->i_private;
	ssize_t ret = 0;

	ret = simple_read_from_buffer(user_buf, count, ppos,
				      &clsic->last_panic,
				      sizeof(clsic->last_panic));

	return ret;
}

static const struct file_operations clsic_panic_fops = {
	.read = &clsic_state_panic_file,
	.llseek = &default_llseek,
};

/* 13 as the name will be at most "clsic-nnn" + \0 */
#define CLSIC_DEBUGFS_DIRNAME_MAX		13
void clsic_init_debugfs(struct clsic *clsic)
{
	char dirname[CLSIC_DEBUGFS_DIRNAME_MAX];

	if (clsic->instance == 0)
		strlcpy(dirname, "clsic", sizeof(dirname));
	else
		snprintf(dirname, sizeof(dirname), "clsic-%d", clsic->instance);

	clsic->debugfs_root = debugfs_create_dir(dirname, NULL);
	if (clsic->debugfs_root == NULL) {
		clsic_err(clsic, "Failed to create debugfs dir\n");
		return;
	}

	debugfs_create_file("bootdone",
			    S_IWUSR | S_IWGRP,
			    clsic->debugfs_root, clsic, &clsic_bootdone_fops);

	debugfs_create_file("services", S_IRUSR | S_IRGRP | S_IROTH,
			    clsic->debugfs_root, clsic, &clsic_services_fops);

	debugfs_create_file("last_panic",
			    S_IRUSR | S_IRGRP,
			    clsic->debugfs_root, clsic, &clsic_panic_fops);
}

void clsic_deinit_debugfs(struct clsic *clsic)
{
	debugfs_remove_recursive(clsic->debugfs_root);

	clsic->debugfs_root = NULL;
}

#else /* ifdef CONFIG_DEBUG_FS */

void clsic_init_debugfs(struct clsic *clsic)
{
}

void clsic_deinit_debugfs(struct clsic *clsic)
{
}

#endif

static ssize_t clsic_store_state(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct clsic *clsic = dev_get_drvdata(dev);

	if (!strncmp(buf, "reset", strlen("reset"))) {
		clsic_info(clsic, "software reset\n");
		clsic_set_state(clsic, CLSIC_STATE_INACTIVE);
		clsic_soft_reset(clsic);
	}
	return count;
}

static ssize_t clsic_show_state(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct clsic *clsic = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%s\n",
			clsic_state_to_string(clsic->state));
}
static DEVICE_ATTR(state, S_IRUGO | S_IWUSR,
		   clsic_show_state, clsic_store_state);

static ssize_t clsic_show_devid(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct clsic *clsic = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "0x%x\n", clsic->devid);
}
static DEVICE_ATTR(devid, S_IRUGO, clsic_show_devid, NULL);

static void clsic_init_sysfs(struct clsic *clsic)
{
	device_create_file(clsic->dev, &dev_attr_devid);
	device_create_file(clsic->dev, &dev_attr_state);
}

static void clsic_deinit_sysfs(struct clsic *clsic)
{
	device_remove_file(clsic->dev, &dev_attr_devid);
	device_remove_file(clsic->dev, &dev_attr_state);
}

MODULE_DESCRIPTION("CLSIC MFD");
MODULE_AUTHOR("Simon Trimmer <simont@opensource.cirrus.com>");
MODULE_LICENSE("GPL v2");
