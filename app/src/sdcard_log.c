/**
 * @file sdcard_log.c
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(sdcard_log);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <devicetree.h>
#include <disk/disk_access.h>
#include <fs/fs.h>
#include <drivers/gpio.h>
#include <ff.h>
#include <stdio.h>
#include <string.h>
#include "FrameworkIncludes.h"
#include "nv.h"
#include "sdcard_log.h"
#include "qrtc.h"

#ifdef CONFIG_BLUEGRASS
# include "sensor_log.h"
#endif
/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define SD_OE_PORT				DT_PROP(DT_NODELABEL(gpio0), label)
#define SD_OE_PIN				4
#define SD_OE_ENABLED			1
#define SD_OE_DISABLED			0

#define TIMESTAMP_LEN			10
#define FMT_CHAR_LEN			3
#define EVENT_MAX_STR_LEN		128
#define EVENT_FMT_CHAR_LEN		5

#define B_PER_KB				1024
#define KB_PER_MB				1024
#define B_PER_MB				(B_PER_KB * KB_PER_MB)
/******************************************************************************/
/* Global Data Definitions                                                    */
/******************************************************************************/

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static FATFS fat_fs;
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
};

static const char *mountPoint = "/SD:";
static const char *batteryFilePath = "/SD:/mg100B.csv";
static const char *sensorFilePath = "/SD:/mg100Ad.csv";
static const char *bl654FilePath = "/SD:/mg100bl6.csv";

static bool sdCardPresent = false;
static struct fs_file_t batteryLogZfp;
static int batteryLogSeekOffset = 0;
static int batteryLogMaxLength = SDCARD_LOG_DEFAULT_MAX_LENGTH * B_PER_MB;
static bool batteryLogFileOpened = false;
static int sensorLogMaxLength = SDCARD_LOG_DEFAULT_MAX_LENGTH * B_PER_MB;
static int bl654LogMaxLength = SDCARD_LOG_DEFAULT_MAX_LENGTH * B_PER_MB;
static struct sdcard_status sdCardLogStatus;

#ifdef CONFIG_BLUEGRASS
static struct fs_file_t sensorLogFileZfp;
static int sensorLogSeekOffset = 0;
static bool sensorLogFileOpened = false;
static struct fs_file_t bl654LogFileZfp;
static int bl654LogSeekOffset = 0;
static bool bl654LogFileOpened = false;
#endif

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
struct sdcard_status *sdCardLogGetStatus()
{
	sdCardLogStatus.currLogSize = GetLogSize();
	sdCardLogStatus.maxLogSize = GetMaxLogSize();
	sdCardLogStatus.freeSpace = GetFSFree();

	return (&sdCardLogStatus);
}
bool UpdateMaxLogSize(int Value)
{
	bool ret = true;
	int ValueB = Value * B_PER_MB;
	sensorLogMaxLength = ValueB;
	bl654LogMaxLength = ValueB;
	batteryLogMaxLength = ValueB;
	nvStoreSDLogMaxSize(Value);
	LOG_INF("Max log file size = %d MB", Value);

	return (ret);
}

int GetMaxLogSize()
{
	int LengthB = sensorLogMaxLength / B_PER_MB;
	return(LengthB);
}

int GetFSFree()
{
	struct fs_statvfs Stats;
	unsigned long FreeSpace = 0;
	int Status = 0;
	int Ret = -1;

	if (sdCardPresent == true)
	{
		Status = fs_statvfs(mountPoint, &Stats);

		if (Status == 0)
		{
			FreeSpace = Stats.f_bfree * Stats.f_frsize;
			/* always round up 1MB */
			Ret = (FreeSpace / B_PER_MB) + 1;
		}
	}

	LOG_INF("Free Space = %d MB", Ret);

	return (Ret);
}

int GetLogSize()
{
	struct fs_dirent fileStat;
	int LogSize = 0;
	int Status = 0;
	int Ret = -1;

	if (sdCardPresent == true)
	{
		Status = fs_stat(bl654FilePath, &fileStat);
		if (Status == 0)
		{
			LogSize += fileStat.size;
		}
		Status = fs_stat(sensorFilePath, &fileStat);
		if (Status == 0)
		{
			LogSize += fileStat.size;
		}
		Status = fs_stat(batteryFilePath, &fileStat);
		if (Status == 0)
		{
			LogSize += fileStat.size;
		}
		/* always round up */
		Ret = (LogSize / B_PER_MB) + 1;
	}

	LOG_INF("Current Log Size = %d MB", Ret);

	return (Ret);
}

int sdCardLogInit()
{
	int ret = 0;
	int LogLength = 0;
	uint32_t blockCount = 0;
	uint32_t blockSize = 0;
	uint64_t sdCardSize = 0;
	static const char *diskPdrv = "SD";
	const struct device * sdcardEnable = 0;

	/* initialize the log size based on the stored log size */
	ret = nvReadSDLogMaxSize(&LogLength);


	/* if nothing valid is present just set it to a default value */
	if ((ret < 0) || (LogLength < SDCARD_LOG_DEFAULT_MAX_LENGTH))
	{
		LogLength = SDCARD_LOG_DEFAULT_MAX_LENGTH;
	}

	UpdateMaxLogSize(LogLength);

	/* enable the voltage translator between the nRF52 and SD card */
	sdcardEnable = device_get_binding(SD_OE_PORT);
	gpio_pin_configure(sdcardEnable, SD_OE_PIN, GPIO_OUTPUT);
	gpio_pin_set(sdcardEnable, SD_OE_PIN, SD_OE_ENABLED);

	do
	{

		ret = disk_access_init(diskPdrv);
		if (ret != 0) {
			LOG_ERR("Storage init error = %d", ret);
			break;
		}

		ret = disk_access_ioctl(diskPdrv,
				DISK_IOCTL_GET_SECTOR_COUNT, &blockCount);
		if (ret) {
			LOG_ERR("Unable to get block count, error = %d", ret);
			break;
		}
		LOG_INF("Block count %u", blockCount);

		ret = disk_access_ioctl(diskPdrv,
				DISK_IOCTL_GET_SECTOR_SIZE, &blockSize);
		if (ret) {
			LOG_ERR("Unable to get block size, error = %d", ret);
			break;
		}
		LOG_INF("Block size %u\n", blockSize);

		sdCardSize = (uint64_t)blockCount * blockSize;
		LOG_INF("Memory Size(MB) %u\n", (uint32_t)sdCardSize>>20);

		mp.mnt_point = mountPoint;

		ret = fs_mount(&mp);

		if (ret == 0) {
			LOG_INF("Disk mounted.\n");
			sdCardPresent = true;
		} else {
			LOG_ERR("Error mounting disk.\n");
		}
	} while(0);

	return ret;
}

#ifdef CONFIG_BLUEGRASS
int sdCardLogBL654Data(BL654SensorMsg_t * msg)
{
	int ret = -ENODEV;
	int totalLength = 0;
	char * logData = 0;
	struct fs_dirent fileStat;

	if (sdCardPresent)
	{
		/* if this is the first open, we want to
		*	be sure to append to the end rather than overwrite from
		*	the beginning.
		*/
		if (bl654LogFileOpened == false)
		{
			ret = fs_stat(bl654FilePath, &fileStat);
			if (ret == 0)
			{
				bl654LogSeekOffset = fileStat.size;
				bl654LogFileOpened = true;
			}
		}

		/* open and close the file every time to help ensure integrity
		*	of the file system if power were to be lost in between writes.
		*/
		ret = fs_open(&bl654LogFileZfp, bl654FilePath, FS_O_RDWR | FS_O_CREATE);
		if (ret >= 0)
		{
			totalLength = EVENT_MAX_STR_LEN + EVENT_FMT_CHAR_LEN;
			logData = k_malloc(totalLength);
			if(logData > 0)
			{
				/* find the end of the file */
				ret = fs_seek(&bl654LogFileZfp, bl654LogSeekOffset, 0);

				/* only try to write if the seek succeeded. */
				if (ret >= 0)
				{
					/* append the timestamp and data */
					snprintf(logData, totalLength, "%d,%d,%d,%d\n", Qrtc_GetEpoch(), (uint32_t)(msg->temperatureC * 100),
								(uint32_t)(msg->humidityPercent * 100), (uint32_t)(msg->pressurePa * 10));

					ret = fs_write(&bl654LogFileZfp, logData, strlen(logData));

					/* only update the offset if the write succeeded. */
					if (ret >= 0)
					{
						bl654LogSeekOffset += strlen(logData);
						/* treat this as a circular buffer to limit log file growth */
						if (bl654LogSeekOffset > bl654LogMaxLength)
						{
							bl654LogSeekOffset = 0;
						}
					}
				}
				k_free(logData);
				logData = 0;
			}
			/* close the file (this also flushes data to the physical media) */
			ret = fs_close(&bl654LogFileZfp);
		}
	}

	return ret;
}
int sdCardLogAdEvent(Bt510AdEvent_t * event)
{
	int ret = 0;
	int totalLength = 0;
	char * logData = 0;
	struct fs_dirent fileStat;
	char bleAddrStr[BT_ADDR_STR_LEN];

	if (sdCardPresent)
	{
		/* if this is the first open, we want to
		*	be sure to append to the end rather than overwrite from
		*	the beginning.
		*/
		if (sensorLogFileOpened == false)
		{
			ret = fs_stat(sensorFilePath, &fileStat);
			if (ret == 0)
			{
				sensorLogSeekOffset = fileStat.size;
				sensorLogFileOpened = true;
			}
		}

		/* open and close the file every time to help ensure integrity
		*	of the file system if power were to be lost in between writes.
		*/
		ret = fs_open(&sensorLogFileZfp, sensorFilePath, FS_O_RDWR | FS_O_CREATE);
		if (ret >= 0)
		{
			totalLength = EVENT_MAX_STR_LEN + EVENT_FMT_CHAR_LEN + BT_ADDR_STR_LEN;
			logData = k_malloc(totalLength);
			if(logData > 0)
			{
				/* find the end of the file */
				ret = fs_seek(&sensorLogFileZfp, sensorLogSeekOffset, 0);

				/* only try to write if the seek succeeded. */
				if (ret >= 0)
				{
					/* append the timestamp and data */
					bt_addr_to_str(&event->addr, bleAddrStr, sizeof(bleAddrStr));
					snprintf(logData, totalLength, "%s,%d,%d,%d,%d\n", bleAddrStr, event->epoch,
								event->recordType, event->id, event->data);
					ret = fs_write(&sensorLogFileZfp, logData, strlen(logData));

					/* only update the offset if the write succeeded. */
					if (ret >= 0)
					{
						sensorLogSeekOffset += strlen(logData);
						/* treat this as a circular buffer to limit log file growth */
						if (sensorLogSeekOffset > sensorLogMaxLength)
						{
							sensorLogSeekOffset = 0;
						}
					}
				}
				k_free(logData);
				logData = 0;
			}
			/* close the file (this also flushes data to the physical media) */
			ret = fs_close(&sensorLogFileZfp);
		}
	}

	return ret;
}
#endif
int sdCardLogBatteryData(void * data, int length)
{
	int ret = 0;
	int totalLength = 0;
	char * logData = 0;
	struct fs_dirent fileStat;

	if (sdCardPresent)
	{
		/* if this is the first open, we want to
		*	be sure to append to the end rather than overwrite from
		*	the beginning.
		*/
		if (batteryLogFileOpened == false)
		{
			ret = fs_stat(batteryFilePath, &fileStat);
			if (ret == 0)
			{
				batteryLogSeekOffset = fileStat.size;
				batteryLogFileOpened = true;
			}
		}

		/* open and close the file every time to help ensure integrity
		*	of the file system if power were to be lost in between writes.
		*/
		ret = fs_open(&batteryLogZfp, batteryFilePath, FS_O_RDWR | FS_O_CREATE);
		if (ret >= 0)
		{
			totalLength = length + TIMESTAMP_LEN + FMT_CHAR_LEN;
			logData = k_malloc(totalLength);
			if(logData > 0)
			{
				/* find the end of the file */
				ret = fs_seek(&batteryLogZfp, batteryLogSeekOffset, 0);

				/* only try to write if the seek succeeded. */
				if (ret >= 0)
				{
					/* append the timestamp and data */
					snprintk(logData, totalLength, "%d,%s\n", Qrtc_GetEpoch(), (char *)data);
					ret = fs_write(&batteryLogZfp, logData, strlen(logData));

					/* only update the offset if the write succeeded. */
					if (ret >= 0)
					{
						batteryLogSeekOffset += strlen(logData);
						/* treat this as a circular buffer to limit log file growth */
						if (batteryLogSeekOffset > batteryLogMaxLength)
						{
							batteryLogSeekOffset = 0;
						}
					}
				}
				k_free(logData);
				logData = 0;
			}
			/* close the file (this also flushes) */
			ret = fs_close(&batteryLogZfp);
		}
	}

	return ret;
}

