add_subdirectory(gpu)

# vedit_engine: MLT adapter (docs/ARCHITECTURE.md §5).
find_package(PkgConfig REQUIRED)
pkg_check_modules(MLTPP REQUIRED IMPORTED_TARGET mlt++-7>=7.20)
add_library(vedit_engine STATIC
    mlt/MltRuntime.h mlt/MltRuntime.cpp
    mlt/Services.h mlt/Services.cpp
    playback/FrameSink.h playback/FrameSink.cpp
    playback/Player.h playback/Player.cpp
    playback/PreviewConsumer.h playback/PreviewConsumer.cpp
    playback/TimelinePlayer.h playback/TimelinePlayer.cpp
    timeline/MediaProducerCache.h timeline/MediaProducerCache.cpp
    timeline/TimelineProjection.h timeline/TimelineProjection.cpp
)
target_include_directories(vedit_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(vedit_engine PUBLIC Qt6::Core Qt6::Gui vedit_core vedit_common PkgConfig::MLTPP PRIVATE vedit_fx)
vedit_target_options(vedit_engine)
