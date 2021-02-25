# Cloud FOTA Updates

To start a firmware update, post to the device update topic. It is not recommended to attempt to do both modem and app firmware at the same time.
Replace the values under "app" with those that correspond to your update.

For example: `$aws/things/deviceId-354616090298915/shadow/update`

To update the app:
```
{
    "state": {
        "desired": {
            "app": {
                "desired": "3.0.0",
                "downloadHost": "https://9851-outpost-test.s3.amazonaws.com",
                "downloadFile": "fw/Pinnacle-100-DVK/app/3.0.0/480-00052-R3.0.0.1603395724_LTE-M_FOTA.bin",
                "start": 1611172151,
                "switchover": 1611172151
            }
        }
    }
}
```

The `desired` firmware version must be different from the running firmware version to kick off the update.
`start` is the time used to schedule when to download the update. If time is in the past, it will begin immediately.
`switchover` is the time used to schedule when to install the update. If time is in the past, it will begin immediately.
The `hash` value is only used to validate the integrity of the downloaded modem images. The has value is a sha256 hash.

IMPORTANT NOTE: Contact Laird Connectivity technical support to obtain an official modem update. Flashing in an
unsupported version may cause your device to malfunction.

To update the hl7800, replace the values under "hl7800" with those that correspond to your update.
```
{
    "state": {
        "desired": {
            "hl7800": {
                "desired": "4.4.14.99",
                "downloadHost": "https://9851-outpost-test.s3.amazonaws.com",
                "downloadFile": "fw/Sentrius-MG100/hl7800/4.4.14.99/4.4.14.0_4.4.14.99.bin",
                "downloadedFilename": "4.4.14.0_4.4.14.99.bin",
                "hash": "8752ce900ced895d548f8dafc0740270dc6a9bc4ee512cbd46f08aeefae04944",
                "start": 0,
                "switchover": 0
            }
        }
    }
}
```
