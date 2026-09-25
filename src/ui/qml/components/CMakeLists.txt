# Vedit.Components: Material 3 components that have no Qt Quick Controls equivalent (icon button, FAB,
# chips, cards, navigation rail, top app bar, snackbar…) and the shared building blocks of the style.
# Names never collide with Qt Quick Controls types (see style/CMakeLists.txt).
qt_add_library(vedit_components STATIC)
qt_add_qml_module(vedit_components
    URI Vedit.Components
    VERSION 1.0
    DEPENDENCIES QtQuick Vedit.Theme
    QML_FILES
        StateLayer.qml Shadow.qml Icon.qml TypeText.qml Divider.qml
        IconButton.qml Fab.qml SegmentedButton.qml Chip.qml Card.qml
        NavigationRail.qml NavigationRailItem.qml TopAppBar.qml Snackbar.qml Badge.qml SearchBar.qml Tabs.qml
)
target_link_libraries(vedit_components PRIVATE Qt6::Quick Qt6::QuickTemplates2 vedit_theme)
