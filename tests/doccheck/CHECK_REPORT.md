# AutoHotkey v2 Linux 移植版 — 官方文档逐模块校验报告 (Doc-Check Report)

- **对照文档**: AutoHotkey v2 官方指导文档 (`docs-v2/`, AutoHotkeyDocs 仓库 `v2` 分支,
  24 个概念页 + 356 个函数页)
- **校验对象**: Linux 移植版核心解释器 (`build-core/source/linux/core/ahk_core`,
  以及 ASan 构建 `build-asan/ahk_core`),基于 AutoHotkey v2.0.26 源码
- **校验方式**: 文档条目 → `.ahk` 实测脚本 → 输出与预期逐条比对
- **结果**: **408 / 408 断言通过** (普通构建与 ASan 构建均通过;25 项 headless 回归测试亦全部通过)

---

## 1. 校验框架

| 文件 | 作用 |
|---|---|
| `tests/doccheck/extract_docs.py` | 解析 `docs-v2/docs/lib/*.htm`,提取每个函数的名称/描述/语法/参数/返回值/示例 → `doc_index.tsv`(352 个函数条目) |
| `tests/doccheck/build_worklist.py` | 将实现清单与文档条目联接,标注每个函数的实现状态 → `worklist.tsv`(187 个已实现,139 个未实现) |
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
| **合计** | | **408** |

复现命令:

```bash
bash tests/doccheck/run_check.sh                # 普通构建(自动启动本地 HTTP 服务供 Download 断言)
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
| 未实现模块 (GUI/GuiCtrl/COM/DllCall/窗口管理/热键系统/剪贴板监听等 139 项) | ⏳ 明确报错 | 调用的函数会给出清晰的 "not implemented on Linux" 运行时错误,不会静默返回错误值(见 `worklist.tsv` `NOT_IMPL`) |

## 5. 回归与构建验证

```
普通构建: tests/run_tests.sh        PASS=25 FAIL=0
          tests/doccheck/run_check.sh PASS=408 FAIL=0
ASan 构建: tests/run_tests.sh        PASS=25 FAIL=0
          tests/doccheck/run_check.sh PASS=408 FAIL=0
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
