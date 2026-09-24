# vedit_gpu: GPU capability detection, fallback chains, safe mode (docs/ARCHITECTURE.md §7).
add_library(vedit_gpu STATIC
    GpuCapabilities.h GpuCapabilities.cpp
    GraphicsSetup.h GraphicsSetup.cpp
)
target_include_directories(vedit_gpu PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(vedit_gpu PUBLIC Qt6::Core vedit_common)
vedit_target_options(vedit_gpu)

# vedit-gpuprobe: runs the risky driver calls in a separate process.
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG_CODEC REQUIRED IMPORTED_TARGET libavcodec libavutil)
find_package(Vulkan REQUIRED) # headers only: Qt loads libvulkan at runtime
add_executable(vedit-gpuprobe
    probe/main.cpp
    probe/Probes.h
    probe/GraphicsProbe.cpp
    probe/VideoProbe.cpp
)
target_link_libraries(vedit-gpuprobe PRIVATE vedit_gpu Qt6::Gui Vulkan::Headers PkgConfig::FFMPEG_CODEC)
set_target_properties(vedit-gpuprobe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
vedit_target_options(vedit-gpuprobe)
