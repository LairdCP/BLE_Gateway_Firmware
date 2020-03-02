# BLE Services and information used in the Pinnacle 100 OOB Demo

## Advertisement

The advertisement includes the UUID of the Cellular Profile. The complete local name is included in the scan response.
The complete local name is "Pinnacle 100 OOB-1234567", where "1234567" are replaced with the last 7 digits of the IMEI.

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
| Status       | ae7203f7-55a9-4a14-bcd7-7c59f234a9b5 | read/notify | One byte representing the current status of the AWS IoT connection: 0 – Not Provisioned, 1 – Disconnected, 2 – Connected, 3 – Connection Error                                                                           |

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

| Name             | UUID                                 | Properties        | Description                                                                                                                                                                                                                                              |
| ---------------- | ------------------------------------ | ----------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| IMEI             | 43787c61-9e84-4eb1-a669-70b6404da336 | read              | 15-digit ASCII string representing the IMEI                                                                                                                                                                                                              |
| APN              | 43787c62-9e84-4eb1-a669-70b6404da336 | read/write/notify | ASCII string representing the APN (63 characters max). Use an empty string if the APN isn't required.                                                                                                                                                    |
| APN Username     | 43787c63-9e84-4eb1-a669-70b6404da336 | read/notify       | ASCII string representing the APN username (64 characters max).                                                                                                                                                                                          |
| APN Password     | 43787c64-9e84-4eb1-a669-70b6404da336 | read/notify       | ASCII string representing the APN password (64 characters max).                                                                                                                                                                                          |
| Network State    | 43787c65-9e84-4eb1-a669-70b6404da336 | read/notify       | One byte. Network state: 0 - Not registered, 1 - Home network, 2 - Searching, 3 - Registration denied, 4 - Out of Coverage, 5 - Roaming, 8 - Emergency, 240 - Unable to configure, 241 - Out of Coverage Network Up, 242 - Out of Coverage Network Down  |
| Firmware Version | 43787c66-9e84-4eb1-a669-70b6404da336 | read              | Firmware version of the LTE modem.                                                                                                                                                                                                                       |
| Startup State    | 43787c67-9e84-4eb1-a669-70b6404da336 | read/notify       | One byte. Modem startup state: 0 - Ready, 1 - Waiting for access code, 2 - SIM not preset, 3 - Simlock, 4 - Unrecoverable error, 5 - Unknown, 6 - Inactive SIM                                                                                           |
| RSSI             | 43787c68-9e84-4eb1-a669-70b6404da336 | read/notify       | Signed 32-bit integer. Reference Signals Receive Power (RSRP) in dBm.                                                                                                                                                                                    |
| SINR             | 43787c69-9e84-4eb1-a669-70b6404da336 | read/notify       | Signed 32-bit integer. Signal to Interference plus Noise Ratio (SINR) in dBm                                                                                                                                                                             |
| Sleep State      | 43787c6a-9e84-4eb1-a669-70b6404da336 | read/notify       | One byte representing sleep state of driver (0 - Uninitialized, 1 - Asleep, 2 - Waking, 3 - Awake)                                                                                                                                                       |
| RAT              | 43787c6b-9e84-4eb1-a669-70b6404da336 | read/write/notify | One byte for Radio Access Technology: 0 - CAT M1, 1 = CAT NB1                                                                                                                                                                                            |
| ICCID            | 43787c6c-9e84-4eb1-a669-70b6404da336 | read              | 20-digit ASCII string                                                                                                                                                                                                                                    |

## Sensor Profile

### UUID: ab010000-5bab-471a-9074-a0ae3937c70c

Characteristics:

| Name                     | UUID                                 | Properties  | Description                                                                                                                                                                                                               |
| ------------------------ | ------------------------------------ | ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Sensor State             | ab010001-5bab-471a-9074-a0ae3937c70c | read/notify | One Byte. BME280 Sensor State: 0 - Finding device, 1 - Finding service, 2 - Finding Temperature Characteristic, 3 - Finding Humidity Characteristic, 4 - Finding pressure characteristic, 5 - Connected and Configured    |
| Sensor Bluetooth Address | ab010002-5bab-471a-9074-a0ae3937c70c | read/notify | String representation of address of connected BME280 sensor.                                                                                                                                                              |
