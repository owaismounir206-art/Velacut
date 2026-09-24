# Compiler options shared by every vedit target (not applied to third_party code).

option(VEDIT_WERROR "Treat compiler warnings as errors" ON)
option(VEDIT_SANITIZERS "Enable AddressSanitizer + UndefinedBehaviorSanitizer (always on in Debug builds)" OFF)

function(vedit_target_options target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(VEDIT_WERROR)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
    target_compile_definitions(${target} PRIVATE
        QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII QT_NO_URL_CAST_FROM_STRING
        QT_NO_NARROWING_CONVERSIONS_IN_CONNECT QT_USE_QSTRINGBUILDER)
    if(VEDIT_SANITIZERS OR CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()
