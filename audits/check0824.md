# 独立技术审计结论

## Executive Summary

**审计对象：** `linux-port` 分支，固定到提交 **`eaeeaf74ad8501f4810e34240a2b6cb9e850d942`**，而不是 README、发布页或历史审计文档描述的抽象状态。该提交晚于当前 `v2.0.26-linux.16` 发布版；项目自己的最新发布说明也仍将其定义为 **technology preview**。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/commit/eaeeaf74ad8501f4810e34240a2b6cb9e850d942))

先更正审计过程中的一处早期表述：当前 X11 `InputHook`/Hotstring 捕获并不是持续调用 `XGrabKeyboard` 独占整把键盘，而是对 **keycode 8–255 × 主修饰键组合 × 锁定键组合**建立大量 passive `XGrabKey`，然后拦截、缓存并重新发送事件。这比整键盘 active grab 稍好，但仍然具有严重的多脚本冲突、事件语义改变和应用兼容问题。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_capture_linux.cpp))

### 三个最核心结论

**1. 这个项目现在处于什么水平？**

我的评级是：

> **Technology preview；在有限的 X11 脚本范围内可视为 advanced preview。**

它已经不是简单 demo：解释器主体大量沿用了 AutoHotkey v2.0.26 的真实 parser、expression、object、script runtime，GTK GUI、X11 XTEST 输入、部分窗口操作、Wayland virtual keyboard、evdev、portal、AT-SPI、打包等都有实质代码。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/CMakeLists.txt))

但它还不是“日常可用的 Linux AutoHotkey”，更不是 production-ready。决定性原因不是函数数量不够，而是：

- 输入捕获、输入注入、合成事件识别、`SendLevel/InputLevel`、多脚本仲裁没有形成一个统一模型；
- X11、portal、GNOME extension、evdev、Wayland virtual keyboard、uinput 实际上是多条语义不同的通道；
- 原生 Wayland 的 portal/extension 只能提供离散 shortcut activation，不能提供 Hotstring/InputHook 所需的完整字符流；
- X11 Hotstring/InputHook 依靠“大范围 passive grabs + XSendEvent 重放”，与普通物理输入并不等价；
- Window/Control 层有大量 process-local 虚拟状态或有限 AT-SPI 近似；
- 测试大量形成“本项目发送、本项目接收”的自验证闭环。

**2. 距离真正的 Linux AutoHotkey v2 有多远？**

| 环境                        | 当前判断                                             | 距离实用移植版                                               |
| --------------------------- | ---------------------------------------------------- | ------------------------------------------------------------ |
| **X11**                     | **Advanced preview，约 55/100**                      | 基本 Send、鼠标、普通组合热键、GTK GUI 可用；但 scan code、自定义组合、多脚本、Hotstring/InputHook、IME、Window/Control 和可靠性仍需重构 |
| **XWayland**                | **Technology preview，约 42/100**                    | 能自动化 XWayland 客户端，但对同一桌面上的原生 Wayland 窗口基本不可见；输入和窗口模型呈 split-brain |
| **GNOME Wayland**           | **Technology preview，约 24/100**                    | extension/portal 可做部分全局快捷键；AT-SPI 可覆盖少量控件；缺少完整输入流、可靠注入、key-up 和 IME 语义 |
| **KDE Wayland**             | **Technology preview，约 27/100**                    | portal/KGlobalAccel 可覆盖离散快捷键，但不是 AHK keyboard hook；窗口与控件仍受 Wayland/AT-SPI 限制 |
| **wlroots/sway**            | **Technology preview，约 31/100**                    | virtual-keyboard、virtual-pointer、screencopy 协议路径相对明确，但这些是 compositor extension；捕获仍需 portal/evdev，语义没有闭环 |
| **其他 Wayland compositor** | **Experimental 到 technology preview，约 15–25/100** | 取决于 portal backend、virtual input protocol、AT-SPI 和权限；不能由“Wayland 支持”四字统一概括 |

在同时存在 `DISPLAY` 与 `WAYLAND_DISPLAY` 的桌面上，当前代码倾向走 X11/XWayland 通道。这会导致脚本能够看到或控制 XWayland 窗口，却无法以相同方式操作原生 Wayland 窗口。基础 Wayland 协议本身没有 Windows 式全局窗口枚举或任意输入截获；但项目也尚未实现可补足部分能力的 libei/EIS、RemoteDesktop portal 输入通道和成熟 compositor-specific broker。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_wayland_linux.cpp))

**3. 两种完成度必须分开看**

### API / Feature Coverage：**82%**

这表示：

- 很多 AHK v2 语法和 API 名称可解析、可调用；
- 大量函数存在 Linux replacement；
- GTK GUI、文件、进程、数学、字符串、对象、表达式等已有较宽表面；
- DllCall、COM、Win/Control、Clipboard 等都有一个 Linux 入口。

它**不表示这些 API 与 Windows AHK v2.0.26 等价**。

### Real-world Compatibility / Production Readiness：**31%**

这表示：

- 简单脚本、纯语言脚本、部分 X11 热键和 Send、项目自己的 GTK GUI，可以有实际价值；
- 普通 Windows AHK 用户把包含 Hotstring、InputHook、复杂 remap、ControlSend、窗口自动化、IME、COM 的真实脚本搬过来，大概率需要重写；
- 脚本长期运行、多脚本并发、跨 GNOME/KDE/wlroots、崩溃恢复及权限升级仍不能视为可靠。

以上百分比是基于源码和测试设计的工程估值，误差约 ±5 分，不是根据函数计数机械换算。

---

# 一、量化评分

| 维度                              |   分数 | 主要依据                                                     |
| --------------------------------- | -----: | ------------------------------------------------------------ |
| AHK v2 语言兼容性                 | **82** | 大量直接复用上游 interpreter core；主要损失来自平台 BIF、内部 stub 和线程/输入语义 |
| X11 自动化能力                    | **56** | XTEST、XGrabKey、基本窗口、鼠标、剪贴板存在；复杂键盘语义、控件、IME、多脚本明显不足 |
| XWayland 自动化能力               | **43** | 对 XWayland 客户端近似 X11；对原生 Wayland 客户端不可统一控制 |
| 原生 Wayland 自动化能力           | **23** | shortcut、注入、截图各有局部 backend，但没有完整 AHK 输入/窗口模型 |
| Hotkey/Hotstring/InputHook 兼容性 | **34** | 普通 VK 热键有实质实现；scan code、自定义组合、多 owner、字符流和 synthetic semantics 不完整 |
| Send/输入注入兼容性               | **41** | X11 XTEST 较实用；各 Send mode 未真正区分，Unicode/Wayland 依赖 keymap 借用、剪贴板或权限通道 |
| Window/Control/UI 自动化          | **34** | X11 基本 Win* 可用；枚举、WinText、Control 状态、后台发送、Wayland 控件均明显弱化 |
| GUI                               | **60** | GTK3 控件实现量可观；ActiveX 不存在，部分选项静默忽略，事件/DPI/桌面矩阵验证不足 |
| Unicode/IME                       | **27** | Unicode 有 workaround，但键盘布局、preedit/commit、IBus/Fcitx、中文 Hotstring/InputHook 仍不可靠 |
| 稳定性                            | **40** | 有 ASan 和资源清理意识；仍有全局 X error handler、阻塞 D-Bus、grab 状态错误及短时 soak |
| 测试可信度                        | **35** | 测试量大且能防回归；但大量 self-validation、headless 环境、skip 和弱 oracle |
| 跨发行版/桌面兼容性               | **34** | 有多发行版容器构建；真实桌面 CI 主要是 headless sway/Xvfb，不等于 GNOME/KDE |
| Production readiness              | **28** | 权限、输入仲裁、真实桌面验证、长运行、发布信任链尚未达到生产标准 |

---

# 二、语言层：项目最强的一层，但不能替桌面自动化层背书

构建系统直接编译了上游的 `script.cpp`、`script2.cpp`、expression、object、string、file、interop、regex、hotkey、window 等大量核心代码，而不是重新实现一个 AHK-like parser。因此，变量、对象、表达式、函数、异常、闭包、类、数组、Map 等纯语言能力预计是项目中兼容度最高的部分。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/CMakeLists.txt))

但构建也同时链接了：

- `core_stubs.cpp`
- `core_builtin_stubs.cpp`
- `core_platform_stubs.cpp`

其中仍有 `A_ScriptHwnd = 0`、hook primitive no-op、modifier-state helper 返回 false、`ControlGetClassNN` 空实现、`WinGroup::IsMember` 空实现、registry 转换空实现等。这说明“上游 interpreter core 被编译”并不等于所有内部依赖已被正确替换。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_builtin_stubs.cpp))

另一个代码质量信号是核心构建使用 `-fpermissive` 和 `-Wno-jump-misses-init`。这不是功能 bug，但它会掩盖本应被 C++ 编译器拒绝或警告的移植问题，不适合作为长期 production 基线。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/CMakeLists.txt))

**判断：已确认。**

---

# 三、输入架构审计

## 3.1 当前并不是一套输入模型，而是多套互不等价的实现

当前输入路径大致是：

| 能力                       | X11                           | Portal                 | GNOME extension    | evdev/uinput         | Wayland virtual protocols |
| -------------------------- | ----------------------------- | ---------------------- | ------------------ | -------------------- | ------------------------- |
| 全局 shortcut              | XGrabKey                      | GlobalShortcuts        | Shell accelerator  | evdev stream         | 无                        |
| key-down/up 原始流         | 部分                          | 否                     | 不完整             | 是                   | 注入侧，不是捕获侧        |
| suppression                | grab                          | portal 不保证 AHK 语义 | accelerator 消费   | EVIOCGRAB            | 不适用                    |
| passthrough                | ungrab/reinject 或 XSendEvent | 否                     | 否/有限            | uinput replay        | 不适用                    |
| Hotstring/InputHook 字符流 | 大范围 grabs                  | 否                     | 否                 | 理论可做，当前不完整 | 否                        |
| 输入注入                   | XTEST                         | 非当前 Send 主路径     | 非当前 Send 主路径 | uinput               | virtual keyboard/pointer  |
| 合成事件身份               | 本进程启发式 log              | 无统一身份             | 无统一身份         | 无统一身份           | 无统一身份                |

`input_backend.h` 声称可以按单个 hotkey 路由，并且 capability 表只描述 passthrough、key-up、wildcard、bare key 等少数维度；它没有表示 scan code、自定义组合、输入级别、字符流或 IME 能力。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/input_backend.h))

更重要的是，`LinuxInputBackendRoute()` 虽然会“返回”一个 backend kind，但实际 `Sync()`、`Dispatch()` 和 `Shutdown()` 都只 switch 全局缓存的 `CurrentKind()`。静态代码中未见真正把不同 hotkey 同时注册到不同 backend 并统一调度的 multiplexer。因此所谓 per-hotkey routing 目前更接近**能力查询/报告接口**，而不是完整运行时路由。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/input_backend.cpp))

**判断：高概率。** 若存在项目其他位置消费 `LinuxInputBackendRoute()` 并执行独立注册，可能覆盖少量情况；但当前 backend 生命周期仍是单 active-kind 模型。

---

## 3.2 X11 Hotkey：普通组合有效，但不是完整 AHK hook

已实现的部分包括：

- 普通虚拟键 hotkey；
- 左右 modifier 的部分匹配；
- wildcard 的 modifier 超集展开；
- key-up 处理；
- HotIf variant 选择；
- 鼠标 button hotkey；
- X11 grab 冲突检测。

但核心缺口是：

### Scan-code hotkey 不支持

`LinuxHotkeyKeycode()` 只接受 `mVK`，否则直接返回 0，并明确注释 scan-code hotkeys unsupported。Windows AHK 中 `scXXX` 是处理布局差异、特殊键、OEM 键和物理位置脚本的基础能力。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_hotkey_linux.cpp))

### 自定义组合 `a & b` 不完整

代码会跳过或无法通过 `mModifierVK` 表达 custom prefix。这个缺口也不在 capability struct 中，因此 backend 甚至可能被报告为“满足要求”。

### `~` passthrough 不是原始事件继续传播

部分 passthrough 依赖临时 ungrab 和 XTEST reinject，Hotstring/InputHook 捕获则用 `XSendEvent` 发往 focus window。重注入事件在时间戳、device identity、`send_event` 标志、grab 行为和应用接受策略上都不同于原始物理事件。X.Org 文档明确规定 `XSendEvent` 会把 `send_event` 设为 True，并忽略 active grabs；应用可以识别或拒绝这种事件。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_capture_linux.cpp))

### Grab 冲突状态存在确定性 bug

`LinuxReconcileHotkeyGrabs()` 捕获一次 `BadAccess` 后会识别冲突的 pending grab，但随后仍把**全部 pending spec**加入 `sInstalled`，包括实际失败的那一个。后续 reconcile 看到它已在 `sInstalled`，可能不再重试。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_hotkey_linux.cpp))

X11 规范明确规定：相同 key/modifier 已被另一个 client grab 时返回 `BadAccess`。这不是理论边界，而是多脚本或与桌面快捷键冲突时的正常路径。([xorg](https://www.x.org/releases/X11R7.6-RC1/doc/man/man3/XGrabKey.3.xhtml?utm_source=chatgpt.com))

**判断：已确认。**

---

## 3.3 X11 Hotstring/InputHook：表面可用，但代价很高

当存在任意 Hotstring 或 InputHook 时，代码会为 keycode 8–255、主 modifier 组合和 lock modifier 组合建立大量 passive grabs。输入被本进程拦住，可能组成 trigger 的事件被保存在固定 128 项数组中，其余再转发给应用。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_capture_linux.cpp))

这带来四类问题：

1. **多脚本不成立。** X11 的相同 passive grab 只能由一个 client 持有；第二个脚本可能得到 `BadAccess`。
2. **应用看到的是合成事件。** 被 hold 后的普通文本使用 `XSendEvent` 转发，不是原物理事件。
3. **事件时间与设备身份丢失。** 对游戏、终端、安全输入、浏览器、Electron、远程桌面等可能产生差异。
4. **固定缓冲上限。** `sHeld[128]`、`sBuffer[128]` 是硬编码上限；长 trigger、输入突发或异常 release 序列存在边界风险。

代码本身也承认 SendLevel 和每个 Hotstring 的 send-mode timing 未建模。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_capture_linux.cpp))

这不是 Windows AHK keyboard hook 的等价实现。它更接近：

> “为了获得全局字符流，用 X11 passive grab 暂停输入，再决定是否合成转发。”

**判断：已确认。**

---

## 3.4 Send 系列：API 名称存在，但 mode semantics 被压平

代码注释明确指出，`SendEvent`、`SendInput`、`SendPlay` 基本都落到同一个 XTEST 机制。Linux 上确实不可能照搬 Windows 的 SendInput API，但为了 AHK 兼容，应至少保留用户可观察的 timing、buffering、hook interaction、fallback 和 synthetic-level 语义；当前实现并未完整做到。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_input_linux.cpp))

更严重的是：

- 显式 `SendInput()` 会启用本进程自抑制；
- 经 `SendMode("Input")` 解析的普通 `Send()` 被刻意允许触发自己的 grabs。

这意味着两个在 AHK 用户认知中应高度接近的调用，在本项目中可能有不同的 self-trigger 行为。Windows AHK 的 InputLevel/SendLevel 语义要求根据发送级别和 hotkey/hotstring 输入级别判断，而不是仅根据“调用的是哪个 wrapper”决定。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_input_linux.cpp))

合成事件跟踪主要是**本进程短期日志/启发式匹配**，无法对跨脚本、跨 backend、uinput、portal、extension 和 compositor-mediated event 提供统一身份。

**判断：已确认。**

---

## 3.5 键盘布局与 Unicode

普通字符解析中存在硬编码的美式键盘 shifted map：

- `! → Shift+1`
- `@ → Shift+2`
- `:` → Shift+;`
- `? → Shift+/`

在 AZERTY、QWERTZ、Dvorak、Colemak、非拉丁布局和自定义 XKB map 上，这不能代表用户实际布局。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_input_linux.cpp))

X11 非 ASCII 字符通过临时借用一个 keycode、修改服务器 keymap、发送 XTEST、等待固定 **30 ms**，然后恢复。代码注释直接承认存在 MappingNotify race，而且修改是 server-wide 的。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_input_linux.cpp))

原生 Wayland 在没有可用 virtual keyboard/uinput Unicode 路径时，可能暂时接管剪贴板并模拟粘贴。它可能：

- 覆盖密码管理器或用户剪贴板；
- 被目标应用的 paste policy 拒绝；
- 触发剪贴板监听器；
- 与 Hotstring replacement 的 timing 不一致；
- 无法等价表达 key-down/key-up。

**判断：已确认。**

---

## 3.6 IME、中文和 preedit

`core_ime_linux.cpp` 主要检查 D-Bus owner 和 XKB group，没有进入 IBus/Fcitx 的 composition lifecycle，也没有获取目标应用的 preedit/commit stream。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_ime_linux.cpp))

因此以下场景不能被认为与 Windows AHK 一致：

- 用户输入拼音但尚未提交；
- 候选窗口选择；
- 中文 commit 后 Hotstring buffer 应如何更新；
- Backspace 对 preedit 与已提交文本的区别；
- 每应用输入法状态；
- InputHook 应收到物理键、拼音字符还是最终中文字符；
- `SendText` 后是否经过 IME 转换。

X11 capture 根据 keysym 近似字符，原生 Wayland shortcut backend 根本没有字符流，因此中文 Hotstring/InputHook 是当前最明显的真实使用缺口之一。

**判断：已确认其未实现完整 IME integration；具体桌面上的失败形式需运行时验证。**

---

# 四、Wayland：哪些是平台限制，哪些是项目缺口

| 缺口                                                       | 分类                                                         | 判断                                                         |
| ---------------------------------------------------------- | ------------------------------------------------------------ | ------------------------------------------------------------ |
| 基础 Wayland 没有任意全局窗口枚举、位置修改和全局按键 hook | **协议本身限制**                                             | 项目无法仅靠 core Wayland protocol 解决                      |
| 离散全局快捷键                                             | **可由 XDG GlobalShortcuts Portal 解决一部分**               | 只能得到 shortcut activation/deactivation，不是原始键盘流    |
| 用户授权的键盘/鼠标注入                                    | **可由 RemoteDesktop portal + libei/EIS 解决**               | 当前项目尚未实现 libei/EIS 主路径                            |
| GNOME 全局 shortcut                                        | **可由 GNOME Shell extension 解决一部分**                    | 当前 extension 主要处理 accelerator activation，不能提供完整 InputHook/Hotstring |
| KDE 全局 shortcut                                          | **可由 KDE portal/KGlobalAccel 解决一部分**                  | 同样是快捷键服务，不是 AHK keyboard hook                     |
| wlroots 输入注入/截图                                      | **可由 virtual-keyboard、virtual-pointer、screencopy extension 解决** | 依赖 compositor 是否暴露协议；不是通用 Wayland 保证          |
| 任意键盘捕获与 suppression                                 | **可由 evdev/EVIOCGRAB 近似**                                | 需要设备权限；整个设备 grab 后重放未匹配事件，风险高         |
| 跨 backend 的 SendLevel、synthetic identity、多脚本仲裁    | **纯项目架构缺口**                                           | 平台没有强迫项目采用当前分裂模型                             |
| 原生 Wayland Hotstring/InputHook                           | **平台限制 + 项目缺口共同造成**                              | portal 不能提供字符流；但可设计受控 input broker、libinput/evdev、compositor extension 等方案 |
| Wayland Window/Control                                     | **平台限制 + 可访问性 API 覆盖不足**                         | AT-SPI 可覆盖可访问控件，但无法替代所有 Win32 HWND/message 语义 |

XDG GlobalShortcuts 的正式模型是创建 session、绑定 shortcut，并产生 Activated/Deactivated 信号；它不是全局 key event subscription。([Flatpak](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.GlobalShortcuts.html?utm_source=chatgpt.com))

RemoteDesktop portal 可以授权 keyboard/pointer/touch input，并可通过 `ConnectToEIS` 接入 EIS；libei/EIS 提供由 compositor/受信 broker 管理的 input device capability 和生命周期。这是项目下一阶段最值得增加的标准化 Wayland 注入通道。([Libinput](https://libinput.pages.freedesktop.org/libei/api/group__libeis.html?utm_source=chatgpt.com))

wlroots virtual keyboard/pointer 可以做注入，但 compositor 有权不暴露或拒绝未授权 client，因此不能据此声称“所有 Wayland 已支持”。([Wayland](https://wayland.app/protocols/virtual-keyboard-unstable-v1?utm_source=chatgpt.com))

---

# 五、不同 Wayland 环境的实际评价

## GNOME Wayland

当前路径可能包括：

- GNOME Shell extension；
- GlobalShortcuts portal，若 desktop/backend 提供；
- evdev/uinput；
- AT-SPI；
- extension 驱动的 clipboard notification。

GNOME extension 的信号路径主要处理 Activated，而 Deactivated/key-up 路径没有形成完整 AHK key-up 模型。它也没有给 Hotstring/InputHook 提供连续按键与文本流。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/input_backend_gnome_shell.cpp))

**结论：** 可以做少量全局 shortcut 和可访问控件操作，不能视为完整 AHK input engine。

## KDE Plasma Wayland

KDE 的 GlobalShortcuts portal/KGlobalAccel 可以承担离散 shortcut registration，但不能提供 wildcard、任意 bare key、custom combo、字符流、suppression/replay 和 InputHook。([KDE Invent](https://invent.kde.org/plasma/xdg-desktop-portal-kde/-/blob/master/src/globalshortcuts.cpp?ref_type=heads&utm_source=chatgpt.com))

**结论：** shortcut 覆盖可能比未装 extension 的 GNOME 更自然，但真实 AHK 语义并没有因此完整。

## wlroots/sway

项目 vendored 了 virtual-keyboard、wlr virtual-pointer 和 wlr-screencopy 协议，headless sway CI 也覆盖这些路径。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/CMakeLists.txt))

但：

- virtual keyboard/pointer 是注入，不是 capture；
- compositor 是否暴露协议取决于配置和实现；
- Hotstring/InputHook 仍需要 evdev 或另一捕获通道；
- CI 的 sway 是 headless、US layout、无真实物理 input device。

**结论：** 对项目自身协议测试较友好，但并不比 GNOME/KDE 更接近完整 AHK hook semantics。

---

# 六、Window / Control / UI 自动化

## 6.1 X11 Win*：有实质功能，但窗口模型过于简化

顶层窗口枚举直接调用 `XQueryTree(root)` 并把 root 的直接 children 视为 top-level windows，没有优先使用 EWMH `_NET_CLIENT_LIST`。在存在 reparenting window manager、frame window、嵌套客户端时，枚举结果可能是装饰 frame 而不是真实 client，或漏掉预期窗口。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_win_linux.cpp))

其他差异包括：

- HWND 被近似为 XID；
- 部分 style/exstyle/transcolor 只保存在本进程 map；
- `WinText` 在匹配路径中被忽略或覆盖有限；
- background message、thread input queue、owner/child/activation semantics 无法照搬 Win32；
- process-local virtual state 若不在 destroy 时严格清理，存在 XID 重用导致旧状态污染新窗口的风险。

最后一点是**高概率**静态风险，尚未通过运行时复现确认。

## 6.2 Control*：API surface 明显高于真实能力

多项 combo/list/check/caret/listview 状态保存在本进程的 virtual map 中。这能让测试调用：

1. `ControlAddItem`
2. 再调用 `ControlGetItems`

得到一致结果，但不代表外部 GTK/Qt/Electron 应用的实际控件发生变化。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_ctrl_linux.cpp))

`ControlSend` 的实现是：

1. 把 X focus 临时切到目标 control；
2. 通过全局 XTEST 发送；
3. 恢复原 focus。

这不是 Windows AHK 中用户常期望的“向后台 control 发送消息”；它会造成焦点闪烁、race、用户输入串入错误窗口，以及目标应用拒绝 focus 的情况。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_ctrl_linux.cpp))

## 6.3 Wayland AT-SPI fallback

Wayland `ControlGetText`：

- 只用 Control 参数作为 accessible name；
- 基本忽略 WinTitle 的应用/窗口限定；
- 遍历 AT-SPI tree 后按名称找到对象；
- 把 UTF-8 逐字节转换，所有非 ASCII byte 直接变成 `?`。

所以中文、日文、emoji 等控件文字会被破坏，这是确定性 bug。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_ctrl_linux.cpp))

AT-SPI 实现对每个对象执行同步 D-Bus 调用，默认递归深度有限，单次调用 timeout 可达数秒。在大型 accessibility tree 或挂起应用上，AHK 主线程可能长时间阻塞。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_atspi_linux.cpp))

此外，`LinuxCtrlSessionIsWayland()` 根据 desktop 字符串中是否含 GNOME 等条件做判断，存在把 GNOME Xorg 会话误判为 Wayland fallback 的风险。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_ctrl_linux.cpp))

---

# 七、Clipboard / GUI / DllCall / COM / 系统 API

## Clipboard

X11 text clipboard 有实质实现，但 `OnClipboardChange` 的 type 判断较粗，owner 存在常被近似为 text type，不能可靠区分图片、自定义 MIME 和空内容。原生 Wayland 的 change notification 主要依赖项目 GNOME extension，KDE/wlroots 没有等价通用监听路径。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_clipboard_linux.cpp))

**评价：**

- X11 文本 clipboard：中等可用；
- 原生 Wayland set/get：取决于 focus/serial/backend；
- `OnClipboardChange` 跨桌面：低兼容；
- 非文本 clipboard：明显不足。

## GUI / Menu

`script_gui_linux.cpp` 是一个数千行的 GTK3 实现，包含 Button、Edit、TreeView/ListView、Tab 等实质控件，因此 GUI 不是 stub。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/gui/script_gui_linux.cpp))

但：

- 不支持的 option 可能只输出 warning，即使所谓 strict/error 模式也未必终止；
- `ActiveX` 被替换为空白 drawing area，不具备 ActiveX 或 WebBrowser 语义；
- 真实 GNOME/KDE、HiDPI、多显示器、主题、输入法、事件顺序测试不足。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/gui/script_gui_linux.cpp))

## DllCall

Linux DllCall 通过 `dlopen/dlsym/libffi` 实现，调用 `.so` 确实有实际价值。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_dllcall_linux.cpp))

但它是 Linux-native FFI，而不是 Windows DllCall compatibility：

- `Str` 被解释为 UTF-8 `char *`，Windows AHK Unicode 下通常是 UTF-16；
- `.dll` 名称会被启发式改写成 `lib*.so`；
- stdcall/cdecl 在目标 ABI 下被压平；
- 参数数量有固定限制；
- 没有 Windows SEH 对非法 native access 的防护，错误指针可能直接终止进程。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_dllcall_linux.cpp))

## COM

所谓“COM over D-Bus”不是 COM compatibility layer，而是把 AHK COM API 名称重新解释为 D-Bus proxy：

- `ComObject("spec")` 创建 D-Bus service/path/interface proxy；
- `ComObjActive()` 被当成 `ComObjGet()`；
- `ComObjQuery` 不支持；
- `ComObjConnect` 不支持；
- `ComObjArray` 不支持。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_com_bif_linux.cpp))

D-Bus method call 使用 `dbus_connection_send_with_reply_and_block(..., -1, ...)`，也就是无限期同步等待；服务不响应时，AHK 主线程可能永久卡住。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_com_dbus_linux.cpp))

这是一个有用的 Linux-specific API 设计实验，但对依赖 Excel、Word、Shell.Application、WMI、IE/WebBrowser 或任意 Windows COM automation 的脚本，兼容度应视为接近零。

## 文件、进程和系统 API

纯文件、目录、进程启动、时间、字符串和数学通常会比桌面自动化层可靠，因为它们有直接 POSIX/Linux 映射。Windows-specific registry、service、shell verb、window message、process handle 和 COM 语义则无法由同名函数自动等价。

部分内部 platform helper 仍返回空值或 no-op，因此应逐个 API 做行为测试，不能以 `core_file_linux.cpp` 等文件存在推导整组 API 完成。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_platform_stubs.cpp))

**判断：中等置信度；本次审计重点在桌面自动化核心，没有逐行穷举所有文件 API。**

---

# 八、测试体系为什么会虚高

最新发布声称：

- 1143/1143 X11/headless doc-check；
- 17/17 native Wayland；
- 252/252 XWayland；
- 27/27 headless regression。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/releases/latest))

这些数字能够证明：

1. 当前实现满足项目自己编写的 assertion；
2. parser/runtime 和现有 Linux approximation 没有发生大量明显回归；
3. Xvfb、headless sway 和部分 package smoke path 可以启动；
4. 一些历史 bug 已有防回归用例。

它们不能证明：

1. 与 Windows AHK v2.0.26 行为一致；
2. 真实物理键盘、真实 IME、真实 layout 正常；
3. GNOME/KDE 桌面可靠；
4. 多脚本并发可靠；
5. external application 真正收到正确系统事件；
6. Control* 真正改变了目标应用，而非项目 virtual map；
7. 长时间运行无泄漏、死锁和 race；
8. 发布包在普通发行版上无需同 CI 一样补齐依赖。

## 8.1 典型 self-validation

Hotkey 测试大量使用同一个 `ahk_core` 的 `Send()` 去触发同一个 `ahk_core` 的 hotkey。InputHook 测试也主要用本项目的 `Send`/`SendText` 产生输入。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/tests/doccheck/assert_hotkey.ahk))

这容易形成：

> sender 和 receiver 对同一个错误约定达成一致，于是测试通过。

例如，如果发送方和捕获方都使用同一套错误的 US key map，测试仍然可以全绿。

## 8.2 Wayland CI 并不代表真实桌面

主 CI 运行在 Ubuntu 24.04，构建矩阵是 regular 和 ASan；Wayland 场景主要启动 headless sway，并明确使用 US layout。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/.github/workflows/ci.yml))

当 XWayland 不可用时，部分脚本会以 `PASS=0 FAIL=0` 正常退出；XWayland suite 也主动省略 Win/input/monitor 等受 WM 影响的测试。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/tests/doccheck/wayland_run.sh))

GNOME/KDE/Flatpak/inputd 场景在缺少真实宿主时会 skip；已知失败场景也不一定 gate CI。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/tests/scenarios/run_scenarios.sh))

## 8.3 Soak 并不是长时间可靠性测试

所谓 RSS/event-count soak 大致是少量 Send 循环、约几十秒级，而不是 24 小时或数日运行。没有看到：

- ThreadSanitizer；
- 输入 event fuzz；
- 多脚本注册/退出风暴；
- compositor restart；
- D-Bus service restart；
- input device hotplug stress；
- crash during EVIOCGRAB；
- IME composition fuzz；
- 高重复率 key repeat；
- 真实 GUI 应用矩阵。

## 8.4 弱 oracle 示例

最新 SNI scenario 启动通用 `dbus-monitor`，等待数秒，然后只要日志中出现任意 `StatusNotifierItem` 字样且脚本 marker 存在就判定通过。它没有严格关联本次 AHK 进程的 bus name、object path 或请求序列，同一桌面上其他 SNI item 可能造成 false positive。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/commit/eaeeaf74ad8501f4810e34240a2b6cb9e850d942))

**测试可信度结论：** 数量大、回归价值真实，但外部效应验证和差分兼容验证不足，不能用总断言数计算完成度。

---

# 九、问题分级

## P0：架构性 / 核心语义问题

### P0-1：输入 backend 没有统一事件与仲裁模型

**状态：已确认。**

**问题：** X11、portal、GNOME extension、evdev、XTEST、uinput、Wayland virtual keyboard 各自处理输入的一部分，但没有统一 event object、device identity、synthetic provenance、owner、suppression decision 和 lifecycle。

**代码证据：** `source/linux/core/input_backend.cpp` 的实际 dispatch/sync 只处理单个 `CurrentKind()`；capture 与 injection 分别在不同文件独立选择路径。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/input_backend.cpp))

**影响：** `SendLevel/InputLevel`、self-trigger、cross-script trigger、remap、passthrough 和 key-up 在不同 backend 上产生不同结果。

**触发场景：** portal 注册热键、uinput 注入；evdev 捕获、Wayland virtual-keyboard 注入；XWayland 捕获、原生 Wayland focus。

**当前测试为何未发现：** 测试通常固定一个 backend，并由同一进程发送和接收，不做 cross-backend matrix。

**建议：** 建立单一 `InputEvent` 模型和 input broker，事件至少携带 device、physical/synthetic、source script、send level、timestamp、scan code、logical key、text commit、suppression owner。

**修复复杂度：极高。**

---

### P0-2：X11 Hotstring/InputHook 的全键 passive-grab 架构无法支持稳健多脚本

**状态：已确认。**

**问题：** 任意 Hotstring/InputHook 会建立几乎全键 grab，再缓存和合成转发。相同 key/modifier grab 在多个 X client 间会冲突。

**代码证据：** `core_capture_linux.cpp:2–20`、`core_hotkey_linux.cpp:1083–1095`。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_capture_linux.cpp))

**影响：** 两个 AHK 脚本不能可靠同时使用 Hotstring/InputHook；脚本崩溃、挂起或重放异常会影响整个桌面输入。

**触发场景：** 同时运行文本扩展脚本和 InputHook 命令面板；与桌面全局快捷键或其他 automation tool 并存。

**测试为何未发现：** 测试主要单脚本运行，没有两个独立 `ahk_core` 同时竞争全部 grabs。

**建议：** 使用单实例 broker 统一拥有 capture device，并向多个脚本分发；X11 可结合 XI2 raw events，但 suppression 仍需要 broker 决策，不能让每个脚本各自 grab 全键。

**修复复杂度：极高。**

---

### P0-3：关键 AHK 键盘语义缺失

**状态：已确认。**

**问题：** scan-code hotkey 不支持；custom combo 不完整；remap、wildcard、key-up、左右 modifier、repeat、tilde 的组合语义未形成完整矩阵。

**代码证据：** `LinuxHotkeyKeycode()` 对无 VK 的 hotkey 返回 0；backend capability 也没有 scan-code/custom-combo 字段。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_hotkey_linux.cpp))

**影响：** 大量真实 AHK remap、游戏键盘、OEM 键、非美式布局、`CapsLock & j`、`a & b` 类脚本无法迁移。

**触发场景：** `sc01E::`、双角色 CapsLock、自定义 prefix、物理位置 remap。

**测试为何未发现：** API surface 测试可以验证 Hotkey 函数存在；有限测试没有覆盖完整语义笛卡尔积。

**建议：** 先定义 Windows v2.0.26 differential conformance suite，再实现 physical scan code → normalized key → layout text 三层模型。

**修复复杂度：高。**

---

### P0-4：原生 Wayland 没有 Hotstring/InputHook 所需的连续输入流

**状态：已确认。**

**问题：** portal 和 GNOME accelerator 都只能提供注册 shortcut 的 activation，不提供任意字符流、preedit 或未注册按键。

**代码证据：** portal/GNOME backend 调用 shortcut activation helper；Wayland capture 只能另走 evdev。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/input_backend.cpp))

**影响：** 原生 Wayland 上核心 AHK 使用场景——文本扩展、按键序列、InputHook 命令输入——不存在通用非特权实现。

**触发场景：** GNOME/KDE 原生应用中运行 `:*:btw::by the way` 或 `InputHook()`。

**测试为何未发现：** native-Wayland assertion 数量少，且主要测试注册、注入和项目自身 harness，不验证真实连续用户输入。

**建议：** 明确产品层级：portal shortcut backend 只声明 shortcut-only；完整 input semantics 必须通过授权 broker、evdev 或 compositor-specific extension，并在 capability API 中诚实暴露。

**修复复杂度：极高。**

---

### P0-5：SendLevel / InputLevel / synthetic-event semantics 不兼容

**状态：已确认。**

**问题：** 合成事件身份是进程本地启发式，且显式 `SendInput()` 与 `SendMode("Input") + Send()` 存在不同 self-trigger 行为。

**代码证据：** `core_input_linux.cpp` 的 self-track/suppress 分支及 `core_capture_linux.cpp` 明确表示 Hotstring SendLevel 未建模。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_input_linux.cpp))

**影响：** 防递归脚本、层级 remap、多脚本宏、热字串 replacement 和 hook hotkey 可能递归、漏触发或误触发。

**触发场景：** 一个脚本 `SendLevel 1`，另一个设置 `#InputLevel`；hotstring replacement 触发另一个 hotstring。

**测试为何未发现：** 主要是同进程、单 backend、短序列，不覆盖跨脚本 provenance。

**建议：** broker 分配不可伪造的 injection source ID；为每个 event 附加 level，并建立与 Windows AHK v2.0.26 的差分测试。

**修复复杂度：极高。**

---

## P1：严重兼容性或可靠性问题

### P1-1：Unicode 与 IME 方案会改变全局状态，且不理解 composition

**状态：已确认。**

**问题：** X11 Unicode 修改 server-wide keymap并等待固定 30 ms；Wayland fallback 可能接管剪贴板；IME 仅做 group/owner 近似。

**代码证据：** `LinuxSendCharUnicode()`、`LinuxSendRunPaste()`、`core_ime_linux.cpp`。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_input_linux.cpp))

**影响：** 中文、日文、emoji、多布局输入可能丢字、错字、污染剪贴板或破坏 Hotstring buffer。

**触发场景：** 中文聊天、浏览器密码框、IBus/Fcitx、快速连续 SendText、并发程序监听 keymap。

**测试为何未发现：** CI 固定 US layout，没有真实 IME preedit/commit。

**建议：** 使用 xkbcommon 做 layout-aware 解析；接入 IBus/Fcitx commit/preedit；优先 libei/uinput Unicode-capable path；彻底移除 server keymap borrowing 作为默认方案。

**修复复杂度：高。**

---

### P1-2：Win*/Control* 的表面实现与外部应用效果不一致

**状态：已确认。**

**问题：** 顶层窗口枚举简化；WinText 能力弱；大量 Control 状态是本进程 virtual map；ControlSend 通过临时抢 focus 实现。

**代码证据：** `core_win_linux.cpp:241–253`、`core_ctrl_linux.cpp:962–997` 及 list virtual operations。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_win_linux.cpp))

**影响：** 测试中 set/get 一致，但真实 GTK/Qt/Electron 控件没有变化；后台自动化不可靠。

**触发场景：** 自动填写后台窗口、读取列表、切换 checkbox、操作浏览器/Electron/LibreOffice。

**测试为何未发现：** 很多测试验证项目自己的状态返回，而非目标应用 UI state。

**建议：** X11 使用 EWMH client list、WM protocols 和 AT-SPI；对无法真实完成的 Control API 返回明确 `NotSupportedError`，不要用 virtual map 冒充系统状态。

**修复复杂度：高。**

---

### P1-3：evdev suppression 会 grab 整个键盘并重放未匹配事件

**状态：已确认。**

**问题：** EVIOCGRAB 是设备级独占；项目随后通过 uinput 重放未 suppression 的键。

**代码证据：** `core_evdev_linux.cpp` 设备 grab、panic sequence 和 replay 路径；header 也明确说明所需权限。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/source/linux/core/core_evdev_linux.cpp?utm_source=chatgpt.com))

**影响：** 进程挂起、调度延迟或错误匹配会造成全桌面键盘卡顿、丢键或重复键；需要 `/dev/input/event*` 与 `/dev/uinput` 权限。

**触发场景：** 高 key repeat、CPU stall、脚本 callback 阻塞、input device hotplug、进程未正常退出。

**测试为何未发现：** GitHub runner 无真实主键盘；headless 测试不能模拟用户被锁键盘的风险。

**建议：** 把 grab 放入最小特权、独立 watchdog broker；脚本进程断开立即 fail-open；加入 systemd socket activation、权限 policy、设备热插拔和 crash tests。

**修复复杂度：高。**

---

### P1-4：X11 grab 冲突记录错误，且使用进程全局 X error handler

**状态：已确认。**

**问题：** 失败 grab 仍进入 `sInstalled`；`XSetErrorHandler` 是进程级全局状态，不是某 Display/线程私有。

**代码证据：** `ScopedXErrorTrap` 及 pending 全量插入逻辑。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_hotkey_linux.cpp))

**影响：** 冲突释放后不重试；与 GTK 或其他 Xlib 使用并发时，error handler 可能互相覆盖。

**触发场景：** 两脚本争用同一 hotkey、桌面快捷键动态变化、GUI 线程同时执行 X call。

**测试为何未发现：** 单进程顺序测试；没有冲突 owner 退出后重新获取的测试，也没有 TSan。

**建议：** 每个 request 精确记录 success/failure，仅成功 spec 加入 installed；集中所有 Xlib 操作到单线程；避免临时覆盖全局 handler 或建立统一 error dispatcher。

**修复复杂度：中高。**

---

### P1-5：D-Bus/AT-SPI 调用阻塞脚本主线程

**状态：已确认。**

**问题：** COM-over-D-Bus 使用无限期 blocking call；AT-SPI tree walk 对节点执行同步数秒调用。

**代码证据：** `dbus_connection_send_with_reply_and_block(..., -1, ...)`；AT-SPI per-object synchronous properties/text calls。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_com_dbus_linux.cpp))

**影响：** 一个卡住的 D-Bus service 或 accessibility object 可冻结整个脚本，包括 hotkey dispatch、grab cleanup 和 GUI。

**触发场景：** 应用无响应、service restart、AT-SPI bridge 卡住、大型浏览器 accessibility tree。

**测试为何未发现：** mock/正常 service 立即返回；无故障注入和 timeout test。

**建议：** 全部改为异步请求 + AHK event loop integration；设置总预算、节点预算、取消 token；service owner change 后重建 connection/cache。

**修复复杂度：高。**

---

### P1-6：当前测试无法证明真实桌面行为

**状态：已确认。**

**问题：** 大量 sender/receiver 同源；Wayland 主要 headless sway；GNOME/KDE 按环境 skip；soak 太短；无 differential Windows oracle。

**代码证据：** CI workflow、assert_hotkey/InputHook、wayland runner、scenario gate。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/.github/workflows/ci.yml))

**影响：** 发布数字可能持续增长，但最重要的现实兼容缺口保持不变。

**触发场景：** 项目以“全部测试通过”判断某 backend 已完成。

**当前测试为何未发现：** 这本身就是测试设计问题。

**建议：** 建立外部 oracle：独立 evdev/XI2 recorder、真实应用状态读取、Windows 2.0.26 差分 trace、GNOME/KDE/sway VM hardware-in-loop。

**修复复杂度：中高。**

---

## P2：重要但不阻塞核心使用

### P2-1：AT-SPI Unicode 和目标限定错误

**状态：已确认。**

**问题：** UTF-8 非 ASCII byte 被逐个替换成 `?`；Control 查询不充分使用 WinTitle。

**代码证据：** `core_ctrl_linux.cpp:395–427`。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_ctrl_linux.cpp))

**影响：** 中文控件文本无法读取；同名控件可能选错应用。

**触发场景：** 多窗口同名 Button/Edit、中文 GTK/Qt 应用。

**测试为何未发现：** 测试以 ASCII accessible name 为主。

**建议：** 正确 UTF-8→UTF-32/wchar 转换；建立 application/window accessible root，再在其子树内查找 control。

**修复复杂度：中。**

---

### P2-2：GUI API 静默弱化

**状态：已确认。**

**问题：** 部分不支持 option 只 warning；ActiveX 返回空 drawing area。

**代码证据：** GTK GUI option 和 ActiveX 分支。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/gui/script_gui_linux.cpp))

**影响：** 旧脚本看似启动，但 UI 行为悄悄改变；错误发现较晚。

**触发场景：** 依赖 ActiveX/WebBrowser、Windows-specific style、精确 message/event ordering。

**测试为何未发现：** 测试只验证 control 可创建或 property 可读。

**建议：** 明确区分兼容控件、Linux-native 替代和 unsupported；默认对语义性 option 抛错，而不是继续运行。

**修复复杂度：中。**

---

### P2-3：DllCall 与 COM 使用同名 API 表达新的 Linux 语义

**状态：已确认。**

**问题：** `Str` ABI、库名、calling convention、COM object model 都与 Windows 不同。

**代码证据：** `core_dllcall_linux.cpp`、`core_com_bif_linux.cpp`。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_dllcall_linux.cpp))

**影响：** Windows AHK 脚本不能仅改 DLL 文件名后运行；使用同名 API 会给用户过强兼容暗示。

**触发场景：** `DllCall("user32\...")`、BSTR、struct packing、COM events、SafeArray。

**测试为何未发现：** Linux-native D-Bus/.so 测试验证的是新功能，不是 Windows 行为。

**建议：** 文档与 capability API 明确标记 `LinuxNativeFFI`/`DBusProxy`；对 Windows DLL/COM 调用立即给迁移错误。

**修复复杂度：中。**

---

### P2-4：发布签名缺少稳定发布者身份

**状态：已确认。**

**问题：** 当 secret key 不存在时，打包脚本会现场生成一个永久不过期但临时的新 RSA key，并把公钥和产物一起发布。

**代码证据：** `tools/linux/pack-finalize.sh:43–58`。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/tools/linux/pack-finalize.sh))

**影响：** 用户只能验证“签名与同包附带公钥匹配”，不能验证“这是长期可信 maintainer 发布的包”。攻击者若能替换 artifact，也能替换公钥和签名。

**触发场景：** CI 无持久 signing secret，用户从镜像或第三方下载。

**测试为何未发现：** package acceptance 只验证文件存在/可运行，不验证信任链。

**建议：** 使用长期离线主密钥/受保护 release subkey、Sigstore/GitHub artifact attestations、固定 fingerprint 和可验证 provenance。

**修复复杂度：中。**

---

### P2-5：Clipboard change/type 与非文本格式覆盖不足

**状态：已确认。**

**问题：** X11 类型近似；Wayland change notification 依赖 GNOME extension；无完整 MIME/data-object 模型。

**代码证据：** `core_clipboard_linux.cpp` X11 owner/type 和 Wayland extension dispatch。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/eaeeaf74ad8501f4810e34240a2b6cb9e850d942/source/linux/core/core_clipboard_linux.cpp))

**影响：** 图片、文件列表、自定义 MIME、剪贴板管理器和 KDE/wlroots change notification 不兼容。

**触发场景：** `A_Clipboard` 之外的 ClipboardAll、图片工作流、跨应用文件复制。

**测试为何未发现：** 以 text clipboard 为主。

**建议：** 使用 MIME-aware clipboard object；各桌面实现 portal/data-control/extension 能力时明确返回 type；不可监听时报告 capability unavailable。

**修复复杂度：中高。**

---

## P3：体验、文档或长期优化问题

### P3-1：源码注释、capability table 和实际行为漂移

**状态：已确认。**

**问题：** 部分注释仍描述旧 InputHook 限制；backend cap 把 X11 标成全 true，却无法表达 scan-code/custom-combo 缺口；代码注释直接引用 `check0820/check_detail0821` 作为设计依据。

**影响：** 开发者和用户容易把“capability=true”理解为完整 AHK semantics；旧审计结论反向侵入实现和文档。

**触发场景：** 用户根据 `HotkeyBackendGet()` 自动选择 backend。

**测试为何未发现：** capability 测试多为字段值测试，没有行为 contract。

**建议：** capability 改成版本化、细粒度、可实测结构，例如 `raw_key_stream`、`scan_code`、`custom_combo`、`suppression`、`text_commit`、`synthetic_provenance`、`multi_owner`。

**修复复杂度：中。**

---

# 十、接下来最值得做的 10 项工作

排序依据是 **用户价值 × 基础性 × 风险降低 ÷ 实现成本**，不是仓库现有 roadmap 顺序。

|   排名 | 工作                                                         | 为什么优先                                                   |
| -----: | ------------------------------------------------------------ | ------------------------------------------------------------ |
|  **1** | **定义 AHK v2.0.26 输入一致性规范和 differential trace suite** | 没有 oracle，继续增加 backend 只会积累互不兼容的近似实现     |
|  **2** | **实现单一 input broker：多脚本仲裁、crash-safe grab、event provenance** | 同时解决多脚本、权限、资源释放、SendLevel 和 evdev 风险，是输入架构基础 |
|  **3** | **完成 physical scan code / logical key / text commit 三层键模型** | 是 scan code、布局、custom combo、IME、remap 的共同前提      |
|  **4** | **统一 synthetic-event、SendLevel、InputLevel 和递归抑制**   | 当前最容易造成脚本逻辑错误且难以调试的核心语义               |
|  **5** | **重做 X11 Hotstring/InputHook capture，移除每脚本全键 grab/replay** | X11 是最接近可用的平台，也是最容易先达到“日常可用”的路径     |
|  **6** | **建立标准 Wayland 注入主路径：RemoteDesktop portal + libei/EIS** | 比依赖 wlroots 私有协议或默认 root/uinput 更可维护、更符合现代桌面安全模型 |
|  **7** | **按 GNOME/KDE/wlroots 分离 capability 与实现，不再统一称 Wayland support** | 防止错误 fallback；让脚本能可靠知道当前到底支持什么          |
|  **8** | **实现 layout-aware Unicode 与 IBus/Fcitx preedit/commit 集成** | 中文、日文和非美式键盘是 Linux 桌面真实用户的基本需求，不是边缘功能 |
|  **9** | **重构 Win*/Control*：EWMH + AT-SPI，删除虚拟外部控件状态**  | 宁可明确 unsupported，也不能让 API 返回成功但不改变目标应用  |
| **10** | **建设真实桌面可靠性实验室**                                 | GNOME、KDE、sway；真实 GTK/Qt/Electron/浏览器；双脚本；24h soak；TSan；设备 hotplug；compositor/D-Bus restart；包升级/卸载测试 |

其中第 1 项应先于继续增加函数数量。否则测试数字会上升，但真实兼容度未必上升。

---

# 最终判断

这个仓库的价值在于，它已经证明：

> AutoHotkey v2 的相当一部分 interpreter core 可以在 Linux 上运行，也可以围绕 GTK、X11、D-Bus、Wayland protocol、AT-SPI 和 evdev 构造出一组可调用的 Linux automation API。

但它尚未证明：

> Windows AHK v2 用户最依赖的“全局输入语义 + 窗口/控件自动化 + 长期可靠运行”可以在 Linux 上保持一致。

当前最大差距不再是“还缺几十个函数”，而是三个基础系统尚未完成：

1. **统一、可仲裁、可追踪的输入模型；**
2. **按 compositor 和权限模型诚实分层的 Wayland 架构；**
3. **以外部真实效果和 Windows 差分行为为 oracle 的测试体系。**

因此，最准确的定位是：

> **一个实现面很宽、工程投入显著、部分 X11 场景已经有实用价值的 technology preview；距离真正可让普通 AHK v2 用户迁移真实桌面自动化脚本并长期可靠运行，仍有一轮输入架构重构、一轮窗口/控件语义收敛和一轮真实桌面验证工程。**