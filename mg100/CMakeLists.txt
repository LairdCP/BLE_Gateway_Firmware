cmake_minimum_required(VERSION 3.13.1)
include($ENV{ZEPHYR_BASE}/cmake/app/boilerplate.cmake NO_POLICY_SCOPE)
project(mg100)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})
include_directories(${CMAKE_SOURCE_DIR}/include)
zephyr_include_directories(${ZEPHYR_BASE}/drivers/modem)

zephyr_library_include_directories($ENV{ZEPHYR_BASE}/samples/bluetooth)