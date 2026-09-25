# vedit_fx: effect and transition kernels (CPU reference; GPU later) and the asset library; no MLT dependency
# (docs/ARCHITECTURE.md §6).
add_library(vedit_fx STATIC
    Image.h
    Audio.h Audio.cpp
    Color.h Color.cpp
    Composite.h Composite.cpp
    Enhance.h Enhance.cpp
    Library.h Library.cpp
    Transform.h Transform.cpp
    Transition.h Transition.cpp
)
target_include_directories(vedit_fx PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(vedit_fx PUBLIC Qt6::Core)
# The core pack is built in: the application works without any installed data file.
set(core_pack "${CMAKE_SOURCE_DIR}/resources/packs/vedit.core")
qt_add_resources(vedit_fx "vedit_core_pack"
    PREFIX "/vedit/packs/vedit.core"
    BASE "${core_pack}"
    FILES
        "${core_pack}/pack.json"
        "${core_pack}/effects.json"
        "${core_pack}/filters.json"
        "${core_pack}/text-styles.json"
        "${core_pack}/transitions.json"
)
vedit_target_options(vedit_fx)
