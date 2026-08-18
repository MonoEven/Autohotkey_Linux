## 总体判断

截至 **2026 年 8 月 17 日**，当前 `linux-port` 分支的热键实现更接近“简单 X11 全局快捷键原型”，还不能视为 AutoHotkey 热键语义的完整迁移。问题不主要在解析器：代码复用了上游 `Hotkey()`、变体和 `HotIf` 解析；真正薄弱的是 **平台后端的注册生命周期、事件路由、按键抑制/透传，以及 Wayland 能力分层**。仓库自己的弱化审计也承认鼠标热键、自定义组合、`~`、`*`、扫描码和部分 `up` 语义尚未落地。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/raw/refs/heads/linux-port/tests/doccheck/AUDIT_2026_WEAKENED.md))

以下审计以热键及其相邻的输入、事件循环、Wayland 和 CI 代码为范围，属于静态代码审计。

---

# 一、代码审计结果

## P0：热键模块会吞掉不属于它的 X11 事件

`LinuxDispatchHotkeys()` 使用：

```cpp
while (XPending(d) > 0) {
    XNextEvent(d, &ev);
    if (ev.type != KeyPress && ev.type != KeyRelease)
        continue;
}
```

也就是说，它把该 X11 连接上的所有事件取空，非键盘事件直接丢弃。与此同时，`LinuxX11Display()` 返回的是窗口模块的共享连接，主循环又先调用热键派发，再调用剪贴板派发。因此 `SelectionRequest`、`SelectionNotify`、`PropertyNotify`、窗口事件等很可能在剪贴板或窗口模块看到之前就被热键模块消费。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/raw/refs/heads/linux-port/source/linux/core/core_hotkey_linux.cpp))

**影响：**

- 脚本注册热键后可能间歇性破坏 X11 剪贴板所有权响应。
- GUI、Tooltip、窗口属性通知等也存在事件丢失风险。
- 事件多少和时序不同，问题可能只在真实桌面出现，Xvfb 测试不容易稳定复现。

**必须修改：**

为热键后端建立独立的 `XOpenDisplay()` 连接，主循环分别轮询：

```text
window/clipboard display fd
hotkey display fd
GTK fd
timer deadline
```

热键连接只负责抓取事件、键盘映射通知和热键错误，不与剪贴板、窗口模块共享事件队列。

---

## P0：`Hotkey(..., "Off")` 没有解除系统级按键抓取

`LinuxUpdateHotkeyGrabs()` 内部使用静态 `std::set<Hotkey *> sGrabbed`，只添加、不删除；注册时也没有检查当前热键是否启用、是否因 Suspend 失效、是否仍有启用变体。代码中不存在对应的 `XUngrabKey()`。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/raw/refs/heads/linux-port/source/linux/core/core_hotkey_linux.cpp))

现有测试只验证关闭后回调计数不增加：

```ahk
Hotkey("F7", "Off")
Send("{F7}")
Log("hk_off=" (cnt1 = 1 ? 1 : 0))
```

它没有验证 F7 是否被正常传递到另一个前台应用。因此测试会把“回调没执行，但按键仍被 AHK 进程独占”误判为成功。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/raw/refs/heads/linux-port/tests/doccheck/assert_hotkey.ahk))

**修复方式：**

不要缓存 `Hotkey *`，而应缓存实际抓取描述：

```cpp
struct GrabSpec {
    KeyCode keycode;
    unsigned modifiers;
    bool operator<(const GrabSpec &) const;
};

std::set<GrabSpec> installed;
```

每次热键添加、删除、On、Off、Toggle、Suspend、恢复或键盘布局变化后：

1. 根据当前有效变体构造 `desired`。
2. `installed - desired` 执行 `XUngrabKey()`。
3. `desired - installed` 执行 `XGrabKey()`。
4. 成功后才更新 `installed`。

---

## P0：`HotIf` 不成立、热键关闭或回调受限时，原始按键仍会被丢弃

当前注册参数是：

```cpp
XGrabKey(..., False, GrabModeAsync, GrabModeAsync);
```

事件到达后，如果 `FindVariant()` 返回空、变体未启用或 `PerformIsAllowed()` 为假，代码只是 `continue`，没有任何重放或透传逻辑。X11 的被动抓取会把匹配事件交给抓取客户端；要在判断后交还给正常目标客户端，需要同步抓取并使用 `XAllowEvents(..., ReplayKeyboard, ...)` 等机制。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/raw/refs/heads/linux-port/source/linux/core/core_hotkey_linux.cpp))

因此这些情况都可能导致前台应用收不到按键：

- `#HotIf` 当前为假。
- 变体被 `Off`。
- 热键线程数或优先级限制阻止回调。
- 带 `~`、本应透传的热键。
- `Key up` 热键收到对应的 KeyPress 时。

这是目前最严重的语义问题之一。

**建议分两类处理：**

- 简单按下型、需要抑制的热键：同步被动抓取，解析结果后决定消费还是 `ReplayKeyboard`。
- `~`、纯观察、Key-up 等不需要抑制的热键：优先通过 XI2 RawKeyPress/RawKeyRelease 观察，不建立会阻断前台应用的被动抓取。

自定义组合和“是否抑制取决于运行时条件”的情况需要单独状态机，不能继续用“抓住以后不匹配就丢弃”的模型。

---

## P0：热键冲突会被静默吞掉

X11 文档明确说明：相同组合已被其他客户端抓取时，`XGrabKey()` 会产生 `BadAccess`。但当前窗口模块安装了全局 Xlib 错误处理器，将所有协议错误直接忽略；热键模块又无条件把 `Hotkey *` 放入 `sGrabbed`。因此一个抓取失败的热键会在内部显示为“已注册”，但永远不会触发。([xorg](https://www.x.org/releases/X11R7.6-RC1/doc/man/man3/XGrabKey.3.xhtml?utm_source=chatgpt.com))

**建议：**

- 最稳妥方案是热键连接改用 XCB checked request，例如 `xcb_grab_key_checked()`。
- 继续使用 Xlib 时，至少实现序列号范围明确的 `ScopedXErrorTrap`，在 `XSync()` 后读取 `BadAccess`。
- 注册失败应向脚本报告明确的 `OSError`，包含键名、修饰键和冲突原因。
- 只有服务器确认成功后，才能写入 `installed`。

全局“忽略全部 X11 错误”的设计也应逐步移除；很多 Xlib 请求的错误是异步产生的，调用者并不能只靠普通返回值检查。

---

## P1：初次抓取存在启动时序风险

`LinuxUpdateHotkeyGrabs()` 只在 `LinuxDispatchHotkeys()` 开头调用；而主循环只有在 X fd 已可读时才调用 `LinuxDispatchHotkeys()`。这形成了一个潜在的冷启动循环：

```text
热键尚未 XGrabKey
→ 没有热键事件进入该连接
→ poll 不返回
→ LinuxDispatchHotkeys 不运行
→ 热键一直未抓取
```

现有测试在每次创建热键后都先 `Sleep(200)`，这可能掩盖了该问题，因为等待路径会额外泵事件。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/raw/refs/heads/linux-port/source/linux/core/core_hotkey_linux.cpp))

**修复：**

- `BIF_Linux_Hotkey` 成功返回后立即执行 `backend->Reconcile()`。
- 进入主循环前无条件执行一次 `Reconcile()`。
- `Reconcile()` 不能依赖先收到 X 事件。

---

## P1：修饰键和锁定键映射写死，不适用于真实用户键盘配置

代码假设：

```text
Alt     = Mod1Mask
Super   = Mod4Mask
NumLock = Mod2Mask
Caps    = LockMask
```

但 X11 的 Mod1–Mod5 是可配置的，NumLock、Alt、Super 可能被映射到不同槽位；键盘映射改变时服务器还会发送 `MappingNotify`。当前实现既没有查询 `XGetModifierMapping()`，也没有处理映射变化。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/raw/refs/heads/linux-port/source/linux/core/core_hotkey_linux.cpp))

**修复：**

启动及 `MappingNotify` 时：

1. 调用 `XGetModifierMapping()`。
2. 通过各 modifier 槽中的 keycode 查找 `XK_Num_Lock`、`XK_Scroll_Lock`、`XK_Alt_L/R`、`XK_Super_L/R` 等。
3. 动态建立 `altMask`、`superMask` 和 `ignoredLockMask`。
4. 键盘映射变化后完整解除并重建 grabs。

不要只枚举 CapsLock 和 Mod2 的四种组合；应根据实际锁定掩码枚举其幂集。

---

## P1：AutoHotkey 已解析的热键属性在 Linux 派发阶段被忽略

当前派发只比较：

```cpp
hk->mKeyUp
hk->mVK
hk->mModifiers
```

但没有处理：

- `mModifiersLR`：左/右 Ctrl、Alt、Shift、Win。
- `mAllowExtraModifiers`：`*` 通配热键。
- 变体的 `mNoSuppress`：`~`。
- 扫描码。
- 鼠标键。
- 自定义前缀 `A & B`。
- 多个候选热键的优先级和唯一决议。

仓库文档一方面宣称复用了完整的 Hotkey 解析和变体逻辑，另一方面弱化审计承认这些语法没有对应实现；更合理的行为不是“接受后悄悄改变语义”，而是根据后端能力明确拒绝。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/raw/refs/heads/linux-port/tests/doccheck/CHECK_REPORT.md))

建议定义能力位：

```cpp
enum HotkeyCapability {
    GlobalActivation,
    Suppression,
    PassThrough,
    KeyUp,
    LeftRightModifiers,
    WildcardModifiers,
    ScanCode,
    PrefixCombo,
    MouseButton,
    Wheel
};
```

解析完成后先进行 capability validation。无法支持的语法应抛出清楚的错误，例如：

```text
The X11 passive-grab backend does not support custom prefix hotkeys.
```

而不是静默注册一个永远不触发或语义错误的热键。

---

## P1：Key-up 热键会受到 X11 自动重复的影响

当前每个 `KeyRelease` 都可触发 `mKeyUp` 热键。标准 X11 自动重复模式可能为每次重复产生一对合成的 KeyRelease/KeyPress，因此按住按键时，“up” 热键可能在物理松开之前反复执行。XKB 提供 `XkbSetDetectableAutoRepeat()`，启用后只有物理释放才产生 KeyRelease。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/raw/refs/heads/linux-port/source/linux/core/core_hotkey_linux.cpp))

**修复：**

- 热键专用连接建立后调用 `XkbSetDetectableAutoRepeat(dpy, True, &supported)`。
- 若服务器不支持，则检测相同时间戳、相同 keycode 的相邻 Release/Press 对并过滤合成 Release。
- 增加“按住 F9 一秒，只在物理松开时触发一次”的集成测试。

---

## P1：Wayland 键码映射存在确定的 off-by-one 和非连续区间错误

`LinuxWaylandKeycodeForVk()` 有三个直接可见的问题：

```cpp
'0'..'9' → KEY_1 + offset
F1..F24  → KEY_F1 + offset
0x6B     → KEY_KPASTERISK
0x6C     → KEY_KPPLUS
```

Linux evdev 中数字键顺序是 `KEY_1 ... KEY_9, KEY_0`，并非从 0 开始连续；F11 紧跟在 F10 后也不成立，`KEY_F10` 后是 NumLock、ScrollLock 和小键盘键；Windows VK 中 `0x6A` 才是 Multiply，`0x6B` 是 Add。当前代码会错误发送数字 0–9、F11 以上以及部分小键盘按键。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/raw/refs/heads/linux-port/source/linux/core/core_wayland_linux.cpp))

应改为显式表，而不是算术偏移：

```cpp
static constexpr unsigned digits[] = {
    KEY_0, KEY_1, KEY_2, KEY_3, KEY_4,
    KEY_5, KEY_6, KEY_7, KEY_8, KEY_9
};

static constexpr unsigned function_keys[] = {
    KEY_F1, KEY_F2, /* ... */ KEY_F24
};

case 0x6A: return KEY_KPASTERISK;
case 0x6B: return KEY_KPPLUS;
case 0x6C: return KEY_KPCOMMA; // 或明确不支持 VK_SEPARATOR
```

虽然这是发送模块的 bug，不是捕获模块的 bug，但目前热键测试使用 `Send()` 触发热键，它会让测试结果本身变得不可信。

---

## P1：现有测试是“同一实现测试自身”

热键测试由 AHK 进程调用自身的 `Send()`，通过 XTEST 产生事件，再由同一进程的 XGrabKey 路径接收。它没有覆盖：

- 真实前台 X 客户端能否收到未被抑制的键。
- 热键 Off 后是否解除抓取。
- `HotIf` 为假时是否透传。
- 抓取冲突。
- 键盘布局和 modifier 重映射。
- 按键长按和自动重复。
- 热键与剪贴板事件同时到达。
- 独立进程产生的事件。

测试脚本本身也明确说明所有事件由 `Send`/XTEST 回送。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/raw/refs/heads/linux-port/tests/doccheck/assert_hotkey.ahk))

此外，CI 的 Xvfb doc-check 使用了 `continue-on-error: true`，热键和其他 X11 回归即使失败也不会阻止合并。专门的 XWayland 热键脚本没有进入 CI，而且包含开发者机器上的硬编码路径。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/raw/refs/heads/linux-port/.github/workflows/ci.yml))

---

## P2：文档对 Wayland 热键能力的描述互相矛盾

README 一处称原生 Wayland 支持“modifier-combo hotkey-style bindings”，另一处又明确说纯 Wayland 中热键不可用；实际 `BIF_Linux_Hotkey` 会在纯 Wayland 下直接报错。Wayland 代码中所谓“hotkey-style bindings”实际上是把虚拟键盘事件发送给合成器，使合成器自身的绑定可能响应，它并不是监听用户全局按键。([GitHub](https://github.com/MonoEven/Autohotkey_Linux))

建议文档严格区分：

```text
Input injection             向桌面发送按键
Compositor shortcut trigger 注入事件恰好触发合成器绑定
Global shortcut registration 应用注册全局快捷键
Global keyboard hook        观察所有物理键盘事件
Input suppression           阻止事件到达目标应用
```

目前只实现了前两者中的部分能力。

---

# 二、推荐的迁移架构

## 1. 把上游语义和平台机制彻底分离

建议建立：

```text
HotkeyParser / HotkeyVariant / HotIf
                │
                ▼
        HotkeyResolver
  统一处理优先级、变体、线程限制、
  Suspend、On/Off、SendLevel、抑制策略
                │
                ▼
        IHotkeyBackend
        ├── X11PassiveGrabBackend
        ├── X11RawHookBackend
        ├── WaylandPortalBackend
        └── EvdevBrokerBackend（可选）
```

平台后端只输出标准化事件：

```cpp
struct PlatformKeyEvent {
    PhysicalKey key;
    ModifierState modifiers;
    bool is_down;
    bool is_repeat;
    uint64_t timestamp;
    DeviceId device;
};
```

不要让 X11 后端直接遍历 `Hotkey::shk` 并启动脚本线程；否则所有平台都必须重复 AutoHotkey 的候选决议逻辑。

---

## 2. X11 分为“简单快捷键”和“高级钩子”两条路径

### 简单快捷键后端

适用：

- 固定修饰键。
- 单个键。
- 按下触发。
- 不依赖扫描码、前缀和鼠标。

实现重点：

- 独立 X 连接。
- desired/current grab diff。
- 抓取冲突检查。
- 动态 modifier map。
- 哈希表查找，不再每个事件遍历全部热键。
- 同步抓取与条件重放。
- 布局变化重建 grabs。

### 高级钩子后端

适用：

- `~`。
- Key-up。
- 左右修饰键。
- 通配修饰键。
- 自定义前缀。
- 鼠标和滚轮。
- KeyHistory、InputHook、Hotstring。

可使用 XI2 RawKeyPress/RawKeyRelease 建立全局观察流；需要抑制的少数组合再辅以 XGrabKey/XGrabButton。纯 Raw Events 只能观察，不能阻止事件，因此应在 capability matrix 中如实标注。

对于前缀缓冲、扫描码级抑制、任意条件抑制等最困难语义，可以先明确标为“不支持”，不要为追求表面兼容而吞键。

---

## 3. Wayland 首选 XDG Global Shortcuts Portal

README 中“Wayland 没有全局热键协议”的结论已经不完整。XDG Desktop Portal 现在有正式的 `org.freedesktop.portal.GlobalShortcuts` 接口，可以创建 session、绑定快捷键，并接收 `Activated`、`Deactivated` 信号；快捷键在应用未聚焦时也能激活。([Flatpak](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.GlobalShortcuts.html?utm_source=chatgpt.com))

仓库已经有 D-Bus 基础设施，因此可新增：

```text
WaylandPortalBackend
  CreateSession
  BindShortcuts
  ListShortcuts
  Activated → HotkeyResolver
  Deactivated → key-up 近似
```

但 Portal 只能作为 **用户授权、应用级全局快捷键后端**，不能宣称具备完整 AHK hook 语义：

- 通常不能任意监听所有键。
- 不能可靠抑制按键。
- 不适合 Hotstring/InputHook/KeyHistory。
- 绑定可能由用户或桌面环境重新配置。
- 需要运行时检测 portal 及 backend 是否可用。

---

## 4. 完整 Wayland 钩子作为可选特权服务

确实需要 Hotstring、任意前缀和输入抑制时，可设计独立的 `ahk-input-broker`：

```text
/dev/input/event* → libevdev → 状态机
                            ├→ AHK Unix socket
                            └→ /dev/uinput 重放未抑制事件
```

Linux 内核的 uinput 正是用于从用户空间创建虚拟输入设备；官方文档也建议新软件优先使用 libevdev 包装，而不是直接手写 uinput ioctl。([Kernel.org](https://www.kernel.org/doc/html/latest/input/uinput.html?utm_source=chatgpt.com))

该后端必须是显式可选项，并建立清晰安全边界：

- 默认不要求 root。
- 通过 udev 规则、专用组或 Polkit 授权。
- broker 与解释器使用 Unix socket 和进程凭据校验。
- 虚拟设备事件必须标记并过滤，防止重放后再次被自身捕获。
- 脚本只能注册明确的规则，不能直接获得全部原始键盘内容，除非用户授予高级权限。

---

# 三、应立即补充的测试

最关键的是增加一个独立 X11 测试客户端，它创建窗口、获得焦点并记录实际收到的 KeyPress/KeyRelease。然后覆盖以下场景：

| 场景 | 期望 |
|---|---|
| 普通 `F7` 热键 | 回调执行，前台客户端不收到 |
| `~F7` | 回调执行，前台客户端收到 |
| `Hotkey("F7", "Off")` | 回调不执行，前台客户端收到 |
| `#HotIf false` | 回调不执行，前台客户端收到 |
| 抓取冲突 | 注册返回明确错误 |
| Caps/Num/Scroll Lock | 均正常触发 |
| NumLock 映射到 Mod3 | 仍正常触发 |
| `setxkbmap us/de/fr` | 键名行为符合定义 |
| 运行中切换布局 | 收到 MappingNotify 后重建 |
| 长按 `F9 up` | 物理松开时仅触发一次 |
| 热键激活时读取/提供剪贴板 | 剪贴板请求不丢失 |
| 1000 个热键 | 派发时间不随总数线性恶化 |

CI 中应去掉 Xvfb doc-check 的 `continue-on-error`，并将 XWayland 专项测试改为使用传入的二进制路径，而不是硬编码本地目录。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/raw/refs/heads/linux-port/.github/workflows/ci.yml))

---

# 四、推荐提交顺序

**第一批先解决正确性：**

1. 热键使用独立 X11 Display。✅ round-29
2. 把静态 `set<Hotkey *>` 改为 GrabSpec 差量同步。✅ round-29
3. 实现 `XUngrabKey`。✅ round-29
4. 捕获并报告 `BadAccess`。✅ round-29
5. 注册成功后立即 Reconcile。✅ round-29
6. 增加 Off、HotIf-false 和剪贴板并发透传测试。✅ round-29 (`assert_hotkey_pt`)
7. 修复 Wayland 数字、F11–F24 和小键盘映射。✅ round-29
8. 让 Xvfb 测试失败阻止 CI。✅ round-29

**第二批补齐简单热键语义：**

- 动态 modifier map。✅ round-29
- MappingNotify。✅ round-29
- XKB detectable autorepeat。✅ round-29
- 左右修饰键和 wildcard。⏳ 计划(需 XI2 raw 观察层)
- 哈希索引与唯一候选决议。⏳ 计划

**第三批扩展平台能力：**

- XI2 原始事件观察后端。⏳ 计划（`~`/Key-up/观察类热键的理想路径）
- XDG Global Shortcuts Portal。⏳ 计划（Wayland 全局快捷键）
- 鼠标热键。⏳ 计划（XGrabButton 可先支持按钮）
- 可选 evdev/uinput broker。⏳ 计划（完整钩子语义，需安全边界）
- 最后再基于统一事件流实现 InputHook 和 Hotstring。🔄 Hotstring 已在 round-32 经按键捕获引擎先行落地(见 AUDIT §2.1);InputHook 实时采集仍待接入统一事件流。

---

# 五、解决状态(round 29 起,随实现更新)

## 第一批(正确性)——已完成(round 29)

| 问题 | 状态 | 实现/验证 |
|---|---|---|
| P0 事件吞噬(共享 X 连接) | ✅ 已解决 | 独立热键 X 连接 `LinuxHotkeyDisplay()`;主循环/MsgSleep 分别 poll 窗口/剪贴板与热键两个 fd |
| P0 `Off` 不解除抓取 | ✅ 已解决 | `GrabSpec` desired/installed 差量同步 + `XUngrabKey`;Off/禁用变体/完全禁用即解除(`IsCompletelyDisabled`) |
| P0 条件不满足时吞键 | ✅ 已解决 | Async 抓取 + 不匹配时 `XUngrabKeyboard` + XTEST 重注入透传;8 槽注入日志防循环(两个 X 连接事件乱序,单标记会失效)。同步抓取方案已否决:冻结事件不通知抓取客户端,必然死锁 |
| P0 BadAccess 静默吞掉 | ✅ 已解决 | per-request 序列号 X 错误陷阱(`ScopedXErrorTrap`);注册冲突向脚本抛 OSError(键名+修饰);失败抓取不记入已安装集合 |
| P1 冷启动时序 | ✅ 已解决 | `BIF_Linux_Hotkey` 注册成功后立即 `LinuxReconcileHotkeyGrabs()`;主循环入口无条件一次;派发时兜底差量 |
| P1 修饰键/锁键写死 | ✅ 已解决 | `XGetModifierMapping` 动态解析 Alt/Super 槽位与实际锁键;锁定掩码幂集枚举;`MappingNotify` 全量重建 |
| P1 Key-up 自动重复 | ✅ 已解决 | 热键连接启用 `XkbSetDetectableAutoRepeat`;无 XKB 时合成 Release 过滤回退 |
| P1 Wayland 键码 off-by-one | ✅ 已解决 | 显式表:数字 `KEY_0..KEY_9`、`F1-F24` 分段(59-68,87-88,183-194)、小键盘 VK_MULTIPLY=0x6A/ADD=0x6B/SUBTRACT=0x6D/DECIMAL=0x6E/DIVIDE=0x6F |
| P1 测试"自己测自己" | ✅ 已解决 | 新增 `assert_hotkey_pt`(xkeycap 独立前台客户端持有输入焦点):普通热键抑制、`~` 透传、Off 解除抓取、HotIf-false 透传,直接观察前台客户端收到的按键 |
| CI continue-on-error | ✅ 已解决 | `.github/workflows/ci.yml` doc-check 步骤已去掉 `continue-on-error`,失败阻止合并 |

验证基线:doc-check **1047/1047**(core+ASan)、回归 27/27、Wayland 13/13、
XWayland 247/247。

## 第二批(简单热键语义增强)——round-31 已完成

- 左右修饰键区分(`mModifiersLR`)、通配修饰键(`*`)。✅ **round-31 已实现**:
  X11 被动抓取只匹配修饰掩码,抓取用中性+LR 掩码的并集,事件处理时以
  **XI2 原始事件观察器**(RawKeyPress/Release 携带按键发生时刻的键码,先于
  被抓取事件到达,批量 XTEST 输入也精确)跟踪 Ctrl/Shift/Alt/Win 左右侧
  (无 XInput2 的服务器回退 XQueryKeymap,人工输入准确);`mModifiersLR`
  精确匹配,错侧按压无变体命中时走标准 XTEST 透传(与 Windows 一致);
  通配 `*` 把抓取展开到全部主修饰组合(最多 16×锁组合),匹配接受额外修饰。
  顺带修复 `ConvertModifiers` 桩(恒 0 导致 consolidated LR 恒空)。
  验证:`assert_hotkey_lr` 10 断言(xkeycap 独立客户端)。
- 扫描码热键、`A & B` 自定义前缀——需要按键缓冲状态机,归入
  Hotstring/InputHook 的统一事件流。
- 哈希索引与唯一候选决议(1000 热键性能)。✅ **round-31 已实现**:
  每次抓取集合变化时重建 (键码/按钮, 主修饰掩码) -> 热键索引,事件处理
  O(1) 查桶;唯一决议:精确热键优先于通配,同优先时允许侧位更少者胜,
  平局按注册顺序。

## 第三批(平台能力扩展)——计划中

- XI2 RawKeyPress/RawKeyRelease 观察后端。🔄 **round-31 已落地最小形态**:
  修饰键左右侧跟踪(XInput2 原始事件,热键连接统一派发);完整的
  `~`/Key-up/观察类热键解耦仍依赖后续统一事件流。
- XDG Global Shortcuts Portal(Wayland 全局快捷键,已有 D-Bus 设施)。
- 鼠标热键/滚轮(XGrabButton 可先支持按钮)。✅ **round-30 已实现**:
  XGrabButton 抓取按钮 1/2/3/8/9 与滚轮 4-7(锁定掩码幂集同键盘),
  ButtonPress/Release 分派到变体匹配+回调;`~`/HotIf-false/Off 透传用
  "暂撤被动抓取(XUngrabButton)+释放活动抓取(XUngrabPointer)+XTEST
  反注入(先假释放再假按下)"——按住期间注入 press 会被服务器吞掉
  (键盘重复投递可用而按钮不可,已用独立 X 客户端探针验证),窗口收到
  一次多余的假释放后是完整按下/释放;存在 enabled up 变体时按下阶段保持
  抓取链,使真实释放送达以触发 up 热键(Windows 会把按下透传给目标,此为
  文档化偏差)。`assert_hotkey_btn` 12 断言(xkeycap 独立客户端观察
  down/up:普通抑制、`~` 完整点击、^组合、WheelUp 抑制、Off 解除、
  HotIf-false 透传各 1)。
- 可选 evdev/uinput broker(完整钩子语义,安全边界设计)。
- 统一事件流上实现 InputHook 按键采集与 Hotstring 展开。

> 说明:第一批中的"透传"采用 XTEST 重注入而非 X11 ReplayKeyboard,
> 原因已记录于 `core_hotkey_linux.cpp`(同步抓取的冻结事件不通知抓取
> 客户端,必然死锁;Async 抓取无法重放)。这是对审计建议的文档化偏差,
> 语义等价(目标窗口收到事件)。

最先应合并的不是更多热键语法，而是 **事件连接隔离、解除抓取、错误报告和按键透传**。这四项没有解决之前，增加的新语法越多，用户遇到“按键消失、热键假注册、剪贴板偶发失效”的概率越高。