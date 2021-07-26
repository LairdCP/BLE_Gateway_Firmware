/**
 * @file attr_table.c
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <string.h>

#include "attr_validator.h"
#include "attr_custom_validator.h"
#include "attr_table.h"
#include "errno_str.h"

/* clang-format off */

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define DRW DEFAULT_RW_ATTRIBUTE_VALUES
#define DRO DEFAULT_RO_ATTRIBUTE_VALUES

/* Add things to the end of the API document.
 * Do not remove items. Change them to deprecated.
 */
typedef struct rw_attribute {
	/* pystart - rw attributes */
	char location[32 + 1];
	uint32_t passkey;
	bool lock;
	int8_t txPower;
	uint16_t networkId;
	uint8_t configVersion;
	uint8_t hardwareVersion;
	uint32_t qrtcLastSet;
	bool commissioned;
	char rootCaName[48 + 1];
	char clientCertName[48 + 1];
	char clientKeyName[48 + 1];
	char endpoint[254 + 1];
	char port[16 + 1];
	char clientId[32 + 1];
	char topicPrefix[32 + 1];
	uint8_t motionOdr;
	uint8_t motionThresh;
	uint8_t motionScale;
	uint8_t motionDuration;
	uint8_t sdLogMaxSize;
	uint8_t ctAesKey[16];
	uint32_t joinDelay;
	uint16_t joinMin;
	uint16_t joinMax;
	uint32_t joinInterval;
	bool delayCloudReconnect;
	char loadPath[32 + 1];
	char dumpPath[32 + 1];
	float floaty;
	uint8_t lwm2mPsk[16];
	char lwm2mClientId[32 + 1];
	char lwm2mPeerUrl[128 + 1];
	enum ethernet_type ethernetType;
	enum ethernet_mode ethernetMode;
	char ethernetStaticIPAddress[15 + 1];
	uint8_t ethernetStaticNetmaskLength;
	char ethernetStaticGateway[15 + 1];
	char ethernetStaticDNS[15 + 1];
	char sntpServer[64 + 1];
	/* pyend */
} rw_attribute_t;

static const rw_attribute_t DEFAULT_RW_ATTRIBUTE_VALUES = {
	/* pystart - rw defaults */
	.location = "",
	.passkey = 123456,
	.lock = false,
	.txPower = 0,
	.networkId = 0,
	.configVersion = 0,
	.hardwareVersion = 0,
	.qrtcLastSet = 0,
	.commissioned = false,
	.rootCaName = "/lfs/root_ca.pem",
	.clientCertName = "/lfs/client_cert.pem",
	.clientKeyName = "/lfs/client_key.pem",
	.endpoint = "a3273rvo818l4w-ats.iot.us-east-1.amazonaws.com",
	.port = "8883",
	.clientId = "",
	.topicPrefix = "mg100-ct/dev/gw/",
	.motionOdr = 5,
	.motionThresh = 10,
	.motionScale = 2,
	.motionDuration = 6,
	.sdLogMaxSize = 0,
	.ctAesKey = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	.joinDelay = 0,
	.joinMin = 1,
	.joinMax = 100,
	.joinInterval = 1,
	.delayCloudReconnect = false,
	.loadPath = "/lfs/params.txt",
	.dumpPath = "/lfs/dump.txt",
	.floaty = 0.13,
	.lwm2mPsk = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f },
	.lwm2mClientId = "Client_identity",
	.lwm2mPeerUrl = "uwterminalx.lairdconnect.com",
	.ethernetType = 1,
	.ethernetMode = 2,
	.ethernetStaticIPAddress = "0.0.0.0",
	.ethernetStaticNetmaskLength = 0,
	.ethernetStaticGateway = "0.0.0.0",
	.ethernetStaticDNS = "0.0.0.0",
	.sntpServer = "time.windows.com"
	/* pyend */
};

typedef struct ro_attribute {
	/* pystart - ro attributes */
	char firmwareVersion[11 + 1];
	char resetReason[12 + 1];
	char bluetoothAddress[12 + 1];
	uint32_t resetCount;
	int64_t upTime;
	char attributeVersion[11 + 1];
	uint32_t qrtc;
	char name[32 + 1];
	char board[32 + 1];
	char buildId[64 + 1];
	char appType[32 + 1];
	char mount[32 + 1];
	enum cert_status certStatus;
	enum gateway_state gatewayState;
	bool motionAlarm;
	char gatewayId[15 + 1];
	enum central_state centralState;
	char sensorBluetoothAddress[30 + 1];
	enum fota_control_point fotaControlPoint;
	enum fota_status fotaStatus;
	char fotaFileName[64 + 1];
	uint32_t fotaSize;
	uint32_t fotaCount;
	enum generate_psk generatePsk;
	enum cloud_error cloudError;
	bool commissioningBusy;
	enum ethernet_init_error ethernetInitError;
	uint8_t ethernetMAC[6];
	bool ethernetCableDetected;
	enum ethernet_speed ethernetSpeed;
	enum ethernet_duplex ethernetDuplex;
	char ethernetIPAddress[15 + 1];
	uint8_t ethernetNetmaskLength;
	char ethernetGateway[15 + 1];
	char ethernetDNS[15 + 1];
	uint32_t ethernetDHCPLeaseTime;
	uint32_t ethernetDHCPRenewTime;
	enum ethernet_dhcp_state ethernetDHCPState;
	uint8_t ethernetDHCPAttempts;
	/* pyend */
} ro_attribute_t;

static const ro_attribute_t DEFAULT_RO_ATTRIBUTE_VALUES = {
	/* pystart - ro defaults */
	.firmwareVersion = "0.0.0",
	.resetReason = "RESETPIN",
	.bluetoothAddress = "0",
	.resetCount = 0,
	.upTime = 0,
	.attributeVersion = "0.4.28",
	.qrtc = 0,
	.name = "",
	.board = "",
	.buildId = "0",
	.appType = "",
	.mount = "/lfs",
	.certStatus = 0,
	.gatewayState = 0,
	.motionAlarm = false,
	.gatewayId = "",
	.centralState = 0,
	.sensorBluetoothAddress = "",
	.fotaControlPoint = 0,
	.fotaStatus = 0,
	.fotaFileName = "",
	.fotaSize = 0,
	.fotaCount = 0,
	.generatePsk = 0,
	.cloudError = 0,
	.commissioningBusy = false,
	.ethernetInitError = 0,
	.ethernetMAC = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
	.ethernetCableDetected = false,
	.ethernetSpeed = 0,
	.ethernetDuplex = 0,
	.ethernetIPAddress = "0.0.0.0",
	.ethernetNetmaskLength = 0,
	.ethernetGateway = "0.0.0.0",
	.ethernetDNS = "0.0.0.0",
	.ethernetDHCPLeaseTime = 0,
	.ethernetDHCPRenewTime = 0,
	.ethernetDHCPState = 0,
	.ethernetDHCPAttempts = 0
	/* pyend */
};

/* pystart - remap */
#define attr_get_string_certStatus          attr_get_string_cert_status
#define attr_get_string_gatewayState        attr_get_string_gateway_state
#define attr_get_string_centralState        attr_get_string_central_state
#define attr_get_string_fotaControlPoint    attr_get_string_fota_control_point
#define attr_get_string_fotaStatus          attr_get_string_fota_status
#define attr_get_string_generatePsk         attr_get_string_generate_psk
#define attr_get_string_cloudError          attr_get_string_cloud_error
#define attr_get_string_ethernetInitError   attr_get_string_ethernet_init_error
#define attr_get_string_ethernetType        attr_get_string_ethernet_type
#define attr_get_string_ethernetMode        attr_get_string_ethernet_mode
#define attr_get_string_ethernetSpeed       attr_get_string_ethernet_speed
#define attr_get_string_ethernetDuplex      attr_get_string_ethernet_duplex
#define attr_get_string_ethernetDHCPState   attr_get_string_ethernet_dhcp_state
/* pyend */

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static rw_attribute_t rw;
static ro_attribute_t ro;

/******************************************************************************/
/* Global Data Definitions                                                    */
/******************************************************************************/

/**
 * @brief Table shorthand
 *
 * @ref CreateStruct (Python script)
 * Writable but non-savable values are populated using RO macro.
 *
 *.........name...value...default....size...writable..readable..get enum str
 */
#define RW_ATTRS(n) STRINGIFY(n), rw.n, DRW.n, sizeof(rw.n), NULL
#define RW_ATTRX(n) STRINGIFY(n), &rw.n, &DRW.n, sizeof(rw.n), NULL
#define RW_ATTRE(n) STRINGIFY(n), &rw.n, &DRW.n, sizeof(rw.n), attr_get_string_ ## n
#define RO_ATTRS(n) STRINGIFY(n), ro.n, DRO.n, sizeof(ro.n), NULL
#define RO_ATTRX(n) STRINGIFY(n), &ro.n, &DRO.n, sizeof(ro.n), NULL
#define RO_ATTRE(n) STRINGIFY(n), &ro.n, &DRO.n, sizeof(ro.n), attr_get_string_ ## n

#define y true
#define n false

/* If min == max then range isn't checked. */

/* index....id.name.....................type.savable.writable.readable.lockable.broadcast.deprecated.validator..min.max. */
const struct attr_table_entry ATTR_TABLE[ATTR_TABLE_SIZE] = {
	/* pystart - attribute table */
	[0  ] = { 1  , RW_ATTRS(location)                      , ATTR_TYPE_STRING        , y, y, y, y, n, n, av_string           , NULL                                , .min.ux = 0         , .max.ux = 32         },
	[1  ] = { 4  , RW_ATTRX(passkey)                       , ATTR_TYPE_U32           , y, y, y, y, y, n, av_uint32           , NULL                                , .min.ux = 0         , .max.ux = 999999     },
	[2  ] = { 5  , RW_ATTRX(lock)                          , ATTR_TYPE_BOOL          , y, y, y, y, n, n, av_bool             , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[3  ] = { 11 , RO_ATTRS(firmwareVersion)               , ATTR_TYPE_STRING        , n, n, y, n, n, n, av_string           , NULL                                , .min.ux = 3         , .max.ux = 11         },
	[4  ] = { 12 , RO_ATTRS(resetReason)                   , ATTR_TYPE_STRING        , n, n, y, n, n, n, av_string           , NULL                                , .min.ux = 0         , .max.ux = 12         },
	[5  ] = { 13 , RO_ATTRS(bluetoothAddress)              , ATTR_TYPE_STRING        , n, n, y, n, n, n, av_string           , NULL                                , .min.ux = 12        , .max.ux = 12         },
	[6  ] = { 14 , RO_ATTRX(resetCount)                    , ATTR_TYPE_U32           , n, n, y, n, n, n, av_uint32           , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[7  ] = { 16 , RO_ATTRX(upTime)                        , ATTR_TYPE_S64           , n, n, y, n, n, n, av_int64            , attr_prepare_upTime                 , .min.ux = 0         , .max.ux = 0          },
	[8  ] = { 59 , RW_ATTRX(txPower)                       , ATTR_TYPE_S8            , y, y, y, n, y, n, av_int8             , NULL                                , .min.sx = -40       , .max.sx = 8          },
	[9  ] = { 60 , RW_ATTRX(networkId)                     , ATTR_TYPE_U16           , y, y, y, y, y, n, av_uint16           , NULL                                , .min.ux = 0         , .max.ux = 65535      },
	[10 ] = { 61 , RW_ATTRX(configVersion)                 , ATTR_TYPE_U8            , y, y, y, n, y, n, av_uint8            , NULL                                , .min.ux = 0         , .max.ux = 255        },
	[11 ] = { 63 , RW_ATTRX(hardwareVersion)               , ATTR_TYPE_U8            , y, y, y, n, y, n, av_uint8            , NULL                                , .min.ux = 0         , .max.ux = 255        },
	[12 ] = { 93 , RO_ATTRS(attributeVersion)              , ATTR_TYPE_STRING        , n, n, y, n, n, n, av_string           , NULL                                , .min.ux = 6         , .max.ux = 11         },
	[13 ] = { 94 , RO_ATTRX(qrtc)                          , ATTR_TYPE_U32           , n, n, y, n, n, n, av_uint32           , attr_prepare_qrtc                   , .min.ux = 0         , .max.ux = 0          },
	[14 ] = { 95 , RW_ATTRX(qrtcLastSet)                   , ATTR_TYPE_U32           , y, n, y, n, n, n, av_uint32           , attr_prepare_qrtcLastSet            , .min.ux = 0         , .max.ux = 0          },
	[15 ] = { 140, RO_ATTRS(name)                          , ATTR_TYPE_STRING        , n, n, y, n, n, n, av_string           , NULL                                , .min.ux = 1         , .max.ux = 32         },
	[16 ] = { 142, RO_ATTRS(board)                         , ATTR_TYPE_STRING        , n, n, y, n, n, n, av_string           , NULL                                , .min.ux = 1         , .max.ux = 32         },
	[17 ] = { 143, RO_ATTRS(buildId)                       , ATTR_TYPE_STRING        , n, n, y, n, n, n, av_string           , NULL                                , .min.ux = 1         , .max.ux = 64         },
	[18 ] = { 144, RO_ATTRS(appType)                       , ATTR_TYPE_STRING        , n, n, y, n, n, n, av_string           , NULL                                , .min.ux = 1         , .max.ux = 32         },
	[19 ] = { 145, RO_ATTRS(mount)                         , ATTR_TYPE_STRING        , n, n, y, n, n, n, av_string           , NULL                                , .min.ux = 0         , .max.ux = 32         },
	[20 ] = { 146, RW_ATTRX(commissioned)                  , ATTR_TYPE_BOOL          , y, y, y, n, y, n, av_cpb              , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[21 ] = { 147, RO_ATTRE(certStatus)                    , ATTR_TYPE_S32           , n, n, y, n, n, n, av_int32            , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[22 ] = { 148, RW_ATTRS(rootCaName)                    , ATTR_TYPE_STRING        , y, y, y, n, y, n, av_string           , NULL                                , .min.ux = 0         , .max.ux = 48         },
	[23 ] = { 149, RW_ATTRS(clientCertName)                , ATTR_TYPE_STRING        , y, y, y, n, y, n, av_string           , NULL                                , .min.ux = 0         , .max.ux = 48         },
	[24 ] = { 150, RW_ATTRS(clientKeyName)                 , ATTR_TYPE_STRING        , y, y, y, n, y, n, av_string           , NULL                                , .min.ux = 0         , .max.ux = 48         },
	[25 ] = { 151, RW_ATTRS(endpoint)                      , ATTR_TYPE_STRING        , y, y, y, n, y, n, av_string           , NULL                                , .min.ux = 0         , .max.ux = 254        },
	[26 ] = { 152, RW_ATTRS(port)                          , ATTR_TYPE_STRING        , y, y, y, n, y, n, av_string           , NULL                                , .min.ux = 4         , .max.ux = 16         },
	[27 ] = { 153, RW_ATTRS(clientId)                      , ATTR_TYPE_STRING        , y, y, y, n, y, n, av_string           , NULL                                , .min.ux = 0         , .max.ux = 32         },
	[28 ] = { 154, RW_ATTRS(topicPrefix)                   , ATTR_TYPE_STRING        , y, y, y, n, y, n, av_string           , NULL                                , .min.ux = 0         , .max.ux = 32         },
	[29 ] = { 155, RO_ATTRE(gatewayState)                  , ATTR_TYPE_U8            , n, n, y, n, n, n, av_uint8            , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[30 ] = { 168, RW_ATTRX(motionOdr)                     , ATTR_TYPE_U8            , y, y, y, n, y, n, av_uint8            , NULL                                , .min.ux = 0         , .max.ux = 255        },
	[31 ] = { 169, RW_ATTRX(motionThresh)                  , ATTR_TYPE_U8            , y, y, y, n, y, n, av_uint8            , NULL                                , .min.ux = 0         , .max.ux = 255        },
	[32 ] = { 170, RW_ATTRX(motionScale)                   , ATTR_TYPE_U8            , y, y, y, n, y, n, av_uint8            , NULL                                , .min.ux = 0         , .max.ux = 255        },
	[33 ] = { 171, RW_ATTRX(motionDuration)                , ATTR_TYPE_U8            , y, y, y, n, y, n, av_uint8            , NULL                                , .min.ux = 0         , .max.ux = 255        },
	[34 ] = { 172, RO_ATTRX(motionAlarm)                   , ATTR_TYPE_BOOL          , n, n, y, n, n, n, av_bool             , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[35 ] = { 173, RW_ATTRX(sdLogMaxSize)                  , ATTR_TYPE_U8            , y, y, y, n, n, n, av_uint8            , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[36 ] = { 174, RW_ATTRX(ctAesKey)                      , ATTR_TYPE_BYTE_ARRAY    , y, y, n, n, n, n, av_array            , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[37 ] = { 176, RO_ATTRS(gatewayId)                     , ATTR_TYPE_STRING        , n, n, y, n, n, n, av_string           , NULL                                , .min.ux = 0         , .max.ux = 15         },
	[38 ] = { 188, RO_ATTRE(centralState)                  , ATTR_TYPE_U8            , n, n, y, n, n, n, av_uint8            , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[39 ] = { 189, RO_ATTRS(sensorBluetoothAddress)        , ATTR_TYPE_STRING        , n, n, y, n, n, n, av_string           , NULL                                , .min.ux = 0         , .max.ux = 30         },
	[40 ] = { 190, RW_ATTRX(joinDelay)                     , ATTR_TYPE_U32           , y, y, y, n, y, n, av_cp32             , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[41 ] = { 191, RW_ATTRX(joinMin)                       , ATTR_TYPE_U16           , y, y, y, n, n, n, av_uint16           , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[42 ] = { 192, RW_ATTRX(joinMax)                       , ATTR_TYPE_U16           , y, y, y, n, n, n, av_uint16           , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[43 ] = { 193, RW_ATTRX(joinInterval)                  , ATTR_TYPE_U32           , y, y, y, n, n, n, av_uint32           , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[44 ] = { 195, RW_ATTRX(delayCloudReconnect)           , ATTR_TYPE_BOOL          , y, y, y, n, n, n, av_bool             , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[45 ] = { 203, RO_ATTRE(fotaControlPoint)              , ATTR_TYPE_U8            , n, y, n, n, y, n, av_cp8              , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[46 ] = { 204, RO_ATTRE(fotaStatus)                    , ATTR_TYPE_U8            , n, n, y, n, n, n, av_uint8            , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[47 ] = { 205, RO_ATTRS(fotaFileName)                  , ATTR_TYPE_STRING        , n, y, y, n, n, n, av_string           , NULL                                , .min.ux = 0         , .max.ux = 64         },
	[48 ] = { 206, RO_ATTRX(fotaSize)                      , ATTR_TYPE_U32           , n, n, y, n, n, n, av_uint32           , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[49 ] = { 207, RO_ATTRX(fotaCount)                     , ATTR_TYPE_U32           , n, n, y, n, n, n, av_uint32           , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[50 ] = { 208, RW_ATTRS(loadPath)                      , ATTR_TYPE_STRING        , y, y, y, n, n, n, av_string           , NULL                                , .min.ux = 0         , .max.ux = 32         },
	[51 ] = { 209, RW_ATTRS(dumpPath)                      , ATTR_TYPE_STRING        , y, y, y, n, n, n, av_string           , NULL                                , .min.ux = 0         , .max.ux = 32         },
	[52 ] = { 211, RW_ATTRX(floaty)                        , ATTR_TYPE_FLOAT         , y, y, y, n, n, n, av_float            , NULL                                , .min.fx = 0.0       , .max.fx = 0.0        },
	[53 ] = { 212, RO_ATTRE(generatePsk)                   , ATTR_TYPE_U8            , n, y, n, n, y, n, av_cp8              , NULL                                , .min.ux = 0         , .max.ux = 1          },
	[54 ] = { 213, RW_ATTRX(lwm2mPsk)                      , ATTR_TYPE_BYTE_ARRAY    , y, n, y, n, n, n, av_array            , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[55 ] = { 214, RW_ATTRS(lwm2mClientId)                 , ATTR_TYPE_STRING        , y, y, y, n, n, n, av_string           , NULL                                , .min.ux = 1         , .max.ux = 32         },
	[56 ] = { 215, RW_ATTRS(lwm2mPeerUrl)                  , ATTR_TYPE_STRING        , y, y, y, n, n, n, av_string           , NULL                                , .min.ux = 6         , .max.ux = 128        },
	[57 ] = { 217, RO_ATTRE(cloudError)                    , ATTR_TYPE_S8            , n, n, y, n, n, n, av_int8             , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[58 ] = { 218, RO_ATTRX(commissioningBusy)             , ATTR_TYPE_BOOL          , n, n, y, n, n, n, av_bool             , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[59 ] = { 221, RO_ATTRE(ethernetInitError)             , ATTR_TYPE_S8            , n, n, y, n, n, n, av_int8             , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[60 ] = { 222, RO_ATTRX(ethernetMAC)                   , ATTR_TYPE_BYTE_ARRAY    , n, n, y, n, n, n, av_array            , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[61 ] = { 223, RW_ATTRE(ethernetType)                  , ATTR_TYPE_U8            , y, y, y, n, n, n, av_uint8            , NULL                                , .min.ux = 1         , .max.ux = 2          },
	[62 ] = { 224, RW_ATTRE(ethernetMode)                  , ATTR_TYPE_U8            , y, y, y, n, n, n, av_uint8            , NULL                                , .min.ux = 1         , .max.ux = 2          },
	[63 ] = { 225, RO_ATTRX(ethernetCableDetected)         , ATTR_TYPE_BOOL          , n, n, y, n, n, n, av_bool             , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[64 ] = { 226, RO_ATTRE(ethernetSpeed)                 , ATTR_TYPE_U8            , n, n, y, n, n, n, av_uint8            , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[65 ] = { 227, RO_ATTRE(ethernetDuplex)                , ATTR_TYPE_U8            , n, n, y, n, n, n, av_uint8            , NULL                                , .min.ux = 0         , .max.ux = 0          },
	[66 ] = { 228, RO_ATTRS(ethernetIPAddress)             , ATTR_TYPE_STRING        , n, n, y, n, n, n, av_string           , NULL                                , .min.ux = 7         , .max.ux = 15         },
	[67 ] = { 229, RO_ATTRX(ethernetNetmaskLength)         , ATTR_TYPE_U8            , n, n, y, n, n, n, av_uint8            , NULL                                , .min.ux = 0         , .max.ux = 32         },
	[68 ] = { 230, RO_ATTRS(ethernetGateway)               , ATTR_TYPE_STRING        , n, n, y, n, n, n, av_string           , NULL                                , .min.ux = 7         , .max.ux = 15         },
	[69 ] = { 231, RO_ATTRS(ethernetDNS)                   , ATTR_TYPE_STRING        , n, n, y, n, n, n, av_string           , NULL                                , .min.ux = 7         , .max.ux = 15         },
	[70 ] = { 232, RW_ATTRS(ethernetStaticIPAddress)       , ATTR_TYPE_STRING        , y, y, y, n, n, n, av_string           , NULL                                , .min.ux = 7         , .max.ux = 15         },
	[71 ] = { 233, RW_ATTRX(ethernetStaticNetmaskLength)   , ATTR_TYPE_U8            , y, y, y, n, n, n, av_uint8            , NULL                                , .min.ux = 0         , .max.ux = 32         },
	[72 ] = { 234, RW_ATTRS(ethernetStaticGateway)         , ATTR_TYPE_STRING        , y, y, y, n, n, n, av_string           , NULL                                , .min.ux = 7         , .max.ux = 15         },
	[73 ] = { 235, RW_ATTRS(ethernetStaticDNS)             , ATTR_TYPE_STRING        , y, y, y, n, n, n, av_string           , NULL                                , .min.ux = 7         , .max.ux = 15         },
	[74 ] = { 236, RO_ATTRX(ethernetDHCPLeaseTime)         , ATTR_TYPE_U32           , n, n, y, n, n, n, av_uint32           , NULL                                , .min.ux = 0         , .max.ux = 4294967294 },
	[75 ] = { 237, RO_ATTRX(ethernetDHCPRenewTime)         , ATTR_TYPE_U32           , n, n, y, n, n, n, av_uint32           , NULL                                , .min.ux = 0         , .max.ux = 4294967294 },
	[76 ] = { 238, RO_ATTRE(ethernetDHCPState)             , ATTR_TYPE_U8            , n, n, y, n, n, n, av_uint8            , NULL                                , .min.ux = 0         , .max.ux = 6          },
	[77 ] = { 239, RO_ATTRX(ethernetDHCPAttempts)          , ATTR_TYPE_U8            , n, n, y, n, n, n, av_uint8            , NULL                                , .min.ux = 0         , .max.ux = 100        },
	[78 ] = { 241, RW_ATTRS(sntpServer)                    , ATTR_TYPE_STRING        , y, y, y, n, n, n, av_string           , NULL                                , .min.ux = 7         , .max.ux = 64         }
	/* pyend */
};

/**
 * @brief map id to table entry (invalid entries are NULL)
 */
static const struct attr_table_entry * const ATTR_MAP[] = {
	/* pystart - attribute map */
	[1  ] = &ATTR_TABLE[0  ],
	[4  ] = &ATTR_TABLE[1  ],
	[5  ] = &ATTR_TABLE[2  ],
	[11 ] = &ATTR_TABLE[3  ],
	[12 ] = &ATTR_TABLE[4  ],
	[13 ] = &ATTR_TABLE[5  ],
	[14 ] = &ATTR_TABLE[6  ],
	[16 ] = &ATTR_TABLE[7  ],
	[59 ] = &ATTR_TABLE[8  ],
	[60 ] = &ATTR_TABLE[9  ],
	[61 ] = &ATTR_TABLE[10 ],
	[63 ] = &ATTR_TABLE[11 ],
	[93 ] = &ATTR_TABLE[12 ],
	[94 ] = &ATTR_TABLE[13 ],
	[95 ] = &ATTR_TABLE[14 ],
	[140] = &ATTR_TABLE[15 ],
	[142] = &ATTR_TABLE[16 ],
	[143] = &ATTR_TABLE[17 ],
	[144] = &ATTR_TABLE[18 ],
	[145] = &ATTR_TABLE[19 ],
	[146] = &ATTR_TABLE[20 ],
	[147] = &ATTR_TABLE[21 ],
	[148] = &ATTR_TABLE[22 ],
	[149] = &ATTR_TABLE[23 ],
	[150] = &ATTR_TABLE[24 ],
	[151] = &ATTR_TABLE[25 ],
	[152] = &ATTR_TABLE[26 ],
	[153] = &ATTR_TABLE[27 ],
	[154] = &ATTR_TABLE[28 ],
	[155] = &ATTR_TABLE[29 ],
	[168] = &ATTR_TABLE[30 ],
	[169] = &ATTR_TABLE[31 ],
	[170] = &ATTR_TABLE[32 ],
	[171] = &ATTR_TABLE[33 ],
	[172] = &ATTR_TABLE[34 ],
	[173] = &ATTR_TABLE[35 ],
	[174] = &ATTR_TABLE[36 ],
	[176] = &ATTR_TABLE[37 ],
	[188] = &ATTR_TABLE[38 ],
	[189] = &ATTR_TABLE[39 ],
	[190] = &ATTR_TABLE[40 ],
	[191] = &ATTR_TABLE[41 ],
	[192] = &ATTR_TABLE[42 ],
	[193] = &ATTR_TABLE[43 ],
	[195] = &ATTR_TABLE[44 ],
	[203] = &ATTR_TABLE[45 ],
	[204] = &ATTR_TABLE[46 ],
	[205] = &ATTR_TABLE[47 ],
	[206] = &ATTR_TABLE[48 ],
	[207] = &ATTR_TABLE[49 ],
	[208] = &ATTR_TABLE[50 ],
	[209] = &ATTR_TABLE[51 ],
	[211] = &ATTR_TABLE[52 ],
	[212] = &ATTR_TABLE[53 ],
	[213] = &ATTR_TABLE[54 ],
	[214] = &ATTR_TABLE[55 ],
	[215] = &ATTR_TABLE[56 ],
	[217] = &ATTR_TABLE[57 ],
	[218] = &ATTR_TABLE[58 ],
	[221] = &ATTR_TABLE[59 ],
	[222] = &ATTR_TABLE[60 ],
	[223] = &ATTR_TABLE[61 ],
	[224] = &ATTR_TABLE[62 ],
	[225] = &ATTR_TABLE[63 ],
	[226] = &ATTR_TABLE[64 ],
	[227] = &ATTR_TABLE[65 ],
	[228] = &ATTR_TABLE[66 ],
	[229] = &ATTR_TABLE[67 ],
	[230] = &ATTR_TABLE[68 ],
	[231] = &ATTR_TABLE[69 ],
	[232] = &ATTR_TABLE[70 ],
	[233] = &ATTR_TABLE[71 ],
	[234] = &ATTR_TABLE[72 ],
	[235] = &ATTR_TABLE[73 ],
	[236] = &ATTR_TABLE[74 ],
	[237] = &ATTR_TABLE[75 ],
	[238] = &ATTR_TABLE[76 ],
	[239] = &ATTR_TABLE[77 ],
	[241] = &ATTR_TABLE[78 ]
	/* pyend */
};
BUILD_ASSERT(ARRAY_SIZE(ATTR_MAP) == (ATTR_TABLE_MAX_ID + 1),
	     "Invalid attribute map");

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void attr_table_initialize(void)
{
	memcpy(&rw, &DRW, sizeof(rw_attribute_t));
	memcpy(&ro, &DRO, sizeof(ro_attribute_t));
}

void attr_table_factory_reset(void)
{
	size_t i = 0;
	for (i = 0; i < ATTR_TABLE_SIZE; i++) {
		memcpy(ATTR_TABLE[i].pData, ATTR_TABLE[i].pDefault,
		       ATTR_TABLE[i].size);
	}
}

const struct attr_table_entry *const attr_map(attr_id_t id)
{
	if (id > ATTR_TABLE_MAX_ID) {
		return NULL;
	} else {
		return ATTR_MAP[id];
	}
}

attr_index_t attr_table_index(const struct attr_table_entry *const entry)
{
	__ASSERT(PART_OF_ARRAY(ATTR_TABLE, entry), "Invalid entry");
	return (entry - &ATTR_TABLE[0]);
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
/* pystart - prepare for read - weak implementations */
__weak int attr_prepare_upTime(void)
{
	return 0;
}

__weak int attr_prepare_qrtc(void)
{
	return 0;
}

__weak int attr_prepare_qrtcLastSet(void)
{
	return 0;
}

/* pyend */

/* pystart - get string */
const char *const attr_get_string_cert_status(int value)
{
	switch (value) {
		case 1:           return "Busy";
		case 0:           return "Success";
		default:          return errno_str_get(value);
	}
}

const char *const attr_get_string_gateway_state(int value)
{
	switch (value) {
		case 0:           return "Power Up Init";
		case 1:           return "Network Init";
		case 2:           return "Wait For Network";
		case 3:           return "Network Connected";
		case 4:           return "Network Disconnected";
		case 5:           return "Network Error";
		case 6:           return "Wait For Commission";
		case 7:           return "Resolve Server";
		case 8:           return "Wait Before Cloud Connect";
		case 9:           return "Cloud Connected";
		case 10:          return "Cloud Wait For Disconnect";
		case 11:          return "Cloud Disconnected";
		case 12:          return "Cloud Error";
		case 13:          return "Fota Busy ";
		case 14:          return "Decommission";
		case 15:          return "Cloud Request Disconnect";
		case 16:          return "Cloud Connecting";
		case 17:          return "Modem Init";
		case 18:          return "Modem Error";
		default:          return "?";
	}
}

const char *const attr_get_string_central_state(int value)
{
	switch (value) {
		case 0:           return "Finding Device";
		case 1:           return "Finding Service";
		case 2:           return "Finding Ess Temperature Char";
		case 3:           return "Finding Ess Humidity Char";
		case 4:           return "Finding Ess Pressure Char";
		case 5:           return "Connected And Configured";
		case 6:           return "Finding Smp Char";
		case 7:           return "Challenge Request";
		case 8:           return "Challenge Response";
		case 9:           return "Log Download";
		default:          return "?";
	}
}

const char *const attr_get_string_fota_control_point(int value)
{
	switch (value) {
		case 0:           return "Nop";
		case 2:           return "Modem Start";
		default:          return "?";
	}
}

const char *const attr_get_string_fota_status(int value)
{
	switch (value) {
		case 0:           return "Success";
		case 1:           return "Busy";
		case 2:           return "Error";
		default:          return "?";
	}
}

const char *const attr_get_string_generate_psk(int value)
{
	switch (value) {
		case 0:           return "LwM2M Default";
		case 1:           return "LwM2M Random";
		default:          return "?";
	}
}

const char *const attr_get_string_cloud_error(int value)
{
	switch (value) {
		case 0:           return "None";
		case -1:          return "Init Endpoint";
		case -2:          return "Init Client";
		case -3:          return "Init Root Ca";
		case -4:          return "Read Cred Fs";
		case -5:          return "Cred Size";
		case -6:          return "Init Topic Prefix";
		case -7:          return "Init Client Cert";
		case -8:          return "Init Client Key";
		default:          return "?";
	}
}

const char *const attr_get_string_ethernet_init_error(int value)
{
	switch (value) {
		case 0:           return "None";
		case -1:          return "No Iface";
		case -2:          return "Iface Cfg";
		case -3:          return "Dns Cfg";
		default:          return "?";
	}
}

const char *const attr_get_string_ethernet_type(int value)
{
	switch (value) {
		case 1:           return "IPv4";
		case 2:           return "IPv6";
		default:          return "?";
	}
}

const char *const attr_get_string_ethernet_mode(int value)
{
	switch (value) {
		case 1:           return "Static";
		case 2:           return "DHCP";
		default:          return "?";
	}
}

const char *const attr_get_string_ethernet_speed(int value)
{
	switch (value) {
		case 0:           return "Unknown";
		case 1:           return "10 Mbps";
		case 2:           return "100 Mbps";
		case 4:           return "1 Gbps";
		default:          return "?";
	}
}

const char *const attr_get_string_ethernet_duplex(int value)
{
	switch (value) {
		case 0:           return "Unknown";
		case 1:           return "Half";
		case 2:           return "Full";
		default:          return "?";
	}
}

const char *const attr_get_string_ethernet_dhcp_state(int value)
{
	switch (value) {
		case 0:           return "Disabled";
		case 1:           return "Init";
		case 2:           return "Selecting";
		case 3:           return "Requesting";
		case 4:           return "Renewing";
		case 5:           return "Rebinding";
		case 6:           return "Bound";
		default:          return "?";
	}
}

/* pyend */