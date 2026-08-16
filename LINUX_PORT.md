# AutoHotkey Linux 化迁移计划

当前基线：AutoHotkey v2.0.26（Git tag `v2.0.26`）
迁移分支：`linux-port`

## 目标

将 AutoHotkey 从 Windows-only 迁移到 Linux（优先 X11，后续兼容 Wayland），
使脚本、热键、键鼠模拟、窗口管理和 GUI 能在 Linux 桌面环境运行。

## 现状分析

- 上游代码使用 Visual Studio 工程（`AutoHotkeyx.sln` / `AutoHotkeyx.vcxproj`）。
- 系统依赖集中在 `source/stdafx.h`：`windows.h`、`commctrl.h`、`shellapi.h`、`shlobj.h`、`mmsystem.h`、`commdlg.h`。
- 核心解释器（script、script2、script_expression、var、util、TextIO 等）相对独立，可优先移植。
- 平台相关模块主要包括：
  - `application.cpp`：Win32 消息循环、`GetMessage`/`PeekMessage`、计时器。
  - `hook.cpp` / `keyboard_mouse.cpp`：低级键盘鼠标钩子、`SendInput`、`GetAsyncKeyState`。
  - `hotkey.cpp`：`RegisterHotKey` 等全局热键。
  - `window.cpp` / `script_gui.cpp` / `script_menu.cpp`：窗口枚举、控件、菜单、消息框。
  - `clipboard.cpp`：剪贴板。
  - `script_registry.cpp`：注册表。
  - `script_com.cpp`：COM/DCom。
  - `os_version.cpp`：系统版本检测。

## 迁移阶段

### 阶段 0：环境与构建系统
- [x] 新建 `Autohotkey_Linux` 目录并克隆 v2.0.26 源码。
- [x] 创建 `linux-port` 分支。
- [x] 编写 CMake 构建脚本，支持 Linux 下的 GCC/Clang。
- [x] 建立 CI（GitHub Actions）编译检查。

### 阶段 1：核心解释器可编译
- [x] 用 `#ifdef` / 平台抽象层隔离 Win32 API。
- [x] 提供 `stdafx.h` 的 Linux 替代头，移除 MSVC 专用特性。
- [x] 先编译不依赖 GUI/钩子的核心模块（script、var、util、TextIO、error 等）。
- [x] 建立 Linux 下可运行的命令行入口（`AutoHotkey` 解释器，无 GUI）。

### 阶段 2：平台抽象层
- [x] 定义 `IPlatformWindow`、`IPlatformHook`、`IPlatformClipboard` 等接口。
- [x] 实现 X11 后端：
  - 窗口枚举/操作：Xlib / XCB + EWMH。
  - 全局热键：XGrabKey / XRecord。
  - 键鼠模拟：XTest。
  - 剪贴板：X11 selections。
  - 消息循环：XNextEvent / poll。
- [x] 保留 Win32 后端，方便对比回归。

### 阶段 3：功能模块移植
- [x] 热键与键鼠钩子。
- [x] 窗口管理与控件（X11 后端;Gui 复杂控件不移植,文档标注）。
- [x] 剪贴板。
- [x] 注册表替代（文件虚拟注册表,见 assert_registry;Windows 注册表不移植）。
- [x] COM 替代（**D-Bus** 实现,`core_com_dbus_linux.cpp`）。
- [x] DllCall(.so 动态库):dlopen/dlsym + libffi,`core_dllcall_linux.cpp`。

### 阶段 4：完善与测试
- [x] 移植 `AutoHotkey` 单元/集成测试。
- [x] Wayland 兼容性评估（全局热键/模拟输入受 Wayland 协议限制）。
- [x] 打包（AppImage / deb / rpm）。

## 风险与注意事项

- Wayland 默认不允许全局键鼠捕获和模拟，需依赖 compositor 扩展或 XWayland。
- GUI 控件数量大，完全等价移植成本高；建议先用消息框和基础窗口验证。
- 内部字符串目前面向 UTF-16/Windows，Linux 需建立 UTF-8 转换层。
- 上游 `alpha` 分支比 v2.0.26 更新，后续可评估是否 rebase 到 alpha。

## 如何开始

```bash
git clone --branch v2.0.26 https://github.com/AutoHotkey/AutoHotkey.git Autohotkey_Linux
cd Autohotkey_Linux
git switch -c linux-port
```

后续每个阶段在 `linux-port` 分支上以小步提交推进。
