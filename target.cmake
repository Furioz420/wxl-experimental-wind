target_include_directories(${wxl_ext_name} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/extension-support/environment"
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui")
target_compile_definitions(${wxl_ext_name} PRIVATE
    WXL_ENVIRONMENT_NAME="${wxl_ext_name}" "IMGUI_API=__declspec(dllimport)")
target_link_libraries(${wxl_ext_name} PRIVATE WarcraftXL d3d9)
target_compile_definitions(${wxl_ext_name} PRIVATE WXL_ENVIRONMENT_SWITCH="WXL_EXPERIMENTAL_WIND" WXL_ENVIRONMENT_WIND_PROVIDER)
