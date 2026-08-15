# Linux 移植目录

本目录用于存放 AutoHotkey Linux 化过程中新增的平台抽象与 Linux 后端代码。

当前内容：

- `PlatformAbstraction.h`：初步定义的平台抽象接口，后续 Win32 模块将围绕该接口进行隔离。
- X11 后端实现尚未开始，`CreateX11Platform()` 暂未提供实现。

设计原则：

1. 不破坏现有 Windows 构建：新文件默认不进入 `AutoHotkeyx.vcxproj`。
2. 用接口隔离 Win32 API，逐步把 `hook.cpp`、`keyboard_mouse.cpp`、`window.cpp`、
   `clipboard.cpp` 等模块切换到平台接口。
3. 后端优先实现 X11；Wayland 的全局热键和输入模拟受协议限制，后续单独评估。
