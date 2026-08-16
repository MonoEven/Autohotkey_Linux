# AutoHotkey v2 Linux 移植版 — 官方文档逐模块校验报告 (Doc-Check Report)

- **对照文档**: AutoHotkey v2 官方指导文档 (`docs-v2/`, AutoHotkeyDocs 仓库 `v2` 分支,
  24 个概念页 + 356 个函数页)
- **校验对象**: Linux 移植版核心解释器 (`build-core/source/linux/core/ahk_core`,
  以及 ASan 构建 `build-asan/ahk_core`),基于 AutoHotkey v2.0.26 源码
- **校验方式**: 文档条目 → `.ahk` 实测脚本 → 输出与预期逐条比对
- **结果**: **595 / 595 断言通过** (普通构建与 ASan 构建均通过;含 Xvfb 虚拟显示下的窗口模块 67 项、输入模块 40 项、控件模块 62 项与显示器/像素模块 18 项实测;25 项 headless 回归测试亦全部通过)

---

## 1. 校验框架

| 文件 | 作用 |
|---|---|
| `tests/doccheck/extract_docs.py` | 解析 `docs-v2/docs/lib/*.htm`,提取每个函数的名称/描述/语法/参数/返回值/示例 → `doc_index.tsv`(352 个函数条目) |
| `tests/doccheck/build_worklist.py` | 将实现清单与文档条目联接,标注每个函数的实现状态 → `worklist.tsv`(292 个已实现,37 个未实现) |
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
| 显示器/像素 (MonitorGet/GetCount/GetName/GetPrimary/GetWorkArea + PixelGetColor/PixelSearch,XRandR/Xinerama + XGetImage 后端) | `assert_monitor.ahk` | 18 |
| **合计** | | **595** |

复现命令:

```bash
bash tests/doccheck/run_check.sh                # 普通构建(自动启动本地 HTTP 服务供 Download 断言)
bash tests/doccheck/run_check.sh --xvfb         # 含窗口模块(Xvfb :99 + xwin_helper 测试窗口)
bash tests/doccheck/run_check.sh build-asan/ahk_core   # ASan 构建
bash tests/run_tests.sh                         # 25 项 headless 回归
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
| 未实现模块 (GUI/GuiCtrl/COM/DllCall/热键系统/剪贴板监听/Edit*/SendMessage/PostMessage/Hotkey/Hotstring/SetTimer/ToolTip/ImageSearch/DirSelect/FileSelect/OnMessage 等 37 项) | ⏳ 明确报错 | 调用的函数会给出清晰的 "not implemented on Linux" 运行时错误,不会静默返回错误值(见 `worklist.tsv` `NOT_IMPL`) |

## 5. 回归与构建验证

```
普通构建: tests/run_tests.sh        PASS=25 FAIL=0
          tests/doccheck/run_check.sh PASS=408 FAIL=0 (headless)
          tests/doccheck/run_check.sh --xvfb PASS=595 FAIL=0 (含窗口 67 + 输入 40 + 控件 62 + 显示器/像素 18)
ASan 构建: tests/run_tests.sh        PASS=25 FAIL=0
          tests/doccheck/run_check.sh --xvfb PASS=595 FAIL=0
```

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
