# 弱化实现盘点 + 完备性说明 (2026 审查)

对 `linux-port` 分支的逐子系统核查,目的是:① 找出"已经实现、但相对
Windows/官方文档被弱化(可调用但不完整/模拟/依赖外部环境)"的地方;② 就
"已实现内容是否完备"给出可检验的证据与明确边界。

审查方法:静态读码 + 运行时探针(build-core/build-asan 实测)+
`doc_index.tsv`/`worklist.tsv`/断言覆盖率脚本交叉核对;所有 MsgBox-
相关脚本均在 Xvfb 下运行。

---

## 1. 本轮审查新发现并已处理的问题

### 1.1 [已修复] Send/SendEvent/SendInput/SendPlay 无显示 + Wayland 可用时段错误
- **现象**(ASan 栈):`SendInput("hi")` 在无 X 显示但 Wayland 活跃的环境
  (如 sway 运行中、DISPLAY 未设置)直接 `SIGSEGV`——
  `LinuxFakeKey(NULL)` 尝试 Wayland 注入失败后落到
  `XKeysymToKeycode(NULL, ks)`(读地址 0xf8)崩溃。
- **根因**:`core_input_linux.cpp` 的 `LinuxSendWrapper` 守卫只拦
  `!d && !LinuxWaylandActive()`;当 Wayland 判定为"活跃"但其注入
  (virtual-keyboard)实际不可用时,d==NULL 被一路传到 Xlib。
- **修复**:`LinuxKeycodeForVk()` 与 `LinuxFakeKey()` 增加 `!d` 守卫
  (静默 no-op 而非崩溃)。对 Xvfb/X11 正常路径零影响(有显示)d==NULL
  分支不再进入)。已用 ASan 复测:原崩溃探针 20/20 正常完成。

### 1.2 [已修正文档] SoundBeep / SoundPlay 实际已实现
- `SoundBeep`:`core_mdfunc_linux.cpp:1442` — 终端 `\a` / X11 `XBell`
  (有显示时),Duration 以等待体现,**Frequency 参数被忽略**。
- `SoundPlay`:`core_mdfunc_linux.cpp:2713` — `paplay` / `aplay`,播放失败
  抛 "The sound could not be played." OSError;Wait 参数恒同步。
- 上一轮文档误写成 "not yet ported / raises an error",已同步修正
  `Sound.htm`/`SoundBeep.htm`/`SoundPlay.htm`/`linux-port.htm`
  (改为 note 描述真实行为)。

---

## 2. 弱化实现清单(审计结果)

按子系统列出;`[主要]`= 文档主要用途偏差大,[次要]= 部分语义丢失,
[轻微]= 边缘/compat。带 `file:line` 依据。

### 2.1 输入 / 热键 / 热字串 / InputHook
- [主要] **Hotstring 注册但永不展开**:`core_mdfunc_linux.cpp:1020-1024`
  注释明示 "registered but never expand, because the port has no keyboard
  hook"。键入触发文本不会替换。
- [次要] **Hotstring 调用形式有限**:实测 `Hotstring("::btw::by the way")`
  (简写整体式)报 "Nonexistent hotstring."、`Hotstring("btw","repl")`
  (裸名)报 "Parameter #1 invalid.";只有 `Hotstring("::btw","repl")`
  (带前缀 `::` 的名称 + 独立替换,`assert_msg` 所用)可用。
- [主要] **Send/SendEvent/SendInput/SendPlay 完全等价**:
  `core_input_linux.cpp:745-748` 四个 BIF 都走同一 `LinuxSendWrapper(...,false)`
  → XTEST;SendInput 的 "脚本忙时丢弃按键" 缓冲语义、SendPlay 的
  注入方式区别均未复刻;只有 SendText(逐字符 aRaw)不同。
- [次要] **InstallKeybdHook/InstallMouseHook 仅记录布尔**:
  `core_input_linux.cpp:1150-1162`,不安装任何低级钩子(无显示时不报错)。
- [次要] **InputHook 不抓键**:`core_inputhook_linux.cpp` 状态机齐备
  (Start/Stop/Timeout/EndReason/InProgress),但 OnKeyDown/OnChar 永不触发、
  采集内容恒空、不抓取键盘(GDK/X# 双消费会崩)——已文档化。
- [轻微] **KeyWait 为 XQueryKeymap 轮询**:无低级钩子,逻辑/物理态、
  防抖细节是近似。
- [轻微] **Hotkey 部分语法不落地**:鼠标键热键、`A & B` 前缀、
  `~`/`*`/`UP` 变体中依赖低级钩子的部分无 XGrabKey 对应(文档化)。

### 2.2 声音 / 光标 / 回调 / DllCall
- [次要] **SoundGet*/SoundSet* 依赖外部工具**:`core_sound_linux.cpp` —
  仅当安装 `pactl`(或 `amixer`)且相应服务存在才工作,否则 OSError;
  Component/Device 只做有限处理(基本限于 master),`SoundGetInterface`
  恒 0(X11/ALSA 无 COM 概念)。
- [次要] **SoundBeep 忽略 Frequency**:见 1.2(仅响铃,不作音高)。
- [次要] **CaretGetPos 仅 GTK 文本控件**:`script_gui_linux.cpp`
  `AhkGtkCaretGetPos` 只对 GDK 窗口内聚焦的 GtkEntry/GtkTextView 有效;
  终端/纯 X/非 GTK 应用取不到真实插入点。
- [轻微] **CallbackCreate 选项简化**:`core_callback_linux.cpp` —
  'C'/'c' 调用约定在 SysV ABI 无差别(no-op);'F' 与默认"新线程"
  语义经 `InitNewThread` 模拟;参数一律 `UINT_PTR` 槽(浮点回调参数
  不会被封送)。
- [次要] **DllCall 仅 .so**:Windows DLL 不可加载;"Proch"/加载偏好等
  Windows 语义无对应(文档化);部分类型经 8 字节槽传递与 Windows
  ABI 仍有细微差别。

### 2.3 COM / 剪贴板 / 注册表 / 文件
- [主要] **COM 仅 D-Bus**:`core_com_*` — 无 IUnknown/IDispatch 指针,
  `ComObjFromPtr` 把指针当不透明句柄,`ComObjType("IID")` 返回
  "(D-Bus)" 占位符,`ComObjActive`≈`ComObjGet`,`ComObjArray`
  (SafeArray)/`ComObjQuery`/`ComObjConnect` 抛明确错误(无可运行
  实现)。
- [次要] **注册表是单用户文件**:`core_builtin_stubs.cpp:697-700` —
  `~/.config/autohotkey-registry.txt`(INI 风格),HKCU/HKLM/HKCR/HKCC
  全部归一进同一文件;无系统范围/权限/32-64 视图语义。
- [次要] **TrayTip/TraySetIcon 已改为明确报未移植**(round-24):Linux 无托盘图标,
  原 `Shell_NotifyIcon` 空 shim 恒返回 FALSE 且 `BIF_Linux_TrayTip` 未设返回值
  → 实测返回垃圾整数;`TraySetIcon` 实测报误导性的 "Can't load icon."。现已
  **改为抛出统一的 "This built-in function has not been ported to Linux yet."**
  错误(与文档一致),并把二者在 `worklist.tsv` 标为 NOT_IMPL(不再冒充已实现)。
- [次要] **ClipboardAll 依赖 X 剪贴板**:空剪贴板时报 "Can't open
  clipboard for reading."(Windows 的二进制 Self.IID 等无对应)。

### 2.4 GUI / 菜单 / 控件
- [主要] **GuiFromHwnd/GuiCtrlFromHwnd/MenuFromHandle 恒返回 ""**:
  `core_mdfunc_linux.cpp:1071-1087` —— 即便 GTK Gui 已实现且有真实
  Hwnd,这三个函数也无反查映射,永远返回空串(与"Gui 已实现"自相
  矛盾;`assert` 只测了无效句柄分支)。
- [次要] **Gui/GuiControl 是 GTK3 子集**:部分控件属性/选项接受但忽略
  (字体/颜色/边距/Resize/Min/Max/部分 OnEvent 变体需逐一核对
  `script_gui_linux.cpp`);非 GTK 的 ActiveX 等无对应。
- [次要] **控件状态部分虚拟**:`core_ctrl_linux.cpp` 中 enabled/
  checked/style 等是按本地 shadow 记录,外部 X 客户端直接改的
  状态读不回;文本/几何/聚焦/点击走真实 X。
- [次要] **MenuSelect 实际报错**:`core_mdfunc.cpp:1005` 解析目标后抛
  "does not have a standard Win32 menu" TargetError(X11/GTK 菜单不是
  Win32 菜单);等价于"菜单交互不可用"。

### 2.5 窗口 / 屏幕 / 图像
- [主要] **图像只解 BMP(24/32 位 BI_RGB)+ PPM(P6/P3)**:
  `core_image_linux.cpp:7-8` — 无 PNG/GIF/JPEG/ICO/CUR/AVIF;
  LoadPicture/IL_* 只能处理这两种格式。
- [次要] **Win 风格/透明度/置顶是虚拟 shadow + EWMH 发布**:
  `core_win_linux.cpp:12-13,86-98` — WinGet/WinSetStyle、Transparent
  等记在每窗口 shadow map;对本移植自己设过的窗口准确,对外部窗口
  不保证;`_NET_WM_STATE_ABOVE`/`_NET_WM_WINDOW_OPACITY` 会发给 WM;
  WinSetTransColor 颜色概念在 X11 无对应。
- [轻微] **MonitorGetWorkArea 等近似**(XRandR/Xinerama/单屏回退);
  Pixel 函数在无显示时按文档报 OSError。
- [次要] **FileCreateShortcut 用 .desktop/.url 替代 .lnk**(图标序号、
  运行状态等字段回填 0/1,文档化)。

### 2.6 杂项 BIF / A_* 变量
- [次要] **RunAs 存凭据但 Run 抛错**:`core_mdfunc.cpp:1053-1056`,
  无 CreateProcessWithLogonW → Run/RunWait 报 "Launch Error (possibly
  related to RunAs)"。
- [次要] **OnMessage/SendMessage/PostMessage 在 X11 无 Win32 消息**:
  已按上游 BIF 适配(校验+监视器存储),但消息不投递(返回默认 0 /
  解析目标后按文档错误),`linux-port.htm` 仍列为不可用(偏差小)。
- [轻微] **Set*LockState 在纯 Wayland 报 "No X display"**;输出函数
  OutputDebug 走 stderr(无系统调试器)。
- [轻微] **GetKey*/SysGet/Drive* 依赖 /proc + 外部工具**(lsblk/eject/
  udisksctl)、Download 依赖 curl/wget——语义为"文档要求的错误路径 +
  外部工具可用才成功"。

---

## 3. 完备性:能证明什么,不能证明什么

### 3.1 能证明的(有可复现证据)
1. **入口层面全覆盖**:`doc_index.tsv` 352 个 lib 页全部有状态——
   `worklist.tsv` 370 行(367 IMPL + 3 行 NOT_IMPL:ComObjArray、TrayTip、
   TraySetIcon),另有 ComObjConnect/ComObjQuery 等 3 个 D-Bus/COM 边界在
   g_BIF 注册为可运行错误路径(build 流程打印 NOT_IMPL=6);
   54 个非函数页(语句/指令/类别/索引)已识别,其中语句/类别/指令/
   索引页代码形式本轮已纳入 `assert_statements`(19 断言)。
2. **行为断言**:doc-check **907 断言**(core + asan 双构建全绿),
   逐条对照官方文档语义;回归 26/26;Wayland 13;XWayland 229。
3. **示例审计**:`verify_examples*.py` 对 docs-v2 全部 1390 个示例块
   做 headless + Xvfb 自动化审计,244 个无法独立运行的已分类
   (绝大多数是依赖上下文的教学片段,非移植缺陷)。
4. **与上游对标**:关键解析语义用官方 v2.0.26 二进制实测确认
   (如 `catch e` 需要 `as`、"MenuSelect 第三参必填"),确认一致。
5. **内存安全**:ASan+UBSan 双构建;本轮又用 ASan 抓到并修复
   Send 崩溃。

### 3.2 边界(诚实说明)
- 907 断言是**采样式**语义校验,不是每个文档行为全量:
  313/367 IMPL 函数直接出现在断言源码;54 个未直接引用(多为叶函数/
  难自动化/间接覆盖),例如 ClipboardAll、ComObjActive、
  WinActivateBottom、Set*LockState 的无显示分支等。
- doc-check 的合格标准是"调用不崩、返回或抛出文档规定的错误"——
  对 §2 的弱化区(InputHook 采集、Hotstring 展开、GuiFromHwnd 反查、
  COM IID/SafeArray、图像多格式、tray、注册表 hive 等)并不保证
  与 Windows 全语义一致。
- 无法声明"与 Windows 逐字节一致"或"每一页每一行为都被覆盖";
  可声明的是:可运行断言覆盖的语义全部通过,且未覆盖/弱化点已由本
  报告与各页面 note 明示。

---

## 4. 后续建议(backlog,按价值排序)
round-24 已完成:**Send 崩溃回归断言**(`assert_notimpl: send_nocrash`,无显示 +
Wayland 活跃时也绝不段错误)与 **TrayTip/TraySetIcon 明确报未移植**(不再返回
垃圾值/误导性错误,worklist 改标 NOT_IMPL)。剩余:
- 补 `GuiFromHwnd`(Hwnd→Gui 反查)真实映射——成本低、文档自洽;
- LoadPicture 增加 ICO(内嵌 DIB)/PNG 简易解码(最常用),其余格式可文档化;
- Hotstring 触发需要按键缓冲引擎(现有热键基础设施可扩展);
- InputHook 按键采集需要真实低级钩子/或被文档定为"仅状态机";
- 为 54 个未直接引用函数补最小断言,提高 traceability 到接近全量。

---
生成时间:2026;(分支 linux-port,上游 v2.0.26)
