# BLE Services and information used in the AWS OOB Demo

## Advertisement

The advertisement includes the UUID of the SMP Profile. The complete local name is included in the scan response.
The complete local name for an MG100 is `MG100 OOB-1234567`, where `1234567` are replaced with the last 7 digits of the IMEI.
For a Pinnacle 100 DVK the complete local name will look like `Pinnacle 100 OOB-1234567`.
The complete local name for a BL5340 DVK is `BL5340 OOB-1234567`, where `1234567` are replaced with the last 7 digits of the BLE address.

## Device Information Service

### UUID: 180a

Characteristics:

| Name              | UUID | Properties | Description                           |
| ----------------- | ---- | ---------- | ------------------------------------- |
| Model Number      | 2a24 | read       | Model number of the device (string)   |
| Firmware Revision | 2a26 | read       | Zephyr RTOS version (string)          |
| Software Revision | 2a28 | read       | OOB demo application version (string) |
| Manufacturer      | 2a29 | read       | Manufacturer (string)                 |

## SMP Service

### UUID: 8d53dc1d-1db7-4cd3-868b-8a527460aa84

Characteristics:

| Name | UUID                                 | Properties   | Description                                                                                                                                                                                                              |
| -----| ------------------------------------ | ------------ | ---------------------------------------------------------------------------------------- |
| SMP  | da2e7828-fbce-4e01-ae9e-261174997c48 | write/notify | Implements the SMP (Simple Management Protocol) system for communicating with the module |
