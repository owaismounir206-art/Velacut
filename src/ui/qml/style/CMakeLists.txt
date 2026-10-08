# Velacut.Style: the Qt Quick Controls style (QQuickStyle::setStyle("Velacut.Style")) implementing Material 3
# with our Theme tokens (D-11). Applications use it through `import QtQuick.Controls`, never directly:
# the qmldir import of QtQuick.Controls.Material (the fallback for controls not redefined here, verified
# with a probe: it must be unversioned) re-exports Material's types to direct importers.
qt_add_library(velacut_style STATIC)
qt_add_qml_module(velacut_style
    URI Velacut.Style
    VERSION 1.0
    IMPORTS QtQuick.Controls.Material
    DEPENDENCIES QtQuick Velacut.Theme Velacut.Components
    QML_FILES
        Label.qml Button.qml Slider.qml Switch.qml CheckBox.qml RadioButton.qml TextField.qml TextArea.qml
        ProgressBar.qml BusyIndicator.qml ToolTip.qml Menu.qml MenuItem.qml MenuSeparator.qml Dialog.qml
        ComboBox.qml DialogButtonBox.qml ScrollBar.qml
)
target_link_libraries(velacut_style PRIVATE Qt6::Quick Qt6::QuickControls2 velacut_theme velacut_components)
