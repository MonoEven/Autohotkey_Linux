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
- [x] 核心模块（script/var/util/TextIO/error 等）通过 Linux 编译
- [x] 提供 Linux 命令行入口 `main()`，替代 `_tWinMain`（临时 stub）
- [x] 能解析并执行不依赖 GUI/热键的脚本（MsgBox/变量赋值/算术表达式已可运行）
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
- [x] 最小解释器支持简单整数算术表达式
- [x] script_object.cpp 已能在 Linux 下编译（链接仍需更多核心模块）
- [x] script_object.cpp 已能在 Linux 下编译（链接仍需更多核心模块）
- [x] var.cpp 已能在 Linux 下编译（链接仍需更多核心模块）
- [x] util.cpp 已能在 Linux 下编译并链接进 ahk_core
- [x] error.cpp 已能在 Linux 下编译（链接仍需更多核心模块）
- [x] script.cpp 已能在 Linux 下编译（链接仍需更多核心模块）
- [x] script2.cpp 已能在 Linux 下编译（链接仍需更多核心模块）
- [x] hotkey.cpp 已能在 Linux 下编译（链接仍需更多核心模块）
- [x] window.cpp 已能在 Linux 下编译（链接仍需更多核心模块）
- [x] script_expression.cpp 已能在 Linux 下编译（修复 GCC goto 跨初始化问题）
- [x] os_version.cpp 已能在 Linux 下编译（新增 GetVersionExW 兼容实现）
- [x] 新增 ahk_core_objects 静态库，集中编译 script/script2/script_expression/script_object/error/hotkey/os_version/var/window 核心源文件
- [x] ahk_core 已链接全部核心解释器对象（script/script2/script_expression/script_object/error/hotkey/os_version/var/window）及 Linux 平台桩
- [x] 新增 BIF_*/BIV_* 桩，使核心解释器可链接
- [x] 修复 glibc 宽字符格式化（%s→%ls）与 GetFileAttributes/SetDllDirectory/MessageBox 等 Linux 兼容
- [x] 修复 LoadIncludedFile 中 continue 跳过 buffer swap 导致的死循环，真实 LoadFromFile 已能读取 .ahk
- [x] 定义 _WIN64 修正 64 位 Linux 结构体布局，真实 AutoExecSection 已可运行简单 MsgBox/赋值脚本
- [x] 链接 script_object_bif.cpp，接入真实 Object::HasBase/Op_Array/Op_Object 等对象内建函数
- [x] 新增 BIF_MsgBox 控制台实现，MsgBox "Hi" 可输出到终端
- [x] 接入 lib/math.cpp，启用真实 Abs/Sqrt/Round/Mod/Random 等数学内建函数
- [ ] 复杂 GUI/热键/其余内置函数仍需继续移植
- [ ] 逐文件修复编译错误
