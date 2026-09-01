# check_detail0901 — 缺失项细化与逐项解决方案

> 本文以 `audits/check0901.md` 为唯一问题基线，对应源码快照：
>
> ```text
> branch: linux-port
> HEAD:   5bc26e5a78d11888c60bc64511e1ea470a099df8
> release: v2.0.26-linux.19
> Windows baseline: AutoHotkey v2.0.26
> ```
>
> 本文不重新评分，也不把“已有入口”重复写成“已解决”。目标是把原审计中所有 P0/P1/P2/P3 缺口，以及正文中尚未完全落入 issue 表的输入、IME、GUI、测试和发布问题，拆成可实施的设计、修改落点、失败策略和验收标准。
>
> 本文中的“验收通过”均指未来应建立的 gate，不表示本轮已运行编译、实机或 root/systemd 测试。

---

# 0. 解决问题时必须共同遵守的工程约束

单独修一个条件判断不足以使这个 port 达到可长期依赖的状态。后续实现应共同遵守以下不变量。

## 0.1 安全不变量

1. 只要物理键盘已被 `EVIOCGRAB`，就必须存在一个已验证可写的 replay 输出；否则不得进入 grabbed 状态。
2. replay 失败、broker 主循环失去进展、设备拓扑无法确认或仲裁状态损坏时，默认动作必须是释放全部 grab。
3. 任何要求 suppress/remap 的客户端都不能仅因能连接 socket 就获得该权限；observe、suppress、exclusive、inject 必须是不同 capability。
4. 任何恢复动作都不能遗留“只有 key-down、没有 key-up”的状态。重新连接、切 lane、设备拔出和 compositor 重启都必须执行 held-key reconciliation。

## 0.2 语义不变量

1. 先统一判定 event 的来源、mode、SendLevel 与 provenance，再由 consumer-specific policy 决定是否接收；“统一语义入口”不等于所有 consumer 共用一个布尔条件。
2. 对 Hotkey/Hotstring 的 `#InputLevel`，可识别 synthetic event 的基础触发条件是严格大于：

   ```text
   send_level > input_level
   ```

3. 对 InputHook 的 `MinSendLevel`，SendEvent 类输入在 `send_level >= min_send_level` 时才收集；SendInput 与 SendPlay 不论阈值都应忽略；非 AutoHotkey/物理来源不因 MinSendLevel 被忽略。
4. `SendMode "Input"` 下的 `Send` 与显式 `SendInput` 必须进入同一语义路径；若某 backend 无法提供相同保证，应明确降级或报错，而不是静默表现成另一种 mode。
5. 一次 remap/replacement 必须作为同一事务跟踪，但不能承诺 capture suppression 与外部 injection 原子提交；应先 preflight replacement capability，再 suppress/submit，记录 partial outcome，并在失败时执行有界补偿与 fail-open。
6. 同一事件只能有一个 authoritative sequence；X11、evdev、portal 和 IME 的本地时间不能被误当成跨 lane 的天然总序。

## 0.3 生命周期不变量

1. backend 的“能力存在”与“此刻健康可用”必须分开表达。
2. 每个连接、注册集合、keymap、accessibility cache 和 portal session 都必须带 generation。
3. 旧 generation 的异步 reply、callback、event 或 timeout 到达时必须被丢弃，不能修改新连接状态。
4. reconnect 成功不等于恢复完成；必须依次完成 capability probe、registration replay、held-state reconcile 和健康确认。

## 0.4 证据不变量

1. 所有 generated matrix 必须记录 commit、构建产物 hash、环境、时间和 evidence level。
2. self-assertion 不能提升为 Windows semantic parity 的证据。
3. 与 Windows v2.0.26 比较时，只比较用户可观察行为；内部 Linux event struct 不能充当 Windows oracle。
4. fail-open、跨进程 provenance、服务重启、24 小时 soak 等 claim 必须有独立进程或真实 host oracle。

---

# 第一部分：P0 — 必须先关闭的安全与核心语义问题

# 1. P0-1 — `ahk-inputd` replay 失败后仍持有独占 grab

## 1.1 缺口进一步拆分

原审计已经确认启动顺序为先 `scan_devices()`、后 `open_uinput_replay()`，且 `replay_key()` 忽略写错误。实际应拆成七个可分别关闭的问题。

| 编号 | 细化缺口 | 当前风险 |
|---|---|---|
| F1 | uinput 创建晚于物理设备 grab | 初始化失败时整把键盘被吞 |
| F2 | `UI_SET_*` ioctl 返回值未逐项验证 | 创建出的 virtual device capability 可能不完整 |
| F3 | `UI_DEV_CREATE` 成功不代表 compositor/libinput 已接管设备 | 创建后立即 replay 可能丢首批事件 |
| F4 | `write()` 返回短写、`EAGAIN`、`EIO`、`ENODEV` 均被忽略 | 运行中静默丢键且 grab 保持 |
| F5 | grab 边界没有处理已按下按键 | 启动或退出时可制造 stuck key |
| F6 | 多设备只要部分 grab 成功就进入模糊状态 | 脚本不知道哪些键盘受 broker 管理 |
| F7 | “ready”只表示进程启动，不表示 replay/grab 闭环健康 | systemd 与客户端会接受假健康状态 |

Linux 社区已有直接证据说明：在某键已按下时切换 `EVIOCGRAB`，旧消费者可能只看到 down 而看不到 up，从而出现 stuck key。应在 grab 前查询当前按键状态并等待释放，而不是仅依赖一个固定 sleep。参见 [freedesktop bug 101796](https://lists.freedesktop.org/archives/wayland-bugs/2017-July/013022.html) 和 [2024 libevdev_grab 讨论](https://lists.freedesktop.org/archives/input-tools/2024-January/001587.html)。

## 1.2 推荐状态机

将 inputd 的输入侧改成显式状态机：

```text
STARTING
  → REPLAY_CREATING
  → REPLAY_PROBING
  → DISCOVERING_DEVICES
  → WAITING_KEYS_RELEASED
  → GRABBING
  → HEALTHY

HEALTHY
  → REPLAY_FAILED → RELEASING_GRABS → FAIL_OPEN
  → DEVICE_CHANGED → RECONCILING → HEALTHY
  → WATCHDOG_EXPIRED → RELEASING_GRABS → FAIL_OPEN

FAIL_OPEN
  → REPLAY_RETRYING → REPLAY_PROBING → GRABBING → HEALTHY
  → PERMANENT_DEGRADED
```

`FAIL_OPEN` 的含义必须是“内核层已没有本进程持有的 grab”，不能只是设置布尔值。

## 1.3 实施步骤

### A. 调整初始化顺序

在 `source/linux/inputd/inputd.c::main()` 中按以下顺序执行：

1. 建立或接管 listen socket；
2. 初始化信号和 watchdog；
3. 创建 uinput device；
4. 验证全部 `UI_SET_EVBIT`、`UI_SET_KEYBIT`、`UI_DEV_SETUP`、`UI_DEV_CREATE` 返回值；
5. 通过 sysfs/udev 确认 virtual device 已完成 kernel 枚举，并把状态标成 `REPLAY_KERNEL_READY`；这不等于 compositor/libinput 已开始消费；
6. 在支持的平台由 session-side helper（例如 X11 XI2 或 compositor-specific integration）确认 desktop sink 已看见该 virtual device；无法证明时进入 `SINK_UNVERIFIED`，strict 模式拒绝 grab，显式 operator policy 才允许继续；
7. 扫描物理键盘，但先不 grab；
8. 对每个候选设备执行 `EVIOCGKEY`，若存在按下键则等待对应 release 或进入明确的 deferred-grab 状态；
9. 逐设备执行带前后检查的 grab commit；
10. 生成设备覆盖与 replay-sink confidence 报告；
11. 只有 replay kernel 状态、sink policy、设备 policy 和 socket 都达到配置要求时才发布对应 readiness。

不应把“无键盘可 grab”与“uinput 不可用”混为一类。前者可作为 listen-only/degraded 模式，后者在任何 grab 之前就应失败。

### B. 让 replay 成为可失败操作

将：

```c
static void replay_key(...)
```

改成：

```c
static int replay_event_batch(...)
```

要求：

- 输入端按原始 evdev frame 读取并缓存至 `SYN_REPORT`；对 frame 内各 `EV_KEY` 做决策，但 replay 保留相关 `EV_MSC/EV_KEY` 顺序并只在原 frame 边界发送一次 `SYN_REPORT`，不能给每个 EV_KEY 人工制造独立 frame；
- 遇到 `SYN_DROPPED` 时按 [kernel input event protocol](https://docs.kernel.org/input/event-codes.html) 丢弃至下一个 `SYN_REPORT`，通过 `EVIOCGKEY`（及需要的 LED/switch state）重建 physical snapshot，neutralize/reconcile virtual held state，并向 client 报 sequence gap；恢复完成前不得继续普通 suppress/remap；
- 对 `EINTR` 重试；
- 对 `EAGAIN` 只允许在非常短的有界 poll 后重试；
- 短写视为失败；
- `EPIPE/EIO/ENODEV/EBADF` 立即进入 `REPLAY_FAILED`；
- 持续维护“已成功提交给 replay device 的 down”集合；失败处理的第一优先级是立即关闭/释放全部物理设备 fd，不能为了补 release 延迟 fail-open；
- 释放 physical grabs 后，只允许用非阻塞、极短 budget 对仍可写输出补齐 release；随后必须 `UI_DEV_DESTROY`（可用时）并关闭 uinput fd，让 desktop input stack 清理该 virtual device 的 held state；cleanup 失败只记录诊断，不能重新取得 physical grab；
- 必须承认运行中首次写失败对应的 in-flight event 可能无法挽回；关闭标准是立即恢复后续物理输入并避免永久 held/grab，而不是伪称故障点上的事件一定送达；初始化失败场景则因为从未 grab，应保证全部原始事件自然送达；
- 失败后给全部客户端广播 `BACKEND_DEGRADED`，不得继续假装 suppress 成功。

### C. two-phase grab

对每个设备实施：

```text
prepare:
  open fd
  validate keyboard capability
  compute stable device identity
  snapshot EVIOCGKEY state
  wait/reconcile held keys

commit:
  EVIOCGRAB(1)
  immediately EVIOCGKEY again
  if any key is down or state is ambiguous:
      EVIOCGRAB(0), defer until all released, then retry
  otherwise add to active set
  publish device generation
```

若任一“必须覆盖”的键盘 commit 失败，可由策略选择：

- strict 模式：回滚本批次全部 grab；
- partial 模式：保留成功设备，但向客户端返回精确的 covered/uncovered device list，并禁止宣称全局 suppress。

默认应采用 strict，partial 只用于显式配置。grab 前后的 `EVIOCGKEY` 是为了收窄 check→grab 的 TOCTOU；若边界上出现新 down，立即 ungrab 并等待该设备回到全释放状态。这个动作仍可能使其他消费者看到不完整的边界序列，因此必须有专门 race oracle，不能把双检查当成数学原子性保证。

### D. 稳定设备身份

不要继续把数组 slot 当作 device identity。建议按优先级构造：

1. udev `ID_SERIAL` + interface；
2. udev `ID_PATH`；
3. `EVIOCGUNIQ`；
4. `EVIOCGID` + `EVIOCGNAME` + `EVIOCGPHYS` 的 hash；
5. 最后才使用 boot-local nonce。

wire 上发送 64-bit `device_id`，另提供可诊断的字符串 identity；hotplug 后同一实体应尽量保持稳定。同名同型号无序列号设备必须允许区分实例。

### E. readiness 与 watchdog

若继续使用 systemd，应把“进程存在”升级为分层 readiness：

- service 使用 `Type=notify`；
- 区分 `REPLAY_KERNEL_READY`、`SINK_CONFIRMED`、`SINK_UNVERIFIED` 和 `GRAB_HEALTHY`，sysfs/udev 出现不能单独证明 desktop sink 已消费；
- M0 先提供最小 health 通道：systemd `STATUS`、协议 ACK/ERROR（或退出码）和 client-visible `BACKEND_DEGRADED`；M2 再把它推广为所有 backend 的统一 health/generation API；
- 只有 replay probe、sink policy、设备 policy 和 socket 都完成后才发送与实际 guarantee 相符的 `READY=1`；
- 主循环发送 `WATCHDOG=1`；
- fail-open 后更新 `STATUS=degraded: replay unavailable, grabs released`。

systemd 的 `READY=1` 和 `WATCHDOG=1` 语义见 [sd_notify 文档](https://www.freedesktop.org/software/systemd/man/sd_notify.html)。这不是 fail-open 的替代物；内置 grab watchdog 仍需保留。

## 1.4 fault injection 与外部 oracle

新增 `tests/oracle/run_inputd_replay_failure_oracle.sh`，至少覆盖：

1. `/dev/uinput` 不存在；
2. open 返回 `EACCES`；
3. 某个 `UI_SET_KEYBIT` 失败；
4. `UI_DEV_CREATE` 失败；
5. 启动后第一次 `EV_KEY` 写失败；
6. `EV_KEY` 成功但 `SYN_REPORT` 失败；
7. 运行中关闭 uinput fd；
8. uinput device 被移除；
9. 多键盘中一个 `EVIOCGRAB` 返回 `EBUSY`；
10. 启动时 Enter/Ctrl/Shift 已按下；
11. 在第一次 `EVIOCGKEY` 返回全释放后、`EVIOCGRAB` 前精确注入新 down，验证 post-grab 检查会立即 ungrab/defer；
12. daemon 在 key-down 后、key-up 前崩溃；
13. replay device 已提交 modifier-down 后发生写失败，验证 physical grab 先释放、uinput device 被销毁/关闭且 desktop 无永久 held；
14. 只有 kernel 枚举、没有 desktop sink confirmation 时 strict policy 不 grab；
15. 强制制造/模拟 `SYN_DROPPED`，验证丢弃到 frame 边界、`EVIOCGKEY` 重建、virtual neutralization 和 client gap event；
16. 一个原始 frame 含 MSC_SCAN/多个 key change 时，target 只收到正确的单一 frame boundary；
17. replay 失败后尝试自动恢复。

oracle 必须由独立进程观察目标应用收到的物理序列，并检查：

- 初始化失败前没有任何 grab；
- 运行中 replay 失败后，grab 在限定时间内全部释放；
- 不存在永远 held 的 modifier；
- 客户端收到明确 degraded event；
- `systemctl status` 不显示虚假的 healthy/ready。

## 1.5 关闭标准

- 所有 replay 初始化和写故障都由外部 oracle 证明 fail-open；
- grab 前 held-key 测试无 stuck key；
- 部分设备失败有机器可读 coverage；
- 代码中不存在忽略 replay write 结果的路径；
- 不允许以 panic sequence 作为正常 failure recovery 的唯一保证。

复杂度：中。优先级：所有其他 inputd 功能之前。

---

# 2. P0-2 — SendLevel/InputLevel 判定方向错误且语义未集中

## 2.1 精确定义

官方 AutoHotkey v2 规则为：要让脚本生成的事件触发 **hook hotkey** 或 hotstring，事件的 SendLevel 必须**严格大于**目标的 InputLevel。使用 Windows registered (`reg`) method 的 hotkey 无法区分 physical/artificial，因而不受 SendLevel 影响；InputLevel 高于 0 会迫使 hotkey 使用 hook。官方参考：[SendLevel](https://www.autohotkey.com/docs/v2/lib/SendLevel.htm) 与 [#InputLevel](https://www.autohotkey.com/docs/v2/lib/_InputLevel.htm)。

Linux 端也必须把 dispatch method 纳入 policy：X11 hook-like/broker lane 才能声称上述 level gate；Portal/GNOME accelerator activation 更接近 registered/adapted lane，不能直接冒充 hook semantics。任何 InputLevel>0、跨进程 provenance 或明确要求 level gate 的 hotkey，都应路由到可提供 hook/broker guarantee 的 lane，否则注册失败并说明原因。

当前 `core_hotkey_linux.cpp` 使用的条件会在 `inputLevel < sendLevel` 时阻止触发，方向相反；`core_capture_linux.cpp` 的 Hotstring 条件则接近正确规则。问题不只是改一个 `<`，还包括各 lane、InputHook、remap 和 mode exception 未统一。

## 2.2 建立统一入口与 consumer-specific policy

新增例如 `source/linux/core/input_semantics.{h,cpp}`。统一的是 provenance/mode/level 分类、trace 和调用入口；Hotkey/Hotstring 与 InputHook 必须选择不同 policy：

```cpp
enum class AhkInputTrust {
    PHYSICAL,
    SYNTHETIC_KNOWN,
    SYNTHETIC_UNKNOWN_LEVEL,
    IME_COMMIT
};

struct AhkTriggerDecision {
    bool trigger;
    const char *reason;
};

AhkTriggerDecision EvaluateInputForConsumer(
    AhkInputTrust trust,
    int send_level,
    int consumer_level, // InputLevel or MinSendLevel, interpreted by consumer.
    bool suppress_own_sendinput,
    bool same_script,
    AhkConsumerKind consumer,
    AhkSendMode mode);
```

policy 规则：

| consumer / 来源 | 结果 |
|---|---|
| hook-like Hotkey + physical/non-AHK | 不因 InputLevel 被忽略 |
| hook-like Hotkey + known synthetic | 只有 `send_level > input_level` 才触发；相等也不触发 |
| registered/accelerator Hotkey | 不能可靠区分 physical/artificial，按该 lane 的 adapted semantics；不得声称 enforce SendLevel |
| Hotstring trigger character + physical/non-AHK | 不因 InputLevel 被忽略 |
| Hotstring trigger character + known synthetic | 只有 `send_level > input_level` 才允许本字符触发；buffer accumulation 另按 Hotstring 专用规则处理 |
| InputHook + physical/non-AHK | 不因 MinSendLevel 被忽略 |
| InputHook + SendEvent 类 synthetic | `send_level >= min_send_level` 才收集；低于阈值忽略 |
| InputHook + SendInput/SendPlay | 不论 MinSendLevel 均忽略 |
| unknown synthetic level | Linux 无等价 Windows hook metadata 时只能采用显式 adaptation policy（默认保守忽略或由 strict/best-effort 配置决定）；必须记录 unknown，不能伪装 physical 或声称 Windows parity |
| 同脚本正在执行真正 Input mode | 自身 hook hotkey/InputHook 不触发；跨脚本行为仍取决于有效 mode/provenance |
| Hotkey/Hotstring + SendPlay | 不按 SendLevel 参与普通 hook level 触发；作为独立 mode 例外验证 |
| IME commit | 作为文本事务进入 Hotstring/InputHook；不能虚构 SendLevel |

注意：Hotstring 有全局 buffer 和 ending character 的细节，不能仅把每个字符独立调用一个布尔谓词就宣称完全兼容。Hotkey 与 Hotstring 的“最后一个字符是否允许触发”可共享 strict-`>` policy，但 buffer accumulation 必须有 Hotstring 专用层：物理输入照常收集；AHK synthetic level 1–100 可进入共享 buffer，而 level 0 不应进入；具体 Hotstring 的 InputLevel 主要决定最后键入字符是否允许触发。官方语义还要求验证“前缀由某个可收集 level 进入 buffer、最后一个结束字符决定是否触发”的混合场景；因此应加入“脚本发送前缀 + 用户物理输入 end char”“不同 level 字符混合”“level 0 污染负例”“auto-replace replacement 固定 level 0”等 golden，不能只测整串字符来自同一 Send transaction。

## 2.3 修改落点

1. `core_hotkey_linux.cpp`：删除本地反向比较；
2. `core_capture_linux.cpp`：Hotstring 改用共享 helper；
3. `core_inputhook_linux.cpp`：`MinSendLevel` 使用 InputHook policy（SendEvent 按 `>=` threshold；SendInput/SendPlay 总是忽略），不能调用 Hotkey 的 strict-`>` policy；
4. `core_evdev_linux.cpp`：combo/remap 作为 Hotkey/Hotstring producer/consumer 时选择对应 policy；
5. portal/GNOME shortcut lane：标记为 accelerator/adapted dispatch；若 compositor 无法提供 provenance/level，caps 必须报告 level gate unsupported/unknown，不能假装已 enforce；InputLevel>0 或明确 hook requirement 不得路由到此 lane；
6. `core_input_linux.cpp`：每次 injection transaction 固定并携带 SendLevel，不能在 transaction 中途读取已变化的 thread global；
7. diagnostics trace：记录 `send_level`、`input_level`、decision、reason 和 provenance confidence。

## 2.4 完整差分矩阵

Windows fixture 分成两套矩阵，禁止混用阈值含义：

```text
Hotkey/Hotstring:
  send_level: 0, 1, 5, 10, 100
  input_level: 0, 1, 5, 10, 100
  dispatch_method: forced hook, registered/accelerator where available
  expected boundary: strict send_level > input_level only for hook-like path;
                     registered path records its no-level-gate exception

InputHook:
  send_level: 0, 1, 5, 10, 100
  min_send_level: 0, 1, 5, 10, 101
  mode: SendEvent, SendInput, SendPlay
  expected boundary: SendEvent send_level >= min_send_level;
                     SendInput/SendPlay always ignored

common:
  process: same-script, second-script
  phase: down, up, repeat
```

不是所有笛卡尔积都需要单独脚本，但 Hotkey/Hotstring 必须覆盖 `<`、`==`、`>`、0、100；InputHook 必须覆盖 threshold 的 `<`、`==`、`>`、0、101 和 mode exception。

重点 case：

- SendLevel 0 → InputLevel 0：不触发；
- 1 → 0：触发；
- 5 → 5：不触发；
- 10 → 5：触发；
- 5 → 10：不触发；
- hotkey thread 启动后的默认 SendLevel 是否继承该 hotkey 的 InputLevel；
- auto-replace Hotstring replacement 固定 level 0；
- 显式 `SendInput` 与 `SendMode "Input"` + `Send` 自身 hook 行为一致；
- 第二脚本能否按 level 触发；
- unknown provenance 不被当成 physical 悄悄绕过 gate；
- InputHook MinSendLevel 5：SendEvent level 4 忽略、level 5/6 收集；
- InputHook MinSendLevel 0/101 的边界；
- InputHook 对 SendInput/SendPlay 在任意 MinSendLevel 下都忽略。

## 2.5 关闭标准

- 所有 input consumer 进入同一个分类/诊断入口，但依据 consumer kind 分派到经官方 golden 验证的独立 policy；
- X11、evdev 和 broker path 的同进程矩阵与 Windows golden 一致；
- 跨进程矩阵在 protocol v2 完成后也一致，或明确标为 unsupported；
- trace 能解释每个未触发事件的 reason；
- 旧的反向注释和条件完全删除。

复杂度：低至中（同进程修复）；跨进程部分依赖 P0-3。

---

# 3. P0-3 — 跨进程 synthetic provenance 与 protocol v2

## 3.1 需要解决的不是“再加一个 bool”

当前 `AhkInputEvent` 有 `source` 和 `send_level`，但它无法表达：谁发送、哪次 Send、哪个脚本、经过几次 remap、由谁 suppress、捕获事件与注入事件如何对应。XTEST/uinput 也没有可供其他进程读取的 AHK metadata 字段。

真正目标应是：

> 在由项目控制的 broker 路径中，提供 authoritative provenance；在系统无法携带 metadata 的 XTEST、第三方 uinput、clipboard fallback 路径中，诚实标记 heuristic/unknown，而不是假装完全可知。

## 3.2 `AhkInputEvent` v2 建议字段

建议不要直接无限膨胀或把 keyboard 字段硬套给 pointer/text。使用公共 envelope + tagged payload；inputd v2.0 可以先声明 keyboard-only，但全系统 transaction protocol 必须可扩展：

```cpp
struct AhkInputEventEnvelopeV2 {
    uint16_t schema_version;
    uint16_t header_size;
    uint16_t event_kind;          // KEY / POINTER / TEXT / DEVICE
    uint16_t flags;

    AhkAuthorityId authority_id;  // 128-bit broker/runtime instance identity.
    uint64_t authority_generation;
    uint64_t event_seq;           // Monotonic only inside authority+generation.
    uint64_t timestamp_us;
    uint64_t device_id;
    uint64_t seat_id;

    uint8_t source;               // PHYSICAL/SELF/OTHER/IME/UNKNOWN
    uint8_t origin;
    uint8_t provenance_confidence;
    uint8_t reserved0;
    int16_t send_level;
    uint16_t payload_size;

    uint64_t producer_client_id;
    uint64_t transaction_id;
    uint64_t parent_transaction_id;
    uint64_t registration_id;
    uint64_t suppression_owner_id;
    uint64_t focus_context_id;
    // followed by one tagged payload
};

struct AhkKeyPayloadV2 {
    uint32_t evdev_code;
    uint16_t vk;
    uint16_t sc;
    uint32_t unicode_scalar;
    uint8_t phase;                // DOWN/UP/REPEAT
};

struct AhkPointerPayloadV2 {
    uint16_t event_type;           // MOTION/BUTTON/WHEEL
    uint16_t button;
    double x, y, dx, dy;
    double wheel_x, wheel_y;
};

struct AhkTextPayloadV2 {
    uint64_t composition_id;
    uint32_t utf8_size;
    uint32_t text_flags;           // PREEDIT/COMMIT/CANCEL
    // followed by bounded UTF-8 bytes
};
```

补充说明：这些 C++ struct 只表达语义 schema，wire 必须逐字段序列化并验证长度/enum/range；不得直接 `write(sizeof(struct))`，pointer double 需固定 IEEE-754/endianness 或改用定点表示。

- event identity 是 `(authority_id, authority_generation, event_seq)`；`event_seq` 单独不能跨 broker、跨重启去重；多个 authority 的 monotonic timestamp 也不能直接构造总序，如需统一 dispatch order，应由 session coordinator 记录 acceptance sequence；
- inputd v2.0 若只实现 keyboard payload，HELLO capability 必须明确 `KEY_EVENT` only，pointer/text transaction 由后续版本或其他 authority 承担，不能因为 envelope 可扩展就声称已支持 mouse；
- `PID` 不是稳定脚本身份，必须使用 broker 分配的 `client_id`；PID/UID 仅用于审计与授权；
- 每次进程启动生成随机 `script_nonce`，broker hello 后绑定为 client identity；
- `transaction_id` 在一次 `Send`/remap/replacement 开始时分配，在全部 down/up、文本或鼠标事件完成后关闭；
- remap 链用 `parent_transaction_id` 或 bounded chain 表达；必须设置最大深度，避免循环；
- `provenance_confidence` 至少区分 authoritative、device-derived、time-correlated、unknown；
- focus/application identity 只能在有可靠来源时填充，禁止通过窗口标题猜测后标 authoritative。

## 3.3 inputd protocol v2

### A. framing 与协商

协议应从主机字节序的临时 frame 升级为明确 wire format：

```text
magic        4 bytes  "AHK2"
version      u16 LE
header_len   u16 LE
message_len  u32 LE
message_type u16 LE
flags        u16 LE
request_id   u64 LE
client_seq   u64 LE
payload      message_len - header_len
```

HELLO/HELLO_ACK 协商：

- min/max protocol version；
- event schema versions 与 typed payload kinds（KEY/POINTER/TEXT/DEVICE）；
- authority ID、server generation 与 sequence scope；
- observe/suppress/exclusive/inject capability；
- max frame/rules；
- device filtering；
- provenance support；
- transaction ack support；
- server boot/generation ID。

v1 与 v2 不应在同一 socket 上靠 frame 长度猜测。可由 hello magic 明确分流，或发布独立 v2 socket。旧 client 仍可观察，但不得被授予 v2-only suppress/inject claim。

### B. 消息集合

至少需要：

```text
HELLO / HELLO_ACK
SUBSCRIBE / SUBSCRIBE_ACK
UNSUBSCRIBE
EVENT
DECISION_REQUEST / DECISION_REPLY
INJECT_BEGIN / INJECT_EVENT / INJECT_COMMIT / INJECT_ABORT
REPLACEMENT_ACK
CONFLICT
DEVICE_ADDED / DEVICE_REMOVED / DEVICE_COVERAGE
BACKEND_HEALTH
PING / PONG
ERROR
```

### C. broker-owned injection

推荐路径不是让每个客户端各建一个 uinput device，而是：

1. client 提交带 transaction metadata 的注入计划；
2. broker 验证 capability、level、深度和 quota；
3. broker 通过专用 uinput 或 user-session libei backend 提交外部输出；root inputd 不得代替用户获取 portal consent；
4. broker 不应重新 grab/read 自己的 uinput device（必须继续跳过它以防反馈环）；而是在成功提交相应 frame 后，把同一 transaction 的 normalized synthetic event 发布到内部 authoritative AHK stream；
5. 所有 AHK client 从内部 stream 收到相同 sideband provenance，并按 consumer policy 决定触发；外部桌面路径则只记录 submitted/processed/target-unknown outcome；
6. broker 回传 complete/partial/abort 结果，包含 original suppression 与 replacement output 各自状态。

“专用 tagged uinput device”只能帮助 compositor/诊断识别 producer class，不能把每个 event 的 SendLevel 自动塞进 kernel event。因此仍需要 broker 内部序列关联；不得把 vendor/product ID 当成完整 transaction identity，也不得依赖 kernel loopback来分发给其他 AHK script。

X11-only 环境可先建立一个 user-session `ahk-input-broker`，统一本项目 XTEST injection 的 metadata；root `ahk-inputd` 负责 evdev/uinput。最终可以合并，也可以通过受保护 IPC 建立两级 broker，但必须保证唯一 ordering authority。

## 3.4 非 broker 路径的诚实降级

| 路径 | 可达到的 provenance | 政策 |
|---|---|---|
| broker-owned uinput | authoritative sideband inside capture/injection broker | 可做 client level/transaction gate；target delivery/consumption仍需 outcome oracle |
| session-broker-owned libei | authoritative sideband only among broker participants | EI wire 不携带 AHK metadata；可记录 submitted/EIS-processed，target delivery/consumption unknown |
| 本进程 XTEST + XI2 ring | heuristic/self-known | 同进程可 gate；跨进程不可声称完整 |
| 其他进程 XTEST | synthetic known, producer unknown | level unknown；按策略忽略或警告 |
| 第三方 uinput | 通常看似物理 | 只能 device-policy 分类，不能伪造 AHK level |
| clipboard paste | 无逐键 event | 不能参与键级 SendLevel 语义 |

## 3.5 安全与滥用控制

protocol v2 引入 injection 后，安全边界显著扩大。必须同时完成：

- `SO_PEERCRED` 真正参与 authorization；
- 默认 socket 不应让整个 `input` group 自动获得 suppress+inject；
- observe、suppress、exclusive、inject 分权；
- per-UID/client rule、event-rate、transaction-size quota；
- client nonce 与当前 socket connection 绑定，断线后不可 replay；
- monotonic client sequence，重复 request idempotent 或拒绝；
- transaction TTL；
- remap chain depth limit；
- 审计日志不记录实际文本内容，除非显式 debug consent；
- 敏感按键范围可配置禁止 observe；
- 冲突和拒绝必须返回机器可读 reason。

## 3.6 验收场景

至少建立 A/B 两脚本和 A/B/C 三脚本场景：

1. A SendLevel 10，B InputLevel 5：B 触发；
2. A SendLevel 5，B InputLevel 5：B 不触发；
3. A remap `a→b`，B 监听 `b`，验证 parent transaction；
4. A/B 同时 remap 同一键，只允许策略指定的 owner 输出；
5. Hotstring replacement 不递归；
6. client crash 于 `INJECT_BEGIN` 后，broker abort 并平衡 held key；
7. broker restart 时旧 transaction 不被新 generation 接受；
8. third-party uinput 被标 unknown/physical-like，不获得虚假 SendLevel；
9. 同一 key 高频并发时 transaction 不串线；
10. protocol v1 client 与 v2 client 共存时不破坏安全规则。

关闭标准：项目控制的跨进程 injection path 可提供 authoritative provenance；无法控制的 path 明确降级；多脚本 differential 与 policy 文档一致。

复杂度：高，是后续 arbitration、统一 pipeline 和 production multi-script 的共同前置。

---

# 第二部分：P1 — 架构闭环与生命周期

# 4. P1-1 — 让 normalized event 成为唯一输入语义 pipeline

## 4.1 当前缺口

`AhkInputEvent` v1 已存在，但 X11 backend 仍明确绕过统一接口；Send 直接进入不同 injection primitive。结果是：

- X11/evdev 各自判 repeat；
- Hotkey/Hotstring 各自判 level；
- suppression 在 grab、replay、portal 中各自实现；
- callback dispatch 和 object lifetime 规则不一致；
- trace 只能看到部分链路；
- 修复一条 lane 不会自动修复其他 lane。

## 4.2 推荐分层

```text
Backend adapters
  X11 / evdev / inputd / portal / GNOME / IBus / Fcitx
        ↓ normalize
Authoritative event queue
        ↓ order + deduplicate + provenance
Physical state reducer
        ↓
Matcher layer
  Hotkey / Hotstring / InputHook / combo / remap
        ↓
Decision layer
  trigger / suppress / pass / replace / conflict
        ↓
AHK dispatch queue
        ↓
Injection transaction planner
        ↓
XTEST / broker-uinput / libei / wlroots / paste
        ↓
Outcome + trace
```

必须区分三类对象：

1. `InputEvent`：发生了什么；
2. `InputDecision`：哪个 registration 决定了 trigger/suppress/replace；
3. `InjectionTransaction`：要向外界生成什么以及结果如何。

不要在一个 struct 中混合事实和决定。

## 4.3 迁移顺序

### 阶段 A：只读镜像

所有旧入口在现有逻辑之前生成 normalized event，并比较 old/new trace，不改变行为。目标是发现字段缺失和重复事件。

### 阶段 B：统一 state reducer

先统一 modifier、held key、repeat、device/seat state；`GetKeyState` 和 `KeyWait` 改为读取 reducer snapshot。对不能提供物理状态的 portal lane，返回 capability error，而不是从 X11 猜。

### 阶段 C：统一 matcher

按低风险顺序迁移：普通 hotkey → key-up/wildcard → InputHook key callbacks → Hotstring → combo/remap。

### 阶段 D：统一 suppression decision

backend 只执行决定，不再自己决定语义：

- X11 执行 grab/replay；
- inputd 执行 broker suppress/replay；
- portal 返回其先天能力限制；
- matcher 输出 `PASS_ORIGINAL`、`SUPPRESS_ORIGINAL`、`REPLACE_TRANSACTION`。

### 阶段 E：统一 Send transaction

所有 Send family 先生成 transaction plan，再由 backend adapter 执行。transaction 记录 mode、level、modifier snapshot、target/focus generation、backend guarantee 和 outcome。

## 4.4 去重与排序

跨 lane 不能只用 `keycode + 5ms` 去重。建议 key：

```text
(source domain, device_id, backend sequence, phase)
```

只有在明确知道两个 adapter 观察的是同一个 kernel/X server event 时才合并。未知时宁可记录 conflict/duplicate candidate，也不能静默丢掉合法快速重复键。

## 4.5 验收

- 所有 Hotkey/Hotstring/InputHook matcher 的公共入口参数只有 normalized event/context；
- provenance/mode/level 分类只有一个入口，Hotkey/Hotstring 与 InputHook 的 consumer-specific policy 各只有一个实现；
- `GetKeyState`/KeyWait 来源可诊断；
- 每个事件 trace 可串起 capture→decision→dispatch→injection→outcome；
- 故意禁用一个 adapter 时其他 adapter 行为不依赖其私有 global；
- 旧路径被 feature flag 关闭后全量测试仍可运行。

复杂度：很高。应拆成多个可回滚 milestone，不宜一次性重写。

---

# 5. P1-2 — health-aware mux、动态重新协商与状态恢复

## 5.1 capability 与 health 分离

把静态 `AhkInputBackendCaps` 拆成：

```cpp
struct BackendCapabilities {
    // 该实现理论上能做什么
};

struct BackendHealth {
    BackendState state;
    uint64_t generation;
    uint64_t last_success_us;
    int last_errno;
    std::string reason;
    DeviceCoverage coverage;
    PermissionState permission;
};

struct RouteGuarantees {
    DispatchSemantic dispatch;   // HOOK_LIKE / REGISTERED_OR_ACCELERATOR / STREAM
    ProvenanceGrade provenance;
    LevelGateGrade level_gate;
    SuppressionGrade suppression;
    CharacterGrade text;
    StateGrade physical_state;
    InterleavingGuarantee interleaving;
    RecoveryGrade recovery;
};
```

路由不应只问“能否注册”，还要问“是否满足这个 hotkey 的全部 required guarantees”。例如 InputLevel>0 或依赖 SendLevel 的 hotkey 必须要求 `HOOK_LIKE + level_gate`；Portal/GNOME accelerator 即使能绑定同一按键也不满足该 contract。

## 5.2 backend 状态机

```text
UNINITIALIZED
  → PROBING
  → AVAILABLE
  → BINDING
  → HEALTHY

HEALTHY
  → DEGRADED
  → DISCONNECTED
  → RETRY_WAIT
  → PROBING
  → RESUBSCRIBING
  → RECONCILING_STATE
  → HEALTHY

任意状态 → PERMISSION_DENIED / UNSUPPORTED / SHUTDOWN
```

事件：socket HUP、D-Bus `NameOwnerChanged`、Wayland dispatch error、registry remove、portal session closed、uinput write error、permission revoke、device coverage change。

## 5.3 retry 策略

采用 capped exponential backoff + full jitter，例如：

```text
base = 100ms
cap  = 30s
delay = random(0, min(cap, base * 2^attempt))
```

健康稳定一段时间后 reset attempt。full jitter 用于避免大量脚本在 portal/compositor 重启后同时重连；参考 [AWS Exponential Backoff and Jitter](https://aws.amazon.com/blogs/architecture/exponential-backoff-and-jitter/)。

permission denied、用户拒绝/撤销 portal consent 等非 transient 错误不能自动高频重试；没有有效 persistent grant/restore token 时进入 `REAUTH_REQUIRED`，只在用户发起或 UI 明确提示后重建 consent session。所谓“自动恢复”仅限 compositor/service transient restart 且既有授权仍有效的情况。

## 5.4 registration reconciliation

每个 registration 使用 stable runtime id，维护 desired/actual 两张表：

```text
desired: script 当前要求
actual[generation]: backend 当前确认已安装
```

重连时：

1. 清空旧 generation actual；
2. probe capability；
3. 重新计算 route；
4. 按事务批量注册；
5. 确认 ack；
6. reconcile held state；
7. 发布 route changed event；
8. 才标 healthy。

不能把“发送了注册请求”视为 installed。

## 5.5 failover 政策

路由改变可能改变用户可观察语义，故需要三种模式：

- strict：原 lane 失效就报错，不自动降级；
- compatible：只切到满足相同 guarantees 的 lane；
- best-effort：允许降级，但一次性 warning + diagnostics 标记。

默认建议 compatible。涉及 suppress/remap 时，不得从 authoritative broker 自动切到 observe-only portal。

## 5.6 验收

真实断线而非 STOP/CONT：

- kill/restart portal backend；
- reload GNOME extension；
- restart inputd；
- 关闭并重建 Wayland compositor socket；
- 撤销再恢复权限；
- 重建 keymap；
- 在 key held 时断线。

要求同一脚本自动恢复、无 stuck key、无 duplicate registration，且诊断可见完整状态转换。

---

# 6. P1-3 — Send family 语义与 backend guarantee

## 6.1 先拆开 transport 与 interpretation

AutoHotkey 的发送 transport 是 Event、Input、Play；`InputThenPlay` 是选择/回退策略，不是第四种底层 transport。`SendText`、`{Text}`、`{Raw}` 与 `{Blind}` 是与 transport 正交的解释/修饰模式：`SendText` 仍使用当前 SendMode 所选择的 transport，只是把内容按 literal text 解释。当前 `BIF_Linux_SendText` 直接传 `LSE_TEXT`，绕过 `LinuxResolveSendMode()`；这应列为确定性语义缺口。Linux 内部即使保留 `LSE_TEXT` 便于 parser/planner 实现，也不能在 compatibility model 中把它当成与 Event/Input/Play 并列的公开 transport。

`core_input_linux.cpp` 当前还通过 `aExplicitSendInput` 区分显式 `SendInput()` 与 `SendMode("Input")` 下的 `Send`。应改为由**解析后的有效 transport**决定 self-suppression，而不是由调用哪个 BIF 决定：

```cpp
bool suppress_own_hook = effective_transport == SendTransport::INPUT;
```

显式函数名仍可保留诊断来源，但不能改变相同 effective transport 的语义。另需单列当前 `SM_INPUT_FALLBACK_TO_PLAY`/InputThenPlay 的缺口：当前 `LinuxResolveSendMode()` 将其与 Input 一起无条件映射为 `LSE_INPUT`。应先按 Windows/项目 contract 尝试 Input，在检测到外部 AHK hook 等定义好的不可用条件时原子选择 Play，并记录回退原因；官方语义下，InputThenPlay 还会影响显式 `SendInput` 的回退，不能只修普通 `Send`。

## 6.2 `InjectionTransaction` 设计

```cpp
enum class SendTransport { EVENT, INPUT, PLAY };

struct SendInterpretation {
    bool text;      // SendText or {Text}
    bool raw;       // {Raw}
    BlindMask blind;
};

struct InjectionTransaction {
    uint64_t id;
    SendModePolicy requested_policy; // Event/Input/Play/InputThenPlay
    SendTransport effective_transport;
    SendInterpretation interpretation;
    int send_level;
    ModifierSnapshot physical_before;
    ModifierSnapshot logical_before;
    std::vector<PlannedInput> events;
    BackendGuarantees guarantees;
    InterleavingGuarantee interleaving;
    DowngradeReason downgrade;
};
```

transaction 开始时冻结 thread 的 SendLevel、SendMode、key/mouse delay、modifier state 和 interpretation flags。回调或 timer 不能在中途改变本 transaction。

## 6.3 每个 transport/interpretation 的最低保证

| 领域 | 最低应保证 |
|---|---|
| Event transport | 按序发送；遵守 key/mouse delay；可被符合 level 的 hook 观察 |
| Input transport | 本脚本 hook/InputHook 不被自身 transaction 激活；忽略 key delay；requested/effective transport 可诊断 |
| Play transport | Windows 上不触发 AHK hotkey/hotstring、InputHook、其他程序或 OS registered global hotkey；Linux 若 Event-like XTEST/libei 无法做到，必须返回 `DEGRADED`/`NOT_SUPPORTED`，不能只写“journal adaptation” |
| InputThenPlay policy | 明确定义何时 Input 不可用及何时回退 Play；回退发生前不得发送半个 Input transaction |
| Text interpretation | literal text，与 effective transport 正交；若退化为 clipboard paste，必须公开这是 paste，不是逐键输入 |
| Raw interpretation | 关闭特殊字符解析，但仍遵守 effective transport |
| Blind modifier policy | 只改变 modifier reconciliation，不把 SendText 变成独立 transport；`{Blind}{Text}` 单独验证 |

跨操作系统无法复制的内容应列入 compatibility status，而不是用同一 primitive 后仍命名为完全等价 mode。Text/Unicode fallback 可能为某个字符选择不同 injection primitive；`PlannedInput` 应逐项记录 primitive 和 guarantee，但这不应反过来把整个 transaction 错标成第四种 transport。

## 6.4 physical-input buffering 与 interleaving guarantee

Windows SendInput 与 SendPlay 的可观察保证都包括用户物理输入在发送期间被延后，避免与批次交错。XTEST、普通 uinput 或 libei sender 自身不能阻止并缓存独立物理设备事件；只有同一 authority 同时拥有 exclusive capture 与 injection，并在 transaction 期间有界排队 physical input，才可能提供接近保证。

每个 backend 必须报告：

```text
NONE                 可能与物理输入任意交错
SELF_QUEUE_ONLY      只保证项目自己的 injection transaction 不互插
EXCLUSIVE_BUFFERED   同一 seat 的物理输入被捕获、排队并在 transaction 后平衡 replay
```

`EXCLUSIVE_BUFFERED` 只允许 broker 在已获 exclusive seat lease、队列有大小/时间上限、panic/fail-open 可用时声明；超限或 injection 失败必须先恢复 physical delivery，并返回 partial outcome。建立独立 physical producer + target recorder，在 SendInput 大批次中并发输入，比较 Windows event order、延迟和 down/up 平衡。

## 6.5 modifier reconciliation

`{Blind}`、用户同时按键和跨 backend 状态必须由统一 reducer 支持：

1. transaction 前快照 physical/logical modifier；
2. 计算临时 release/press plan；
3. 每次 output outcome 后更新 transaction state；
4. 失败或 abort 时生成平衡 release；
5. transaction 结束后只恢复本 transaction 改变的状态，不覆盖用户期间的新动作。

需要用 event sequence/generation 区分“用户在发送期间新按下 Ctrl”与“发送器临时按下 Ctrl”。

## 6.6 clipboard paste fallback

此路径必须改名为显式 guarantee，例如 `TEXT_VIA_CLIPBOARD`。建议：

- 仅在 direct Unicode backend 不可用且用户允许时启用；
- 保存所有可读 MIME，而不只是 text；
- 记录原 owner generation；
- 目标确认读取 offer 后才考虑恢复；
- 若外部 owner 在期间变化，禁止覆盖用户的新 clipboard；
- 敏感文本提供禁用选项和 warning；
- 超时后返回部分失败，不宣称所有字符已发送；
- Flatpak/remote app 单独验证。

## 6.7 鼠标、lock state 与 dead/compose 的剩余语义

- mouse transaction 使用 typed pointer payload，分别表达 absolute/relative motion、button、wheel 与 coordinate space；`SetMouseDelay` 只作用于适用 mode；
- `SetDefaultMouseSpeed` 若 backend/compositor 无法复制 Windows movement curve，应标 `ADAPTED`，并以目标坐标/轨迹误差而不是 API 存在验收；
- Caps/Num/Scroll Lock 在 transaction 前后读取可获得的真实 lock state；发送失败或用户并发切换时不得强行恢复过期 snapshot；SendPlay 无法改变 lock state 的限制单列；
- dead key/compose/AltGr 不等同普通 modifier chord。Send planner 应优先选择当前 keymap 可表达序列，无法安全复制 composition 时选择 text/IME backend或明确降级；
- SendInput 的“外部 AHK hook 存在时有效 mode 回退”在 broker protocol 可知其他 AHK client 后才能实现；transaction 必须记录 requested/effective mode 和 downgrade reason。

## 6.8 验收

官方 Windows differential 增加：默认 Send、切换 SendMode、显式 SendEvent/SendInput/SendPlay/SendText、InputThenPlay、Event+Text/Input+Text/Play+Text、`{Raw}`、`{Blind}{Text}`、delay、own hotkey、other-script hotkey、InputHook、registered global hotkey、physical interleaving、modifier 并发、lock keys、Unicode、dead/compose、AltGr、mouse button/wheel/relative/absolute movement、clipboard fallback、失败中断。

---

# 7. P1-4 — inputd 多客户端 arbitration

## 7.1 明确政策，而不是试图隐式复制 Windows hook 顺序

Linux broker 应公开一套确定、可审计的 policy。建议注册类型：

```text
OBSERVE       只收事件，不影响 replay
SUPPRESS      希望阻止 original；不负责 replacement
EXCLUSIVE     获得该规则范围的唯一决策权
REMAP         suppress + committed replacement transaction
```

每条规则至少包含：

```text
registration_id
client_id
uid
key/device/seat selector
mode
priority
input_level
HotIf evaluation class
lease_expiry
conflict_policy
```

## 7.2 推荐默认仲裁

1. 先承认物理现实：`EVIOCGRAB` + uinput replay 对同一 seat 是全局效果，broker 无法做到“只对 UID A suppress、同时让 UID B 收到 original”；per-UID 只能是授权政策，不是 delivery isolation 能力；
2. OBSERVE 永远不能改变 delivery，并且只向当前 logind active seat/session 的获授权主体开放；跨 session 的键盘观察默认拒绝；
3. SUPPRESS/EXCLUSIVE/REMAP 只授予当前 active seat owner，或由管理员/Polkit 明确授予的 exclusive seat principal；一旦 active session/seat ownership 变化，lease 立即撤销并 fail-open；
4. 在同一获授权 principal/UID 的多个 client 内，优先级高者先决策，优先级相同按 broker acceptance sequence；
5. REMAP 要求 exclusive lease；其他 UID/seat principal 的冲突请求直接拒绝，不能描述成“互不影响”；
6. 多个 REMAP 冲突时不得重复输出，后来的 registration 收到 `CONFLICT`；
7. HotIf 必须在规定 deadline 内回复；超时默认 fail-open；
8. replacement 只有在 broker 收到完整 transaction、通过 capability/queue preflight 后才允许 suppress original；但 injection 仍可能在 suppress 后失败，必须返回 `ORIGINAL_SUPPRESSED_REPLACEMENT_FAILED` 等 partial outcome，并尽力 neutralize/release，不能声称原子性；
9. client 断线、lease 过期、logind session 失活或 replacement abort，立即释放 ownership。

## 7.3 决策 deadline

若每个事件等待多个 client 回复，会增加输入延迟。建议：

- 静态可判定规则在 broker 内直接处理；
- HotIf 等动态条件只允许在可接受的短 deadline 内参与 suppress；
- 超时行为写入 registration：fail-open 或 fail-closed；键盘默认必须 fail-open；
- 统计 p50/p95/p99 decision latency；
- 慢 client 自动降级为 observe-only 或撤销 lease。

## 7.4 A/B/C corpus

必须覆盖：

- A suppress、B observe、C remap；
- 两个 remapper；
- priority tie；
- 跨 UID；
- owner crash；
- replacement 发送失败；
- HotIf false/timeout；
- 共享 prefix combo；
- key-up owner；
- client 恢复后旧 lease 不复活；
- broker restart generation。

每个 case 输出完整 decision trace，最终 target app 只能收到政策允许的唯一序列。

---

# 8. P1-5 — pure Wayland continuous input/text stream

## 8.1 先划清平台边界

core Wayland seat 只把事件发给本进程获得焦点的 surface；GlobalShortcuts 只产生已注册 accelerator activation；它们都不是全局连续键盘流。

XDG InputCapture portal 虽支持 KEYBOARD capability，但当前捕获是 trigger-based，已标准化的触发器主要是屏幕边缘 pointer barrier。它适合远程桌面/Barrier 类会话，不能直接当成永远在线的 Hotstring hook。官方流程和限制见 [InputCapture portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.InputCapture.html) 与 [设计讨论](https://github.com/flatpak/xdg-desktop-portal/pull/714)。

因此应形成分层产品能力：

| 层级 | 捕获来源 | 能力 |
|---|---|---|
| consented shortcut | GlobalShortcuts/extension | selected hotkey |
| consented session capture | InputCapture+libei | 激活会话中的流，非永久全局 hook |
| privileged continuous | inputd/evdev | Hotstring/InputHook/remap |
| compositor-specific | GNOME/KWin/wlroots integration | 取决于明确协议 |

## 8.2 broker 的 pure-Wayland key/text model

inputd 读取 evdev 后需要独立 xkb context：

1. 获取当前 seat 的 keymap、layout group 和 compose 配置；
2. 对每个 device/seat 维护 xkb state；
3. 按 EV_KEY 更新 modifier/group；
4. 输出 physical evdev code、canonical SC、keysym、Unicode scalar；
5. keymap generation 变化时原子替换；
6. replacement 时保留 transaction 与 layout snapshot。

layout 来源按可信度排序：compositor/desktop API → input method state →用户显式配置 →系统默认。无法确认当前 layout 时，应继续提供 physical hotkey，但关闭 char_stream capability，不能用 US layout 猜字符。

## 8.3 libei 用于 injection

RemoteDesktop portal 的 `ConnectToEIS()` 返回 fd，可传给 `ei_setup_backend_fd()`；libei client 随后在自己的 event loop 中完成 handshake。参考 [RemoteDesktop portal v2](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.RemoteDesktop.html)、[libportal 当前实现](https://github.com/flatpak/libportal/blob/main/libportal/remote.c) 与 [libei client API](https://libinput.pages.freedesktop.org/libei/api/group__libei.html)。

这里必须规避一个上游文档陷阱：libportal 当前生成页/源码注释仍写“在 `xdp_session_start()` 前调用”，但 main 分支实现明确要求 `XDP_SESSION_ACTIVE`，否则返回 `Session has not been started`；portal v2 规范也要求 Start 后 ConnectToEIS。因此本项目应以 pin 的 libportal 实现 + portal interface 规范为准，采用 `CreateSession → SelectDevices → Start/consent 成功 → ConnectToEIS`，并为所支持的每个 libportal 版本建立 integration test，而不是相信未校验的生成注释。

实施：

1. 新增 user-session `input_backend_libei.{h,cpp}`；root `ahk-inputd` 不能代表用户完成 portal consent，libei 必须由当前登录 session 的 broker/runtime 持有，再通过受保护 sideband 与 root capture/uinput broker 协调；
2. 异步创建 RemoteDesktop session，申请 keyboard/pointer，Start 成功后每个 session 只调用一次 ConnectToEIS；一旦 EIS 建立，该 session 不再混用 portal `Notify*` injection；
3. `ei_new_sender()` 与 `ei_setup_backend_fd()` 是同步初始化调用，后者接管 EIS fd；后续连接 handshake 才通过 `ei_get_fd()`/`ei_dispatch()` 异步推进；
4. 处理 `CONNECT → SEAT_ADDED`，按 seat 实际 capability 调用 `ei_seat_bind_capabilities()`；随后处理 `DEVICE_ADDED/REMOVED/PAUSED/RESUMED`；
5. device resumed 后，以递增 32-bit sequence 调用 `ei_device_start_emulating()`；建立它到 64-bit AHK transaction 的 session-local sideband 映射并处理 wrap，但不能把该 sequence 当全局 provenance；每组 keyboard/pointer/text 事件后调用 `ei_device_frame(ei_now())`；停止前先把 key/button/touch/gesture恢复 neutral，再 `ei_device_stop_emulating()`；
6. keyboard keycode 路径读取 EIS keymap；libei 1.6+ 若 seat/device 实际提供 `EI_DEVICE_CAP_TEXT`，可 runtime-gated 使用 `ei_device_text_keysym()`/`ei_device_text_utf8()`；不能把编译到 1.6 等同 compositor 已授予 TEXT capability；
7. sender context 只用于 RemoteDesktop injection；InputCapture 使用独立 `ei_new_receiver()` context，并遵守 portal Activated/Deactivated、barrier、frame 与 start/stop-emulating 事件流，不能复用 sender 对象；
8. 处理 disconnect/session closed/device pause/permission revoke；旧 context、seat、device 全部归 session generation；
9. 纳入 mux health state，并在持久授权失效时进入 `REAUTH_REQUIRED`；
10. GNOME/KDE real-host 验证实际 portal、libportal 和 libei 版本组合。

libei 解决的是授权 injection，不自动解决永久 global capture，也不传递 AHK SendLevel/transaction metadata。项目只能通过同一 session broker 参与者之间的 sideband 把某次 libei frame关联到 AHK transaction；这不是系统级 loopback metadata。outcome 至少区分：

```text
SUBMITTED_TO_LIBEI
EIS_PROCESSED        // 可选 ping/pong 到达，仅表示 EIS 处理到同步点
TARGET_DELIVERED_UNKNOWN
TARGET_CONSUMED_UNKNOWN
```

libei/portal 没有目标应用消费 ack，不能把 transaction 写成 delivered。TEXT capability 不存在时只能可靠发送当前 keymap 可表达字符；TEXT 存在时可提交 keysym/UTF-8，但目标 toolkit 如何消费仍需真实 oracle。

## 8.4 验收

- GNOME/KDE pure Wayland、无 XWayland：用户授权后 Send 基本键和当前 keymap 可表达字符；当 runtime 提供 `EI_DEVICE_CAP_TEXT` 时另测 keysym/UTF-8，不提供时不可表达字符按 capability 明确走 IME/paste fallback 或返回不支持；
- 拒绝授权时明确错误；
- portal session closed 后不静默 no-op；
- InputCapture 只在实际支持场景标 enabled；
- inputd 无 layout source 时 char_stream 明确 unavailable；
- layout switch 后文本正确更新。

---

# 9. P1-6 — Wayland compositor/display restart recovery

## 9.1 当前遗漏

单纯 `wl_display_dispatch()` 失败后把全局状态设为 failed，会使所有旧 proxy、keymap fd、virtual device 和 screenshot object 失效；重新调用某个 Send 函数并不能安全复用它们。

## 9.2 完整 teardown/rebuild

建立 `WaylandSession` owner，所有 proxy 归其 generation：

```text
on fatal dispatch/flush error:
  mark generation dead
  stop new operations
  fail/abort outstanding transactions
  destroy child proxies in dependency order
  close keymap and capture fds
  disconnect display
  clear registry-name map
  schedule reconnect

on reconnect:
  wl_display_connect
  get registry
  bind globals with negotiated versions
  roundtrip/bootstrap
  recreate seat/keyboards/virtual devices
  upload keymap
  replay desired subscriptions
  reconcile held state
  mark healthy
```

`global_remove` 不能是 no-op。需要保存 registry name→object/type，移除 compositor、seat、virtual keyboard manager、screencopy manager 时分别降级对应 capability。

## 9.3 outstanding operation policy

- Send transaction：失败并返回已发送数量，不能无条件重放非幂等按键；
- screenshot：cancel；
- pointer move：由调用者决定是否 retry；
- hotkey registration：desired state 可重放；
- clipboard offer：旧 generation 立即作废；
- keymap lease：旧 fd 关闭。

## 9.4 真实 oracle

必须真正销毁 socket/compositor，而不是 SIGSTOP/SIGCONT：nested sway/Weston 启动→运行脚本→终止 compositor→重启新实例→验证同一 AHK 进程恢复。另测 registry global 动态移除、seat remove、manager version change。

---

# 10. P1-7 — D-Bus blocking、restart 与 reentrancy

## 10.1 统一 call abstraction

`core_com_dbus_linux.cpp` 中的 `dbus_connection_send_with_reply_and_block(..., -1, ...)` 应从主事件线程移除。这里必须校准原审计措辞：libdbus 的 `-1` 表示使用库的[默认 method-call timeout](https://lists.freedesktop.org/pipermail/dbus/2023-March/018263.html)（常见约 25 秒），不是与 bus/service 动态协商的 timeout，也并非 API 层字面“永不超时”；问题仍然严重，因为该 pseudo-blocking 调用在默认 timeout 内不泵 AHK/GUI 事件、不可由本次脚本操作取消，且项目 budget 不透明。项目已有 `core_atspi_linux.cpp::AtspiPendingReply()` 作为可复用范式：pending call、显式有界 deadline、短片 dispatch、cancel、nested-call guard。

建议抽成 `dbus_runtime.{h,cpp}`：

```cpp
struct DbusCallOptions {
    int timeout_ms;
    uint64_t service_generation;
    bool pump_runtime;
    bool allow_reentry;
    CancellationToken cancel;
};

DbusCallResult LinuxDbusCall(...);
```

若改用 GDBus，其 `g_dbus_connection_call()` 原生提供 async callback、`timeout_msec` 与 `GCancellable`；参考 [GDBusConnection.call](https://docs.gtk.org/gio/method.DBusConnection.call.html)。无论用 libdbus 还是 GDBus，都不能在主事件线程保留不可取消、不可见 budget 的 pseudo-blocking wait；每次调用应有项目自定义的显式上限。

## 10.2 timeout 分级

不要用一个 magic timeout 覆盖全部服务：

- capability probe：250–500ms；
- 用户显式 Control 操作：1–3s；
- portal consent：由 session async lifecycle 管理，不在主线程同步等待；
- clipboard target request：单独 bounded budget；
- shutdown/cancel：短 deadline。

所有 timeout 应有配置边界和 telemetry，但不能让环境变量把安全上限设成无限。deadline 必须从进入公共 call wrapper 前开始，覆盖 message build、可能阻塞的 flush、poll/dispatch 与 reply parse；如果继续使用同步 `dbus_connection_flush()`，就不能声称 budget 是端到端有界。`dbus_pending_call_cancel()` 只表示本地不再接收/处理 reply，不会撤销远端方法副作用，故超时后的自动 retry 必须按方法幂等性决定。

## 10.3 service generation

监听 well-known name 的 `NameOwnerChanged`：

- 任何 well-known name 的 unique owner 变化（包括非空 old→非空 new）都 generation++、取消 pending、旧 proxy/cache stale；
- D-Bus connection/bus reconnect 产生新的 connection generation，即使 well-known name 字符串看似相同也不得复用旧对象；
- new owner 出现：re-probe/re-register；
- reply 到达时若 connection/service generation 不匹配，丢弃；
- AT-SPI bus address 变化时整个 cache generation 作废；
- tray watcher、portal、IBus、Fcitx 各自维护 service generation。

## 10.4 reentrancy policy

主循环 pump 会使 AHK callback 在同步 API 尚未返回时运行。必须：

- 对共享 cache/connection state 使用 operation guard；
- 同类 nested call 返回明确 `BusyError` 或进入独立 queue；
- callback 不持有裸对象指针，使用 object generation/weak handle；
- 取消时确保 callback 只完成一次；
- 禁止 nested call 覆写上一层 deadline。

## 10.5 fault oracle

fake D-Bus service 覆盖：不回复、延迟回复、reply 后退出、owner 快速切换、错误 signature、断 bus、callback 内重入、取消与 reply 竞态。要求 GUI/timer/hotkey 仍有响应，fd/pending call 无泄漏。

---

# 11. P1-8 — production-duration reliability

## 11.1 分层 gate

| Gate | 频率 | 时长 | 主要目标 |
|---|---:|---:|---|
| PR smoke | 每 PR | 30–120s | 快速 regression |
| nightly | 每晚 | 1h | leak、reconnect、event drift |
| weekly | 每周 | 8h | rare race、服务轮换、storm |
| release candidate | 每 RC | 24h | production claim |

24 小时结果必须是可下载 artifact，记录 commit/build/env，不应只在 release note 口述。

## 11.2 指标

至少每分钟记录：

```text
RSS/PSS, heap where available
fd count and fd type
thread count
D-Bus pending calls/proxies
Wayland proxy generation/count
X11 grabs
inputd device/client/rule count
uinput replay errors
backend reconnects
registration desired/actual delta
held key count
input latency p50/p95/p99/max
callback queue depth
clipboard owner changes
IME transaction count/cancel count
```

只看 RSS slope 会漏 fd、proxy、stale registration 和 latency drift。

## 11.3 fault 与 storm schedule

在 soak 中确定性注入：inputd restart、portal owner restart、GNOME extension reload、D-Bus service stall、clipboard owner storm、layout switch、IME engine switch、device hotplug、client crash、compositor restart。每次注入后检查恢复不变量。

另外建立非时长型 stress gate，避免“跑得久但事件太少”：

- 至少百万级独立 producer 输入事件，核对 sequence gap、down/up balance、latency 和 target delivery；
- registration/unregistration storm，包含 HotIf variant、combo/shared prefix 与 route recalculation；
- 多脚本并发启动/退出/崩溃 storm，检查 lease、rule、client fd 和 provenance generation 回收；
- device hotplug/keymap/IME/clipboard owner 高频轮换；
- protocol malformed/slow-client/backpressure 与正常事件并行。

## 11.4 通过标准

- 无单调增长的资源计数；
- 事件总数、down/up 平衡；
- 每次故障在 SLO 内恢复或明确 degraded；
- 无 silent no-op；
- p99 latency 有阈值；
- 失败 seed 可复现。

---

# 12. P1-9 — Tray、clipboard 与 session service restart

## 12.1 共用 ServiceLifecycle

Tray、portal、IME、clipboard extension 不应各自实现半套 reconnect。共用：

```text
service name
unique owner
connection generation
proxy generation
desired registrations
last applied state
pending calls
health/reason
```

## 12.2 Tray/SNI

- 监听 `org.kde.StatusNotifierWatcher` owner；
- watcher 新 generation 出现后重新注册 item；
- 重建 dbusmenu object export；
- 重放 icon、tooltip、visible、menu revision；
- 旧 watcher 的 callback 丢弃；
- 无 tray host 时状态为 `UNAVAILABLE_ENVIRONMENT`，不是 success；
- KDE real-host 测 watcher kill/restart。

## 12.3 Clipboard

- 每次 ownership/offer 带 generation；
- X11 selection owner、Wayland offer、GNOME extension watcher 分开建模；
- SendText paste transaction 记录原 owner，恢复时 compare-and-swap；
- 外部 owner 已变化则不恢复旧数据；
- service restart 后重新订阅 watcher；
- OnClipboardChange 去重基于 owner/offer identity，不只基于文本相等；
- pure Wayland 的通用 `wl_data_device` 不是全局 clipboard watcher；若 compositor 不提供 data-control/extension/portal watcher，状态必须是 `GLOBAL_WATCH_UNAVAILABLE`，不能靠本进程 offer 冒充全局观察；
- OnClipboardChange type 需要根据 MIME 集合区分 text/non-text/mixed/unknown；无法枚举时返回 unknown，而不是固定近似 0/1。

## 12.4 验收

kill/restart bus、watcher、extension、clipboard manager 后，同进程功能恢复或返回明确状态；菜单不重复、clipboard 不回滚用户新复制内容。另在 GNOME、KDE 和至少一个 data-control compositor 上分别验证全局 watcher、non-text type、owner storm；没有协议支持的环境应稳定报告 unavailable。

---

# 第三部分：P2 — 功能纵深与真实环境验证

# 13. P2-1 — libei/EIS/RemoteDesktop injection

## 缺失细化

- 无 build dependency 和 runtime probe；
- 无 portal session/EIS fd 生命周期；
- 无 seat/device capability binding；
- 无 reconnect/permission revoke；
- 未进入 Send route；
- 无 GNOME/KDE E2E。

## 解决思路

1. 可选构建依赖 `libei`，pin/记录 libei、libportal 与 portal interface version；
2. 新增 `LIBEI` backend kind 和 caps，由 user-session broker 持有 consent/EIS；
3. portal Start/consent 成功后 ConnectToEIS；`ei_new_sender/setup_backend_fd` 同步初始化，后续 handshake 异步 dispatch；
4. 完整处理 seat bind、device add/resume/pause/remove、start/stop emulating 和 per-frame flush；
5. Send planner 按 device runtime capability 选择 KEYBOARD、POINTER、BUTTON、SCROLL、TEXT；TEXT 仅在 libei 1.6+ 且 compositor 实际提供时使用；
6. transaction outcome 分 `SUBMITTED/EIS_PROCESSED/TARGET_UNKNOWN`，不虚构 delivery ack；
7. InputCapture 另用 receiver context 和 portal activation flow；
8. 处理 portal closed、EIS disconnect、device pause 与 `REAUTH_REQUIRED`；
9. AHK level/provenance 仅用 sideband 管理，不声称 EI wire 携带 metadata；
10. GNOME/KDE 各至少两个版本实机验证。

验收：无 XWayland、无 root uinput 时，授权后可向 GTK/Qt/native Wayland 应用提交 key/pointer，并在 TEXT capability 可用时提交 UTF-8；拒绝/撤权后明确失败或 `REAUTH_REQUIRED`；授权仍有效的 transient session restart 可恢复。

---

# 14. P2-2 — KDE Plasma real-host matrix

建立专用 KDE VM，不把 headless sway 或 XWayland 当 KDE 证据。矩阵至少包括：

- Plasma 6 当前 LTS/主流发行版；
- Wayland 与 X11 session；
- GlobalShortcuts portal/KGlobalAccel；
- SNI/dbusmenu；
- Qt6 AT-SPI text/action/selection/value；
- Fcitx5 中文；
- libei RemoteDesktop；
- clipboard text/rich formats；
- compositor restart、logout/login；
- scale 100%/150%、双显示器；
- Flatpak Qt app。

产物记录 `plasmashell --version`、KWin、portal backend、Qt、Fcitx、locale、GPU/VM 类型和 commit。

## 14.1 其他 compositor 与 mixed XWayland matrix

不要把 headless sway 外推到全部 wlroots，也不要把 XWayland application 成功外推到 native Wayland：

- 至少加入真实 sway、Hyprland，以及 River/Wayfire/niri 中一个；Weston 作为协议基线，COSMIC 按其实际 portal/compositor API 单列；
- 每个环境记录 GlobalShortcuts、InputCapture、RemoteDesktop/EIS、virtual-keyboard/pointer、screencopy、data-control、AT-SPI 的实际版本和 availability；
- mixed corpus 同时运行 native GTK/Qt 与 XWayland app，验证 focus identity、Send target、clipboard ownership、窗口枚举和 PID/app-id confidence；
- compositor 不支持某 private protocol 时必须按 capability 降级，不能通过环境名推测“兼容 wlroots 即可用”；
- compositor kill/restart、output hotplug、fractional scale 与 XWayland restart 分别做 recovery oracle。

---

# 15. P2-3 — 真实 Fcitx5 中文 E2E

Fcitx5 的 D-Bus frontend 提供 `org.fcitx.Fcitx.InputMethod1` 与 `InputContext1`，包括 `ProcessKeyEvent`、`ProcessKeyEventBatch`、`CommitString`、formatted preedit、focus 和 surrounding text。参考 [Fcitx5 dbusfrontend 源码](https://github.com/fcitx/fcitx5/blob/e70d2b18/src/frontend/dbusfrontend/dbusfrontend.cpp)。

必须先说明 D-Bus frontend 的观察边界：AHK 自己创建的 InputContext 只会收到由 AHK 提交给该 context 的 key/commit/preedit；它不能旁观 GTK/Qt/Electron 已创建的其他应用 context。要验证“目标应用真实 Fcitx 输入被 AHK Hotstring/InputHook 观察”，必须选择以下之一并把架构写清：

1. Fcitx5 addon/受支持代理，在 engine→目标 context 的 commit/preedit 点镜像带 context identity 的事件给 AHK；
2. compositor/inputd 独占捕获 → AHK 自建 InputContext 调用 `ProcessKeyEventBatch` → 将 commit/forwarded key 正确重注入目标应用的受控链路；
3. 目标应用/toolkit 内 test instrumentation，仅作为 E2E oracle，不冒充通用产品能力。

## 实施

1. 在真实 KDE Wayland 安装 Fcitx5 + 拼音 engine；
2. 先实现并记录上述 observation architecture，不能只创建旁路 context；
3. 先调用/探测 `InputMethod1.Version`，再决定 batch capability；`ProcessKeyEventBatch` 返回 UnknownMethod 或版本不足时回退 signal + `ProcessKeyEvent`，fallback 必须有 oracle；
4. 明确选择 transport mode：signal mode 才订阅 CommitString、UpdateFormattedPreedit、ForwardKey、DeleteSurroundingText、NotifyFocusOut；batch mode 调用 `ProcessKeyEventBatch` 时，commit/preedit/forward/delete 会作为返回的 `a(uv)` batched records 提供，必须完整解码，不能同时等待同一批次的普通 signals；
5. 为每个真实 context identity/focus generation 分配 composition transaction；
6. 若采用受控代理链，batch mode 只保证一次 method call 返回的 records 有序，不自动保证跨多次调用的整个 composition；实现仍需 composition id/focus generation，并正确处理 handled bool、全部 batched variant、forwarded key 与目标重注入；
7. `KeyEventOrderFix` 是 client 设置/协商的 capability flag；实现应在支持的 context 上设置并验证效果，不能把它写成 server 自动提供的普通 guarantee；
8. focus out/reset/engine restart 时明确 cancel transaction；
9. commit 以完整 UTF-8 字符串进入 normalized text event，再按 scalar/grapheme policy 分发；
10. 分开测试“AHK 自有 context 工作”和“目标应用真实 context 可观察”，后者只有在 addon/proxy/instrumentation 到位时才能标 real E2E；
11. 测试 Hotstring/InputHook 与 commit 的交互、重复抑制和目标应用实际文本。

## corpus

`你好`、连续长句、中英切换、数字/标点、候选翻页、快速选词、取消 composition、focus 在 GTK/Qt/Electron 间切换、engine restart、一次 commit 多 scalar、emoji candidate；每项都记录 context owner，防止把 AHK 自测 context 当成目标应用证据。若现有 CI 只是发送 destinationless/broadcast-shaped signal，则最高只能标 payload/parser oracle；真实 Fcitx signal 是定向 context owner 的，必须由真实 owner/context 链产生才算 integration/E2E。

---

# 16. P2-4 — Flatpak host E2E

manifest 存在不等于功能可用。需要真实安装的 Flatpak app 与打包后的 AHK runtime 双向测试：

- portal app-id 与 desktop file 正确；
- GlobalShortcuts consent；
- RemoteDesktop/libei；
- InputCapture 的实际可用边界；
- AT-SPI exposure；
- IBus/Fcitx；
- host clipboard；
- 文件 chooser/沙箱路径；
- host inputd socket 是否有意暴露；
- extension interaction；
- 卸载后 permission/session 清理。

默认禁止直接把 privileged inputd socket 暴露给任意 Flatpak；若提供，需 portal/host helper 和 capability grant。

---

# 17. P2-5 — OEM、media、vendor scan code

## 方案

1. 维护机器可读 mapping table：evdev KEY_* → canonical set-1/AHK SC → VK/name；
2. 区分标准、E0、E1、多字节/无 Win32 等价键；
3. 未知键保留 evdev code，不能映射成 0 后丢失；
4. 媒体键、brightness、mic mute、Fn/vendor key 单独标记；
5. 多布局只改变 logical layer，不改变 physical SC；
6. 生成 table completeness report；
7. 使用真实 USB 键盘/consumer-control device 与 `evtest` 独立录制；
8. 测试 hotplug 和 keymap replacement。

验收 corpus：Pause/Break、PrintScreen、numpad enter/divide、左右 modifier、media、browser、launch、国际键、JIS/ABNT、vendor extra keys。

---

# 18. P2-6 — Rich `ClipboardAll`

## 数据模型

```cpp
struct ClipboardSnapshot {
    uint64_t owner_generation;
    std::vector<ClipboardRepresentation> items;
};

struct ClipboardRepresentation {
    std::string mime;
    std::vector<uint8_t> data;
    bool lazy;
};
```

支持至少：text/plain UTF-8、HTML、RTF、PNG、URI list/file list、custom MIME。设置时一次 snapshot 可提供多个 representation；读取要有 per-MIME size/time limit。

## 平台实现

- X11：TARGETS 枚举、INCR 大数据、MULTIPLE（可选）、selection manager/persistence；
- Wayland：枚举 data offer MIME，异步 fd 读取；
- GNOME extension/data-control：按实际 capability；
- ClipboardAll serialize format 要带 version、长度、MIME、checksum 和上限；
- paste fallback 恢复必须 compare owner generation。

## 安全

限制单项/总大小；临时文件 0600；拒绝 path traversal；敏感 clipboard 不落日志；custom MIME 视为不可信二进制。

验收：text+HTML、PNG、文件列表、未知 MIME、多 representation、100MB bounded failure、owner exit、X11↔Wayland bridge、并发 user copy。

---

# 19. P2-7 — native Wayland Win* foreign-window

这是平台+实现问题，不能设计一个虚假的通用 backend。应建立 adapter 层：

- GNOME：Shell extension/desktop API，明确版本约束；
- KDE：KWin scripting/DBus；
- wlroots：sway/i3 IPC 或 compositor-specific protocol；
- AT-SPI：仅提供 semantic accessible app/window identity；
- XWayland：保留 EWMH 路径。

每个 Win* operation 返回 `SUPPORTED / ADAPTED / NOT_SUPPORTED`，getter 不得返回本进程 cache 冒充 compositor state。App-specific identity 应包含 backend、native handle/opaque id、PID confidence 和 generation。

X11 路径也要继续收窄“本地缓存即真实状态”的问题：

- `WinGetText` 优先组合 EWMH/X11 property 与 AT-SPI semantic text，并在无法取得 child-control text 时返回明确的窄能力状态；
- `TransColor` 若没有 compositor effect，必须返回 `NOT_SUPPORTED`，不得仅写入 process-local map；
- opacity、workspace、min/max、style getter 优先重新查询目标窗口；仅能映射的字段标记 `ADAPTED`；
- X11 child window 与 GTK/Qt semantic control 必须分开枚举，不能合并成一个伪 Win32 control tree；
- XWayland PID、sandbox/app-id 的 confidence 写入结果，不能把代理进程当成确定目标应用。

验收应覆盖多个 WM（至少 Mutter/X11、KWin/X11、轻量 EWMH WM）、workspace、override-redirect、WM restart、目标进程退出和 stale XID 复用。

---

# 20. P2-8 — LibreOffice virtual table cell

实现 AT-SPI Table/TableCell 扩展路径：

1. 从 table 获取 row/column count；
2. 通过 cell-at-index/row-column API 获取 virtual cell accessible；
3. 读取 Text/Name/Value；
4. 编辑时优先 EditableText/Action；
5. 写后 readback；
6. 滚动/虚拟化后重新 resolve，禁止长期缓存 object path；
7. 大表使用窗口化访问与 deadline。

测试 Calc 真实文件：空/文本/数字/公式、合并单元格、隐藏行列、滚动后 cell、编辑与 readback、service restart。

---

# 21. P2-9 — Chromium/Firefox accessibility corpus

建立页面 fixture：native form、ARIA button/listbox/grid、contenteditable、Shadow DOM、iframe、Canvas、自绘 widget、virtual list、Monaco-like editor。

每项记录 accessibility tree 实际 exposure，并区分：

- port 未实现接口；
- 浏览器未暴露语义；
- app 禁用 accessibility；
- sandbox/flag 限制。

当 Document 仅返回 U+FFFC 或不可编辑时返回明确 `NotSupported/ContentNotExposed`，不要以空字符串表示成功。对于 Monaco，推荐可选 VS Code extension/命令 API adapter，不把它伪装成通用 AT-SPI。

---

# 22. P2-10 — Unicode grapheme/ZWJ

内部输入 pipeline 应保留两层：

- code point stream：兼容 AHK OnChar/键级语义；
- text transaction：保留一次 IME commit 的完整 UTF-8/UTF-32 序列、grapheme boundaries。

不要把 grapheme 当物理 key。Hotstring buffer 应定义匹配单位（与 Windows golden 对齐），backspace replacement 应按目标应用实际文本单位验证。

corpus：combining acute、Hindi/Arabic combining、emoji skin tone、variation selector、family ZWJ、flag regional indicators、一次 commit 多汉字、invalid UTF-8 拒绝、supplementary plane。

可采用 Unicode grapheme break 数据生成测试，但最终仍需 Windows AHK observable differential。

---

# 23. P2-11 — custom combo 完整 Windows matrix

将 evdev combo 状态机正式建模：

```text
IDLE
→ PREFIX_DOWN_PENDING
→ COMBO_ARMED
→ SUFFIX_DOWN
→ FIRED_DOWN
→ WAIT_RELEASE
→ FIRED_UP
→ BALANCE_AND_IDLE
```

每次 transition 输出 reason 和 transaction。覆盖：

- prefix 单独 tap/hold/repeat；
- 错误 second key；
- 快速 rollover；
- suffix 先释放/prefix 先释放；
- shared prefix；
- wildcard/tilde；
- modifier + combo；
- sc prefix；
- HotIf true/false/slow；
- combo up；
- remap；
- 两脚本冲突；
- device hotplug 时 prefix held。

所有 replay 必须 down/up 平衡；HotIf false 时 original delivery 与 Windows golden 一致。X11 无法实现时应路由到 evdev 或明确拒绝。

---

# 24. P2-12 — keymap replacement/layout daemon restart

## 需要区分

- layout group switch；
- 整个 keymap replacement；
- compositor/seat replacement；
- X11 MappingNotify/XKB notification；
- input method engine switch；
- device hotplug；
- locale/compose table变化。

## 方案

每个 keymap 带 generation 和 immutable snapshot。收到变化：

1. 建立新 xkb keymap/state；
2. 从 physical reducer 重放当前 held keys；
3. 原子交换 active snapshot；
4. 旧 event 继续绑定旧 generation 或丢弃；
5. 重新计算 logical hotkey registration；
6. physical `scXXX` registration 不应无故变化；
7. 更新 Send Unicode reverse mapping；
8. 发布 diagnostics event。

验收：US↔AZERTY↔含 AltGr、group switch、完整 keymap reload、daemon restart、切换时键 held、高频 typing。

---

# 第四部分：P3 — 文档、诊断与可维护性

# 25. P3-1 — 历史 CHECK_REPORT 与 current 状态混杂

每份报告顶部增加固定 front matter：

```yaml
kind: historical-audit | generated-current | user-doc
valid_for_commit: <sha>
generated_at: <UTC>
environment: <id>
evidence_level: A|B|C|D|E
superseded_by: <path or null>
```

历史章节加明显 banner，不进入 current capability 生成流程。`audits/README.md` 索引 `check0901.md` 与本文，并指出最新用户状态入口。

---

# 26. P3-2 — SUPPORT_MATRIX 元数据不足

生成器必须输出：

- source commit/dirty；
- binary SHA-256；
- runner version；
- kernel/distro/session/compositor；
- X11/Wayland display；
- portal/GNOME/KDE/IME versions；
- privilege/inputd mode；
- timestamp/duration；
- evidence level；
- pass/fail/skip/not-run reason；
- log artifact URL/hash。

`not-run` 不能参与通过率；`skip` 必须分类 environment-unavailable、permission-denied、unsupported、flaky-quarantined。当前 15 个 `not-run` 必须进入 owner+environment+deadline 清单：能在 hermetic/Xvfb/inputd namespace 跑的转 blocking CI，AT-SPI/SNI/evdev root 等转专用 host job，KDE/Flatpak 等保留明确 external-host gate；不得让 generated matrix 长期静态停留在 not-run。

Sanitizer 也按实际路径拆 gate：TSan 除现有 input subset 外逐步覆盖 GTK callback queue、D-Bus、Wayland registry 与 inputd client；ASan/LSan 记录实际执行模块和 suppression，不能从一个绿色 job 外推全程序无 race/leak。

---

# 27. P3-3 — silent fallback/no-op

建立统一 `CompatibilityOutcome`：

```text
EXACT
ADAPTED
DEGRADED
NOT_SUPPORTED
PERMISSION_DENIED
TEMPORARILY_UNAVAILABLE
FAILED
```

首次降级发一次 warning，后续可查询；严格模式把 DEGRADED 转 error。重点覆盖：SendText clipboard、SendPlay adaptation、native Wayland Win*、tray host absent、portal denied、Control content not exposed、ClipboardAll text-only。

---

# 28. P3-4 — source comments 引用旧 audit 编号

把“check_detail0821 §…”迁移为稳定设计文档链接，例如：

```text
docs/architecture/input-pipeline.md
ADR-0007-input-provenance.md
ADR-0008-inputd-arbitration.md
ADR-0009-backend-recovery.md
```

audit 只用于历史依据，源码注释引用设计 invariant、函数 contract 或 ADR。这样 audit 改名不会使代码注释失效。

---

# 29. P3-5 — magic timeout、poll interval、环境变量

建立 typed config registry：名称、单位、默认值、min/max、是否安全敏感、是否 test-only、文档。生产不能通过 env 将 fail-open watchdog、D-Bus timeout 等设为无限。

统一 monotonic clock 和 injectable test clock；避免散落 `30ms/200ms/1s/2s`。诊断输出 effective config，但对路径/token 等敏感值脱敏。

---

# 30. P3-6 — capability 的 implemented 与 verified

每项能力至少四维：

```text
implemented: yes/partial/no
runtime_available: yes/no/degraded
verified_level: A/B/C/D/E/none
limitations: [...]
```

按 backend+environment 展开，不允许一个 `bool supported` 同时代表“代码存在”“当前可用”“实机通过”“Windows 等价”。generated docs 从同一 machine-readable registry 生成。

---

# 31. P3-7 — backend diagnostics/status API

新增用户可查询对象或 CLI JSON：

```json
{
  "schema": 1,
  "session": "wayland",
  "generation": 7,
  "routes": [],
  "backends": {
    "inputd": {
      "state": "healthy",
      "coverage": {"grabbed": 2, "failed": 0},
      "replay": "healthy",
      "protocol": 2,
      "provenance": "authoritative"
    },
    "portal": {"state": "permission-denied"},
    "libei": {"state": "not-configured"}
  },
  "last_errors": []
}
```

应能回答：某 hotkey 为什么选这条 lane、能否 suppress、为何未触发、Send 实际走了什么 backend、是否 clipboard fallback、上次重连/降级原因、当前 keymap/IME generation。

---

# 第五部分：正文中未完全进入 P0-P3 表的缺口

# 32. X11 pass-through reinjection

X11 passive grab 后再 XTEST reinject 不等于 original event 继续送达。短期方案：

- trace 标记 `PASSTHROUGH_VIA_REINJECTION`；
- 严格 compatibility 模式拒绝依赖 original identity 的场景；
- 对 target delivery 建独立 recorder，比较时间、repeat、device/source；
- 防止 reinjected copy 再次被同/其他 AHK client递归处理；
- 尽可能评估同步 grab + `XAllowEvents(ReplayKeyboard)`，但要验证它与 HotIf、key-up、capture 的实际可行性，不能仅凭 API 名称替换。

长期方案是把需要 suppression/replacement 的多脚本场景移到 broker arbitration；纯观察的 `~` 优先走 XI2 raw observation，避免 grab。

## 32.1 wildcard、key-up、remap 与左右 modifier 收尾

这些能力已有实现基础，但还需要统一状态与 differential 才能关闭：

- wildcard：匹配必须基于 normalized modifier snapshot，分别覆盖额外 Shift/Ctrl/Alt/Super、Caps/NumLock、AltGr 和左右 modifier；不能让不同 lane 各自屏蔽不同 lock mask；
- key-up：将 press/release 视为一个 ownership pair。仅监听 `up` 时，press 应尽可能原样送达目标，release 才触发；若 X11 lane 只能通过消费 press 保持 grab，就在 capability 中标出偏差，并优先路由到能表达 pair ownership 的 broker；
- remap：把 source-down→target-down 与 source-up→target-up 放入同一 remap transaction；脚本退出、backend 切换、device unplug 或 replacement 失败时必须补齐 target release；
- 左右 modifier：physical SC/device state 与 logical AltGr 必须并存，不能把布局产生的 `ISO_Level3_Shift` 一概当普通 RAlt，也不能用合并后的 Ctrl/Alt 状态回答明确的 L/R `GetKeyState`；
- repeat：repeat 只扩展 down 语义，不得制造提前的 key-up callback；跨 X11/evdev lane 使用同一 reducer 判定。

验收矩阵应包含 `*F10`、`F12 Up`、`~a`、CapsLock/NumLock、L/R Ctrl/Alt/Shift/Super、AltGr、按住源键时 reload、remap 期间切 backend，以及独立 target recorder 对 original/replacement down-up 平衡的验证。

---

# 33. process-global text buffer 与 IME transaction

把文本状态按 context key 分区：

```text
(seat_id, focus_context_id, application_id, composition_id)
```

焦点切换、caret navigation、mouse click、Escape/cancel、target process exit、IME preedit start/end 都应刷新或关闭相应 buffer。一次 IME commit 保留 transaction，不应只拆成无关联 scalar。

若无法可靠取得 focus/application identity，至少在每次 focus generation 变化时清 buffer，并把 confidence 写入 trace。

## 33.1 IBus 剩余问题的具体关闭方式

- 把 focused InputContext、preedit、commit queue 按 D-Bus owner + object path + focus generation 分区，禁止用一个 process-global active context；
- engine 查询和 reconnect 使用 P1-7 的 async D-Bus wrapper，owner 变化时取消旧 pending call；
- 用 composition id/sequence 关联 raw key、preedit update/end、commit/cancel，逐步淘汰固定等待窗口；无法关联时标记 confidence，而不是凭时间邻近合并；
- focus transfer、application exit、Escape cancel、engine switch 都必须产生明确 transaction end/cancel；
- real-host corpus 扩到连续中文长句、中英快速切换、候选翻页、标点、一次多 scalar commit、focus 在两个应用间切换、IBus daemon/engine restart；
- Hotstring/InputHook oracle 同时检查不重复字符、不丢 commit、callback order 和 replacement 对目标应用的实际效果。

---

# 34. inputd security hardening

除 P0-3 的 protocol authorization 外，还需：

- packaged socket 默认最小权限；
- 考虑每用户 broker 或 root broker + per-user authenticated channel；
- 使用 `SO_PEERCRED` 做 policy，不只日志；
- 可选 `SO_PASSSEC`/LSM context；
- observe-only 默认，suppress/inject 需显式 grant；
- 敏感字段不进 journal；
- connection/rule/rate/memory quota；
- malformed frame fuzz；
- slow reader/backpressure policy；
- systemd sandbox 不得阻断必要 `/dev/input`/uinput，但尽量限制其他 filesystem/network capability。

安全评审必须把“本地 keyboard DoS”和“键盘窃听”作为正式 threat，而不仅是权限文档提示。

---

# 35. GUI/Menu 语义与 DPI

建立 event-order trace：create/show/focus/change/click/close/destroy、callback 中 destroy、嵌套 modal、timer/hotkey reentry。Windows 与 GTK 不必内部一致，但公开 callback order 应尽量兼容或明确 adaptation。

对象生命周期使用 generation/weak handle：callback 已排队后控件、Menu 或窗口被销毁时，旧 callback 必须安全取消或收到明确 closed state，不能访问悬空对象。Menu popup/MenuBar/check/disable/icon 需覆盖 callback 内修改同一 menu、destroy parent、重复 show；SNI/dbusmenu revision 与本地 Menu 状态必须在同一提交后同步。

DPI/坐标需区分 logical、device pixel、screen、client、monitor scale generation；fractional scaling 和跨 monitor move 时重新计算。IME focus lifecycle 与 GUI control destroy 同时测试，native Wayland 与 X11 分开记录 focus/activation 行为。

---

# 36. AT-SPI cache 与应用边界

已有 pending-call 和 bulk cache 是正确方向，后续需：

- cache entry 带 service owner generation；
- object path + owner 才是 identity；
- target disappear 立即 invalidate；
- focus/selection/value write 后 readback；
- large tree 分页/预算；
- callback nested call policy；
- Java wrapper 的虚假成功保持 readback fail；
- VS Code/Monaco 返回 `ContentNotExposed`，而非空成功；
- 应用专用 adapter 与通用 AT-SPI 明确分层。

---

# 37. 扩大 Windows differential

从当前 7 组扩到阶段性目标：

## Wave 1（阻断 P0）

SendLevel/InputLevel、SendMode/Input、self/other script、tilde target delivery、remap、combo。

## Wave 2（输入高级语义）

HotIf timing、A_PriorHotkey、repeat cadence、InputHook KeyOpt/Match/Timeout/reentry、Hotstring 全 option、Unicode。

## Wave 3（runtime/API）

exception type/message、File/Process、object/thread state、GUI event order、clipboard text/rich。

## Wave 4（Linux adaptation）

不能直接要求 byte-identical 的领域建立明确 compatibility contract，再由 Linux real-host oracle 验证。

每个 Windows golden 至少重复三次；非确定性字段需有逐字段理由，不能通过大量 whitelist 把差异隐藏掉。

---

# 38. Packaging、升级与协议迁移

建立 N-1↔N 测试：

- 旧 runtime + 新 inputd；
- 新 runtime + 旧 inputd；
- v1/v2 protocol negotiation；
- running daemon package upgrade；
- socket/service unit变化；
- group/udev permission migration；
- GNOME extension upgrade/rollback；
- user config preservation；
- uninstall 后 grab、socket、unit、permission 无残留；
- rollback 后仍可启动；
- AppImage self-update 前后 helper/protocol compatibility；
- AUR clean-chroot rebuild、依赖升级和 reproducible package metadata；
- GTK/X11/xkbcommon/libei/DBus 等 ABI/SONAME 变化后的启动与功能 probe。

协议不兼容时应 fail-open 并报 `PROTOCOL_INCOMPATIBLE`，绝不能已 grab 后才发现 client/server 不兼容。release artifact 要同时提供 minimum/maximum host-helper protocol 与关键 ABI dependency 清单。

## 38.1 AppImage 的 privileged backend 部署

AppImage 可以携带 `ahk-inputd`，但不应在首次运行时静默提权或自行修改 systemd、udev 和用户组。推荐拆成两个明确组件：

1. 无特权 AppImage 主程序，只提供 X11、portal、libei、AT-SPI 等当前会话可用能力；
2. 发行版签名/可审计的 host integration package，负责安装 inputd binary、socket/service unit、udev policy 和卸载迁移；
3. `ahk-linux-doctor` 只诊断缺失项并生成安装指引，不自动执行 root 变更；
4. helper 与 AppImage 通过 protocol version/capability negotiation 配对，版本不符时保持 fail-open；
5. GNOME extension、portal consent 和 input group 变更分别说明需要 logout/reload 的条件。

验收要覆盖“仅 AppImage”“AppImage + host integration package”“卸载 helper 后继续运行”“helper 版本旧/新于 AppImage”，并确保任何失败都不会留下 grab、过宽 socket mode 或失效 udev rule。

---

# 第六部分：实施顺序、依赖与完成定义

# 39. 推荐里程碑

| 里程碑 | 内容 | 前置 | 关键 gate |
|---|---|---|---|
| MF | P3 文档元数据、stable ADR 引用、typed config、implemented/verified schema | 无；贯穿全程 | generated docs lint、无 magic-unbounded config、audit/current 边界清晰 |
| M0 | inputd fail-open + held-key boundary + 最小 health ACK/STATUS | MF | replay fault/race oracle 全过 |
| M1 | consumer-specific SendLevel policies + SendMode Input 修正 | 无 | Hotkey/Hotstring 与 InputHook 两套 Windows matrix |
| M2 | backend health/generation/diagnostics API + CompatibilityOutcome | M0/MF | 断线状态可见、无 silent no-op；开始 nightly 1h baseline |
| M3 | protocol v2 identity + authorization | M0/M2 | framing/fuzz/security + v1/v2 negotiation tests |
| M4 | broker-owned injection + arbitration | M1/M3 | A/B/C multi-script corpus；开始 broker 1h/8h soak |
| M5a | normalized capture/state reducer 与普通 matcher | M1/M3 | trace 全链、基础旧路径可关闭 |
| M5b | normalized suppression/remap/combo 全迁移 | M4/M5a | replacement/owner transaction 全链 |
| M6a | libei injection + pure-Wayland keymap | M2/M5a | GNOME/KDE native functional E2E |
| M7 | D-Bus/portal/Wayland/service 统一 recovery | M2 | kill/restart fault matrix |
| M6b | libei/portal production recovery gate | M6a/M7 | 可持久 grant/restore token 下自动恢复；撤权或新 consent 必需时进入 `REAUTH_REQUIRED` 并提示用户 |
| M8 | P2 host/Unicode/Clipboard/AT-SPI 深化 | M5b/M6b/M7 | 各真实 host matrix；weekly 8h |
| M9 | release 24h + upgrade/rollback matrix | 前述全部 | RC artifact evidence |

## 39.1 不宜倒置的依赖

- 不要先扩展更多 inputd remap 功能，再修 fail-open；
- 不要先声称 multi-script parity，再做 protocol v2/provenance；
- 不要把 libei 当 global capture 解法；
- 不要在没有 generation/state machine 时给每个 backend 各写重连；
- broker arbitration 必须建立在已校正的 consumer-specific level policy 之上；
- 完整 normalized suppression/remap 迁移必须等 arbitration transaction 到位，不能只依赖 protocol identity；
- libei functional backend 可以先落地，但 production recovery claim 必须等待统一 service/compositor recovery；
- reliability 证据应从 M2 开始增量累积，不能把所有 1h/8h/24h 工作推迟到最终里程碑；
- 不要只扩 assertion 数量而不扩 external observable oracle；
- 不要让 ClipboardAll API 入口存在替代 rich MIME 实现。

# 40. 每个 issue 的统一完成定义

一个缺口只能在同时满足以下条件时标为 closed：

1. 设计 contract 已写入稳定 architecture/ADR；
2. 所有相关 backend 使用同一 contract；
3. error/degrade 行为机器可读；
4. 有独立或真实 host oracle；
5. fault/restart 路径已测；
6. generated capability 文档自动更新；
7. audit-bound commit 与 artifact 可追溯；
8. 不存在同名 API 的 silent narrower semantic 未披露。

---

# 41. 外部技术参考（Exa 补充）

1. AutoHotkey v2 SendLevel：<https://www.autohotkey.com/docs/v2/lib/SendLevel.htm>
2. AutoHotkey v2 #InputLevel：<https://www.autohotkey.com/docs/v2/lib/_InputLevel.htm>
3. AutoHotkey v2 SendMode/InputThenPlay：<https://www.autohotkey.com/docs/v2/lib/SendMode.htm>
4. AutoHotkey v2 Send/SendText/Text/Raw/Blind：<https://www.autohotkey.com/docs/v2/lib/Send.htm>
5. libei client API：<https://libinput.pages.freedesktop.org/libei/api/group__libei.html>
6. libei sender/frame/TEXT API：<https://libinput.pages.freedesktop.org/libei/api/group__libei-sender.html>
7. libportal `connect_to_eis` 生成页（调用顺序注释与当前实现冲突，只作签名参考）：<https://libportal.org/method.Session.connect_to_eis.html>
8. libportal 当前 `xdp_session_connect_to_eis` 实现：<https://github.com/flatpak/libportal/blob/main/libportal/remote.c>
9. RemoteDesktop `ConnectToEIS` 设计：<https://github.com/flatpak/xdg-desktop-portal/pull/762>
10. XDG InputCapture portal：<https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.InputCapture.html>
11. InputCapture 设计讨论：<https://github.com/flatpak/xdg-desktop-portal/pull/714>
12. GDBus async call/timeout/cancel：<https://docs.gtk.org/gio/method.DBusConnection.call.html>
13. EVIOCGRAB stuck-key bug：<https://lists.freedesktop.org/archives/wayland-bugs/2017-July/013022.html>
14. libevdev grab 边界讨论：<https://lists.freedesktop.org/archives/input-tools/2024-January/001587.html>
15. systemd readiness/watchdog：<https://www.freedesktop.org/software/systemd/man/sd_notify.html>
16. Exponential Backoff and Jitter：<https://aws.amazon.com/blogs/architecture/exponential-backoff-and-jitter/>
17. Fcitx5 D-Bus frontend：<https://github.com/fcitx/fcitx5/blob/e70d2b18/src/frontend/dbusfrontend/dbusfrontend.cpp>
18. Linux persistent input rules：<https://github.com/systemd/systemd/blob/master/rules.d/60-persistent-input.rules>
19. Linux uinput documentation：<https://docs.kernel.org/input/uinput.html>
20. XDG RemoteDesktop portal v2：<https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.RemoteDesktop.html>
21. AutoHotkey v2 InputHook/MinSendLevel：<https://www.autohotkey.com/docs/v2/lib/InputHook.htm>
22. Linux input event frame / SYN_DROPPED protocol：<https://docs.kernel.org/input/event-codes.html>
23. libdbus default method timeout 说明：<https://lists.freedesktop.org/pipermail/dbus/2023-March/018263.html>

---

# 42. 最终结论

`check0901.md` 所列问题的共同根因可以压缩为三个工程目标：

1. **输入安全闭环**：任何 grab 都必须与健康 replay 成对，失败自动释放；
2. **输入身份闭环**：capture、decision、Send/remap、injection、跨进程观察共享 transaction 与 provenance；
3. **生命周期闭环**：backend、D-Bus、Wayland、IME、tray、clipboard 都使用 generation、reconcile 和可查询 health。

最先应做的不是继续增加 API surface，而是依次关闭 inputd fail-open、SendLevel 方向错误、SendMode Input 不等价、protocol v2 provenance 与 arbitration。完成这四项后，再把 normalized event 推成唯一 pipeline，并以真实断线和 Windows differential 验证。否则更多 backend 和更多 assertion 只会继续放大多套语义之间的漂移。


