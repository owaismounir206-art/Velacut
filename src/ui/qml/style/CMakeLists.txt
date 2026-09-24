# Vedit.Style: the Qt Quick Controls style (QQuickStyle::setStyle("Vedit.Style")) implementing Material 3
# with our Theme tokens (D-11). Applications use it through `import QtQuick.Controls`, never directly:
# the qmldir import of QtQuick.Controls.Material (the fallback for controls not redefined here, verified
# with a probe: it must be unversioned) re-exports Material's types to direct importers.
qt_add_library(vedit_style STATIC)
qt_add_qml_module(vedit_style
    URI Vedit.Style
    VERSION 1.0
    IMPORTS QtQuick.Controls.Material
    DEPENDENCIES QtQuick Vedit.Theme Vedit.Components
    QML_FILES
        Label.qml Button.qml Slider.qml Switch.qml CheckBox.qml RadioButton.qml TextField.qml
        ProgressBar.qml BusyIndicator.qml ToolTip.qml Menu.qml MenuItem.qml MenuSeparator.qml Dialog.qml
)
target_link_libraries(vedit_style PRIVATE Qt6::Quick Qt6::QuickControls2 vedit_theme vedit_components)
