/**
 * @file lcd.c
 * @brief Displays demo text on a connected display
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL CONFIG_DISPLAY_LOG_LEVEL
LOG_MODULE_REGISTER(lcd);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <zephyr.h>
#include <drivers/display.h>
#include <lvgl.h>
#include <lcz_rpmsg.h>
#include <bl5340pa.h>
#include "lcd.h"
#include "attr.h"
#include "app_version.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define INFO_TEXT_STRING_MAX_SIZE 384

#define TAKE_MUTEX(m) k_mutex_lock(&m, K_FOREVER)
#define GIVE_MUTEX(m) k_mutex_unlock(&m)

#define BL5340PA_INTERNAL_ANTENNA_PART_NUMBER 68
#define BL5340PA_EXTERNAL_ANTENNA_PART_NUMBER 76
#define BL5340PA_VARIANT_STRING_MAX_SIZE 24

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
#ifdef CONFIG_BOARD_BL5340PA_DVK_CPUAPP
LV_IMG_DECLARE(module_image_int);
LV_IMG_DECLARE(module_image_ext);
static bool variant_set = false;
static uint8_t variant;
static char variant_string[BL5340PA_VARIANT_STRING_MAX_SIZE];
int endpoint_id;
#else
LV_IMG_DECLARE(module_image);
#endif
static const struct device *display_dev = NULL;
static char display_string_buffer[INFO_TEXT_STRING_MAX_SIZE];
static lv_obj_t *ui_container_main;
static lv_obj_t *ui_image_module;
static lv_obj_t *ui_text_info;
K_MUTEX_DEFINE(lcd_mutex);

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
#ifdef CONFIG_BOARD_BL5340PA_DVK_CPUAPP
static void variant_string_update(void);

static bool rpmsg_handler(uint8_t component, void *data, size_t len,
			  uint32_t src, bool handled);
#endif

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
#ifdef CONFIG_BOARD_BL5340PA_DVK_CPUAPP
static void variant_string_update(void)
{
        TAKE_MUTEX(lcd_mutex);
	memset(variant_string, 0, sizeof(variant_string));
	if (variant_set == true) {
		sprintf(variant_string, "(453-%05d) %s\n", (variant ==
				BL5340PA_ANTENNA_PIN_INTERNAL_ANTENNA ?
					BL5340PA_INTERNAL_ANTENNA_PART_NUMBER :
					BL5340PA_EXTERNAL_ANTENNA_PART_NUMBER),
				(variant ==
				BL5340PA_ANTENNA_PIN_INTERNAL_ANTENNA ?
					"Int. Ant." : "Ext. Ant."));
	}
        GIVE_MUTEX(lcd_mutex);
}

static bool rpmsg_handler(uint8_t component, void *data, size_t len,
			  uint32_t src, bool handled)
{
	/* Only process variant message once */
	if (variant_set == false) {
		uint8_t *buffer = (uint8_t*)data;
		if (len == RPMSG_LENGTH_BL5340PA_GET_VARIANT_RESPONSE &&
		    buffer[RPMSG_BL5340PA_OFFSET_OPCODE] ==
		    RPMSG_OPCODE_BL5340PA_GET_VARIANT) {
			variant = buffer[RPMSG_BL5340PA_OFFSET_DATA];

			if (variant == BL5340PA_VARIANT_INTERNAL_ANTENNA) {
				/* Internal antenna variant */
				lv_img_set_src(ui_image_module,
					       &module_image_int);
			} else if (variant == BL5340PA_VARIANT_EXTERNAL_ANTENNA) {
				/* External antenna variant */
				lv_img_set_src(ui_image_module,
					       &module_image_ext);
			} else {
				/* Unknown response, stop processing further */
				LOG_ERR("Invalid module variant: %d", variant);
				return true;
			}

			variant_set = true;
			variant_string_update();

	                lv_task_handler();
			lcd_display_update_details();
		} else {
			if (len == 0) {
				LOG_INF("Unhandled message from network core, "
					"length: 0, no opcode");
			} else {
				LOG_INF("Unhandled message from network core, "
					"length: %d, opcode: 0x%02x", len,
					buffer[RPMSG_BL5340PA_OFFSET_OPCODE]);
			}
		}
	}

	LOG_DBG("Message from network core, length: %d, src: %d", len, src);
        LOG_HEXDUMP_DBG(data, len, "Data: ");

	return true;
}
#endif

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void lcd_display_init(void)
{
	display_dev = device_get_binding(CONFIG_LVGL_DISPLAY_DEV_NAME);

	if (display_dev == NULL) {
		LOG_ERR("Display device %s was not found.",
			CONFIG_LVGL_DISPLAY_DEV_NAME);
	} else {
		memset(display_string_buffer, 0, sizeof(display_string_buffer));

		ui_container_main = lv_cont_create(lv_scr_act(), NULL);
		lv_obj_set_auto_realign(ui_container_main, true);
		lv_cont_set_fit(ui_container_main, LV_FIT_TIGHT);
		lv_cont_set_layout(ui_container_main, LV_LAYOUT_ROW_MID);

		ui_image_module = lv_img_create(ui_container_main, NULL);
#ifndef CONFIG_BOARD_BL5340PA_DVK_CPUAPP
		lv_img_set_src(ui_image_module, &module_image);
#endif
		lv_obj_align(ui_image_module, NULL, LV_ALIGN_CENTER, 0, 0);

		ui_text_info = lv_label_create(ui_container_main, NULL);
		lv_label_set_text(ui_text_info, display_string_buffer);
		lv_obj_align(ui_text_info, NULL, LV_ALIGN_CENTER, 0, 0);

		/* Set initial text */
		lcd_display_update_details();

		display_blanking_off(display_dev);
		lv_task_handler();

#ifdef CONFIG_BOARD_BL5340PA_DVK_CPUAPP
		/* Query module variant from network core using RPMSG. Once we
		 * know the module variant, we can display the correct image
		 * for the board and show the model number in the text
		 */
		if (lcz_rpmsg_register(&endpoint_id, 0, rpmsg_handler) == true) {
			uint8_t buffer[RPMSG_LENGTH_BL5340PA_GET_VARIANT];
			buffer[RPMSG_BL5340PA_OFFSET_OPCODE] =
				RPMSG_OPCODE_BL5340PA_GET_VARIANT;
			lcz_rpmsg_send(0, &buffer, sizeof(buffer));
		} else {
			LOG_ERR("Failed to register RPMSG handler");
		}
#endif
	}
}

void lcd_display_update_details(void)
{
	extern const char *get_app_type_short(void);

	if (display_dev == NULL) {
		LOG_ERR("Display device %s was not found.",
			CONFIG_LVGL_DISPLAY_DEV_NAME);
	} else {
	        TAKE_MUTEX(lcd_mutex);
		/* Display a simple message on the LCD with a picture of the
		 * module. This information also includes the advertising BLE
		 * name and passkey used to pair with the module (if enabled)
		 */
		sprintf(display_string_buffer, "Laird Connectivity\n"
#ifdef CONFIG_BOARD_BL5340PA_DVK_CPUAPP
					       "BL5340PA Bluetooth 5.2\n"
#else
					       "BL5340 Bluetooth 5.2\n"
#endif
					       "Development Kit\n"
#ifdef CONFIG_BOARD_BL5340PA_DVK_CPUAPP
					       "%s"
#endif
					       "\n"
					       "BLE Gateway Firmware\n"
					       "Version %s (%s)\n\n"
					       "Please download the\n"
					       "Pinnacle Connect app\n"
					       "from your app store\n"
					       "to configure firmware\n"
					       "settings.\n\n"
					       "Device name:\n  %s\n"
#ifdef CONFIG_MCUMGR_SMP_BT_AUTHEN
					       "Passkey:\n  %06u"
#endif
#ifdef CONFIG_BOARD_BL5340PA_DVK_CPUAPP
					       , variant_string
#endif
					       , APP_VERSION_STRING,
					       get_app_type_short(),
					       (char *)attr_get_quasi_static(ATTR_ID_name)
#ifdef CONFIG_MCUMGR_SMP_BT_AUTHEN
					       , attr_get_uint32(ATTR_ID_passkey, 0)
#endif
					       );


		lv_label_set_text(ui_text_info, display_string_buffer);

		lv_task_handler();
	        GIVE_MUTEX(lcd_mutex);
	}
}
