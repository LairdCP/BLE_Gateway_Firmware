
if (CONFIG_DISPLAY)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lcd.c
    )

# BL5340 has 1 image, BL5340PA has 2 images
target_sources_ifdef(CONFIG_BOARD_BL5340_DVK_CPUAPP app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bl5340_module.c
    )

target_sources_ifdef(CONFIG_BOARD_BL5340PA_DVK_CPUAPP app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bl5340pa_module_ext.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bl5340pa_module_int.c
    )
endif()