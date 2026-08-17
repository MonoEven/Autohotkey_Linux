# AutoHotkey Linux 化迁移目标

> 目标随进展持续更新。基线：AutoHotkey v2.0.26，分支：`linux-port`。
> **状态：全部核心目标已完成并发布（v2.0.26-linux.1，含 GTK3 GUI 的 v2.0.26-linux.2 已打包）。**

## 总目标

让 AutoHotkey v2 在 Linux 桌面上可运行：优先 X11（含 XWayland），并提供原生 Wayland 后端。

✅ **已完成**：X11 后端全功能 + 原生 Wayland 后端 + XWayland 回退，已打包发布。

## 里程碑目标

### M1：构建与基础环境 ✅
- [x] 创建 `Autohotkey_Linux` 目录并克隆 v2.0.26
- [x] 创建 `linux-port` 分支
- [x] 在 WSL Ubuntu 24.04 安装 CMake/Ninja/X11 开发库
- [x] CMake 脚手架可配置、可构建
- [x] 提供 Linux 下可用的 `stdafx.h` / Win32 兼容层（初版）

### M2：核心解释器可在 Linux 编译 ✅
- [x] 核心模块（script/var/util/TextIO/error 等）通过 Linux 编译
- [x] 提供 Linux 命令行入口 `main()`，替代 `_tWinMain`（含 `--version`/`--help`）
- [x] 能解析并执行不依赖 GUI/热键的脚本（MsgBox/变量赋值/算术表达式）
- [x] 建立回归测试 `tests/run_tests.sh`（26 项，常规与 ASan 构建全部通过）
- [x] 建立 CI（`.github/workflows/ci.yml`，常规 + ASan 双构建 + 全量 doc-check + Wayland，见 M5）

### M3：平台抽象与 X11 后端 ✅
- [x] 平台层：未采用 `PlatformAbstraction.h` 抽象类，而是按模块直接实现 Linux 后端
      （`source/linux/core/core_*_linux.cpp`），效果等同且更贴合移植版结构
- [x] 实现 X11 后端（Xlib + XTest + XRandR/Xinerama + XGrabKey 热键）
- [x] 热键、Send、窗口枚举/操作、剪贴板、像素/显示器、对话框、ToolTip、
      ImageSearch、WinSetRegion 全部可用（doc-check **868/868** 断言通过，
      含 DllCall 29 项、D-Bus COM 18 项与 GTK3 GUI/Menu 26 项）

### M4：GUI 与系统集成 ✅（按 Linux 实际情况落地）
- [x] 消息框/输入框/文件选择对话框：真实 X11 对话框（`source/linux/gui/x11_gui.cpp`），
      无 DISPLAY 时回退控制台/stdin；`AHK_MSGBOX_AUTOCLOSE_MS` 测试钩子供自动化套件
- [x] 注册表：**不移植**（Windows 专属），相关函数按文档抛出明确错误
- [x] **COM：改用 D-Bus 实现**（`core_com_dbus_linux.cpp`）：`ComObject("service")`
      创建总线服务代理，方法调用/属性访问映射 D-Bus，`ComValue` 包装类型化值；
      `ComObjGet`/`ComObjType`/`ComObjValue`/`ComObjFlags` 可用，
      `ComObjQuery`/`ComObjConnect`/`ComObjArray` 按文档报错（18 项断言）
- [x] **DllCall：实现 .so 动态库调用**（`core_dllcall_linux.cpp`）：
      dlopen/dlsym + libffi，全类型支持、`&Var` 输出参数、HRESULT 报错（29 项断言）
- [x] **复杂 GUI：GTK3 后端**（`source/linux/gui/script_gui_linux.cpp`）：
      `Gui`/`GuiControl` 的窗口、布局、属性、事件与 Text/Edit/Button/CheckBox/
      Radio/DDL/Combo/ListBox/ListView/TreeView/Slider/Progress/UpDown/Tab/
      MonthCal/StatusBar/GroupBox/Link/Pic 等控件；`Submit`/`OnEvent`/`Move`/
      `GetPos`/`Opt` 可用。`Menu`/`MenuBar` 由 `script_menu_linux.cpp` 实现；
      metadata 成员调用以 libffi 替代 Windows DynaCall（26 项 Xvfb 断言）
- [x] 原生 Wayland 后端：xdg-shell 窗口、虚拟键盘/指针、wlr-screencopy 抓屏
      （无 X11 时亦可运行；XWayland 回退 229 断言 + 纯 Wayland 13 断言通过）

### M5：完善与发布 ✅
- [x] 移植测试用例：26 项回归 + 903 项 doc-check（Xvfb，含 GUI/未移植错误行为断言）
      + Wayland/XWayland 套件
- [x] Wayland 兼容性：原生 Wayland 后端 + XWayland 回退（sway headless 验证）
- [x] 打包：`tools/linux/pack.sh` 生成 **tar.gz + .deb**；CLI 安装器
      `install.sh` 与 GUI 安装器 `install-gui.sh`（zenity/yad）
- [x] 发布：**Release v2.0.26-linux.1**（含两个安装包资产）；GTK3 GUI 落地后
      打包 **v2.0.26-linux.2**（tar.gz + .deb，依赖补 libgtk-3-0/libdbus-1-3/libffi8）；
      本轮（Sound*/CaretGetPos/CallbackCreate+Free/InputHook + 语句/指令/类别/索引页
      代码形式校验，903 项 doc-check 双构建绿）发布 **v2.0.26-linux.3**
- [x] 文档：官方 v2 文档镜像重建为 Linux 移植版（删除 v1 迁移内容，
      新增 linux-port.htm；DllCall/COM 页面含 Linux 可运行示例；
      25 个平台专属页面标注 Linux note），GitHub Pages 发布：
      https://monoeven.github.io/Autohotkey_Linux/

## 当前推进重点

全部里程碑目标已完成。仓库状态：

- **语言**：仅 AutoHotkey v2（v1 语法不支持，v1 迁移文档已从站点移除）
- **后端**：X11（含 XWayland）优先；无 X11 时原生 Wayland
- **函数**：369 个函数已实现；本轮补齐 Sound*/CaretGetPos/InputHook/
      CallbackCreate+Free。其余 4 个（ComObjArray/ComObjConnect/ComObjQuery
      等 D-Bus/COM 边界操作）抛明确错误（见 `tests/doccheck/worklist.tsv`）；
      其中 **DllCall** 调用 .so 动态库（dlopen/dlsym+libffi，29 项断言）、
      **COM** 映射到 D-Bus（ComObject/ComValue/ComObj*，18 项断言）
- **文档**：`tests/doccheck/CHECK_REPORT.md` 为完整逐模块校验报告；
      示例按三类处理（原样运行/平台适配/不可用标注），
      DllCall/ComObject 页面含已实测的 Linux 可运行示例，
      示例审计工具在 `tests/doccheck/verify_examples*.py`

## 后续增强（已完成）

- [x] **GitHub Actions CI**（`.github/workflows/ci.yml`）：常规 + ASan 双构建，
      跑 `tests/run_tests.sh`、`run_check.sh --xvfb`、`wayland_run.sh`
      （纯 Wayland + XWayland），package job 构建并检查 tar.gz/.deb/AppImage/rpm
- [x] **更多发行版包**：`tools/linux/pack-appimage.sh`（AppImage）、
      `pack-rpm.sh`（rpm）、`PKGBUILD`（Arch）
- [x] **系统剪贴板**：`core_clipboard_linux.cpp` —— X11 CLIPBOARD selection
      （读写、跨进程经 xclip 验证）与 Wayland wl_data_device（数据源/offer）；
      无显示时保留进程内回退
- [x] **DllCall（.so 动态库）**：`core_dllcall_linux.cpp` —— dlopen/dlsym +
      libffi，全类型/输出参数/HRESULT，29 项断言（含真实 libc/libm 调用）
- [x] **COM 改用 D-Bus**：`core_com_dbus_linux.cpp` —— ComObject/ComValue/
      ComObj* 映射到桌面总线，18 项断言（真实 org.freedesktop.DBus 调用）
- [x] **输入法集成评估**：`docs/IME-Integration.md`（XIM / Wayland text-input
      分阶段方案、热键与 preedit 交互、推荐路径）

## 后续候选增强（未实现）

- [ ] 输入法实际集成（Phase 1：XIM 事件过滤 + IME 状态变量）
- [ ] Wayland text-input（zwp_text_input_v3）Send 文本投递
