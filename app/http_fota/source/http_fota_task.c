/**
 * @file http_fota_task.c
 * @brief State machine for HTTP FOTA
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(http_fota, CONFIG_HTTP_FOTA_TASK_LOG_LEVEL);
#define FWK_FNAME "http_fota"

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <net/tls_credentials.h>
#include <sys/reboot.h>
#include <net/fota_download.h>

#ifdef CONFIG_MODEM_HL7800
#include <drivers/modem/hl7800.h>
#include "hl7800_http_fota.h"
#endif

#include "lcz_memfault.h"
#include "FrameworkIncludes.h"
#include "laird_utility_macros.h"
#include "file_system_utilities.h"
#include "http_fota_shadow.h"
#include "http_fota_task.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/

#define HTTP_FOTA_TASK_PRIORITY K_PRIO_PREEMPT(CONFIG_HTTP_FOTA_TASK_PRIO)

#define HTTP_FOTA_TICK_RATE K_SECONDS(1)
#define TIMER_PERIOD_ONE_SHOT K_SECONDS(0)

#define TLS_SEC_TAG 143

static K_SEM_DEFINE(wait_fota_download, 0, 1);

typedef struct http_fota_task {
	FwkMsgTask_t msgTask;
	bool fs_mounted;
	bool network_connected;
	bool aws_connected;
	bool bluegrass_ready;
	bool shadow_update;
	bool allow_start;
	uint32_t delay_timer;
	fota_context_t app_context;
#ifdef CONFIG_MODEM_HL7800
	fota_context_t modem_context;
#endif
} http_fota_task_obj_t;

K_MSGQ_DEFINE(http_fota_task_queue, FWK_QUEUE_ENTRY_SIZE,
	      CONFIG_HTTP_FOTA_TASK_QUEUE_DEPTH, FWK_QUEUE_ALIGNMENT);

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static http_fota_task_obj_t tctx;

static K_THREAD_STACK_DEFINE(http_fota_task_stack,
			     CONFIG_HTTP_FOTA_TASK_STACK_SIZE);

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void http_fota_task_thread(void *pArg1, void *pArg2, void *pArg3);
static int tls_init(void);

static FwkMsgHandler_t http_start_ack_msg_handler;
static FwkMsgHandler_t connection_msg_handler;
static FwkMsgHandler_t http_fota_tick_msg_handler;

static char *fota_state_get_string(fota_fsm_state_t state);
static char *fota_image_type_get_string(enum fota_image_type type);
static void fota_fsm(fota_context_t *pCtx);
static bool transport_not_required(void);
void fota_download_handler(const struct fota_download_evt *evt);
static int initiate_update(fota_context_t *pCtx);

/******************************************************************************/
/* Framework Message Dispatcher                                               */
/******************************************************************************/
static FwkMsgHandler_t *http_fota_taskMsgDispatcher(FwkMsgCode_t MsgCode)
{
	/* clang-format off */
	switch (MsgCode) {
	case FMC_INVALID:                return Framework_UnknownMsgHandler;
	case FMC_PERIODIC:               return http_fota_tick_msg_handler;
	case FMC_NETWORK_CONNECTED:      return connection_msg_handler;
	case FMC_NETWORK_DISCONNECTED:   return connection_msg_handler;
	case FMC_CLOUD_CONNECTED:        return connection_msg_handler;
	case FMC_CLOUD_DISCONNECTED:     return connection_msg_handler;
	case FMC_CLOUD_READY:            return connection_msg_handler;
	case FMC_FOTA_START_ACK:         return http_start_ack_msg_handler;
	default:                         return NULL;
	}
	/* clang-format on */
}

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
int http_fota_task_initialize(void)
{
	memset(&tctx, 0, sizeof(http_fota_task_obj_t));

	tctx.msgTask.rxer.id = FWK_ID_HTTP_FOTA_TASK;
	tctx.msgTask.rxer.pQueue = &http_fota_task_queue;
	tctx.msgTask.rxer.rxBlockTicks = K_FOREVER;
	tctx.msgTask.rxer.pMsgDispatcher = http_fota_taskMsgDispatcher;
	tctx.msgTask.timerDurationTicks = HTTP_FOTA_TICK_RATE;
	tctx.msgTask.timerPeriodTicks = TIMER_PERIOD_ONE_SHOT;
	Framework_RegisterTask(&tctx.msgTask);

	tctx.msgTask.pTid =
		k_thread_create(&tctx.msgTask.threadData, http_fota_task_stack,
				K_THREAD_STACK_SIZEOF(http_fota_task_stack),
				http_fota_task_thread, &tctx, NULL, NULL,
				HTTP_FOTA_TASK_PRIORITY, 0, K_NO_WAIT);

	k_thread_name_set(tctx.msgTask.pTid, FWK_FNAME);

	tctx.app_context.type = APP_IMAGE_TYPE;
#ifdef CONFIG_MODEM_HL7800
	tctx.modem_context.type = MODEM_IMAGE_TYPE;
	tctx.modem_context.wait_download = &wait_fota_download;
#endif
	tctx.app_context.wait_download = &wait_fota_download;

	return 0;
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void http_fota_task_thread(void *pArg1, void *pArg2, void *pArg3)
{
	ARG_UNUSED(pArg2);
	ARG_UNUSED(pArg3);
	http_fota_task_obj_t *pObj = (http_fota_task_obj_t *)pArg1;
	int rc;

	if (fsu_lfs_mount() == 0) {
		pObj->fs_mounted = true;
	}

	tls_init();

	http_fota_shadow_init();
#ifdef CONFIG_MODEM_HL7800
	http_fota_modem_shadow_init(CONFIG_FSU_MOUNT_POINT);
#endif

	rc = fota_download_init(fota_download_handler);
	if (rc != 0) {
		LOG_ERR("Could not init APP FOTA download %d", rc);
	}

#ifdef CONFIG_MODEM_HL7800
	rc = hl7800_download_client_init(fota_download_handler);
	if (rc != 0) {
		LOG_ERR("Could not init HL7800 FOTA download %d", rc);
	}
#endif

	Framework_StartTimer(&pObj->msgTask);

	while (true) {
		Framework_MsgReceiver(&pObj->msgTask.rxer);
	}
}

static int tls_init(void)
{
	int err = -EINVAL;

	/* clang-format off */
	static const char cert[] = {
		#include "../include/fota_root_ca"
	};
	/* clang-format on */

	tls_credential_delete(TLS_SEC_TAG, TLS_CREDENTIAL_CA_CERTIFICATE);
	err = tls_credential_add(TLS_SEC_TAG, TLS_CREDENTIAL_CA_CERTIFICATE,
				 cert, sizeof(cert));
	if (err < 0) {
		LOG_ERR("Failed to register root CA: %d", err);
		return err;
	}

	return err;
}

static DispatchResult_t http_start_ack_msg_handler(FwkMsgReceiver_t *pMsgRxer,
						   FwkMsg_t *pMsg)
{
	http_fota_task_obj_t *pObj = FWK_TASK_CONTAINER(http_fota_task_obj_t);

	pObj->allow_start = true;
	return DISPATCH_OK;
}

/* AWS must be connected to get shadow information.
 * AWS must be disconnected to run HTTP FOTA because there isn't enough
 * memory to support two simultaneous connections.
 */
static DispatchResult_t connection_msg_handler(FwkMsgReceiver_t *pMsgRxer,
					       FwkMsg_t *pMsg)
{
	http_fota_task_obj_t *pObj = FWK_TASK_CONTAINER(http_fota_task_obj_t);

	switch (pMsg->header.msgCode) {
	case FMC_NETWORK_CONNECTED:
		pObj->network_connected = true;
		break;

	case FMC_NETWORK_DISCONNECTED:
		pObj->network_connected = false;
		break;

	case FMC_CLOUD_READY:
		pObj->aws_connected = true;
		pObj->bluegrass_ready = true;
		http_fota_enable_shadow_generation();
		break;

	case FMC_CLOUD_CONNECTED:
		pObj->aws_connected = true;
		break;

	case FMC_CLOUD_DISCONNECTED:
		pObj->aws_connected = false;
		pObj->bluegrass_ready = false;
		http_fota_disable_shadow_generation();
		break;
	}

	return DISPATCH_OK;
}

static DispatchResult_t http_fota_tick_msg_handler(FwkMsgReceiver_t *pMsgRxer,
						   FwkMsg_t *pMsg)
{
	UNUSED_PARAMETER(pMsg);
	http_fota_task_obj_t *pObj = FWK_TASK_CONTAINER(http_fota_task_obj_t);

	if (pObj->bluegrass_ready) {
		pObj->shadow_update = http_fota_shadow_update_handler();
	} else {
		pObj->shadow_update = false;
	}

	/* Allow possible delta shadow changes to be processed before starting.
	 * Allow FOTA shadow updates to be sent before disconnecting from AWS.
	 */

	if (pObj->network_connected && pObj->bluegrass_ready &&
	    !pObj->shadow_update) {
		if (pObj->delay_timer < CONFIG_HTTP_FOTA_START_DELAY) {
			pObj->delay_timer += 1;
		}
	} else {
		pObj->delay_timer = 0;
	}

	if (pObj->fs_mounted) {
		fota_fsm(&pObj->app_context);
#ifdef CONFIG_MODEM_HL7800
		fota_fsm(&pObj->modem_context);
#endif
	}

	Framework_StartTimer(&pObj->msgTask);

	return DISPATCH_OK;
}

static char *fota_state_get_string(fota_fsm_state_t state)
{
	/* clang-format off */
	switch (state) {
		PREFIXED_SWITCH_CASE_RETURN_STRING(FOTA_FSM, ABORT);
		PREFIXED_SWITCH_CASE_RETURN_STRING(FOTA_FSM, ERROR);
		PREFIXED_SWITCH_CASE_RETURN_STRING(FOTA_FSM, IDLE);
		PREFIXED_SWITCH_CASE_RETURN_STRING(FOTA_FSM, END);
		PREFIXED_SWITCH_CASE_RETURN_STRING(FOTA_FSM, SUCCESS);
		PREFIXED_SWITCH_CASE_RETURN_STRING(FOTA_FSM, MODEM_WAIT);
		PREFIXED_SWITCH_CASE_RETURN_STRING(FOTA_FSM, WAIT);
		PREFIXED_SWITCH_CASE_RETURN_STRING(FOTA_FSM, START);
		PREFIXED_SWITCH_CASE_RETURN_STRING(FOTA_FSM, START_DOWNLOAD);
		PREFIXED_SWITCH_CASE_RETURN_STRING(FOTA_FSM, WAIT_FOR_DOWNLOAD_COMPLETE);
		PREFIXED_SWITCH_CASE_RETURN_STRING(FOTA_FSM, DELETE_EXISTING_FILE);
		PREFIXED_SWITCH_CASE_RETURN_STRING(FOTA_FSM, WAIT_FOR_SWITCHOVER);
		PREFIXED_SWITCH_CASE_RETURN_STRING(FOTA_FSM, INITIATE_UPDATE);
		default:
			return "UNKNOWN";
	}
	/* clang-format on */
}

static char *fota_image_type_get_string(enum fota_image_type type)
{
	switch (type) {
	case APP_IMAGE_TYPE:
		return "APP";
#ifdef CONFIG_MODEM_HL7800
	case MODEM_IMAGE_TYPE:
		return "MODEM";
#endif
	default:
		return "UNKNOWN";
	}
}

static void fota_fsm(fota_context_t *pCtx)
{
	int r = 0;
	fota_fsm_state_t next_state = pCtx->state;

	switch (pCtx->state) {
	case FOTA_FSM_ERROR:
		pCtx->delay = CONFIG_HTTP_FOTA_ERROR_DELAY;
		http_fota_increment_error_count(pCtx->type);
		LCZ_MEMFAULT_COLLECT_LOGS();
		next_state = FOTA_FSM_END;
		break;

	case FOTA_FSM_ABORT:
		next_state = FOTA_FSM_END;
		break;

	case FOTA_FSM_SUCCESS:
#ifdef CONFIG_MODEM_HL7800
		if (pCtx->type == MODEM_IMAGE_TYPE) {
			LOG_WRN("Modem Updating");
			pCtx->delay = CONFIG_HTTP_FOTA_MODEM_INSTALL_DELAY;
			next_state = FOTA_FSM_MODEM_WAIT;
		} else {
#endif
			LOG_WRN("Entering mcuboot");
			LCZ_MEMFAULT_REBOOT_TRACK_FIRMWARE_UPDATE();
			/* Allow last print to occur. */
			k_sleep(K_MSEC(CONFIG_LOG_PROCESS_THREAD_SLEEP_MS));
			sys_reboot(SYS_REBOOT_COLD);
			next_state = FOTA_FSM_END; /* don't care */
#ifdef CONFIG_MODEM_HL7800
		}
#endif
		break;

#ifdef CONFIG_MODEM_HL7800
	case FOTA_FSM_MODEM_WAIT:
		/* The modem is going to reboot.  If the cloud fsm
		 * stays in its fota state, then its queue won't get overfilled
		 * by the app fsm requesting its turn (or by sensor data).
		 */
		if (http_fota_modem_install_complete()) {
			pCtx->delay = 0;
		}

		if (pCtx->delay > 0) {
			pCtx->delay -= 1;
			next_state = FOTA_FSM_MODEM_WAIT;
		} else {
			LOG_WRN("Rebooting to complete modem update");
			LCZ_MEMFAULT_REBOOT_TRACK_FIRMWARE_UPDATE();
			/* Allow last print to occur. */
			k_sleep(K_MSEC(CONFIG_LOG_PROCESS_THREAD_SLEEP_MS));
			sys_reboot(SYS_REBOOT_COLD);
			next_state = FOTA_FSM_END;
		}
		break;
#endif

	case FOTA_FSM_END:
		pCtx->using_transport = false;
		if (transport_not_required()) {
			FRAMEWORK_MSG_CREATE_AND_SEND(
				FWK_ID_RESERVED, FWK_ID_CLOUD, FMC_FOTA_DONE);
		}
		next_state = FOTA_FSM_WAIT;
		break;

	case FOTA_FSM_WAIT:
		/* Allow time for the shadow to be updated if there is an error. */
		if (pCtx->delay > 0) {
			pCtx->delay -= 1;
			next_state = FOTA_FSM_WAIT;
		} else {
			next_state = FOTA_FSM_IDLE;
		}
		break;

	case FOTA_FSM_IDLE:
		if (tctx.delay_timer >= CONFIG_HTTP_FOTA_START_DELAY) {
			if (http_fota_request(pCtx->type)) {
				FRAMEWORK_MSG_CREATE_AND_SEND(
					FWK_ID_RESERVED, FWK_ID_CLOUD,
					FMC_FOTA_START_REQ);
				next_state = FOTA_FSM_START;
			}
		}
		break;

	case FOTA_FSM_START:
		/* Ack is used to ensure AWS didn't disconnect for another reason. */
		if (tctx.allow_start && !tctx.aws_connected) {
			/* The FOTA state machine requires a connection until
			 * it gets to the switchover state.
			 */
			tctx.allow_start = false;
			pCtx->using_transport = true;
			pCtx->file_path =
				(char *)http_fota_get_fs_name(pCtx->type);
			pCtx->download_error = false;
			next_state = FOTA_FSM_START_DOWNLOAD;
		}
		break;

	case FOTA_FSM_START_DOWNLOAD:
		if (pCtx->type == APP_IMAGE_TYPE) {
			r = fota_download_start(
				http_fota_get_download_host(pCtx->type),
				http_fota_get_download_file(pCtx->type),
				TLS_SEC_TAG, 0, 0);
#ifdef CONFIG_MODEM_HL7800
		} else if (pCtx->type == MODEM_IMAGE_TYPE) {
			r = hl7800_download_start(
				pCtx, http_fota_get_download_host(pCtx->type),
				http_fota_get_download_file(pCtx->type),
				TLS_SEC_TAG, 0, 0);
#endif
		} else {
			r = -EINVAL;
		}
		if (r < 0) {
			next_state = FOTA_FSM_ERROR;
		} else {
			next_state = FOTA_FSM_WAIT_FOR_DOWNLOAD_COMPLETE;
		}
		break;

	case FOTA_FSM_WAIT_FOR_DOWNLOAD_COMPLETE:
		LOG_DBG("Wait for download");
		r = k_sem_take(pCtx->wait_download, K_FOREVER);

		if ((pCtx->download_error) || (r < 0)) {
			next_state = FOTA_FSM_ERROR;
		} else {
			next_state = FOTA_FSM_WAIT_FOR_SWITCHOVER;
		}
		break;

	case FOTA_FSM_WAIT_FOR_SWITCHOVER:
		if (http_fota_ready(pCtx->type)) {
			next_state = FOTA_FSM_INITIATE_UPDATE;
		} else if (http_fota_abort(pCtx->type)) {
			next_state = FOTA_FSM_ABORT;
		} else if (pCtx->using_transport) {
			pCtx->using_transport = false;
			if (transport_not_required()) {
				LOG_DBG("Transport not required");
				FRAMEWORK_MSG_CREATE_AND_SEND(FWK_ID_RESERVED,
							      FWK_ID_CLOUD,
							      FMC_FOTA_DONE);
			}
			next_state = FOTA_FSM_WAIT_FOR_SWITCHOVER;
		}
		break;

	case FOTA_FSM_INITIATE_UPDATE:
		r = initiate_update(pCtx);
		if (r < 0) {
			next_state = FOTA_FSM_ERROR;
		} else {
			next_state = FOTA_FSM_SUCCESS;
		}
		break;

	default:
		next_state = FOTA_FSM_ERROR;
		break;
	}

	if (next_state != pCtx->state) {
		LOG_INF("%s: %s->%s",
			log_strdup(fota_image_type_get_string(pCtx->type)),
			fota_state_get_string(pCtx->state),
			fota_state_get_string(next_state));
	}
	pCtx->state = next_state;
}

static bool transport_not_required(void)
{
	if (tctx.app_context.using_transport) {
		return false;
#ifdef CONFIG_MODEM_HL7800
	} else if (tctx.modem_context.using_transport) {
		return false;
#endif
	}

	return true;
}

static int initiate_update(fota_context_t *pCtx)
{
	int r = 0;

#ifdef CONFIG_MODEM_HL7800
	if (pCtx->type == MODEM_IMAGE_TYPE) {
		r = hl7800_initiate_modem_update(pCtx);
#ifdef CONFIG_HTTP_FOTA_DELETE_FILE_AFTER_UPDATE
		if (r == 0) {
			fsu_delete("/lfs", pCtx->file_path);
		}
#endif
	}
#endif

	return r;
}

void fota_download_handler(const struct fota_download_evt *evt)
{
	switch (evt->id) {
	case FOTA_DOWNLOAD_EVT_ERROR:
		if (tctx.app_context.using_transport) {
			tctx.app_context.download_error = true;
#ifdef CONFIG_MODEM_HL7800
		} else if (tctx.modem_context.using_transport) {
			tctx.modem_context.download_error = true;
#endif
		}
		LOG_ERR("FOTA download error");
		k_sem_give(&wait_fota_download);
		break;
	case FOTA_DOWNLOAD_EVT_FINISHED:
		LOG_INF("FOTA download finished");
		k_sem_give(&wait_fota_download);
		break;
	case FOTA_DOWNLOAD_EVT_PROGRESS:
		LOG_INF("FOTA progress %d", evt->progress);
		break;
	default:
		LOG_WRN("Unhandled FOTA event %d", evt->id);
		break;
	}
}
