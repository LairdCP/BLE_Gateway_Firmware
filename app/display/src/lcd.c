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
#include "lcd.h"
#include "attr.h"
#include "app_version.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/
#define INFO_TEXT_STRING_MAX_SIZE 256

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
LV_IMG_DECLARE(module_image);
static char display_string_buffer[INFO_TEXT_STRING_MAX_SIZE];
static lv_obj_t *ui_container_main;
static lv_obj_t *ui_image_module;
static lv_obj_t *ui_text_info;

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
void lcd_display_init(void)
{
	const struct device *display_dev;

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
		lv_img_set_src(ui_image_module, &module_image);
		lv_obj_align(ui_image_module, NULL, LV_ALIGN_CENTER, 0, 0);

		ui_text_info = lv_label_create(ui_container_main, NULL);
		lv_label_set_text(ui_text_info, display_string_buffer);
		lv_obj_align(ui_text_info, NULL, LV_ALIGN_CENTER, 0, 0);

		display_blanking_off(display_dev);
		lv_task_handler();
	}
}

void lcd_display_update_details(void)
{
	extern const char *get_app_type(void);
	const struct device *display_dev;

	display_dev = device_get_binding(CONFIG_LVGL_DISPLAY_DEV_NAME);

	if (display_dev == NULL) {
		LOG_ERR("Display device %s was not found.",
			CONFIG_LVGL_DISPLAY_DEV_NAME);
	} else {
		/* Display a simple message on the LCD with a picture of the
		 * module. This information also includes the advertising BLE
		 * name and passkey used to pair with the module
		 */
		sprintf(display_string_buffer, "Laird Connectivity\n"
					       "BL5340 Bluetooth 5.2\n"
					       "Development Kit\n\n"
					       "BLE Gateway Firmware\n"
					       "Version %s (%s)\n\n"
					       "Please download the\n"
					       "Pinnacle Connect app\n"
					       "from your app store\n"
					       "to configure firmware\n"
					       "settings.\n\n"
					       "Device name:\n  %s\n"
					       "Passkey:\n  %06u",
					       APP_VERSION_STRING,
					       get_app_type(),
					       (char *)attr_get_quasi_static(ATTR_ID_name),
					       attr_get_uint32(ATTR_ID_passkey, 0));

		lv_label_set_text(ui_text_info, display_string_buffer);

		lv_task_handler();
	}
}
