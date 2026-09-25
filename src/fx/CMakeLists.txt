# vedit_fx: effect and transition kernels (CPU reference; GPU later), no MLT dependency (docs/ARCHITECTURE.md §6).
add_library(vedit_fx STATIC
    Image.h
    Composite.h Composite.cpp
)
target_include_directories(vedit_fx PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
vedit_target_options(vedit_fx)
