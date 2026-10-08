add_subdirectory(gpu)

# velacut_engine: MLT adapter (docs/ARCHITECTURE.md §5).
find_package(PkgConfig REQUIRED)
pkg_check_modules(MLTPP REQUIRED IMPORTED_TARGET mlt++-7>=7.20)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)
add_library(velacut_engine STATIC
    analysis/AudioSync.h analysis/AudioSync.cpp
    analysis/BeatDetection.h analysis/BeatDetection.cpp
    analysis/Decoding.h analysis/Decoding.cpp
    analysis/Fingerprint.h analysis/Fingerprint.cpp
    analysis/MediaAnalysis.h analysis/MediaAnalysis.cpp
    analysis/Spectrum.h analysis/Spectrum.cpp
    analysis/MediaImporter.h analysis/MediaImporter.cpp
    analysis/MediaProbe.h analysis/MediaProbe.cpp
    analysis/ReverseProxy.h analysis/ReverseProxy.cpp
    analysis/Cutout.h analysis/Cutout.cpp
    analysis/SmoothMotion.h analysis/SmoothMotion.cpp
    mlt/MltRuntime.h mlt/MltRuntime.cpp
    mlt/Services.h mlt/Services.cpp
    playback/FrameSink.h playback/FrameSink.cpp
    playback/Player.h playback/Player.cpp
    playback/AudioMeters.h playback/AudioMeters.cpp
    playback/PreviewConsumer.h playback/PreviewConsumer.cpp
    playback/TimelinePlayer.h playback/TimelinePlayer.cpp
    render/ExportSettings.h render/ExportSettings.cpp
    text/CaptionRenderer.h text/CaptionRenderer.cpp
    text/TextRenderer.h text/TextRenderer.cpp
    render/Renderer.h render/Renderer.cpp
    render/RenderJob.h render/RenderJob.cpp
    timeline/ClipPlacement.h timeline/ClipPlacement.cpp
    timeline/MediaProducerCache.h timeline/MediaProducerCache.cpp
    timeline/TimelineProjection.h timeline/TimelineProjection.cpp
    gpu/GpuTransitions.h gpu/GpuTransitions.cpp
)
# The GLSL of the GPU path (OpenGL 2.1 / ES 2.0), read at run time from the resources.
qt_add_resources(velacut_engine "velacut_gpu_shaders" PREFIX "/velacut/gpu" BASE gpu/shaders FILES gpu/shaders/transitions.frag)
target_include_directories(velacut_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(velacut_engine PUBLIC Qt6::Core Qt6::Gui Qt6::OpenGL Qt6::Svg velacut_core velacut_common velacut_fonts velacut_gpu PkgConfig::MLTPP PUBLIC velacut_fx PRIVATE PkgConfig::FFMPEG)
velacut_target_options(velacut_engine)

# velacut-render: export in a separate process (docs/ARCHITECTURE.md §5.4).
add_executable(velacut-render render/main.cpp)
target_link_libraries(velacut-render PRIVATE velacut_engine)
set_target_properties(velacut-render PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
velacut_target_options(velacut-render)
