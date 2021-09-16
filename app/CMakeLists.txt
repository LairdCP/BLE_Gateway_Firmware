cmake_minimum_required(VERSION 3.13.1)

# Add configuration for building with mcuboot
# because it cannot be done with relative paths in VS code on Windows
if((BOARD STREQUAL mg100) OR (BOARD STREQUAL pinnacle_100_dvk))
set(mcuboot_DTC_OVERLAY_FILE
    ${CMAKE_SOURCE_DIR}/boards/pinnacle_100.overlay
)

set(mcuboot_CONF_FILE
    ${CMAKE_SOURCE_DIR}/../../modules/zephyr_lib/mcuboot_config/pinnacle_100.conf
)
endif()

# Prepend the additional boards directory but still allow the original Zephyr
# boards directory to be used with lower priority
list(PREPEND BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(oob_demo)

if(CONFIG_LCZ_MEMFAULT)
zephyr_include_directories(${CMAKE_SOURCE_DIR}/memfault)
endif()

target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/common/src/main.c
    ${CMAKE_SOURCE_DIR}/common/src/jsmn_json.c
    ${CMAKE_SOURCE_DIR}/common/src/button.c
    ${CMAKE_SOURCE_DIR}/common/src/ble.c
    ${CMAKE_SOURCE_DIR}/common/src/lcz_certs.c
    ${CMAKE_SOURCE_DIR}/common/src/gateway_common.c
    ${CMAKE_SOURCE_DIR}/common/src/control_task.c
    ${CMAKE_SOURCE_DIR}/common/src/gateway_fsm.c
    ${CMAKE_SOURCE_DIR}/common/src/fota_smp.c
)

target_sources_ifdef(CONFIG_MODEM_HL7800 app PRIVATE
    ${CMAKE_SOURCE_DIR}/common/src/lte.c
)

target_sources_ifdef(CONFIG_NET_L2_ETHERNET app PRIVATE
    ${CMAKE_SOURCE_DIR}/common/src/ethernet_network.c
)

target_sources_ifdef(CONFIG_SNTP app PRIVATE
    ${CMAKE_SOURCE_DIR}/common/src/sntp_qrtc.c
)

target_sources_ifdef(CONFIG_LCZ_SNTP_SHELL app PRIVATE
    ${CMAKE_SOURCE_DIR}/common/src/lcz_sntp_shell.c
)

if(CONFIG_DISPLAY)
include_directories(${CMAKE_SOURCE_DIR}/display/include)
target_sources_ifdef(CONFIG_DISPLAY app PRIVATE
    ${CMAKE_SOURCE_DIR}/display/src/bl5340_module.c
    ${CMAKE_SOURCE_DIR}/display/src/lcd.c
)
endif()

if(CONFIG_CONTACT_TRACING)
zephyr_include_directories(${CMAKE_SOURCE_DIR}/contact_tracing/include)
target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/contact_tracing/src/ct_ble.c
    ${CMAKE_SOURCE_DIR}/contact_tracing/src/ct_app.c
    ${CMAKE_SOURCE_DIR}/contact_tracing/src/rpc_params.c
    ${CMAKE_SOURCE_DIR}/contact_tracing/src/dfu_smp_c.c
    ${CMAKE_SOURCE_DIR}/contact_tracing/src/ct_fs_intercept.c
)
endif()

target_sources_ifdef(CONFIG_CT_SHELL app PRIVATE
    ${CMAKE_SOURCE_DIR}/contact_tracing/src/ct_shell.c
)

target_sources_ifdef(CONFIG_SINGLE_PERIPHERAL app PRIVATE
    ${CMAKE_SOURCE_DIR}/common/src/single_peripheral.c
)

target_sources_ifdef(CONFIG_WDT app PRIVATE
    ${CMAKE_SOURCE_DIR}/common/src/wdt.c)

include_directories(${CMAKE_SOURCE_DIR}/common/include)
include_directories(${CMAKE_SOURCE_DIR}/framework_config)
include_directories(${CMAKE_SOURCE_DIR}/../../modules/jsmn)

if(CONFIG_BOARD_MG100)
target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/common/src/lairdconnect_battery.c
)
endif()

if(CONFIG_SD_CARD_LOG)
target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/common/src/sdcard_log.c
)
endif()

if(CONFIG_LCZ_MOTION)
target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/common/src/lcz_motion.c
)
endif()

if(CONFIG_LWM2M)
include_directories(${CMAKE_SOURCE_DIR}/lwm2m/include)
target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/lwm2m/source/lcz_lwm2m_client.c
)
endif()

if(CONFIG_BLUEGRASS)
include_directories(${CMAKE_SOURCE_DIR}/bluegrass/include)
target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/bluegrass/source/bluegrass.c
    ${CMAKE_SOURCE_DIR}/bluegrass/source/sensor_cmd.c
    ${CMAKE_SOURCE_DIR}/bluegrass/source/sensor_gateway_parser.c
    ${CMAKE_SOURCE_DIR}/bluegrass/source/shadow_builder.c
    ${CMAKE_SOURCE_DIR}/bluegrass/source/to_string.c
    ${CMAKE_SOURCE_DIR}/common/src/aws.c
)
endif()

if(CONFIG_SENSOR_TASK)
target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/bluegrass/source/sensor_log.c
    ${CMAKE_SOURCE_DIR}/bluegrass/source/sensor_table.c
    ${CMAKE_SOURCE_DIR}/bluegrass/source/sensor_task.c
)
endif()

if(CONFIG_BL654_SENSOR)
include_directories(${CMAKE_SOURCE_DIR}/bl654_sensor/include)
target_sources(app PRIVATE ${CMAKE_SOURCE_DIR}/bl654_sensor/source/bl654_sensor.c)
endif()

if (CONFIG_COAP_FOTA)
include_directories(${CMAKE_SOURCE_DIR}/coap/include)
target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/coap/source/coap_fota_json_parser.c
    ${CMAKE_SOURCE_DIR}/coap/source/coap_fota.c
    ${CMAKE_SOURCE_DIR}/coap/source/coap_fota_shadow.c
    ${CMAKE_SOURCE_DIR}/coap/source/coap_fota_task.c
)
endif() # CONFIG_COAP_FOTA

if (CONFIG_HTTP_FOTA)
include_directories(${CMAKE_SOURCE_DIR}/http_fota/include)
target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/http_fota/source/http_fota_task.c
    ${CMAKE_SOURCE_DIR}/http_fota/source/http_fota_shadow.c
)
target_sources_ifdef(CONFIG_MODEM_HL7800 app PRIVATE
    ${CMAKE_SOURCE_DIR}/http_fota/source/hl7800_http_fota.c
)
endif() # CONFIG_HTTP_FOTA

if(CONFIG_SHELL AND CONFIG_MODEM_HL7800)
target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/common/src/lcz_modem_shell.c
)
endif()