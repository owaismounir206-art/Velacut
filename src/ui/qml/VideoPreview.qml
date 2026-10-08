// Video preview that picks the surface matching the scene graph backend: shader-based on Vulkan/OpenGL,
// plain image nodes on the software backend (SPEC 1bis).
import QtQuick
import Velacut.UI
import Velacut.Theme

Item {
    id: root

    property var sink: null
    // Letterbox around the video: a neutral surface color; the video itself is never tinted.
    property color backgroundColor: Theme.color.surfaceContainerLowest

    Accessible.role: Accessible.Animation
    Accessible.name: qsTr("Video preview")

    Loader {
        anchors.fill: parent
        // The backend really in use (Qt may fall back at runtime), not only the one requested at startup.
        sourceComponent: root.GraphicsInfo.api === GraphicsInfo.Software ? softwareSurface : rhiSurface
    }
    Component {
        id: rhiSurface
        VideoSurfaceRhi {
            sink: root.sink
            backgroundColor: root.backgroundColor
        }
    }
    Component {
        id: softwareSurface
        VideoSurfaceSoftware {
            sink: root.sink
            backgroundColor: root.backgroundColor
        }
    }
}
