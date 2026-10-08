cmake_minimum_required(VERSION 3.25)

project(velacut
    VERSION 0.1.0
    DESCRIPTION "Offline desktop video editor for Linux"
    HOMEPAGE_URL "https://github.com/owaismounir206-art/Velacut"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(VelacutCompilerOptions)

# During development the app and the tests keep every XDG directory inside the build tree,
# so nothing is written to the user's home (see docs/ARCHITECTURE.md, D-03).
option(VELACUT_DEV_SANDBOX "Redirect XDG directories to <build>/dev-home" ON)
set(VELACUT_DEV_HOME "${CMAKE_BINARY_DIR}/dev-home")

# GuiPrivate: QRhi headers (<rhi/qrhi.h>) for the video preview, a semi-public API with limited
# compatibility guarantees (docs/ARCHITECTURE.md D-17). The warning is known and accepted.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui GuiPrivate OpenGL DBus Network Qml Quick QuickControls2 ShaderTools LinguistTools Test Svg)
qt_standard_project_setup(REQUIRES 6.7)
qt_policy(SET QTP0004 NEW)
# QML modules are laid out as <build>/qml/Velacut/<Module> so that qmllint and the QML engine find them.
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")

enable_testing()

add_subdirectory(third_party/material-color-utilities)
add_subdirectory(src)
add_subdirectory(tests)

# Installation (when VELACUT_DEV_SANDBOX is OFF, i.e. for packaging and system installs)
if(NOT VELACUT_DEV_SANDBOX)
    install(TARGETS velacut DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(TARGETS velacut-render DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(TARGETS velacut-gpuprobe DESTINATION ${CMAKE_INSTALL_BINDIR})

    # Desktop entry
    install(FILES packaging/velacut.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

    # Application icon
    install(FILES packaging/velacut.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

    # MIME type definition
    install(FILES packaging/velacut.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)

    # AppStream metadata
    install(FILES packaging/velacut.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)

    # Man page
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/docs/velacut.1")
        install(FILES docs/velacut.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
    endif()

    # License
    install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/velacut)
endif()

# Uninstall target
if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        IMMEDIATE @ONLY)
    add_custom_target(uninstall
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")
endif()

# CPack packaging configuration
set(CPACK_PACKAGE_NAME "velacut")
set(CPACK_PACKAGE_VENDOR "Velacut Team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Offline desktop video editor for Linux")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "TGZ;DEB")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Owais <https://github.com/owaismounir206-art/Velacut>")
set(CPACK_DEBIAN_PACKAGE_SECTION "video")

add_subdirectory(tools)
