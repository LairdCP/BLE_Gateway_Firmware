/**
 * @file sensor_adv_format.h
 * @brief Advertisement format for Laird BT sensors
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __SENSOR_ADV_FORMAT_H__
#define __SENSOR_ADV_FORMAT_H__

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr/types.h>
#include <bluetooth/bluetooth.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Common                                                                     */
/******************************************************************************/
#define SENSOR_ADDR_STR_SIZE 13
#define SENSOR_ADDR_STR_LEN (SENSOR_ADDR_STR_SIZE - 1)

#define SENSOR_NAME_MAX_SIZE 32
#define SENSOR_NAME_MAX_STR_LEN (SENSOR_NAME_MAX_SIZE - 1)

#define LAIRD_CONNECTIVITY_MANUFACTURER_SPECIFIC_COMPANY_ID1 0x0077
#define LAIRD_CONNECTIVITY_MANUFACTURER_SPECIFIC_COMPANY_ID2 0x00E4

/* clang-format off */
#define BT510_1M_PHY_AD_PROTOCOL_ID      0x0001
#define BT510_CODED_PHY_AD_PROTOCOL_ID   0x0002
#define BT510_1M_PHY_RSP_PROTOCOL_ID     0x0003
/* clang-format on */

#define ADV_FORMAT_HW_VERSION(major, minor) ((uint8_t)(((((uint32_t)(major)) << 3) & 0x000000F8) | ((((uint32_t)(minor)) << 0 ) & 0x00000007))

#define ADV_FORMAT_HW_VERSION_GET_MAJOR(x) ((x & 0x000000F8) >> 3)
#define ADV_FORMAT_HW_VERSION_GET_MINOR(x) ((x & 0x00000007) >> 0)

/******************************************************************************/
/* BT510                                                                      */
/******************************************************************************/
#define BT510_RESET_ACK_TO_DUMP_DELAY_TICKS K_SECONDS(10)
#define BT510_WRITE_TO_RESET_DELAY_TICKS K_MSEC(1500)

/* Format of the Manufacturer Specific Data (MSD) using 1M PHY.
 * Format of the 1st chunk of MSD when using coded PHY.
 */
struct Bt510AdEvent {
	uint16_t companyId;
	uint16_t protocolId;
	uint16_t networkId;
	uint16_t flags;
	bt_addr_t addr;
	uint8_t recordType;
	uint16_t id;
	uint32_t epoch;
	uint16_t data;
	uint16_t dataReserved;
	uint8_t resetCount;
} __packed;
typedef struct Bt510AdEvent Bt510AdEvent_t;

/* Format of the response payload for 1M PHY.
 * This is the second chunk of the extended advertisement data
 * when using the coded PHY.
 */
struct Bt510Rsp {
	uint16_t productId;
	uint8_t firmwareVersionMajor;
	uint8_t firmwareVersionMinor;
	uint8_t firmwareVersionPatch;
	uint8_t firmwareType;
	uint8_t configVersion;
	uint8_t bootloaderVersionMajor;
	uint8_t bootloaderVersionMinor;
	uint8_t bootloaderVersionPatch;
	uint8_t hardwareVersion; /* major + minor stuffed into one byte */
} __packed;
typedef struct Bt510Rsp Bt510Rsp_t;

/* Format of the Manufacturer Specific Data using 1M PHY in Scan Response */
struct Bt510RspWithHeader {
	uint16_t companyId;
	uint16_t protocolId;
	Bt510Rsp_t rsp;
} __packed;
typedef struct Bt510RspWithHeader Bt510RspWithHeader_t;

/* Format of the Manufacturer Specific Data for Coded PHY */
struct Bt510Coded {
	Bt510AdEvent_t ad;
	Bt510Rsp_t rsp;
} __packed;
typedef struct Bt510Coded Bt510Coded_t;

/*
 * This is the format for the 1M PHY.
 */
#define BT510_MSD_AD_FIELD_LENGTH 0x1b
#define BT510_MSD_AD_PAYLOAD_LENGTH (BT510_MSD_AD_FIELD_LENGTH - 1)
BUILD_ASSERT(sizeof(Bt510AdEvent_t) == BT510_MSD_AD_PAYLOAD_LENGTH,
	     "BT510 Advertisement data size mismatch (check packing)");

#define BT510_MSD_RSP_FIELD_LENGTH 0x10
#define BT510_MSD_RSP_PAYLOAD_LENGTH (BT510_MSD_RSP_FIELD_LENGTH - 1)
BUILD_ASSERT(sizeof(Bt510RspWithHeader_t) == BT510_MSD_RSP_PAYLOAD_LENGTH,
	     "BT510 Scan Response size mismatch (check packing)");

/*
 * Coded PHY
 */
#define BT510_MSD_CODED_FIELD_LENGTH 0x26
#define BT510_MSD_CODED_PAYLOAD_LENGTH (BT510_MSD_CODED_FIELD_LENGTH - 1)
BUILD_ASSERT(sizeof(Bt510Coded_t) == BT510_MSD_CODED_PAYLOAD_LENGTH,
	     "BT510 Coded advertisement size mismatch (check packing)");

/* Bytes used to differentiate advertisement types/sensors. */
#define SENSOR_AD_HEADER_SIZE 4
extern const uint8_t BT510_AD_HEADER[SENSOR_AD_HEADER_SIZE];
extern const uint8_t BT510_RSP_HEADER[SENSOR_AD_HEADER_SIZE];
extern const uint8_t BT510_CODED_HEADER[SENSOR_AD_HEADER_SIZE];

#ifdef __cplusplus
}
#endif

#endif /* __SENSOR_ADV_FORMAT_H__ */
