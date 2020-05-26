cmake_minimum_required(VERSION 3.13.1)
include($ENV{ZEPHYR_BASE}/cmake/app/boilerplate.cmake NO_POLICY_SCOPE)
project(mg100)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})
include_directories(${CMAKE_SOURCE_DIR}/include)
zephyr_include_directories(${ZEPHYR_BASE}/drivers/modem)

include_directories(${CMAKE_SOURCE_DIR}/framework_config)

include_directories(${CMAKE_SOURCE_DIR}/../../modules/jsmn)

if(CONFIG_LWM2M)
include_directories(${CMAKE_SOURCE_DIR}/lwm2m/include)
target_sources(app PRIVATE
    ${CMAKE_SOURCE_DIR}/lwm2m/source/lwm2m_client.c
    ${CMAKE_SOURCE_DIR}/lwm2m/source/ble_lwm2m_service.c
)
endif()
