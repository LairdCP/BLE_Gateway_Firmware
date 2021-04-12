/**
 * @file ble_lwm2m_service.c
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(ble_lwm2m_service);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <bluetooth/bluetooth.h>
#include <random/rand32.h>

#include "lcz_bluetooth.h"
#include "nv.h"
#include "ble_lwm2m_service.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define BSS_BASE_UUID_128(_x_)                                                 \
	BT_UUID_INIT_128(0x4c, 0x72, 0x8f, 0x51, 0x05, 0xc4, 0x4a, 0x36, 0x8c, \
			 0x76, 0x20, 0xd3, LSB_16(_x_), MSB_16(_x_), 0xfd,     \
			 0x07)

static struct bt_uuid_128 LWM2M_UUID = BSS_BASE_UUID_128(0x0000);
static struct bt_uuid_128 LWM2M_GENERATE_UUID = BSS_BASE_UUID_128(0x0001);
static struct bt_uuid_128 LWM2N_CLIENT_PSK = BSS_BASE_UUID_128(0x0002);
static struct bt_uuid_128 LWM2M_CLIENT_ID_UUID = BSS_BASE_UUID_128(0x0003);
static struct bt_uuid_128 LWM2M_PEER_URL_UUID = BSS_BASE_UUID_128(0x0004);

struct lwm2m_config {
	uint8_t client_psk[CONFIG_LWM2M_PSK_SIZE];
	char client_id[CONFIG_LWM2M_CLIENT_ID_MAX_SIZE];
	char peer_url[CONFIG_LWM2M_PEER_URL_MAX_SIZE];
};

/* "000102030405060708090a0b0c0d0e0f" */
static const struct lwm2m_config DEFAULT_CONFIG = {
	.client_psk = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
			0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f },
	.client_id = "Client_identity",
	.peer_url = "uwterminalx.lairdconnect.com"
};

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static struct lwm2m_config lwm2m;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static ssize_t generate_psk(struct bt_conn *conn,
			    const struct bt_gatt_attr *attr, const void *buf,
			    uint16_t len, uint16_t offset, uint8_t flags);

static ssize_t write_client_id(struct bt_conn *conn,
			       const struct bt_gatt_attr *attr, const void *buf,
			       uint16_t len, uint16_t offset, uint8_t flags);

static ssize_t write_peer_url(struct bt_conn *conn,
			      const struct bt_gatt_attr *attr, const void *buf,
			      uint16_t len, uint16_t offset, uint8_t flags);

static ssize_t read_psk(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, uint16_t len, uint16_t offset);

static ssize_t read_client_id(struct bt_conn *conn,
			      const struct bt_gatt_attr *attr, void *buf,
			      uint16_t len, uint16_t offset);

static ssize_t read_peer_url(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr, void *buf,
			     uint16_t len, uint16_t offset);

/******************************************************************************/
/* Sensor Service                                                             */
/******************************************************************************/
static struct bt_gatt_attr l2m2m_attrs[] = {
	BT_GATT_PRIMARY_SERVICE(&LWM2M_UUID),
	BT_GATT_CHARACTERISTIC(&LWM2M_GENERATE_UUID.uuid, BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_WRITE, NULL, generate_psk, NULL),
	BT_GATT_CHARACTERISTIC(&LWM2N_CLIENT_PSK.uuid, BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, read_psk, NULL,
			       lwm2m.client_psk),
	BT_GATT_CHARACTERISTIC(&LWM2M_CLIENT_ID_UUID.uuid,
			       BT_GATT_CHRC_WRITE | BT_GATT_CHRC_READ,
			       BT_GATT_PERM_WRITE | BT_GATT_PERM_READ,
			       read_client_id, write_client_id,
			       &lwm2m.client_id),
	BT_GATT_CHARACTERISTIC(&LWM2M_PEER_URL_UUID.uuid,
			       BT_GATT_CHRC_WRITE | BT_GATT_CHRC_READ,
			       BT_GATT_PERM_WRITE | BT_GATT_PERM_READ,
			       read_peer_url, write_peer_url, &lwm2m.peer_url),
};

static struct bt_gatt_service lwm2m_service = BT_GATT_SERVICE(l2m2m_attrs);

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void ble_lwm2m_service_init()
{
	nvInitLwm2mConfig(&lwm2m, (void *)&DEFAULT_CONFIG, sizeof(lwm2m));

	bt_gatt_service_register(&lwm2m_service);
}

uint8_t *ble_lwm2m_get_client_psk(void)
{
	return lwm2m.client_psk;
}

char *ble_lwm2m_get_client_id(void)
{
	return lwm2m.client_id;
}

char *ble_lwm2m_get_peer_url(void)
{
	return lwm2m.peer_url;
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static ssize_t generate_psk(struct bt_conn *conn,
			    const struct bt_gatt_attr *attr, const void *buf,
			    uint16_t len, uint16_t offset, uint8_t flags)
{
#if CONFIG_LWM2M_ENABLE_PSK_GENERATION
	uint8_t value = *((uint8_t *)buf);
	uint32_t rand;
	if (value) {
		if (CONFIG_LWM2M_PSK_SIZE % 4 != 0) {
			LOG_ERR("PSK length must be divisible by 4");
			return BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
		}
		LOG_WRN("Generating a new LwM2M PSK");
		for (uint8_t i = 0; i < CONFIG_LWM2M_PSK_SIZE; i += 4) {
			rand = sys_rand32_get();
			memcpy(lwm2m.client_psk + i, &rand, sizeof(rand));
		}
	} else {
		LOG_WRN("Setting LwM2M config to defaults");
		memcpy(&lwm2m, &DEFAULT_CONFIG, sizeof(lwm2m));
		LOG_DBG("LwM2M Client Identity: %s",
			log_strdup(lwm2m.client_id));
		LOG_DBG("LwM2M Peer URL: %s", log_strdup(lwm2m.peer_url));
	}
	nvWriteLwm2mConfig(&lwm2m, sizeof(lwm2m));
	LOG_HEXDUMP_DBG(lwm2m.client_psk, CONFIG_LWM2M_PSK_SIZE,
			"LwM2M Client PSK (hex)");
	return len;
#else
	return BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
#endif
}

static ssize_t write_client_id(struct bt_conn *conn,
			       const struct bt_gatt_attr *attr, const void *buf,
			       uint16_t len, uint16_t offset, uint8_t flags)
{
	ssize_t length = lbt_write_string(conn, attr, buf, len, offset, flags,
					  CONFIG_LWM2M_CLIENT_ID_MAX_SIZE);
	if (length > 0) {
		nvWriteLwm2mConfig(&lwm2m, sizeof(lwm2m));
		LOG_DBG("LwM2M Client Identity: %s",
			log_strdup(lwm2m.client_id));
	}
	return length;
}

static ssize_t write_peer_url(struct bt_conn *conn,
			      const struct bt_gatt_attr *attr, const void *buf,
			      uint16_t len, uint16_t offset, uint8_t flags)
{
	ssize_t length = lbt_write_string(conn, attr, buf, len, offset, flags,
					  CONFIG_LWM2M_PEER_URL_MAX_SIZE);
	if (length > 0) {
		nvWriteLwm2mConfig(&lwm2m, sizeof(lwm2m));
		LOG_DBG("LwM2M Peer URL: %s", log_strdup(lwm2m.peer_url));
	}
	return length;
}

static ssize_t read_psk(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, uint16_t len, uint16_t offset)
{
	const char *value = attr->user_data;
	return bt_gatt_attr_read(conn, attr, buf, len, offset, value,
				 CONFIG_LWM2M_PSK_SIZE);
}

static ssize_t read_client_id(struct bt_conn *conn,
			      const struct bt_gatt_attr *attr, void *buf,
			      uint16_t len, uint16_t offset)
{
	return lbt_read_string(conn, attr, buf, len, offset,
			       CONFIG_LWM2M_CLIENT_ID_MAX_SIZE - 1);
}

static ssize_t read_peer_url(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr, void *buf,
			     uint16_t len, uint16_t offset)
{
	return lbt_read_string(conn, attr, buf, len, offset,
			       CONFIG_LWM2M_PEER_URL_MAX_SIZE - 1);
}
