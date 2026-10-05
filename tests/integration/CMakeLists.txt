vedit_add_test(tst_projectroundtrip SOURCES tst_projectroundtrip.cpp LIBRARIES vedit_core)
vedit_add_test(tst_player SOURCES tst_player.cpp LIBRARIES vedit_engine)
vedit_add_test(tst_projection SOURCES tst_projection.cpp TestMedia.h LIBRARIES vedit_engine)
vedit_add_test(tst_timelineplayer SOURCES tst_timelineplayer.cpp TestMedia.h LIBRARIES vedit_engine)
target_compile_definitions(tst_timelineplayer PRIVATE VEDIT_RENDER_EXECUTABLE="$<TARGET_FILE:vedit-render>")
add_dependencies(tst_timelineplayer vedit-render)
vedit_add_test(tst_export SOURCES tst_export.cpp TestMedia.h LIBRARIES vedit_engine)
target_compile_definitions(tst_export PRIVATE VEDIT_RENDER_EXECUTABLE="$<TARGET_FILE:vedit-render>"
                                     VEDIT_GPUPROBE_EXECUTABLE="$<TARGET_FILE:vedit-gpuprobe>")
add_dependencies(tst_export vedit-render vedit-gpuprobe)
vedit_add_test(tst_document SOURCES tst_document.cpp LIBRARIES vedit_document)
vedit_add_test(tst_import SOURCES tst_import.cpp TestMedia.h LIBRARIES vedit_engine)
target_compile_definitions(tst_import PRIVATE VEDIT_RENDER_EXECUTABLE="$<TARGET_FILE:vedit-render>")
add_dependencies(tst_import vedit-render)
vedit_add_test(tst_editor SOURCES tst_editor.cpp TestMedia.h LIBRARIES vedit_ui)
target_compile_definitions(tst_editor PRIVATE VEDIT_RENDER_EXECUTABLE="$<TARGET_FILE:vedit-render>")
add_dependencies(tst_editor vedit-render)
vedit_add_test(tst_ui SOURCES tst_ui.cpp TestMedia.h
               LIBRARIES vedit_ui vedit_uiplugin vedit_style vedit_styleplugin vedit_components vedit_componentsplugin
                         vedit_theme vedit_themeplugin)
target_compile_definitions(tst_ui PRIVATE VEDIT_RENDER_EXECUTABLE="$<TARGET_FILE:vedit-render>")
add_dependencies(tst_ui vedit-render)
set_tests_properties(tst_ui PROPERTIES TIMEOUT 600) # ~260 s with ASan
vedit_add_test(tst_services SOURCES tst_services.cpp TestMedia.h LIBRARIES vedit_engine)
# CPU/GPU transitions (SPEC §5.11bis, Phase 5 criterion): on the machine's GPU, and again on Mesa's software rasterizer.
vedit_add_test(tst_gputransitions SOURCES tst_gputransitions.cpp LIBRARIES vedit_engine)
add_test(NAME tst_gputransitions_llvmpipe COMMAND tst_gputransitions)
get_test_property(tst_gputransitions ENVIRONMENT gputransitions_env)
# llvmpipe uses every core by design: alone, so that it does not starve the timing of the other tests.
set_tests_properties(tst_gputransitions_llvmpipe PROPERTIES ENVIRONMENT "${gputransitions_env};LIBGL_ALWAYS_SOFTWARE=1" RUN_SERIAL TRUE)
# The paused sdl2_audio preview consumer can miss its wakeups when the machine is saturated (docs/PROGRESS.md, known
# bugs): the player test runs alone, so that it tests the player and not the scheduler.
set_tests_properties(tst_timelineplayer PROPERTIES RUN_SERIAL TRUE)
