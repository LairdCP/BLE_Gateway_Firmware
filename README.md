# MG100

The MG100 is designed to gather sensor data over BLE and transfer it to the cloud. It can operate in two modes:
* [LTE-M and AWS](#lte-m-and-aws)
* [NB-IoT and LwM2M](#nb-iot-and-lwm2m)

These two modes are selected at compile time. See the following sections for documentation on the demo and how it operates.

## Programming

MG100 units with version 2.0.0 on installed on it must be upgraded via JTAG. Flashing over the air is not yet supported. To do this please consult the [MG100 Hardware Guide](https://connectivity-staging.s3.us-east-2.amazonaws.com/2020-08/CS-USER_GUIDE-MicroGateway%20v1_0.pdf) section 5.4.4 to learn how to connect the J-Link debugger to the board.

MG100 units with version 3.x.y or greater support flashing over the air via mcumgr. 

## LTE-M and AWS

The default [build task](.vscode/tasks.json) is setup to build the demo source code for LTE-M and AWS operation. [Read here](docs/readme_ltem_aws.md) for details on how the demo operates.

## NB-IoT and LwM2M

The MG100 can be compiled to work with NB-IoT and LwM2M communication to the cloud with the `build-lwm2m` task in [tasks.json](.vscode/tasks.json).

For more details on the LwM2M demo, [read here](docs/readme_nbiot_lwm2m.md).

## Development

### Cloning and Building the Source

This is a Zephyr-based repository, **DO NOT** `git clone` this repo. To clone and build the project properly, please see the instructions in the [MG100_firmware_manifest](https://github.com/LairdCP/MG100_firmware_manifest) repository.

### BLE Profiles

Details on the BLE profiles used to interface with the mobile app can be found [here](docs/ble.md)

### Development and Debug

See [here](docs/development.md) for details on developing and debugging this app.
