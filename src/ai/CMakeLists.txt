# vedit_ai: the local AI functions (SPEC §5.12), each an AiTask whose result the editor turns into editable changes.
add_library(vedit_ai STATIC
    AiTask.h AiTask.cpp
    Analysis.h Analysis.cpp
    Tasks.h Tasks.cpp
    Transcript.h Transcript.cpp
    Whisper.h Whisper.cpp
)
target_include_directories(vedit_ai PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(vedit_ai PUBLIC Qt6::Core vedit_core vedit_engine)
vedit_target_options(vedit_ai)
