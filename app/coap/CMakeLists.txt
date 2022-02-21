
if (CONFIG_COAP_FOTA)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source/coap_fota_json_parser.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/coap_fota.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/coap_fota_shadow.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/coap_fota_task.c
    )
endif()

