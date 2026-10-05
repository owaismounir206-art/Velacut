add_library(vedit_common INTERFACE)
target_include_directories(vedit_common INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(vedit_common INTERFACE Qt6::Core)
if(VEDIT_DEV_SANDBOX)
    target_compile_definitions(vedit_common INTERFACE VEDIT_DEV_HOME="${VEDIT_DEV_HOME}")
endif()

# vedit_fonts: the fonts shipped with vedit, for the interface (vedit_theme) and for rendering (vedit_engine, also in
# vedit-render).
add_library(vedit_fonts STATIC fonts/BundledFonts.h fonts/BundledFonts.cpp)
qt_add_resources(vedit_fonts "vedit_bundled_fonts"
    PREFIX "/vedit"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../assets"
    FILES
        ../assets/fonts/InterVariable.ttf
        ../assets/fonts/InterVariable-Italic.woff2
)
target_include_directories(vedit_fonts PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(vedit_fonts PUBLIC Qt6::Gui)
vedit_target_options(vedit_fonts)
