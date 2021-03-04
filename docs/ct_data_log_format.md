# Overview

This document focuses on the byte-level payload format of the CT log data as it is sent from the tracker to the gateway (SMP) and from gateway to AWS (MQTT).

# CT Log Data Transfer From BT510-CT/BT710 to the Gateway

The **ct_log_header** structure is sent between a BT510-CT (or BT710) and MG100-CT during a data log download process. The MG100-CT uses the contents from this **ct_log_header** to generate the **ct_publish_header** when publishing CT logs to AWS IoT (see section 2.0 for details).

The format of the CT log header that is transferred from a BT510-CT/BT710 tracker to an MG100 starts with the **ct_log_header** structure that contains as one of its fields a _ct_record_local structure_:

```
/* data stored about this local device to record some of its status values. */
struct ct_record_local {
   u8_t fw_version[4];      // firmware version of this tracker device **
   u16_t devices_seen;      // total tracker devices seen in BLE scans
   u16_t network_id;        // networkId parameter assigned to this tracker device **
   u16_t ad_interval_ms;    // active ad interval
   u16_t log_interval_min;  // logInterval parameter assigned to this tracker device
   u16_t scan_interval_sec; // active scan interval
   u8_t battery_level;      // last reported battery level. Left-shift by 4 to get millivolts **
   u8_t scan_duration_sec;  // active scan duration
   u8_t profile;            // profile parameter assigned to this tracker device
   s8_t rssi_threshold;     // rssi threshold assigned to this tracker device
   s8_t tx_power;           // tx power assigned to this tracker device
   u32_t up_time_sec;       // total up time of this tracker device in seconds
};
```

```
struct ct_log_header {
    u16_t entry_protocol_version; // **
    u16_t max_entry_size;  // max number of bytes per entry
    u16_t entry_count;     // number of entries
    u8_t device_id[6];     // deviceId of this tracker device **
    u32_t device_time;     // RTC time of this tracker device (seconds since epoch)
    u32_t log_size;        // total size of data log data
    u32_t last_upload_time;// RTC time of last time the log was uploaded (seconds since epoch) **
    struct ct_record_local local_info; // (see ct_record_local above)
};
```

Note the fields marked with \*\* correspond directly to the fields of the **ct_publish_header** published by the gateway as described in the following section.

# CT Log Data Publishing Format from the MG100-CT Gateway to AWS IoT

CT data logs are published by the MG100 to a single “telemetry reporting” topic. These are binary packets that consist of a CT Device Status followed by a CT Log Entry. These packets are published as a binary buffer directly from the MG100 CT to a configurable AWS IoT topic.

All messages will be published to the topic:
\<topic prefix\>\<device id\>\/up

All diagnostic logging messages will be published to the topic:
\<topic prefix\>\<device id\>\/log

For the MG100 CT device (CT designates an MG100 with “contact tracing” firmware), the \<topic prefix\> is configurable as a parameter at the device itself. The \<device id\> matches the IMEI number of the Pinnacle Modem within the MG100.

Default MG100-CT Settings:
\<topic prefix\> = mg100-ct/dev/gw/

So, an example topic the MG100 CT will publish its CT logs (telemetry data) to assuming its IMEI number is 123456789012345 would be:
mg100-ct/dev/gw/123456789012345/up

Diagnostic log output captured to microSD card and requested would be published to:
mg100-ct/dev/gw/123456789012345/log

## CT Device Status

Each binary packet starts with a 16-byte **ct_publish_header** containing status information including the device id of the CT device that generated the log entries that follow in this packet. Status data is fixed in size and corresponds directly to the **ct_log_header** structure defined further below, received by an MG100 CT when downloading a log from the BT510-CT or BT710.

```
struct ct_publish_header {
    u16_t entry_protocol_version;
    u8_t device_id[6];      // deviceId of the CT tracker that generated the entries to follow
    u32_t device_time;      // gateway RTC time when publishing (seconds since the epoch)
    u32_t last_upload_time; // timestamp of last time this CT tracker’s logs were uploaded
    u8_t fw_version[4];     // firmware version of the reporting CT tracker
    u8_t battery_level;     // battery level of the reporting CT tracker
    u16_t network_id;       // networkID parameter of the reporting CT tracker
}
```

## CT Log Entry

Immediately following the **ct_publish_header** is a single Log Entry defined by the **log_entry** structure. A single log entry may vary in size but shall not exceed 256 bytes. Each **log_entry** is published individually to the configured AWS IoT “up” topic, prefixed by a **ct_publish_header** described above. Packets are published as binary buffers directly from the MG100 CT to an AWS IoT topic.

Each entry consists of a 16 byte header defined by **log_entry_header**, followed by 1 or more records defined by **log_entry_record**. A record format is determined by its first byte, recordType. For example, the default record type is an Ad Tracking Record, recordType = 0x11 and total length for this type of record is 8 bytes. The number of records may vary, but for this record type, up to a maximum of 30 records can fit into a single 256 byte entry (16 byte header followed by up to 240 bytes of record data). There may be fewer records reported in an entry, i.e. the entry may be less than a total of 256 bytes but will always be at least 24 bytes (16 byte **log_entry_header** followed by one 8 byte **log_entry_record**).

All entries are a maximum of 256 bytes so will always be between 24 bytes and 256 bytes per publish depending on the number of **log_entry_record** bytes following the **log_entry_header**.

```
/* Log entry item */
struct log_entry
{
    /* single header for this entry */
    struct log_entry_header
    {
        u8_t entryStart;   // 0xA5
        u8_t flags;        // flash log flags
        u16_t scanInterval;// seconds between BLE scans
        u8_t serial[6];    // deviceId of the remote CT tracker detected
        u32_t timestamp;   // absolute timestamp for the first record (seconds since epoch)
        u8_t entrySize[2]; // indicates the number of bytes in the log entries to follow
    };

    /* one or more data records to follow contained
     * in this entry. One log_entry_record below
     * for each record within this entry */
    struct log_entry_record
    {
        u8_t recordType;         // 0x11
        u8_t status;             // Antenna index receiving the BLE advertisement (for BT710)
        u8_t reserved1;
        u8_t scanIntervalOffset; // scanIntervals since "timestamp" this record was captured
        s8_t rssi;               // RSSI value captured by the reporting tracker
        u8_t motion;             // Motion report in the remote tracker’s BLE advertisement
        s8_t txPower;            // TX power captured from remote tracker’s BLE advertisement
    };
    /* more log_entry_record instances may follow here */
};
```
