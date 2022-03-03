
if (CONFIG_BLUEGRASS)
target_include_directories(app PRIVATE
    ${CMAKE_SOURCE_DIR}/../modules/jsmn
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source/bluegrass.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/sensor_cmd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/sensor_gateway_parser.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/shadow_builder.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/to_string.c
    )
endif()

if(CONFIG_SENSOR_TASK)
target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source/sensor_log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/sensor_table.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/sensor_task.c
    )
endif()
