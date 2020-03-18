cmake_minimum_required(VERSION 3.13.1)
include($ENV{ZEPHYR_BASE}/cmake/app/boilerplate.cmake NO_POLICY_SCOPE)
project(mg100)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})
include_directories(${CMAKE_SOURCE_DIR}/include)
zephyr_include_directories(${ZEPHYR_BASE}/drivers/modem)

FILE(GLOB fwk_sources ./../../modules/framework/source/*.c)
target_sources(app PRIVATE ${fwk_sources})
include_directories(${CMAKE_SOURCE_DIR}/../../modules/framework/include)
include_directories(${CMAKE_SOURCE_DIR}/framework_config)

include_directories(${CMAKE_SOURCE_DIR}/../../modules/jsmn)