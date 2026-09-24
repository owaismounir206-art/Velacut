# vedit_add_test(<name> SOURCES <files...> LIBRARIES <targets...>)
# Every test runs headless and with XDG directories inside the build tree.
function(vedit_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBRARIES" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBRARIES})
    vedit_target_options(${name})
    add_test(NAME ${name} COMMAND ${name})
    set(test_home "${CMAKE_BINARY_DIR}/test-home/${name}")
    set_tests_properties(${name} PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${test_home}/config;XDG_DATA_HOME=${test_home}/data;XDG_CACHE_HOME=${test_home}/cache;XDG_STATE_HOME=${test_home}/state;VEDIT_TEST_DATA=${CMAKE_CURRENT_SOURCE_DIR}/data")
endfunction()

add_subdirectory(unit)
add_subdirectory(integration)
