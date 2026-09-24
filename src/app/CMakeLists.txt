qt_add_executable(vedit
    main.cpp
    Logging.h Logging.cpp
)
target_link_libraries(vedit PRIVATE
    vedit_ui vedit_uiplugin
    vedit_style vedit_styleplugin
    vedit_components vedit_componentsplugin
    vedit_theme vedit_themeplugin
    vedit_engine vedit_gpu vedit_common
    Qt6::Quick Qt6::QuickControls2
)
target_compile_definitions(vedit PRIVATE VEDIT_VERSION="${PROJECT_VERSION}")
set_target_properties(vedit PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_dependencies(vedit vedit-gpuprobe)
vedit_target_options(vedit)
