cmake_minimum_required(VERSION 3.13.1)

# Add framework ID and msgcode files prior to Zephyr package init
set(FWK_APP_ID_FILE_LIST ${CMAKE_SOURCE_DIR}/app/framework_config/FrameworkIds.h)
set(FWK_APP_MSG_FILE_LIST ${CMAKE_SOURCE_DIR}/app/framework_config/FrameworkMsgCodes.h)

# Add configuration for building with mcuboot
# because it cannot be done with relative paths in VS code on Windows
if((BOARD STREQUAL mg100) OR (BOARD STREQUAL pinnacle_100_dvk))
set(mcuboot_CONF_FILE
    ${CMAKE_SOURCE_DIR}/../modules/zephyr_lib/mcuboot_config/pinnacle_100.conf
)

if (CONFIG_LWM2M)
list(APPEND OVERLAY_CONFIG
    ${CMAKE_SOURCE_DIR}/app/overlay_lwm2m_pinnacle_100.conf
)
endif()

elseif((BOARD STREQUAL bl5340_dvk_cpuapp) OR (BOARD STREQUAL bl5340pa_dvk_cpuapp))
set(mcuboot_CONF_FILE
    ${CMAKE_SOURCE_DIR}/../modules/zephyr_lib/mcuboot_config/bl5340.conf
)
endif()

list(APPEND OVERLAY_CONFIG
    ${CMAKE_SOURCE_DIR}/../modules/zephyr_lib/mcumgr_wrapper/config/overlay-mcuboot.conf
)

# Use static partition layout for board
set(PM_STATIC_YML_FILE
    ${CMAKE_SOURCE_DIR}/../modules/zephyr_lib/mcuboot_config/pm_static.${BOARD}.yml
)

if(DEFINED CONFIG_LCZ_MEMFAULT OR DEFINED CONFIG_MEMFAULT_NCS_PROJECT_KEY)
list(APPEND OVERLAY_CONFIG
    ${CMAKE_SOURCE_DIR}/app/overlay_memfault.conf
)
endif()

# Prepend the additional boards directory but still allow the original Zephyr
# boards directory to be used with lower priority
list(PREPEND BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(oob_demo)

add_subdirectory(app)

#
# These items may be custom for a particular application
#
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app/version
    )

if (CONFIG_FRAMEWORK)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app/framework_config
    )
endif()

if (CONFIG_LCZ_MEMFAULT)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app/memfault
    )
endif()
