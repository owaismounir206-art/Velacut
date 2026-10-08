# velacut_theme: Material 3 design system (QML module "Velacut.Theme", singleton Theme).
qt_add_library(velacut_theme STATIC)
qt_add_qml_module(velacut_theme
    URI Velacut.Theme
    VERSION 1.0
    SOURCES
        ColorScheme.h ColorScheme.cpp
        SchemeGenerator.h SchemeGenerator.cpp
        SystemAppearance.h SystemAppearance.cpp
        ThemeColors.h
        ThemeTypography.h ThemeTypography.cpp
        ThemeTokens.h
        ThemeManager.h ThemeManager.cpp
)
qt_add_resources(velacut_theme "velacut_icon_font"
    PREFIX "/velacut"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../assets"
    FILES
        ../assets/icons/MaterialSymbolsRounded.woff2
        ../assets/icons/MaterialSymbolsRounded.codepoints
)
target_include_directories(velacut_theme PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(velacut_theme PUBLIC Qt6::Gui Qt6::DBus Qt6::Qml velacut_common velacut_fonts
                                  PRIVATE material_color_utilities)
velacut_target_options(velacut_theme)
