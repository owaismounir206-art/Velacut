add_subdirectory(gpu)

# vedit_engine: MLT adapter (docs/ARCHITECTURE.md §5).
find_package(PkgConfig REQUIRED)
pkg_check_modules(MLTPP REQUIRED IMPORTED_TARGET mlt++-7>=7.20)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)
add_library(vedit_engine STATIC
    analysis/AudioSync.h analysis/AudioSync.cpp
    analysis/BeatDetection.h analysis/BeatDetection.cpp
    analysis/Decoding.h analysis/Decoding.cpp
    analysis/Fingerprint.h analysis/Fingerprint.cpp
    analysis/MediaAnalysis.h analysis/MediaAnalysis.cpp
    analysis/Spectrum.h analysis/Spectrum.cpp
    analysis/MediaImporter.h analysis/MediaImporter.cpp
    analysis/MediaProbe.h analysis/MediaProbe.cpp
    analysis/ReverseProxy.h analysis/ReverseProxy.cpp
    mlt/MltRuntime.h mlt/MltRuntime.cpp
    mlt/Services.h mlt/Services.cpp
    playback/FrameSink.h playback/FrameSink.cpp
    playback/Player.h playback/Player.cpp
    playback/AudioMeters.h playback/AudioMeters.cpp
    playback/PreviewConsumer.h playback/PreviewConsumer.cpp
    playback/TimelinePlayer.h playback/TimelinePlayer.cpp
    render/ExportSettings.h render/ExportSettings.cpp
    text/TextRenderer.h text/TextRenderer.cpp
    render/Renderer.h render/Renderer.cpp
    render/RenderJob.h render/RenderJob.cpp
    timeline/ClipPlacement.h timeline/ClipPlacement.cpp
    timeline/MediaProducerCache.h timeline/MediaProducerCache.cpp
    timeline/TimelineProjection.h timeline/TimelineProjection.cpp
)
target_include_directories(vedit_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(vedit_engine PUBLIC Qt6::Core Qt6::Gui Qt6::Svg vedit_core vedit_common PkgConfig::MLTPP PUBLIC vedit_fx PRIVATE PkgConfig::FFMPEG)
vedit_target_options(vedit_engine)

# vedit-render: export in a separate process (docs/ARCHITECTURE.md §5.4).
add_executable(vedit-render render/main.cpp)
target_link_libraries(vedit-render PRIVATE vedit_engine)
set_target_properties(vedit-render PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
vedit_target_options(vedit-render)
