# AutoHotkey Linux 化迁移目标

> 目标随进展持续更新。基线：AutoHotkey v2.0.26，分支：`linux-port`。
> **状态：全部核心目标已完成并发布（v2.0.26-linux.1）。**

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
- [ ] 建立基础 CI 编译检查（未做：本地双构建 + 全量 doc-check 代替；可作为后续增强）

### M3：平台抽象与 X11 后端 ✅
- [x] 平台层：未采用 `PlatformAbstraction.h` 抽象类，而是按模块直接实现 Linux 后端
      （`source/linux/core/core_*_linux.cpp`），效果等同且更贴合移植版结构
- [x] 实现 X11 后端（Xlib + XTest + XRandR/Xinerama + XGrabKey 热键）
- [x] 热键、Send、窗口枚举/操作、剪贴板、像素/显示器、对话框、ToolTip、
      ImageSearch、WinSetRegion 全部可用（doc-check **795/795** 断言通过）

### M4：GUI 与系统集成 ✅（按 Linux 实际情况落地）
- [x] 消息框/输入框/文件选择对话框：真实 X11 对话框（`source/linux/gui/x11_gui.cpp`），
      无 DISPLAY 时回退控制台/stdin
- [x] 注册表/COM：**不移植**（Windows 专属），相关函数按文档抛出明确错误
- [x] 复杂 GUI 控件（Gui/GuiControl/Menu 对象）：**不移植**（Windows 专属），文档标注
- [x] 原生 Wayland 后端：xdg-shell 窗口、虚拟键盘/指针、wlr-screencopy 抓屏
      （无 X11 时亦可运行；XWayland 回退 229 断言 + 纯 Wayland 13 断言通过）

### M5：完善与发布 ✅
- [x] 移植测试用例：26 项回归 + 795 项 doc-check（Xvfb）+ Wayland/XWayland 套件
- [x] Wayland 兼容性：原生 Wayland 后端 + XWayland 回退（sway headless 验证）
- [x] 打包：`tools/linux/pack.sh` 生成 **tar.gz + .deb**；CLI 安装器
      `install.sh` 与 GUI 安装器 `install-gui.sh`（zenity/yad）
- [x] 发布：**Release v2.0.26-linux.1**（含两个安装包资产）
- [x] 文档：官方 v2 文档镜像重建为 Linux 移植版（删除 v1 迁移内容，
      新增 linux-port.htm），GitHub Pages 发布：
      https://monoeven.github.io/Autohotkey_Linux/

## 当前推进重点

全部里程碑目标已完成。仓库状态：

- **语言**：仅 AutoHotkey v2（v1 语法不支持，v1 迁移文档已从站点移除）
- **后端**：X11（含 XWayland）优先；无 X11 时原生 Wayland
- **函数**：327/327 已实现（0 未实现，见 `tests/doccheck/worklist.tsv`）
- **文档**：`tests/doccheck/CHECK_REPORT.md` 为完整逐模块校验报告

## 后续候选增强（非阻塞）

- [ ] GitHub Actions CI（双构建 + 套件跑批）
- [ ] 更多发行版包（AppImage/rpm/Arch PKGBUILD）
- [ ] 输入法（ibus/fcitx）集成评估
- [ ] 剪贴板接入系统（当前为进程内实现）
