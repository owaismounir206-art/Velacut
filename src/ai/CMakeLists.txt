# velacut_ai: the local AI functions (SPEC §5.12), each an AiTask whose result the editor turns into editable changes.
add_library(velacut_ai STATIC
    AiTask.h AiTask.cpp
    Analysis.h Analysis.cpp
    Tasks.h Tasks.cpp
    Highlights.h Highlights.cpp
    Montage.h Montage.cpp
    Script.h Script.cpp
    Separation.h Separation.cpp
    Speech.h Speech.cpp
    Transcript.h Transcript.cpp
    Whisper.h Whisper.cpp
)
target_include_directories(velacut_ai PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(velacut_ai PUBLIC Qt6::Core velacut_core velacut_engine)
velacut_target_options(velacut_ai)
