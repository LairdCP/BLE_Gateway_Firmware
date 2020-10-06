cmake_minimum_required(VERSION 3.13.1)

# Re-direct the directory where the 'boards' directory is found from
# $ZEPHYR_BASE to this directory.
set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mg100)

target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/src/main.c
    ${CMAKE_SOURCE_DIR}/src/lte.c
    ${CMAKE_SOURCE_DIR}/src/nv.c
    ${CMAKE_SOURCE_DIR}/src/single_peripheral.c
    ${CMAKE_SOURCE_DIR}/src/lairdconnect_battery.c
    ${CMAKE_SOURCE_DIR}/src/ble_battery_service.c
    ${CMAKE_SOURCE_DIR}/src/ble_motion_service.c
    ${CMAKE_SOURCE_DIR}/src/sdcard_log.c
)

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/framework_config)
include_directories(${CMAKE_SOURCE_DIR}/../../modules/jsmn)

if(CONFIG_LWM2M)
include_directories(${CMAKE_SOURCE_DIR}/lwm2m/include)
target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/lwm2m/source/lwm2m_client.c
    ${CMAKE_SOURCE_DIR}/lwm2m/source/ble_lwm2m_service.c
)
endif()

if(CONFIG_BLUEGRASS)
include_directories(${CMAKE_SOURCE_DIR}/bluegrass/include)
FILE(GLOB bluegrass_sources ${CMAKE_SOURCE_DIR}/bluegrass/source/*.c)
target_sources(app PRIVATE
    ${bluegrass_sources}
    ${CMAKE_SOURCE_DIR}/src/aws.c
    ${CMAKE_SOURCE_DIR}/src/ble_aws_service.c
)
endif()

if(CONFIG_BL654_SENSOR)
include_directories(${CMAKE_SOURCE_DIR}/bl654_sensor/include)
target_sources(app PRIVATE ${CMAKE_SOURCE_DIR}/bl654_sensor/source/ble_sensor_service.c)
target_sources(app PRIVATE ${CMAKE_SOURCE_DIR}/bl654_sensor/source/bl654_sensor.c)
endif()

if (CONFIG_LAIRDCONNECTIVITY_BLR)
target_sources(app PRIVATE ${CMAKE_SOURCE_DIR}/src/bootloader.c)
endif()
