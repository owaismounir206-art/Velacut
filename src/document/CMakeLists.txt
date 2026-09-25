# vedit_document: open drafts (project + undo + continuous save), the drafts folder (docs/FILE_FORMAT.md §6, §9).
add_library(vedit_document STATIC
    AutoSaver.h AutoSaver.cpp
    Document.h Document.cpp
    DraftLock.h DraftLock.cpp
    DraftStore.h DraftStore.cpp
)
target_include_directories(vedit_document PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(vedit_document PUBLIC Qt6::Core Qt6::Gui vedit_core vedit_common)
vedit_target_options(vedit_document)
