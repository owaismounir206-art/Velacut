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

# Translations (Qt Linguist). Source strings are English; Italian in i18n/vedit_it.ts.
# Update the .ts files with: cmake --build build --target update_translations
qt_add_translations(vedit
    SOURCE_TARGETS vedit vedit_ui vedit_components vedit_style vedit_theme vedit_engine vedit_gpu vedit_core
                   vedit_document
    TS_FILES "${CMAKE_SOURCE_DIR}/i18n/vedit_it.ts"
    # English needs a translation only for plurals ("%n clips deleted").
    PLURALS_TS_FILE "${CMAKE_SOURCE_DIR}/i18n/vedit_en.ts"
    RESOURCE_PREFIX "/i18n"
    LUPDATE_OPTIONS -no-obsolete -locations relative
)
