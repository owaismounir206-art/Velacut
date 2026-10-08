add_library(velacut_common INTERFACE)
target_include_directories(velacut_common INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(velacut_common INTERFACE Qt6::Core)
if(VELACUT_DEV_SANDBOX)
    target_compile_definitions(velacut_common INTERFACE VELACUT_DEV_HOME="${VELACUT_DEV_HOME}")
endif()

# velacut_fonts: the fonts shipped with velacut, for the interface (velacut_theme) and for rendering (velacut_engine, also in
# velacut-render).
add_library(velacut_fonts STATIC fonts/BundledFonts.h fonts/BundledFonts.cpp)
qt_add_resources(velacut_fonts "velacut_bundled_fonts"
    PREFIX "/velacut"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../assets"
    FILES
        ../assets/fonts/InterVariable.ttf
        ../assets/fonts/InterVariable-Italic.woff2
)
target_include_directories(velacut_fonts PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(velacut_fonts PUBLIC Qt6::Gui)
velacut_target_options(velacut_fonts)
