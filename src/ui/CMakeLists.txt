add_subdirectory(qml/components)
add_subdirectory(qml/style)

# Vedit.UI: C++ items and controllers exposed to QML, and the application screens.
qt_add_library(vedit_ui STATIC)
qt_add_qml_module(vedit_ui
    URI Vedit.UI
    VERSION 1.0
    DEPENDENCIES QtQuick Vedit.Theme Vedit.Components
    SOURCES
        items/VideoFit.h
        items/VideoSurfaceRhi.h items/VideoSurfaceRhi.cpp
        items/VideoSurfaceSoftware.h items/VideoSurfaceSoftware.cpp
        items/MediaThumbnail.h items/MediaThumbnail.cpp
        items/AssetThumbnail.h items/AssetThumbnail.cpp
        items/WaveformView.h items/WaveformView.cpp
        items/ScopeView.h items/ScopeView.cpp
        models/AudioLibraryModel.h models/AudioLibraryModel.cpp
        models/AiModelsModel.h models/AiModelsModel.cpp
        models/AssetLibraryModel.h models/AssetLibraryModel.cpp
        models/DraftsModel.h models/DraftsModel.cpp
        models/PackagesModel.h models/PackagesModel.cpp
        models/BrandKitModel.h models/BrandKitModel.cpp
        models/MediaPoolModel.h models/MediaPoolModel.cpp
        models/TimelineModel.h models/TimelineModel.cpp
        controllers/EngineTypes.h
        controllers/AppController.h controllers/AppController.cpp
        controllers/EditorController.h controllers/EditorController.cpp
        controllers/AiController.h controllers/AiController.cpp
        controllers/CaptionsController.h controllers/CaptionsController.cpp
        controllers/ClipInspector.h controllers/ClipInspector.cpp
        controllers/TranscriptController.h controllers/TranscriptController.cpp
        controllers/ActionRegistry.h controllers/ActionRegistry.cpp
        controllers/RecordController.h controllers/RecordController.cpp
    QML_FILES
        qml/Main.qml
        qml/Gallery.qml
        qml/VideoPreview.qml
        qml/HomeScreen.qml
        qml/DraftCard.qml
        qml/EditorScreen.qml
        qml/EditorTopBar.qml
        qml/PreviewPanel.qml
        qml/MediaPanel.qml
        qml/MediaTile.qml
        qml/AudioPanel.qml
        qml/ContextToolbar.qml
        qml/TimelineView.qml
        qml/TimelineClip.qml
        qml/ExportDialog.qml
        qml/PropertiesPanel.qml
        qml/PropertySection.qml
        qml/PropertySlider.qml
        qml/EffectSlider.qml
        qml/ColorSwatches.qml
        qml/AssetPanel.qml
        qml/CanvasHandles.qml
        qml/FormatButton.qml
        qml/SearchDialog.qml
        qml/LevelMeter.qml
        qml/KeyframeButton.qml
        qml/CurveEditor.qml
        qml/SpeedCurveEditor.qml
        qml/ColorWheel.qml
        qml/ColorCurvesEditor.qml
        qml/ScopePanel.qml
        qml/Teleprompter.qml
        qml/RecordDialog.qml
        qml/ShortcutsDialog.qml
        qml/LibraryHeader.qml
        qml/TemplateCard.qml
        qml/PreferencesDialog.qml
        qml/SlideshowDialog.qml
        qml/BrandKitPanel.qml
        qml/CaptionsPanel.qml
        qml/TranscriptPanel.qml
        qml/FullScreenPreview.qml
)
qt_add_shaders(vedit_ui "vedit_preview_shaders"
    PREFIX "/vedit/shaders"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/items/shaders"
    FILES
        items/shaders/preview.vert
        items/shaders/preview.frag
)
target_include_directories(vedit_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_CURRENT_SOURCE_DIR}"
                                   # the generated QML registration includes headers by file name
                                   PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/items" "${CMAKE_CURRENT_SOURCE_DIR}/controllers"
                                           "${CMAKE_CURRENT_SOURCE_DIR}/models")
target_link_libraries(vedit_ui PUBLIC Qt6::Network vedit_engine vedit_ai vedit_document vedit_gpu vedit_theme Qt6::Quick Qt6::QuickControls2 Qt6::DBus
                               PRIVATE Qt6::GuiPrivate)
vedit_target_options(vedit_ui)
