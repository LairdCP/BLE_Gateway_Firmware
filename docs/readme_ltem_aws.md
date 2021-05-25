# AWS Out-of-Box (OOB) Demo

## Table of Contents

1. **[Introduction](#introduction)**
2. **[Resources](#resources)**
3. **[Prerequisites](#prerequisites)**
4. **[Setup](#setup)**
5. **[Using the Demo](#using-the-demo)**  
   [Signup and Login](#signup-and-login)  
   [Scan for Devices](#scan-for-devices)  
   [Home Page](#home-page)  
   [Provision](#provision)  
   [Device Information](#device-information)  
   [MG100 Settings](#mg100-settings)  
   [Cellular Settings](#cellular-settings)  
   [Power Settings](#power-settings)  
   [Firmware Updates](#firmware-updates)
6. **[Cloud Data](#cloud-data)**  
   [BT510 Sensor Data](#bt510-sensor-data)
7. **[LED Behavior](#led-behavior)**
8. **[My Own AWS Connection](#my-own-aws-connection)**
9. **[Building the Firmware](#building-the-firmware)**
10. **[Development](#development)**

## Introduction

The firmware in the AWS configuration gathers sensor data over BLE and sends the data to the cloud (AWS) via LTE-M.

One sensor that can be used is the BL654 sensor. This sensor ships with the development kit (DVK) and measures temperature, humidity, and pressure.

Another supported sensor is the BT510. It records temperature and movement. The BT510 can also be configured to detect a door opened/closed.

The Pinnacle 100 device scans for the BL654 sensor and connects to the first one it finds. At the same time, the device gathers data for the BT510 sensors from advertisements without creating a connection.
The demo supports one BL654 sensor and up to five BT510 sensors. The demo can be recompiled to remove support for either sensor.

Using the Laird Pinnacle Connect mobile app, the user can provision the Pinnacle 100 device to connect to AWS. Once connected to AWS, the device sends sensor data (if a sensor is found) to the cloud every 60 seconds.
Once a BT510 is discovered and enabled via the web portal, its sensor data is reported. The details of the BT510 data reporting are detailed [below](#bt510-sensor-data).

```
                XXXXX
              XXX   XXX
    XXXXX   XX        XX
 XXX    XX XX          X XXXXXXX
 X        XX            X       XX
X                                X
 X         AWS Cloud              X
 XX                              X
  XXXXXXX                       XX
        XX          XXXX    XXXX
         XXX      XXX   XXXX
           XXXXXXX
               ^
               +
              LTE-M (MQTT with TLS)
               +
               v
     +---------+----------+
     |                    |
     |      DVK/MG100     |
     |                    |
     +---------+----------+
               ^
               +
              BLE
               +-------------------------------+
               v                               v
    +----------+------------+      +-----------+-----------+
    |                       |      |                       |
    |  BL654 Sensor         |      |  BT510 Sensor (0-5)   |
    |        (0-1)          |      |                       |
    +-----------------------+      +-----------------------+

```

## Resources

- [MG100 product page](https://www.lairdconnect.com/iot-devices/iot-gateways/sentrius-mg100-gateway-lte-mnb-iot-and-bluetooth-5)
- [Pinnacle 100 Modem product page](https://www.lairdconnect.com/wireless-modules/cellular-solutions/pinnacle-100-cellular-modem)
- [BT510 Product page](https://www.lairdconnect.com/iot-devices/iot-sensors/bt510-bluetooth-5-long-range-ip67-multi-sensor)

## Prerequisites

The following are required to fully exercise this demo firmware:

- An activated SIM card. See [here](https://www.lairdconnect.com/documentation/truphone-sim-setup-guide) for instructions on activating the SIM card that came with your kit.
- DVK or MG100 programmed with the latest LTE-M/AWS firmware. Releases available [here!](https://github.com/LairdCP/Pinnacle_100_firmware/releases)
- Laird Pinnacle Connect app installed on a mobile device. An active internet connection on the mobile device is required.
  - [Android app](http://play.google.com/store/apps/details?id=com.lairdconnect.pinnacle.connect)
  - [iOS app](https://apps.apple.com/us/app/laird-pinnacle-connect/id1481075861?ls=1)

## Setup

To set up the Pinnacle 100 device, follow these steps:

1. Ensure an _activated_ SIM card is inserted into the Pinnacle 100 SIM slot.
2. On your phone, launch the Pinnacle mobile app and follow the on-screen prompts.

## Using the Demo

### Signup and Login

If you do not have an account, you must create one. Use an email address for the username.

![Sign-up screen](images/signup.png) ![Login screen](images/login.png)  
_Sign-up and Login screens_

### Scan for Devices

Once logged in, the scan screen displays.

![Scan screen](images/scan.png)  
_Scan screen_

To scan for devices, follow these steps:

1. Click **Find Pinnacle Devices**. Discovered devices are displayed.
   > **Note:** The device name contains the last seven digits of the IMEI of the device so that you can easily identify the device to which you wish to connect. The IMEI is printed on the label on the bottom of the MG100 housing or on the Pinnacle 100 Modem label.

![Scan screen - devices found](images/devices_found.png)  
_Scan screen - devices found_

2. Click on the desired device to connect to it.

### Home Page

The home screen displays status information related to the OOB demo. From the home page you can commission or decommission the device and navigate to other settings or info pages.

> **Note:** Provisioning is only allowed if the device is in an un-provisioned state.

![Home screen](images/mg100_home.png)  
_Home screen_

### Provision

To provision the device click **Provision**.
During provisioning, certificates are generated for the device and then programmed into the device over BLE.

![Provision in process](images/provision_progress.png)  
_Provision in process_

Once sending provisioning data is complete, a prompt displays and you are directed back to the information page.

![Provisioning is complete](images/provision_success.png)  
_Provisioning is complete_

The provisioning data is committed to non-volatile memory in the Pinnacle 100 device.

![Device commissioned and connected to AWS](images/provisioned_and_commissioned.png)  
_Device commissioned and connected to AWS_

> **Note:** You can decommission a device if you no longer want it to connect to AWS. Just click **Decommission**

![Device decommissioned](images/decommissioned.png)  
_Device decommissioned_

### Device Information

The device information page displays relevant version information of the connected device.

![Device information](images/device_info.png)  
_Device info_

### MG100 Settings

> **Note:** Only available if you are connected to an MG100.

All MG100 variants have an onboard accelerometer. It is currently configured to detect movement on the gateway and report that over BLE or to the cloud via the shadow. The MG100 application will send a notification when motion is detected and another notification 30 seconds after movement has stopped. Currently, no accelerometer parameters are configurable. Subsequent versions of the MG100 firmware will allow some configuration of the accelerometer via the shadow. This view shows the status of the motion sensor.

![MG100 Settings](images/mg100_settings.png)  
_MG100 settings_

### Cellular Settings

The cellular settings page displays status information related to the cellular radio and allows the user to change the APN.

![Cellular settings](images/cell_settings.png)  
_Cell settings_

### Power Settings

The power settings page displays the battery voltage and allows the user to reboot the modem. MG100 variants, 450-00038-K1 and 450-00054-K1 are equipped with a rechargeable Li-ion battery used for backup power when external power is lost. The battery will automatically start charging again once external power is restored and the the battery is not fully charged. The firmware monitors the battery voltage and temperature to provide remaining capacity left in the battery. This data is sent over BLE or cellular to report voltage and remaining battery capacity. The battery thresholds are defaulted to reasonable values, but they can be modified via BLE or the shadow if customization is desired. The threshold defaults are as follows. The lowest the battery is allowed to discharge to is 2.75V before the under-voltage protection hardware kicks in.

Battery Threshold 4 - 4200 mV  
Battery Threshold 3 - 3800 mV  
Battery Threshold 2 - 3400 mV  
Battery Threshold 1 - 3000 mV

These thresholds are designed to provide approximately equal discharge time between thresholds 4-3 and 3-2. Below threshold 2, the amount of time left before the battery discharges completely, drops off quickly. This behavior can vary depending on conditions and battery health.

This view displays the battery information. Note the MG100 does not allow for measuring the voltage of the external power supply like the Pinnacle 100 DVK. That is why power supply voltage is always shown as -1 for MG100 devices.

![Power settings](images/mg100_power_settings.png)  
_Power settings_

### Firmware Updates

If the Pinnacle 100 device is running firmware 3.0.0 or greater, firmware updates will be available in the mobile app or via the web UI. In LWM2M mode, firmware update can only be done using the mobile app (Bluetooth).

#### FOTA Updates via BLE

After connecting to the device with the mobile app, click Device Firmware Upgrade.

The firmware upgrade view will fetch any available updates from the cloud. Selecting a file will enable the update button, clicking the update button will start the firmware update.

When a firmware file is selected, a download icon will appear on the file to indicate that the file is currently in the cloud and not saved on the mobile device. You can click the download button to save it to the mobile device.
The firmware update can be started without downloading the file first. During the update the file will automatically download and save to the mobile device.

![Cloud file](images/fota_cloud_file.png)  
_Cloud file_

If the selected file is local on the mobile device, a delete button will be displayed. You can click it to remove the file from the mobile device.

![Local file](images/fota_local_file.png)  
_Local file_

#### FOTA Updates via the Cloud

After the device has been commissioned and connected to the cloud, it can receive firmware updates from the cloud.

![Cloud FOTA](images/bluegrass_fota.png)  
_Cloud FOTA_

The application firmware or HL7800 cellular firmware can be scheduled for update. Each firmware update can be scheduled when to download the image to the Pinnacle 100 device, and when to apply the update (switchover date).

## Cloud Data

Once the Pinnacle 100 device is commissioned with the mobile app and it is connected to AWS, the user can log into the web portal to view sensor data.

The web portal is located at: <https://demo.lairdconnect.com>

Log in with the same credentials used to login to the mobile app.

Once logged in go to the devices page to see devices that have been added to the user account.

![Web portal devices page](images/device_portal.png)  
_Web portal devices page_

Each device that is added is identified by its IMEI.

Click on the device ID to display its data.

![Pinnacle 100 data](images/pinnacle_100_data.png)  
_Pinnacle 100 data_

If a BL654 sensor is discovered, the Pinnacle device connects and reports sensor data every 60 seconds. The graph only displays live data that is logged while viewing the webpage. If the user leaves the web page and returns, the graph starts over with the most recent data that was received when returning to the page.

### BT510 Sensor Data

If a BT510 sensor is discovered, it is displayed in the Sensor Network list.

![BT510 discovered](images/bt510_discovered.png)  
_BT510 discovered_

In order to receive sensor readings from a BT510 sensor, the sensor must be enabled via the web portal. Click the check box and then hit Submit to enable the sensor.

![BT510 enabling](images/enabling_bt510.png)  
_BT510 enabling_

![BT510 enabled](images/enabled_bt510.png)  
_BT510 enabled_

Once enabled, it can take up to 30 seconds for the sensor to subscribe. Once subscribed, the sensor begins to send data.

![BT510 subscribed](images/bt510_subscribed.png)  
_BT510 subscribed_

By default a BT510 sensor reports temperature data every two minutes, battery data once an hour, and movement and button presses immediately.

Click on the BT510 sensor name to view the details and settings of the sensor.

![BT510 graph](images/bt510_graph.png)  
_BT510 graph_

![BT510 parameters](images/bt510_parameters.png)  
_BT510 parameters_

![BT510 log](images/bt510_log.png)  
_BT510 log_

## LED Behavior

The Blue LED blinks once a second when the Pinnacle device is searching for a BL654 sensor. When it finds a sensor and successfully connects to it, the LED remains on.

The Green LED turns on when connected to AWS. When data is sent to AWS the LED turns off and then turns back on. When disconnected from AWS, the LED remains off.

The Red LED blinks when the device is searching for a cellular network. It remains on and does not blink when connected to a network. If there is an error with the SIM card or network registration, then the LED remains off.

> **Note:** Before to version 3.x of the firmware, Red LED performed the Blue LED function. Blue LED performed the Green LED function. Green LED performed the Red LED function.

## My Own AWS Connection

If you wish to connect to your own AWS instance, [See here](aws_iot.md) for instructions.

## Building the Firmware

The firmware can be built to work with or without the mcuboot bootloader. Building without mcuboot is faster and easier for development and debug, but gives up the ability to update the Zephyr app via UART or BLE.

Issue these commands **from the pinnacle_100_firmware directory**.

> **WARNING:** If using windows, checkout code to a path of 12 characters or less to avoid build issues related to path length limits.

Build without mcuboot:

> **Note:** `[board]` should be replaced with `mg100` or `pinnacle_100_dvk`

```
# Linux and macOS

rm -f app/pm_static.yml && west build -b [board] -d build/[board]/aws app

# Windows

del app\pm_static.yml && west build -b [board] -d build\[board]\aws app
```

> **Note:** When switching between builds with or without mcuboot, be sure to delete the build directory before building.

Build with mcuboot:

> **Note:** `[board]` should be replaced with `mg100` or `pinnacle_100_dvk`

```
# Linux and macOS

cp ../modules/zephyr_lib/mcuboot_config/pm_static.pinnacle100.yml app/pm_static.yml

west build -b [board] -d ${PWD}/build/[board]/aws ${PWD}/app -- -DOVERLAY_CONFIG=${PWD}/../modules/zephyr_lib/mcumgr_wrapper/config/overlay-mcuboot.conf -Dmcuboot_DTC_OVERLAY_FILE="${PWD}/app/boards/pinnacle_100.overlay" -Dmcuboot_CONF_FILE=${PWD}/../modules/zephyr_lib/mcuboot_config/pinnacle_100.conf

# Windows

copy ..\modules\zephyr_lib\mcuboot_config\pm_static.pinnacle100.yml app\pm_static.yml

west build -b [board] -d %CD%\build\[board]\aws %CD%\app -- -DOVERLAY_CONFIG=%CD%\..\modules\zephyr_lib\mcumgr_wrapper\config\overlay-mcuboot.conf -Dmcuboot_DTC_OVERLAY_FILE="%CD%\app\boards\pinnacle_100.overlay" -Dmcuboot_CONF_FILE=%CD%\..\modules\zephyr_lib\mcuboot_config\pinnacle_100.conf
```

After building the firmware, it can be flashed with the following command:

> **Note:** `[board]` should be replaced with `mg100` or `pinnacle_100_dvk`

```
# Linux and macOS

west flash -d build/[board]/aws

# Windows

west flash -d build\[board]\aws
```

If the firmware was built with mcuboot, `west flash` will program merged.hex which contains the mcuboot bootloader and app in a combined image.

## Development

See [here](development.md) for help on getting started with custom development.

## Cellular Network Information

See [here](site_survey.md) for instructions on how to get details about the cell network.