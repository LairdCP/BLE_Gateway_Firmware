/**
 * @file ct_datalog.h
 * @brief Contact Tracing data log structures
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __CT_DATALOG_H__
#define __CT_DATALOG_H__

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Global Constants, Macros and Type Definitions                              */
/******************************************************************************/
/* clang-format off */
#define LOG_ENTRY_IDX_START_BYTE  0
#define LOG_ENTRY_IDX_FLAGS       1
#define LOG_ENTRY_IDX_INTERVAL    2
#define LOG_ENTRY_IDX_DEVICE_ID   3
#define LOG_ENTRY_IDX_TIMESTAMP   4

#define LOG_ENTRY_START_BYTE    0xA5
#define LOG_ENTRY_FLAGS_UNSENT  0xFF
#define LOG_ENTRY_FLAGS_SENT    0x00
/* clang-format on */

#define LOG_ENTRY_PROTOCOL_V1 0x0001
#define LOG_ENTRY_PROTOCOL_V2 0x0002

#define LOG_ENTRY_FW_VERSION_SIZE 4

#define LOG_ENTRY_DEVICE_ID_SIZE 6

#define LOG_ENTRY_MAX_SIZE 256

#define BT_MAC_ADDR_LEN 6

/* data entry data */
typedef struct __attribute__((packed)) __log_entry_data_rssi_tracking_t__ {
	uint8_t recordType;
	int8_t rssi;
	uint8_t motion;
	int8_t txPower;

} log_entry_data_rssi_tracking_t;

/* data entry data */
typedef struct __attribute__((packed)) __log_entry_data_rssi_tracking_w_ts_t__ {
	uint8_t recordType;
	uint8_t status;
	uint8_t reserved1;
	uint16_t scanIntervalOffset;
	int8_t rssi;
	uint8_t motion;
	int8_t txPower;

} log_entry_data_rssi_tracking_w_ts_t;

/* Log entry item */
typedef struct __attribute__((packed)) __LOG_ENTRY_T__ {
	struct __attribute__((packed)) {
		uint8_t entryStart;
		uint8_t flags;
		uint16_t scanInterval;
		uint8_t serial[BT_MAC_ADDR_LEN];
		uint32_t timestamp;
		/* Replaced with bytes of record data when the log entry is requested. */
		uint8_t reserved[2];
	} header;

	/* All data structures need to include type as first byte */
	union __attribute__((packed)) {
		uint8_t recordType;
		log_entry_data_rssi_tracking_t rssiTrackData;
		log_entry_data_rssi_tracking_w_ts_t rssiTrackWTsData;
	} data;
	/* Start of multiple possible data records */

} log_entry_t;

/* data stored to a log entry by this device itself to record some of its status
 * values. This roughly equates to values that would end up in a "thing shadow"
 * for this device.
 */
struct ct_record_local {
	uint8_t fw_version[LOG_ENTRY_FW_VERSION_SIZE];
	uint16_t devices_seen;
	uint16_t network_id;
	uint16_t ad_interval_ms;
	uint16_t log_interval_min;
	uint16_t scan_interval_sec;
	uint8_t battery_level;
	uint8_t scan_duration_sec;
	uint8_t profile;
	int8_t rssi_threshold;
	int8_t tx_power;
} __attribute__((packed));

struct ct_record_local_v2 {
	uint8_t fw_version[LOG_ENTRY_FW_VERSION_SIZE];
	uint16_t devices_seen;
	uint16_t network_id;
	uint16_t ad_interval_ms;
	uint16_t log_interval_min;
	uint16_t scan_interval_sec;
	uint8_t battery_level;
	uint8_t scan_duration_sec;
	uint8_t profile;
	int8_t rssi_threshold;
	int8_t tx_power;
	uint32_t up_time_sec;
} __attribute__((packed));

struct ct_log_header_entry_protocol_version {
	uint16_t entry_protocol_version;
} __attribute__((packed));

struct ct_log_header {
	uint16_t entry_protocol_version;
	uint16_t entry_size;
	uint16_t entry_count;
	uint8_t device_id[LOG_ENTRY_DEVICE_ID_SIZE];
	uint32_t device_time;
	uint32_t log_size;
	uint32_t last_upload_time;
	struct ct_record_local local_info;
} __attribute__((packed));

struct ct_log_header_v2 {
	uint16_t entry_protocol_version;
	uint16_t max_entry_size;
	uint16_t entry_count;
	uint8_t device_id[LOG_ENTRY_DEVICE_ID_SIZE];
	uint32_t device_time;
	uint32_t log_size;
	uint32_t last_upload_time;
	struct ct_record_local_v2 local_info;
} __attribute__((packed));

struct ct_publish_header_t {
	uint16_t entry_protocol_version;
	uint8_t device_id[LOG_ENTRY_DEVICE_ID_SIZE];
	/* NOTE: This is gateway time when publishing, not BT510 time. */
	uint32_t device_time;
	uint32_t last_upload_time;
	uint8_t fw_version[LOG_ENTRY_FW_VERSION_SIZE];
	uint8_t battery_level;
	uint16_t network_id;
} __attribute__((packed));

#ifdef __cplusplus
}
#endif

#endif /* __CT_DATALOG_H__ */
