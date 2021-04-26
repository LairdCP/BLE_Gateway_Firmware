/**
 * @file control_task.c
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(control, CONFIG_CONTROL_TASK_LOG_LEVEL);
#define THIS_FILE "control"

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <logging/log_ctrl.h>
#include <fs/fs.h>

#ifdef CONFIG_MODEM_HL7800
#include <drivers/modem/hl7800.h>
#include "fota_smp.h"
#endif

#include "FrameworkIncludes.h"
#include "lcz_software_reset.h"
#include "attr.h"
#include "lte.h"
#include "gateway_common.h"
#include "gateway_fsm.h"
#include "rand_range.h"

#ifdef CONFIG_BLUEGRASS
#include "bluegrass.h"
#endif

#ifdef CONFIG_CONTACT_TRACING
#include "ct_ble.h"
#endif

#ifdef CONFIG_BOARD_MG100
#include "lcz_motion.h"
#endif

#ifdef CONFIG_LWM2M
#include "lcz_lwm2m_client.h"
#endif

#include "control_task.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/

typedef struct control_task_obj {
	FwkMsgTask_t msgTask;
	uint32_t broadcast_count;
	bool fota_request;
	bool cloud_connected;
} control_task_obj_t;

#define CONTROL_TASK_QUEUE_DEPTH 32

#define MSG "These attributes must be consecutive."
BUILD_ASSERT(ATTR_ID_joinDelay + 1 == ATTR_ID_joinMin, MSG);
BUILD_ASSERT(ATTR_ID_joinMin + 1 == ATTR_ID_joinMax, MSG);
BUILD_ASSERT(ATTR_ID_joinMax + 1 == ATTR_ID_joinInterval, MSG);

#if defined(CONFIG_COAP_FOTA) && defined(CONFIG_HTTP_FOTA)
#error "Dual network FOTA not supported"
#endif

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static control_task_obj_t cto;

K_MSGQ_DEFINE(control_task_queue, FWK_QUEUE_ENTRY_SIZE,
	      CONTROL_TASK_QUEUE_DEPTH, FWK_QUEUE_ALIGNMENT);

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void control_task_thread_internal(void *, void *, void *);

static FwkMsgHandler_t software_reset_msg_handler;
static FwkMsgHandler_t gateway_fsm_tick_handler;
static FwkMsgHandler_t attr_broadcast_msg_handler;
static FwkMsgHandler_t factory_reset_msg_handler;
static FwkMsgHandler_t cloud_state_msg_handler;
static FwkMsgHandler_t fota_msg_handler;
static FwkMsgHandler_t default_msg_handler;

#ifdef CONFIG_LWM2M
static FwkMsgHandler_t lwm2m_msg_handler;
#endif

static void commission_handler(void);
static void random_join_handler(attr_index_t idx);
static void update_apn_handler(void);
static void update_modem_log_level_handler(void);
static void update_rat_handler(void);

/******************************************************************************/
/* Framework Message Dispatcher                                               */
/******************************************************************************/
static FwkMsgHandler_t *control_task_msg_dispatcher(FwkMsgCode_t MsgCode)
{
	/* clang-format off */
	switch (MsgCode) {
	case FMC_INVALID:                    return Framework_UnknownMsgHandler;
	case FMC_PERIODIC:                   return gateway_fsm_tick_handler;
	case FMC_SOFTWARE_RESET:             return software_reset_msg_handler;
	case FMC_ATTR_CHANGED:               return attr_broadcast_msg_handler;
	case FMC_FACTORY_RESET:              return factory_reset_msg_handler;
	case FMC_CLOUD_CONNECTED:            return cloud_state_msg_handler;
	case FMC_CLOUD_DISCONNECTED:         return cloud_state_msg_handler;
	case FMC_FOTA_START:                 return fota_msg_handler;
	case FMC_FOTA_DONE:                  return fota_msg_handler;
	default:                             return default_msg_handler;
	}
	/* clang-format on */
}

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void control_task_initialize(void)
{
	cto.msgTask.rxer.id = FWK_ID_CONTROL_TASK;
	cto.msgTask.rxer.rxBlockTicks = K_FOREVER;
	cto.msgTask.rxer.pMsgDispatcher = control_task_msg_dispatcher;
	cto.msgTask.timerDurationTicks = K_SECONDS(1);
	cto.msgTask.timerPeriodTicks = K_MSEC(0);
	cto.msgTask.rxer.pQueue = &control_task_queue;

	Framework_RegisterTask(&cto.msgTask);

	/* control task == main thread */
	cto.msgTask.pTid = k_current_get();

	k_thread_name_set(cto.msgTask.pTid, THIS_FILE);
}

void control_task_thread(void)
{
	control_task_thread_internal(&cto, NULL, NULL);
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void control_task_thread_internal(void *pArg1, void *pArg2, void *pArg3)
{
	control_task_obj_t *pObj = (control_task_obj_t *)pArg1;

	attr_prepare_networkJoinDelayed();
	random_join_handler(ATTR_ID_joinDelay);
	update_modem_log_level_handler();

	configure_app();
	gateway_fsm_init();

	Framework_StartTimer(&pObj->msgTask);

	while (true) {
		Framework_MsgReceiver(&pObj->msgTask.rxer);
	}
}

static DispatchResult_t gateway_fsm_tick_handler(FwkMsgReceiver_t *pMsgRxer,
						 FwkMsg_t *pMsg)
{
	ARG_UNUSED(pMsg);
	control_task_obj_t *pObj = FWK_TASK_CONTAINER(control_task_obj_t);

	gateway_fsm();

#ifdef CONFIG_BLUEGRASS
	bluegrass_subscription_handler();
#endif

#ifdef CONFIG_MODEM_HL7800
	if (!pObj->cloud_connected) {
		fota_smp_start_handler();
	}
#endif

	Framework_StartTimer(&pObj->msgTask);
	return DISPATCH_OK;
}

static DispatchResult_t attr_broadcast_msg_handler(FwkMsgReceiver_t *pMsgRxer,
						   FwkMsg_t *pMsg)
{
	control_task_obj_t *pObj = FWK_TASK_CONTAINER(control_task_obj_t);
	attr_changed_msg_t *pb = (attr_changed_msg_t *)pMsg;
	size_t i;
	bool update_commission = false;
	bool update_apn = false;
	bool update_rat = false;

	pObj->broadcast_count += 1;

	for (i = 0; i < pb->count; i++) {
		switch (pb->list[i]) {
		case ATTR_ID_commissioned:
		case ATTR_ID_endpoint:
		case ATTR_ID_port:
			update_commission = true;
			break;

		case ATTR_ID_topicPrefix:
#ifdef CONFIG_CONTACT_TRACING
			ct_ble_topic_builder();
#endif
			break;

		case ATTR_ID_joinDelay:
			random_join_handler(pb->list[i]);
			break;

		case ATTR_ID_apnControlPoint:
			/* Flag prevents ordering issues when processing a file. */
			update_apn = true;
			break;

		case ATTR_ID_modemDesiredLogLevel:
			update_modem_log_level_handler();
			break;

		case ATTR_ID_lteRat:
			update_rat = true;
			break;

		case ATTR_ID_fotaControlPoint:
#ifdef CONFIG_MODEM_HL7800
			fota_smp_cmd_handler();
#else
			attr_set_uint32(ATTR_ID_fotaStatus, FOTA_STATUS_ERROR);
#endif
			break;

#ifdef CONFIG_BOARD_MG100
		case ATTR_ID_motionOdr:
			lcz_motion_update_odr();
			break;

		case ATTR_ID_motionThresh:
			lcz_motion_update_scale();
			break;

		case ATTR_ID_motionScale:
			lcz_motion_update_threshold();
			break;

		case ATTR_ID_motionDuration:
			lcz_motion_update_duration();
			break;
#endif

#ifdef CONFIG_LWM2M
		case ATTR_ID_generatePsk:
			if (attr_get_uint32(ATTR_ID_generatePsk, 0) != 0) {
				lwm2m_generate_psk();
			}
			break;
#endif

		default:
			/* Don't care about this attribute. This is a broadcast. */
			break;
		}
	}

	if (update_commission) {
		commission_handler();
	}

	if (update_apn) {
		update_apn_handler();
	}

	if (update_rat) {
		update_rat_handler();
	}

	return DISPATCH_OK;
}

static DispatchResult_t factory_reset_msg_handler(FwkMsgReceiver_t *pMsgRxer,
						  FwkMsg_t *pMsg)
{
	ARG_UNUSED(pMsgRxer);
	ARG_UNUSED(pMsg);

	LOG_WRN("Factory Reset");

	gateway_fsm_request_decommission();

	/* clear certs (doesn't handle multiple sets of certs) */
	fs_unlink((char *)attr_get_quasi_static(ATTR_ID_rootCaName));
	fs_unlink((char *)attr_get_quasi_static(ATTR_ID_clientCertName));
	fs_unlink((char *)attr_get_quasi_static(ATTR_ID_clientKeyName));

	attr_factory_reset();

	/* Resetting the system ensures read-only attributes have correct value. */
	FRAMEWORK_MSG_CREATE_AND_SEND(FWK_ID_CONTROL_TASK, FWK_ID_CONTROL_TASK,
				      FMC_SOFTWARE_RESET);

	return DISPATCH_OK;
}

static DispatchResult_t software_reset_msg_handler(FwkMsgReceiver_t *pMsgRxer,
						   FwkMsg_t *pMsg)
{
	ARG_UNUSED(pMsgRxer);
	ARG_UNUSED(pMsg);

	log_panic();
	lcz_software_reset(0);
	return DISPATCH_OK;
}

static DispatchResult_t fota_msg_handler(FwkMsgReceiver_t *pMsgRxer,
					 FwkMsg_t *pMsg)
{
	ARG_UNUSED(pMsgRxer);

#if defined(CONFIG_BLUEGRASS) &&                                               \
	(defined(CONFIG_COAP_FOTA) || defined(CONFIG_HTTP_FOTA))

	control_task_obj_t *pObj = FWK_TASK_CONTAINER(control_task_obj_t);
	FwkId_t fota_task_id;

	/* This logic won't work if both are enabled */
	if (pMsg->header.msgCode == FMC_FOTA_START) {
		pObj->fota_request = true;

#ifdef CONFIG_COAP_FOTA
		fota_task_id = FWK_ID_COAP_FOTA_TASK;
#elif CONFIG_HTTP_FOTA
		fota_task_id = FWK_ID_HTTP_FOTA_TASK;
#endif

		FRAMEWORK_MSG_CREATE_AND_SEND(FWK_ID_RESERVED, fota_task_id,
					      FMC_FOTA_START_ACK);

	} else {
		pObj->fota_request = false;
	}
#endif

	return DISPATCH_OK;
}

static DispatchResult_t cloud_state_msg_handler(FwkMsgReceiver_t *pMsgRxer,
						FwkMsg_t *pMsg)
{
	ARG_UNUSED(pMsgRxer);

#ifdef CONFIG_BLUEGRASS
	control_task_obj_t *pObj = FWK_TASK_CONTAINER(control_task_obj_t);

	if (pMsg->header.msgCode == FMC_CLOUD_CONNECTED) {
		bluegrass_connected_callback();
		pObj->cloud_connected = true;
	} else {
		bluegrass_disconnected_callback();
		pObj->cloud_connected = false;
	}
#endif

	return DISPATCH_OK;
}

static DispatchResult_t default_msg_handler(FwkMsgReceiver_t *pMsgRxer,
					    FwkMsg_t *pMsg)
{
#ifdef CONFIG_BLUEGRASS
	gateway_fsm();
	return bluegrass_msg_handler(pMsgRxer, pMsg);
#elif defined(CONFIG_LWM2M)
	return lwm2m_msg_handler(pMsgRxer, pMsg);
#else
	ARG_UNUSED(pMsgRxer);
	ARG_UNUSED(pMsg);
	return DISPATCH_OK;
#endif
}

static void commission_handler(void)
{
	attr_set_signed32(ATTR_ID_certStatus, CERT_STATUS_BUSY);
	attr_set_uint32(ATTR_ID_commissioningBusy, true);

	/* If the value is written, then always decommission so that the connection
	 * is closed and the certs are unloaded.  The files aren't deleted.
	 * If commission is true, then the state machine will load the certs
	 * from the file system after the join cloud delay has expired.
	 */
	gateway_fsm_request_decommission();
}

static void random_join_handler(attr_index_t idx)
{
	/* delay, min, max, and interval must be consecutive */
	uint32_t delay = attr_get_uint32(idx, 1);
	uint32_t min = attr_get_uint32(idx + 1, 0);
	uint32_t max = attr_get_uint32(idx + 2, 0);
	uint32_t interval = attr_get_uint32(idx + 3, 0);

	LOG_DBG("min: %u max: %u delay: %u", min, max, delay);

	if (delay == 0) {
		delay = RAND_RANGE(min, max);
		delay *= interval;
		LOG_DBG("base * interval: %u * %u", delay, interval);
		attr_set_uint32(idx, delay);
		/* The set will cause a broadcast which will run this again (if 0). */
	}
}

/* Currently only the name is writable. */
static void update_apn_handler(void)
{
#ifdef CONFIG_MODEM_HL7800
	int32_t status = mdm_hl7800_update_apn(
		(char *)attr_get_quasi_static(ATTR_ID_apn));
	attr_set_signed32(ATTR_ID_apnStatus, status);
#else
	attr_set_signed32(ATTR_ID_apnStatus, -EPERM);
#endif
}

static void update_modem_log_level_handler(void)
{
#ifdef CONFIG_MODEM_HL7800
	uint32_t desired =
		attr_get_uint32(ATTR_ID_modemDesiredLogLevel, LOG_LEVEL_DBG);
	uint32_t new_level = mdm_hl7800_log_filter_set(desired);
	LOG_INF("modem log level: desired: %u new_level: %u", desired,
		new_level);
#endif
}

static void update_rat_handler(void)
{
#ifdef CONFIG_MODEM_HL7800
	mdm_hl7800_update_rat(attr_get_uint32(ATTR_ID_lteRat, MDM_RAT_CAT_M1));
#endif
}

#ifdef CONFIG_LWM2M
static DispatchResult_t lwm2m_msg_handler(FwkMsgReceiver_t *pMsgRxer,
					  FwkMsg_t *pMsg)
{
	ARG_UNUSED(pMsgRxer);

	switch (pMsg->header.msgCode) {
	case FMC_BL654_SENSOR_EVENT: {
		BL654SensorMsg_t *pBmeMsg = (BL654SensorMsg_t *)pMsg;
		if (lwm2m_set_bl654_sensor_data(pBmeMsg->temperatureC,
						pBmeMsg->humidityPercent,
						pBmeMsg->pressurePa) != 0) {
			LOG_ERR("Error setting BL654 Sensor Data in LWM2M server");
		}
	} break;

	default:
		break;
	}

	return DISPATCH_OK;
}
#endif

/******************************************************************************/
/* Weak overrides                                                             */
/******************************************************************************/

bool gateway_fsm_fota_request(void)
{
#ifdef CONFIG_MODEM_HL7800
	return cto.fota_request || fota_smp_modem_busy();
#else
	return cto.fota_request;
#endif
}
