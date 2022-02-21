
if (CONFIG_HTTP_FOTA)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source/http_fota_task.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/http_fota_shadow.c
    )

target_sources_ifdef(CONFIG_MODEM_HL7800 app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source/hl7800_http_fota.c
    )
endif()