/**
 * @file sdcard_log.c
 * @brief
 *
 * Copyright (c) 2020-2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(sdcard_log, CONFIG_SD_CARD_LOG_LEVEL);

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
#include <stdlib.h>

#include "FrameworkIncludes.h"
#include "attr.h"
#include "sdcard_log.h"
#include "lcz_qrtc.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define SD_OE_PORT DT_PROP(DT_NODELABEL(gpio0), label)
#define SD_OE_PIN 4
#define SD_OE_ENABLED 1
#define SD_OE_DISABLED 0

#define TIMESTAMP_LEN 10
#define FMT_CHAR_LEN 3
#define EVENT_MAX_STR_LEN 128
#define EVENT_FMT_CHAR_LEN 5

#define B_PER_KB 1024
#define KB_PER_MB 1024
#define B_PER_MB (B_PER_KB * KB_PER_MB)

#define SD_ACCESS_SEM_TIMEOUT K_MSEC(2000)

/******************************************************************************/
/* Global Data Definitions                                                    */
/******************************************************************************/
K_SEM_DEFINE(sd_card_access_sem, 1, 1);

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

#ifdef CONFIG_SCAN_FOR_BT510
static struct fs_file_t sensorLogFileZfp;
static int sensorLogSeekOffset = 0;
static bool sensorLogFileOpened = false;
#endif

#ifdef CONFIG_BL654_SENSOR
static struct fs_file_t bl654LogFileZfp;
static int bl654LogSeekOffset = 0;
static bool bl654LogFileOpened = false;
#endif

#ifdef CONFIG_CONTACT_TRACING
static struct fs_file_t sdLogZfp;
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

int UpdateMaxLogSize(int Value)
{
	int ValueB = Value * B_PER_MB;
	sensorLogMaxLength = ValueB;
	bl654LogMaxLength = ValueB;
	batteryLogMaxLength = ValueB;
	LOG_INF("Max log file size = %d MB", Value);

	return attr_set_uint32(ATTR_ID_sdLogMaxSize, Value);
}

int GetMaxLogSize()
{
	int LengthB = sensorLogMaxLength / B_PER_MB;
	return (LengthB);
}

int GetFSFree()
{
	struct fs_statvfs Stats;
	unsigned long FreeSpace = 0;
	int Status = 0;
	int Ret = -1;

	if (sdCardPresent == true) {
		Status = fs_statvfs(mountPoint, &Stats);

		if (Status == 0) {
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

	if (sdCardPresent == true) {
		Status = fs_stat(bl654FilePath, &fileStat);
		if (Status == 0) {
			LogSize += fileStat.size;
		}
		Status = fs_stat(sensorFilePath, &fileStat);
		if (Status == 0) {
			LogSize += fileStat.size;
		}
		Status = fs_stat(batteryFilePath, &fileStat);
		if (Status == 0) {
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
	const struct device *sdcardEnable = 0;

	/* initialize the log size based on the stored log size */
	ret = attr_copy_uint32(&LogLength, ATTR_ID_sdLogMaxSize);

	/* if nothing valid is present just set it to a default value */
	if ((ret < 0) || (LogLength < SDCARD_LOG_DEFAULT_MAX_LENGTH)) {
		LogLength = SDCARD_LOG_DEFAULT_MAX_LENGTH;
	}

	UpdateMaxLogSize(LogLength);

	/* enable the voltage translator between the nRF52 and SD card */
	sdcardEnable = device_get_binding(SD_OE_PORT);
	gpio_pin_configure(sdcardEnable, SD_OE_PIN, GPIO_OUTPUT);
	gpio_pin_set(sdcardEnable, SD_OE_PIN, SD_OE_ENABLED);

	do {
		ret = disk_access_init(diskPdrv);
		if (ret != 0) {
			LOG_ERR("Storage init error = %d", ret);
			break;
		}

		ret = disk_access_ioctl(diskPdrv, DISK_IOCTL_GET_SECTOR_COUNT,
					&blockCount);
		if (ret) {
			LOG_ERR("Unable to get block count, error = %d", ret);
			break;
		}
		LOG_INF("Block count %u", blockCount);

		ret = disk_access_ioctl(diskPdrv, DISK_IOCTL_GET_SECTOR_SIZE,
					&blockSize);
		if (ret) {
			LOG_ERR("Unable to get block size, error = %d", ret);
			break;
		}
		LOG_INF("Block size %u\n", blockSize);

		sdCardSize = (uint64_t)blockCount * blockSize;
		LOG_INF("Memory Size(MB) %u\n", (uint32_t)sdCardSize >> 20);

		mp.mnt_point = mountPoint;

		ret = fs_mount(&mp);

		if (ret == 0) {
			LOG_INF("Disk mounted.\n");
			sdCardPresent = true;
		} else {
			LOG_ERR("Error mounting disk.\n");
		}
	} while (0);

	return ret;
}

#ifdef CONFIG_BL654_SENSOR
int sdCardLogBL654Data(BL654SensorMsg_t *msg)
{
	int ret = -ENODEV;
	int totalLength = 0;
	char *logData = 0;
	struct fs_dirent fileStat;

	if (sdCardPresent) {
		/* if this is the first open, we want to
		*	be sure to append to the end rather than overwrite from
		*	the beginning.
		*/
		if (bl654LogFileOpened == false) {
			ret = fs_stat(bl654FilePath, &fileStat);
			if (ret == 0) {
				bl654LogSeekOffset = fileStat.size;
				bl654LogFileOpened = true;
			}
		}

		/* open and close the file every time to help ensure integrity
		*	of the file system if power were to be lost in between writes.
		*/
		ret = fs_open(&bl654LogFileZfp, bl654FilePath,
			      FS_O_RDWR | FS_O_CREATE);
		if (ret >= 0) {
			totalLength = EVENT_MAX_STR_LEN + EVENT_FMT_CHAR_LEN;
			logData = k_malloc(totalLength);
			if (logData > 0) {
				/* find the end of the file */
				ret = fs_seek(&bl654LogFileZfp,
					      bl654LogSeekOffset, 0);

				/* only try to write if the seek succeeded. */
				if (ret >= 0) {
					/* append the timestamp and data */
					snprintf(logData, totalLength,
						 "%d,%d,%d,%d\n",
						 lcz_qrtc_get_epoch(),
						 (uint32_t)(msg->temperatureC *
							    100),
						 (uint32_t)(
							 msg->humidityPercent *
							 100),
						 (uint32_t)(msg->pressurePa *
							    10));

					ret = fs_write(&bl654LogFileZfp,
						       logData,
						       strlen(logData));

					/* only update the offset if the write succeeded. */
					if (ret >= 0) {
						bl654LogSeekOffset +=
							strlen(logData);
						/* treat this as a circular buffer to limit log file growth */
						if (bl654LogSeekOffset >
						    bl654LogMaxLength) {
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
#endif

#ifdef CONFIG_SCAN_FOR_BT510
int sdCardLogAdEvent(LczSensorAdEvent_t *event)
{
	int ret = 0;
	int totalLength = 0;
	char *logData = 0;
	struct fs_dirent fileStat;
	char bleAddrStr[BT_ADDR_STR_LEN];

	if (sdCardPresent) {
		/* if this is the first open, we want to
		*	be sure to append to the end rather than overwrite from
		*	the beginning.
		*/
		if (sensorLogFileOpened == false) {
			ret = fs_stat(sensorFilePath, &fileStat);
			if (ret == 0) {
				sensorLogSeekOffset = fileStat.size;
				sensorLogFileOpened = true;
			}
		}

		/* open and close the file every time to help ensure integrity
		*	of the file system if power were to be lost in between writes.
		*/
		ret = fs_open(&sensorLogFileZfp, sensorFilePath,
			      FS_O_RDWR | FS_O_CREATE);
		if (ret >= 0) {
			totalLength = EVENT_MAX_STR_LEN + EVENT_FMT_CHAR_LEN +
				      BT_ADDR_STR_LEN;
			logData = k_malloc(totalLength);
			if (logData > 0) {
				/* find the end of the file */
				ret = fs_seek(&sensorLogFileZfp,
					      sensorLogSeekOffset, 0);

				/* only try to write if the seek succeeded. */
				if (ret >= 0) {
					/* append the timestamp and data */
					bt_addr_to_str(&event->addr, bleAddrStr,
						       sizeof(bleAddrStr));
					snprintf(logData, totalLength,
						 "%s,%d,%d,%d,%d\n", bleAddrStr,
						 event->epoch,
						 event->recordType, event->id,
						 event->data.u16);
					ret = fs_write(&sensorLogFileZfp,
						       logData,
						       strlen(logData));

					/* only update the offset if the write succeeded. */
					if (ret >= 0) {
						sensorLogSeekOffset +=
							strlen(logData);
						/* treat this as a circular buffer to limit log file growth */
						if (sensorLogSeekOffset >
						    sensorLogMaxLength) {
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

int sdCardLogBatteryData(void *data, int length)
{
	int ret = 0;
	int totalLength = 0;
	char *logData = 0;
	struct fs_dirent fileStat;

	if (sdCardPresent) {
		/* if this is the first open, we want to
		*	be sure to append to the end rather than overwrite from
		*	the beginning.
		*/
		if (batteryLogFileOpened == false) {
			ret = fs_stat(batteryFilePath, &fileStat);
			if (ret == 0) {
				batteryLogSeekOffset = fileStat.size;
				batteryLogFileOpened = true;
			}
		}

		/* open and close the file every time to help ensure integrity
		*	of the file system if power were to be lost in between writes.
		*/
		ret = fs_open(&batteryLogZfp, batteryFilePath,
			      FS_O_RDWR | FS_O_CREATE);
		if (ret >= 0) {
			totalLength = length + TIMESTAMP_LEN + FMT_CHAR_LEN;
			logData = k_malloc(totalLength);
			if (logData > 0) {
				/* find the end of the file */
				ret = fs_seek(&batteryLogZfp,
					      batteryLogSeekOffset, 0);

				/* only try to write if the seek succeeded. */
				if (ret >= 0) {
					/* append the timestamp and data */
					snprintk(logData, totalLength,
						 "%d,%s\n",
						 lcz_qrtc_get_epoch(),
						 (char *)data);
					ret = fs_write(&batteryLogZfp, logData,
						       strlen(logData));

					/* only update the offset if the write succeeded. */
					if (ret >= 0) {
						batteryLogSeekOffset +=
							strlen(logData);
						/* treat this as a circular buffer to limit log file growth */
						if (batteryLogSeekOffset >
						    batteryLogMaxLength) {
							batteryLogSeekOffset =
								0;
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

#ifdef CONFIG_CONTACT_TRACING

int sdCardCleanup(void)
{
	/* if there are more than 256 entries, delete the oldest files beyond 256 */

	int ret, i;
	struct fs_dir_t dirp;
	struct fs_dirent entry;
	char full_path[32] = "/SD:/";
	int total_files = 0;
	int delete_idx = 0;
	uint32_t *dir_listing;

	if (k_sem_take(&sd_card_access_sem, SD_ACCESS_SEM_TIMEOUT) != 0) {
		return -1;
	}

	ret = fs_opendir(&dirp, full_path);
	if (ret) {
		k_sem_give(&sd_card_access_sem);
		return ret;
	}

	dir_listing = (uint32_t *)k_malloc(4 * 256);
	if (!dir_listing) {
		k_sem_give(&sd_card_access_sem);
		return -1;
	}

	memset(dir_listing, 0, 4 * 256);

	for (;;) {
		ret = fs_readdir(&dirp, &entry);

		/* entry.name[0] == 0 means end-of-dir */
		if (ret || entry.name[0] == 0) {
			break;
		}

		if (entry.type == FS_DIR_ENTRY_FILE) {
			if (strstr(entry.name, ".LOG") != NULL) {
				if (total_files < 256) {
					dir_listing[total_files] =
						strtoul(entry.name, NULL, 10) &
						0xFFFFFFFF;
				}
				total_files++;
			}
		}
	}
	ret = fs_closedir(&dirp);
	k_sem_give(&sd_card_access_sem);

	if (total_files > 255) {
		LOG_WRN("cleaning up SD card, too many log files (%d)",
			total_files);
	} else {
		k_free(dir_listing);
		return 0;
	}

	if (k_sem_take(&sd_card_access_sem, SD_ACCESS_SEM_TIMEOUT) != 0) {
		k_free(dir_listing);
		return -1;
	}

	/* now, delete the oldest file (lowest numbered file) */
	delete_idx = -1;
	int min = 0xFFFFFFFF;
	for (i = 0; i < 255; i++) {
		if (dir_listing[i] < min && dir_listing[i]) {
			min = dir_listing[i];
			delete_idx = i;
		}
	}

	if (delete_idx >= 0) {
		snprintf(full_path, sizeof(full_path), "/SD:/%08u.log",
			 dir_listing[delete_idx]);
		fs_unlink(full_path);
	}

	k_free(dir_listing);
	k_sem_give(&sd_card_access_sem);
	LOG_WRN("removed %s", log_strdup(full_path));
	return 0;
}

int sdCardLsDir(const char *path)
{
	int ret;
	struct fs_dir_t dirp;
	struct fs_dirent entry;
	char full_path[128] = "/SD:";

	if (!path)
		return -1;

	strncat(full_path, path, sizeof(full_path) - strlen(full_path) - 1);

	if (k_sem_take(&sd_card_access_sem, SD_ACCESS_SEM_TIMEOUT) != 0) {
		return -1;
	}

	ret = fs_opendir(&dirp, full_path);
	if (ret) {
		LOG_ERR("Error opening dir %s [%d]", full_path, ret);
		k_sem_give(&sd_card_access_sem);
		return ret;
	}

	printk("Listing dir %s:\n", full_path);
	for (;;) {
		ret = fs_readdir(&dirp, &entry);

		/* entry.name[0] == 0 means end-of-dir */
		if (ret || entry.name[0] == 0) {
			break;
		}

		if (entry.type == FS_DIR_ENTRY_DIR) {
			printk("[DIR ] %s\n", entry.name);
		} else {
			printk("[FILE] %s (size = %zu)\n", entry.name,
			       entry.size);
		}
	}

	ret = fs_closedir(&dirp);
	k_sem_give(&sd_card_access_sem);

	return ret;
}

int sdCardLsDirToString(const char *path, char *buf, int maxlen)
{
	/* ("/", sd_log_publish_buf, SD_LOG_PUBLISH_MAX_CHUNK_LEN); */
	int ret, i;
	struct fs_dir_t dirp;
	struct fs_dirent entry;
	char full_path[128] = "/SD:";
	uint32_t dir_index = 0;
	uint32_t *dir_listing;
	uint32_t *dir_fsize;

	if (!path)
		return -1;
	if (!buf)
		return -1;
	if (maxlen < 64)
		return -1;

	strncat(full_path, path, sizeof(full_path) - strlen(full_path) - 1);
	memset(buf, 0, maxlen);

	if (k_sem_take(&sd_card_access_sem, SD_ACCESS_SEM_TIMEOUT) != 0) {
		return -1;
	}

	ret = fs_opendir(&dirp, full_path);
	if (ret) {
		k_sem_give(&sd_card_access_sem);
		return ret;
	}

	dir_listing = (uint32_t *)k_malloc(4 * 256);
	if (!dir_listing) {
		k_sem_give(&sd_card_access_sem);
		return -1;
	}

	dir_fsize = (uint32_t *)k_malloc(4 * 256);
	if (!dir_fsize) {
		k_free(dir_listing);
		k_sem_give(&sd_card_access_sem);
		return -1;
	}

	memset(dir_listing, 0, 4 * 256);

	for (;;) {
		ret = fs_readdir(&dirp, &entry);

		/* entry.name[0] == 0 means end-of-dir */
		if (ret || entry.name[0] == 0) {
			break;
		}

		if (entry.type == FS_DIR_ENTRY_FILE) {
			if (strstr(entry.name, ".LOG") != NULL) {
				dir_listing[dir_index] =
					strtoul(entry.name, NULL, 10) &
					0xFFFFFFFF;
				dir_fsize[dir_index++] = entry.size;
				dir_index = (dir_index & 0xFF);
			}
		}
	}

	ret = fs_closedir(&dirp);

	int bytes_written = 0;
	int part = 0;
	uint32_t max;
	uint32_t max_idx;
	while (1) {
		/* find the maximum integer in the list */
		max = 0;
		max_idx = 0xFFFFFFFF;
		for (i = 0; i < 255; i++) {
			if (dir_listing[i] > max && dir_listing[i]) {
				max = dir_listing[i];
				max_idx = i;
			}
		}

		if (max_idx < 0xFFFFFFFF) {
			if ((maxlen - bytes_written) > 32 &&
			    dir_listing[max_idx] && dir_fsize[max_idx]) {
				part = snprintf(&buf[bytes_written], 32,
						"%08u.log %u\n",
						dir_listing[max_idx],
						dir_fsize[max_idx]);
				/* set this entry to 0 so it is no longer the max */
				dir_listing[max_idx] = 0;
				if (part > 0) {
					bytes_written += part;
				} else {
					break;
				}
			} else {
				break;
			}
		} else {
			break;
		}
	}

	k_free(dir_listing);
	k_free(dir_fsize);
	k_sem_give(&sd_card_access_sem);

	return ret;
}

int sdCardLogGet(char *pbuf, log_get_state_t *lstate, uint32_t maxlen)
{
	int ret;
	char full_path[128] = "/SD:/";
	struct fs_dirent fileStat;

	/* start by indicating no bytes ready to send */
	lstate->bytes_ready = 0;
	strncat(full_path, lstate->rpc_params.filename, sizeof(full_path) - 1);
	memset(pbuf, 0, maxlen);

	/* Use lstate->rpc_params.whence and lstate->rpc_params.offset to determine
     * where to seek in the file. Then based on lstate->rpc_params.length and
     * lstate->bytes_remaining read the next bytes into pbuf.
     */
	if (lstate->rpc_params.length == lstate->bytes_remaining) {
		/* calculate the "cur_seek" value before any bytes are read */
		if (k_sem_take(&sd_card_access_sem, SD_ACCESS_SEM_TIMEOUT) ==
		    0) {
			ret = fs_stat(full_path, &fileStat);
			if (ret == 0) {
				/* cap the size being requested to length of file */
				if (lstate->rpc_params.length > fileStat.size) {
					lstate->rpc_params.length =
						fileStat.size;
					lstate->bytes_remaining =
						lstate->rpc_params.length;
				}
				if (lstate->rpc_params.offset > fileStat.size) {
					lstate->rpc_params.offset =
						fileStat.size;
				}
				if (strncmp(lstate->rpc_params.whence, "end",
					    3) == 0) {
					/* offset is bytes from end of file */
					lstate->cur_seek =
						fileStat.size -
						lstate->rpc_params.offset;
				} else {
					/* offset is bytes from start of file */
					lstate->cur_seek =
						lstate->rpc_params.offset;
				}

				/* cap the bytes remaining after seek offset is applied */
				if (lstate->bytes_remaining >
				    (fileStat.size - lstate->cur_seek)) {
					lstate->bytes_remaining =
						fileStat.size -
						lstate->cur_seek;
				}
			} else {
				/* file does not exist */
				k_sem_give(&sd_card_access_sem);
				/* returning 0 with 0 bytes ready effectively aborts the send */
				return 0;
			}
			k_sem_give(&sd_card_access_sem);
		}
	}

	if (lstate->bytes_remaining > 0) {
		if (k_sem_take(&sd_card_access_sem, SD_ACCESS_SEM_TIMEOUT) ==
		    0) {
			if (maxlen > lstate->bytes_remaining) {
				maxlen = lstate->bytes_remaining;
			}

			ret = fs_open(&sdLogZfp, full_path, FS_O_RDWR);
			if (ret >= 0) {
				ret = fs_seek(&sdLogZfp, lstate->cur_seek,
					      FS_SEEK_SET);

				/* read the log data */
				lstate->bytes_ready =
					fs_read(&sdLogZfp, pbuf, maxlen);
				if (lstate->bytes_ready < maxlen) {
					lstate->bytes_remaining = 0;
				} else {
					lstate->bytes_remaining -=
						lstate->bytes_ready;
				}
				lstate->cur_seek += lstate->bytes_ready;

				/* close the file */
				ret = fs_close(&sdLogZfp);
			} else {
				k_sem_give(&sd_card_access_sem);
				return 0;
			}
			k_sem_give(&sd_card_access_sem);
		} else {
			/* no bytes to send */
			/* returning 0 with 0 bytes ready effectively aborts the send */
			return 0;
		}
	}

	return lstate->bytes_remaining;
}

void sdCardLogTest(void)
{
	int ret = 0, i;
	char sdTestFilePath[32] = { 0 };

	if (sdCardPresent && lcz_qrtc_epoch_was_set()) {
		if (k_sem_take(&sd_card_access_sem, SD_ACCESS_SEM_TIMEOUT) ==
		    0) {
			time_t now = lcz_qrtc_get_epoch();
			struct tm now_tm;
			gmtime_r(&now, &now_tm);
			uint32_t ts = (now_tm.tm_year % 100) * 1000000;
			ts += (now_tm.tm_mon + 1) * 10000;
			ts += (now_tm.tm_mday) * 100;
			ts += now_tm.tm_hour;

			for (i = 0; i < 255; i++) {
				sprintf(sdTestFilePath, "/SD:/%08u.log",
					ts - i - 1);
				ret = fs_open(&sdLogZfp, sdTestFilePath,
					      FS_O_RDWR);
				if (ret >= 0) {
					ret = fs_write(&sdLogZfp, "test", 4);
					ret = fs_close(&sdLogZfp);
				} else {
					break;
				}
			}

			k_sem_give(&sd_card_access_sem);
		}
	}
}
#endif
