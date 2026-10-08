# velacut_add_test(<name> SOURCES <files...> LIBRARIES <targets...>)
# Every test runs headless and with XDG directories inside the build tree.
function(velacut_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBRARIES" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBRARIES})
    velacut_target_options(${name})
    add_test(NAME ${name} COMMAND ${name})
    set(test_home "${CMAKE_BINARY_DIR}/test-home/${name}")
    # Temporary files too stay inside the build tree (QTemporaryDir uses TMPDIR).
    file(MAKE_DIRECTORY "${test_home}/tmp")
    set_tests_properties(${name} PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy;TMPDIR=${test_home}/tmp;XDG_CONFIG_HOME=${test_home}/config;XDG_DATA_HOME=${test_home}/data;XDG_CACHE_HOME=${test_home}/cache;XDG_STATE_HOME=${test_home}/state;VELACUT_TEST_DATA=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/data;ASAN_OPTIONS=fast_unwind_on_malloc=0;LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/lsan.supp:print_suppressions=0")
endfunction()

add_subdirectory(unit)
add_subdirectory(integration)

# Application smoke tests (SPEC 1bis verification): start velacut headless, play a generated video, require
# frames decoded *and* displayed, exit. The test video is generated with ffmpeg inside the build tree.
find_program(FFMPEG_EXECUTABLE ffmpeg)
if(FFMPEG_EXECUTABLE)
    set(smoke_video "${CMAKE_BINARY_DIR}/testmedia/smoke_720p30.mp4")
    add_test(NAME smoke_media COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/testmedia")
    add_test(NAME smoke_media_generate COMMAND "${FFMPEG_EXECUTABLE}" -hide_banner -loglevel error -y
             -f lavfi -i testsrc2=size=1280x720:rate=30 -f lavfi -i sine=frequency=440:sample_rate=48000
             -t 5 -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest "${smoke_video}")
    set_tests_properties(smoke_media PROPERTIES FIXTURES_SETUP smoke_dir)
    set_tests_properties(smoke_media_generate PROPERTIES FIXTURES_SETUP smoke_video FIXTURES_REQUIRED smoke_dir)
    set(smoke_home "${CMAKE_BINARY_DIR}/test-home/smoke")
    set(smoke_env "QT_QPA_PLATFORM=offscreen;SDL_AUDIODRIVER=dummy;XDG_CONFIG_HOME=${smoke_home}/config;XDG_DATA_HOME=${smoke_home}/data;XDG_CACHE_HOME=${smoke_home}/cache;XDG_STATE_HOME=${smoke_home}/state;VELACUT_NO_SANDBOX=1;ASAN_OPTIONS=fast_unwind_on_malloc=0;LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp:print_suppressions=0")
    add_test(NAME smoke_software COMMAND velacut --smoke-test "${smoke_video}")
    set_tests_properties(smoke_software PROPERTIES FIXTURES_REQUIRED smoke_video TIMEOUT 60
                         ENVIRONMENT "${smoke_env};QT_QUICK_BACKEND=software;LIBGL_ALWAYS_SOFTWARE=1")
    add_test(NAME smoke_safe_mode COMMAND velacut --safe-mode --smoke-test "${smoke_video}")
    set_tests_properties(smoke_safe_mode PROPERTIES FIXTURES_REQUIRED smoke_video TIMEOUT 60 ENVIRONMENT "${smoke_env}")
    add_test(NAME smoke_gallery COMMAND velacut --component-gallery --screenshot "${smoke_home}/gallery.png")
    set_tests_properties(smoke_gallery PROPERTIES TIMEOUT 60 ENVIRONMENT "${smoke_env};QT_QUICK_BACKEND=software")
endif()
