# Contact Tracing

Contact Tracing (CT) is a version of gateway firmware that communicates with the Sentrius™ BT710 tracker or BT510-CT.

# Overview

The Sentrius™ tracker’s firmware is designed for detecting and logging when trackers are within proximity of one another, and sending and receiving data to communicate this. This firmware allows trackers to indicate their presence and sense the presence of other nearby compatible trackers. The behavior of BLE advertisement transmission and scanning/discovering other trackers is configurable based on use case via parameters (see the [Sentrius™ BT710 User Guide Parameters](https://www.lairdconnect.com/documentation/user-guide-sentrius-bt710) section for details on user-configurable parameters).

# System Requirements for Data Logging

In order to capture data logs from the a Bluetooth tracking device (BT710 or BT510-CT), your system must include a Laird Connectivity gateway with contact tracing (CT) compatible firmware. The tracker will automatically interact with a properly configured gateway over Bluetooth Low Energy, allowing the gateway to download and forward CT data logs to its configured cloud-based endpoint. The Laird Connectivity MG100, IG60-BL654 and IG60-SERIAL gateways are designed for connectivity to the AWS IoT platform, however they may be configured for other platforms with support from the Laird Connectivity Design Services team, if necessary (separate business arrangement and development contract required). The BL5340 development kit can be used as a demonstration board with a wired (ethernet) internet connection, however is not a ready-to-deploy packaged product.
The figure below depicts a typical scenario for a small deployment of trackers and an MG100 BLE-to-Cellular gateway. In this scenario, trackers will sense one another, capture CT log data locally and when nearby the MG100 will forward their logs to AWS IoT.

![Contact Tracing Overview](images/contact_tracing.png)

# Data Log Overview

The CT log data is published by the gateway to a remote MQTT broker in JSON format. This makes it easy for a web service interested in the data to subscribe to the MQTT topic and parse the data for further processing and storage. This stored data may be retrieved to aid in contact tracing operations.

The JSON format for CT log data represents a series of events captured by a **reporting sensor** (sensor that scanned for other sensors, generated log entries and is now reporting that data in this JSON document) and a _remote sensor_ (the sensor detected by the reporting sensor in a BLE scan). These JSON documents contain an array of one or more **entry** objects, each containing an array of one or more log objects. Each log object represents a single BLE scan performed by the reporting sensor where the remote sensor was detected in that BLE scan. The entries[n].timestamp value is the absolute timestamp in UTC time captured by the reporting sensor when the first log was added to the logs[] array. Timestamps of each log object can be calculated by multiplying the **delta** value with the **scanInterval** value and adding it to the **entries[n].timestamp** value in the parent entry object.

## Data Log JSON Example

```
{
	"entryProtocolVersion": 2,
	"deviceId": "01020304aaaa",
	"deviceTime": 1597936969,
	"lastUploadTime": 1597935821,
	"fwVersion": "00000600",
	"batteryLevel": 3040,
	"networkId": 65535,
	"entries": [{

		"entryStart": 165,
		"flags": 253,
		"scanInterval": 30,
		"serial": "01020304bbbb",
		"timestamp": 1597935576,
		"length": 112,
		"logs": [{
			"recordType": 17,
			"log": {
				"delta": 0,
				"rssi": -44,
				"motion": 0,
				"txPower": 0
			}
		}]
	}]
}
```

## Data Log Details

| Key (Value Pair)             | Description                                                                                                 |
| ---------------------------- | ----------------------------------------------------------------------------------------------------------- |
| "entryProtocolVersion": 2    | Indicates the protocol ID for this entire JSON object                                                       |
| "deviceId": "01020304aaaa"   | BLE device ID for the **reporting sensor**                                                                  |
| "deviceTime": 1597936969     | Timestamp from the gateway when it published this JSON message                                              |
| "lastUploadTime": 1597935821 | Reporting sensor timestamp when prior log was captured by the gateway                                       |
| "fwVersion": "00000600"      | Firmware Version of **reporting sensor**, Major: 00, Minor: 06, Revision: 0600 can be interpreted as v0.0.6 |
| "batteryLevel": 3040         | Battery voltage of reporting sensor in millivolts                                                           |
| "networkId": 65535           | Network ID of **reporting sensor**                                                                          |
| "entries": [                 | Array of CT Entries                                                                                         |
| "entryStart": 165            | Fixed value                                                                                                 |
| "flags": 253                 | Fixed value                                                                                                 |
| "scanInterval": 30           | Seconds between BLE scans for the **reporting sensor**                                                      |
| "serial": "01020304bbbb"     | Serial number of the _remote sensor_ detected in a BLE scan                                                 |
| "timestamp": 1597935576      | UTC timestamp (seconds since epoch) of the first log in this entry                                          |
| "length": 112                | Byte size of reported payload, only used for diagnostic purposes                                            |
| "logs": [                    | Array of Record object associated with this entry                                                           |
| "recordType": 17             | Indicates the format for this record object                                                                 |
| "log":                       | Log object                                                                                                  |
| "delta": 0                   | Delta in “scan intervals” after this entry’s timestamp that this log object was captured                    |
|                              | To calculate absolute timestamp, multiply the parent entry “scanInterval” value                             |
|                              | by this delta value and add to the parent entry “timestamp” value                                           |
|                              | to get seconds since epoch in UTC.                                                                          |
| "rssi": -44                  | "rssi": -44, RSSI measured by **reporting sensor** for BLE scan of _remote sensor_                          |
| "motion": 0                  | "motion": 0, 0: _remote sensor_ not in motion, 1: _remote sensor_ in motion                                 |
| "txPower": 0                 | "txPower": 0 TX power advertised by the _remote sensor_ during this BLE scan                                |

# CT Logging/Transfer Formats

Logs from CT tracker devices (BT510-CT and BT710) are transferred to a gateway using the SMP + CBOR protocol over Bluetooth Low Energy. Describing SMP and CBOR protocol are outside the scope of this document. This [document](ct_data_log_format.md) focuses on the byte-level payload format of the CT log data as it is sent from the tracker to the gateway (SMP) and from gateway to AWS (MQTT).

# CT Gateway RPC Overview

The Sentrius™ MG100-CT Gateway firmware implements an RPC mechanism providing system integrators with a means to send remote commands to trigger a set of maintenance operations. The device shadow is used as the mechanism to send RPC requests to the MG100-CT gateway and trigger operations that may perform maintenance tasks or post diagnostic data to designated MQTT topics. Details can be found [here.](ct_gateway_rpc.md)

# Building for Contact Tracing

> **Note:** If using [VS Code for development](development.md) the `build contact tracing with mcuboot` and `build contact tracing` tasks can be used to easily build the firmware for the MG100. For the BL5340, please follow the below steps.

The steps to build the Contact Tracing application are similar to the [Out-of-Box Demo](readme_aws.md#building-the-firmware).
However, the contact tracing build requires another configuration file.

```
# MG100

## Linux and macOS

cp ../modules/zephyr_lib/mcuboot_config/pm_static.pinnacle100.yml app/pm_static.yml

west build -b [board] -d ${PWD}/build/[board]/ct ${PWD}/app -- -DOVERLAY_CONFIG="${PWD}/app/contact_tracing/overlay_ct.conf ${PWD}/../modules/zephyr_lib/mcumgr_wrapper/config/overlay-mcuboot.conf" -Dmcuboot_DTC_OVERLAY_FILE=${PWD}/app/boards/pinnacle_100.overlay -Dmcuboot_CONF_FILE=${PWD}/../modules/zephyr_lib/mcuboot_config/pinnacle_100.conf

## Windows

copy ..\modules\zephyr_lib\mcuboot_config\pm_static.pinnacle100.yml app\pm_static.yml

west build -b [board] -d %CD%\build\[board]\ct %CD%\app -- -DOVERLAY_CONFIG="%CD%\app\contact_tracing\overlay_ct.conf %CD%\..\modules\zephyr_lib\mcumgr_wrapper\config\overlay-mcuboot.conf" -Dmcuboot_DTC_OVERLAY_FILE=%CD%\app\boards\pinnacle_100.overlay -Dmcuboot_CONF_FILE=%CD%\..\modules\zephyr_lib\mcuboot_config\pinnacle_100.conf

# BL5340

## Linux and macOS

cp app/boards/pm_static_bl5340_dvk.yml app/pm_static.yml

west build -b [board] -d ${PWD}/build/[board]/ct ${PWD}/app -- -DOVERLAY_CONFIG="${PWD}/app/contact_tracing/overlay_ct.conf ${PWD}/../modules/zephyr_lib/mcumgr_wrapper/config/overlay-mcuboot.conf" -Dmcuboot_CONF_FILE=${PWD}/../modules/zephyr_lib/mcuboot_config/bl5340.conf

## Windows

copy app\boards\pm_static_bl5340_dvk.yml app\pm_static.yml

west build -b [board] -d %CD%\build\[board]\ct %CD%\app -- -DOVERLAY_CONFIG="%CD%\app\contact_tracing\overlay_ct.conf %CD%\..\modules\zephyr_lib\mcumgr_wrapper\config\overlay-mcuboot.conf" -Dmcuboot_CONF_FILE=%CD%\..\modules\zephyr_lib\mcuboot_config\bl5340.conf
```
