# vedit_core: pure data model. Depends only on Qt Core/Gui (Gui is needed for QUndoStack).
# Must never depend on MLT or QML (docs/ARCHITECTURE.md §2).
add_library(vedit_core STATIC
    time/Rational.h time/Rational.cpp
    time/RationalTime.h time/RationalTime.cpp
)
target_include_directories(vedit_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(vedit_core PUBLIC Qt6::Core Qt6::Gui)
vedit_target_options(vedit_core)
