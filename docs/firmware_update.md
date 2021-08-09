# Firmware Updates

## Prerequisites

1. [mcumgr CLI](https://github.com/apache/mynewt-mcumgr#command-line-tool) (cross platform)
2. Pinnacle 100/MG100/BL5340 device running firmware v3.x or greater. See [here](https://github.com/LairdCP/Pinnacle_100_firmware/releases/tag/v3.0.0) for instructions on updating to 3.0.0.
3. Terminal program: Putty (Windows, Linux, macOS), Teraterm (Windows), Serial (macOS)

## Update Zephyr App Via UART

1. Connect terminal program to console UART and turn off log messages. Log messages output by the firmware can interfere with the firmware transfer process.

   Issue command:

   ```
   log halt
   ```

2. Disconnect the terminal program from the console UART and transfer the update file to the device using the mcumgr CLI via the console UART.

   ```
   # Linux/macOS

   mcumgr -t 20 -r 3 --conntype serial --connstring dev=/dev/tty.usbserial-A908JLEI,mtu=512 image upload /Users/ryan/Desktop/mg100_v3.0.103.bin

   # Windows

   mcumgr -t 20 -r 3 --conntype serial --connstring dev=COM4,mtu=512 image upload C:\mg100_v3.0.103.bin

   ```

   Depending on the size of the update file, the transfer can take some time.

3. List the images to obtain the hash of the update image in slot 1

   ```
   # Linux/macOS

   mcumgr --conntype serial --connstring dev=/dev/tty.usbserial-A908JLEI image list

   # Windows

   mcumgr --conntype serial --connstring dev=COM4 image list

   ```

   Response should look like

   ```
   Images:
   image=0 slot=0
       version: 3.0.101
       bootable: true
       flags: active confirmed
       hash: 292df381866bf65cab8f007897e3bcd8e936d5e37ba78183162e6f5fe1085b03
   image=0 slot=1
       version: 3.0.103
       bootable: true
       flags:
       hash: e378dde02fe58825fe0b620926ec932f0a4aaaa82857e897e40f7486d2011276
   Split status: N/A (0)
   ```

4. Test the image in slot 1. This sets the image in slot 1 to be swapped and booted.

   ```
   # Linux/macOS

   mcumgr --conntype serial --connstring dev=/dev/tty.usbserial-A908JLEI image test e378dde02fe58825fe0b620926ec932f0a4aaaa82857e897e40f7486d2011276

   # Windows

   mcumgr --conntype serial --connstring dev=COM4 image test e378dde02fe58825fe0b620926ec932f0a4aaaa82857e897e40f7486d2011276

   ```

   Response should look like

   ```
   Images:
   image=0 slot=0
       version: 3.0.101
       bootable: true
       flags: active confirmed
       hash: 292df381866bf65cab8f007897e3bcd8e936d5e37ba78183162e6f5fe1085b03
   image=0 slot=1
       version: 3.0.103
       bootable: true
       flags: pending
       hash: e378dde02fe58825fe0b620926ec932f0a4aaaa82857e897e40f7486d2011276
   Split status: N/A (0)
   ```

   Note the `flags` for slot 1 are now set to pending.

5. Issue a reset to swap to the slot 1 image and boot it. This can take some time to complete.

   ```
   # Linux/macOS

   mcumgr --conntype serial --connstring dev=/dev/tty.usbserial-A908JLEI reset

   # Windows

   mcumgr --conntype serial --connstring dev=COM4 reset

   ```

6. Re-connect the terminal program to the console UART to monitor when the new image boots. Once it boots, issue the turn off logging command in preparation for the last step.

   Issue command:

   ```
   log halt
   ```

7. Confirm the image. If the new image is not confirmed, the image will be swapped back to slot 1 on the next reboot (Note that the BLE gateway firmware will automatically confirm the image when it boots).

   ```
   # Linux/macOS

   mcumgr --conntype serial --connstring dev=/dev/tty.usbserial-A908JLEI image confirm

   # Windows

   mcumgr --conntype serial --connstring dev=COM4 image confirm

   ```

## Update Zephyr App Via BLE (mcumgr CLI)

Using mcumgr CLI and BLE is only supported on Linux or macOS.

1. Transfer the update file to the device using the mcumgr CLI via BLE.

   ```
   mcumgr -t 20 -r 3 --conntype ble --connstring ctlr_name=hci0,peer_name='MG100-0303848' image upload /Users/ryan/Desktop/mg100_v3.0.103.bin

   ```

   Depending on the size of the update file, the transfer can take some time.

2. List the images to obtain the hash of the update image in slot 1

   ```
   mcumgr --conntype ble --connstring ctlr_name=hci0,peer_name='MG100-0303848' image list

   ```

   Response should look like

   ```
   Images:
   image=0 slot=0
       version: 3.0.101
       bootable: true
       flags: active confirmed
       hash: 292df381866bf65cab8f007897e3bcd8e936d5e37ba78183162e6f5fe1085b03
   image=0 slot=1
       version: 3.0.103
       bootable: true
       flags:
       hash: e378dde02fe58825fe0b620926ec932f0a4aaaa82857e897e40f7486d2011276
   Split status: N/A (0)
   ```

3. Test the image in slot 1. This sets the image in slot 1 to be swapped and booted.

   ```
   mcumgr --conntype ble --connstring ctlr_name=hci0,peer_name='MG100-0303848' image test e378dde02fe58825fe0b620926ec932f0a4aaaa82857e897e40f7486d2011276

   ```

   Response should look like

   ```
   Images:
   image=0 slot=0
       version: 3.0.101
       bootable: true
       flags: active confirmed
       hash: 292df381866bf65cab8f007897e3bcd8e936d5e37ba78183162e6f5fe1085b03
   image=0 slot=1
       version: 3.0.103
       bootable: true
       flags: pending
       hash: e378dde02fe58825fe0b620926ec932f0a4aaaa82857e897e40f7486d2011276
   Split status: N/A (0)
   ```

   Note the `flags` for slot 1 are now set to pending.

4. Issue a reset to swap to the slot 1 image and boot it. This can take some time to complete.

   ```
   mcumgr --conntype ble --connstring ctlr_name=hci0,peer_name='MG100-0303848' reset

   ```

5. Confirm the image once it has booted. If the new image is not confirmed, the image will be swapped back to slot 1 on the next reboot.

   ```
   mcumgr --conntype ble --connstring ctlr_name=hci0,peer_name='MG100-0303848' image confirm

   ```

## Updating HL7800 Firmware Via UART (Pinnacle 100/MG100 only)

1. Connect terminal program to console UART and turn off log messages. Log messages output by the firmware can interfere with the firmware transfer process.

   Issue command:

   ```
   log halt
   ```

2. Disconnect the terminal program from the console UART and transfer the update file to the device using the mcumgr CLI via the console UART.

   ```
   # Linux/macOS

   mcumgr -t 5 -r 2 --conntype serial --connstring dev=/dev/tty.usbserial-A908JLEI,mtu=2048 fs upload /Users/ryan/Desktop/4.3.8.0_4.4.14.0.bin /lfs/4.3.8.0_4.4.14.0.bin

   # Windows

   mcumgr -t 5 -r 2 --conntype serial --connstring dev=COM4,mtu=2048 fs upload C:\4.3.8.0_4.4.14.0.bin /lfs/4.3.8.0_4.4.14.0.bin

   ```

   Depending on the size of the update file, the transfer can take some time.

3. Re-connect the terminal to the console UART and restart logging. Resuming log messages is important so the rest of the update process can be monitored.

   ```
   log go
   ```

4. Issue the update command to start the update.

   ```
   hl fup /lfs/4.3.8.0_4.4.14.0.bin
   ```

   This will start the firmware update by transferring the file to the HL7800 via the XModem protocol. Depending on the size of the update file this may take some time. You will see log messages periodically describing the state of the HL7800 update. Look for log messages similar to these:

   ```
   [00:04:42.037,109] <inf> modem_hl7800: Initiate FW update, total packets: 30
   [00:04:42.037,109] <inf> modem_hl7800: FOTA state: IDLE->START
   [00:04:43.304,565] <inf> modem_hl7800: FOTA state: START->WIP
   [00:04:51.190,887] <inf> modem_hl7800: FOTA state: WIP->PAD
   [00:04:51.458,679] <inf> modem_hl7800: FOTA state: PAD->SEND_EOT
   [00:04:51.467,529] <inf> modem_hl7800: +WDSI: 3
   [00:04:51.479,095] <inf> modem_hl7800: FOTA state: SEND_EOT->INSTALL
   [00:06:12.876,190] <inf> modem_hl7800: Startup State: READY
   [00:06:12.876,220] <inf> modem_hl7800: FOTA state: INSTALL->REBOOT_AND_RECONFIGURE
   [00:06:12.876,251] <inf> modem_hl7800: Modem Reset
   [00:06:12.926,361] <inf> modem_hl7800: Sleep State: UNINITIALIZED
   [00:06:12.926,361] <inf> modem_hl7800: Network State: 0 NOT_REGISTERED
   [00:06:12.926,391] <inf> modem_hl7800: Startup State: UNKNOWN
   [00:06:12.926,422] <inf> modem_hl7800: FOTA state: REBOOT_AND_RECONFIGURE->IDLE
   [00:06:12.926,422] <inf> modem_hl7800: Modem Run
   ```

   Once the `FOTA state: SEND_EOT->INSTALL` is triggered, the HL7800 will reboot and install its update. This can take a few minutes before the HL7800 reboots to resume normal operation. `FOTA state: REBOOT_AND_RECONFIGURE->IDLE` signals the update has completed.
