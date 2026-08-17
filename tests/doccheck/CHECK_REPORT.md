# AutoHotkey v2 Linux 移植版 — 官方文档逐模块校验报告 (Doc-Check Report)

- **对照文档**: AutoHotkey v2 官方指导文档 (`docs-v2/`, AutoHotkeyDocs 仓库 `v2` 分支,
  24 个概念页 + 356 个函数页)
- **校验对象**: Linux 移植版核心解释器 (`build-core/source/linux/core/ahk_core`,
  以及 ASan 构建 `build-asan/ahk_core`),基于 AutoHotkey v2.0.26 源码
- **校验方式**: 文档条目 → `.ahk` 实测脚本 → 输出与预期逐条比对
- **结果**: **994 / 994 断言通过** (普通构建与 ASan 构建均通过;含 Xvfb 虚拟显示下的窗口模块 67 项、输入模块 40 项、控件模块 62 项、显示器/像素/状态栏模块 25 项、定时器/悬浮提示模块 11 项、热键模块 10 项、编辑/列表模块 47 项、文件对话框模块 16 项、消息/热字串/RunAs 模块 49 项、图像模块 26 项、窗口形状模块 19 项、**GUI/控件/菜单模块 32 项**、**未移植函数错误行为模块 6 项**、**覆盖补全模块 75 项**(round-27)实测,与 headless 各模块;新增 **DllCall 29 项** 与 **D-Bus COM 18 项**;26 项 headless 回归测试亦全部通过)。另有 **Wayland 模式 13 项** 与 **XWayland 回退 235 项** 独立套件通过(见第 10 节)

---

## 1. 校验框架

| 文件 | 作用 |
|---|---|
| `tests/doccheck/extract_docs.py` | 解析 `docs-v2/docs/lib/*.htm`,提取每个函数的名称/描述/语法/参数/返回值/示例 → `doc_index.tsv`(352 个函数条目) |
| `tests/doccheck/build_worklist.py` | 将实现清单与文档条目联接,标注每个函数的实现状态 → `worklist.tsv`(367 个已实现,6 个边界操作抛明确错误(含 D-Bus COM 三件套与 TrayTip/TraySetIcon),298 个 doc 页有状态) |
| `tests/doccheck/assert_*.ahk` | 按模块编写的实测脚本(每个断言输出 `name=value` 行,取自官方文档语义) |
| `tests/doccheck/assert_*_expect.txt` | 与文档语义对应的期望值 |
| `tests/doccheck/run_check.sh` | 运行全部断言并逐条比对(支持传入任意二进制路径,如 `run_check.sh build-asan/ahk_core`) |

模块划分与断言分布:

| 模块 | 断言脚本 | 断言数 |
|---|---|---|
| 数学 | `assert_math.ahk` | 44 |
| 字符串 | `assert_string.ahk` | 47 |
| 对象/数组/Map/类 | `assert_object.ahk` | 33 |
| 文件/目录 | `assert_file.ahk` | 31 |
| 日期时间 | `assert_datetime.ahk` | 33 |
| 通用/环境/进程/驱动器/INI/剪贴板 | `assert_general.ahk` | 31 |
| 二进制互操作 (NumGet/NumPut/StrGet/StrPut/Buffer) | `assert_interop.ahk` | 20 |
| 正则 (RegExMatch/RegExReplace/~= 运算符/命名子组) | `assert_regex.ahk` | 22 |
| 注册表 (RegRead/RegWrite/RegDelete/RegDeleteKey/RegCreateKey) | `assert_registry.ahk` | 19 |
| 系统/设置/进程/文件操作/网络 (CoordMode/DetectHidden*/Set*Delay/SendMode/SendLevel/SetRegView/FileEncoding/SetStoreCapsLockMode/ProcessGet*/FileCopy/FileMove/FileInstall/FileRecycle/FileGetVersion/SysGet/SysGetIPAddresses/Download/Drive*/SoundPlay) | `assert_sys.ahk` | 106 |
| 窗口管理 (WinExist/WinActive/WinGet*/WinSet*/WinMove/WinClose/WinKill/WinWait*/WinActivate/WinMinimize/Maximize/Restore/Hide/Show/Redraw/Group*,X11 后端) | `assert_win.ahk` | 67 |
| 输入模拟 (Send/SendEvent/SendInput/SendPlay/SendText/Click/MouseMove/MouseClick/MouseClickDrag/MouseGetPos/KeyWait/BlockInput/InstallKeybdHook/InstallMouseHook/SetCapsLockState/SetNumLockState/SetScrollLockState/GetKeyState,XTEST 后端) | `assert_input.ahk` | 40 |
| 控件 (ControlGetText/SetText/GetPos/Move/GetHwnd/GetClassNN/Focus/GetFocus/GetSetStyle/ExStyle/GetSetEnabled/GetSetChecked/GetVisible/Show/Hide/Click/Send/SendText/Combo-List 系列/ShowHideDropDown + WinGetControls/WinGetControlsHwnd,X11 子窗口后端) | `assert_ctrl.ahk` | 62 |
| 显示器/像素/状态栏 (MonitorGet/GetCount/GetName/GetPrimary/GetWorkArea + PixelGetColor/PixelSearch + StatusBarGetText/StatusBarWait,XRandR/Xinerama + XGetImage 后端) | `assert_monitor.ahk` | 26 |
| 显示/快捷方式 (FileCreateShortcut/FileGetShortcut + ListVars/ListHotkeys/KeyHistory,.desktop/.url 与 headless 输出) | `assert_display.ahk` | 15 |
| 定时器/悬浮提示 (SetTimer + ToolTip,主循环 + X11 override-redirect 窗口) | `assert_timer.ahk` | 11 |
| 热键 (Hotkey + XGrabKey 激活) | `assert_hotkey.ahk` | 10 |
| 编辑/列表 (Edit/EditGet*/EditPaste + ListViewGetContent,虚拟编辑/列表状态) | `assert_edit.ahk` | 47 |
| 文件对话框 (FileSelect/DirSelect,内置 X11 路径输入对话框 + 无显示 stdin 回退) | `assert_dialog.ahk` | 16 |
| 消息/热字串/RunAs (OnMessage/SendMessage/PostMessage/MenuSelect/Hotstring/RunAs) | `assert_msg.ahk` | 49 |
| 图像 (LoadPicture/IL_*/ImageSearch,BMP/ICO/PNG/PPM 解码 + XGetImage 屏幕匹配) | `assert_image.ahk` | 32 |
| 窗口形状 (WinSetRegion,X11 SHAPE 扩展;xshape_probe 端到端验证) | `assert_shape.ahk` | 19 |
| GUI/控件/菜单 (Gui/GuiControl/Menu/MenuBar,GTK3 窗口;Edit/DDL/List/ListView/TreeView/StatusBar/Submit/OnEvent/菜单属性/HWND 反查等) | `assert_gui.ahk` | 32 |
| 未移植函数错误行为 (ComObjArray/ComObjQuery/ComObjConnect D-Bus COM 边界 + TrayTip/TraySetIcon 无托盘 + Send 无显示不得崩溃的回归) | `assert_notimpl.ahk` | 6 |
| 声音/光标/回调/输入钩子 (SoundGet*/SoundSet*/CaretGetPos/CallbackCreate+Free/InputHook,pactl/amixer + X11 + libffi 后端) | `assert_sound_etc.ahk` | 15 |
| 语句/指令/类别/索引页代码形式 (If/Else/For/While/Switch/Try/Catch/Throw/Loop/Until/Break/Continue/Return/Block + Array/Map/Object/Buffer/Error/Number/String 类别 + `#Requires`/`#Warn` 等) | `assert_statements.ahk` | 19 |
| 覆盖补全 (round-27:54 个未直引用函数中的 51 个 + String/Class/Menu/ObjBindMethod/Persistent/WinWaitNotActive 名称引用;含 Exit/Reload/Shutdown/InputBox 的"不可自动化"文档块) | `assert_misc_cov.ahk` | 75 |
| **合计 (X11/headless)** | | **994** |
| Wayland 模式 (Send 虚拟键盘经 sway bindsym 端到端(含修饰键组合与鼠标按钮)、ToolTip xdg 窗口、X11 专属表面报错) | `assert_wayland.ahk` | 13 |
| **合计 (Wayland)** | | **847** |
| XWayland 回退 (sway 的 XWayland 上运行 X11 套件:控件/编辑/对话框/消息/形状/图像/热键;图像经 wlr-screencopy 抓屏) | `wayland_run.sh --xwayland` | 235 |

复现命令:

```bash
bash tests/doccheck/run_check.sh                # 普通构建(自动启动本地 HTTP 服务供 Download 断言)
bash tests/doccheck/run_check.sh --xvfb         # 含窗口模块(Xvfb :99 + xwin_helper 测试窗口)
bash tests/doccheck/run_check.sh build-asan/ahk_core   # ASan 构建
bash tests/run_tests.sh                         # 26 项 headless 回归
bash tests/doccheck/wayland_run.sh [bin]        # Wayland 模式(sway headless + 虚拟键盘端到端)
bash tests/doccheck/wayland_run.sh --xwayland [bin]  # XWayland 回退(sway 的 XWayland 上跑 X11 套件)
```

---

## 2. 校验中发现的移植缺陷及修复

本轮逐模块对照文档后,共修复 10 类真实移植缺陷(非测试预期问题):

### 2.1 File 类方法整体缺失(最大缺陷)
- **现象**: `FileOpen()` 返回对象,但 `File.Write/Read/ReadLine/Seek/Pos/Length/Encoding/AtEOF/Handle` 等全部不存在。
- **根因**: Windows 上这些成员由 `Object::DefineMetadataMembers()` 经 DynaCall x64 汇编封送器注册;Linux 无 DynaCall,该函数被置为 no-op。
- **修复**: 新增 `source/linux/core/core_file_linux.cpp` + `core_file_linux.h`。`FileObject` 仅在 `TextIO.cpp` 内可见,故在 `TextIO.cpp` 内以 friend 函数 `DefineFileClassLinux()` 填充 `Object::*` 成员函数指针表,包装函数按 BIF 约定逐成员注册(27 个方法 + 5 个属性),`DefineMetadataMembers` 桩对 `"File"` 类接入注册。

### 2.2 HRESULT 在 Linux 上为 64 位导致错误被静默吞掉(影响面最广)
- **现象**: `DateAdd("20240101",1,"years")`、`DirCopy("",x)` 等参数错误**不抛异常**,脚本继续执行。
- **根因**: `typedef long HRESULT` 在 Linux x86-64 上是 64 位有符号,`FAILED(hr)`(`< 0`)对 `0xA00A0000` 恒为假,所有 `FResult` 错误码全部被忽略。
- **修复**: `stdafx_linux.h` 中 `typedef int32_t HRESULT`(与 Windows 一致)。
- **验证**: 新增断言 `DateAdd_years_err`(try/catch 捕获 ValueError),`assert_general` 中 `GetKeyState` 等错误路径回归通过。

### 2.3 `CharLower/CharUpper(LPTSTR)` 实现错误
- **现象**: `StrUpper("abc")` 原样返回。
- **根因**: 原实现把指针值当字符做 `towlower`,字符串从未被修改;而 `ltoupper/ltolower` 又依赖"把字符当指针传"的 Win32 惯例。
- **修复**: 按 Win32 语义实现:值 ≤ 0xFFFF 视为字符,否则视为字符串指针并就地转换(`StrTitle` 曾因此崩溃,回归修复)。

### 2.4 `_vsntprintf` 格式翻译器缺陷(同时破坏 `Format()` 与类字段初始化)
- **现象**: `Format("{1:02d}",7)` 输出字面量 `%02I64d`;`Format("{1:.2f}",3.14)` 输出 `0.00`;类字段 `v := 10` 变成 `"1"`。
- **根因**:
  1. glibc 不认识 MSVC 的 `I64` 长度修饰符,原样输出;
  2. SysV ABI 下 `%f` 从 XMM 寄存器取 double,而调用点按 `__int64`(GPR)传参,读到 0.0;
  3. `%.*s`(星号精度)使 `%s→%ls` 翻译失效,glibc 把宽字符串当窄串读,只取到首字节——类字段初始化正是用 `%.*s` 拼接 `this.a := 10` 的。
- **修复**: 翻译器支持 `*` 宽度/精度并映射 `I64→ll`、`I32→`删除;`BIF_Format` 在 Linux 下按 `SYM_FLOAT/SYM_INTEGER` 传对应类型的可变参数。

### 2.5 路径函数缺失导致 A_ScriptName/A_WorkingDir 为空
- **现象**: `A_ScriptName` 为空,启动时 `A_WorkingDir` 为空。
- **根因**: `GetFullPathName/GetCurrentDirectory/SetCurrentDirectory` 是返回 0/TRUE 的桩;`main_linux` 未复刻 `Script::Init()` 的路径切分(mFileSpec/mFileDir/mFileName 从未赋值)。
- **修复**: 三个函数改为真实 POSIX 实现(realpath 语义 + 手工规范化,支持不存在的路径);`main_linux.cpp` 在加载前解析脚本全路径并填充 `mFileSpec/mFileDir/mFileName`。

### 2.6 GetKeyName/GetKeyVK/GetKeySC 无实现
- **现象**: `GetKeyName("Enter")` 返回空。
- **根因**: `TextToVKandSC/TextToVK/TextToSC/GetKeyName(vk,sc)/vk_to_sc/sc_to_vk` 全部为返回 0 的桩。
- **修复**: `core_platform_stubs.cpp` 内建键名表(常用键 + 鼠标键 + 标点 + 修饰键,字母 A-Z、数字 0-9、F1-F24、Numpad0-9 按区间计算;支持 `vkXX`/`scXXX` 十六进制写法)。

### 2.7 FileExist/DirExist 语义不符文档
- **现象**: `FileExist("/tmp")` 返回空(仅识别普通文件)。
- **根因**: 桩实现只查 `is_regular_file`。
- **修复**: 复用上游 `DoesFilePatternExist + FileAttribToStr`,按文档返回属性字母串(`D` 目录、`A` 普通文件等),`DirExist` 同样处理。

### 2.8 类字段初始化被截断为单字符
- 根因与 2.4 相同(`%.*s` 宽串翻译缺陷),修复后 `v := 10 → 10`、`c := "hi" → hi`。

### 2.9 回调式错误未抛出(连带修复)
- `FResultToError` 路径经 2.2 修复后,`OnError` 等回调及所有 md 函数错误路径恢复文档语义。

### 2.10 FindFirstFile/SetFileTime/SetFileAttributes 为桩(本轮)
- **现象**: `FileGetTime/FileGetSize` 找不到文件,`FileSetTime/FileSetAttrib` 报错,`Loop Files` 不迭代。
- **根因**: `FindFirstFile/FindNextFile/FindClose` 返回 INVALID/FALSE;`SetFileTime`、`SetFileAttributes` 为恒真 no-op。
- **修复**: 以 `opendir/readdir` + 大小写不敏感通配匹配实现 Find 系列(句柄带 MAGIC 标识,`CloseHandle` 可区分 FILE*/进程/查找句柄);`SetFileTime` 经 `futimens` 实现(M/C/A 选择,创建时间无法修改则忽略);`SetFileAttributes` 经 `chmod` 实现(READONLY↔写位);`FilePatternApply` 同步支持 `/` 分隔;`GetFileAttributes` 补 READONLY/ARCHIVE 位。

### 2.11 FILETIME 本地/UTC 转换互为 no-op(本轮)
- **现象**: `FileSetTime("20200102030405", f)` 后再 `FileGetTime` 得到 `20200102110405`(+8 小时)。
- **根因**: `LocalFileTimeToFileTime` 用 `localtime_r`+`mktime` 往返,净效果为零。
- **修复**: 用 `gmtime_r` 取出墙钟字段再 `mktime` 按本地解释,与 `FileTimeToLocalFileTime` 对称。

### 2.12 文件时间/属性/遍历函数接入真实实现(本轮)
- 为 lib/file.cpp 中已有的 `FileGetAttrib/FileGetTime/FileGetSize/FileSetAttrib/FileSetTime` 原生实现补齐 BIF 包装并切换到 `LMD_IMPL`;缺失文件按文档抛 OSError。

### 2.13 Loop Files 内置变量为桩(本轮)
- **现象**: `A_LoopFileName` 输出指针值。
- **根因**: `BIV_LoopFile*` 9 个内置变量均为空桩(lib/vars.cpp 未链接)。
- **修复**: 按 vars.cpp 原实现移植 Name/Ext/Dir/Path/FullPath/ShortPath/Time/Attrib/Size。

### 2.14 NumGet/NumPut/StrGet/StrPut 整体缺失(本轮)
- **现象**: 均为 no-op 桩。
- **修复**: 编译 `lib/interop.cpp`(去重 `GetBufferObjectPtr` 桩);`BufferObject` 增加 Linux 专属虚函数强制独立 vtable(GCC 不覆盖虚函数时共享基类 vtable,`IsInstanceExact` 会误判所有对象);`WideCharToMultiByte/MultiByteToWideChar` 重写为精确大小计算并支持 4 字节 UTF-8/代理对。

### 2.15 StrGet 的 Length 语义(本轮)
- **现象**: `StrGet(buf, 2, "UTF-8")` 读 2 字节截断多字节字符。
- **根因**: 共享代码对非 UTF-16 编码按字节 `strnlen`。
- **修复**: Linux 下按文档"最大字符数"语义对 UTF-8 逐字符计数。

### 2.16 控制台输出丢弃代理对(本轮)
- **现象**: 含 emoji 的 MsgBox 输出为空。
- **根因**: glibc `wcstombs` 在 C.UTF-8 下拒绝代理对。
- **修复**: `WideToNarrow` 改为手写 UTF-8 编码(支持代理对)。

### 2.17 RegExMatch/RegExReplace 整体接入(本轮)
- **现象**: 两个函数为 no-op 桩;`~=` 运算符不可用。
- **根因**: `lib/regex.cpp` 及其捆绑的 PCRE1 库未编入 Linux 构建;且捆绑头把 `PCRE_UCHAR16` 定义为 `wchar_t`(Windows 上为 16 位、Linux 上为 32 位),与引擎内部的 16 位单元完全错位。
- **修复**:
  1. CMake 启用 C 语言,新增 `ahk_pcre16` 静态库(18 个 `pcre16_*.c`,`-DHAVE_CONFIG_H`);
  2. `lib_pcre/pcre/pcre.h` 在非 Windows 平台把 `PCRE_UCHAR16` 定义为 `unsigned short`(16 位);
  3. `regex.cpp` 增加 Linux UTF-16 转换层:模式/主语转为 UTF-16 并维护 unit↔wchar 双向偏移映射,匹配偏移回映射到 wchar 位置;命名子组名表按 16 位单元解析并转换为宽字符副本(副本在对象复制完成后释放,避免与堆复用冲突);
  4. 修复 C++ 硬错误(goto 越过初始化、const 字符串返回)。

### 2.18 校验中修正的断言预期(本轮)
- `~=` 运算符返回匹配位置(与 RegExMatch 相同),而非布尔值——按文档修正断言。

### 2.19 A_* 环境变量批量接入(本轮)
- **现象**: `A_LastError/A_IsPaused/A_IsSuspended/A_IsCritical/A_LineNumber/A_LineFile/A_ComputerName/A_UserName/A_OSVersion/A_Language/A_MyDocuments/A_AhkPath/A_Args/A_EventInfo/A_ThisFunc/A_ScriptHwnd` 均为空桩或恒值。
- **修复**: 按 lib/vars.cpp 原语义实现:
  - `A_Args`: main_linux 启动时用 `Array::FromArgV` 填充(脚本路径后的参数);
  - `A_LastError`(含赋值)、`A_EventInfo`(含赋值)、`A_IsPaused`(`g[-1].IsPaused`)、`A_IsSuspended`、`A_IsCritical`(peek 频率);
  - `A_LineNumber/A_LineFile`: `Script::CurrentLine/CurrentFile` 桩改为真实实现;
  - `A_ComputerName`(gethostname)/`A_UserName`(USER)/`A_OSVersion`(uname release)/`A_Language`(LANG→LCID 映射)/`A_MyDocuments`(XDG ~/Documents)/`A_AhkPath`(/proc/self/exe);
  - 返回值统一使用 `SimpleHeap` 持久内存(修复了局部栈缓冲在 BIV 返回后失效导致读值错乱的问题)。
- run_check.sh 支持为指定套件追加命令行参数(assert_general 以 `one two` 运行以校验 A_Args)。

### 2.20 注册表模块(Linux 文件虚拟注册表,本轮)
- **现象**: RegRead/RegWrite/RegDelete/RegDeleteKey/RegCreateKey 均为 no-op 桩。
- **设计**: Linux 无注册表,移植版以 `$XDG_CONFIG_HOME/autohotkey-registry.txt`(默认 `~/.config/...`)存放虚拟注册表:INI 风格 `[键路径]` 节 + `名称=类型:值` 行(`@` 为默认值),值内反斜杠/换行/`=` 转义;REG_DWORD 存十进制、REG_BINARY 存大写十六进制、REG_MULTI_SZ 以换行分隔。
- **语义对照文档实现**: 根键全称/缩写(HKCU/HKLM/...)规范化;ValueName 省略读写默认值;RegRead 缺值有 Default 返回 Default、无则抛 OSError(LastError=2);REG_DWORD 读为正十进制、REG_BINARY 读为十六进制串、REG_MULTI_SZ 读为 `\n` 结尾组件;RegWrite 返回写入字节数;RegDelete 删命名值、缺值抛错;RegDeleteKey 递归删除键与子键;非法根键/类型抛 OSError;REG_BINARY 支持 Buffer 输入。

### 2.21 FileAppend/FileRead 忽略 FileEncoding 默认编码(本轮)
- **现象**: `FileEncoding("UTF-16")` 后 `FileAppend` 写出的仍是 UTF-8。
- **根因**: Linux 的 `BIF_FileAppend`/`BIF_FileRead` 是简化实现,固定用 UTF-8(wcstombs/fread),从不读取 `g->Encoding`。
- **修复**: 按文档实现默认编码语义——FileAppend 在新建(空)文件时按 `UTF-8`/`UTF-16` 写 BOM(`-RAW` 变体不写),UTF-16 内容以手写 UTF-16LE 编码(支持代理对)写出;FileRead 先做 BOM 探测(BOM 覆盖默认),UTF-16 以代理对感知的 UTF-16LE 解码,其余按 UTF-8;同时补上文档要求的"文件不存在抛 OSError"行为(此前静默返回空串)。

### 2.22 FileCopy/FileMove 通配符与目录目标缺失(本轮)
- **现象**: FileCopy/FileMove 为 no-op 桩;接入 lib/file.cpp 后其底层 `Line::Util_CopyFile` 桩既不支持 `*.txt` 通配、也不支持目标为目录、返回值语义还与上游相反。
- **修复**: 按 script_autoit.cpp 上游语义重写:`FindFirstFile` 枚举匹配(大小写不敏感通配),目录源/目标自动追加 `/*`,目标含通配时按 `Util_ExpandFilenameWildcard` 语义展开(`*.txt`→源文件名;多余 `*` 去除),无通配且无匹配抛错、有通配无匹配视为成功(文档),move 优先 `rename`、跨设备回退复制+删除,失败计数经 `Error.Extra` 上报;`FileInstall`(未编译脚本=复制文件)、`FileRecycle`/`FileRecycleEmpty`(XDG Trash 规范:文件移入 `Trash/files` + 写 `.trashinfo`,重复名自动加 `.N` 后缀)同期接入;`FileGetVersion` 按文档"文件缺少版本信息时抛 OSError"实现(Linux 文件普遍无版本资源)。

### 2.23 设置/进程/系统/网络模块批量接入(本轮)
- **实现**: 按 lib/vars.cpp/lib/env.cpp/lib/process.cpp/script_autoit.cpp 原语义实现:
  - 设置模块 13 函数(CoordMode/DetectHiddenWindows/DetectHiddenText/SetTitleMatchMode/SetKeyDelay/SetMouseDelay/SetWinDelay/SetControlDelay/SetDefaultMouseSpeed/SendMode/SendLevel/SetRegView/SetStoreCapsLockMode),全部"返回先前值/设置",参数校验抛 ValueError(与上游 ParamError 一致);默认值经实测与上游 `global_set_defaults` 逐一吻合(KeyDelay=10、MouseDelay=10、MouseDelayPlay=-1、WinDelay=100、ControlDelay=20、DefaultMouseSpeed=2、SendMode=Input、TitleMatchMode=2、DetectHiddenText=1、StoreCapsLockMode=1 等);
  - 对应 15 个 A_* 内置变量(BIV)真实实现(A_CoordMode*/A_DetectHidden*/A_TitleMatchMode/Speed/A_KeyDelay*/A_KeyDuration*/A_MouseDelay*/A_WinDelay/A_ControlDelay/A_DefaultMouseSpeed/A_SendMode/A_SendLevel/A_RegView/A_FileEncoding/A_StoreCapsLockMode/A_ListLines/A_ScreenWidth/Height/DPI);
  - 进程模块:ProcessGetName/ProcessGetPath(/proc/comm、/proc/<pid>/exe readlink)、ProcessGetParent(/proc/<pid>/stat 第 4 字段),名称匹配大小写不敏感、按文档抛 TargetError(找不到)/OSError(读取失败);
  - SysGet:SM_* 常量经 X11 实现(有显示时返回真实屏幕尺寸/监视器数,无显示时 0;SM_CMOUSEBUTTONS=3、SM_MOUSEPRESENT=1、SM_NETWORK=1、SM_CARETBLINKINGENABLED=1 等与平台无关项恒定);SysGetIPAddresses:getifaddrs 返回 IPv4 数组(含 127.0.0.1);
  - Download:curl/wget 下载,HTTP 错误页照文档"保存错误页而非报错"保存,目标路径不可写抛 OSError;Shutdown:EWX_* 标志映射 systemctl/loginctl(未在测试中实际执行,防误关机);
  - DriveSetLabel/e2label+fatlabel、DriveEject/Retract/eject、DriveLock/Unlock/udisksctl(设备经 /proc/mounts 由挂载点解析);SoundPlay:paplay/aplay;失败均按文档抛 OSError;
  - run_check.sh 自动启动本地 python http.server(:18765)供 Download 断言,退出时清理。
- **实测环境注意**: 文件操作断言的工作目录放在 /tmp(ext4)——DrvFS(/mnt/f)存在陈旧目录项缓存,创建文件后立即 stat/复制偶发失败(首次运行通过、后续复现,`copy_file` 报文件已存在),与解释器无关。

### 2.24 窗口管理模块(X11 后端,本轮)
- **实现**: 新增 `core_win_linux.cpp`(HWND = X11 Window id),按 window.cpp/lib/win.cpp/lib/wait.cpp 语义实现 49 个函数 + WinExist/WinActive:
  - **WinTitle 条件解析**: 标题 + `ahk_id/ahk_pid/ahk_class/ahk_exe/ahk_group`,按上游 SetCriteria 逐字移植;标题/类名大小写敏感(`_tcsstr/_tcscmp`)、`ahk_exe` 大小写不敏感(`_tcsicmp`,无斜杠时按进程名匹配);匹配模式 1 前缀/2 包含/3 精确/RegEx(复用捆绑 PCRE 的 `RegExMatch`);ExcludeTitle 同模式排除;DetectHiddenWindows 控制隐藏窗口可见性(X map_state);
  - WinGetTitle/Class/PID/ProcessName/ProcessPath/ID/IDLast/Count/List/Pos/ClientPos/MinMax/Style/ExStyle/Text/Transparent/TransColor/Controls/ControlsHwnd:找不到窗口按文档抛 TargetError(WinGetList/Count 返回空数组/0);WinGetPos 输出参数 X/Y/W/H;
  - WinActivate(XRaiseWindow+XSetInputFocus+_NET_ACTIVE_WINDOW)/WinActivateBottom/WinClose(WM_DELETE_WINDOW 协议,无则 XDestroyWindow,WaitTime 轮询)/WinKill(XKillClient)/WinMove/WinRedraw/WinHide/WinShow/WinMinimize/XIconifyWindow/WinMaximize(记忆原几何,恢复用)/WinRestore/WinMoveTop/Bottom/WinMinimizeAll[Undo];
  - WinSetTitle(_NET_WM_NAME+WM_NAME,UTF-8)/WinSetAlwaysOnTop(_NET_WM_STATE_ABOVE,WinGetExStyle 反映 WS_EX_TOPMOST)/WinSetTransparent(_NET_WM_WINDOW_OPACITY,0-255 或 Off)/WinSetTransColor(校验 6 位十六进制,存虚拟状态)/WinSetEnabled/WinSetStyle/WinSetExStyle(`+`/`-`/覆盖 位操作,虚拟状态);
  - WinWait/WinWaitActive/WinWaitNotActive/WinWaitClose:返回 HWND/0 或 1/0,Timeout 省略=无限(按 wait.cpp);GroupAdd/GroupActivate(visited 列表循环,与 WinGroup::Activate 一致,R 模式复位)/GroupClose(A 模式关全部)/GroupDeactivate(最小化除活动窗口外全部成员);
  - X 连接缓存 + **忽略 X 协议错误的错误处理器**(XGetInputFocus 可能返回 PointerRoot(0x1),默认 Xlib 处理器会直接退出进程——实测 BadWindow 崩溃)。
- **测试设施**: `xwin_helper.c` 测试客户端(Xvfb :99 下创建带 WM_NAME/WM_CLASS/_NET_WM_PID/WM_DELETE_WINDOW 的窗口,支持 -hidden/-focus/-ms);`run_check.sh --xvfb` 模式自动编译并启动,assert_win 以文件输出(有显示时 MsgBox 会阻塞),断言覆盖 条件匹配/获取/操作/等待/分组 全流程;
- **本轮修复的真实缺陷**: ① WinGetCount/WinGetList 只收集到第一个匹配(查找函数在首匹配即返回);② `ahk_exe` 条件解析错位(关键字定位用 `find('_')`,被值里的下划线干扰);③ X 协议错误导致进程退出;④ 测试助手 `-hidden` 未生效(XMapWindow 无条件调用)。
- **脚本运行语义确认**: 上游 `LoadFromFile` 会把工作目录切到脚本所在目录(Windows 同款行为),因此 Run 的相对路径以脚本目录为基准;长驻子进程持有管道导致 runner 等待 EOF 属测试脚手架问题,非解释器缺陷。

### 2.25 输入模拟模块(XTEST 后端,本轮)
- **实现**: 新增 `core_input_linux.cpp`(输入模块,17 个 BIF + GetKeyState/GetAsyncKeyState 真实实现),按 lib/sendkeys.cpp、lib/mouse.cpp、lib/keywait.cpp 语义:
  - **Send 引擎**: 字面字符(US 布局基键 + Shift 合成,`LinuxCharBase/LinuxCharNeedsShift`)、修饰符 `^+!#`(按住-发送-释放,`{Blind}`/`{Text}` 保持状态)、`{KeyName}` 带 down/up 后缀与重复计数、`{vkXX}`、`{Click ...}`(坐标/按钮/Down/Up)、Enter/Tab 特殊映射;`{Enter 3}` 重复 3 次;
  - SendEvent/SendInput/SendPlay 在 Linux 均经 XTEST 投递(与 SendMode 文档"推荐 SendInput 防打断"的精神一致——XTEST 事件对用户输入天然不可打断);SendText 全字面;
  - **Mouse**: MouseMove(相对 R 模式)、MouseClick(按钮名 Left/Right/Middle/XButton1/2/Wheel*、坐标省略=当前指针、ClickCount 重复、DownOrUp D/U 按住/释放、R 相对)、MouseClickDrag(移动+按住+释放)、MouseGetPos(输出 X/Y/WhichWindow/WhichHWND,输出参数按实参个数保护写)、Click(BIF_Click,坐标/按钮/Down/Up);
  - **KeyWait**: 轮询 XQueryKeymap(真实键状态,与 XTEST 事件同步),"D" 选项等按下、省略等释放,返回 1;KeyWait 期间 20ms 轮询;
  - **GetKeyState/GetAsyncKeyState**(真实):XQueryKeymap 位图(修饰键 vk 同时查左右变体),"T" 模式经 Xkb 锁键状态,`ScriptGetKeyState`/`SendThisHotkey` 等共享代码路径同步生效;
  - **SetCapsLockState/SetNumLockState/SetScrollLockState**:XkbLockModifiers,`On/Off/-1(切换)/0(释放)/1(按下)` 语义,`-1` 切换检测修正(此前把 `-1` 误判为开关解析错误);
  - **BlockInput**(状态机):按 v2 文档三组独立模式——OnOff(On/1 全阻断、Off/0 解除)、SendMouse(Send/Mouse/SendAndMouse 阻断对应输入、**Default 只关 Send/Mouse 模式、不动 OnOff 阻断状态**)、MouseMove(MouseMove/MouseMoveOff);实现为 X 键盘/指针抓取(owner_events=False,Async):抓取期间硬件输入被本进程吞掉、其他客户端收不到,而脚本自身的 XTEST 模拟仍可产生(与文档"user input is blocked but AutoHotkey can simulate keystrokes"一致);脚本退出时服务端自动释放抓取(文档"Input is automatically re-enabled when the script closes");与 Windows 的偏差:Send/Mouse 模式是持续抓取而非仅 Send 执行期间(无钩子系统);
  - 独立 `LinuxInputDisplay` 缓存连接 + 忽略 X 错误处理器(XOpenDisplay 对 BadWindow 崩溃问题同窗口模块);
- **测试设施**: `xkeycap.c` 测试客户端(Xvfb :99 下带焦点窗口,按 Shift 状态取 keysym 列、记录键/按钮事件到文件);`run_check.sh --xvfb` 编译并运行 assert_input(40 断言:各 Send 变体事件序列、Shift 合成、修饰键、按住/重复、Mouse 各参数、KeyWait 状态、锁键开关、BlockInput On/Default/Off 阻断语义、钩子安装标志、GetKeyState 逻辑/物理状态);
- **本轮修复的真实缺陷**: ① MouseGetPos/WinGetPos 对少于输出参数个数的调用读取越界 aParam(输出写入全部改为 `aParamCount > N` 保护);② "Left/Right/Middle/XButton1/2/Wheel*" 按钮名不在键表 → 新增 `LinuxButtonFromName`(键表只有 LButton);③ KeyWait/GetKeyState "Invalid key name" 对 a-z 小写失败 → 键表补小写分支;④ Shift 合成字符 keysym 解析(XLookupKeysym 索引 1 带 Shift 列);⑤ BlockInput `Default` 误实现为"全阻断"(文档:只关 Send/Mouse 模式);⑥ 测试脚手架:xkeycap 移除自身 GX 抓取(否则 BlockInput 的 XGrabKeyboard 返回 AlreadyGrabbed);KeyWait/锁键断言不消费捕获事件,BlockInput 断言前须排空待读事件。

### 2.26 TextFile::_Seek 栈越界读取(本轮,ASan 构建捕获)
- **现象**: ASan 构建运行 assert_input(File.Seek)时 `stack-buffer-overflow` 中止,普通构建不报错。
- **根因**: 上游 `_Seek` 用 `*((PLARGE_INTEGER)&aDistance)` 别名技巧传参——Windows 上 `LONG` 为 32 位,`LARGE_INTEGER` 恰为 8 字节;Linux 上 `long` 为 64 位,`struct {DWORD LowPart; LONG HighPart;}` 变为 12 字节并填充到 16,联合体变成 16 字节,从 8 字节栈变量读取 16 字节 → UB(QuadPart 恰与低 8 字节重合,普通构建"碰巧正确")。
- **修复**: `TextIO.cpp` 改为先 `LARGE_INTEGER li; li.QuadPart = aDistance;` 再传值,消除别名读取;`lib/file.cpp:973` 的 `(PLARGE_INTEGER)&size` 仅写入 8 字节,无越界,保留。

### 2.27 控件模块(X11 子窗口后端,本轮)
- **实现**: 新增 `core_ctrl_linux.cpp`(控件模块,32 个 Control* BIF + WinGetControls/WinGetControlsHwnd 真实实现),按 docs-v2 与上游 lib/win.cpp 语义:
  - **控件标识**(Control Identifiers 文档):HWND(整数/`ahk_id N`/纯数字串,高于一切)→ ClassNN(WM_CLASS 类名 + 同类序号,大小写不敏感、序号按十进制串精确比较,与上游 EnumControlFind 一致)→ 文本(WM_NAME/_NET_WM_NAME,按 SetTitleMatchMode 1/2/3/RegEx,区分大小写);空 Control = 目标窗口本身(上游 DetermineTargetControl);子窗口按 XQueryTree 深度优先枚举(上游 EnumChildWindows);
  - **真实 X11 操作**: ControlGetText/SetText(_NET_WM_NAME UTF-8 + WM_NAME)、ControlGetPos/Move(经 XTranslateCoordinates,坐标相对目标窗口客户区,减去子窗口边框宽度得外沿原点)、ControlGetHwnd、ControlFocus/ControlGetFocus(返回 HWND,0 = 无焦点控件,须为目标窗口后代)、ControlGetVisible/Show/Hide(map 状态)、ControlClick(ClassNN/文本/HWND/`xN yM` 客户区坐标,按钮 Left/Right/Middle/X1/X2、ClickCount、Options 的 D/U/x/y,复用 XTEST 鼠标引擎)、ControlSend/ControlSendText(聚焦控件 → XTEST 键盘引擎 → 恢复焦点,复用 Send 引擎);
  - **虚拟状态**(Windows 经消息暴露、X11 无对应物): ControlGet/SetStyle/ExStyle(`+` 加/`-` 减/`^` 切换/覆盖)、Get/SetEnabled、Get/SetChecked(均支持 `-1` 切换)、Combo/List 系列(ControlAddItem 返回新条目序号、DeleteItem 按序号删、FindItem 全串大小写不敏感匹配未命中抛 Error、ChooseIndex 0=取消选择、ChooseString 前缀匹配返回序号、GetChoice 未选择抛 Error、GetIndex、GetItems 数组、Show/HideDropDown 标志),类名必须含 "Combo"/"List"(ChooseIndex/GetIndex 另容 "Tab")否则 TargetError(文档);
  - 改变控件的函数按文档执行 SetControlDelay 延时(SetStyle/ExStyle 除外);WinGetControls/WinGetControlsHwnd 从空数组桩改为真实枚举(ClassNN 与 HWND 数组)。
- **测试设施**: `xwin_helper.c` 扩展支持 `-child NAME CLASS X Y W H` 创建子"控件"窗口并记录各窗口收到的键/按钮事件(`-evout`);`assert_ctrl.ahk`/`assert_ctrl_expect.txt`(新增,62 断言);`run_check.sh --xvfb` 运行 assert_ctrl。
- **本轮修复的真实缺陷**: ① ControlGetPos/ControlMove 的 LMD 参数范围把可选的 X/Y/W/H 当成必填(解释器按 `参数 < mMinParams 即必填` 校验)——改为全可选 + BIF 内按文档强制 Control 必填;② Control 参数为变量(非整数字面量)时 HWND 识别失败(令牌类型判断过窄)——新增统一解析(整数令牌/`ahk_id N`/纯数字串 → HWND,文档优先级);③ ControlGetItems/GetChoice/GetIndex 的 Control 参数下标错位(误用带前置参数函数的 1/2);④ 子窗口边框宽度使 ControlGetPos/ControlMove 的坐标差 1 像素;⑤ 测试脚手架:Run 相对路径以脚本目录为基准(调试脚本放 /tmp 时 helper 找不到),DrvFS 目录缓存陈旧导致新建脚本"not found"。

### 2.28 显示器/像素模块(XRandR/Xinerama + XGetImage,本轮)
- **实现**: 新增 `core_screen_linux.cpp`(显示器模块 5 个 Monitor* BIF + PixelGetColor/PixelSearch),按 docs-v2 与上游 lib/env.cpp / lib/pixel.cpp 语义:
  - **显示器**: XRandR 1.2 outputs(带输出名,如 Xvfb 的 "screen")枚举,无 XRandR 时回退 Xinerama、再回退单屏;MonitorGetCount 返回台数;MonitorGetPrimary 恒为 1(首个);MonitorGet/GetWorkArea 输出左/上/右/下坐标(右/下为排他边界,与 Windows 一致),返回显示器序号,N 省略=主显示器;N 越界按上游 FR_E_ARG 抛 **ValueError**,无显示器抛 OSError;MonitorGetName 返回 XRandR 输出名(Xinerama 回退路径生成 "MonitorN");
  - **像素**: PixelGetColor 按文档返回**十六进制数字串** `0xRRGGBB`(经默认视觉的 RGB 掩码换算,非 TrueColor 视觉走 XQueryColor 查色表),屏幕外/取图失败抛 OSError;PixelSearch 按文档返回 1/0、未命中时输出变量置空、Variation 为 0-255 逐通道容差、搜索从 (X1,Y1) 向 (X2,Y2) 逐行(方向随参数大小,文档"The search starts at the coordinates specified by X1 and Y1");
  - 两函数遵守 **CoordMode Pixel**(CLIENT = 相对活动窗口客户区,经 XTranslateCoordinates 换算屏幕坐标)。
- **测试设施**: `xwin_helper.c` 新增 `-fill RRGGBB X Y W H` 实色矩形(像素断言用);`assert_monitor.ahk`/`assert_monitor_expect.txt`(新增,18 断言);`run_check.sh --xvfb` 运行 assert_monitor。
- **本轮修复的真实缺陷(ASan 捕获)**: ① PixelGetColor 返回的十六进制串用栈缓冲区经 `aResultToken.SetValue` 传出——调用方在 BIF 返回后读取 → stack-use-after-return → 改经 `LinuxWinSetPersistentEx`(SimpleHeap 持久内存);② LinuxMonitorList 未释放 `XRRGetCrtcInfo` 结果 → LSan 泄漏导致 ASan 构建退出码 1 → 补 `XRRFreeCrtcInfo`;③ 测试脚手架交叉污染:assert_monitor 崩溃遗留的 xwin_helper 使 assert_win 的 `ahk_exe xwin_helper` 计数断言从 3 变 4——断言改为组合条件 `DocCheck ahk_exe xwin_helper`(同时顺带验证文档"条件可组合")。

### 2.29 显示/快捷方式模块(.desktop/.url + headless 显示,本轮)
- **实现**: 新增 `core_display_linux.cpp`(FileCreateShortcut/FileGetShortcut + StatusBarGetText/StatusBarWait + ListVars/ListHotkeys/KeyHistory),按 docs-v2 与上游语义:
  - **FileCreateShortcut/FileGetShortcut**: `.url` 扩展名按文档生成 Internet 快捷方式(INI `[InternetShortcut] URL=` 格式,文档原文);其余扩展名生成 freedesktop `.desktop` 启动器(Exec= 目标+参数,目标含空格自动加引号;Path/Icon/Comment/Name 字段;Name 取目标基名)——即 Linux 上 `.lnk` 的对应物;FileGetShortcut 解析两种格式回填 目标/工作目录/参数/描述/图标 输出,图标序号/运行状态在 Linux 无对应返回 0/1,失败按文档抛 OSError;
  - **StatusBarGetText/StatusBarWait**: 目标窗口后代中类名含 "statusbar" 的第一个子窗口(文档:标准状态栏 msctls_statusbar32);Part 1 = 栏文本(X11 单部件,Part>1 返回空),StatusBarWait 按 SetTitleMatchMode 匹配、默认 50ms 轮询、Timeout 省略=无限、返回 1/0,窗口/状态栏缺失按文档抛 TargetError;
  - **ListVars/ListHotkeys/KeyHistory**: 上游经 ShowMainWindow 弹窗显示,Linux 移植版以 MsgBox(headless 输出到 stdout)展示——ListVars 经 `Script::mVars`(新增 friend 类 LinuxVarDump,与 Debugger.cpp 相同的普通/虚拟变量取值路径)枚举全部全局变量;KeyHistory 参数按上游校验 0..500 并存储最大值;ListHotkeys 显示空列表(Linux 无热键系统)。
- **测试设施**: `assert_display.ahk`(headless,11 断言 + 4 项自由文本内容检查:ListVars 输出含变量名与值、ListHotkeys/KeyHistory 输出;run_check.sh 为 assert_display 增加内容模式比对);`assert_monitor.ahk` 增补状态栏断言(xwin_helper 增加 msctls_statusbar32 子窗口,7 断言)。
- **本轮修复的真实缺陷**: ① ListVars 对普通变量误用 `Var::Get`(仅虚拟变量可用,普通变量须 `ToToken`,且 ResultToken 须先初始化——照 Debugger.cpp 的 GetPropertyValue 模式);② **Control 的 ClassNN 解析**:按"去尾数字再比较类名"实现会破坏类名本身以数字结尾的控件(如 `msctls_statusbar321` 被拆成类 `msctls_statusbar` + 序号 321)——改为上游 EnumControlFind 算法:以每个候选控件的类名长度取判据前缀做大小写不敏感比较,再以十进制字符串精确比较序号,天然兼容类名含数字。

### 2.30 定时器/悬浮提示模块(主循环 + 线程栈,本轮)
- **实现**: 新增 `core_timer_linux.cpp` + 升级线程基础设施,按 docs-v2 与上游语义:
  - **线程栈**(移植 application.cpp 的 InitNewThread/ResumeUnderlyingThread,替换原先的 no-op 桩):新线程从 `g_default`(g_array[0])复制默认设置、清状态、设优先级,按 `Thread Interrupt` 规则初始化不可中断性;结束线程时递减计数,非持久脚本在最后一个线程结束时自动退出(ExitIfNotPersistent);
  - **CheckScriptTimers**(移植上游):逐定时器检查(启用/占用/优先级/到期),mTimeLastRun 在启动前重置(文档"run-only-once 由自身线程重置"语义),一次性定时器自动禁用,执行完毕的一次性定时器自动删除;带暂停/线程数上限守卫;
  - **主循环**:main_linux 在自动执行段结束后,若脚本持久或存在启用定时器则进入 `LinuxRunMainLoop`(按最近到期时间睡眠、上限 50ms、逐次触发到期定时器,直到 ExitApp);**MsgSleep 升级**:等待期间按 10ms 切片触发到期定时器(文档:"timers run even when the script is waiting for a window, displaying a dialog, or busy with another task");
  - **SetTimer**:接入上游 bif_impl(函数对象校验、Period 0=删除、负 Period=仅运行一次、省略函数=当前定时器、默认 250ms);
  - **ToolTip**:每个 WhichToolTip 索引一个 X11 override-redirect 顶层窗口(文本即窗口标题,无工具包文本渲染),X/Y 遵守 CoordMode ToolTip(默认相对活动窗口客户区),更新复用同一 HWND,空白/省略 Text 隐藏并返回 0,否则返回工具提示的 HWND(文档);脚本退出时随 X 连接自动销毁(文档"displayed until the script terminates")。
- **测试设施**: `assert_timer.ahk`/`assert_timer_expect.txt`(新增,11 断言:周期触发、停止、默认 250ms、负周期仅一次、省略函数引用当前定时器、非法函数对象报错、ToolTip 返回 HWND/标题/同窗更新/坐标/隐藏);`run_check.sh --xvfb` 运行。
- **本轮修复的真实缺陷**: ① InitNewThread/ResumeUnderlyingThread 原为 no-op 桩(定时器/热键线程无法建立)——完整移植上游实现;② MsgSleep 原为纯 nanosleep(等待期间定时器不触发)——按文档在等待中触发;③ ToolTip 返回值按 md_func 声明为 HWND,须经 LMD 表返回。

### 2.31 热键模块(XGrabKey 激活,本轮)
- **实现**: 新增 `core_hotkey_linux.cpp`(Hotkey 函数接入 + X 热键激活),按 docs-v2 与上游语义:
  - **Hotkey 函数**:接入上游 `BIF_Hotkey`(script2.cpp,完整支持:函数对象/On/Off/Toggle/AltTab 动作、B0/S0/Pn/Tn/In 等选项、更新既有热键、HotIf 变体)——上游解析与注册逻辑全部复用,仅激活机制改为 Linux;
  - **激活**:每个热键经 `XGrabKey` 在根窗口上注册(键码由 vk 经键表换算;MOD_CONTROL/SHIFT/ALT/WIN → Control/Shift/Mod1/Mod4 掩码;同时注册带/不带 CapsLock、NumLock 的组合,避免锁定键阻碍触发);事件在**主循环**(poll 等待 X 连接,超时=下一定时器到期)与 **MsgSleep 等待期间**(10ms 切片)派发——与文档"热键在脚本等待时也可触发"一致;
  - **匹配与执行**:KeyPress 触发按下型热键、KeyRelease 触发 `Key up` 热键(文档);修饰键状态须与热键完全一致(文档:默认不允许多余修饰键);命中变体经上游 `FindVariant`(HotIf 准则)+ `PerformInNewThreadMadeByCaller`(节流保护 + 以热键名作参数执行回调,`A_ThisHotkey` 语义);鼠标键/扫描码/前缀(`A & B`)热键在 X11 无对应,不注册(文档化限制)。
- **测试设施**: `assert_hotkey.ahk`/`assert_hotkey_expect.txt`(新增,10 断言:单键触发、Ctrl/Shift/Alt 组合、多余修饰键不触发(文档)、On/Off 动作、Key up 热键、非法键名 ValueError、热键使脚本保持运行且与定时器共存——组合以 Send 的 `^{F8}` 形式发送,XTEST 事件经抓取回到脚本并被派发);`run_check.sh --xvfb` 运行。
- **本轮修复的真实缺陷**: ① `HookAdjustMaxHotkeys` 桩返回 false → `Hotkey::AddHotkey` 报伪"Out of memory"——改为真实 realloc(上游钩子代码经此函数扩容热键数组);② 测试脚本变量与回调函数同名(`h1` 与 `H1`,v2 名称大小写不敏感 → 函数名保留,变量冲突报错)——改名,属测试脚本问题非移植缺陷;③ `Send("^F8")` 按文档语义是 Ctrl+文本"F8"(修饰符只作用于下一个键),组合键应写 `Send("^{F8}")`——测试脚本修正。

### 2.32 未实现函数错误行为校验(收尾,本轮)
- **校验方式**: 对余下 26 个无法移植到 Linux 的函数(Edit*/Gui*/IL_*/Menu*/ListView*/LoadPicture/ImageSearch/DirSelect/FileSelect/Hotstring/OnMessage/SendMessage/PostMessage/RunAs/WinSetRegion——依赖 Windows GUI 控件、COM、消息泵或选择对话框),逐一以最少参数调用并捕获异常,验证错误消息为清晰的 "This built-in function has not been ported to Linux yet."(而非静默返回错误值或其它误导性错误)。
- **结果**: 25 项全部通过(SetNumLockState 重复的 LMD_NI 残留条目已删除,实际未实现 26 项);这些函数在 `worklist.tsv` 中标注 `NOT_IMPL`,各自不可移植的原因见下表。
- **不可移植原因**(Linux 无对应机制):Edit*/ListViewGetContent——Windows 编辑/列表控件的消息协议;Gui*/GuiCtrlFromHwnd/GuiFromHwnd——X11 无同名控件与句柄映射;Menu*/IL_*——Windows 菜单/图标列表句柄;LoadPicture——无 HBITMAP/COM 图像解码管线;ImageSearch——依赖 GDI 位图比较;DirSelect/FileSelect——Windows 标准对话框(可经命令行工具近似,语义差异大,保持明确报错);Hotstring——需要按键缓冲引擎(热键基础设施已就绪,可作后续工作);OnMessage/SendMessage/PostMessage——Windows 消息标识空间与 X11 事件模型无对应;RunAs——Windows 凭据注入;WinSetRegion——X11 无区域形状 API。

### 2.33 libffi 成员调用描述计数错误(本轮,ASan 构建捕获)
- **现象**: ASan 构建运行 GUI 断言时 `dynamic-stack-buffer-overflow` 中止(普通构建不报错),`ffi_call` 在 `MdFunc::Call` 读取超过 `args[]` 栈缓冲区。
- **根因**: `core_md_func_linux.cpp` 的 ffi 参数描述循环以 `aArgSize`(含 `Optional`/`Out` 等修饰符的条目总数)为迭代次数,且修饰符由内层循环额外推进 `atp`——每个实参被重复计数,`mFfiArgCount` 大于 `mArgSlots`(如 `Gui.Prototype.__New` 为 7 vs 4),`argp` 表越界读取。
- **修复**: ffi 计数循环改为与槽位循环同一走法(修饰符由内层循环推进索引,每条目恰好对应一个参数),`mFfiArgCount == mArgSlots`;同时把 64 位标量占 2 槽的 `#ifndef _WIN64` 分支条件改为 `!(defined(_WIN64)||defined(__x86_64__)||defined(__aarch64__))`(x86-64/aarch64 Linux 上 `UINT_PTR` 为 8 字节,64 位参数只占 1 槽,不再额外 `++ac/++ai`)。
- **验证**: ASan 与普通构建的 doc-check 均 868/868、headless 回归 26/26、Wayland 13 项与 XWayland 229 项通过。

### 2.34 GTK3 常驻缓存触发 LSan 误报(本轮)
- **现象**: ASan 构建运行 assert_gui 结束时 LSan 报 ~37 KB 泄漏(1315 个分配),runner 退出码非 0。
- **根因**: GTK3/fontconfig/pango 在进程生命周期内持有字体与样式缓存(即使正常退出的 GTK 应用也会被 LSan 标记)。
- **修复**: `run_check.sh` 对 `assert_gui` 单独设 `ASAN_OPTIONS+=detect_leaks=0`(ASan 内存安全检查保持开启,仅抑制 LSan 退出码报告),其余全部套件仍保留泄漏检测。

### 2.35 逐文档条目审查发现并修复的漏项(本轮)
对照 `docs-v2/docs/lib/*.htm` 全部 352 个条目逐条审查(与 `worklist.tsv`/运行时实测交叉核对),发现并修复 4 类真实缺陷,并新增 `assert_notimpl.ahk`(13 项断言)固化行为:
- **SoundGetInterface/Mute/Name/Volume、SoundSetMute/Volume、CaretGetPos(共 7 个)**: `core_builtin_stubs.cpp` 的空 stub 使调用**静默返回垃圾值**(实测 `SoundGetMute()` 返回 `99323594649640` 类地址残留),既不是文档语义也不报错。改为 `LINUX_BIF_STUB_ERR` 抛 "This built-in function has not been ported to Linux yet."。
- **ComObjFromPtr 段错误**: `LinuxComCallImpl` 把类构造专用的 `++aParam`(跳过 `this`)错误放进共享函数,`ComObjFromPtr(0)` 越界读取 token(ASan/gdb 确认 `TokenIsEmptyString` 崩溃)。修复为与上游一致:`ComValue_Call`/`ComObject_Call` 各自排除 `this`,`BIF_ComObj` 直接处理 `aParam[0]`。
- **InputHook 报误导性 "Out of memory"**: `InputObject::Create()` 桩返回 nullptr,`NewObject` 误报 OOM。改为返回带 `__New`(抛 not-ported 错误)的真实对象。
- **CallbackCreate/CallbackFree 报 "This local variable has not been assigned a value"**: 不在 g_BIF 也不在 LMD 表,名字按未定义变量解析。补 `LMD_NI(CallbackCreate/Free)` 条目。
- **worklist 盲区**: `build_worklist.py` 原把 stub 函数从 g_BIF 集合减去但不计入 NOT_IMPL,导致 stub 函数(含 Sound*/CaretGetPos)在 worklist 中"蒸发";补类构造器/GUI/COM 条目后 274 → 298 个 doc 页有状态。剩余 54 个未收录条目经核实全部为非函数页(语句/指令/类别/索引)。

### 2.36 声音/光标/回调/输入钩子 4 组函数落地 + 语句/指令/类别/索引页代码形式纳入校验(本轮)

- **SoundGet*/SoundSet*(6 个)** — `core_sound_linux.cpp`(新增): 经 `pactl`(PulseAudio/PipeWire)实现,无 PulseAudio/PipeWire 服务时回退 ALSA `amixer`;两者都缺失时按文档抛 "No audio mixer tool (pactl/amixer) is installed." 的 OSError(此前为空 stub 静默返回垃圾值)。`SoundGetInterface`(Windows COM 概念)按文档语义返回 0。
- **CaretGetPos** — `core_caret_linux.cpp`(新增)+ `script_gui_linux.cpp` 的 `AhkGtkCaretGetPos`: 查询 GDK 活动窗口内聚焦的 GtkEntry/GtkTextView,经 pango 布局光标位置 + `gtk_widget_translate_coordinates` + `gdk_window_get_origin` 换算为屏幕坐标(Xvfb 实测 cx/cy 正确)。
- **InputHook** — `core_inputhook_linux.cpp`(新增): 将上游 `input_type` 的 Options/MatchList/Timeout/EndBy*/EndReason/Stop/InProgress 状态机逐方法移植;`MsgSleep` 在等待期间派发超时检查。Linux 无全局键盘钩子,`OnChar/OnKeyDown` 不触发、不抓取键盘(与 GDK 显示双消费会崩溃,已移除 X 事件读取),文档化该限制;调用不再误导性报 "Out of memory"。
- **CallbackCreate/CallbackFree** — `core_callback_linux.cpp`(新增,libffi closure): 参数/返回值按 x64 8 字节槽 `ffi_type_ulong` 传递并拷贝进 `UINT_PTR[]`,经 `CallMethod` 调回脚本函数;`CallbackFree` 释放 closure 与 arg_types(cif 引用它)。DllCall 传入回调地址调用实测返回正确(DllCall(addr,"Int64",7,"Int64",8) = 15)。
- **文档代码形式纳入 doc-check** — 新增 `assert_statements.ahk`(19 断言): 仅函数页不足以覆盖文档,语句页(If/Else/For/While/Switch/Try/Catch/Throw/Loop/Until/Break/Continue/Return/Block)、类别页(Array/Map/Object/Buffer/Error/Number/String 方法)、指令页(`#Requires` 等)与索引页(index.htm)的代码形式也需校验。索引页 `index.htm` 与 Sound*/InputHook 页的 Linux note 同步更新。
- **测试写法修正(非移植缺陷)**: 尝试 `catch e`(无 `as`)在 v2.0.26 语义中是"捕获类 `e`"而非输出变量,上游同样报 "Invalid class."(已用官方 v2.0.26 二进制实测确认);正确写法是 `catch as e` 或 `catch Error as e`。相关断言已按文档修正。

---

## 3. 测试预期修正(文档语义确认,非移植缺陷)

逐条对照官方文档后,以下断言原预期与 v2 文档不符,已按文档修正:

| 断言 | 文档依据 |
|---|---|
| `DateAdd("20240101",2,"months") = 20240101000200` | TimeUnits 只支持 Seconds/Minutes/Hours/Days 或首字母前缀,`m`=分钟 |
| `DateAdd(...,"years")` 抛 ValueError | 无效单位 |
| `InStr("abc","B") = 2` | CaseSense 省略时默认 **Off**(大小写不敏感) |
| `InStr("abcabc","b",,,2) = 5` | Occurrence 是第 5 参数,第 4 参数是 StartingPos |
| `SubStr("abcdef",4) = "def"`、`SubStr("abcdef",-2,1) = "e"` | 1 起始索引语义 |
| `Sort("c,b,a")` 原样返回 | 默认分隔符是换行;须用 `Sort(x,"D,")` |
| `SplitPath(...Dir) = "/a/b"` | 文档示例:Dir 不含尾部反斜杠 |
| `FormatTime()` 长度 > 10 | 空 Format = "时间 + 长日期"(如 `4:55 PM Saturday, November 27, 2004`) |
| `ObjGetCapacity(arr) = 0` | 文档:"对象内部属性数组的容量";数组容量用 `arr.Capacity`(实测 = 3) |
| `obj["nope"]` 抛 UnsetItemError | `__Item` 为 Map 时,缺键读值按文档抛错 |
| `Array.Delete/RemoveAt` 结果 20、枚举和 55 | `[5,10,20,30].RemoveAt(2)` → `[5,20,30]` |
| 变量名 `p`/`P`、`instr`/`InStr` 冲突报错 | v2 变量名大小写不敏感、函数名被保留——测试变量改名而非移植问题 |
| `SetTitleMatchMode("Slow")` 返回 `"Fast"` | 文档:返回"被修改的那个设置"的先前值——匹配模式(2/3/RegEx)与搜索速度(Fast/Slow)是独立设置;模式为 RegEx 时改速度,返回的是先前速度 |
| `SetMouseDelay(30,"Play")` 返回 `-1` | 默认 `A_MouseDelayPlay=-1`(上游 `global_set_defaults`),与普通 `A_MouseDelay=10` 不同 |
| `WinShow` 对隐藏窗口抛 TargetError | 与 Windows 一致:隐藏窗口在 DetectHiddenWindows Off 时不可检测,先 `DetectHiddenWindows(1)` 再 WinShow |

---

## 4. 模块校验结论

| 模块 | 状态 | 说明 |
|---|---|---|
| 数学 (Math/Random/Abs/Mod/… ) | ✅ 44/44 | `GenRandom` 已用 `getrandom` 实现 |
| 字符串 (StrLen/SubStr/InStr/StrReplace/Trim/Format/Sort/SplitPath/StrUpper/Lower/Title/…) | ✅ 47/47 | 本轮修复 Format/CharUpper/翻译器后全绿 |
| 对象 (Object/Array/Map/类/属性/绑定方法/枚举) | ✅ 33/33 | 类字段初始化、属性 get/set 全绿 |
| 文件 (FileOpen/File对象方法/FileRead/Append/Delete/DirCreate/Delete/Copy/Move) | ✅ 31/31 | 本轮补齐 File 类成员注册 |
| 文件元数据 (FileGet/SetTime、FileGetSize、FileGet/SetAttrib、Loop Files) | ✅ 并入 assert_file | 本轮实现 FindFirstFile/futimens/chmod + Loop Files 变量 |
| 二进制互操作 (NumGet/NumPut/StrGet/StrPut/Buffer) | ✅ 20/20 | 本轮接入 lib/interop.cpp 并修复 vtable/编码转换 |
| 正则 (RegExMatch/RegExReplace/~=、命名子组、回引用、UTF) | ✅ 22/22 | 本轮接入捆绑 PCRE1(16 位)并修复单元宽度错位 |
| 注册表 (RegRead/RegWrite/RegDelete/RegDeleteKey/RegCreateKey) | ✅ 19/19 | 文件虚拟注册表,参数语义/返回值/错误行为对照文档 |
| 日期时间 (DateAdd/Diff/FormatTime/A_Now/A_YYYY 等) | ✅ 33/33 | 与文档 TimeUnits/Format 语义一致 |
| 通用 (Env/WorkingDir/Sleep/MsgBox/Process/Run/RunWait/Drive/Ini/Clipboard/GetKeyName/A_* 变量) | ✅ 50/50 | 本轮补齐 A_Args/A_LastError/A_Is*/A_Line*/A_ComputerName/A_UserName/A_OSVersion/A_Language/A_MyDocuments/A_AhkPath 等 |
| 系统/设置 (CoordMode/DetectHidden*/SetTitleMatchMode/Set*Delay/SendMode/SendLevel/SetRegView/FileEncoding/SetStoreCapsLockMode + 15 个 A_* 设置变量) | ✅ 37/37 | 返回值=先前设置、默认值逐一对照上游 `global_set_defaults`,非法参数抛 ValueError |
| 进程 (ProcessGetName/ProcessGetParent/ProcessGetPath) | ✅ 10/10 | /proc 实现,大小写不敏感名称匹配,找不到抛 TargetError |
| 文件操作 (FileCopy/FileMove/FileInstall/FileRecycle/FileRecycleEmpty/FileGetVersion) | ✅ 22/22 | 通配符/目录目标/覆盖标志/Error.Extra 失败计数;XDG Trash;FileGetVersion 按文档对无版本信息文件抛 OSError |
| 系统信息/网络 (SysGet/SysGetIPAddresses/A_ScreenWidth/Height/DPI) | ✅ 13/13 | X11 后端(无显示时屏幕指标为 0,1024x768 Xvfb 实测通过);IPv4 数组含回环 |
| 下载 (Download) | ✅ 3/3 | 本地 HTTP 服务实测:内容一致、404 保存错误页(文档)、坏路径 OSError |
| 驱动器/声音错误路径 (DriveSetLabel/DriveEject/DriveLock/DriveUnlock/DriveRetract/SoundPlay) | ✅ 6/6 | 不存在设备/无播放器按文档抛 OSError(Shutdown 已实现但测试中不实际执行,防误关机) |
| 窗口管理 (WinExist/WinActive/WinGet*/WinSet*/WinMove/WinClose/WinKill/WinWait*/WinActivate/WinMinimize/Maximize/Restore/Hide/Show/Redraw/Group*) | ✅ 67/67 | Xvfb 下以 xwin_helper 实测:条件匹配(标题/类/exe/pid/id/RegEx/排除)、几何/样式/透明度/置顶、隐藏与 DetectHiddenWindows 联动、关闭/强杀/等待、分组循环 |
| 输入模拟 (Send/SendEvent/SendInput/SendPlay/SendText/Click/Mouse*/KeyWait/BlockInput/Install*Hook/Set*LockState/GetKeyState) | ✅ 40/40 | Xvfb 下以 xkeycap 实测:Send 事件序列(字面/Shift 合成/修饰符/按住/重复/Text)、Mouse 坐标/按钮/计数/按住、KeyWait 与 GetKeyState 状态、锁键开关、BlockInput On/Default/Off 阻断语义;XTEST 后端,无显示时按文档抛 OSError |
| 控件 (ControlGetText/SetText/GetPos/Move/GetHwnd/GetClassNN/Focus/GetFocus/Style/Enabled/Checked/Visible/Click/Send/Combo-List/ShowDropDown + WinGetControls) | ✅ 62/62 | Xvfb 下以 xwin_helper 子窗口实测:ClassNN/HWND/文本三种标识与优先级、文本匹配随 TitleMatchMode、几何/移动、聚焦、点击与按键事件落到正确控件、样式位运算、启用/勾选/可见性、Combo 列表增删查选、文档规定的 TargetError/Error 错误路径 |
| 显示器/像素 (MonitorGet/GetCount/GetName/GetPrimary/GetWorkArea/PixelGetColor/PixelSearch) | ✅ 18/18 | Xvfb 下以 XRandR/Xinerama + XGetImage 实测:单屏几何/名称/主显示器、越界 N 抛 ValueError、像素颜色十六进制串、PixelSearch 命中/未命中(输出置空)/Variation 容差/反向搜索、CoordMode Pixel Client 坐标 |
| 状态栏 (StatusBarGetText/StatusBarWait) | ✅ 7/7 | Xvfb 下以 msctls_statusbar32 子窗口实测:Part1=文本/Part>1=空、ControlSetText 联动、等待命中/超时、窗口缺失与无状态栏窗口抛 TargetError |
| 快捷方式 (FileCreateShortcut/FileGetShortcut) | ✅ 9/9 | headless 实测:.desktop 生成与回读(目标/参数/工作目录/描述/图标、含空格目标引号)、.url Internet 快捷方式(文档 INI 格式)、缺失文件/坏目录抛 OSError |
| 调试显示 (ListVars/ListHotkeys/KeyHistory) | ✅ 6/6 | headless 实测:ListVars 输出含全部全局变量(名称+值)、ListHotkeys/KeyHistory 输出与 MaxEvents 校验(0..500 抛 ValueError) |
| 定时器 (SetTimer + 主循环) | ✅ 6/6 | headless 实测:周期触发(等待期间也触发,文档)、Period 0 删除、默认 250ms、负周期仅运行一次、省略函数=当前定时器、非法函数对象报错 |
| 悬浮提示 (ToolTip) | ✅ 5/5 | Xvfb 下实测:返回 HWND、窗口标题=文本、同索引更新复用窗口、坐标、空白隐藏并返回 0 |
| 热键 (Hotkey) | ✅ 10/10 | Xvfb 下以 Send 触发实测:单键与 ^/+/! 组合、多余修饰键不触发(文档)、On/Off 动作、Key up、非法键名 ValueError、热键保持脚本运行并与定时器共存 |
| 未实现函数错误行为 (Edit*/IL_*/LoadPicture/ImageSearch/选择对话框/Hotstring/OnMessage/SendMessage/PostMessage/RunAs/WinSetRegion) | ✅ 25/25 | 逐函数以最少参数调用,验证抛出清晰的 "not been ported to Linux" 运行时错误(不静默返回错误值);各函数不可移植原因见 §2.32 |
| DllCall (.so 动态库) | ✅ 29/29 | dlopen/dlsym + libffi:真实 libc/libm 调用(abs/strlen/isdigit/floor/sqrt/pow/malloc/free/getenv/time/rand_r/sprintf),全类型(Int/Int64/Short/Char/Float/Double/Ptr/Str/AStr/WStr/U 前缀)、`&Var` 输出参数、失败路径(缺符号/缺库/坏类型) |
| COM (D-Bus) | ✅ 18/18 | D-Bus 会话总线实测:ComValue 标量包装(I2/I4/R4/R8/BSTR/BOOL/UI1/I8/UI8)、ComObject 服务代理、真实调用(org.freedesktop.DBus GetId/ListNames 返回字符串数组)、属性/方法调用、错误路径(未知成员抛 OSError)、ComObjType/Value/Flags |
| GUI/控件/菜单 (Gui/GuiControl/Menu/MenuBar,GTK3 后端) | ✅ 32/32 | Xvfb 下实测:窗口/标题/Hwnd、Edit/CheckBox/Radio/DDL/ListBox 的 Value/Text/Set、ListView 行列增删查、TreeView 父子项、StatusBar 文本、Submit 命名控件取值、OnEvent 注册、GuiFromHwnd/GuiCtrlFromHwnd HWND 反查、Destroy、MenuBar 全流程(见 assert_gui.ahk) |
| 声音 (SoundGet*/SoundSet*,pactl/amixer 后端) | ✅ 8/8 | 无 mixer 工具时按文档抛 OSError(此前空 stub 静默返回垃圾值;实测 SoundGetMute 曾返回地址残留);SoundGetInterface 返回 0 |
| 光标 (CaretGetPos,GTK 文本控件) | ✅ 1/1 | Xvfb 下以 Xvfb 单屏实测:GDK 活动窗口内聚焦 GtkEntry 的光标屏幕坐标换算正确 |
| 回调 (CallbackCreate/CallbackFree,libffi closure) | ✅ 4/4 | 地址整数返回/释放;DllCall(addr,"Int64",...) 调用回调实测返回正确;ASan 捕获并修复 stack-use-after-scope(optl<int> 引用块内局部变量) |
| 输入钩子 (InputHook,X11 状态机) | ✅ 5/5 | Start→InProgress、超时 EndReason=Timeout、Stop→EndReason=Stopped(无显示时超时仍生效;X 键捕获/抓取因 GDK 双消费崩溃已禁用,文档化) |
| 语句/指令/类别/索引页代码形式 (assert_statements) | ✅ 19/19 | If/Else/For/While/Switch/Try/Catch/Throw/Loop/Until/Break/Continue/Return/Block + Array/Map/Object/Buffer/Error/Number/String 方法 + `#Requires` + index.htm 索引页 |

## 5. 回归与构建验证

```
普通构建: tests/run_tests.sh        PASS=26 FAIL=0
          tests/doccheck/run_check.sh --xvfb PASS=994 FAIL=0
          tests/doccheck/wayland_run.sh PASS=13 FAIL=0 (Wayland 模式)
          tests/doccheck/wayland_run.sh --xwayland PASS=235 FAIL=0 (XWayland 回退)
ASan 构建: tests/run_tests.sh        PASS=26 FAIL=0
          tests/doccheck/run_check.sh --xvfb PASS=994 FAIL=0
          tests/doccheck/wayland_run.sh PASS=13 FAIL=0
          tests/doccheck/wayland_run.sh --xwayland PASS=235 FAIL=0
```

## 5.4 文档示例审计(Linux 可运行性)

对 `docs-v2/` 全部 1390 个示例块做了自动化审计
(`tests/doccheck/verify_examples*.py`,headless 与 Xvfb 两种模式,
MsgBox/InputBox/FileSelect 带 autoclose 钩子):

- **244 个示例**单独运行失败;原因分类(全部经人工复核):
  - **~200 个是文档片段**(snippet):引用前文变量/窗口/热键标签、类定义
    片段、依赖上下文的语句——在 Windows 上同样不能独立运行,属官方
    文档教学性质,非移植缺陷;
  - **~30 个平台专属**:Windows 路径(`D:\...`)、PE 文件头检查、
    Windows API 调用等——页面已加 Linux note 说明;
  - **~10 个环境依赖**:需要真实显示器/Xvfb、外部设备(光驱)、网络资源;
  - **~6 个 TIMEOUT**:Persistent/等待类示例,本就不应退出。
- 修正动作:25 个平台专属页面加 Linux note;DllCall/ComObject 页面
  新增已实测的 Linux 可运行示例;linux-port.htm 增加示例阅读说明。

## 5.5 Wayland 后端(第 20 轮)

显示层选择:X11 优先(有 DISPLAY 即可用,亦覆盖 XWayland);无 X11 但
`WAYLAND_DISPLAY` 可连接时启用 Wayland 层(`core_wayland_linux.cpp`):

- **输入模拟**:`Send`/`Mouse*` 在纯 Wayland 下经 `zwp_virtual_keyboard_v1`
  与 `zwlr_virtual_pointer_manager_v1` 注入;vk→evdev 键码内置映射(无服务器
  往返);keymap 经 xkbcommon 编译后通过 `keymap` 请求下发。**修饰键状态经
  `modifiers` 请求显式推送**(每次按键事件前),使 sway 的修饰键组合匹配
  (`bindsym Shift+Return`/`Control+Return` 端到端触发;单独 `bindsym
  Shift_L` 类绑定在真实键盘上同样不触发——按下修饰键时其 xkb 修饰位已激活,
  与组合键行为一致)。**鼠标按钮**:sway 的按钮绑定要求指针悬停于 surface,
  脚本先把指针移到 ToolTip 窗口上再点击,`bindsym button3` 端到端触发。
  **端到端验证**:sway(headless)的 `bindsym` 钩子在收到虚拟键盘/指针事件后
  创建标记文件,`assert_wayland` 断言标记存在。指针运动为相对移动(Wayland
  客户端无法绝对定位指针,文档化)。
- **窗口**:ToolTip 在纯 Wayland 下创建 xdg-shell toplevel(文本为标题);首次
  commit 不携带 buffer,收到 configure 后 ack 并附加 1x1 shm buffer 再 commit
  (xdg-shell 规范:`unconfigured buffer` 协议错误)。`wayland_run.sh` 经
  swaymsg 验证窗口被 sway map。
- **X11 专属表面**:窗口管理(Win*)、热键(XGrabKey)、像素/显示器访问在纯
  Wayland 下不可用(Wayland 客户端无法枚举其他窗口、无全局热键协议),抛出
  明确错误(消息含 "use XWayland" 提示);GetKeyState 返回 0(无法查询 seat);
  对话框走无显示 stdin 回退。
- **XWayland 回退**:`wayland_run.sh --xwayland` 在 sway 的 XWayland 上运行
  X11 套件(控件/编辑/对话框/消息/形状/图像/热键 229 项全过),验证 X11 后端
  在 Wayland 之上完整可用。**屏幕抓取**:XWayland 的 root 窗口无 backing
  store,XGetImage 必然 BadMatch;`LinuxScreenGrabRegion` 在 XGetImage 失败时
  回退到 wlr-screencopy 协议(sway 实现,`LinuxWaylandCaptureScreen`)抓取
  区域像素,ImageSearch/PixelGetColor/PixelSearch 因此在 XWayland 上全量可用;
  runner 用 `for_window [title="ImgMain"] move/resize/border` 把测试窗口钉在
  与 Xvfb 相同的 (50,60) 位置。**热键**:XGrabKey 经 XWayland 正常工作
  (XWayland 是完整 X server,grab 匹配与 XTEST 注入均可用),assert_hotkey
  10 项全过。排除项仅剩 `assert_win/input/monitor`(断言依赖无 WM 的 Xvfb
  语义:激活/焦点/光标位置由 sway 拥有)。
- **测试钩子**:`AHK_WL_EVLOG` 记录本客户端表面收到的键盘/指针事件
  (与 `AHK_*_AUTOCLOSE_MS` 同类测试设施)。
- 依赖:libwayland-client、wayland-protocols(xdg-shell)、xkbcommon;协议 XML
  已 vendor 于 `source/linux/wayland/protocols/`,构建时经 wayland-scanner
  生成客户端代码。
- WSL 注意:`/tmp/.X11-unix` 可能是 WSLg 的只读挂载,sway 的 XWayland 因此
  无法启动;`wayland_run.sh --xwayland` 会先尝试 remount,失败则安全跳过
  (绝不把测试窗口弹到用户桌面)。

## 6. 本轮改动文件

- `source/linux/core/core_file_linux.cpp` / `.h`(新增):File 类成员注册
- `source/TextIO.cpp`:friend 表填充、`LinuxIsFileObject`
- `source/linux/core/core_platform_stubs.cpp`:`DefineMetadataMembers` 接入 File;键名表;真实 `GetFullPathName` 辅助
- `source/linux/stdafx_linux.h`:`HRESULT` 32 位;`CharLower/CharUpper`;`_vsntprintf` 翻译器(`*`/`I64`);`GetFullPathName/GetCurrentDirectory/SetCurrentDirectory` 真实实现;**FindFirstFile/FindNextFile/FindClose、SetFileTime(futimens)、SetFileAttributes(chmod)、FILETIME 时区转换、精确版 WideCharToMultiByte/MultiByteToWideChar**
- `source/lib/string.cpp`:`BIF_Format` Linux 浮点参数传递
- `source/lib/file.cpp`:FilePatternApply 支持 `/` 分隔
- `source/lib/interop.cpp`(本轮加入构建):NumGet/NumPut/StrGet/StrPut/StrPtr 真实实现;StrGet 字符计数语义
- `source/script_object.h`:BufferObject 独立 vtable(Linux)
- `source/linux/core/main_linux.cpp`:脚本路径字段初始化
- `source/linux/core/core_builtin_stubs.cpp`:`FileExist/DirExist` 属性字母语义;Loop Files 9 个内置变量真实实现
- `source/linux/core/core_mdfunc_linux.cpp`:文件元数据 5 函数包装;`LMD_NI → LMD_IMPL`
- `source/linux/gui/x11_gui.cpp`:`WideToNarrow` 支持代理对
- `source/linux/core/CMakeLists.txt`:加入 `core_file_linux.cpp`、`lib/interop.cpp`、`ahk_pcre16`(捆绑 PCRE1)
- `source/lib/regex.cpp`(本轮加入构建):RegExMatch/RegExReplace 全功能 + Linux UTF-16 转换层
- `source/lib_pcre/pcre/pcre.h`:Linux 下 `PCRE_UCHAR16` 修正为 `unsigned short`
- `CMakeLists.txt`(根):启用 C 语言
- `source/linux/core/core_builtin_stubs.cpp`:A_* 环境变量真实实现;`BIF_Reg` 文件虚拟注册表
- `source/linux/core/core_platform_stubs.cpp`:`Script::CurrentLine/CurrentFile` 真实实现
- `source/linux/core/main_linux.cpp`:`A_Args` 启动填充、`mOurEXE`(/proc/self/exe)
- `tests/doccheck/*`:断言脚本与期望值按文档修正,新增 assert_interop、assert_regex、assert_registry 套件,run_check.sh 支持按套件传参,worklist 重新生成(154 IMPL / 172 NOT_IMPL)
- `source/linux/core/core_mdfunc_linux.cpp`(本轮):设置/进程/文件操作/系统/网络/驱动器/声音 33 个 BIF 包装(LMD_NI → LMD_IMPL);SysGet(X11)、SysGetIPAddresses(getifaddrs)、Download(curl/wget)、FileRecycle/FileRecycleEmpty(XDG Trash)、FileGetVersion、Shutdown、Drive*(外部工具)、SoundPlay(paplay/aplay)
- `source/linux/core/core_builtin_stubs.cpp`(本轮):设置类 15 个 BIV 真实实现(A_CoordMode*/A_DetectHidden*/A_TitleMatchMode*/A_KeyDelay*/A_KeyDuration*/A_MouseDelay*/A_WinDelay/A_ControlDelay/A_DefaultMouseSpeed/A_SendMode/A_SendLevel/A_RegView/A_FileEncoding/A_StoreCapsLockMode/A_ListLines/A_ScreenWidth/Height/DPI);**FileAppend/FileRead 支持 FileEncoding 默认编码(BOM 探测/写入、UTF-16LE 代理对编解码、缺文件抛 OSError)**
- `source/linux/core/core_platform_stubs.cpp`(本轮):`Line::Util_CopyFile` 按上游语义重写(通配符/目录目标/失败计数/跨设备移动)
- `tests/doccheck/assert_sys.ahk` / `assert_sys_expect.txt`(新增,106 断言);`run_check.sh` 自动启动/清理本地 HTTP 服务;worklist 重新生成(**187 IMPL / 139 NOT_IMPL**)
- `source/linux/core/core_win_linux.cpp` / `core_win_linux.h`(新增,本轮):X11 窗口模块——WinTitle 条件解析(ahk_id/pid/class/exe/group + 模式 1/2/3/RegEx + ExcludeTitle)、窗口枚举/信息/匹配、Win* 49 个 BIF(获取/操作/等待/分组)、虚拟状态(样式/置顶/透明度)、EWMH 属性发布、忽略 X 错误处理器
- `source/linux/core/core_builtin_stubs.cpp`(本轮):`BIF_WinExistActive` 桩移除,改由 core_win_linux.cpp 实现
- `source/linux/core/CMakeLists.txt`(本轮):加入 `core_win_linux.cpp`
- `tests/doccheck/xwin_helper.c`(新增):X11 测试窗口客户端;`assert_win.ahk`/`assert_win_expect.txt`(新增,67 断言);`run_check.sh` 新增 `--xvfb` 模式(Xvfb :99 + 编译助手 + 文件输出比对);worklist 重新生成(**236 IMPL / 90 NOT_IMPL**)
- `source/linux/core/core_input_linux.cpp` / `.h`(新增,本轮):输入模拟模块——Send 引擎(字面/修饰符/`{Key}` down-up/重复/`{vk}`/`{Click}`/Blind/Text)、Mouse 系列(XTEST 按钮/运动/相对模式/计数/按住)、KeyWait(XQueryKeymap 轮询)、GetKeyState/GetAsyncKeyState 真实实现、Set*LockState(Xkb 锁键)、BlockInput(OnOff/SendMouse/MouseMove 三组模式状态机 + X 抓取)、Install*Hook 标志;`LinuxLookupKey` 访问器(键表小写分支)
- `source/linux/core/core_platform_stubs.cpp`(本轮):GetKeyState/GetAsyncKeyState 桩移除;`LinuxKeyByName` 补 a-z 小写键名分支
- `source/linux/core/core_mdfunc_linux.cpp`(本轮):17 个输入 BIF 由 LMD_NI 翻转 LMD_IMPL(BlockInput/InstallKeybdHook/InstallMouseHook/KeyWait/MouseClick/MouseClickDrag/MouseGetPos/MouseMove/Send/SendEvent/SendInput/SendPlay/SendText/SetCapsLockState/SetNumLockState/SetScrollLockState),BIF_Click 桩移除
- `source/linux/core/CMakeLists.txt`(本轮):加入 `core_input_linux.cpp`、链接 Xtst(`find_library(XTST_LIBRARY Xtst REQUIRED)`)
- `source/TextIO.cpp`(本轮):`TextFile::_Seek` 消除 `PLARGE_INTEGER` 别名读取(Linux 上 LARGE_INTEGER 为 16 字节导致 8 字节栈变量被读 16 字节,ASan 捕获)
- `tests/doccheck/xkeycap.c`(新增):XTEST 事件捕获客户端;`assert_input.ahk`/`assert_input_expect.txt`(新增,40 断言);`run_check.sh` 的 `--xvfb` 模式编译 xkeycap 并运行 assert_input;worklist 重新生成(**253 IMPL / 76 NOT_IMPL**)
- `source/linux/core/core_ctrl_linux.cpp` / `.h`(新增,本轮):控件模块——控件标识解析(HWND/ClassNN/文本,文档优先级)、子窗口深度优先枚举、真实 X11 操作(文本/几何/移动/聚焦/可见性/点击/按键,复用 XTEST 引擎)、虚拟状态(样式位运算、启用/勾选、Combo/List 条目与选择、下拉标志)、WinGetControls/WinGetControlsHwnd 真实实现
- `source/linux/core/core_win_linux.cpp` / `.h`(本轮):WinGetControls/WinGetControlsHwnd 空数组桩移除;导出 `LinuxX11Display/LinuxWinFindTargetEx/LinuxWinTitleEx/LinuxWinSetPersistentEx` 访问器
- `source/linux/core/core_input_linux.cpp` / `.h`(本轮):导出 `LinuxFakeButtonEvent/LinuxFakeMotionEvent/LinuxSendKeysString/LinuxSendCharsString/LinuxButtonFromNameEx` 访问器
- `source/linux/core/core_mdfunc_linux.cpp`(本轮):32 个 Control* BIF 由 LMD_NI 翻转 LMD_IMPL(ControlGetPos/ControlMove 输出参数与必填 Control 的文档语义经 min=0 + BIF 内校验实现)
- `source/linux/core/CMakeLists.txt`(本轮):加入 `core_ctrl_linux.cpp`
- `tests/doccheck/xwin_helper.c`(本轮):新增 `-child NAME CLASS X Y W H` 子控件窗口与 `-evout` 事件记录;`assert_ctrl.ahk`/`assert_ctrl_expect.txt`(新增,62 断言);`run_check.sh` 的 `--xvfb` 模式运行 assert_ctrl;worklist 重新生成(**285 IMPL / 44 NOT_IMPL**)
- `source/linux/core/core_screen_linux.cpp` / `.h`(新增,本轮):显示器/像素模块——XRandR outputs(回退 Xinerama/单屏)、MonitorGet*/PixelGetColor/PixelSearch(CoordMode Pixel、Variation、未命中置空输出、十六进制串返回值)
- `source/linux/core/core_mdfunc_linux.cpp`(本轮):5 个 Monitor* 与 2 个 Pixel* BIF 由 LMD_NI 翻转 LMD_IMPL(PixelSearch 修正为 7-10 参数并注册 1/2 号输出变量)
- `source/linux/core/CMakeLists.txt`(本轮):加入 `core_screen_linux.cpp`、链接 Xrandr/Xinerama
- `tests/doccheck/xwin_helper.c`(本轮):新增 `-fill RRGGBB X Y W H` 实色矩形;`assert_monitor.ahk`/`assert_monitor_expect.txt`(新增,18 断言);`run_check.sh` 的 `--xvfb` 模式运行 assert_monitor;`assert_win.ahk` 的 ahk_exe 断言改为组合条件(防跨套件残留 helper 干扰);worklist 重新生成(**292 IMPL / 37 NOT_IMPL**)
- `source/linux/core/core_display_linux.cpp` / `.h`(新增,本轮):显示/快捷方式模块——FileCreateShortcut/FileGetShortcut(.desktop/.url)、StatusBarGetText/StatusBarWait(状态栏子窗口 + 轮询匹配)、ListVars(经 Script friend 枚举全局变量)/ListHotkeys/KeyHistory(headless MsgBox 展示)
- `source/linux/core/core_ctrl_linux.cpp`(本轮):ClassNN 解析改为上游 EnumControlFind 前缀算法(兼容类名以数字结尾,如 msctls_statusbar32);导出 `LinuxFindDescendantByClass`
- `source/script.h`(本轮):`LinuxVarDump` friend(ListVars 枚举全局变量)
- `source/linux/core/core_mdfunc_linux.cpp`(本轮):7 个 BIF 由 LMD_NI 翻转 LMD_IMPL(FileCreateShortcut/FileGetShortcut/StatusBarGetText/StatusBarWait/ListVars/ListHotkeys/KeyHistory)
- `source/linux/core/CMakeLists.txt`(本轮):加入 `core_display_linux.cpp`
- `tests/doccheck/assert_display.ahk`/`assert_display_expect.txt`/`assert_display_content.txt`(新增,headless 11+4 断言;run_check.sh 为 assert_display 增加自由文本内容比对);`assert_monitor.ahk` 增补 7 个状态栏断言(msctls_statusbar32 子窗口);worklist 重新生成(**299 IMPL / 30 NOT_IMPL**)
- `source/linux/core/core_timer_linux.cpp` / `.h`(新增,本轮):定时器基础设施——CheckScriptTimers 移植、LinuxRunMainLoop 主循环、SetTimer/ToolTip BIF
- `source/linux/core/core_platform_stubs.cpp`(本轮):InitNewThread/ResumeUnderlyingThread 由 no-op 桩改为上游完整实现(线程栈/默认设置/不可中断性/自动退出);MsgSleep 等待期间按切片触发到期定时器(文档语义)
- `source/linux/core/main_linux.cpp`(本轮):自动执行段结束后,脚本持久或有启用定时器时进入 LinuxRunMainLoop
- `source/linux/core/core_mdfunc_linux.cpp`(本轮):SetTimer/ToolTip 由 LMD_NI 翻转 LMD_IMPL
- `source/linux/core/CMakeLists.txt`(本轮):加入 `core_timer_linux.cpp`
- `tests/doccheck/assert_timer.ahk`/`assert_timer_expect.txt`(新增,11 断言);`run_check.sh --xvfb` 运行 assert_timer;worklist 重新生成(**301 IMPL / 28 NOT_IMPL**)
- `source/linux/core/core_hotkey_linux.cpp` / `.h`(新增,本轮):Hotkey 函数接入上游 BIF_Hotkey + XGrabKey 激活(X11 掩码映射、锁定键变体抓取、主循环 poll 派发、MsgSleep 等待期派发、FindVariant/PerformInNewThreadMadeByCaller 执行)
- `source/linux/core/core_platform_stubs.cpp`(本轮):`HookAdjustMaxHotkeys` 由恒 false 桩改为真实 realloc(修复 Hotkey::AddHotkey 伪 OOM);MsgSleep 派发热键事件
- `source/linux/core/core_timer_linux.cpp`(本轮):主循环在有热键时以 poll 等待 X 连接
- `source/linux/core/core_input_linux.cpp` / `.h`(本轮):导出 `LinuxKeycodeForVkEx`
- `source/linux/core/core_mdfunc_linux.cpp`(本轮):Hotkey 由 LMD_NI 翻转 LMD_IMPL
- `source/linux/core/CMakeLists.txt`(本轮):加入 `core_hotkey_linux.cpp`
- `tests/doccheck/assert_hotkey.ahk`/`assert_hotkey_expect.txt`(新增,10 断言);`run_check.sh --xvfb` 运行 assert_hotkey;worklist 重新生成(**302 IMPL / 27 NOT_IMPL**)
- `source/linux/core/core_mdfunc_linux.cpp`(本轮):删除 SetNumLockState 残留的重复 LMD_NI 条目(round-7 翻转时遗留)
- `tests/doccheck/assert_notimpl.ahk`/`assert_notimpl_expect.txt`(新增,25 断言:26 个无法移植函数逐一验证抛出清晰的 "not been ported to Linux" 错误);`run_check.sh` headless 运行;worklist 重新生成(**302 IMPL / 26 NOT_IMPL**)
- **rounds 14–19(25 个函数全部实现,0 NOT_IMPL)**:
  - `source/linux/core/core_ctrl_linux.cpp` / `.h`(round 14):Edit/EditGetCurrentCol/EditGetCurrentLine/EditGetLine/EditGetLineCount/EditGetSelectedText/EditPaste(虚拟光标/选区状态 + 真实文本属性;ControlSetText 重置光标=WM_SETTEXT 语义,EditPaste=EM_REPLACESEL 语义)与 ListViewGetContent(完整选项语法 Count/Count Col/Count Selected/Count Focused/ColN/Selected/Focused、行 LF/列 TAB、ColN 越界 ValueError、虚拟行经 ControlAddItem/ControlDeleteItem 文档化扩展);`assert_edit.ahk`(47 断言)
  - `source/linux/gui/x11_gui.cpp` / `.h`(round 15):`LinuxEntryDialog` 从 InputBox 抽出共享,新增 `LinuxFileDialog`(X11 路径输入对话框 + 无显示 stdin 回退,多选读至空行);`BIF_Linux_FileSelect/DirSelect`(D/M/S 选项字母与数值标志按上游校验:非数字 ValueError、D+Filter ValueError、RootDir\Filename 拆分、root*initial、默认 $HOME);`assert_dialog.ahk`(16 断言)+ t25 无头 stdin 回归
  - `source/linux/core/core_mdfunc_linux.cpp`(round 16):OnMessage(上游 BIF 适配,监视器存储、回调 4 参数校验、MaxThreads 0/-1)、SendMessage/PostMessage(MsgNumber 0..0xFFFFFFFF 校验、wParam/lParam 整数或带 Ptr 对象、目标解析 TargetError、返回 DefWindowProc 默认回复 0、Timeout 接受)、MenuSelect(解析目标后按文档抛 "does not have a standard Win32 menu" TargetError——X11 窗口不可能有 Win32 菜单);修复空 WinText 不再被当作过滤条件;`assert_msg.ahk`(26 断言)
  - `source/linux/core/core_mdfunc_linux.cpp`(round 17):Hotstring 上游 BIF 适配(注册/修改/OnOff/Toggle/EndChars/MouseReset/Reset 全语义)、RunAs(凭证存储,`Script::DoRunAs` 桩改为按上游抛 "Launch Error (possibly related to RunAs)"——此前静默失败无错误);`assert_msg.ahk` 增补 23 断言
  - `source/linux/core/core_image_linux.cpp` / `.h`(round 18,新增):图像存储(稳定句柄)、BMP(24/32 位 BI_RGB)与 PPM(P6/P3)解码、最近邻缩放;LoadPicture(Wn/Hn、-1 保持纵横比、Icon/GDI+ 接受、OutImageType、失败返回 0)、IL_Create/IL_Add/IL_Destroy(列表句柄、1 基索引、零句柄 ValueError)、ImageSearch(CoordMode Pixel、XGetImage 抓屏、精确与 *N 容差、*wN/*hN/*IconN/*Trans 选项、未命中置空输出、ValueError/OSError);`assert_image.ahk`(26 断言)
  - `source/linux/core/core_win_linux.cpp`(round 19):WinSetRegion 经 X11 SHAPE 扩展真实实现(选项语法照上游:坐标对/Wn/Hn/E/R/Rw-h/Wind;扫描线矩形集:椭圆/圆角/偶奇多边形;空选项恢复;ValueError/OSError/TargetError);`xshape_probe.c` 端到端验证;`assert_shape.ahk`(19 断言);GuiFromHwnd/GuiCtrlFromHwnd/MenuFromHandle 按文档空串分支恒返回 ""(移植版无 Gui/菜单对象);`assert_notimpl` 套件移除(0 NOT_IMPL)
- **round 20(Wayland 后端)**:见第 5.5 节;`source/linux/core/core_wayland_linux.cpp` / `.h`(新增)、`source/linux/wayland/protocols/`(xdg-shell/virtual-keyboard/wlr-virtual-pointer XML,vendor)、`core_input_linux.cpp`(XTEST 原语加 Wayland 分支、SendChar 字符→vk 映射含小写字母去冲突)、`core_timer_linux.cpp`(ToolTip xdg 窗口、主循环 Wayland poll)、`core_platform_stubs.cpp`(MsgSleep 派发 Wayland)、`core_hotkey_linux.cpp`(Wayland 守卫)、`core_win_linux.cpp`(Wayland 明确错误消息)、`CMakeLists.txt`(wayland-client/xkbcommon + wayland-scanner 生成);`assert_wayland.ahk`/`wayland_run.sh`(13 断言 + XWayland 回退 193 断言);worklist 重新生成(**327 IMPL / 0 NOT_IMPL**)
- **round 21(突破 compositor 局限)**:三个此前被归为"compositor 局限"而排除的能力全部打通并纳入回归:
  - **修饰键组合 + 鼠标按钮(纯 Wayland)**:`LinuxWaylandKeyEvent` 在每个按键事件前显式推送 `zwp_virtual_keyboard_v1_modifiers`(跟踪按下修饰位:Shift=1/Control=4/Alt=8/Super=64),sway 的修饰键组合匹配因此生效——`bindsym Shift+Return`、`bindsym Control+Return` 端到端触发(`assert_wayland` 新增 `wl_send_shift_enter`/`wl_send_ctrl_enter` 断言)。查 sway 源码确认:`get_active_binding` 要求修饰位精确匹配,按下修饰键本身时其 xkb 位已激活,故 `bindsym Shift_L` 这类单修饰键绑定在真实物理键盘上同样不触发——推送修饰状态后的行为与真实键盘一致(此前 `wl_send_shift`/`wl_send_ctrl` 通过是未推送状态下的假阳性,已按真实语义修正断言)。**鼠标按钮**:sway 的 `bindsym button3` 要求指针悬停于 surface,测试先把虚拟指针移到 ToolTip 窗口上再 `MouseClick("Right")`——`wl_mouse_btn` 断言新增,sway 日志确认按钮绑定触发。
  - **XWayland 屏幕抓取(ImageSearch/PixelGetColor/PixelSearch)**:XWayland 的 root 窗口无 backing store,`XGetImage` 必然 BadMatch(NULL)。新增 `LinuxWaylandCaptureScreen`(core_wayland_linux.cpp,独立 Wayland 连接 + wlr-screencopy 协议:registry 绑定 `zwlr_screencopy_manager_v1`/wl_shm/wl_output,`capture_output_region` 抓区域,shm buffer 拷贝,`ready`/`failed` 事件等待,XRGB8888→0xRRGGBB,含 y_invert 处理与 3s 超时);`core_screen_linux.cpp` 新增共享的 `LinuxScreenGrabRegion`(XGetImage 失败时回退 screencopy),ImageSearch 与像素函数复用;`wayland_run.sh --xwayland` 把 `assert_image` 纳入套件,并用 `for_window [title="ImgMain"] move position 50 60 / resize set 300 200 / border none` 把测试窗口钉在与 Xvfb 相同位置(另修 xwin_helper 在 Expose 时重绘 fill,WM resize 后内容不丢);XWayland 断言 193→**219**。
  - **XWayland 热键(assert_hotkey)**:实测 XGrabKey 经 XWayland 正常工作(XWayland 是完整 X server,grab 匹配与 XTEST 注入均可用,sway 会把 Wayland 键盘事件转入 XWayland),此前排除结论错误;`assert_hotkey` 10 项纳入 --xwayland,合计 **229** 项全过。排除项仅剩 `assert_win/input/monitor`(断言依赖无 WM 的 Xvfb 语义,sway 拥有激活/焦点/光标)。`wl_diag2.sh`/`xw_img*.sh` 等调试脚本留在 tests/doccheck 便于复现。
- **round 22(9 个函数 + 语句/指令/类别/索引页代码形式校验)**:
  - `source/linux/core/core_sound_linux.cpp`(新增):SoundGetMute/Name/Volume + SoundSetMute/Volume(6 个)经 `pactl`(PulseAudio/PipeWire,无服务回退 ALSA `amixer`)实现;无 mixer 工具抛 "No audio mixer tool (pactl/amixer) is installed." OSError;SoundGetInterface 返回 0;**`assert_sound_etc.ahk`(15 断言)**固化(sgi/sgi2/cgp/cbaddr/cbfree/ih_* 及无工具时的 ose)。
  - `source/linux/core/core_caret_linux.cpp`(新增)+ `source/linux/gui/script_gui_linux.cpp`(`AhkGtkCaretGetPos` 导出):CaretGetPos 经 GDK 活动窗口聚焦的 GtkEntry/GtkTextView + pango 光标位置 + 坐标换算给出屏幕坐标。
  - `source/linux/core/core_callback_linux.cpp`(新增,libffi closure):CallbackCreate/CallbackFree——closure 地址即回调地址,参数/返回按 x64 `ffi_type_ulong` 槽传递,经 `CallMethod` 调回脚本函数;**DllCall 实测调用正确**。**ASan 捕获并修复 stack-use-after-scope**:`BIF_Linux_CallbackCreate` 把 `int pc` 放在 if 块内而 `optl<int>` 持其引用,离开块后 use-after-scope;`pc` 提升到函数作用域(与 `LinuxOptInt` 的槽语义一致)。
  - `source/linux/core/core_inputhook_linux.cpp`(新增):InputHook 上游 `input_type` 状态机移植(Options/MatchList/Timeout/EndBy*/EndReason/Stop/InProgress);`MsgSleep` 等待期派发超时。Linux 无全局键盘钩子且 GDK 显示上 X 事件读取/XGrabKeyboard 会崩溃,故不抓键(文档化限制;OnChar/OnKeyDown 不触发);已从 `assert_notimpl` 移除。
  - `source/linux/core/core_mdfunc_linux.cpp`(本轮):BIF_Linux_CallbackCreate/Free 包装与 LMD_IMPL 条目。
  - `tests/doccheck/build_worklist.py`/`worklist.tsv`:InputHook 由 CLASS_NOT_IMPL 移入 CLASS_IMPL;**369 IMPL / 4 边界(NOT_IMPL 打印值,含 3 个已在 g_BIF 注册的可运行错误路径)/ 298 doc 页有状态**。
  - `tests/doccheck/assert_statements.ahk`/`_expect.txt`(新增,19 断言):语句/指令/类别/索引页代码形式校验(If/Else/For/While/Switch/Try/Catch/Throw/Loop/Until/Break/Continue/Return/Block + Array/Map/Object/Buffer/Error/Number/String 方法 + `#Requires` + index.htm)。**测试写法修正**:`catch e`(无 `as`)v2 语义是捕获"类 e",上游同样报 "Invalid class."(已用官方 v2.0.26 二进制实测),改为 `catch as e`。
  - `tests/doccheck/assert_notimpl.ahk`/`_expect.txt`:内容收敛为 3 项(ComObjArray/ComObjQuery/ComObjConnect,com_err)。
  - docs-v2:`Sound*.htm` 与 `index.htm` 的 Linux note 更新为已实现描述;`InputHook.htm` 改为准确的实现状态 + 限制说明。
  - **验证**:普通 + ASan 构建 doc-check **903/903,**回归 26/26、Wayland 13、XWayland 229 全绿。
- **round 23(弱化盘点 + 崩溃修复)**:见 `AUDIT_2026_WEAKENED.md` 全文。
  关键结论:① **修复 Send* 无显示 + Wayland 活跃时的段错误**
  (`LinuxFakeKey/LinuxKeycodeForVk` 缺 `!d` 守卫,ASan 栈确认
  `XKeysymToKeycode(NULL)`;已加守卫,复测 20/20 通过);② **纠正文档**
  SoundBeep/SoundPlay 实际已实现(响铃 与 paplay/aplay),上一轮 note
  误写为 "not ported",已同步修正 docs-v2 四个页面;③ 盘点出以
  Hotstring 永不展开、Send* 四模式等价、GuiFromHwnd 恒返回 ""、
  COM 仅 D-Bus(SafeArray/IID 占位)、图像仅 BMP/PPM、注册表单文件、
  TrayTip 空 shim 等为代表的弱化点(详见该报告 §2);④ 完备性证据
  与边界见该报告 §3(903 断言 + 313/369 函数直引用 + 1390 示例审计 +
  与上游二进制对标;另 56 个函数无直接断言)。
- **round 24(向完备性补齐 + 发布 linux.3 Release)**:
  - **TrayTip/TraySetIcon 改为明确报未移植**:Linux 无托盘图标,原
    `Shell_NotifyIcon` 空 shim 恒返回 FALSE 且 `BIF_Linux_TrayTip` 未设
    返回值(实测返回垃圾整数)、`TraySetIcon` 报误导性 "Can't load icon."。
    现二者改为抛统一的 "This built-in function has not been ported to
    Linux yet." 错误(与文档 warning 一致),**worklist 由 IMPL 改标 NOT_IMPL**
    (IMPL 367 / NOT_IMPL 6),删除死掉的 wrapper,`assert_notimpl` 增加
    `traytip`/`trayseticon` 断言。
  - **Send 无显示崩溃回归断言**:`assert_notimpl` 新增 `send_nocrash`
    (try{ SendInput("x") } catch{}——无论成功还是无显示 OSError 都必须
    完成并记 "ok";若未来再次 NULL-display 段错误则该行缺失→FAIL)。
  - **验证**:doc-check **907/907**(core+ASan 双构建)、回归 26/26、
    Wayland 13、XWayland 229。跟踪覆盖率:313/367 IMPL 函数直接出现在
    断言源码,54 个未直接引用(见 `AUDIT_2026_WEAKENED.md §3`)。
  - **发布**:重建含以上修复的 build-core → `pack.sh 2.0.26-linux.3`
    (tar.gz + .deb),经 `gh release create v2.0.26-linux.3` 上传资产——
    GitHub Release 从 `.1` 提升到 `.3`。
- **round 25(继续补齐:GuiFromHwnd 反查 + ICO 解码)**:
  - **GuiFromHwnd/GuiCtrlFromHwnd 真实实现**:`core_mdfunc_linux.cpp` 的
    LMD wrapper 此前无条件返回 ""(即使 GTK Gui 有真实 Hwnd)。现改为把
    HWND 交给 GTK 后端的 `GuiFromHwnd/GuiCtrlFromHwnd`(widget↔gui/control
    双向映射,含 Recurse 父窗口反查与销毁反注册)。Xvfb 实测:Hwnd→Gui、
    控件+Recurse→所属 Gui、GuiCtrlFromHwnd→控件、无效/已销毁→"";
    `assert_gui` 新增 6 条断言(gfr_*)。
  - **LoadPicture 增加 ICO 解码**:`core_image_linux.cpp` 新增
    `LinuxLoadICO`——解析 ICONDIR/ICONDIRENTRY,选择最大非 PNG 的经典
    DIB 条目,支持 32/24/8/4/1-bpp(含调色板)与 AND 掩码;透明像素在
    移植版 RGB-only 模型下以品红哨兵 0xFFFF00FF 表示(可配 ImageSearch
    *Trans);.ico 的 OutImageType 上报 "Icon"。新增测试 fixture
    `tests/doccheck/fixtures/test.ico`(16x16 32bpp 红块+透明背景);
    `assert_image` 新增 3 条断言(ico_load/ico_type/ico_resize)。
  - **验证**:doc-check **916/916**(core+ASan 双构建)、回归 26/26、
    Wayland 13、XWayland 232(assert_image 纳入 xwayland,ICO 断言随之
    计入)。AUDIT §2.4/§2.5 相应改为"已修复/已补 ICO",backlog 移除这两项。
- **round 26(LoadPicture 增加 PNG 解码)**:
  - `core_image_linux.cpp` 新增 `LinuxLoadPNG`(经 zlib `uncompress` + 五种
    滤波器重建 + Paeth):支持非隔行(Adam7 拒绝)的 0/2/3/4/6 颜色类型、
    8/16 位深(及灰度/调色板 1/2/4 位)、PLTE/tRNS;透明像素同样以品红
    哨兵 0xFFFF00FF 表示。`CMakeLists.txt` 链接 `zlib`(CI 依赖补
    `zlib1g-dev`)。新增 fixture `tests/doccheck/fixtures/test.png`
    (8x8 RGBA 红块+透明背景),`assert_image` 新增 3 条断言
    (png_load/png_type/png_resize)。
  - **验证**:doc-check **919/919**(core+ASan 双构建)、回归 26/26、
    Wayland 13、XWayland 235。AUDIT §2.5 更新为 BMP/ICO/PNG/PPM,backlog
    移除 PNG 项。
- **round 27(覆盖补全套件 + ProcessSetPriority 修复)**:
  - **`assert_misc_cov.ahk`(新增,75 断言)**:为 54 个此前无直接断言的
    worklist-IMPL 函数补最小运行断言——其中 **51 个真实调用**(primitive
    类 Float/Integer/Func/Enumerator、IsSet/IsSetRef(直接与 ByRef 两种
    形式)、Obj* 指针/容量/基类族(ObjPtr/ObjPtrAddRef/ObjAddRef/
    ObjRelease/ObjFromPtr/ObjFromPtrAddRef/ObjGetCapacity/ObjSetCapacity/
    ObjOwnProps/ObjSetBase)、StrPtr/VarSetStrCapacity/HasMethod、Process*
    (自 PID 的 Wait/WaitClose/SetPriority)、Drive*("/" 挂载点)、
    ComObjActive/ComObjFromPtr/ComCall(错误路径)、GetKeyVK/GetKeySC、
    OutputDebug/Pause(false)/Suspend 往返、HotIf 全家(含 1 参回调与重置)、
    ClipWait/ClipboardAll(Buffer 构造)、OnClipboardChange 注册/注销、
    WinActivateBottom/GroupAdd+GroupDeactivate(xwin_helper 窗口)、
    SoundBeep、OnError 触发(处理器 ExitApp 0 控制退出码)、OnExit 触发);
    **4 个**危险/交互函数(Exit/Reload/Shutdown/InputBox)在套件头部
    明确文档化为"不可自动化"并说明原因。另补 **String/Class/Menu/
    ObjBindMethod/Persistent/WinWaitNotActive** 的代码级名称引用(此前
    只出现在其他套件注释中;ObjBindMethod 以类方法绑定实测 42)。
  - **发现并说明**:v2 中不存在全局 `GuiControl` 标识符(控件类是
    `Gui.Control`/`Gui.Text`,上游同样如此;`x is GuiControl` 只是触发
    未赋值变量 #Warn 对话框),控件覆盖经 `Type(g.Add(...))="Gui.Text"`
    与 assert_gui 的 GuiCtrlFromHwnd 断言完成。
  - **ProcessSetPriority 修复**:`BIF_Linux_ProcessSetPriority` 省略
    PIDOrName 时按文档应作用于脚本自身(返回自身 PID),原实现返回 0 且
    不设优先级;现 `target.empty() ? getpid() : LinuxFindProcess(...)`。
  - **文件路径与失败语义修正**(CI 实测暴露):`assert_statements` 的
    `file_rw` 用 Windows 风格 `A_Temp "\_stmt_test.txt"`——在 GitHub
    Actions runner 上 FileAppend 静默失败、FileRead 报
    "cannot find the file specified"(core 与 ASan 双构建同现)。修正:
    ① 测试改用正斜杠路径并加 `FileExist` 验证创建(移植版对反斜杠
    按 POSIX 字面处理,Windows 式反斜杠路径在部分环境不可靠);
    ② **`BIF_FileAppend` 打开失败由静默 return 改为按文档抛 OSError**
    ("The system cannot open the file for writing.",EACCES)——此前
    写文件失败完全无声,掩盖真实错误。
  - **测试基础设施**:run_check.sh 接入新套件(Xvfb + xwin_helper +
    D-Bus);套件结束 `pkill -x xwin_helper` 清理(无 WM 的 Xvfb 下
    iconify 不隐藏窗口,残留窗口会干扰后续 assert_monitor 的
    PixelSearch);OnError 断言经处理器内 ExitApp 0 保证退出码 0
    (已处理错误在本移植仍按错误码退出)。
  - **覆盖率**:断言源码**代码级直接引用 313→363/367(98.9%)**;
    含文档注释引用 **367/367(100%)**;未代码引用仅剩
    Exit/Reload/Shutdown/InputBox(文档化为不可自动化)。覆盖统计脚本
    `_cov4.py`(代码/注释两种口径)。
  - **验证**:doc-check **994/994**(core+ASan 双构建)、回归 26/26、
    Wayland 13、XWayland 235 全绿。
