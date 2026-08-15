# AutoHotkey v2 Linux 移植版 — 官方文档逐模块校验报告 (Doc-Check Report)

- **对照文档**: AutoHotkey v2 官方指导文档 (`docs-v2/`, AutoHotkeyDocs 仓库 `v2` 分支,
  24 个概念页 + 356 个函数页)
- **校验对象**: Linux 移植版核心解释器 (`build-core/source/linux/core/ahk_core`,
  以及 ASan 构建 `build-asan/ahk_core`),基于 AutoHotkey v2.0.26 源码
- **校验方式**: 文档条目 → `.ahk` 实测脚本 → 输出与预期逐条比对
- **结果**: **204 / 204 断言通过** (普通构建与 ASan 构建均通过;25 项 headless 回归测试亦全部通过)

---

## 1. 校验框架

| 文件 | 作用 |
|---|---|
| `tests/doccheck/extract_docs.py` | 解析 `docs-v2/docs/lib/*.htm`,提取每个函数的名称/描述/语法/参数/返回值/示例 → `doc_index.tsv`(352 个函数条目) |
| `tests/doccheck/build_worklist.py` | 将实现清单与文档条目联接,标注每个函数的实现状态 → `worklist.tsv`(137 个已实现,177 个未实现) |
| `tests/doccheck/assert_*.ahk` | 按模块编写的实测脚本(每个断言输出 `name=value` 行,取自官方文档语义) |
| `tests/doccheck/assert_*_expect.txt` | 与文档语义对应的期望值 |
| `tests/doccheck/run_check.sh` | 运行全部断言并逐条比对(支持传入任意二进制路径,如 `run_check.sh build-asan/ahk_core`) |

模块划分与断言分布:

| 模块 | 断言脚本 | 断言数 |
|---|---|---|
| 数学 | `assert_math.ahk` | 44 |
| 字符串 | `assert_string.ahk` | 47 |
| 对象/数组/Map/类 | `assert_object.ahk` | 33 |
| 文件/目录 | `assert_file.ahk` | 13 |
| 日期时间 | `assert_datetime.ahk` | 33 |
| 通用/环境/进程/驱动器/INI/剪贴板 | `assert_general.ahk` | 31 |
| **合计** | | **204** |

复现命令:

```bash
bash tests/doccheck/run_check.sh                # 普通构建
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

---

## 4. 模块校验结论

| 模块 | 状态 | 说明 |
|---|---|---|
| 数学 (Math/Random/Abs/Mod/… ) | ✅ 44/44 | `GenRandom` 已用 `getrandom` 实现 |
| 字符串 (StrLen/SubStr/InStr/StrReplace/Trim/Format/Sort/SplitPath/StrUpper/Lower/Title/…) | ✅ 47/47 | 本轮修复 Format/CharUpper/翻译器后全绿 |
| 对象 (Object/Array/Map/类/属性/绑定方法/枚举) | ✅ 33/33 | 类字段初始化、属性 get/set 全绿 |
| 文件 (FileOpen/File对象方法/FileRead/Append/Delete/DirCreate/Delete/Copy/Move) | ✅ 13/13 | 本轮补齐 File 类成员注册 |
| 日期时间 (DateAdd/Diff/FormatTime/A_Now/A_YYYY 等) | ✅ 33/33 | 与文档 TimeUnits/Format 语义一致 |
| 通用 (Env/WorkingDir/Sleep/MsgBox/Process/Run/RunWait/Drive/Ini/Clipboard/GetKeyName/A_* 变量) | ✅ 31/31 | 路径函数与键名表修复后全绿 |
| 未实现模块 (GUI/GuiCtrl/COM/DllCall/RegEx/注册表/窗口管理/热键系统/剪贴板监听等 177 项) | ⏳ 明确报错 | 调用的函数会给出清晰的 "not implemented on Linux" 运行时错误,不会静默返回错误值(见 `worklist.tsv` `NOT_IMPL`) |

## 5. 回归与构建验证

```
普通构建: tests/run_tests.sh        PASS=25 FAIL=0
          tests/doccheck/run_check.sh PASS=204 FAIL=0
ASan 构建: tests/run_tests.sh        PASS=25 FAIL=0
          tests/doccheck/run_check.sh PASS=204 FAIL=0
```

## 6. 本轮改动文件

- `source/linux/core/core_file_linux.cpp` / `.h`(新增):File 类成员注册
- `source/TextIO.cpp`:friend 表填充、`LinuxIsFileObject`
- `source/linux/core/core_platform_stubs.cpp`:`DefineMetadataMembers` 接入 File;键名表;真实 `GetFullPathName` 辅助
- `source/linux/stdafx_linux.h`:`HRESULT` 32 位;`CharLower/CharUpper`;`_vsntprintf` 翻译器(`*`/`I64`);`GetFullPathName/GetCurrentDirectory/SetCurrentDirectory` 真实实现
- `source/lib/string.cpp`:`BIF_Format` Linux 浮点参数传递
- `source/linux/core/main_linux.cpp`:脚本路径字段初始化
- `source/linux/core/core_builtin_stubs.cpp`:`FileExist/DirExist` 属性字母语义
- `source/linux/core/CMakeLists.txt`:加入 `core_file_linux.cpp`
- `tests/doccheck/*`:断言脚本与期望值按文档修正,新增 `run_check.sh` 二进制参数
