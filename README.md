
[![Laird Connectivity](docs/images/LairdConnnectivityLogo_Horizontal_RGB.png)](https://www.lairdconnect.com/)
# Pinnacle 100 Modem and MG100 Firmware
[![Pinnacle 100 Modem](docs/images/pinnacle_100_modem.png)](https://www.lairdconnect.com/wireless-modules/cellular-solutions/pinnacle-100-cellular-modem) [![Pinnacle 100 DVK](docs/images/450-00010-K1-Contents_0.jpg)](https://www.lairdconnect.com/wireless-modules/cellular-solutions/pinnacle-100-cellular-modem) [![MG100](docs/images/MG100-Starter-Kit.png)](https://www.lairdconnect.com/iot-devices/iot-gateways/sentrius-mg100-gateway-lte-mnb-iot-and-bluetooth-5)

The Pinnacle 100 modem can be purchased separately and is also embedded into the Sentrius MG100 Gateway. The MG100 is an out-of-the-box product allowing the end user to develop a fully featured IoT solution with minimum effort. With the addition of the optional battery backup, it provides uninterrupted reporting of sensor data. Additionally the sensor data is logged locally on an SD card to ensure data is captured even if the LTE connection is interrupted. Based on Laird Connectivity's Pinnacle 100 modem, the Sentrius MG100 Gateway captures data from Bluetooth 5 sensors and sends it to the cloud via a global low power cellular (LTE-M/NB-IoT) connection. It is based on the innovative integration of Nordic Semiconductor nRF52840 and the Sierra Wireless HL7800 module. This enables the MG100 hardware to support LTE-M/NB-IoT (supports LTE bands 1, 2, 3, 4, 5, 12, 13, 20, and 28) as well as Bluetooth 5 features like CODED PHY, 2M PHY, and LE Advertising Extensions. A version of the MG100 firmware can be built to support contact tracing with Laird Connectivity BLE sensor products.

The firmware can also be built for the standalone Pinnacle 100 modem used in the DVK. This is designed to showcase gathering sensor data over BLE and transferring it to the cloud.

>**Note:** This readme file and associated documentation should be viewed on GitHub selecting the desired branch. The main branch will always be up to date with the latest features. Viewing documentation from a release GA branch is recommended to get documentation for the specific feature set of that release.

This repository contains firmware that can run on the Pinnacle 100 Modem development kit (DVK) or the Sentrius MG100 gateway.

The firmware can operate in three modes:
* [LTE-M and AWS](#lte-m-and-aws)
* [LTE-M, AWS, and Contact Tracing](#lte-m-aws-and-contact-tracing)
* [NB-IoT and LwM2M](#nb-iot-and-lwm2m)

These two modes are selected at compile time. See the following sections for documentation on the firmware and how it operates.

Download firmware releases from [here!](https://github.com/LairdCP/Pinnacle_100_firmware/releases)

## LTE-M and AWS

The default [build with mcuboot task](.vscode/tasks.json) is setup to build the firmware source code for LTE-M and AWS operation. [Read here](docs/readme_ltem_aws.md) for details on how the firmware operates.

## LTE-M, AWS, and Contact Tracing
This configuration [build ct with mcuboot task](.vscode/tasks.json) builds Laird Connectivity's Contact Tracing application. 

## NB-IoT and LwM2M

The firmware can be compiled to work with NB-IoT and LwM2M communication to the cloud with the `build lwm2m` task in [tasks.json](.vscode/tasks.json).

For more details on the LwM2M firmware, [read here](docs/readme_nbiot_lwm2m.md).

## Firmware Updates

If the Pinnacle 100 device is running v2.0.0 firmware or earlier, firmware updates must be programmed via SWD(Serial Wire Debug). To do this please consult:
* MG100: the MG100 Hardware Guide section 5.4.4 to learn how to connect a J-Link debugger to the board.
* Pinnacle 100 DVK: The DVK has a built in debugger to easily program firmware.

Pinnacle 100 devices with firmware version 3.x or greater support firmware updates via UART, BLE or LTE.

To update firmware with the Pinnacle Connect mobile app or via the cloud [see here.](docs/readme_ltem_aws.md#firmware-updates)

To update firmware over UART using the mcumgr CLI [see here.](docs/firmware_update.md)

To update the firmware over LTE directly from AWS [see here.](docs/cloud_fota.md)

## Development

### Cloning and Building the Source

This is a Zephyr-based repository, **DO NOT** `git clone` this repo. To clone and build the project properly, please see the instructions in the [Pinnacle_100_firmware_manifest](https://github.com/LairdCP/Pinnacle_100_firmware_manifest) repository.

### BLE Profiles

Details on the BLE profiles used to interface with the mobile app can be found [here](docs/ble.md)

### Development and Debug

See [here](docs/development.md) for details on developing and debugging this app.
