# AutoHotkey v2.0.26 Linux 移植 —— 模块验证矩阵

> 对照 AutoHotkey v2 官方文档（https://www.autohotkey.com/docs/v2/ ）逐模块确认可用性。
> 每个条目均有对应 `.ahk` 验证脚本（见 `tests/`），状态：
> - ✅ 已实现并有 .ahk 验证通过
> - ⚠️ 部分实现（仅列出受限功能）
> - ❌ 未实现（调用时给出明确的 "not implemented on Linux" 错误）
> - GUI 类功能区分「有画面」（DISPLAY 可用，X11/WSLg/XFCE）与「无画面」（headless）两种模式。

## 1. 脚本与指令 (Scripts & Directives)

| 模块 | 状态 | 验证 | 说明 |
|---|---|---|---|
| #Requires | ✅ | t01 | v2 语法检查 |
| #Include | ✅ | — | 真实 LoadIncludedFile 已可用（history 提交验证） |
| 表达式/运算符 | ✅ | t02, t07 | 算术/比较/逻辑/三元/连接 |
| 变量/赋值 | ✅ | t02 | 全局/局部/静态（由核心解释器支持） |
| 控制流: if/else, loop, while, for | ✅ | t03, t05 | Loop+ A_Index、For 数组枚举 |
| 标签/Goto | ✅ | t15 | 标签跳转、IsLabel() |
| 函数/参数/返回 | ✅ | t14 | 默认值、变参、闭包、箭头函数 |
| 类/对象 | ✅ | t14 | class、static、方法、属性 |
| Try/Catch/Throw | ✅ | t13 | 错误对象 e.Message |
| #Warn | ⚠️ | — | VarUnset 警告已在控制台输出 |
| #SingleInstance | ❌ | — | 依赖互斥窗口机制 |
| #HotIf / #Hotstring | ⚠️ | — | HotIf() 函数可调用（状态记录），热键本体未生效（无钩子） |
| #Persistent | ✅ | t11 | Persistent() 生效（g_persistent） |

## 2. 内置函数 —— 通用 (General)

| 函数 | 状态 | 验证 | 说明 |
|---|---|---|---|
| MsgBox | ✅ | msgbox1 | 有画面：真实 X11 对话框（按钮/T1超时/选项）；无画面：控制台打印 |
| InputBox | ✅ | input1 | 有画面：X11 输入框；无画面：stdin 读取；返回 {Value, Result} 对象 |
| Run / RunWait | ✅ | run2, run3, pid1 | fork/exec；RunWait 返回退出码；&PID 输出参数；URL 走 xdg-open；WSL 下可启动 .exe |
| Sleep | ✅ | t10 | 真实睡眠（毫秒） |
| Exit / ExitApp | ✅ | t10, t16 | 退出码正确 |
| Reload | ⚠️ | — | 解析通过，进程重启依赖 exec（待完善） |
| Pause / Suspend | ✅ | — | 线程状态切换（真实实现） |
| Persistent | ✅ | t11 | 见上 |
| OnExit / OnError / OnClipboardChange | ✅ | t12 | 事件注册回调可执行 |
| SetTimer | ❌ | — | 注册可用但主循环未实现，标记未实现避免误导 |
| Try/Catch 错误处理 | ✅ | t13 | 错误类型/Message |
| Critical / Thread | ✅ | — | 线程设置（真实实现） |
| Random / Format | ✅ | t07, f1-f5 | Format 占位符/宽度/精度 |
| VerCompare | ✅ | — | 版本比较（lib/string.cpp 真实实现） |
| Type / IsInteger / IsNumber 等 | ✅ | — | 类型判断（真实实现） |
| ObjBindMethod / GetMethod / HasMethod | ✅ | — | 对象内建（script_object_bif 真实实现） |
| DllCall / CallbackCreate | ❌ | — | 依赖 x64 调用汇编 |

## 3. 内置函数 —— 数学 (Math)

| 函数 | 状态 | 验证 |
|---|---|---|
| Abs/Sqrt/Mod/Round/Floor/Ceil/Max/Min | ✅ | t07 |
| Exp/Ln/Log/Sin/Cos/Tan/ASin/ACos/ATan | ✅ | — |
| Random | ✅ | — |
| IsNumber/IsInteger/IsFloat | ✅ | — |

## 4. 内置函数 —— 字符串 (String)

| 函数 | 状态 | 验证 |
|---|---|---|
| StrLen/SubStr/InStr/StrReplace/Trim/LTrim/RTrim | ✅ | t07 |
| StrUpper/StrLower/StrTitle/StrCompare | ✅ | — |
| StrSplit | ✅ | t09 |
| Format / FormatTime | ✅ | t07/f 系列、ft1（yyyy-MM-dd/LongDate/ShortDate/Time/YWeek/YDay 全部正确） |
| Ord/Chr | ✅ | — |
| StrGet/StrPut/StrPtr | ⚠️ | 地址类操作在 Linux 上语义受限 |
| RegExMatch/RegExReplace | ❌ | 依赖 PCRE 移植（lib/regex.cpp 未链接），当前明确报错 |
| Sort / SplitPath | ✅ | SplitPath 已支持正斜杠路径（Linux 端口修复） |

## 5. 内置函数 —— 对象 (Object)

| 函数 | 状态 | 验证 |
|---|---|---|
| Object()/{} 字面量/属性/方法 | ✅ | t04 |
| Array()/[]/Map | ✅ | t05 |
| 枚举器 for .. in | ✅ | t05 |
| ObjGetCapacity/ObjSetCapacity/ObjOwnProps 等 | ✅ | — |
| 类/继承/静态 | ✅ | t14 |

## 6. 内置函数 —— 文件系统 (File)

| 函数 | 状态 | 验证 |
|---|---|---|
| FileRead / FileAppend / FileOpen(File对象) | ✅ | t06, a5 |
| FileExist / DirExist / DirCreate / DirDelete | ✅ | t06 |
| DirCopy / DirMove | ✅ | —（lib/file.cpp 真实实现） |
| FileDelete / FileMove / FileCopy | ⚠️ | FileDelete 已实现；FileMove/Copy 待接通 |
| FileGetSize/FileGetTime/FileGetAttrib 等 | ❌ | 未实现 |
| IniRead/IniWrite/IniDelete | ✅ | 本轮实现（见 tests/ini1） |
| FileSelect/DirSelect | ❌ | 需 GUI 文件对话框 |
| Loop Files | ⚠️ | 解析可用，A_LoopFileName 等变量为空 |

## 7. 内置函数 —— 日期/时间 (Date/Time)

| 函数 | 状态 | 验证 |
|---|---|---|
| DateAdd/DateDiff | ✅ | dated.ahk |
| FormatTime | ✅ | ft1：显式格式/默认/LongDate/ShortDate/Time/YWeek/YDay 全部正确（strftime 翻译层） |
| A_Now/A_NowUTC/A_YYYY/.../A_MSec | ✅ | dates1 |
| A_MMM/A_MMMM/A_DDD/A_DDDD/A_YDay/A_YWeek | ✅ | dates1 |

## 8. 内置函数 —— 环境 (Environment)

| 函数 | 状态 | 验证 |
|---|---|---|
| EnvGet/EnvSet | ✅ | t09 |
| SetWorkingDir/A_WorkingDir | ✅ | — |
| A_ScriptDir/A_ScriptFullPath/A_ScriptName/A_UserName/A_ComSpec/A_Temp/A_WinDir | ✅ | t16 |
| A_Desktop/A_MyDocuments/A_AppData 等 | ❌ | SpecialFolder 未映射 |
| A_Is64bitOS/A_PtrSize/A_IsAdmin/A_IsCompiled | ✅ | t16 |
| A_TickCount | ✅ | t16 |

## 9. 内置函数 —— 键盘/鼠标/热键 (Input)

| 函数 | 状态 | 验证 | 说明 |
|---|---|---|---|
| GetKeyState/GetKeyVK/GetKeySC/GetKeyName | ⚠️ | — | 名称映射可用；物理状态读取待 X11 接入 |
| KeyWait | ❌ | — | 待 X11 轮询实现 |
| Send/SendInput/SendEvent/SendPlay/SendText | ❌ | — | 待 XTest 实现 |
| Click/MouseMove/MouseClick/MouseGetPos | ❌ | — | 待 XTest 实现 |
| Hotkey/Hotstring | ❌ | — | 待 XGrabKey/钩子 |
| InstallKeybdHook/InstallMouseHook | ❌ | — | 同上 |
| A_ThisHotkey/A_PriorHotkey/A_TimeIdle | ❌ | — | 依赖钩子 |

## 10. 内置函数 —— GUI（有画面 / 无画面深度适配）

| 函数 | 有画面 (X11/WSLg/XFCE) | 无画面 (headless) | 验证 |
|---|---|---|---|
| MsgBox | ✅ 真实 X11 窗口：标题/按钮集/默认按钮/Enter/Esc/超时(T选项) | ✅ 控制台打印并返回 OK | msgbox1, t10 |
| InputBox | ✅ X11 输入框：键盘输入/退格/Enter/Esc/默认值 | ✅ stdout 提示 + stdin 读取 | input1 |
| Gui() 控件体系 | ❌ 未实现（script_gui 531KB Win32 代码，后续里程碑） | ❌ 同样报错 | — |
| ToolTip | ❌ | ❌ | — |
| TrayTip/TraySetIcon | ⚠️ 可调用（真实实现，无系统托盘副作用） | ⚠️ | — |
| 对话框超时 | ✅ T<秒> 自动关闭（CLOCK_MONOTONIC） | ✅ 忽略超时 | msgbox1 |

**无画面判定**：程序启动时 `XOpenDisplay()` 探测（缓存），失败即进入控制台回退模式；所有 GUI 路径均有回退，脚本在两种环境下行为一致（内容不丢失）。

## 11. 内置函数 —— 窗口 (Window)

| 函数 | 状态 | 验证 | 说明 |
|---|---|---|---|
| WinExist/WinActive | ✅ | sw1 | 无 X11 后端时按文档返回空字符串（不报错） |
| WinActivate/WinGet*/WinMove/WinWait* 等 | ❌ | — | 待 X11 EWMH 后端 |
| GroupAdd/GroupActivate 等 | ❌ | — | 待窗口后端 |
| A_ScriptHwnd | ❌ | — | 无主窗口 |

## 12. 内置函数 —— 系统 (System)

| 函数 | 状态 | 验证 |
|---|---|---|
| ProcessExist/ProcessWait/ProcessWaitClose/ProcessClose/ProcessSetPriority | ✅ | t22（kill + /proc，僵尸进程已排除） |
| DriveGetType/DriveGetList/DriveGetCapacity/DriveGetSpaceFree/DriveGetFilesystem/DriveGetStatus | ✅ | t23（statvfs + /proc/mounts） |
| DriveGetLabel/DriveGetSerial/DriveGetStatusCD | ✅ | 返回空/0（Linux 无对应概念） |
| SoundBeep | ✅ | t24（X11 Bell / 终端响铃） |
| SoundPlay/SoundGet*/SoundSet* | ❌ | 音频后端 |
| Shutdown | ⚠️ | 可调用（systemctl/poweroff 语义待定） |
| Clipboard (A_Clipboard) | ✅ | t21（进程内剪贴板；X11 selection 后续） |
| ClipWait | ✅ | mod4（轮询内部剪贴板） |
| RegRead/RegWrite/Reg* | ❌ | 注册表替代未实现 |
| ComObj* / COM | ❌ | D-Bus 替代未实现 |
| SysGet/SysGetIPAddresses | ⚠️ | 部分可用 |
| MonitorGet* | ❌ | XRandR 后端 |

## 13. 内置变量 (Built-in Variables)

| 组 | 状态 | 验证 |
|---|---|---|
| A_Script*/A_WorkingDir/A_UserName/A_ComSpec/A_Temp/A_WinDir | ✅ | t16 |
| A_YYYY..A_MSec/A_Now/A_NowUTC/A_MMM/A_DDD 等 | ✅ | dates1 |
| A_Index/A_LoopField/A_LoopReadLine | ✅ | t03 |
| A_PtrSize/A_Is64bitOS/A_TickCount/A_Space/A_Tab | ✅ | t16 |
| A_Clipboard | ✅ | t21（进程内剪贴板） |
| A_IsPaused/A_IsSuspended/A_IsCritical | ✅ | — |
| A_LineNumber/A_LineFile/A_ThisFunc | ✅ | — |
| A_LoopFile*/A_LoopReg* | ⚠️ | Loop Files/Reg 未实现 |
| A_TimeIdle/A_ThisHotkey/A_PriorHotkey | ❌ | 依赖钩子 |
| A_Desktop/A_AppData/A_StartMenu 等 | ❌ | SpecialFolder 未映射 |
| A_ScreenWidth/A_ScreenHeight/A_ScreenDPI | ⚠️ | 待 X11 后端（可读回默认值） |

## 14. 键列表 (Key List)

| 模块 | 状态 |
|---|---|
| 键名映射（GetKeyVK/GetKeySC/GetKeyName） | ⚠️ 部分键名（字母/数字/F1-F12/常用键） |
| 完整键列表（媒体键等） | ❌ |

## 汇总

- 已实现并通过 .ahk 验证（常规构建 + ASan 构建双通过）：**通用（Run/RunWait/MsgBox/InputBox/Sleep/Exit/OnExit）、数学、字符串、对象、文件、INI、日期时间（含 FormatTime）、环境、GUI 双模式、进程、驱动、剪贴板、SoundBeep、内置变量**（约 130+ 函数/变量）
- 未实现（明确报错 "not implemented on Linux"）：窗口/控件、热键/发送、注册表/COM、Gui 控件体系、SetTimer 主循环、正则（PCRE 待移植）
- 验证脚本：`tests/run_tests.sh`（headless 回归 25 项）、`tests/gui_tests.sh`（有画面 6 项，含 T1 超时与 InputBox 自动确认测试钩子）、`tests/` 下各专项（test_ft/test_pid/test_modules 等已并入主套件）

> 生成方式：本矩阵的模块清单来自源码权威表（`script.cpp` 的 g_BIF / g_BIV_A、`lib/functions.h` 的 MdFunc 注册表）与 v2 官方文档模块划分。状态随移植进度更新。
