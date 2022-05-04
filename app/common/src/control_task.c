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
#endif

#include "fota_smp.h"
#include "FrameworkIncludes.h"
#include "lcz_software_reset.h"
#include "attr.h"
#include "lte.h"
#include "gateway_common.h"
#include "gateway_fsm.h"
#include "rand_range.h"
#include "led_configuration.h"

#ifdef CONFIG_CONTACT_TRACING
#include "ct_ble.h"
#endif

#ifdef CONFIG_LCZ_MOTION
#include "lcz_motion.h"
#endif

#ifdef CONFIG_LWM2M
#include "lcz_lwm2m_client.h"
#endif

#ifdef CONFIG_DISPLAY
#include "lcd.h"
#endif

#include "cloud.h"
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
BUILD_ASSERT(ATTR_ID_join_delay + 1 == ATTR_ID_join_min, MSG);
BUILD_ASSERT(ATTR_ID_join_min + 1 == ATTR_ID_join_max, MSG);
BUILD_ASSERT(ATTR_ID_join_max + 1 == ATTR_ID_join_interval, MSG);

#if defined(CONFIG_COAP_FOTA) && defined(CONFIG_HTTP_FOTA)
#error "Dual network FOTA not supported"
#elif defined(CONFIG_COAP_FOTA) || defined(CONFIG_HTTP_FOTA)
#define FOTA_ENABLED 1
#endif

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static control_task_obj_t cto;

K_MSGQ_DEFINE(control_task_queue, FWK_QUEUE_ENTRY_SIZE,
	      CONTROL_TASK_QUEUE_DEPTH, FWK_QUEUE_ALIGNMENT);

static struct gateway_fsm_user gw_fsm_user;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void control_task_thread_internal(void *, void *, void *);

static FwkMsgHandler_t software_reset_msg_handler;
static FwkMsgHandler_t gateway_fsm_tick_handler;
static FwkMsgHandler_t attr_broadcast_msg_handler;
static FwkMsgHandler_t factory_reset_msg_handler;
static FwkMsgHandler_t cloud_state_msg_handler;
static FwkMsgHandler_t dispatch_to_sub_task;

#ifdef FOTA_ENABLED
static FwkMsgHandler_t fota_msg_handler;
#endif

static void random_join_handler(attr_index_t idx);

#ifdef CONFIG_MODEM_HL7800
static void update_apn_handler(void);
static void update_modem_log_level_handler(void);
static void update_rat_handler(void);
static void update_gps_rate_handler(void);
#ifdef ATTR_ID_polte_control_point
static void polte_cmd_handler(void);
#endif
#endif

static bool gateway_fsm_fota_request(void);

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
#ifdef FOTA_ENABLED
	case FMC_FOTA_START_REQ:             return fota_msg_handler;
	case FMC_FOTA_DONE:                  return fota_msg_handler;
#endif
	default:                             return cloud_sub_task_msg_dispatcher(MsgCode);
	}
	/* clang-format on */
}

__weak FwkMsgHandler_t *cloud_sub_task_msg_dispatcher(FwkMsgCode_t MsgCode)
{
	ARG_UNUSED(MsgCode);

	return NULL;
}

__weak void cloud_init_shadow_request(void)
{
	return;
}

__weak int cloud_commission(void)
{
	return 0;
}

__weak int cloud_decommission(void)
{
	return 0;
}

__weak int cloud_commission_handler(void)
{
	return 0;
}

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void control_task_initialize(void)
{
	cto.msgTask.rxer.id = FWK_ID_CLOUD;
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

	random_join_handler(ATTR_ID_join_delay);

#ifdef CONFIG_MODEM_HL7800
	attr_prepare_modem_boot();
	update_modem_log_level_handler();
	update_gps_rate_handler();
#endif

	configure_app();
	gateway_fsm_init();

	gw_fsm_user.cloud_disable = gateway_fsm_fota_request;
	gatway_fsm_register_user(&gw_fsm_user);

	Framework_StartTimer(&pObj->msgTask);

	while (true) {
		Framework_MsgReceiver(&pObj->msgTask.rxer);
	}
}

static DispatchResult_t gateway_fsm_tick_handler(FwkMsgReceiver_t *pMsgRxer,
						 FwkMsg_t *pMsg)
{
	control_task_obj_t *pObj = FWK_TASK_CONTAINER(control_task_obj_t);

	gateway_fsm();

	if (IS_ENABLED(CONFIG_MODEM_HL7800_FW_UPDATE) &&
	    IS_ENABLED(CONFIG_FOTA_SMP)) {
		if (!pObj->cloud_connected) {
			fota_smp_start_handler();
		}
	}

	Framework_StartTimer(&pObj->msgTask);
	return dispatch_to_sub_task(pMsgRxer, pMsg);
}

static DispatchResult_t attr_broadcast_msg_handler(FwkMsgReceiver_t *pMsgRxer,
						   FwkMsg_t *pMsg)
{
	control_task_obj_t *pObj = FWK_TASK_CONTAINER(control_task_obj_t);
	attr_changed_msg_t *pb = (attr_changed_msg_t *)pMsg;
	size_t i;
	bool update_commission = false;
#ifdef CONFIG_MODEM_HL7800
	bool update_apn = false;
	bool update_rat = false;
#endif
#ifdef CONFIG_DISPLAY
	bool update_display = false;
#endif

	pObj->broadcast_count += 1;

	for (i = 0; i < pb->count; i++) {
		switch (pb->list[i]) {
		case ATTR_ID_commissioned:
		case ATTR_ID_endpoint:
		case ATTR_ID_port:
			update_commission = true;
			break;

#ifdef CONFIG_CONTACT_TRACING
		case ATTR_ID_topic_prefix:
			ct_ble_topic_builder();
			break;
#endif

		case ATTR_ID_join_delay:
			random_join_handler(pb->list[i]);
			break;

#ifdef CONFIG_MODEM_HL7800
		case ATTR_ID_apn_control_point:
			/* Flag prevents ordering issues when processing a file. */
			update_apn = true;
			break;

		case ATTR_ID_modem_desired_log_level:
			update_modem_log_level_handler();
			break;

		case ATTR_ID_lte_rat:
			update_rat = true;
			break;

		case ATTR_ID_gps_rate:
			update_gps_rate_handler();
			break;

#ifdef ATTR_ID_polte_control_point
		case ATTR_ID_polte_control_point:
			polte_cmd_handler();
			break;
#endif

#endif

#ifdef ATTR_ID_fota_control_point
		case ATTR_ID_fota_control_point:
			if (IS_ENABLED(CONFIG_FOTA_SMP)) {
				fota_smp_cmd_handler();
			}
			break;
#endif

#ifdef CONFIG_LCZ_MOTION
		case ATTR_ID_motion_odr:
			lcz_motion_update_odr();
			break;

		case ATTR_ID_motion_thresh:
			lcz_motion_update_scale();
			break;

		case ATTR_ID_motion_scale:
			lcz_motion_update_threshold();
			break;

		case ATTR_ID_motion_duration:
			lcz_motion_update_duration();
			break;
#endif

#ifdef CONFIG_LWM2M
		case ATTR_ID_generate_psk:
			lwm2m_generate_psk();
			break;
#endif

#ifdef CONFIG_DISPLAY
		case ATTR_ID_passkey:
			update_display = true;
			break;
#endif


		default:
			/* Don't care about this attribute. This is a broadcast. */
			break;
		}
	}

	if (update_commission) {
		cloud_commission_handler();
	}

#ifdef CONFIG_MODEM_HL7800
	if (update_apn) {
		update_apn_handler();
	}

	/* Do this last because radio will reset. */
	if (update_rat) {
		update_rat_handler();
	}
#endif

#ifdef CONFIG_DISPLAY
	if (update_display) {
		lcd_display_update_details();
	}
#endif

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
	fs_unlink((char *)attr_get_quasi_static(ATTR_ID_root_ca_name));
	fs_unlink((char *)attr_get_quasi_static(ATTR_ID_client_cert_name));
	fs_unlink((char *)attr_get_quasi_static(ATTR_ID_client_key_name));

	attr_factory_reset();

	/* Resetting the system ensures read-only attributes have correct value. */
	FRAMEWORK_MSG_CREATE_AND_SEND(FWK_ID_CLOUD, FWK_ID_CLOUD,
				      FMC_SOFTWARE_RESET);

	return DISPATCH_OK;
}

static DispatchResult_t software_reset_msg_handler(FwkMsgReceiver_t *pMsgRxer,
						   FwkMsg_t *pMsg)
{
	ARG_UNUSED(pMsgRxer);
	ARG_UNUSED(pMsg);

	LOG_PANIC();
	lcz_software_reset(0);
	return DISPATCH_OK;
}

#ifdef FOTA_ENABLED
static DispatchResult_t fota_msg_handler(FwkMsgReceiver_t *pMsgRxer,
					 FwkMsg_t *pMsg)
{
	ARG_UNUSED(pMsgRxer);
	control_task_obj_t *pObj = FWK_TASK_CONTAINER(control_task_obj_t);

	if (pMsg->header.msgCode == FMC_FOTA_START_REQ) {
		pObj->fota_request = true;
		FRAMEWORK_MSG_CREATE_AND_BROADCAST(FWK_ID_CLOUD,
						   FMC_FOTA_START_ACK);
	} else {
		pObj->fota_request = false;
	}

	return DISPATCH_OK;
}
#endif

static DispatchResult_t cloud_state_msg_handler(FwkMsgReceiver_t *pMsgRxer,
						FwkMsg_t *pMsg)
{
	control_task_obj_t *pObj = FWK_TASK_CONTAINER(control_task_obj_t);

	if (pMsg->header.msgCode == FMC_CLOUD_CONNECTED) {
		pObj->cloud_connected = true;
		lcz_led_turn_on(CLOUD_LED);
	} else {
		pObj->cloud_connected = false;
		lcz_led_turn_off(CLOUD_LED);
	}

	return dispatch_to_sub_task(pMsgRxer, pMsg);
}

/* Determine if sub task wants to process this message */
static DispatchResult_t dispatch_to_sub_task(FwkMsgReceiver_t *pMsgRxer,
						FwkMsg_t *pMsg)
{
	FwkMsgHandler_t *handler =
		cloud_sub_task_msg_dispatcher(pMsg->header.msgCode);

	if (handler) {
		return handler(pMsgRxer, pMsg);
	} else {
		return DISPATCH_OK;
	}
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

#ifdef CONFIG_MODEM_HL7800
/* Currently only the name is writable. */
static void update_apn_handler(void)
{
	int32_t status = mdm_hl7800_update_apn(
		(char *)attr_get_quasi_static(ATTR_ID_apn));
	attr_set_signed32(ATTR_ID_apn_status, status);
}

static void update_modem_log_level_handler(void)
{
	uint32_t desired =
		attr_get_uint32(ATTR_ID_modem_desired_log_level, LOG_LEVEL_DBG);
	uint32_t new_level = mdm_hl7800_log_filter_set(desired);
	LOG_INF("modem log level: desired: %u new_level: %u", desired,
		new_level);
}

static void update_gps_rate_handler(void)
{
	if (IS_ENABLED(CONFIG_MODEM_HL7800_GPS)) {
		attr_set_signed32(ATTR_ID_gps_status,
				  mdm_hl7800_set_gps_rate(
					  attr_get_uint32(ATTR_ID_gps_rate, 0)));
	} else {
		attr_set_signed32(ATTR_ID_gps_status, -EPERM);
		LOG_INF("GPS not enabled");
	}
}

#ifdef ATTR_ID_polte_control_point
static void polte_cmd_handler(void)
{
	uint8_t cmd = attr_get_uint32(ATTR_ID_polte_control_point, 0);
	int32_t status = -EPERM;

	if (IS_ENABLED(CONFIG_MODEM_HL7800_POLTE)) {
		attr_set_signed32(ATTR_ID_polte_status, POLTE_STATUS_BUSY);

		switch (cmd) {
		case POLTE_CONTROL_POINT_REGISTER:
			status = mdm_hl7800_polte_register();
			break;

		case POLTE_CONTROL_POINT_ENABLE:
			status = mdm_hl7800_polte_enable(
				attr_get_quasi_static(ATTR_ID_polte_user),
				attr_get_quasi_static(ATTR_ID_polte_password));
			break;

		case POLTE_CONTROL_POINT_LOCATE:
			status = mdm_hl7800_polte_locate();
			break;
		}
	}

	/* If command was issued without an error,
	 * wait for second response from modem.
	 */
	if (status < 0 || cmd == POLTE_CONTROL_POINT_ENABLE) {
		attr_set_signed32(ATTR_ID_polte_status, status);
	}

	LOG_DBG("PoLTE command status %d", status);
}
#endif

static void update_rat_handler(void)
{
	mdm_hl7800_update_rat(attr_get_uint32(ATTR_ID_lte_rat, MDM_RAT_CAT_M1));
}
#endif

#ifdef CONFIG_LWM2M
static DispatchResult_t lwm2m_msg_handler(FwkMsgReceiver_t *pMsgRxer,
					  FwkMsg_t *pMsg)
{
	ARG_UNUSED(pMsgRxer);
	ESSSensorMsg_t *pBmeMsg = (ESSSensorMsg_t *)pMsg;

	if (lwm2m_set_ess_sensor_data(pBmeMsg->temperatureC,
				      pBmeMsg->humidityPercent,
				      pBmeMsg->pressurePa) != 0) {
		LOG_ERR("Error setting ESS Sensor Data in LWM2M server");
	}

	return DISPATCH_OK;
}

FwkMsgHandler_t *cloud_sub_task_msg_dispatcher(FwkMsgCode_t MsgCode)
{
	/* clang-format off */
	switch (MsgCode) {
	case FMC_ESS_SENSOR_EVENT:  return lwm2m_msg_handler;
	default:                    return NULL;
	}
	/* clang-format on */
}
#endif

static bool gateway_fsm_fota_request(void)
{
	return cto.fota_request;
}
