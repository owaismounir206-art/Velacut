# vedit_fx: effect and transition kernels (CPU reference; GPU later), no MLT dependency (docs/ARCHITECTURE.md §6).
add_library(vedit_fx STATIC
    Image.h
    Audio.h Audio.cpp
    Color.h Color.cpp
    Composite.h Composite.cpp
    Transform.h Transform.cpp
    Transition.h Transition.cpp
)
target_include_directories(vedit_fx PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
vedit_target_options(vedit_fx)
