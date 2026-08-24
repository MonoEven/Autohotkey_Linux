# ahk-inputd: single-instance evdev input broker (check_detail0824 §1-C / M4).
# A standalone daemon with no X11 or AutoHotkey interpreter dependency, so it
# can run under systemd or be invoked by a privileged wrapper; clients (each
# ahk_core script with AHK_INPUT_BACKEND=evdev) connect over a UNIX socket.
add_executable(ahk-inputd inputd.c)
target_compile_options(ahk-inputd PRIVATE -Wall -Wextra)
install(TARGETS ahk-inputd RUNTIME DESTINATION bin)
