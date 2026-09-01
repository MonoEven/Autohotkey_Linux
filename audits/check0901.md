# AutoHotkey_Linux 当前源码独立技术审计

**审计日期：2026-08-30**

**本报告唯一对应的审计快照：**

```text
branch: linux-port
HEAD:   5bc26e5a78d11888c60bc64511e1ea470a099df8
```

除“历史问题复核”章节外，所有源码结论均以该 SHA 为准。

---

# 1. 审计对象固定

| 项目                      | 当前值                                             |
| ------------------------- | -------------------------------------------------- |
| 默认审计 branch           | `linux-port`                                       |
| 当前 HEAD                 | `5bc26e5a78d11888c60bc64511e1ea470a099df8`         |
| HEAD 时间                 | 2026-08-26 00:06:32 +0800                          |
| 最新 GitHub Release       | `v2.0.26-linux.19`                                 |
| release tag commit        | `04c0ad2842bfbe68a49372dc35557c36d59c1c59`         |
| HEAD 是否领先 release     | 是，领先 2 个 commit                               |
| 两个领先 commit 的内容    | 仅 README/致谢类文档，未改产品源码、测试或构建逻辑 |
| README 声明版本           | `v2.0.26-linux.19`，状态为 technology preview      |
| Windows upstream baseline | 官方 AutoHotkey v2.0.26                            |

HEAD patch 明确给出了完整 SHA、时间和仅修改三个文档文件；release 页面显示 linux.19 为最新 release，并链接到完整 tag commit。release 到 HEAD 的比较只有两个 documentation commit、三个文件。因此，**本报告绑定 HEAD `5bc26e5…`，而产品源码实质上就是 linux.19 release 的源码**。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/commit/5bc26e5a78d11888c60bc64511e1ea470a099df8.patch))

README 仍明确称这是基于 AutoHotkey v2.0.26 的 source-level Linux port，状态是 technology preview，并将 linux.19 标为 current release；没有发现 upstream baseline 已切换到其他 AHK 版本的证据。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/README.md))

## 审计方法与边界

本次工作包括：

- commit-pinned 源码静态追踪；
- backend 到 runtime 的执行路径分析；
- 当前测试、CI、oracle、generated matrix 的证据质量分级；
- 当前 release、package workflow、systemd/inputd/security 设计复核；
- check0824 所涉架构问题的逐项重新判断；
- 以官方 Windows AHK v2.0.26 文档语义作为主要 reference。

本次审计环境未能完成仓库 clone 和本地编译，因此**没有把任何测试写成“本审计独立运行通过”**。报告中 Level A/B 的 real-host、Windows differential、systemd/root 测试，是对仓库中 committed harness、fixture、workflow 和记录的审计，而非我重新运行后的外部认证。这一限制已计入“证据可信度”和误差范围。

---

# 2. 首要结论：项目已经越过“玩具原型”，但尚未形成统一的 AHK 输入语义系统

当前最准确的定位是：

> **advanced technology preview，且已经 useful for selected workloads。**

它不再是一个只有解释器和少量 X11 wrapper 的实验项目。以下能力是真实而且具有工程深度的：

- 大部分 AHK v2 语言/runtime；
- X11 上相当广的 Hotkey、Hotstring、InputHook、Send、Win*；
- GTK3 GUI、Menu；
- 基于 EWMH 的窗口管理；
- 基于 AT-SPI 的部分真实应用 Control*；
- 基于 XI2/XKB 的物理按键和布局模型；
- IBus commit 到 Hotstring/InputHook 的链路；
- evdev/uinput 和多客户端 `ahk-inputd`；
- package、systemd、ASan、TSan-input、独立 X11 oracle；
- 首批官方 Windows v2.0.26 differential trace。

但它距离“熟悉 Windows AHK v2 的用户可以把真实脚本迁移到 Linux，并长期依赖”仍有三个核心断层：

1. **输入语义没有真正收敛到一套端到端模型。**  
   `AhkInputEvent` 已经存在，但 X11 热键路径明确保留并绕过统一 backend 接口，capture、matching、suppression、Send injection 仍有多套并行语义。

2. **跨进程 synthetic provenance、SendLevel/InputLevel 和多脚本 arbitration 没有闭环。**  
   当前字段不足以表达“哪个脚本、哪个发送事务、哪个 owner、哪个 suppression decision”，XTEST/uinput 也不能可靠携带这些信息。

3. **Wayland 支持高度依赖 compositor、extension、privilege 和 toolkit。**  
   它不是一个统一的“Wayland backend”。GNOME、KDE、wlroots 和 XWayland 的实际能力差距很大。

---

# 3. 四个总体数字

这些数字是工程判断，不是由“370 个函数”或 assertion 数量换算而来。

| 总体指标                                      |    估计 | 误差 | 含义                                                         |
| --------------------------------------------- | ------: | ---: | ------------------------------------------------------------ |
| **A. API Surface Coverage**                   | **94%** |   ±3 | 绝大多数 v2 入口、类、属性可以解析和调用；部分入口是 adaptation、NotSupported、local-only 或语义较窄 |
| **B. Semantic Compatibility**                 | **61%** |   ±8 | 与 Windows AHK v2.0.26 的实际可观察行为相符程度              |
| **C. Real-world Linux Automation Capability** | **57%** |  ±10 | 在真实 Linux 应用、toolkit 和桌面上完成自动化目标的能力      |
| **D. Production Readiness**                   | **38%** |   ±9 | 多脚本、长运行、故障恢复、安全边界、升级和环境变化下可依赖程度 |

API surface 很高，但 semantic compatibility 明显较低，因为 `SendInput`、InputLevel、跨进程 synthetic identity、Control/Clipboard rich data、custom combo、native Wayland capture 等并不能通过函数入口存在来证明。

---

# 4. check0824 历史 P0/P1 在当前 HEAD 上的实际状态

| 历史问题类别                           | 当前状态                            | 当前代码证据                                                 | 真正关闭              |
| -------------------------------------- | ----------------------------------- | ------------------------------------------------------------ | --------------------- |
| 输入 backend 缺少统一事件模型          | **Partial**                         | `input_event.h::AhkInputEvent` 已建立 version/source/origin/VK/SC/text/device；但 `input_backend.h` 明示 X11 不重路由到统一接口 | 否                    |
| X11 Hotstring/InputHook 多脚本冲突     | **Partial**                         | XI2 raw observation 和 selected suppression 已取代早期全量抢占；但 suppressing hooks/passive grabs 仍跨进程竞争 | 否                    |
| scan code                              | **大部分解决**                      | `core_keymodel_linux.cpp` 使用 evdev→set-1 canonical SC，常见 PC 键和左右 modifier 已覆盖 | 部分                  |
| custom combination                     | **Partial**                         | evdev 有状态机；X11 capability 明确 `custom_combo=false`     | 否                    |
| wildcard                               | **大部分解决**                      | X11/evdev matcher 有额外 modifier 处理，Windows differential 有 `*F10` 样本 | 部分                  |
| tilde                                  | **Partial**                         | 可 pass-through，但 X11 grabbed 路径常由 suppress 后 XTEST reinjection 实现，不是原始事件继续送达 | 否                    |
| key-up                                 | **Partial**                         | 已支持 up hotkey；为等待 release，press-side 仍会被 grab/consume，存在可观察差异 | 否                    |
| remapping                              | **Partial**                         | evdev/uinput、XTEST 均有路径；跨 backend physical state、provenance 和 multi-client remap 未统一 | 否                    |
| 左右 modifier                          | **大部分解决**                      | XKB/evdev 层区分 L/R Shift、Ctrl、Alt、Win，AltGr 单独处理   | 部分                  |
| SendLevel/InputLevel                   | **Still present，且发现确定性错误** | X11 hotkey comparison 与官方规则相反；跨进程也不闭环         | 否                    |
| synthetic provenance                   | **Partial**                         | X11 有 XI2 device/时间窗口 heuristic；schema 有 source/level；broker protocol 不携带这些字段 | 否                    |
| native Wayland continuous input stream | **Still present**                   | core Wayland seat 只能看到本进程 surface 事件；broker 在 pure Wayland 又缺 compositor layout source | 否                    |
| Unicode/IME                            | **显著改善，仍 Partial**            | XKB Unicode、IBus real commit、Fcitx5 signal protocol；composition/focus/grapheme coverage 仍不足 | 否                    |
| Window enumeration                     | **Fixed on X11**                    | `_NET_CLIENT_LIST` + recursive `WM_STATE` fallback + unmanaged root children | 是，限 X11            |
| Control* fake virtual state            | **外部目标问题已解决**              | 不可实现的 external X11 control operation 改为 NotSupported；自有 GUI 仍保留合法 local state | 是，外部 fake-success |
| AT-SPI                                 | **显著改善**                        | pending call pump、timeout budget、bulk cache、Text/Action/Selection/Value/EditableText，且有 toolkit matrix | 部分                  |
| blocking D-Bus                         | **Partial**                         | AT-SPI 已改 pending/pump；COM adaptation 仍有无限/同步 blocking call，IME/backend probe 也有秒级 block | 否                    |
| evdev/uinput 生命周期                  | **显著改善但出现新 P0**             | hotplug、watchdog、panic、client disconnect 已有；但 grab 后 uinput 初始化失败会吞键 | 否                    |
| 长运行可靠性                           | **Partial**                         | 30 秒 CI soak、5 分钟 VM、短 fault oracle；没有 1/8/24 小时生产证据 | 否                    |
| 测试 self-validation                   | **显著改善**                        | 外部 X11 recorder、Windows fixture、真实 GNOME/toolkit；但 doccheck/self-oracle 仍占较大比例 | 否                    |

这里最重要的结论不是“旧问题仍然都在”。恰恰相反，窗口枚举、AT-SPI、external fake control state、XI2 capture、IBus 和输入 daemon 生命周期都发生了实质性重构。问题在于：**新架构解决了一些局部故障，但尚未形成跨 backend、跨脚本的一致语义。**

---

# 5. 新输入架构是否闭环

## 5.1 normalized event model 包含什么

当前 `AhkInputEvent` schema version 为 1，包含：

- monotonic timestamp；
- canonical evdev code；
- VK；
- set-1 SC；
- Unicode scalar；
- press/release；
- repeat；
- physical/self-inject/other-inject/IME source；
- SendLevel；
- device ID；
- X11/Portal/GNOME/evdev/Broker/IBus/Fcitx5 origin。

这是一次真正有价值的架构进步，不是空 struct。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/source/linux/core/input_event.h))

但它缺少实现完整 AHK input semantics 所需的关键身份：

- script/client identity；
- injection transaction ID；
- registration/hotkey owner；
- suppression owner；
- seat identity；
- focus/application identity；
- remap chain identity；
- provenance confidence；
- protocol capability/version negotiation；
- capture sequence与injection sequence 的一一关系。

## 5.2 从物理输入到应用的端到端追踪

### X11 路径

```text
physical key
→ X server / XI2 raw ring
→ legacy X11 grab event
→ XKB key model
→ Hotkey 或 capture engine
→ suppression decision
→ AHK quasi-thread
→ Send/XTEST
→ process-local self mark
→ X server
→ target application
```

关键问题是：`input_backend.h` 明确说明现有 X11 路径“不重新路由”到统一 backend interface。X11 capture、hotkey dispatch、Hotstring/InputHook capture 和 Send/XTEST 仍直接使用旧路径；normalized event 更像 trace/adapter，而不是调度系统的唯一事实源。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/source/linux/core/input_backend.h))

### evdev/inputd 路径

```text
physical EV_KEY
→ inputd exclusive EVIOCGRAB
→ per-code subscriptions
→ any-suppress arbitration
→ protocol frame(code/value/time)
→ ahk_core reconstruct normalized event
→ evdev hotkey/combo matcher
→ AHK thread
→ uinput/XTEST/Wayland injection
→ application
```

broker frame 只有 code、value、timestamp；device identity、physical/synthetic source、SendLevel、origin confidence 都丢失。客户端收到后把 broker 事件重建成 physical/unknown。也就是说，schema 虽然有字段，**broker wire protocol v1 并没有保存它们**。

### Portal/GNOME shortcut 路径

这些路径主要提供“某个已注册 accelerator 被激活”，而不是完整按键流。它们不能自然提供：

- arbitrary character stream；
- physical scan sequence；
- modifier physical state；
- suppression ownership；
- Hotstring/InputHook 所需的连续文本和 non-text 输入；
-可靠 synthetic provenance。

### injection 路径

Send 不接受一个完整 `AhkInputEvent` 或 transaction，而是直接调用 XTEST、uinput、wlroots virtual keyboard、clipboard paste 等 backend。capture 和 injection 使用不同的状态和 metadata，因而 normalized model 没有贯穿闭环。

## 判断

> **normalized event model 是重要基础设施，但还不是系统中心。**

它当前更接近：

> 多 backend 的公共描述格式、诊断格式和部分 matcher 输入。

而不是：

> 所有 capture、matching、suppression、thread dispatch、Send/remap、injection 和 replay 的唯一事件总线。

---

# 6. per-hotkey input mux

当前 mux 根据 hotkey 所需 capability 选择 X11、GNOME、Portal 或 EVDEV lane，方向是正确的。它会考虑 pass-through、up、wildcard、bare key、scan code 和 custom combo 等 capability，并允许同一进程不同 hotkey 选择不同 backend。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/source/linux/core/input_backend.cpp))

但当前选择算法存在四类缺口。

## 6.1 capability 匹配不等于可用性匹配

EVDEV capability 可以被选中，但 selection 阶段并未证明：

- broker socket 当前可连接；
-用户当前有权限；
- `/dev/uinput` 可用；
-物理键盘实际 grab 成功；
- compositor/session 未变化。

Portal/GNOME availability probe 也包含同步 D-Bus 调用。session backend classification 有静态缓存，运行中从 X11/XWayland 切换、compositor restart、extension disconnect 时不会完整重新协商。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/source/linux/core/input_backend.cpp))

## 6.2 没有统一 recovery state machine

没有看到覆盖以下状态转换的中心状态机：

```text
healthy
→ degraded
→ disconnected
→ retrying
→ permission-revoked
→ reauthorized
→ resubscribe
→ reconcile physical state
```

各 backend 各自处理重连；Wayland display 有 sticky failure，inputd 有有限 reconnect，portal/GNOME 又是另一套逻辑。

## 6.3 capability table 没表达全部 AHK 语义

当前 route needs 没有完整表达：

- SendLevel/InputLevel；
- synthetic provenance；
- HotIf evaluation timing；
- physical `GetKeyState` source；
- KeyWait ownership；
- remap pair atomicity；
- character decoding能力；
- injection backend compatibility；
- cross-process owner semantics。

因此，mux 可以选到“能注册这个 key”的 lane，却不保证选到“能保持该 AHK hotkey 全部语义”的 lane。

## 6.4 cross-backend thought experiment

假设：

```ahk
F12::...       ; X11 lane
a & b::...     ; evdev lane
```

脚本随后通过 XTEST 或 uinput Send。

当前可能发生：

- `A_ThisHotkey/A_PriorHotkey` 只在进程内共同更新，基本可工作；
- repeat 判定来自不同 backend；
- `GetKeyState` 可能问 X11 状态，而 combo 状态来自 evdev；
- X11 key-up 与 evdev key-up 的 suppression/replay 时序不同；
- XTEST synthetic event 能被 XI2 heuristic 识别，但 uinput 通常看起来像物理设备；
- SendLevel gate 仅部分 lane 实现；
- self-trigger prevention 对显式 `SendInput()`、普通 `Send`、uinput 和其他脚本不一致。

所以当前 mux 能证明的是：

> 一个脚本可以同时注册两个不同 lane 的 hotkey。

它尚不能证明：

> 所有 AHK observable state 在多个 lane 中仍等价于同一 Windows keyboard hook。

---

# 7. `ahk-inputd` 独立审计

## 7.1 Capture

daemon 会：

-扫描多个 `/dev/input/event*`；
-识别键盘；
-跳过自己的 virtual device；
-对每个键盘执行 `EVIOCGRAB`；
-周期 rescan；
-处理 device removal；
-通过 uinput replay 未被 suppress 的事件；
-最多维护 64 个设备。

这比早期单进程直接 evdev 方案成熟得多。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/source/linux/inputd/inputd.c))

限制：

-当前 daemon 本质上是 keyboard broker，不是完整 keyboard+mouse broker；
-device frame 不携带具体 device ID；
-只按 key code 订阅，不能按设备、seat 或来源过滤；
-部分键盘 grab 失败时，系统处于部分覆盖状态，没有对脚本暴露精确设备 capability；
-设备 ID 在进程内 fallback 路径按数组 slot 构造，hotplug 后不稳定。

## 7.2 Multi-client 和 arbitration

协议 v1 的 subscription 是：

```text
{ evdev code, suppress: bool }
```

当前确定性规则是：

> 任一 live client 对该 code 请求 suppress，则 original event 不 replay；所有订阅 client 仍收到 event。

这比“最后注册者获胜”更可预测，但不够表达 AHK 多脚本语义。

### A/B/C thought experiment

- Script A：想 suppress；
- Script B：只观察、要求 pass-through；
- Script C：想 remap。

结果：

1. A 使 original event 被全局 suppress；
2. B 仍收到 event，但其 pass-through 意图不会改变结果；
3. C 也收到 event，并在自己的进程里独立发送 remap output；
4. 如果两个脚本都 remap，可能产生两个 output；
5. broker 不知道哪个 client 是 remap owner；
6. 没有 priority、transaction、acknowledged replacement、exclusive ownership 或 conflict reporting。

因此 arbitration 是**确定但过于粗糙**，不能复制 Windows AHK 多脚本 hook 的全部交互。

## 7.3 client failure

已确认的积极点：

- client disconnect 会删除规则；
- socket nonblocking；
- stalled client 的写失败会被标记 dead，不会永久堵住 broker；
- SIGKILL daemon 时，kernel 会释放 fd 对应的 grab；
- watchdog 超过两秒关闭 input fd；
-物理 panic sequence 为 Backspace→Escape→Enter；
- systemd socket activation 和 idle exit 已有代码与测试。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/source/linux/inputd/inputd.c))

## 7.4 严重 fail-open 缺陷

启动顺序是：

```c
scan_devices();               // 已 EVIOCGRAB
sUinputFd = open_uinput_replay();
```

而 `open_uinput_replay()` 失败只返回 `-1`，主程序仍打印 ready 并继续运行；`replay_key()` 在 `sUinputFd < 0` 时直接 return，write error 也被忽略。未被 suppress 的键只会调用这个 replay 函数。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/source/linux/inputd/inputd.c))

这意味着：

> 当键盘已被独占 grab，但 `/dev/uinput` 不可打开、创建失败，或运行中 uinput write 失效时，正常输入可能全部被吞掉。

用户仍可依靠 panic sequence、watchdog、kill daemon 恢复，但这不等于自动 fail-open。

**修复要求：**

-必须先创建并验证 replay device，再 grab；
- grab 应采用 two-phase commit；
- replay write 失败立即 release all grabs；
-创建一个真实外部 oracle：拒绝 `/dev/uinput`、强制 `UI_DEV_CREATE` 失败、运行中关闭 uinput fd，验证物理键盘仍可用。

## 7.5 Security

积极点：

- packaged socket 是 `root:input 0660`；
- manual socket 默认为 0600；
-记录 `SO_PEERCRED`；
-协议不提供直接 injection command，因此连接者不能通过 broker 调用任意注入。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/source/linux/inputd/inputd.c))

风险：

- `SO_PEERCRED` 只被记录，没有用于授权策略；
-任何获得 `input` group socket 访问权的用户都可以订阅并观察键盘；
-同样可以请求 suppress，构成本地键盘 DoS；
-没有每 UID/client quota、优先级、允许按键集合或显式“observe-only”政策；
-最多 32 clients，但没有按身份隔离；
-协议没有 replay protection、session token 或 capability grant；
-`input` group 本来就是非常高权限，文档有警告，但 broker 又把这项权限扩展为长期脚本服务面。

这不是 remote privilege escalation，但它是一个真实的本地 high-trust boundary。当前 systemd hardening 不能替代 protocol authorization。

---

# 8. X11 Hotstring / InputHook

## 8.1 已经真正改善的部分

当前 X11 capture engine 使用：

- XI2 raw observation；
- XKB key model；
- selected passive grabs；
-独立 raw-event/source ring；
-通过 Backspace 和 replacement send 处理 Hotstring；
-回调排队到主 loop，避免直接从 capture thread 重入 runtime；
-支持 InputHook Visible、KeyOpt、EndKey、Match、Timeout、OnChar、OnKeyDown、OnKeyUp 的实际代码路径。

这已不是旧版本那种“广泛 grab 后全部 synthetic forward”的简单模型。

Windows differential 当前也覆盖了 InputHook 的 `a`、Shift+A、F13 down/char/up 顺序，以及部分 Hotstring option。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/tests/differential/README.md))

## 8.2 仍存在的语义风险

### pass-through 仍可能是 reinjection

对于被 passive grab 截获的 `~` hotkey，代码是先 consume，再通过 XTEST reinject 给目标应用，而不是保证 original target event 自然继续。这影响：

-设备来源；
-timestamp；
-XI2 source identity；
-repeat；
应用是否区分 synthetic；
其他脚本是否再次观察；
SendLevel/provenance。

### process-global text buffer

Hotstring/InputHook capture 使用进程级共享字符缓冲，不携带：

-当前 focus window；
-application；
-seat；
-device；
-IME composition transaction。

光标移动、focus transfer、多 seat 或多个独立输入源可能把语义混在一起。

### multi-script suppress 仍会冲突

raw observation 可以被多个脚本共享，但只要某个 InputHook 要 suppress non-text/text，仍必须安装 grabs。多个进程无法共享 suppression ownership，也无法协调 replacement。

### provenance ring 是 heuristic

XTEST/self-injection tracking 依赖有限大小的进程内 ring、keycode/phase 和时间窗口。高事件频率、多个同 key event、其他进程 injection 或设备重排时可能误关联。

### IME 与 raw stream 的边界

有 IME listener 时 capture engine 使用固定延迟等待 IME commit。这个办法可以避免某些重复字符，但并不是一个正式的 composition transaction model；rapid typing、cancel composition、focus transfer 和连续中英混输仍需要更强测试。

## 多脚本场景

```text
A: Hotstring
B: InputHook
C: global hotkeys
```

在纯观察模式下，XI2 raw 基本可以共享。出现 suppression 时：

- A/B 可能对同一个 key 安装重叠 grab；
- C 的 global hotkey 又可能拥有同键 passive grab；
- replacement XTEST 可能被其他进程视为外部 synthetic 或 physical；
-一个进程 crash 后 X server 会释放其 grabs，但其他进程的 capture buffer/provenance 不知道发生了 owner 变化；
-没有 broker 级统一 replacement transaction。

结论是：

> 多脚本观察能力已大幅改善，但多脚本 suppression/remap/replacement 仍未完成。

---

# 9. Scan code、key model 与 custom combination

## 9.1 三层 key model

当前模型有明确的三层：

```text
evdev physical code
→ canonical AHK/set-1 SC
→ current-layout keysym / Unicode / VK
```

X11 通过 xkbcommon 维护 keymap/state，并能处理：

-普通字母数字；
-左右 Shift/Ctrl/Alt/Win；
-AltGr / ISO_Level3_Shift；
-numpad；
-常见 function/navigation keys；
-当前 layout group；
-从 Unicode 反查 Shift/AltGr 组合。

布局 oracle 也包含 AZERTY/AltGr，而不是只测 US QWERTY。

### 仍有的边界

-常见 `sc01E` 路径是物理的，不再简单按 logical `a` 解释；
-完整 OEM/media/vendor-specific key 列表并不充分；
-运行时 layout group switch 有处理，但整个 keymap replacement/hotplug/layout daemon change 的长期重建证据较弱；
-Portal/GNOME shortcut lane 本身未必提供 physical SC；
-broker protocol 不携带 device/layout snapshot；
-pure Wayland broker character stream 仍需要独立的 compositor layout source。

因此：

| 测试项                | 当前判断                                                     |
| --------------------- | ------------------------------------------------------------ |
| `sc01E::`             | X11/evdev 主路径基本可用                                     |
| `vkXX::`              | X11 logical VK 可用；其他 lane 取决于 accelerator mapping    |
| `a::`                 | 当前 layout logical mapping 可用                             |
| 左右 modifier         | 常见键已区分                                                 |
| numpad                | 大部分常见键已覆盖                                           |
| media/OEM             | 不完整，证据有限                                             |
| QWERTY↔AZERTY         | X11 group/layout 模型较成熟                                  |
| AltGr                 | 有独立处理和 oracle                                          |
| runtime layout switch | group switch 有能力；完整 keymap replacement 仍需 fault test |

## 9.2 Custom combo 状态机

| 语义                 | evdev/inputd                          | X11        | 判断                             |
| -------------------- | ------------------------------------- | ---------- | -------------------------------- |
| `a & b`              | 已实现                                | 明确不支持 | Partial                          |
| `a & b up`           | 有 release 状态路径                   | 不支持     | Partial                          |
| `~a & b`             | prefix pass-through 有实现            | 不支持     | 需 differential                  |
| `*a & b`             | 额外 modifiers 有匹配逻辑             | 不支持     | 需 differential                  |
| modifier + combo     | 可组合，但状态交互证据窄              | 不支持     | 未充分验证                       |
| prefix 单独按下/释放 | 有 delayed standalone/balanced replay | 不支持     | 需 Windows oracle                |
| prefix hold/repeat   | autorepeat suppression 有实现         | 不支持     | 需高频验证                       |
| 错误第二键           | 状态机会标 prefix used                | 不支持     | Windows 兼容性未证明             |
| 快速 roll-over       | 可能工作                              | 不支持     | 未差分验证                       |
| 多 combo 共用 prefix | 数据结构可注册                        | 不支持     | 冲突顺序未证明                   |
| `scXXX & key`        | 可使用 physical code                  | 不支持     | Partial                          |
| combo + HotIf        | variant 层可评估                      | 不支持     | suppression 时机仍可能先于 HotIf |
| combo + remap        | 无统一 transaction                    | 不支持     | 高风险                           |

这是一套真实状态机，不是只识别单一 `a & b`；但距离 Windows custom-combo 语义仍远，特别是 prefix standalone、roll-over、shared prefix、HotIf false 时 pass-through 和 remap atomicity。

---

# 10. Send / SendEvent / SendInput / SendText

## 10.1 当前实现结构

Linux 端把 Send family 解析成四种内部 mode：

- Event；
- Input；
- Play；
- Text。

但它们最终仍共享 XTEST/uinput/wlroots/clipboard 等 injection primitive。主要差异是 pacing、press duration 和部分 self-suppression，而不是 Windows 的不同低层发送机制。`SendPlay` 没有 Linux 对应 journal path，是 adaptation。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/source/linux/core/core_input_linux.cpp))

## 10.2 已确认的 `SendMode("Input")` 语义差异

源码明确写道：

> 普通 `Send` 即使当前 SendMode 为 Input，也不会采用显式 `SendInput()` 的 self-suppression；只有直接调用 `SendInput()` 才会。

相关路径：

```text
core_input_linux.cpp
LinuxSetSendMode()
LinuxModeSuppressSelf()
LinuxSendWrapper()
BIF_Linux_Send
BIF_Linux_SendInput
```

`BIF_Linux_Send` 将 `aExplicitSendInput=false`，`BIF_Linux_SendInput` 才传 true。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/source/linux/core/core_input_linux.cpp))

而官方 Windows AHK 文档说明默认 `Send` 等价于 `SendInput`，且 SendInput 进行中本脚本的 hook hotkeys/InputHooks 不会被激活。([Doggy 8088](https://doggy8088.github.io/AutoHotkeyDocs/docs/lib/SendLevel.htm))

这是一个**明确、用户可观察的 semantic incompatibility**：

```ahk
SendMode "Input"
Send "a"
```

与：

```ahk
SendInput "a"
```

在当前 Linux port 中可能触发不同的自身 hotkey/hotstring 行为；Windows v2.0.26 不应有这种差别。

## 10.3 Send mode 兼容性

| 领域                                         | 当前状态                                                     |
| -------------------------------------------- | ------------------------------------------------------------ |
| Send / SendEvent / SendInput / SendPlay 入口 | 有                                                           |
| SendMode                                     | 有，但 Input semantic 差异明确                               |
| SetKeyDelay                                  | Event/Play 有 pacing；Input/Text 忽略延迟                    |
| SetMouseDelay                                | 有接口，跨 backend 可观察一致性未证明                        |
| SetDefaultMouseSpeed                         | X11/Wayland backend 不可能完全复制 Windows movement curve    |
| `{Text}` / `{Raw}`                           | 有 parser 路径                                               |
| `{Blind}`                                    | 部分；主要控制内部 held modifier，不是完整查询并恢复 physical modifier state |
| key down/up/repeat                           | 基本有                                                       |
| modifier restoration                         | 部分；多 backend、用户同时按键时不等价                       |
| Unicode X11                                  | 当前 layout 优先，缺键时临时 keysym mapping                  |
| Unicode wlroots                              | 临时 keymap/virtual keyboard                                 |
| native Wayland fallback                      | clipboard overwrite → Ctrl+V → bounded wait → restore        |
| AltGr/layout-aware Send                      | X11 有较好的 XKB 反查                                        |
| dead/compose                                 | 不是完整模拟输入法状态机                                     |
| Caps/Num/Scroll Lock                         | 可发 key，但 lock-state preservation/differential evidence不足 |
| SendPlay                                     | 平台 adaptation，而非 Windows journal semantic               |

## 10.4 clipboard paste fallback

native Wayland 无 virtual keyboard 时，非 ASCII SendText 可以临时：

1.保存 clipboard；
2.设置目标文本；
3.发送 Ctrl+V；
4.等待目标请求 offer；
5.恢复 clipboard。

这比立即恢复要可靠，但仍有真实问题：

-期间其他进程观察到或修改 clipboard；
-目标慢于 timeout；
-用户同时复制；
-rich/custom MIME 原 clipboard 无法完整保存；
-敏感文本短暂成为系统 clipboard；
-Electron/remote app 的 request timing 不稳定。

## 10.5 backend divergence

XTEST、uinput、wlroots virtual keyboard 和 clipboard-paste 不具有相同 observable semantics：

- XTEST 能通过 XI2 source device/heuristic 辨认；
- uinput 往往像物理设备；
- wlroots virtual keyboard 仅特定 compositor；
- clipboard fallback 不产生每字符 key events；
- Hotstring/InputHook 是否观察到它们完全不同；
- SendLevel 不能随 Linux kernel input event 跨进程传播。

因此“SendText 能在测试窗口输入字符”不能证明 AHK Send compatibility。

---

# 11. SendLevel / InputLevel / synthetic provenance

这是当前最严重的 semantic blocker 之一。

## 11.1 X11 hotkey gate 方向错误

官方规则是：

> synthetic event 只有在 `SendLevel > hotkey/hotstring InputLevel` 时才触发。([Doggy 8088](https://doggy8088.github.io/AutoHotkeyDocs/docs/lib/SendLevel.htm))

当前 X11 hotkey 代码：

```cpp
if (hk_fire && self_injected
    && (int)vp_fire->mInputLevel < self_level)
    hk_fire = nullptr;
```

也就是当：

```text
InputLevel < SendLevel
```

时反而阻止 hotkey。

这与官方规则相反。源码注释本身也写成“InputLevel >= SendLevel 才触发”，说明不是单纯变量命名歧义。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/source/linux/core/core_hotkey_linux.cpp))

更重要的是，Hotstring capture 的另一条路径使用的是近似正确的 `sendLevel <= inputLevel` 跳过规则。因此当前同一 synthetic event 可能：

-不触发 Hotstring；
-却以相反条件触发 Hotkey；
-或反之。

## 11.2 跨进程无法可靠保存 provenance

`AhkInputEvent` 有 source/send_level，但：

- XTEST 事件没有携带 script ID/level 的系统字段；
-当前靠本进程 ring 和 XI2 source heuristic；
-另一 AHK script 的 Send 无法匹配本进程 ring；
-uinput 事件看起来像物理设备；
-inputd protocol 只传 code/value/timestamp；
-wlroots virtual keyboard 也不携带 AHK SendLevel；
-clipboard fallback 没有对应 key stream。

## 11.3 Script A / Script B thought experiment

Script A：

```ahk
#InputLevel 0
F1::{
    SendLevel 10
    SendEvent "a"
}
```

Script B：

```ahk
#InputLevel 5
a::MsgBox "B"
```

Windows reference 下，10 > 5，应允许触发 B。

当前 Linux：

-同进程 X11 hotkey comparison 可能反向阻止；
-不同进程时 B 不知道事件 SendLevel=10；
-如果 XTEST 被判 OTHER_INJECT，可能按默认/unknown 处理；
-如果走 uinput，可能被当 physical；
-如果走 clipboard fallback，则根本没有对应的 `a` key event。

因此：

> 当前无法声称 SendLevel/InputLevel 与 Windows v2.0.26 兼容，尤其不能声称 multi-script compatibility。

---

# 12. Unicode 与 IME

## 12.1 IBus

当前实现确实不再是“IME 未实现”：

-监听 IBus InputContext；
-跟踪 focused context；
-处理 preedit update/end；
-处理 commit text；
-commit 被转换成 IME_COMMIT normalized event；
-送入 Hotstring/InputHook capture；
-已有 GNOME VM + IBus/libpinyin + `你好` 的 real-host 记录；
-还有第二进程观察路径。

这是当前被低估的成熟能力之一。README 也明确把 IBus real-VM 和 Fcitx protocol-only 区分开了。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/README.md))

## 12.2 剩余 IBus 风险

- preedit/focused context 状态仍偏全局；
-启动/重连 engine query 有同步 timeout；
-commit 被拆成 Unicode scalar，缺少 composition transaction ID；
- canceled composition、caret movement、focus transfer、process switch 的状态一致性证据不足；
-raw capture 与 IME listener 之间使用固定等待窗口；
-连续中文、中文英文切换、候选快速选择和 punctuation corpus 太窄。

## 12.3 Fcitx5

当前最高结论只能是：

> Fcitx5 signal/protocol integration 有 CI oracle。

不能写成：

> Fcitx5 real desktop Chinese input supported。

缺少的关键证据：

-真实 Fcitx5 daemon；
-真实 KDE/GNOME desktop；
-真实 candidate/preedit；
-真实中文 engine；
-焦点切换；
-连续 commit；
-与 Hotstring/InputHook 的真实交互。

## 12.4 Supplementary Unicode

内部使用 `char32_t` 和 wchar/scalar 路径，单个 supplementary-plane scalar、emoji code point 具备实现基础。

但：

- combining sequence；
-ZWJ emoji；
-variation selectors；
-grapheme cluster；
-IME 一次 commit 多 scalar；
-Hotstring buffer 的 grapheme 边界；

都没有足够 oracle。当前更接近“Unicode scalar capable”，不是“完整 Unicode text-semantic compatible”。

---

# 13. Wayland 必须分环境评价

| 环境                             | 当前可用路径                                                 | 主要实际能力                                                 | 最大缺口                                                     | 等级                          |
| -------------------------------- | ------------------------------------------------------------ | ------------------------------------------------------------ | ------------------------------------------------------------ | ----------------------------- |
| **GNOME Wayland**                | XDG GlobalShortcuts、GNOME extension、evdev/inputd、uinput、AT-SPI、IBus、SNI/clipboard extension | selected hotkeys、部分 Send、真实 AT-SPI、IBus commit、GTK应用 | 无 libei；无统一 global stream；extension/permission/reconnect；native Win* | advanced technology preview   |
| **KDE Plasma Wayland**           | portal/KGlobalAccel 类路径、AT-SPI、SNI，理论上可配 evdev/uinput | basic shortcut/tray/Qt accessibility 的实现基础              | 缺 real KDE VM/host matrix；无 libei；Fcitx real E2E不足     | technology preview            |
| **wlroots/sway**                 | virtual keyboard、virtual pointer、screencopy、evdev/inputd  | 当前最深的直接 native input/injection path                   | headless sway 不能代表全部 wlroots；connection restart；global text decoding | advanced technology preview   |
| **XWayland**                     | 基本复用 X11/XTEST/EWMH/XI2                                  | 对 XWayland 应用最接近 X11                                   | native Wayland app 不在同一窗口/输入模型；混合 focus/clipboard | useful for selected workloads |
| **Hyprland/River/Weston/COSMIC** | 取决于是否暴露兼容 wlroots/portal/AT-SPI 能力                | 只能按具体协议推断                                           | 没有足够 real-host 证据                                      | 未充分验证                    |

README 自己也承认：wlroots 提供较深直接路径，GNOME/KDE 通常依赖 portal、extension、libei 或 evdev/uinput；release 还明确说 KDE、Flatpak 和 libei/InputCapture 仍有环境/实现限制。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/README.md))

## Wayland 缺口分类

| 缺口                                        | 分类                                   |
| ------------------------------------------- | -------------------------------------- |
| core Wayland 任意 global physical capture   | **A — protocol inherent limitation**   |
| core Wayland 任意操作其他应用窗口           | **A**                                  |
| GlobalShortcuts 注册有限组合                | **B — standard portal 可改善**         |
| consented pointer/keyboard injection        | **B — RemoteDesktop/EIS/libei 可改善** |
| GNOME 特殊快捷键/clipboard                  | **C — compositor-specific**            |
| KDE KGlobalAccel/桌面集成                   | **C**                                  |
| wlroots virtual keyboard/pointer/screencopy | **C**                                  |
| 任意全局 capture/suppress/remap             | **D — privileged broker 可改善**       |
| semantic widget automation                  | **E — AT-SPI 可改善**                  |
| compositor restart 后不重连                 | **F — 项目实现缺口**                   |
| broker arbitration/provenance               | **F**                                  |
| silent no-op Send                           | **F**                                  |
| pure Wayland broker 没有独立 layout source  | **F/D**                                |
| KDE/Fcitx/Flatpak 缺真实验证                | **F — 验证缺口**                       |

---

# 14. libei / EIS / RemoteDesktop Portal

当前 HEAD 未发现：

- libei build dependency；
-EIS client；
-RemoteDesktop portal session negotiation；
-libei backend 进入 Send route；
-真实 libei E2E。

现状仍属于 roadmap/known limit，而非 skeleton 已进入实际 backend。

它仍是高优先级，原因是它可以为 GNOME/KDE 提供比 compositor-private protocol 更标准的、用户授权的 injection path。

但必须明确：

> libei/EIS 主要解决 consented injection，不自动解决 arbitrary global capture。

Hotstring、InputHook、physical KeyWait、suppression、custom combo 仍需要：

-InputCapture portal 在可用范围内；
-compositor-specific capture；
-或 privileged evdev/inputd。

---

# 15. Window / Win*

## 15.1 已关闭的历史问题

X11 window enumeration 已改为：

1. `_NET_CLIENT_LIST`；
2.递归查找 `WM_STATE` 的 ICCCM fallback；
3.保留 unmanaged/root children。

因此旧的“只靠 XQueryTree，面对 reparenting WM 列错窗口”问题已经真正关闭。

## 15.2 当前能力

实际 EWMH/X11 路径可以较好完成：

- WinExist / WinActive；
- WinGetList / title / class / PID；
- activate/move/minimize/maximize/restore；
-close/kill；
-always-on-top；
-opacity；
-wait polling；
-group 对窗口集合的组合。

## 15.3 仍不是实际目标状态的部分

- `WinGetText` 基本不能复刻 Windows child-control text；
- `WinText` criterion 受限；
- TransColor 主要是 process-local state，没有真实 compositor effect；
-透明度 getter 可能只记住本进程设置，而不是查询所有真实 window state；
-Style/ExStyle/MinMax 等若干值是 Linux adaptation 或 local cache；
-X11 Controls 枚举的是 child windows，不是 GTK/Qt semantic widgets；
-native Wayland 没有通用 foreign-window control；
-XWayland PID/process identity 可能经过代理或 sandbox。

结论：

> X11 Win* 已经对窗口管理脚本有实用价值，但不应把所有 getter 的返回值理解为目标应用真实 Win32 window state。

---

# 16. Control* / AT-SPI

## 16.1 fake external state 的问题已真正解决

当前外部 X11 control operation 如果需要 Windows control messages、但 Linux 不存在等价路径，会返回 NotSupported，而不是自己 set 后再自己 get 并声称成功。

仍存在的 `LinuxCtrlState` 主要用于：

-本进程自有 GTK GUI；
-对 X11 child-window adaptation；
-合法的本地 selection/caret/list state。

不能把它和旧的 external fake-success 混为一谈。

## 16.2 AT-SPI 当前真实支持面

| Toolkit / 应用    | 当前最高已见能力                                          | 仍有限的部分                                                 |
| ----------------- | --------------------------------------------------------- | ------------------------------------------------------------ |
| GTK               | Text、EditableText、Action、WinTitle scope                | 复杂 tree/table/caret 组合需逐控件确认                       |
| Qt6               | text/Unicode、Button Action、list Selection、slider Value | 自定义绘制控件不一定暴露                                     |
| Java Swing        | text/action/selection/value read                          | ATK wrapper Value write 会虚假成功，runtime readback 后返回 EIO |
| LibreOffice Calc  | dialog Action、title scope、Table dimensions              | 虚拟 cell 内容需要未实现的 Table-cell API                    |
| Firefox           | 取决于 accessibility tree，基础 Document/widget 可见      | 网页/Shadow DOM/自定义控件差异大                             |
| Chromium/Electron | 窗口和部分 Document/Action                                | 内容 exposure 取决于 Chromium accessibility                  |
| VS Code           | 窗口和 Document 可见                                      | Monaco source、selection、caret、editable content 未暴露     |

真实 host matrix 对 GTK、Qt、Java、LibreOffice 和 VS Code 做了分层验证，而不是只测 fake AT-SPI service。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/5bc26e5a78d11888c60bc64511e1ea470a099df8/tests/oracle/GUI_HOST_MATRIX.md))

## 16.3 AT-SPI D-Bus 改善

AT-SPI client 已使用 pending call，并以小片 time slice pump D-Bus，同时有总 timeout budget、bulk cache 和 nested-call EBUSY 防护。这实质上改善了旧的 UI freeze/reentrancy 问题。

但它仍需处理：

-回调中再次调用 Control*；
-target service disappear；
-GNOME accessibility bus restart；
-large tree cache invalidation；
-应用无响应；
-跨线程调用；
-focus 变化和 stale accessible object。

## 16.4 VS Code / Electron 边界

当前判断应明确：

-窗口：可见；
-顶层 Document：可见；
-Document text：可能只有 U+FFFC/object replacement；
-Monaco source text：不可见；
-selected text：不可可靠取得；
-caret：不可可靠取得；
-editor actions/editable content：不可作为普通 EditableText 使用。

这主要是当前 Electron/VS Code accessibility tree 的 exposure boundary，不应全部归为 port bug。

项目仍可改善：

-清晰检测“Document 可见但 editor content 不可用”；
-提供明确 NotSupported，而不是空文本；
-考虑 VS Code extension/DBGp/editor API 作为应用专用 adaptation。

---

# 17. D-Bus / COM adaptation

Linux 端将部分 COM-like API 适配为 D-Bus proxy，这个方向合理。不能因为没有 IDispatch、SafeArray 或 Windows COM server 就直接判失败。

当前适配具有：

- session/system bus proxy；
-路径/interface/member 调用；
-基础 scalar type conversion；
-ComObject/ComObjActive/ComObjGet 类入口；
-D-Bus error 到 AHK error 的映射；
-proxy lifetime。

但历史 blocking 问题并未全部关闭：

```text
core_com_dbus_linux.cpp
dbus_connection_send_with_reply_and_block(..., -1, ...)
```

仍存在无显式 timeout、无 message pump 的同步 call。AT-SPI 已修复，不代表 COM adaptation 也修复。

用户影响：

-服务挂住时整个 AHK thread 可无限等待；
-GUI/timer/hotkey responsiveness 下降；
-service disappear/restart 的 recovery 不统一；
-嵌套 D-Bus callback 中调用 proxy 有 deadlock/reentrancy 风险。

---

# 18. GUI / Menu / Tray

## GTK3 GUI

已经拥有相当完整的控件和事件基础。linux.19 还修复了两个真实用户问题：

- GUI events 曾通过 Linux no-op `PostMessage()` 丢失；
-窗口被设置 app-paintable 却没有 draw handler，导致透明。

release 对 Button Click、window Close、Menu activation、背景渲染和 callback 首参数做了 real VM/Xvfb 验证。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/releases/tag/v2.0.26-linux.19))

仍缺 broad differential 的领域：

- Windows AHK GUI event ordering；
-callback reentrancy；
-destroy during callback；
-IME focus lifecycle；
-fractional scaling；
-multi-monitor move；
-theme restart；
-native Wayland vs X11 focus；
-high-DPI coordinate semantics。

## Menu

Menu popup、MenuBar、check/disable、icon 和 callbacks 有实际实现。风险主要是 GTK event ordering、object destroy lifetime 和 tray/dbusmenu 同步，而不是 API 不存在。

## Tray

SNI + dbusmenu 是真实 implementation，不是单纯 GTK status icon stub。

但当前 connection 状态主要以已有 D-Bus connection pointer 判断；当 bus、watcher 或 tray host restart 时：

-connection 可能存在但已 disconnected；
-dispatch 会直接 return；
-没有完整 NameOwnerChanged/re-register state machine；
-菜单状态和 icon 生命周期可能停留在旧状态。

GNOME 无 tray host 时 silent no-op 是环境边界；KDE 中 SNI 是自然路径，但当前没有真实 KDE host matrix。

---

# 19. Clipboard

## 当前可用

- X11 CLIPBOARD；
-XFixes ownership change；
-Wayland text offer；
-GNOME extension watcher；
-A_Clipboard text；
-ClipWait；
-OnClipboardChange 的基本路径；
-large Unicode text；
-SendText paste fallback 的 bounded restore。

## 与 Windows `ClipboardAll` 的距离

当前核心实现主要处理：

-UTF8_STRING / XA_STRING；
-text/plain UTF-8。

未形成完整：

-图片；
-custom MIME；
-HTML/RTF；
-file list；
-多 representation；
-owner/process exit 后持久保存；
-rich clipboard snapshot/restore。

因此即使 `ClipboardAll()` 入口存在，也不能把它评为 Windows rich clipboard semantic parity。

## race

- X11 selection 是异步 ownership protocol；
-目标 request、owner exit、clipboard manager 可能竞态；
-OnClipboardChange 的 type 往往只能近似 0/1，不能稳定区分 non-text type；
-pure Wayland 通用 wl_data_device 并没有完整全局 watcher；
-SendText fallback 恢复前，外部 clipboard ownership 可能已变化；
-X11↔Wayland bridge 依赖 compositor/desktop clipboard bridge，不是项目自己保证。

---

# 20. 测试证据质量

## 20.1 证据级别

| Level | 定义                                     | 当前例子                                                     |
| ----- | ---------------------------------------- | ------------------------------------------------------------ |
| **A** | external differential / real-host oracle | 官方 Windows v2.0.26 fixture；GNOME VM + IBus；真实 GTK/Qt/Java/LibreOffice |
| **B** | independent process oracle               | X11 recorder/injector；外部 DBGp client；systemd/inputd observer |
| **C** | project-controlled integration           | Wayland/XWayland suites、scenario runner、soak、package scripts |
| **D** | internal/self assertion                  | runtime set/get、mock、API existence、static contract        |
| **E** | documentation claim                      | README、MODULE_MATRIX、报告文字                              |

## 20.2 Windows differential gate

这是项目非常值得肯定的新进展。

当前 fixture：

-来自官方 AutoHotkey v2.0.26 x64 portable；
-校验下载 archive 和 exe SHA-256；
-至少重复运行两次，初始 fixture 重复三次且 byte-identical；
-manifest 固定 release、hash、时间、repeat count；
-Linux sender 是独立 XTEST C tool，不调用本 port 的 Send。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/tests/differential/README.md))

Trace schema 包含：

```text
schema
seq
case
kind
vk
sc
text
name
```

它比较：

-严格 event order；
-VK；
-SC；
-Unicode scalar；
-callback/hotkey/hotstring name；
-terminal state。

它故意不比较 timestamp 和 device ID。

### 当前覆盖量

当前只有 7 组 case，约 20 行 JSONL，其中实际语义 observation 约 18 行：

1. InputHook `a`、Shift+A、F13；
2. `^F11`；
3. `F12 Up`；
4.动态 Hotstring；
5. wildcard；
6. case-sensitive Hotstring；
7. inside-word 和 end-char。

### 尚未覆盖

- SendMode/SendEvent/SendInput/SendPlay；
- SendLevel/InputLevel；
-A_PriorHotkey；
-custom combo；
-remap；
-tilde target delivery；
-HotIf timing；
-multi-script；
-Window/Control；
-GUI；
-clipboard；
-files/process/language；
-exceptions/error type；
-timing、repeat cadence；
-IME；
-native Wayland。

项目自己的 differential README 也明确列出 custom combo/remap、SendInput/Play timing 和其余 Hotstring options 尚未覆盖。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/tests/differential/README.md))

**结论：**

> 这是高质量但非常小的 Level A slice。它能证明一小段 X11 input observable sequence 精确匹配，不能证明“AHK v2 semantic compatibility 已建立”。 

## 20.3 测试数字到底能证明什么

README 宣称：

- 1143/1143 X11/headless；
- 17/17 native Wayland；
- 234/234 XWayland；
- ASan；
- TSan-input；
-四 distro containers；
-soak；
-Windows differential。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/README.md))

合理解释如下：

| 证据                         | 能证明                                          | 不能证明                                        |
| ---------------------------- | ----------------------------------------------- | ----------------------------------------------- |
| 1143 X11/headless assertions | 大量 parser/runtime/API regression 没破坏       | 1143 个独立 Windows semantics                   |
| 17 native Wayland            | 特定 headless sway downgrade/protocol path      | GNOME/KDE/native apps compatibility             |
| 234 XWayland                 | 特定 XWayland harness                           | 混合 native Wayland desktop                     |
| distro containers            | 可构建、基础 runtime、部分 Xvfb smoke           | GNOME/KDE/portal/IME/AT-SPI desktop integration |
| ASan                         | 被执行路径未发现 ASan 错误                      | 无 UAF/leak                                     |
| TSan-input                   | headless和若干 X11 input oracle 路径未报告 race | 全程序、GTK、D-Bus、Wayland、inputd 无 race     |
| package lifecycle            | 构建、安装、运行、卸载脚本有效                  | 所有发行版升级/rollback/权限迁移                |
| headless sway                | 特定 wlroots protocol 和 no-XWayland downgrade  | 全部 wlroots compositor                         |
| real GNOME VM                | 指定版本、指定测试场景有效                      | KDE、其他 GNOME version、所有应用               |
| soak                         | 短期 event count 和 RSS slope                   | 8/24 小时可靠性                                 |

TSan job实际运行 headless tests、X11 oracle、keymodel、raw capture 和 suppression scope，而不是 GUI、AT-SPI、Wayland、inputd daemon 全路径。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/.github/workflows/ci.yml))

## 20.4 SUPPORT_MATRIX 的 pass/skip/not-run

当前 committed `SUPPORT_MATRIX.md` 有 23 个 scenario：

- **pass：5**
- **skip：3**
- **not-run：15**

pass 只有：

- clipboard_roundtrip；
-highfreq_cpu；
-hotkey_basic；
-hotkey_thisprior；
-keydown_longpress。

而 custom combo、evdev hotplug/remap、input mux、multiscript conflict、panic escape、AT-SPI matrix、SNI registration 等在该 generated matrix 中都是 not-run。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/tests/scenarios/SUPPORT_MATRIX.md))

这不等于 CI 完全没有对应 oracle；有些能力通过 `tests/oracle` 的独立 job 运行。它说明的是：

> 当前 SUPPORT_MATRIX 不是全项目验证总表，而是某一次 scenario runner 的生成快照。

因此 README 的总体 assertions 数量与 SUPPORT_MATRIX 不能相加，也不能用 pass/skip/not-run 混合计算“通过率”。

---

# 21. 长期可靠性、并发和 lifecycle

## 当前已有

- 30 秒 CI mixed soak；
-release 记录的 VM 5 分钟；
-可参数化到 86400 秒，但 24 小时结果明确未运行、未声称；
-hotkey + Hotstring + clipboard + Timer mixed counts；
-RSS slope gate；
-inputd hotplug；
-client disconnect；
-watchdog；
-panic；
-portal owner restart；
-compositor STOP/CONT；
-TSan input subset。

CI 明确把常规 soak 设置为 30 秒；script 默认 60 秒、允许手动 86400 秒。release 同样声明 24 小时 soak 已取消，不做 24 小时 claim。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/tests/oracle/run_mixed_soak.sh))

## 仍缺 production 证据

| 故障/负载                         | 当前覆盖判断                               |
| --------------------------------- | ------------------------------------------ |
| 1 小时                            | 无正式 gate                                |
| 8 小时                            | 无                                         |
| 24 小时                           | 无，明确未运行                             |
| millions of physical events       | 无明确证据                                 |
| registration/unregistration storm | 无                                         |
| 多脚本启动退出风暴                | 无                                         |
| inputd restart while keys held    | 部分、缺完整 physical reconciliation       |
| X server disconnect/restart       | 无可靠恢复模型                             |
| Wayland compositor restart        | STOP/CONT 不等于 socket disconnect/restart |
| D-Bus daemon restart              | 不完整                                     |
| portal service restart            | 有窄 oracle                                |
| GNOME Shell reload                | 无                                         |
| device hotplug storm              | 有单次/少量路径，非 storm                  |
| IME engine switch storm           | 无                                         |
| clipboard owner storm             | 无                                         |
| layout/keymap replacement storm   | 无                                         |

## 源码并发风险

重点风险包括：

- process-global input state；
-Xlib/XI2/raw-ring 和 AHK main thread 间状态；
-callback queued 后对象销毁；
-inputd hotplug 和 ppoll snapshot；
-Wayland registry object removal；
-D-Bus connection跨线程；
-AT-SPI nested calls；
-tray reconnect；
-static cached Display/session/backend；
-Send 临时 X keymap lease；
-clipboard ownership asynchronous restore。

当前代码已经有多处 mutex、main-loop queue、object guard 和 ppoll snapshot 延迟 mutation，这说明作者实际处理过并发问题。但 TSan coverage 尚不能外推到全部这些路径。

---

# 22. Packaging / release / supply chain

## 已确认 release assets

linux.19 release 列出了：

- deb；
-RPM；
-tarball；
-AppImage；
-VSIX；
-CKSUMS；
-UNSIGNED marker；
-另有 source assets。

release workflow 会构建 deb/RPM/AppImage/tar/VSIX，检查 payload、icon、examples、inputd、systemd unit，并执行 package install/run/uninstall。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/releases/tag/v2.0.26-linux.19))

## Provenance

release-tag workflow 使用 `actions/attest@v4`，对 package 和 checksum 建立 GitHub/Sigstore attestation；同时验证 attestation。OpenPGP 未配置长期 pinned key 时明确标记 UNSIGNED，而不是生成一次性假身份。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/.github/workflows/ci.yml))

这是合理的 supply-chain policy。

## 限制

### AppImage

AppImage 可以携带 `ahk-inputd` binary，但不能自行完成：

-安装 root systemd service/socket；
-配置 `input` group；
-配置 udev/uinput permission；
-安装/启用 GNOME Shell extension；
-提供 host portal permission。

因此“AppImage 含 daemon”不等于“AppImage 用户获得 global evdev backend”。

### Flatpak

存在 manifest 只能证明可描述构建，不能证明：

-AT-SPI portal；
-IME visibility；
-global input；
-host clipboard；
-portal consent；
-extension interaction；
-self-contained pack；

在真实 Flatpak sandbox 中可用。README 已承认需要 dedicated host。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/README.md))

### 升级/回滚

package scripts有 install/remove test，但生产升级仍需验证：

-旧 socket/service remains；
-group/permission migration；
-running daemon 被替换；
-protocol v1→v2 compatibility；
-GNOME extension version rollback；
-user service state；
-AppImage self-update；
-AUR rebuild；
-ABI dependency变化。

---

# 23. 文档漂移

| 功能                | 文档 A                                     | 文档 B / 当前证据                                            | 当前判断                                                     |
| ------------------- | ------------------------------------------ | ------------------------------------------------------------ | ------------------------------------------------------------ |
| InputHook callbacks | CHECK_REPORT 某历史段落称 OnChar 不工作    | 当前源码已有 callback queue，MODULE_MATRIX/CI 有 input tests | 历史记录未标清时会误导                                       |
| Tray                | CHECK_REPORT 历史部分仍可见未实现状态      | 当前已有 SNI/dbusmenu                                        | harmless history，但 generated report 不宜混成 current       |
| SUPPORT_MATRIX      | 5 pass、3 skip、15 not-run                 | README 写 1143/17/234                                        | 属不同测试集合，但普通用户极易误读                           |
| ClipboardAll        | API/matrix可能显示 implemented             | 源码主要 text MIME                                           | 当前 user-facing claim 过宽                                  |
| normalized event    | README 写 versioned normalized events      | X11 仍绕过 backend interface                                 | implementation claim真实，但 architecture-centrality 容易夸大 |
| native Wayland      | README 一行概括 portal/GNOME/evdev/wlroots | KDE real host、libei、global char stream缺失                 | 需要按 compositor 拆表                                       |
| inputd multi-client | daemon确实多客户端                         | arbitration仅 any-suppress，wire protocol不含 owner/provenance | “multi-client”不等于 AHK multi-script parity                 |
| Fcitx5              | README已写 protocol-only                   | 部分 matrix 容易简写成 IME supported                         | README 当前措辞相对诚实                                      |
| VS Code/AT-SPI      | 概括为 VS Code matrix                      | 详细 matrix明确 Monaco 不暴露                                | 应在主 capability 表中保留限制                               |
| soak                | workflow可配置 24h                         | release明确只有30秒/5分钟                                    | 当前 release措辞是准确的                                     |

文档最大问题不是 README 完全失实，而是：

> 多个历史报告、generated report、capability matrix 和当前 snapshot 并列存在，缺乏统一的“valid for commit / generated at / environment / evidence level”元数据。

---

# 24. Windows AHK v2 脚本迁移 corpus

| 真实脚本类型                 | X11/XWayland                      | GNOME Wayland                         | KDE Wayland        | wlroots             | 迁移判断               |
| ---------------------------- | --------------------------------- | ------------------------------------- | ------------------ | ------------------- | ---------------------- |
| global hotkey launcher       | 通常可用                          | portal/extension/evdev                | portal能力依赖环境 | 可用路径较多        | unchanged～minor       |
| CapsLock navigation layer    | evdev/inputd较好，纯X11受组合限制 | 需要 privileged broker                | 同左               | evdev+uinput较可行  | minor～major           |
| text expansion               | X11较可用                         | IBus场景可用；非IME global stream受限 | Fcitx real证据不足 | 需 layout stream    | minor～major           |
| clipboard manager            | 纯文本较可用                      | text可用，rich受限                    | 同                 | 同                  | major for rich data    |
| app-specific HotIf           | X11窗口criteria可用               | native app identity不统一             | 同                 | compositor-specific | minor～major           |
| InputHook command palette    | X11较可用                         | 纯native global input不足             | 不足               | evdev可做           | major                  |
| window tiling                | EWMH实用                          | 需要 GNOME API/extension              | 需要 KWin API      | sway IPC更自然      | Linux-specific rewrite |
| GUI utility                  | 多数可运行                        | GTK3可运行                            | GTK3可运行         | GTK3可运行          | unchanged～minor       |
| form automation              | AT-SPI toolkit-dependent          | GTK/部分Electron                      | Qt基础较有希望     | toolkit-dependent   | minor～impossible      |
| Unicode/Chinese workflow     | X11 + IBus可用                    | 当前最强IME路径                       | Fcitx/KDE证据弱    | 依IME/compositor    | minor～major           |
| multi-script setup           | provenance/grab冲突               | broker arbitration问题                | 同                 | 同                  | major                  |
| long-running tray automation | 可用但restart弱                   | tray host/extension差异               | SNI更自然但未实测  | host-dependent      | minor～major           |

---

# 25. Compatibility Pyramid

| Level                            | 已实现 | 已验证 | 最大缺口                                                  |
| -------------------------------- | -----: | -----: | --------------------------------------------------------- |
| **1 — Language Runtime**         | **94** | **80** | edge error/thread semantics 和 broad Windows differential |
| **2 — Linux Utility Runtime**    | **90** | **72** | blocking D-Bus、rich clipboard、跨发行版 lifecycle        |
| **3 — Basic Desktop Automation** | **72** | **61** | backend divergence、native Wayland、Send modifier/state   |
| **4 — Advanced AHK Semantics**   | **57** | **47** | SendLevel/provenance、custom combo、remap、multi-script   |
| **5 — Cross-desktop Automation** | **51** | **35** | KDE/Fcitx/Flatpak real host、libei、foreign windows       |
| **6 — Production Runtime**       | **42** | **33** | inputd fail-open、安全政策、长期 soak、reconnect/recovery |

“已验证”分数评价仓库现有证据，而不是本次审计重新运行后的通过率。

---

# 26. 维度评分

| 维度                       | 实现度 | 证据可信度 | 实际兼容度 |
| -------------------------- | -----: | ---------: | ---------: |
| AHK v2 language/runtime    |     94 |         78 |         88 |
| X11                        |     84 |         74 |         72 |
| XWayland                   |     77 |         62 |         63 |
| GNOME Wayland              |     61 |         48 |         47 |
| KDE Wayland                |     47 |         28 |         33 |
| wlroots Wayland            |     70 |         54 |         56 |
| Hotkey                     |     80 |         70 |         69 |
| Custom combo / remap       |     56 |         44 |         44 |
| Hotstring                  |     71 |         57 |         55 |
| InputHook                  |     69 |         53 |         52 |
| Send/input injection       |     76 |         65 |         57 |
| SendLevel/InputLevel       |     43 |         36 |         29 |
| Unicode                    |     81 |         67 |         68 |
| IME                        |     67 |         51 |         48 |
| Win*                       |     75 |         65 |         62 |
| Control*/AT-SPI            |     61 |         50 |         45 |
| Clipboard                  |     69 |         54 |         54 |
| GUI/Menu/Tray              |     78 |         56 |         60 |
| DllCall/interop            |     86 |         68 |         74 |
| Multi-script behavior      |     48 |         39 |         31 |
| Stability/lifecycle        |     54 |         43 |         39 |
| Security/permissions       |     52 |         45 |         40 |
| Packaging                  |     79 |         58 |         61 |
| Test quality               |     72 |         72 |         66 |
| Cross-desktop verification |     52 |         36 |         39 |
| Production readiness       |     46 |         38 |         38 |

---

# 27. Issue 分级

## P0-1 — inputd 在 replay backend 失败后仍持有独占 grab

**状态：已确认**

**当前代码证据：**  
`source/linux/inputd/inputd.c`：

- `scan_devices()` lines 419–474；
- `open_uinput_replay()` lines 140–162；
- `replay_key()` lines 163–175；
- `main()` lines 696–700；
- main loop lines 785–786。

**与旧 audit 的关系：**  
旧的“缺少统一、多客户端、fail-open broker”已经部分解决；这是新 broker 引入的新 failure mode。

**用户影响：**  
可能暂时失去整个键盘输入。

**触发场景：**

- `/dev/uinput` 权限缺失；
- module/device 不存在；
- `UI_DEV_CREATE` 失败；
- uinput fd 运行中失效；
-write 返回 EIO/ENODEV。

**当前测试覆盖：**  
panic、SIGKILL、watchdog、socket activation 有覆盖；未看到“grab 成功但 uinput 初始化/写失败”的 external fail-open oracle。

**为什么现有测试可能漏掉：**  
典型 CI 要么 protocol-only，要么有正常 uinput；正常 crash 测试会由 kernel 释放 fd，不会触发该顺序问题。

**建议修复：**

1.先创建/health-check replay device；
2.再扫描并 grab；
3.任一 replay write 失败立即 `release_all_grabs()`；
4.周期 verify uinput；
5.提供 optional bypass mode；
6.加入 fault injection oracle。

**复杂度：中**

**是否 platform inherent：否**

---

## P0-2 — X11 Hotkey 的 SendLevel/InputLevel gate 方向与官方语义相反

**状态：已确认**

**当前代码证据：**  
`core_hotkey_linux.cpp::LinuxHandleHotkeyEvent` 附近 lines 718–721。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/source/linux/core/core_hotkey_linux.cpp))

**官方 reference：**  
synthetic event 仅在 `SendLevel > InputLevel` 时触发。([Doggy 8088](https://doggy8088.github.io/AutoHotkeyDocs/docs/lib/SendLevel.htm))

**与旧 audit 的关系：**  
旧的 SendLevel/InputLevel 缺口没有关闭；现在已有实现，但实现条件本身错误。

**用户影响：**  
高级 hotkey、remap、nested Send 和跨脚本链路会触发错误或不触发。

**触发场景：**

```ahk
#InputLevel 5
a::...

SendLevel 10
SendEvent "a"
```

**当前测试覆盖：**  
mixed soak使用少量 SendLevel，但不对官方规则做 differential；Windows differential 没有该 case。

**为什么会漏：**  
测试通常只检查“某个 hotkey 是否触发”，而没有建立完整 InputLevel×SendLevel 矩阵。

**建议修复：**

-统一定义 `ShouldSyntheticTrigger(sendLevel,inputLevel)`；
-Hotkey、Hotstring、InputHook、remap 全部调用同一函数；
-加入 0/1/5/10/100 矩阵；
-同进程和两进程分别差分。

**复杂度：低至中**

**是否 platform inherent：否**

---

## P0-3 — 跨进程 synthetic provenance 无法实现 Windows AHK 多脚本语义

**状态：已确认的架构缺口**

**当前代码证据：**

- `AhkInputEvent` 没有 script/transaction identity；
-inputd v1 frame 只有 code/value/time；
-X11 self marks 是进程内 heuristic；
-uinput 不能携带 SendLevel；
-injection 不以 normalized transaction 为中心。

**与旧 audit 的关系：**  
字段和 heuristic 是实质进步，但旧的 synthetic identity 问题只解决到同进程、部分 X11。

**用户影响：**

- Script A 的 Send 被 Script B 当 physical；
-SendLevel 无法跨进程；
-remap 互相触发；
-Hotstring replacement 递归；
-多脚本 self-trigger prevention 不一致。

**当前测试覆盖：**  
有 multi-process Hotstring observation；没有跨脚本 SendLevel/provenance differential。

**为什么会漏：**  
两个脚本都能收到 physical key，不代表能正确分类 synthetic key。

**建议修复：**

- input protocol v2；
- injection transaction ID、producer UID/PID/script nonce、SendLevel；
-专用 tagged uinput device 或 broker-owned injection；
-broker同时管理 capture+injection；
-定义 cross-process policy，而不是尝试伪装 Windows hook 的所有细节。

**复杂度：高**

**是否 platform inherent：部分。Linux input stack 不原生携带 AHK metadata，但项目可以通过 broker 协议和专用设备改善。**

---

## P1-1 — normalized model 尚未成为唯一 input pipeline

**状态：已确认**

**证据：** X11 backend 明示继续使用既有 X11 paths，不通过统一接口；Send injection 也绕开 normalized event。

**用户影响：** 修复一个 backend 不会自动修复其他 backend；同一语义多份实现漂移。

**测试覆盖：** normalized trace oracle 验证 struct/trace，不证明 architecture centrality。

**建议：** 所有 capture 都先产生 normalized event；所有 matcher、level gate、suppression、thread dispatch 只消费它；Send/remap 生成带 transaction metadata 的 normalized synthetic event。

**复杂度：很高**

**platform inherent：否**

---

## P1-2 — mux 缺少 health-aware recovery 和动态重新协商

**状态：高概率，部分静态确认**

**证据：** capability route 与 live health 分离；session classification/cache、各 backend reconnect 独立；Wayland connection failure sticky。

**用户影响：** portal permission revoked、GNOME extension restart、inputd disconnect、compositor restart 后，已注册 hotkey 可能长期失效或仍指向死亡 lane。

**测试覆盖：** portal owner restart、compositor STOP/CONT、inputd reconnect 有窄测试；不是完整断线/re-auth/resubscribe。

**建议：** backend state machine、generation counter、subscription reconciliation、held-key reset、exponential backoff、脚本可查询 degraded status。

**复杂度：高**

**platform inherent：否**

---

## P1-3 — SendMode Input 与显式 SendInput 不等价，且不同 backend 语义坍缩

**状态：已确认**

**证据：** `core_input_linux.cpp::LinuxModeSuppressSelf` 和 BIF wrappers。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/source/linux/core/core_input_linux.cpp))

**用户影响：** 默认 Send 可能自触发；SendPlay、SendEvent、SendInput 可观察差别小于 Windows；已有脚本依赖的 hook buffering/interrupt 行为变化。

**测试覆盖：** 没有 official Windows Send differential。

**建议：** 默认 Send(Input) 与显式 SendInput 共用语义；明确每 backend 可提供的 guarantee；无法满足时抛清晰 compatibility warning，不要静默退化。

**复杂度：中至高**

**platform inherent：部分**

---

## P1-4 — inputd multi-client arbitration 不足以表达 suppress/pass-through/remap

**状态：已确认**

**证据：** any-client-suppress，全部 client receive；protocol 无 priority/owner/remap transaction。

**用户影响：** 多 remapper 重复输出、pass-through 意图被其他脚本覆盖、脚本无法判断冲突。

**测试覆盖：** framing、多 client observation；无 A/B/C arbitration corpus。

**建议：** subscription priority、exclusive owner、observe-only、replacement transaction、conflict event、per-UID policy。

**复杂度：高**

**platform inherent：否**

---

## P1-5 — pure native Wayland 无通用 continuous text/input stream

**状态：已确认**

**证据：** Wayland seat 只向本进程 surface 发送事件；Portal/GNOME shortcut不是 arbitrary stream；inputd pure-Wayland character decoding又需要额外 layout source。

**用户影响：** 无 XWayland/evdev/extension 时，Hotstring、InputHook、advanced remap 不能工作。

**测试覆盖：** no-XWayland downgrade、wlroots protocol；没有 generic GNOME/KDE continuous stream。

**建议：** broker维护 xkb keymap/layout state；结合 compositor layout notifications；明确 privileged requirement；InputCapture portal 可用时加入。

**复杂度：高**

**platform inherent：捕获限制属于平台，layout/broker integration 属于项目缺口**

---

## P1-6 — Wayland display/compositor restart 后缺乏完整恢复

**状态：高概率**

**证据：** registry remove 处理不完整、connection failure sticky、对象不自动重建。

**用户影响：** compositor reload、logout/login、nested compositor 重启后 Send/pointer/screenshot 永久失效直到重启脚本。

**测试覆盖：** STOP/CONT 不会重建 socket/object，不能覆盖真正 restart。

**建议：** disconnect detection、destroy all proxies、重新 connect/registry bind/keymap upload/subscription replay。

**复杂度：高**

**platform inherent：否**

---

## P1-7 — D-Bus adaptation 仍有无限 blocking 和 restart/reentrancy 缺口

**状态：已确认**

**证据：** COM D-Bus path仍有 `send_with_reply_and_block(...,-1,...)`；backend/IME还有秒级同步 probe。

**用户影响：** service hang 可冻结 AHK thread、GUI、timer、hotkey event processing。

**测试覆盖：** AT-SPI pending-call source guard不能覆盖 COM proxy。

**建议：**统一 async pending-call wrapper、总预算、cancel、main-loop pump、service owner generation。

**复杂度：中至高**

**platform inherent：否**

---

## P1-8 — production-duration reliability 证据不足

**状态：已确认**

**证据：** CI soak 30 秒；VM 5 分钟；24 小时明确未运行。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/5bc26e5a78d11888c60bc64511e1ea470a099df8/tests/oracle/run_mixed_soak.sh))

**用户影响：** fd leak、state drift、rare race、D-Bus reconnect、grab leak 可能只在数小时后出现。

**建议：** nightly 1h、weekly 8h、release-candidate 24h；事件数、RSS、fd、threads、D-Bus objects、Wayland proxies 和 latency distributions 全部记录。

**复杂度：中，基础设施成本较高**

**platform inherent：否**

---

## P1-9 — Tray/clipboard/session service restart recovery 不完整

**状态：高概率**

**用户影响：** D-Bus daemon、SNI watcher、clipboard owner/extension restart 后功能静默消失。

**测试覆盖：** portal restart有窄 test；缺 tray watcher和D-Bus daemon restart。

**建议：** NameOwnerChanged、connection generation、re-register、state replay、explicit degraded diagnostic。

**复杂度：中**

**platform inherent：否**

---

## P2

| 问题                                          | 状态      |
| --------------------------------------------- | --------- |
| libei/EIS/RemoteDesktop injection 未实现      | 已确认    |
| KDE Plasma real-host matrix 缺失              | 已确认    |
| real Fcitx5 Chinese E2E 缺失                  | 已确认    |
| Flatpak host E2E 缺失                         | 已确认    |
| OEM/media/vendor scan code coverage不足       | 高概率    |
| rich ClipboardAll/image/custom MIME 缺失      | 已确认    |
| native Wayland Win* foreign-window能力不足    | 平台+实现 |
| LibreOffice virtual table cell API 缺失       | 已确认    |
| Chromium/Firefox复杂 accessibility matrix不足 | 已确认    |
| Unicode grapheme/ZWJ differential不足         | 已确认    |
| custom combo完整 Windows matrix缺失           | 已确认    |
| keymap replacement/layout daemon restart不足  | 高概率    |

## P3

| 问题                                                        | 状态   |
| ----------------------------------------------------------- | ------ |
| CHECK_REPORT 历史章节与current状态混杂                      | 已确认 |
| SUPPORT_MATRIX缺 commit/env/timestamp/evidence-level header | 已确认 |
| 部分 silent fallback/no-op 需要 warning                     | 已确认 |
| source comments仍引用旧 audit编号而非当前设计文档           | 已确认 |
| magic timeout、polling interval和环境变量较多               | 已确认 |
| capability表需要区分“implemented”和“verified”               | 已确认 |
| 用户可查询的 backend diagnostics/status API不足             | 高概率 |

---

# 28. Confirmed Improvements Since Previous Audits

1. **X11 window enumeration 已重构。**  
   不再是单纯 XQueryTree；EWMH、ICCCM 和 unmanaged fallback 已建立。

2. **external Control* fake success 已删除。**  
   不支持的外部操作现在可以明确失败，自有 GUI local state 与外部目标状态已经区分。

3. **AT-SPI 已从同步粗糙调用升级。**  
   pending call、message pump、timeout budget、cache、WinTitle scoping 和 toolkit matrix 都是实质改善。

4. **X11 Hotstring/InputHook observation 架构改善。**  
   XI2 raw、多进程观察、selected suppression 和 independent target delivery 比旧 broad-grab 模型成熟。

5. **key model 明显成熟。**  
   canonical evdev/set-1 SC、左右 modifier、XKB layout、AltGr 和 AZERTY oracle 已建立。

6. **`ahk-inputd` 是真实的多客户端 broker。**  
   包含 systemd socket activation、hotplug、panic、watchdog、client disconnect cleanup，而非单进程 demo。

7. **IBus 不再只是 stub。**  
   real VM/libpinyin commit 和跨进程 capture 是高价值进展。

8. **测试体系不再完全 self-validation。**  
   独立 X11 recorder/injector、external process oracle、真实 toolkits 和官方 Windows fixture 显著提高可信度。

9. **GUI 用户级 regression 已修复。**  
   event callbacks、window drawing、callback argument 和 VS Code debugger/package 问题在 linux.19 被真实修正。

10. **release engineering 已生产化一部分。**  
    deb/RPM/AppImage/tar/VSIX、install/remove、checksums、GitHub attestation 和 explicit UNSIGNED policy 都是成熟工程实践。

---

# 29. 修复之后新增的风险

新 architecture 引入了旧版本没有的 failure modes：

| 新机制                    | 新风险                                                       |
| ------------------------- | ------------------------------------------------------------ |
| normalized event          | schema/version迁移、字段丢失、backend bypass                 |
| per-hotkey mux            | 同脚本 state 来自不同 backend、lane健康状态不一致            |
| inputd broker             | privileged attack surface、arbitration、protocol compatibility |
| multi-client              | owner、priority、duplicate remap、stalled client policy      |
| XTEST provenance ring     | 高频误匹配、跨进程丢失                                       |
| broker + local fallback   | device identity和state不一致                                 |
| 多种 injection backend    | observable semantics不同                                     |
| AT-SPI cache              | stale objects、service generation、focus transfer            |
| 多 D-Bus服务              | restart、nested call、blocking、connection ownership         |
| clipboard fallback        | 敏感数据暴露、restore race                                   |
| Wayland private protocols | compositor version和restart lifecycle                        |
| package service           | 旧 daemon、新 runtime protocol mismatch                      |

---

# 30. 下一步最值得做的 10 项工作

| 排名 | 工作                                         | 为什么 / 实现路径                                            | 验收标准                                                    |
| ---: | -------------------------------------------- | ------------------------------------------------------------ | ----------------------------------------------------------- |
|    1 | 修复 inputd grab/replay fail-open            | replay device先建、two-phase grab、write failure立即release  | 强制uinput初始化/运行中失败时，外部物理接收器仍收到全部按键 |
|    2 | 修复统一 SendLevel/InputLevel 规则           | 单一 shared predicate，Hotkey/Hotstring/InputHook/remap共用  | 官方Windows全矩阵逐行一致                                   |
|    3 | protocol v2：script/transaction/provenance   | broker-owned capture+injection、producer nonce、SendLevel、device、origin | 两脚本 nested Send/remap不误分类                            |
|    4 | 定义 inputd arbitration                      | observe/suppress/exclusive/remap priority、conflict event、per-UID policy | A/B/C scenario结果确定且文档化                              |
|    5 | 让 normalized event 成为唯一 pipeline        | X11也先normalize，matcher和dispatch统一；Send产生synthetic transaction | backend-specific shortcut显著减少，trace覆盖全链            |
|    6 | health-aware mux/reconnect                   | generation state、动态failover、resubscribe、held-state reconciliation | portal/extension/inputd/compositor重启后同进程自动恢复      |
|    7 | 扩大官方 Windows migration differential      | Send modes、InputLevel、combo、remap、HotIf、多脚本、error和GUI | 至少100–200个稳定 observable cases，不以内部state为oracle   |
|    8 | pure-Wayland key/text model + libei          | broker独立xkb layout source；RemoteDesktop/EIS用于consented injection | GNOME/KDE无XWayland下可Send Unicode，并明确capture边界      |
|    9 | 消除 blocking D-Bus 并建立service generation | COM/IME/tray/portal统一pending-call、timeout、cancel、reconnect | service kill/restart期间GUI/hotkey不冻结，状态恢复          |
|   10 | production fault/host matrix                 | 1h/8h/24h；KDE、Fcitx5、Flatpak；device/clipboard/IME storm  | release前有可下载、commit-bound、external-oracle evidence   |

---

# 31. 证据缺口表

| 能力                  | 当前实现                            | 当前最高证据级别 | 缺少的关键验证                                           |
| --------------------- | ----------------------------------- | ---------------: | -------------------------------------------------------- |
| AHK language/runtime  | 广泛实现                            |              B/D | 大规模官方Windows语言/error differential                 |
| X11 basic Hotkey      | 有                                  |                A | 更多modifier、repeat、HotIf、冲突和long-run              |
| X11 wildcard/up       | 有                                  |         A narrow | 完整option矩阵和target pass-through                      |
| Send                  | 多backend                           |              B/C | 官方Windows Send mode differential                       |
| SendInput             | 显式路径有                          |                D | self-hook、buffering、external-hook oracle               |
| SendLevel/InputLevel  | 部分且hotkey条件错误                |                D | 同/跨进程官方 differential                               |
| Hotstring             | XI2 raw + replacement               |         A narrow | 完整options、focus、multi-script、IME边界                |
| InputHook             | callbacks/suppression/Match/Timeout |         A narrow | 多实例、reentrancy、KeyOpt全矩阵、IME                    |
| custom combo          | evdev状态机                         |                C | Windows prefix/roll-over/shared-prefix differential      |
| remap                 | XTEST/uinput                        |                C | atomicity、physical state、multi-script                  |
| scan code             | common physical model               |                B | OEM/media/non-US exotic和runtime keymap replacement      |
| input mux             | capability route                    |              C/D | disconnect、permission revoke、dynamic failover          |
| inputd capture        | multi-device grab                   |              B/C | partial-device、storm、held-key hotplug                  |
| inputd fail-open      | watchdog/panic/crash                |              B/C | uinput-init/write failure oracle                         |
| inputd security       | root:input socket                   |                D | adversarial peer、quota、authorization review            |
| IBus                  | real commit path                    |                A | cancel/focus/engine switch/continuous mixed Chinese      |
| Fcitx5                | protocol path                       |                C | 真实daemon/candidate/preedit/Chinese desktop             |
| supplementary Unicode | scalar path                         |                C | emoji/combining/ZWJ/grapheme differential                |
| GNOME Wayland         | portal/extension/evdev/AT-SPI/IBus  |         A narrow | compositor restart、permission revoke、native app corpus |
| KDE Wayland           | implementation基础                  |              D/E | 真实KDE VM、KGlobalAccel/SNI/Qt/Fcitx                    |
| wlroots               | VK/pointer/screencopy               |              B/C | 多个真实compositor，而非仅headless sway                  |
| XWayland              | X11 reuse                           |              C/B | mixed native/XWayland focus和clipboard                   |
| Win* X11              | EWMH                                |                B | 多WM、workspace、override-redirect、restart              |
| Win* native Wayland   | 极窄                                |              E/D | compositor-specific integration                          |
| GTK Control*          | AT-SPI                              |              A/B | 复杂tree/table/caret和long-run                           |
| Qt Control*           | AT-SPI                              |              A/B | 自定义Qt controls和KDE host                              |
| Java Control*         | 部分                                |              A/B | Value write和wrapper version matrix                      |
| LibreOffice           | 部分Action/Table                    |              A/B | virtual cell content/edit                                |
| Firefox               | 部分accessibility                   |              B/C | 真实网页widget corpus                                    |
| VS Code/Monaco        | 窗口/Document，不含source           |              A/B | extension-based semantic adaptation                      |
| Clipboard text        | 有                                  |              B/C | owner storm、X11↔Wayland races                           |
| ClipboardAll          | 不完整                              |                D | image/custom MIME/HTML/files/full restore                |
| GTK GUI               | 广泛                                |            A/B/C | event-order Windows differential、DPI/IME/multimonitor   |
| Tray SNI              | 有                                  |                C | KDE real host、watcher/D-Bus restart                     |
| D-Bus COM adaptation  | 有                                  |              C/D | timeout、service restart、nested/reentrant               |
| Packaging             | 广泛                                |              B/C | 跨版本upgrade/rollback、service protocol migration       |
| Long-running          | 短soak                              |                C | 1h/8h/24h和fault storms                                  |
| Multi-script          | 部分                                |         B narrow | synthetic provenance、arbitration、restart storm         |

---

# 32. Executive Summary

## 32.1 当前项目到底是什么水平

### 整体

> **advanced technology preview，useful for selected workloads。**

不是 experimental prototype，也还不是一般意义上的 beta。

### 按 backend

| Backend                         | 当前等级                                                     |
| ------------------------------- | ------------------------------------------------------------ |
| X11                             | useful for selected workloads；接近高级 technology preview   |
| XWayland                        | useful for selected workloads，但不能代表 native Wayland app |
| GNOME Wayland                   | advanced technology preview                                  |
| KDE Wayland                     | technology preview                                           |
| wlroots/sway                    | advanced technology preview，特定协议路径较强                |
| Production multi-script         | technology preview                                           |
| Language/runtime-only workloads | 接近 beta                                                    |
| 全桌面 AHK replacement          | 尚未到 beta                                                  |

---

## 32.2 距离真正 Linux AutoHotkey v2 还有多远

| 环境                                | 已解决估计 | 剩余主要问题                                                 |
| ----------------------------------- | ---------: | ------------------------------------------------------------ |
| X11                                 | **约 72%** | Send/InputLevel、多脚本provenance、combo、rich Control/clipboard、long-run |
| XWayland                            | **约 63%** | native/XWayland边界、focus、application identity、Wayland服务 |
| GNOME Wayland                       | **约 47%** | 无通用capture、无libei、extension/permission/reconnect、native Win* |
| KDE Wayland                         | **约 33%** | 真实host证据、Fcitx、KGlobalAccel/libei、Control/tray matrix |
| wlroots                             | **约 56%** | global text model、compositor diversity、restart/provenance  |
| Linux overall automation capability | **约 57%** | 高级键盘语义、跨desktop、真实应用边界                        |
| Linux overall production runtime    | **约 38%** | fail-open、安全、多脚本、fault recovery、长运行              |

这里的“已解决”按用户意图和行为能力计算，不按函数数量。

---

## 32.3 Windows 用户迁移脚本的现实成功率

以下是跨 Linux desktop 的工程估计，不是用户遥测；单项误差约 ±10–15 个百分点。

| 脚本类别                        | unchanged | minor modifications | major rewrite | impossible |
| ------------------------------- | --------: | ------------------: | ------------: | ---------: |
| Simple AHK scripts              |       65% |                 24% |            9% |         2% |
| Typical desktop automation      |       30% |                 34% |           26% |        10% |
| Advanced keyboard automation    |       12% |                 22% |           42% |        24% |
| Heavy Window/Control automation |        8% |                 17% |           45% |        30% |
| Chinese/IME workflows           |       20% |                 28% |           34% |        18% |
| Multi-script power-user setups  |        7% |                 17% |           46% |        30% |

环境差异很大：

- X11/XWayland 的 unchanged/minor 比例会高；
- GNOME + IBus 中文流程优于表中整体平均；
- KDE/Fcitx 当前证据不足，成功率应下调；
-纯 wlroots + evdev/uinput 对 keyboard layers 可较强，但部署要求高；
-依赖 Win32 Control messages、COM server 或复杂 Monaco/Canvas accessibility 的脚本最难迁移。

---

## 32.4 当前最危险的“完成度幻觉”

### 1. “370/370 functions implemented”

它说明入口覆盖，不说明同一脚本与 Windows v2.0.26 行为一致。尤其是 ClipboardAll、Send family、Win state 和 Control*。

### 2. “1143/1143 tests passed”

大量 assertion 属于 project-controlled regression、doccheck 或 internal contract。Level A Windows differential 目前只覆盖非常小的输入 slice。

### 3. “normalized input event 已建立”

struct 确实存在，但 X11、capture、broker 和 injection 尚未统一围绕它运行。它还不是系统总线。

### 4. “Wayland supported”

实际是：

```text
portal shortcuts
+ GNOME extension
+ privileged evdev/uinput
+ wlroots private protocols
+ AT-SPI
+ XWayland fallback
```

而不是一个统一、跨 GNOME/KDE/wlroots 的 Wayland AHK backend。

### 5. “inputd fail-open / multi-client”

client crash、daemon crash、watchdog和panic确实改善；但 uinput 初始化/写失败仍可能在 grab 存续时吞键，multi-client arbitration 也不足以表达 remap ownership。

---

## 32.5 当前最被低估的成熟能力

1. **X11 EWMH/ICCCM window enumeration 已真正修复。**
2. **AT-SPI 不只是 mock：已有 GTK、Qt、Java、LibreOffice、VS Code real-host matrix。**
3. **XKB/evdev 三层 key model、AltGr、AZERTY 和物理 SC 比旧 impression 成熟。**
4. **IBus/libpinyin commit 到 Hotstring/InputHook 是真实进展。**
5. **inputd 的 watchdog、panic、hotplug、socket activation 和 client cleanup 有相当工程深度。**
6. **Windows differential fixture 的 provenance 设计质量很高，只是覆盖面尚小。**
7. **release/package/Sigstore policy 已超过许多同阶段 technology preview 项目。**

---

## 32.6 最终回答

一个熟悉 AutoHotkey v2 的用户今天迁移脚本时：

### 已经可以合理承担

- Linux 文件、进程、网络、对象和语言 runtime 脚本；
- GTK3 GUI utility；
- X11/XWayland global launcher；
-普通 Hotkey、部分 Hotstring/InputHook；
-X11 window management；
-纯文本 clipboard；
-部分 GTK/Qt/Java/LibreOffice AT-SPI automation；
-GNOME/IBus 中经过限定的中文输入 workflow；
-wlroots 或 privileged evdev 环境下的部分 keyboard layer；
-打包后的独立 utility。

### 仍容易失败

-依赖精确 SendInput、自触发抑制或 SendLevel/InputLevel 的脚本；
-多个脚本互相 Send/remap/hotstring 的 power-user setup；
-完整 custom combo/CapsLock layer；
-纯 native Wayland 上的 global Hotstring/InputHook；
-KDE/Fcitx/Flatpak 未经实机验证的组合；
-复杂 Chromium/Electron/Monaco、Canvas、自绘 Qt 控件；
-rich ClipboardAll；
-需要 Win32 message/COM server parity 的脚本；
-compositor/D-Bus/service restart 后必须自动恢复的常驻脚本；
-要求 8–24 小时无人工干预的生产自动化。

最关键的工程判断是：

> **项目的主要差距已经不是“API 没写”，而是“多个 backend 中同一 AHK 语义没有统一身份、统一状态和统一故障模型”。**

在修复 inputd fail-open、SendLevel/InputLevel、跨进程 provenance、broker arbitration 和 backend recovery 之前，它适合由技术用户在明确限定的 workload 中使用，但不适合作为普通 AutoHotkey 用户可以无条件长期依赖的 Linux runtime。