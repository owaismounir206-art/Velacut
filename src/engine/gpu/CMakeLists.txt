find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG_GPU REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil)

# velacut_gpu: GPU capability detection, fallback chains, safe mode, HW video decoder (docs/ARCHITECTURE.md §7).
add_library(velacut_gpu STATIC
    GpuCapabilities.h GpuCapabilities.cpp
    GraphicsSetup.h GraphicsSetup.cpp
    HwVideoDecoder.h HwVideoDecoder.cpp
    ../playback/LruFrameCache.h ../playback/LruFrameCache.cpp
)
target_include_directories(velacut_gpu PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(velacut_gpu PUBLIC Qt6::Core Qt6::Gui velacut_common PkgConfig::FFMPEG_GPU)
velacut_target_options(velacut_gpu)

# velacut-gpuprobe: runs the risky driver calls in a separate process.
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG_CODEC REQUIRED IMPORTED_TARGET libavcodec libavutil)
find_package(Vulkan REQUIRED) # headers only: Qt loads libvulkan at runtime
add_executable(velacut-gpuprobe
    probe/main.cpp
    probe/Probes.h
    probe/GraphicsProbe.cpp
    probe/VideoProbe.cpp
)
target_link_libraries(velacut-gpuprobe PRIVATE velacut_gpu Qt6::Gui Vulkan::Headers PkgConfig::FFMPEG_CODEC)
set_target_properties(velacut-gpuprobe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
velacut_target_options(velacut-gpuprobe)
