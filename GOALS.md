# AutoHotkey Linux 化迁移目标

> 目标会随进展持续更新。当前基线：AutoHotkey v2.0.26，分支：`linux-port`。

## 总目标

让 AutoHotkey 在 Linux 桌面上可运行，优先支持 X11，再评估 Wayland。

## 里程碑目标

### M1：构建与基础环境
- [x] 创建 `Autohotkey_Linux` 目录并克隆 v2.0.26
- [x] 创建 `linux-port` 分支
- [x] 在 WSL Ubuntu 24.04 安装 CMake/Ninja/X11 开发库
- [x] CMake 脚手架可配置、可构建
- [x] 提供 Linux 下可用的 `stdafx.h` / Win32 兼容层（初版）

### M2：核心解释器可在 Linux 编译
- [ ] 核心模块（script/var/util/TextIO/error 等）通过 Linux 编译
- [x] 提供 Linux 命令行入口 `main()`，替代 `_tWinMain`（临时 stub）
- [ ] 能解析并执行不依赖 GUI/热键的脚本
- [ ] 建立基础 CI 编译检查

### M3：平台抽象与 X11 后端
- [ ] 用 `PlatformAbstraction.h` 隔离窗口、热键、剪贴板、键鼠模拟
- [ ] 实现 X11 后端（Xlib/XCB + XTest + XRecord + X11 selections）
- [ ] 热键、Send、窗口枚举/操作、剪贴板可用

### M4：GUI 与系统集成
- [ ] 消息框、菜单、基础 GUI 控件可用（GTK 或 Qt 承载）
- [ ] 注册表替代方案（GSettings/XDG）
- [ ] COM 相关功能评估 D-Bus 替代

### M5：完善与发布
- [ ] 移植测试用例
- [ ] Wayland 兼容性评估
- [ ] 打包（AppImage/deb/rpm）

## 当前推进重点

M2：核心解释器 Linux 编译。

## 进行中任务

- [x] 创建 `source/linux/stdafx_linux.h` Win32 兼容头（初版）
- [x] 调整 `stdafx.h` 在 Linux 下加载兼容层
- [x] 添加可选 CMake target `ahk_core`，当前可编译 `ahkversion.cpp` + `SimpleHeap.cpp` + `StringConv.cpp` + Linux `main` stub
- [x] 通过 `source/linux/core_errors.h` 解耦 `SimpleHeap.cpp` 对 `globaldata.h` 的依赖
- [x] `StringConv.cpp` 已进入 Linux 核心构建
- [x] 扩充 Win32 兼容层（UINT8/VK/HRESULT/GetForegroundWindow 等）
- [x] script.h 已通过 Linux 语法编译
- [x] TextIO.cpp 已可编译（尚未链接完整对象模型）
- [x] globaldata.cpp 已编译并链接进 hk_core（含临时对象模型桩）
- [x] TextIO.cpp 已链接进 hk_core（对象模型暂为桩实现）
- [x] Linux 兼容层已实现 POSIX 文件读写，hk_core 可读取 .ahk 文件内容
- [x] hk_core 已能最小化执行简单 .ahk（MsgBox 示例输出成功）
- [x] 最小解释器支持变量赋值与 MsgBox 变量引用
- [x] script_object.cpp 已能在 Linux 下编译（链接仍需更多核心模块）
- [x] ar.cpp 已能在 Linux 下编译（链接仍需更多核心模块）
- [ ] 继续为其他核心模块（util/var/script/TextIO 等）解耦平台依赖
- [ ] 逐文件修复编译错误