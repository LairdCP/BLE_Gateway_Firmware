
if (CONFIG_LCZ_LWM2M)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source/lcz_lwm2m_client.c
    )
endif()

if (CONFIG_LCZ_LWM2M_SENSOR)
target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source/lcz_lwm2m_gateway.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/lcz_lwm2m_sensor.c
    )
endif()

if (CONFIG_LCZ_LWM2M_FW_UPDATE)
target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source/lcz_lwm2m_fw_update.c
    )
endif()

if (CONFIG_LCZ_LWM2M_CONN_MON)
target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source/lcz_lwm2m_conn_mon.c
    )
endif()

if (CONFIG_LWM2M_UCIFI_BATTERY)
target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source/lcz_lwm2m_battery.c
    )
endif()
