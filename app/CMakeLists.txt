cmake_minimum_required(VERSION 3.13.1)

set(lcz_module_path ${CMAKE_SOURCE_DIR}/../../modules/lib/laird_connect)

if (CONFIG_LWM2M)
list(APPEND OVERLAY_CONFIG
    ${CMAKE_SOURCE_DIR}/overlay_lwm2m_dtls.conf
)
endif()

# Add configuration for building with mcuboot
# because it cannot be done with relative paths in VS code on Windows
if((BOARD STREQUAL mg100) OR (BOARD STREQUAL pinnacle_100_dvk))
set(mcuboot_CONF_FILE
    ${lcz_module_path}/zephyr_lib/mcuboot_config/pinnacle_100.conf
)

if (CONFIG_LWM2M)
list(APPEND OVERLAY_CONFIG
    ${CMAKE_SOURCE_DIR}/overlay_lwm2m_pinnacle_100.conf
)
endif()

elseif((BOARD STREQUAL bl5340_dvk_cpuapp) OR (BOARD STREQUAL bl5340pa_dvk_cpuapp))
set(mcuboot_CONF_FILE
    ${lcz_module_path}/zephyr_lib/mcuboot_config/bl5340.conf
)
endif()

list(APPEND OVERLAY_CONFIG
    ${lcz_module_path}/zephyr_lib/mcumgr_wrapper/config/overlay-mcuboot.conf
)

# Use static partition layout for board
set(PM_STATIC_YML_FILE
    ${lcz_module_path}/zephyr_lib/mcuboot_config/pm_static.${BOARD}.yml
)

# Generate build ID based on UTC timestamp
string(TIMESTAMP build_time "%s" UTC)
set(APP_VERSION_BUILD ${build_time})
set(gen_dir ${CMAKE_BINARY_DIR}/zephyr/include/generated/app)
set(ver_file ${gen_dir}/app_version.h)
configure_file(${CMAKE_SOURCE_DIR}/version/app_version.h.in ${ver_file})
# Get application version
file(READ ${ver_file} ver)
string(REGEX MATCH "APP_VERSION_MAJOR ([0-9]+)" _ ${ver})
set(ver_major ${CMAKE_MATCH_1})
string(REGEX MATCH "APP_VERSION_MINOR ([0-9]+)" _ ${ver})
set(ver_minor ${CMAKE_MATCH_1})
string(REGEX MATCH "APP_VERSION_PATCH ([0-9]+)" _ ${ver})
set(ver_patch ${CMAKE_MATCH_1})
set(app_version ${ver_major}.${ver_minor}.${ver_patch}+${build_time})
message("app version: ${app_version}")

# Set application version for mcuboot
set(mcuboot_ver_conf ${CMAKE_BINARY_DIR}/generated_app_version.conf)
set(APP_IMAGE_VERSION ${app_version})
set(MEMFAULT_APP_VERSION ${ver_major}.${ver_minor}.${ver_patch})
configure_file(${CMAKE_SOURCE_DIR}/version/app_version.conf.in ${mcuboot_ver_conf})

# Prepend the additional boards directory but still allow the original Zephyr
# boards directory to be used with lower priority
list(PREPEND BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(oob_demo)

if(CONFIG_LCZ_MEMFAULT)
zephyr_include_directories(${CMAKE_SOURCE_DIR}/memfault)
if((BOARD STREQUAL mg100) OR (BOARD STREQUAL pinnacle_100_dvk))
zephyr_include_directories(${CMAKE_SOURCE_DIR}/memfault/mg100)
elseif((BOARD STREQUAL bl5340_dvk_cpuapp) OR (BOARD STREQUAL bl5340pa_dvk_cpuapp))
zephyr_include_directories(${CMAKE_SOURCE_DIR}/memfault/bl5340)
endif()
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
target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/display/src/lcd.c
)

# BL5340 has 1 image, BL5340PA has 2 images
target_sources_ifdef(CONFIG_BOARD_BL5340_DVK_CPUAPP app PRIVATE
    ${CMAKE_SOURCE_DIR}/display/src/bl5340_module.c
)
target_sources_ifdef(CONFIG_BOARD_BL5340PA_DVK_CPUAPP app PRIVATE
    ${CMAKE_SOURCE_DIR}/display/src/bl5340pa_module_ext.c
    ${CMAKE_SOURCE_DIR}/display/src/bl5340pa_module_int.c
)
endif()

if(CONFIG_CONTACT_TRACING)
zephyr_include_directories(${CMAKE_SOURCE_DIR}/contact_tracing/include)
target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/contact_tracing/src/ct_ble.c
    ${CMAKE_SOURCE_DIR}/contact_tracing/src/ct_app.c
    ${CMAKE_SOURCE_DIR}/contact_tracing/src/rpc_params.c
    ${CMAKE_SOURCE_DIR}/contact_tracing/src/dfu_smp_c.c
    ${CMAKE_SOURCE_DIR}/contact_tracing/src/lcz_fs_mgmt_intercept.c
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
include_directories(${gen_dir})
include_directories(${CMAKE_SOURCE_DIR}/framework_config)
include_directories(${CMAKE_SOURCE_DIR}/../../modules/lib/jsmn)

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

if(CONFIG_LCZ_MOTION_TEMPERATURE)
target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/common/src/lcz_motion_temperature.c
)
endif()

if(CONFIG_LCZ_LWM2M)
include_directories(${CMAKE_SOURCE_DIR}/lwm2m/include)
target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/lwm2m/source/lcz_lwm2m_client.c
)
if(CONFIG_LCZ_LWM2M_MEMFAULT_TASK)
target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/lwm2m/source/memfault_task.c
)
endif()
endif()

if(CONFIG_LCZ_LWM2M_SENSOR)
target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/lwm2m/source/lcz_lwm2m_gateway.c
    ${CMAKE_SOURCE_DIR}/lwm2m/source/lcz_lwm2m_sensor.c
)
endif()

if(CONFIG_LCZ_LWM2M_FW_UPDATE)
target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/lwm2m/source/lcz_lwm2m_fw_update.c
)
endif()

if(CONFIG_LCZ_LWM2M_CONN_MON)
target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/lwm2m/source/lcz_lwm2m_conn_mon.c
)
endif()

if(CONFIG_LWM2M_UCIFI_BATTERY)
target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/lwm2m/source/lcz_lwm2m_battery.c
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

if(CONFIG_ESS_SENSOR)
include_directories(${CMAKE_SOURCE_DIR}/ess_sensor/include)
target_sources(app PRIVATE ${CMAKE_SOURCE_DIR}/ess_sensor/source/ess_sensor.c)
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
