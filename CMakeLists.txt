cmake_minimum_required(VERSION 3.25)

project(vedit
    VERSION 0.1.0
    DESCRIPTION "Offline desktop video editor for Linux"
    HOMEPAGE_URL "https://example.invalid/vedit"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(VeditCompilerOptions)

# During development the app and the tests keep every XDG directory inside the build tree,
# so nothing is written to the user's home (see docs/ARCHITECTURE.md, D-03).
option(VEDIT_DEV_SANDBOX "Redirect XDG directories to <build>/dev-home" ON)
set(VEDIT_DEV_HOME "${CMAKE_BINARY_DIR}/dev-home")

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Test)
qt_standard_project_setup(REQUIRES 6.7)

enable_testing()

add_subdirectory(third_party/material-color-utilities)
add_subdirectory(src)
add_subdirectory(tests)
