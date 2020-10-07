# Firmware Updates

## Prerequisites
1. [mcumgr CLI](https://github.com/apache/mynewt-mcumgr#command-line-tool) (cross plaform)
2. MG100 running firmware part number 480-00070 v3.x or greater
3. Terminal program: Putty (Windows,Linux,macOS), Teraterm (Windows), Serial (macOS)

## Updating HL7800 Firmware Via UART
1. Connect terminal program to console UART and turn off log messages. Log messages output by the firmware can interfere with the firmware transfer process.

    Issue command:
    ```
    log halt
    ```

2. Disconnect the terminal program from the console UART and transfer the update file to the MG100 using the mcumgr CLI via the console UART.

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
