# BLE Services and information used in the AWS OOB Demo

## Advertisement

The advertisement includes the UUID of the Cellular Profile. The complete local name is included in the scan response.
The complete local name for an MG100 is `MG100 OOB-1234567`, where `1234567` are replaced with the last 7 digits of the IMEI.
For a DVK the complete local name will look like `Pinnacle 100 OOB-1234567`.

## Device Information Service

### UUID: 180a

Characteristics:

| Name              | UUID | Properties | Description                           |
| ----------------- | ---- | ---------- | ------------------------------------- |
| Model Number      | 2a24 | read       | Model number of the device (string)   |
| Firmware Revision | 2a26 | read       | Zephyr RTOS version (string)          |
| Software Revision | 2a28 | read       | OOB demo application version (string) |
| Manufacturer      | 2a29 | read       | Manufacturer (string)                 |

## AWS Provisioning Profile

### UUID: ae7203f0-55a9-4a14-bcd7-7c59f234a9b5

Characteristics:

| Name         | UUID                                 | Properties  | Description                                                                                                                                                                                                              |
| ------------ | ------------------------------------ | ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Client ID    | ae7203f1-55a9-4a14-bcd7-7c59f234a9b5 | read/write  | Up to 32 bytes of ASCII AWS client ID. This is used for the MQTT connection and as the name used in the device shadow and sensor data MQTT topics.                                                                       |
| Endpoint     | ae7203f2-55a9-4a14-bcd7-7c59f234a9b5 | read/write  | Up to 256 bytes of ASCII AWS endpoint hostname. This is the server that the device will attempt to establish an MQTT connection with.                                                                                    |
| Root CA Cert | ae7203f3-55a9-4a14-bcd7-7c59f234a9b5 | read/write  | Up to 2048 bytes of ASCII PEM root CA certificate. See note below about long characteristics.                                                                                                                            |
| Client Cert  | ae7203f4-55a9-4a14-bcd7-7c59f234a9b5 | read/write  | Up to 2048 bytes of ASCII PEM client certificate. See note below about long characteristics.                                                                                                                             |
| Client Key   | ae7203f5-55a9-4a14-bcd7-7c59f234a9b5 | read/write  | Up to 2048 bytes of ASCII PEM private key corresponding to the client certificate. See note below about long characteristics.                                                                                            |
| Save/Clear   | ae7203f6-55a9-4a14-bcd7-7c59f234a9b5 | write       | One byte. Writing a value of 1 will cause any data written to the above characteristics to be stored in non-volatile memory. Writing a value of 2 will cause all of the above data to be cleared in non-volatile memory. |
| Status       | ae7203f7-55a9-4a14-bcd7-7c59f234a9b5 | read/notify | One byte representing the current status of the AWS IoT connection: 0 – Not Provisioned, 1 – Disconnected, 2 – Connected, 3 – Connection Error, 4 - Connecting                                                           |

For the three characteristics that store large amounts of data (two certificates and the private key), a solution is needed to permit writes to the characteristic of more than the 512 bytes allowed by BLE. The solution implemented here is to add a four-byte (unsigned 32-bit integer, little-endian byte order) offset field to the start of the characteristic write data.
Though not technically required to be written in order, the procedure for writing one of these long characteristics is shown below. The assumption is made that writes are done in 64 bytes chunks.

1. Write 0x00 0x00 0x00 0x00 followed by the first 64 bytes of the certificate/key
2. Write 0x40 0x00 0x00 0x00 followed by the next 64 bytes
3. Write 0x80 0x00 0x00 0x00 followed by the next 64 bytes
4. Repeating as above until all of the data is written

Reads of the same characteristics will always return a 32-byte SHA256 hash of the contents of the characteristic. This allows for the verification that certificates and keys were programmed correctly with a high degree of certainty without having to allow the characteristic to be read. Allowing future reads of the private key are especially undesirable.

## Cellular Profile

### UUID: 43787c60-9e84-4eb1-a669-70b6404da336

Characteristics:

| Name             | UUID                                 | Properties        | Description                                                                                                                                                                       |
| ---------------- | ------------------------------------ | ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| IMEI             | 43787c61-9e84-4eb1-a669-70b6404da336 | read              | 15-digit ASCII string representing the IMEI                                                                                                                                       |
| APN              | 43787c62-9e84-4eb1-a669-70b6404da336 | read/write/notify | ASCII string representing the APN (63 characters max). Use an empty string if the APN isn't required.                                                                             |
| APN Username     | 43787c63-9e84-4eb1-a669-70b6404da336 | read/notify       | ASCII string representing the APN username (64 characters max).                                                                                                                   |
| APN Password     | 43787c64-9e84-4eb1-a669-70b6404da336 | read/notify       | ASCII string representing the APN password (64 characters max).                                                                                                                   |
| Network State    | 43787c65-9e84-4eb1-a669-70b6404da336 | read/notify       | One byte. Network state: 0 - Not registered, 1 - Home network, 2 - Searching, 3 - Registration denied, 4 - Out of Coverage, 5 - Roaming, 8 - Emergency, 240 - Unable to configure |
| Firmware Version | 43787c66-9e84-4eb1-a669-70b6404da336 | read              | Firmware version of the LTE modem.                                                                                                                                                |
| Startup State    | 43787c67-9e84-4eb1-a669-70b6404da336 | read/notify       | One byte. Modem startup state: 0 - Ready, 1 - Waiting for access code, 2 - SIM not preset, 3 - Simlock, 4 - Unrecoverable error, 5 - Unknown, 6 - Inactive SIM                    |
| RSSI             | 43787c68-9e84-4eb1-a669-70b6404da336 | read/notify       | Signed 32-bit integer. Reference Signals Receive Power (RSRP) in dBm.                                                                                                             |
| SINR             | 43787c69-9e84-4eb1-a669-70b6404da336 | read/notify       | Signed 32-bit integer. Signal to Interference plus Noise Ratio (SINR) in dBm                                                                                                      |
| Sleep State      | 43787c6a-9e84-4eb1-a669-70b6404da336 | read/notify       | One byte representing sleep state of driver (0 - Uninitialized, 1 - Asleep, 2 - Awake)                                                                                            |
| RAT              | 43787c6b-9e84-4eb1-a669-70b6404da336 | read/write/notify | One byte for Radio Access Technology: 0 - CAT M1, 1 = CAT NB1                                                                                                                     |
| ICCID            | 43787c6c-9e84-4eb1-a669-70b6404da336 | read              | 20-digit ASCII string                                                                                                                                                             |
| Serial Number    | 43787c6d-9e84-4eb1-a669-70b6404da336 | read              | 14 character ASCII string                                                                                                                                                         |
| Bands            | 43787c6e-9e84-4eb1-a669-70b6404da336 | read              | 20 character ASCII string representing LTE band configuration. See section 5.19 of HL7800 AT command guide for more information.                                                  |
| Active Bands     | 43787c6f-9e84-4eb1-a669-70b6404da336 | read/notify       | 20 character ASCII string representing the Active LTE band configuration.                                                                                                         |

## Power Profile

### UUID: dc1c0000-f3d7-559e-f24e-78fb67b2b7eb

Characteristics:

| Name                 | UUID                                 | Properties | Description                                                                                                                                                      |
| -------------------- | ------------------------------------ | ---------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Power supply voltage | dc1c0001-f3d7-559e-f24e-78fb67b2b7eb | notify     | Two bytes. Byte 0 is the integer part of the voltage and byte 1 is the decimal part of the voltage                                                               |
| Reboot               | dc1c0002-f3d7-559e-f24e-78fb67b2b7eb | write      | One bytes. Writing to this will reboot the module, writing a value of 0x01 will stay in the UART bootloader, any other value will reboot to the user application |

## Battery Profile

> **Note:** Only available on an MG100

### UUID: 6d4a06b0-9641-11ea-ab12-0800200c9a66

Characteristics:

| Name                    | UUID                                 | Properties  | Description                                                                                                                                                                            |
| ----------------------- | ------------------------------------ | ----------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Battery Voltage         | 6d4a06b1-9641-11ea-ab12-0800200c9a66 | notify      | unsigned 16-bit value representing the voltage in millivolts                                                                                                                           |
| Battery Capacity        | 6d4a06b2-9641-11ea-ab12-0800200c9a66 | read/notify | unsigned 8-bit value representing the remaining battery capacity (0 - 4).                                                                                                              |
| Charging State          | 6d4a06b3-9641-11ea-ab12-0800200c9a66 | read/notify | 8-bit bit field representing the charging state. Bit 0 = External Power State, Bit 1 = The battery is charging, Bit 2 = The battery is not charging, Bit 3 The battery is discharging. |
| Battery Low Threshold   | 6d4a06b4-9641-11ea-ab12-0800200c9a66 | read/write  | unsigned 16-bit value representing the low battery threshold in millivolts.                                                                                                            |
| Battery Alarm Threshold | 6d4a06b5-9641-11ea-ab12-0800200c9a66 | read/write  | unsigned 16-bit value representing the low battery threshold in millivolts.                                                                                                            |
| Battery Threshold 4     | 6d4a06b6-9641-11ea-ab12-0800200c9a66 | read/write  | unsigned 16-bit value representing the highest threshold in millivolts for a 4-segment charging meter.                                                                                 |
| Battery Threshold 3     | 6d4a06b7-9641-11ea-ab12-0800200c9a66 | read/write  | unsigned 16-bit value representing a threshold in millivolts for a 4-segment charging meter.                                                                                           |
| Battery Threshold 2     | 6d4a06b8-9641-11ea-ab12-0800200c9a66 | read/write  | unsigned 16-bit value representing a threshold in millivolts for a 4-segment charging meter.                                                                                           |
| Battery Threshold 1     | 6d4a06b9-9641-11ea-ab12-0800200c9a66 | read/write  | unsigned 16-bit value representing a threshold in millivolts for a 4-segment charging meter.                                                                                           |
| Battery Threshold 0     | 6d4a06ba-9641-11ea-ab12-0800200c9a66 | read/write  | unsigned 16-bit value representing the lowest threshold in millivolts for a 4-segment charging meter.                                                                                  |
| Battery Low Alarm       | 6d4a06bb-9641-11ea-ab12-0800200c9a66 | notify      | unsigned 8-bit value indicating the low battery alarm state. 0 = no alarm, 1 = alarm.                                                                                                  |

## Motion Profile

> **Note:** Only available on an MG100

### UUID: adce0a30-ac1a-11ea-8b6e-0800200c9a66

Characteristics:

| Name         | UUID                                 | Properties | Description                                       |
| ------------ | ------------------------------------ | ---------- | ------------------------------------------------- |
| Motion Alarm | adce0a31-ac1a-11ea-8b6e-0800200c9a66 | notify     | One Byte. Motion State: 1 - Motion, 0 - No Motion |

## Sensor Profile

### UUID: ab010000-5bab-471a-9074-a0ae3937c70c

Characteristics:

| Name                     | UUID                                 | Properties  | Description                                                                                                                                                                                                     |
| ------------------------ | ------------------------------------ | ----------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Sensor State             | ab010001-5bab-471a-9074-a0ae3937c70c | read/notify | One Byte. Sensor State: 0 - Finding device, 1 - Finding service, 2 - Finding Temperature Characteristic, 3 - Finding Humidity Characteristic, 4 - Finding pressure characteristic, 5 - Connected and Configured |
| Sensor Bluetooth Address | ab010002-5bab-471a-9074-a0ae3937c70c | read/notify | String representation of address of connected sensor. sensor.                                                                                                                                                   |

## LwM2M Client Configuration Profile

This service is only available for the LwM2M example.

### UUID: 07fd0000-d320-768c-364a-c405518f724c

Characteristics:

| Name       | UUID                                 | Properties | Description                                                                  |
| ---------- | ------------------------------------ | ---------- | ---------------------------------------------------------------------------- |
| Generate   | 07fd0001-d320-768c-364a-c405518f724c | write      | One Byte. Write zero to set to defaults. Write non-zero to generate new PSK. |
| Client PSK | 07fd0002-d320-768c-364a-c405518f724c | read       | 16 bytes. Private shared key used to talk to Leshan server.                  |
| Client ID  | 07fd0003-d320-768c-364a-c405518f724c | read/write | Maximum of a 32 character string. Unique ID associated with PSK.             |
| Peer URL   | 07fd0004-d320-768c-364a-c405518f724c | read/write | Maximum of a 128 character string. URL of Leshan server.                     |

## FOTA Profile

### UUID: 3e12f000-0a2a-32tb-2b85-8349747c5745

Characteristics:

| Name          | UUID                                 | Properties        | Description                                                                                                                                                                                                                                                                |
| ------------- | ------------------------------------ | ----------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Control Point | 3e12f001-0a2a-324b-2685-8349747c5745 | read/write/notify | One byte for controlling the FOTA service. 0 - NOP, 1 - List Files, 2 - Modem Start, 3 - Delete Files, 4 - Compute SHA256                                                                                                                                                  |
| Status        | 3e12f002-0a2a-324b-2685-8349747c5745 | read/notify       | Integer. A negative value - indicates system error code, 0 - Success, 1 - Busy, 2 - Unspecific error code. Any value other than 1 can be considered Idle. Busy will always be notified. Any subsequent commands issued before a success or error response will be ignored. |
| Count         | 3e12f003-0a2a-324b-2685-8349747c5745 | read/notify       | The number of bytes that have been transferred in the current FOTA update. This may be larger than the size if data is padded during transfer.                                                                                                                             |
| Size          | 3e12f004-0a2a-324b-2685-8349747c5745 | read/notify       | The size of the file in bytes.                                                                                                                                                                                                                                             |
| File Name     | 3e12f005-0a2a-324b-2685-8349747c5745 | read/write/notify | The file name of the current operation. File names are pattern matched. An empty string will match all files. The filesystem is not traversed.                                                                                                                             |
| Hash          | 3e12f006-0a2a-324b-2685-8349747c5745 | read/notify       | The 32-byte SHA256 hash of the current file.                                                                                                                                                                                                                               |
