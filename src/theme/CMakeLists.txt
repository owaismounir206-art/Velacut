# vedit_theme: Material 3 design system (QML module "Vedit.Theme", singleton Theme).
qt_add_library(vedit_theme STATIC)
qt_add_qml_module(vedit_theme
    URI Vedit.Theme
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
qt_add_resources(vedit_theme "vedit_fonts"
    PREFIX "/vedit"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../assets"
    FILES
        ../assets/fonts/InterVariable.ttf
        ../assets/fonts/InterVariable-Italic.woff2
        ../assets/icons/MaterialSymbolsRounded.woff2
)
target_include_directories(vedit_theme PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(vedit_theme PUBLIC Qt6::Gui Qt6::DBus Qt6::Qml vedit_common
                                  PRIVATE material_color_utilities)
vedit_target_options(vedit_theme)
