#!/bin/bash
# vm_asan_hk.sh -- build ASan binary + run assert_hotkey under Xvfb.
set -u
R=/tmp/xtest_asan_hk.txt
exec > "$R" 2>&1
cd /home/mono/Autohotkey_Linux || exit 1
echo "--- build asan ---"
if [ ! -d build-asan ]; then
  mkdir -p build-asan
  cd build-asan
  cmake ../source/linux -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=address -g" \
    -DCMAKE_C_FLAGS="-fsanitize=address -g" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" > /tmp/asan_cmake.log 2>&1
  cd ..
fi
cmake --build build-asan -j4 2>&1 | grep -E '^.*error:|Built target ahk_core$' | head -4
ls -la build-asan/source/linux/core/ahk_core 2>/dev/null || { echo "NO ASAN BINARY"; echo "asan_done=1"; exit 0; }
echo "--- run assert_hotkey under ASan ---"
pkill -f 'Xvfb :83' 2>/dev/null
sleep 0.5
Xvfb :83 -screen 0 1024x768x24 > /tmp/xvfb83.log 2>&1 &
XPID=$!
sleep 1.5
cd tests/doccheck
rm -rf /tmp/hk_asan_out
mkdir -p /tmp/hk_asan_out
ASAN_OPTIONS=detect_leaks=0 timeout 60 env DISPLAY=:83 /home/mono/Autohotkey_Linux/build-asan/source/linux/core/ahk_core assert_hotkey.ahk > /tmp/hk_asan_out/stdout.log 2>/tmp/hk_asan_out/stderr.log
echo "rc=$?"
kill "$XPID" 2>/dev/null
echo "--- ASan report (if any) ---"
grep -A 25 'ERROR: AddressSanitizer' /tmp/hk_asan_out/stderr.log 2>/dev/null | head -30
echo "--- stderr tail ---"
tail -6 /tmp/hk_asan_out/stderr.log 2>/dev/null
echo "asan_hk_done=1"
