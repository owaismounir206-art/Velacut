qt_add_executable(velacut
    main.cpp
    Logging.h Logging.cpp
)
target_link_libraries(velacut PRIVATE
    velacut_ui velacut_uiplugin
    velacut_style velacut_styleplugin
    velacut_components velacut_componentsplugin
    velacut_theme velacut_themeplugin
    velacut_engine velacut_gpu velacut_common
    Qt6::Quick Qt6::QuickControls2
)
target_compile_definitions(velacut PRIVATE VELACUT_VERSION="${PROJECT_VERSION}")
set_target_properties(velacut PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_dependencies(velacut velacut-gpuprobe)
velacut_target_options(velacut)

# Translations (Qt Linguist). Source strings are English; Italian in i18n/velacut_it.ts.
# Update the .ts files with: cmake --build build --target update_translations
qt_add_translations(velacut
    SOURCE_TARGETS velacut velacut_ui velacut_components velacut_style velacut_theme velacut_engine velacut_gpu velacut_core velacut_fx velacut_ai
                   velacut_document
    TS_FILES "${CMAKE_SOURCE_DIR}/i18n/velacut_it.ts"
    # English needs a translation only for plurals ("%n clips deleted").
    PLURALS_TS_FILE "${CMAKE_SOURCE_DIR}/i18n/velacut_en.ts"
    RESOURCE_PREFIX "/i18n"
    LUPDATE_OPTIONS -no-obsolete -locations relative
)
