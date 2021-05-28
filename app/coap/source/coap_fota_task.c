/**
 * @file coap_fota_task.c
 * @brief
 *
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(coap_fota_task, LOG_LEVEL_DBG);
#define FWK_FNAME "coap_fota"

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <stddef.h>
#include <zephyr.h>
#include <storage/flash_map.h>
#include <dfu/mcuboot.h>
#include <dfu/flash_img.h>
#include <power/reboot.h>
#include <drivers/modem/hl7800.h>

#if USE_PARTITION_MANAGER
#include <pm_config.h>
#endif

#include "FrameworkIncludes.h"
#include "laird_utility_macros.h"
#include "file_system_utilities.h"
#include "coap_fota_query.h"
#include "coap_fota_shadow.h"
#include "coap_fota.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
/* @ref mcuboot.c */
#if USE_PARTITION_MANAGER
#define FLASH_AREA_IMAGE_SECONDARY PM_MCUBOOT_SECONDARY_ID
#else
/* FLASH_AREA_ID() values used below are auto-generated by DT */
#ifdef CONFIG_TRUSTED_EXECUTION_NONSECURE
#define FLASH_AREA_IMAGE_SECONDARY FLASH_AREA_ID(image_1_nonsecure)
#else
#define FLASH_AREA_IMAGE_SECONDARY FLASH_AREA_ID(image_1)
#endif /* CONFIG_TRUSTED_EXECUTION_NONSECURE */
#endif /* USE_PARTITION_MANAGER */

#ifndef COAP_FOTA_TASK_PRIORITY
#define COAP_FOTA_TASK_PRIORITY K_PRIO_PREEMPT(2)
#endif

#ifndef COAP_FOTA_TASK_STACK_DEPTH
#define COAP_FOTA_TASK_STACK_DEPTH 3072
#endif

#ifndef COAP_FOTA_TASK_QUEUE_DEPTH
#define COAP_FOTA_TASK_QUEUE_DEPTH 8
#endif

#define COAP_FOTA_TICK_RATE K_SECONDS(1)
#define TIMER_PERIOD_ONE_SHOT K_SECONDS(0)

typedef enum fota_fsm_state_t {
	FOTA_FSM_ABORT = -2,
	FOTA_FSM_ERROR = -1,
	FOTA_FSM_IDLE = 0,
	FOTA_FSM_END,
	FOTA_FSM_SUCCESS,
	FOTA_FSM_MODEM_WAIT,
	FOTA_FSM_WAIT,
	FOTA_FSM_START,
	FOTA_FSM_GET_SIZE,
	FOTA_FSM_GET_HASH,
	FOTA_FSM_QUERY_FS,
	FOTA_FSM_CHECK_HASH,
	FOTA_FSM_START_DOWNLOAD,
	FOTA_FSM_RESUME_DOWNLOAD,
	FOTA_FSM_DOWNLOAD_COMPLETE,
	FOTA_FSM_SECOND_HASH_CHECK,
	FOTA_FSM_DELETE_EXISTING_FILE,
	FOTA_FSM_WAIT_FOR_SWITCHOVER,
	FOTA_FSM_INITIATE_UPDATE
} fota_fsm_state_t;

typedef struct fota_context {
	enum fota_image_type type;
	coap_fota_query_t query;
	fota_fsm_state_t state;
	bool using_transport;
	uint32_t delay;
} fota_context_t;

typedef struct coap_fota_task {
	FwkMsgTask_t msgTask;
	bool fs_mounted;
	bool aws_connected;
	bool allow_start;
	fota_context_t app_context;
	fota_context_t modem_context;
} coap_fota_task_obj_t;

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static coap_fota_task_obj_t cfto;

K_THREAD_STACK_DEFINE(coap_fota_task_stack, COAP_FOTA_TASK_STACK_DEPTH);

K_MSGQ_DEFINE(coap_fota_task_queue, FWK_QUEUE_ENTRY_SIZE,
	      COAP_FOTA_TASK_QUEUE_DEPTH, FWK_QUEUE_ALIGNMENT);

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static void coap_fota_task_thread(void *pArg1, void *pArg2, void *pArg3);

static DispatchResult_t coap_connection_msg_handler(FwkMsgReceiver_t *pMsgRxer,
						    FwkMsg_t *pMsg);

static DispatchResult_t coap_start_ack_msg_handler(FwkMsgReceiver_t *pMsgRxer,
						   FwkMsg_t *pMsg);

static DispatchResult_t coap_fota_tick_msg_handler(FwkMsgReceiver_t *pMsgRxer,
						   FwkMsg_t *pMsg);

static char *fota_state_get_string(fota_fsm_state_t state);
static void fota_fsm(fota_context_t *pCtx);

static bool hash_match(coap_fota_query_t *q, size_t size);
static int initiate_update(fota_context_t *pCtx);
static int initiate_app_update(coap_fota_query_t *q);
static int initiate_modem_update(coap_fota_query_t *q);
static int copy_app(struct flash_img_context *flash_ctx, const char *path,
		    const char *name, size_t size);
static bool transport_not_required(void);

/******************************************************************************/
/* Framework Message Dispatcher                                               */
/******************************************************************************/
static FwkMsgHandler_t *coap_fota_taskMsgDispatcher(FwkMsgCode_t MsgCode)
{
	/* clang-format off */
	switch (MsgCode) {
	case FMC_INVALID:                return Framework_UnknownMsgHandler;
	case FMC_PERIODIC:               return coap_fota_tick_msg_handler;
	case FMC_AWS_CONNECTED:          return coap_connection_msg_handler;
	case FMC_AWS_DISCONNECTED:       return coap_connection_msg_handler;
	case FMC_FOTA_START_ACK:         return coap_start_ack_msg_handler;
	default:                         return NULL;
	}
	/* clang-format on */
}

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
int coap_fota_task_initialize(void)
{
	memset(&cfto, 0, sizeof(coap_fota_task_obj_t));

	cfto.msgTask.rxer.id = FWK_ID_COAP_FOTA_TASK;
	cfto.msgTask.rxer.pQueue = &coap_fota_task_queue;
	cfto.msgTask.rxer.rxBlockTicks = K_FOREVER;
	cfto.msgTask.rxer.pMsgDispatcher = coap_fota_taskMsgDispatcher;
	cfto.msgTask.timerDurationTicks = COAP_FOTA_TICK_RATE;
	cfto.msgTask.timerPeriodTicks = TIMER_PERIOD_ONE_SHOT;
	Framework_RegisterTask(&cfto.msgTask);

	cfto.msgTask.pTid =
		k_thread_create(&cfto.msgTask.threadData, coap_fota_task_stack,
				K_THREAD_STACK_SIZEOF(coap_fota_task_stack),
				coap_fota_task_thread, &cfto, NULL, NULL,
				COAP_FOTA_TASK_PRIORITY, 0, K_NO_WAIT);

	k_thread_name_set(cfto.msgTask.pTid, FWK_FNAME);

	cfto.app_context.type = APP_IMAGE_TYPE;
	cfto.modem_context.type = MODEM_IMAGE_TYPE;

	return 0;
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void coap_fota_task_thread(void *pArg1, void *pArg2, void *pArg3)
{
	coap_fota_task_obj_t *pObj = (coap_fota_task_obj_t *)pArg1;

	if (fsu_lfs_mount() == 0) {
		pObj->fs_mounted = true;
	}

	coap_fota_init();
	coap_fota_shadow_init(CONFIG_FSU_MOUNT_POINT, CONFIG_FSU_MOUNT_POINT);
	/* Populate query because name is used in fsm print. */
	coap_fota_populate_query(APP_IMAGE_TYPE, &pObj->app_context.query);
	coap_fota_populate_query(MODEM_IMAGE_TYPE, &pObj->modem_context.query);

	while (true) {
		Framework_MsgReceiver(&pObj->msgTask.rxer);
	}
}

static DispatchResult_t coap_start_ack_msg_handler(FwkMsgReceiver_t *pMsgRxer,
						   FwkMsg_t *pMsg)
{
	coap_fota_task_obj_t *pObj = FWK_TASK_CONTAINER(coap_fota_task_obj_t);
	pObj->allow_start = true;
	return DISPATCH_OK;
}

/* AWS must be connected to get shadow information.
 * AWS must be disconnected to run CoAP because there isn't enough
 * memory to support two simultaneous connections.
 */
static DispatchResult_t coap_connection_msg_handler(FwkMsgReceiver_t *pMsgRxer,
						    FwkMsg_t *pMsg)
{
	coap_fota_task_obj_t *pObj = FWK_TASK_CONTAINER(coap_fota_task_obj_t);
	if (pMsg->header.msgCode == FMC_AWS_CONNECTED) {
		pObj->aws_connected = true;
		Framework_StartTimer(&pObj->msgTask);
	} else {
		pObj->aws_connected = false;
	}
	return DISPATCH_OK;
}

static DispatchResult_t coap_fota_tick_msg_handler(FwkMsgReceiver_t *pMsgRxer,
						   FwkMsg_t *pMsg)
{
	UNUSED_PARAMETER(pMsg);
	coap_fota_task_obj_t *pObj = FWK_TASK_CONTAINER(coap_fota_task_obj_t);

	if (pObj->aws_connected) {
		coap_fota_shadow_update_handler();
	}

	if (pObj->fs_mounted) {
		fota_fsm(&pObj->app_context);
		fota_fsm(&pObj->modem_context);
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
		PREFIXED_SWITCH_CASE_RETURN_STRING(FOTA_FSM, GET_SIZE);
		PREFIXED_SWITCH_CASE_RETURN_STRING(FOTA_FSM, GET_HASH);
		PREFIXED_SWITCH_CASE_RETURN_STRING(FOTA_FSM, QUERY_FS);
		PREFIXED_SWITCH_CASE_RETURN_STRING(FOTA_FSM, CHECK_HASH);
		PREFIXED_SWITCH_CASE_RETURN_STRING(FOTA_FSM, START_DOWNLOAD);
		PREFIXED_SWITCH_CASE_RETURN_STRING(FOTA_FSM, RESUME_DOWNLOAD);
		PREFIXED_SWITCH_CASE_RETURN_STRING(FOTA_FSM, DOWNLOAD_COMPLETE);
		PREFIXED_SWITCH_CASE_RETURN_STRING(FOTA_FSM, SECOND_HASH_CHECK);
		PREFIXED_SWITCH_CASE_RETURN_STRING(FOTA_FSM, DELETE_EXISTING_FILE);
		PREFIXED_SWITCH_CASE_RETURN_STRING(FOTA_FSM, WAIT_FOR_SWITCHOVER);
		PREFIXED_SWITCH_CASE_RETURN_STRING(FOTA_FSM, INITIATE_UPDATE);
		default:
			return "UNKNOWN";
	}
	/* clang-format on */
}

static void fota_fsm(fota_context_t *pCtx)
{
	int r = 0;
	fota_fsm_state_t next_state = pCtx->state;

	switch (pCtx->state) {
	case FOTA_FSM_ERROR:
		pCtx->delay = CONFIG_COAP_FOTA_ERROR_DELAY;
		coap_fota_increment_error_count(pCtx->type);
		next_state = FOTA_FSM_END;
		break;

	case FOTA_FSM_ABORT:
		next_state = FOTA_FSM_END;
		break;

	case FOTA_FSM_SUCCESS:
		if (pCtx->type == MODEM_IMAGE_TYPE) {
			LOG_WRN("Modem Updating");
			pCtx->delay = CONFIG_COAP_FOTA_MODEM_INSTALL_DELAY;
			next_state = FOTA_FSM_MODEM_WAIT;
		} else {
			LOG_WRN("Entering mcuboot");
			k_sleep(K_SECONDS(1)); /* Allow last print to occur. */
			sys_reboot(SYS_REBOOT_COLD);
			next_state = FOTA_FSM_END; /* don't care */
		}
		break;

	case FOTA_FSM_MODEM_WAIT:
		/* The modem is going to reboot.  If the cloud fsm
		 * stays in its fota state, then its queue won't get overfilled
		 * by the app fsm requesting its turn (or by sensor data).
		 */
		if (coap_fota_modem_install_complete()) {
			pCtx->delay = 0;
		}

		if (pCtx->delay > 0) {
			pCtx->delay -= 1;
			next_state = FOTA_FSM_MODEM_WAIT;
		} else {
			next_state = FOTA_FSM_END;
		}
		break;

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
		if (coap_fota_request(pCtx->type)) {
			FRAMEWORK_MSG_CREATE_AND_SEND(
				FWK_ID_RESERVED, FWK_ID_CLOUD, FMC_FOTA_START);
			next_state = FOTA_FSM_START;
		}
		break;

	case FOTA_FSM_START:
		/* Ack is used to ensure AWS didn't disconnect for another reason. */
		if (cfto.allow_start && !cfto.aws_connected) {
			/* The FOTA state machine requires a connection until
			 * it gets to the switchover state.
			 */
			cfto.allow_start = false;
			pCtx->using_transport = true;
			coap_fota_populate_query(pCtx->type, &pCtx->query);
			next_state = FOTA_FSM_GET_SIZE;
		}
		break;

	case FOTA_FSM_GET_SIZE:
		r = coap_fota_get_firmware_size(&pCtx->query);
		if (r < 0) {
			next_state = FOTA_FSM_ERROR;
		} else {
			next_state = FOTA_FSM_QUERY_FS;
		}
		break;

	case FOTA_FSM_QUERY_FS:
		r = fsu_single_entry_exists(pCtx->query.fs_path,
					    pCtx->query.filename,
					    FS_DIR_ENTRY_FILE);
		if (r < 0) {
			pCtx->query.offset = 0;
		} else {
			pCtx->query.offset = r;
		}
		next_state = FOTA_FSM_GET_HASH;
		break;

	case FOTA_FSM_GET_HASH:
		/* If a partial image exists on the file system, then the
		 * hash of the partial image will be obtained.
		 */
		r = coap_fota_get_hash(&pCtx->query);
		if (r < 0) {
			next_state = FOTA_FSM_ERROR;
		} else {
			next_state = FOTA_FSM_CHECK_HASH;
		}
		break;

	case FOTA_FSM_CHECK_HASH:
		if (hash_match(&pCtx->query, pCtx->query.offset)) {
			if (pCtx->query.offset == pCtx->query.size) {
				next_state = FOTA_FSM_DOWNLOAD_COMPLETE;
			} else if (pCtx->query.offset < pCtx->query.size) {
				next_state = FOTA_FSM_RESUME_DOWNLOAD;
			} else {
				LOG_DBG("Unexpected file size offset: %u expected: %u",
					pCtx->query.offset, pCtx->query.size);
				next_state = FOTA_FSM_DELETE_EXISTING_FILE;
			}
		} else {
			next_state = FOTA_FSM_DELETE_EXISTING_FILE;
		}
		break;

	case FOTA_FSM_DELETE_EXISTING_FILE:
		pCtx->query.offset = 0;
		r = fsu_delete_files(pCtx->query.fs_path, pCtx->query.filename);
		if (r < 0) {
			next_state = FOTA_FSM_ERROR;
		} else {
			next_state = FOTA_FSM_START_DOWNLOAD;
		}
		break;

	case FOTA_FSM_RESUME_DOWNLOAD:
		/* This state is for debug purposes. */
		next_state = FOTA_FSM_START_DOWNLOAD;
		break;

	case FOTA_FSM_START_DOWNLOAD:
		r = coap_fota_get_firmware(&pCtx->query);
		if (r < 0) {
			next_state = FOTA_FSM_ERROR;
		} else {
			next_state = FOTA_FSM_DOWNLOAD_COMPLETE;
		}
		break;

	case FOTA_FSM_DOWNLOAD_COMPLETE:
		coap_fota_set_downloaded_filename(pCtx->type,
						  pCtx->query.filename,
						  strlen(pCtx->query.filename));

		next_state = FOTA_FSM_SECOND_HASH_CHECK;
		break;

	case FOTA_FSM_SECOND_HASH_CHECK:
		if (coap_fota_resumed_download(&pCtx->query)) {
			LOG_DBG("Get hash for entire file");
			next_state = FOTA_FSM_QUERY_FS;
		} else if (!hash_match(&pCtx->query, pCtx->query.size)) {
			next_state = FOTA_FSM_ERROR;
		} else {
			next_state = FOTA_FSM_WAIT_FOR_SWITCHOVER;
		}
		break;

	case FOTA_FSM_WAIT_FOR_SWITCHOVER:
		pCtx->using_transport = false;
		if (coap_fota_ready(pCtx->type)) {
			next_state = FOTA_FSM_INITIATE_UPDATE;
		} else if (coap_fota_abort(pCtx->type)) {
			next_state = FOTA_FSM_ABORT;
		} else {
			if (transport_not_required()) {
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
		LOG_INF("%s: %s->%s", log_strdup(pCtx->query.image),
			fota_state_get_string(pCtx->state),
			fota_state_get_string(next_state));
	}
	pCtx->state = next_state;
}

static int initiate_update(fota_context_t *pCtx)
{
	int r = 0;
	if (pCtx->type == MODEM_IMAGE_TYPE) {
		r = initiate_modem_update(&pCtx->query);
	} else {
		r = initiate_app_update(&pCtx->query);
	}

#ifdef CONFIG_COAP_FOTA_DELETE_FILE_AFTER_UPDATE
	if (r == 0) {
		if (fsu_delete_files(pCtx->query.fs_path,
				     pCtx->query.filename) < 0) {
			LOG_ERR("Unable to delete");
		}
	}
#endif

	return r;
}

static int copy_app(struct flash_img_context *flash_ctx, const char *path,
		    const char *name, size_t size)
{
	char abs_path[FSU_MAX_ABS_PATH_SIZE];
	(void)fsu_build_full_name(abs_path, sizeof(abs_path), path, name);

	struct fs_file_t f;
	int r = fs_open(&f, abs_path, FS_O_READ);
	if (r < 0) {
		return r;
	}

	uint8_t *buffer = k_malloc(CONFIG_IMG_BLOCK_BUF_SIZE);
	if (buffer != NULL) {
		size_t rem = size;
		ssize_t bytes_read;
		size_t length;
		while (r == 0 && rem > 0) {
			length = MIN(rem, CONFIG_IMG_BLOCK_BUF_SIZE);
			bytes_read = fs_read(&f, buffer, length);
			if (bytes_read == length) {
				rem -= length;
				r = flash_img_buffered_write(
					flash_ctx, buffer, length, (rem == 0));
				if (r < 0) {
					LOG_ERR("Unable to write to slot (%d) rem: %u size %u",
						r, rem, size);
				}
			} else {
				r = -EIO;
			}
		}
	} else {
		r = -ENOMEM;
	}

	k_free(buffer);
	(void)fs_close(&f);
	return r;
}

static int initiate_app_update(coap_fota_query_t *q)
{
	int r = 0;
	struct flash_img_context *flash_ctx = NULL;
	while (1) {
		flash_ctx = k_calloc(sizeof(struct flash_img_context),
				     sizeof(uint8_t));
		if (flash_ctx == NULL) {
			LOG_ERR("Unable to allocate flash context");
			r = -ENOMEM;
			break;
		}

		r = boot_erase_img_bank(FLASH_AREA_IMAGE_SECONDARY);
		if (r < 0) {
			LOG_ERR("Unable to erase secondary image bank");
			break;
		} else {
			LOG_DBG("Secondary slot erased");
		}

		r = flash_img_init(flash_ctx);
		if (r < 0) {
			LOG_ERR("Unable to init image id");
			break;
		}

		r = copy_app(flash_ctx, q->fs_path, q->filename, q->size);
		if (r < 0) {
			LOG_ERR("Unable to copy app to secondary slot");
			break;
		} else {
			LOG_DBG("Image copied");
		}

		r = boot_request_upgrade(BOOT_UPGRADE_PERMANENT);
		if (r < 0) {
			LOG_ERR("Unable to initiate boot request");
			break;
		}

		break;
	}
	k_free(flash_ctx);
	return r;
}

static int initiate_modem_update(coap_fota_query_t *q)
{
	char abs_path[FSU_MAX_ABS_PATH_SIZE];
	(void)fsu_build_full_name(abs_path, sizeof(abs_path), q->fs_path,
				  q->filename);
	return mdm_hl7800_update_fw(abs_path);
}

static bool hash_match(coap_fota_query_t *q, size_t size)
{
	int r = fsu_sha256(q->computed_hash, q->fs_path, q->filename, size);
	if (r == 0) {
		return (memcmp(q->expected_hash, q->computed_hash,
			       FSU_HASH_SIZE) == 0);
	}
	return false;
}

static bool transport_not_required(void)
{
	return (cfto.app_context.using_transport ||
		cfto.modem_context.using_transport) ?
			     false :
			     true;
}