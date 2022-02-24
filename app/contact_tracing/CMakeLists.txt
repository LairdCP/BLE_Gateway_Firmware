

if(CONFIG_CONTACT_TRACING)
# Target scope cannot be used because the lcz_fs_mgmt module
# gets configuration from this location.
zephyr_include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ct_ble.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ct_app.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rpc_params.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dfu_smp_c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lcz_fs_mgmt_intercept.c
    )

target_sources_ifdef(CONFIG_CT_SHELL app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ct_shell.c
    )
endif()
