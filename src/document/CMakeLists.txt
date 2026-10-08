# velacut_document: open drafts (project + undo + continuous save), the drafts folder (docs/FILE_FORMAT.md §6, §9).
add_library(velacut_document STATIC
    AutoSaver.h AutoSaver.cpp
    Document.h Document.cpp
    DraftLock.h DraftLock.cpp
    DraftStore.h DraftStore.cpp
)
target_include_directories(velacut_document PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(velacut_document PUBLIC Qt6::Core Qt6::Gui velacut_core velacut_common)
velacut_target_options(velacut_document)
