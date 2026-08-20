# Linux 移植目录

本目录存放 AutoHotkey Linux 化过程中新增的平台抽象与 Linux 后端代码。

当前内容：

- `PlatformAbstraction.h`：平台抽象接口，Win32 模块围绕该接口隔离。
- **X11 后端已经实现**（`core_hotkey_linux.cpp`、`core_input_linux.cpp`、
  `core_capture_linux.cpp`、`core_win_linux.cpp`、`core_gshortcut_linux.cpp`
  等）：XGrabKey 全局热键、XTEST 输入注入、热字串与 InputHook 捕获、
  窗口/控件操作、GTK3 Gui、Unicode keysym 发送等。核心构建由
  `../CMakeLists.txt` / `build-core` 生成 `ahk_core`。
- **Wayland 后端**（`core_wayland_linux.cpp` + `core_clipboard_linux.cpp`）：
  原生 Wayland 会话下通过 zwp_virtual_keyboard_v1 / wlr-screencopy 支持
  输入模拟与截屏；全局热键经统一 input backend 路由（XDG Global
  Shortcuts Portal 或 GNOME Shell 扩展，见 `core/input_backend*.cpp`）。
- `compat/`：Linux 下的标准库/ABI 兼容层。

设计原则：

1. 不破坏现有 Windows 构建：新文件默认不进入 `AutoHotkeyx.vcxproj`。
2. 用接口隔离 Win32 API，逐步把 `hook.cpp`、`keyboard_mouse.cpp`、
   `window.cpp`、`clipboard.cpp` 等模块切换到平台接口。
3. 后端优先实现 X11；Wayland 的全局热键和输入模拟受协议限制，走
   portal/扩展/虚拟键盘通道；evdev/uinput 完整钩子语义在 roadmap。