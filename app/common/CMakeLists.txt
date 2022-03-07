
target_include_directories(app PRIVATE
    ${CMAKE_SOURCE_DIR}/../modules/jsmn
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/jsmn_json.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/button.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lcz_certs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gateway_common.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control_task.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gateway_fsm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/fota_smp.c
    )

target_sources_ifdef(CONFIG_AWS app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/aws.c
    )

target_sources_ifdef(CONFIG_SHADOW_PARSER app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shadow_parser.c
    )

target_sources_ifdef(CONFIG_SINGLE_PERIPHERAL app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/single_peripheral.c
    )

target_sources_ifdef(CONFIG_WDT app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/wdt.c
    )

target_sources_ifdef(CONFIG_BT app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ble.c
    )

target_sources_ifdef(CONFIG_LTE app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lte.c
    )

target_sources_ifdef(CONFIG_NET_L2_ETHERNET app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ethernet_network.c
    )

target_sources_ifdef(CONFIG_SNTP app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sntp_qrtc.c
    )

target_sources_ifdef(CONFIG_LCZ_SNTP_SHELL app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lcz_sntp_shell.c
    )

if (CONFIG_BOARD_MG100)
target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lairdconnect_battery.c
    )
endif()

if (CONFIG_SD_CARD_LOG)
target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sdcard_log.c
    )
endif()

if (CONFIG_LCZ_MOTION)
target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lcz_motion.c
    )
endif()

if (CONFIG_LCZ_MOTION_TEMPERATURE)
target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lcz_motion_temperature.c
    )
endif()

if (CONFIG_SHELL AND CONFIG_MODEM_HL7800)
target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lcz_modem_shell.c
    )
endif()
