# Vendored material-color-utilities (Apache-2.0). Built without Velacut's -Werror flags.
file(GLOB_RECURSE MCU_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cpp/*.cc")
add_library(material_color_utilities STATIC ${MCU_SOURCES})
target_include_directories(material_color_utilities SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(material_color_utilities PUBLIC cxx_std_20)
set_target_properties(material_color_utilities PROPERTIES POSITION_INDEPENDENT_CODE ON)
