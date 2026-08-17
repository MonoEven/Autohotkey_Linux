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

### 1.3 [已修复] ProcessSetPriority 省略 PIDOrName 时应作用于脚本自身
- 文档:`ProcessSetPriority(Level [, PIDOrName])`,省略 PIDOrName 时
  "the script's own PID is used",并返回该 PID。
- 原实现:`LinuxFindProcess("")` 恒返回 0 → 不设优先级、返回 0。
- 修复(round-27):`target.empty() ? getpid() : LinuxFindProcess(...)`;
  `assert_misc_cov` 的 `psetprio_omit` 断言(返回自身 PID > 0)。

### 1.4 [已修复] FileAppend 打开失败应抛 OSError(原为静默)
- 文档:FileAppend "throws an OSError on failure"。
- 原实现:`core_builtin_stubs.cpp` 的 `BIF_FileAppend` 在
  `std::ofstream` 打开失败时 `if (!ofs) return;` —— 完全静默,掩盖
  真实错误(round-27 在 GitHub Actions runner 上实测:Windows 型
  反斜杠路径 `A_Temp "\_stmt_test.txt"` 的 FileAppend 静默失败,
  FileRead 才报 "cannot find";core 与 ASan 双构建同现)。
- 修复:打开失败改抛 OSError("The system cannot open the file for
  writing.",LastError=EACCES)。测试 `assert_statements::file_rw`
  同时改用正斜杠路径并加 `FileExist` 创建验证(POSIX 下反斜杠是
  合法文件名字符,Windows 式路径在部分环境不可靠)。

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
- [次要] **OnClipboardChange 注册但回调永不触发**:
  `script.cpp:753 EnableClipboardListener` → `AddClipboardFormatListener`
  在 Linux 是 no-op 桩(`stdafx_linux.h:2419`),没有
  WM_CLIPBOARDUPDATE → `AHK_CLIPBOARD_CHANGE` 永远不会投递,
  OnClipboardChange 回调不运行(注册/注销本身无错,`assert_misc_cov`
  的 `onclip_reg`/`onclip_unreg` 断言;另注意注册会使脚本变为
  persistent,测试需先注销再退出)。
- [轻微] **KeyWait 为 XQueryKeymap 轮询**:无低级钩子,逻辑/物理态、
  防抖细节是近似。
- [主要→大部分已修复(round-29,check0818 审计)] **热键后端**:
  原实现与窗口/剪贴板共享 X 连接(`LinuxDispatchHotkeys` 用
  `while (XPending)` 取空连接、丢弃非键盘事件)、`sGrabbed` 静态集合
  只增不减(`Hotkey "X","Off"` 不解除系统抓取)、无 BadAccess 冲突
  检测(全局错误处理器忽略)、抓取只在 X 事件到达时建立(冷启动时序)、
  修饰键掩码写死(Alt=Mod1/Super=Mod4/锁键=Lock|Mod2)、Key-up 受
  自动重复影响、Wayland 键码映射 off-by-one(数字 0-9/F11-F24/小键盘)。
  已修复:
  - **独立热键 X 连接**(`LinuxHotkeyDisplay`):事件隔离,不再吞噬
    剪贴板/窗口事件;
  - **GrabSpec 差量同步 + XUngrabKey**:Off/禁用变体/Suspend 解除
    抓取,按键归还前台应用;
  - **条件透传**:Async 抓取 + 事件不匹配(Off/HotIf 假/Suspend/线程
    限制/`~`/Key-up 的 press 相位)时 `XUngrabKeyboard` + XTEST 重注入
    (带 8 槽注入日志防循环;两个 X 连接事件乱序,单标记不够);
    同步抓取方案被否决:冻结事件不通知抓取客户端,必然死锁;
  - **BadAccess 冲突检测**:per-request 序列号 X 错误陷阱,注册冲突
    向脚本抛明确 OSError(键名+修饰),失败抓取不记入已安装集合;
  - **注册后立即 Reconcile**(BIF 返回即同步)+ 主循环入口一次,
    消除冷启动时序风险;
  - **动态修饰键映射**(`XGetModifierMapping`:Alt/Super 槽位 + 实际
    锁定键)与 **MappingNotify 重建**(布局变化全量解除并重抓);
    锁定掩码按幂集枚举(不再写死 Caps|Num);
  - **XkbSetDetectableAutoRepeat**(Key-up 一次触发;无 XKB 时
    合成 Release 过滤回退);
  - **Wayland 键码显式表**(数字 KEY_0..KEY_9、F1-F24 分段表、
    VK_MULTIPLY=0x6A/ADD=0x6B/SUBTRACT=0x6D/DECIMAL=0x6E/DIVIDE=0x6F);
  - **独立前台客户端测试**:`assert_hotkey_pt`(xkeycap 窗口持有输入
    焦点)验证:普通热键抑制(F7 不到前台)、`~` 透传(F8 到达)、
    Off 解除抓取(F9 到达)、HotIf-false 透传(F10 到达);
  - CI:doc-check 步骤去掉 `continue-on-error`,失败将阻止合并。
  剩余(第二批/第三批,见 check0818.md):左右修饰键区分、通配修饰键
  `*`、扫描码、`A & B` 前缀、鼠标热键——X11 被动抓取无法表达,
  需 XI2 raw 观察或 evdev 层(计划中,不再静默注册假热键:能力校验
  在注册时拒绝并向脚本报错)。

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
- [次要] **ComCall 无 COM vtable**:`core_dllcall_linux.cpp` 的 ComCall
  与 DllCall 共用解析,但 Linux 没有可解引用的接口指针——文档式调用
  `ComCall(0, ComObj, "Int", ...)` 报 "Invalid arg type."(接口对象被当
  作类型串),`Ptr` 首参路径在 vtable 解引用前即抛 "Invalid parameter
  #1"(`assert_misc_cov` 的 `comcall_err` 断言固化该错误路径)。
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
- [主要→已修复(round-25)] **GuiFromHwnd/GuiCtrlFromHwnd 反查已实现**:
  `core_mdfunc_linux.cpp` 现在把 HWND 交给 GTK 后端的
  `GuiFromHwnd/GuiCtrlFromHwnd`(widget↔gui/control 真实映射)——
  实测:Gui Hwnd 返回 Gui 对象、控件 Hwnd+Recurse 返回所属 Gui、
  控件 Hwnd 由 GuiCtrlFromHwnd 返回控件、无效/已销毁句柄返回空串
  (assert_gui 新增 6 条断言)。`MenuFromHandle` 仍恒返回 "":Linux 无
  Win32 HMENU 可映射(文档化空串结果)。
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
- [主要→次要] **图像解码已补 ICO/PNG(round-25/26)**:现在支持 BMP(24/32
  位 BI_RGB)、**ICO(经典 DIB 条目:32/24/8/4/1-bpp + 调色板)与
  PNG(非隔行:0/2/3/4/6 颜色类型、8/16/1/2/4 位、五种滤波器、tRNS,经
  zlib)与 PPM(P6/P3)**;透明像素在移植版 RGB-only 模型下以品红哨兵
  `0xFFFF00FF` 表示(可用于 ImageSearch `*Trans`),`LoadPicture` 对
  .ico 上报 OutImageType="Icon"。仍无 PNG/GIF/JPEG/CUR/AVIF
  (`core_image_linux.cpp`)。
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
- [次要→已实现(round-28)] **Reload 重启语义**:
  `core_mdfunc_linux.cpp` 的 `BIF_Linux_Reload` 改为 Linux 专有协议——
  `ActionExec(mOurEXE, "/restart /script <script> /pid <pid>")` 启动新
  实例;`main_linux.cpp` 解析 `/restart` 参数,**新实例先加载脚本,成功
  后向旧进程发 SIGTERM**;旧进程的 SIGTERM handler(仅置标志)由等待
  循环(MsgSleep/LinuxRunMainLoop)转为 `ExitApp(EXIT_RELOAD)`——OnExit
  回调以 ExitReason="Reload" 运行后进程退出;若新实例加载失败则不发
  信号,旧脚本继续运行(与上游语义一致)。配套修复:**`GetExitReasonString`
  由恒返回空串的桩改为完整映射**(此前所有 OnExit 的 ExitReason 参数
  恒为空——连带影响 Exit/Close/Error 等全部退出原因)。验证:
  `tests/run_tests.sh` 新增 **t26_reload**(端到端:旧实例 OnExit
  reason=Reload、新实例接管、无残留进程),回归 26→27 全绿。
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
2. **行为断言**:doc-check **994 断言**(core + asan 双构建全绿),
   逐条对照官方文档语义;回归 26/26;Wayland 13;XWayland 235。
3. **示例审计**:`verify_examples*.py` 对 docs-v2 全部 1390 个示例块
   做 headless + Xvfb 自动化审计,244 个无法独立运行的已分类
   (绝大多数是依赖上下文的教学片段,非移植缺陷)。
4. **与上游对标**:关键解析语义用官方 v2.0.26 二进制实测确认
   (如 `catch e` 需要 `as`、"MenuSelect 第三参必填"),确认一致。
5. **内存安全**:ASan+UBSan 双构建;本轮又用 ASan 抓到并修复
   Send 崩溃。

### 3.2 边界(诚实说明)
- 994 断言是**采样式**语义校验,不是每个文档行为全量:
  **364/367 IMPL 函数直接出现在断言源码/回归套件的可执行代码中
  (99.2%;round-27 的 `assert_misc_cov` 从 313 提升至 363,
  round-28 的 `t26_reload` 使 Reload 计入代码引用);含文档注释
  引用口径为 367/367(100%)**。未代码引用的仅剩 3 个
  **不可自动化**函数:Exit / Shutdown(破坏进程/系统)与 InputBox
  (交互阻塞)——已在套件头部文档化并说明原因,不硬测。
  覆盖统计脚本 `tests/doccheck/_cov4.py`(注释剥离后按词匹配,
  扫描 assert_*.ahk 与 run_check.sh/run_tests.sh,可复现)。
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
垃圾值/误导性错误,worklist 改标 NOT_IMPL)。
round-25 已完成:**GuiFromHwnd/GuiCtrlFromHwnd 真实反查**(见 §2.4)与
**LoadPicture ICO(经典 DIB 条目)解码**(见 §2.5,透明以品红哨兵表示)。
round-26 已完成:**LoadPicture PNG 解码**(非隔行,颜色类型 0/2/3/4/6 + 调色板
/tRNS,经 zlib;见 §2.5)。round-27 已完成:**54 个未直引用函数的最小断言套件**
(`assert_misc_cov`,75 断言,51 个真实调用 + 4 个"不可自动化"文档化;
代码级直引用 313→363/367,含注释口径 367/367;另修 ProcessSetPriority
省略参数语义、固化 ComCall 错误路径与 OnClipboardChange 不触发的文档化;
FileAppend 打开失败改抛 OSError、`assert_statements::file_rw` 去反斜杠)。
剩余:
- 其余图像格式(GIF/JPEG/Cur/AVIF)文档化即可;
- Hotstring 触发需要按键缓冲引擎(现有热键基础设施可扩展);
- InputHook 按键采集需要真实低级钩子/或被文档定为"仅状态机";
- "GuiControl" 无全局标识符(v2 语义,控件类为 Gui.Control/Gui.Text),
  已通过控件实例断言覆盖。

---
生成时间:2026;(分支 linux-port,上游 v2.0.26)
