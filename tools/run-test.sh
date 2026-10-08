#!/bin/sh
# Runs one test binary with the same environment as CTest (tests/CMakeLists.txt), e.g. to repeat a flaky test:
#   tools/run-test.sh build tst_timelineplayer [test function]
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
build=$(cd "$1" && pwd)
test=$2
shift 2
home=$build/test-home/$test
mkdir -p "$home/tmp"
binary=$(find "$build/tests" -name "$test" -type f -perm -u+x | head -1)
exec env QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy TMPDIR="$home/tmp" XDG_CONFIG_HOME="$home/config" \
    XDG_DATA_HOME="$home/data" XDG_CACHE_HOME="$home/cache" XDG_STATE_HOME="$home/state" \
    VELACUT_TEST_DATA="$root/tests/data" ASAN_OPTIONS=fast_unwind_on_malloc=0 \
    LSAN_OPTIONS="suppressions=$root/tests/lsan.supp:print_suppressions=0" "$binary" "$@"
