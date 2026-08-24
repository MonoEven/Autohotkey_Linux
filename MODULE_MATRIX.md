# AutoHotkey v2.0.26 Linux 移植 —— 模块验证矩阵

> 对照 AutoHotkey v2 官方文档逐模块确认可用性。完整逐模块校验报告见
> `tests/doccheck/CHECK_REPORT.md`（1160/1160 断言，普通 + ASan 双构建）。
> 状态图例：✅ 已实现并有 .ahk 验证；⚠️ 部分实现/依赖外部工具；❌ 未实现（明确报错）。
> GUI 类功能区分「有画面」(DISPLAY 可用，X11/WSLg/XFCE/XWayland) 与「无画面」(headless)。

## 1. 脚本与指令 (Scripts & Directives)

| 模块 | 状态 | 说明 |
|---|---|---|
| #Requires / #Include | ✅ | v2 语法检查;真实 LoadIncludedFile |
| 表达式/运算符/变量/赋值 | ✅ | 算术/比较/逻辑/三元/连接 |
| 控制流 if/else, loop, while, for | ✅ | Loop + A_Index、For 枚举 |
| 标签/Goto、函数/参数/返回 | ✅ | 默认值、变参、闭包、箭头函数 |
| 类/对象、Try/Catch/Throw | ✅ | class、static、方法、属性、错误对象 |
| #HotIf / HotIf | ✅ | X11/XWayland 下条件热键(见热键模块) |
| #Persistent / Persistent() | ✅ | 保持运行 |
| #SingleInstance | ✅ | 单实例锁 |

## 2. 内置函数 —— 通用 (General)

| 函数 | 状态 | 说明 |
|---|---|---|
| MsgBox / InputBox | ✅ | 有画面:真实 X11 对话框(按钮/超时/默认值);无画面:控制台/stdin |
| Run / RunWait | ✅ | fork/exec;URL 走 xdg-open;WSL 下可启动 .exe |
| Sleep / Exit / ExitApp | ✅ | 真实睡眠、退出码 |
| Reload / Pause / Suspend | ✅ | Reload 真重启(round-28,退出原因 Reload);线程状态切换 |
| OnExit / OnError / OnClipboardChange | ✅ | 事件注册回调可执行 |
| SetTimer | ✅ | 周期触发/Period 0 删除/默认 250ms |
| **DllCall** | ✅ | **.so 动态库**(dlopen/dlsym + libffi):全类型、&Var 输出、HRESULT 报错(29 断言) |
| CallbackCreate | ✅ | libffi closure；Float/Double 参数和返回按 SysV ABI 精确封送 |
| VerCompare / Type / Is* | ✅ | 版本比较、类型判断 |

## 3. 数学 / 字符串 / 对象 / 日期 (Math / String / Object / Date)

| 模块 | 状态 | 说明 |
|---|---|---|
| 数学 (Abs/Sqrt/Mod/Round/Floor/Ceil/Exp/Ln/Log/Sin/.../Random) | ✅ | 全绿 |
| 字符串 (StrLen/SubStr/InStr/StrReplace/Trim/Format/Sort/SplitPath/StrUpper/...) | ✅ | 全绿;SplitPath 支持正斜杠(Linux 适配) |
| **正则 (RegExMatch/RegExReplace/~=)** | ✅ | 捆绑 PCRE1(16 位),命名子组/回引用/UTF |
| 对象 (Object/Array/Map/类/属性/绑定方法/枚举) | ✅ | 全绿 |
| 二进制互操作 (NumGet/NumPut/StrGet/StrPut/Buffer) | ✅ | 全绿 |
| 日期时间 (DateAdd/Diff/FormatTime/A_Now/A_YYYY 等) | ✅ | strftime 翻译层,全部正确 |

## 4. 文件系统 / 环境 / 系统

| 模块 | 状态 | 说明 |
|---|---|---|
| 文件 (FileOpen/File对象/FileRead/Append/Delete/Copy/Move/Dir*) | ✅ | 全绿;FileGet/SetTime/Attrib、Loop Files 真实实现 |
| 快捷方式 (FileCreateShortcut/FileGetShortcut) | ✅ | .desktop / .url 生成与回读 |
| INI (IniRead/IniWrite/IniDelete) | ✅ | 全绿 |
| 注册表 (RegRead/RegWrite/RegDelete/...) | ✅ | **文件虚拟注册表**(XDG 配置),语义对照文档(19 断言) |
| 环境变量 / SetWorkingDir / A_* 变量 | ✅ | 15+ A_* 设置变量逐一对照默认值 |
| 进程 (Process*/RunAs) | ✅ | /proc 实现;RunAs 不适用(Linux 用 sudo/runuser) |
| 驱动 (Drive*/DriveEject/...) | ✅ | statvfs + /proc/mounts;设备操作按文档报错 |
| 下载 (Download) | ✅ | libcurl,本地 HTTP 服务实测 |
| 剪贴板 (A_Clipboard/ClipWait/ClipboardAll) | ✅ | X11 CLIPBOARD selection + Wayland wl_data_device;无显示时进程内回退 |
| **SoundBeep / SoundPlay** | ✅ | 终端/X11 响铃等实现(aplay/paplay) |
| SoundGet*/SoundSet* | ⚠️ | 依赖外部 `pactl`/`amixer`,否则 OSError |
| TrayTip / TraySetIcon / A_TrayMenu | ✅ P2 | 通知走 org.freedesktop.Notifications；托盘走 StatusNotifierItem + dbusmenu，默认继承官方 AutoHotkey 图标 |
| ComObjArray | ❌ | Windows SafeArray 不存在于 Linux，明确报错 |

## 5. 键盘 / 鼠标 / 热键 / 热字串 / InputHook (Input)

| 函数 | 状态 | 说明 |
|---|---|---|
| Send/SendEvent/SendInput/SendPlay/SendText | ✅ | X11:XTEST + xkbcommon-x11 当前布局 Shift/AltGr 反查；Wayland:虚拟键盘/指针；独立 XI2/keysym oracle 验证 |
| Click/Mouse*/KeyWait/GetKeyState/BlockInput | ✅ | X11 实测;锁键开关、阻断语义 |
| Hotkey / HotIf | ✅ | **per-hotkey mux**:按能力路由到 X11/portal/GNOME/evdev 并多 lane 同存；X11/evdev 支持 canonical set-1 `scXXX`；evdev 支持 `A & B` 状态机；**鼠标热键**(round-30,XGrabButton)、**左右修饰键与通配**、哈希索引、动态 modifier map、BadAccess 冲突报错、Off/透传 |
| Hotstring | ✅ | XI2.1 raw multi-client 字符流；原始 trigger 到达目标，按精确 Backspace 数回删再替换；C/*/O/X/B0、大小写、inside-word、HotIf、Unicode；双进程外部 oracle |
| InputHook | ✅ | XI2 raw 观察 + KeyOpt/visibility 精确 keycode suppression grabs；运行时重配；多脚本/目标可见；缓冲/EndKey/Match/退格、canonical VK/SC、Unicode/回调 |
| ahk-inputd broker | ✅ (M4-D) | 独立守护进程：EVIOCGRAB 捕获 + uinput 0x0FAC 回放 + UNIX socket v1 多客户端订阅/抑制仲裁 + 崩溃清理/watchdog/panic；root oracle 全绿（客户端消费接 M4-C） |

## 6. GUI(有画面 / 无画面)

| 函数 | 有画面 (X11/XWayland) | 无画面 (headless) | 说明 |
|---|---|---|---|
| MsgBox / InputBox / FileSelect / DirSelect | ✅ 真实 X11 对话框 | ✅ 控制台/stdin 回退 | 测试钩子 AHK_*_AUTOCLOSE_MS |
| ToolTip | ✅ X11 窗口 | ❌ 报错 | 同索引更新复用窗口(5 断言) |
| Gui()/GuiControl 对象 | ✅ **GTK3 窗口** | ❌ 报错 | Add*/Text/Edit/Button/CheckBox/Radio/DDL/Combo/ListBox/LV/TV/Slider/Progress/UpDown/Tab/MonthCal/StatusBar/GroupBox/Link/Pic;Value/Text/Submit/OnEvent/Move/GetPos/Opt 等(26 断言) |
| Menu / MenuBar | ✅ GTK3 菜单 | ❌ 报错 | Menu/MenuBar:Add/Insert/Delete/Check/Enable/Rename/SetIcon/Show 弹窗 + Gui.MenuBar |
| ImageSearch | ✅ X11 (XGetImage) | ❌ 报错 | 命中/未命中/容差/反向搜索 |
| WinSetRegion | ✅ X11 (XShape) | ❌ 报错 | 窗口形状(19 断言) |

## 7. 窗口管理 (Window)

| 函数 | 状态 | 说明 |
|---|---|---|
| WinExist/WinActive/WinGet*/WinSet*/WinMove/WinClose/WinKill/WinWait*/Group* | ✅ | X11/XWayland(67 断言,以 xwin_helper 实测) |
| 标题/类/exe/pid/id/RegEx 匹配、隐藏窗口联动、透明度/置顶 | ✅ | 全绿 |
| 纯 Wayland 下窗口枚举 | ❌ | Wayland 客户端无法枚举窗口;用 XWayland |

## 8. 显示器 / 像素 / 状态栏

| 模块 | 状态 | 说明 |
|---|---|---|
| Monitor*/PixelGetColor/PixelSearch | ✅ | XRandR/Xinerama + XGetImage(18 断言) |
| StatusBarGetText/StatusBarWait | ✅ | msctls_statusbar32 子窗口实测(7 断言) |
| SysGet/SysGetIPAddresses | ✅ | X11 后端;IPv4 数组含回环 |

## 9. COM(D-Bus)

| 函数 | 状态 | 说明 |
|---|---|---|
| ComObject / ComObjGet / ComObjActive | ✅ | **D-Bus 服务代理**(18 断言,真实 org.freedesktop.DBus 调用) |
| ComValue / ComObjType / ComObjValue / ComObjFlags | ✅ | 标量包装(I2/I4/R4/R8/BSTR/BOOL/UI1/I8/UI8) |
| ComCall | ✅ | 走 DllCall 实现 |
| ComObjQuery / ComObjConnect | ⚠️ | Windows COM 接口不存在,按文档报错 |
| ComObjArray | ❌ | Windows SafeArray,有意未实现,明确报错 |

## 汇总

- **1160/1160** X11/headless doc-check 断言通过（普通 + ASan 双构建）
- **27/27** headless 回归测试；Wayland **17/17** + XWayland **255/255** 独立套件
- 场景门禁覆盖 X11、纯 Wayland、GNOME 会话、evdev/uinput、打包和 SNI；CI 另跑四发行版容器、no-XWayland、pack 容器验收与 soak
- Unicode 文本发送（round-34 + round-36）:X11/XWayland 非 ASCII 经 keysym 传输（借键码重映射 + 跨进程 X Selection 租约），纯 Wayland 走剪贴板粘贴回退（等待目标实际消费、恢复空原剪贴板、`AHK_WAYLAND_PASTE=0` 可禁、uinput 通道），无注入路径明确报错
- 主要限制：ComObjArray、跨进程 Win32 消息和纯 Wayland 窗口枚举没有 Linux 等价物；完整 inputd 守护进程、IBus engine、KDE/Flatpak 宿主矩阵仍是后续项；SoundGet*/SoundSet* 依赖 pactl/amixer
- 完整逐模块报告:`tests/doccheck/CHECK_REPORT.md`

> 本矩阵为现状快照;状态随移植进度更新,以 CHECK_REPORT.md 与 worklist.tsv 为准。
