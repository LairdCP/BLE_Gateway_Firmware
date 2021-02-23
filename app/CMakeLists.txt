cmake_minimum_required(VERSION 3.13.1)

# Re-direct the directory where the 'boards' directory is found from
# $ZEPHYR_BASE to this directory.
set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../)

list(APPEND ZEPHYR_EXTRA_MODULES $ENV{ZEPHYR_BASE}/../modules/memfault-firmware-sdk/ports/nrf-connect-sdk)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(oob_demo)

if(CONFIG_LCZ_MEMFAULT)
zephyr_include_directories(${CMAKE_SOURCE_DIR}/memfault)
endif()

target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/src/main.c
    ${CMAKE_SOURCE_DIR}/src/lte.c
    ${CMAKE_SOURCE_DIR}/src/nv.c
    ${CMAKE_SOURCE_DIR}/src/jsmn_json.c
    ${CMAKE_SOURCE_DIR}/src/button.c
    ${CMAKE_SOURCE_DIR}/src/sensor_state.c
    ${CMAKE_SOURCE_DIR}/src/ble.c
)


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

target_sources_ifdef(CONFIG_CT_WDT app PRIVATE
    ${CMAKE_SOURCE_DIR}/contact_tracing/src/ct_wdt.c)

target_sources_ifdef(CONFIG_CT_SHELL app PRIVATE
    ${CMAKE_SOURCE_DIR}/contact_tracing/src/ct_shell.c
)

target_sources_ifdef(CONFIG_SINGLE_PERIPHERAL app PRIVATE
    ${CMAKE_SOURCE_DIR}/src/single_peripheral.c
)

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/framework_config)
include_directories(${CMAKE_SOURCE_DIR}/../../modules/jsmn)

if(CONFIG_BOARD_MG100)
target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/src/lairdconnect_battery.c
    ${CMAKE_SOURCE_DIR}/src/ble_battery_service.c
    ${CMAKE_SOURCE_DIR}/src/ble_motion_service.c
    ${CMAKE_SOURCE_DIR}/src/sdcard_log.c
)
endif()

if(CONFIG_LWM2M)
include_directories(${CMAKE_SOURCE_DIR}/lwm2m/include)
target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/lwm2m/source/lcz_lwm2m_client.c
    ${CMAKE_SOURCE_DIR}/lwm2m/source/ble_lwm2m_service.c
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
    ${CMAKE_SOURCE_DIR}/src/aws.c
    ${CMAKE_SOURCE_DIR}/src/ble_aws_service.c
)

if(CONFIG_SENSOR_TASK)
target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/bluegrass/source/sensor_log.c
    ${CMAKE_SOURCE_DIR}/bluegrass/source/sensor_table.c
    ${CMAKE_SOURCE_DIR}/bluegrass/source/sensor_task.c
)
endif()

if(CONFIG_SHELL AND CONFIG_APP_AWS_CUSTOMIZATION)
target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/src/aws_config.c
)
endif()
endif() # CONFIG BLUEGRASS

if(CONFIG_BL654_SENSOR)
include_directories(${CMAKE_SOURCE_DIR}/bl654_sensor/include)
target_sources(app PRIVATE ${CMAKE_SOURCE_DIR}/bl654_sensor/source/bl654_sensor.c)
endif()

target_sources_ifdef(CONFIG_BLE_SENSOR_SERVICE app PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ble_sensor_service.c
)

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
endif() # CONFIG_HTTP_FOTA
