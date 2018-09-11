/*****************************************************************************
 *
 * Copyright (c) 2018
 * Cirrus Logic, Inc. and Cirrus Logic International Semiconductor Ltd.
 * All rights reserved.
 * This software as well as any related documentation is furnished under
 * license and may only be used or copied in accordance with the terms of the
 * license. The information in this file is furnished for informational use
 * only, is subject to change without notice, and should not be construed as
 * a commitment by Cirrus Logic. Cirrus Logic assumes no responsibility or
 * liability for any errors or inaccuracies that may appear in this document or
 * any software that may be provided in association with this document.
 * Except as permitted by such license, no part of this document may be
 * reproduced, stored in a retrieval system, or transmitted in any form or by
 * any means without the express written consent of Cirrus Logic.
 *
 ******************************************************************************/

#ifndef CLSICMESSAGEDEFINESDIAGNOSTICS_DBG_H_
#define CLSICMESSAGEDEFINESDIAGNOSTICS_DBG_H_

#include "clsicmessagedefines.h"

/**
 *  Service type identifier.
 */
#define CLSIC_SRV_TYPE_DBG			(0x4244)

/**
 *  Service version number.
 */
#define CLSIC_SRV_VERSION_DBG			(0x00020000)

/**
 *  Debug Service message identifiers.
 */
enum clsic_dbg_msg_id {
	CLSIC_DBG_MSG_CR_INVOKE_PANIC		= 0,
	CLSIC_DBG_MSG_CR_DUMP_COV		= 1,
	CLSIC_DBG_MSG_CR_DUMP_MEM_INFO		= 2,
};

/**
 *  OOPS panic subcategories used in cmd_invoke_panic structure.
 */
enum clsic_sys_panic_oops_subcategory {
	CLSIC_PANIC_ASSERT	= 0,
	CLSIC_PANIC_SYSTEM	= 1,
};

/**
 *  Debug Service messages.
 */
union clsic_dbg_msg {
	uint8_t raw_msg[CLSIC_FIXED_MSG_SZ];

	/**
	 *  CLSIC_DBG_MSG_CR_INVOKE_PANIC command structure. Pass a
	 *  clsic_sys_panic_category enum as category to invoke a panic of that
	 *  category. OOPS category supports subcategories.
	 *  Passing CLSIC_PANIC_EXCEPT_PREFETCH is not supported.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
		uint8_t category;
		uint8_t subcategory;
	} PACKED cmd_invoke_panic;

	/**
	 *  CLSIC_DBG_MSG_CR_INVOKE_PANIC response structure. This is only
	 *  returned if there was an error in executing the command (e.g.
	 *  invalid param). Otherwise the panic is invoked without sending a
	 *  response.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_invoke_panic;

	/**
	 *  CLSIC_DBG_MSG_CR_DUMP_COV command structure. In coverage builds,
	 *  causes the coverage data to be dumped to the trace buffer. For
	 *  non-coverage builds, this is a noop.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
	} PACKED cmd_dump_cov;

	/**
	 *  CLSIC_DBG_MSG_CR_DUMP_COV response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_dump_cov;

	/**
	 *  CLSIC_DBG_MSG_CR_DUMP_STACK command structure.
	 *  Will print stack information for active tasks and heap stats
	 */
	struct {
		struct clsic_cmd_hdr hdr;
	} PACKED cmd_dump_stack;

	/**
	 *  CLSIC_DBG_MSG_CR_DUMP_STACK response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_dump_stack;
} PACKED;

#endif /* CLSICMESSAGEDEFINESDIAGNOSTICS_DBG_H_ */
