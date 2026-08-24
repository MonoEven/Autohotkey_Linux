# check_detail0824 — 缺失项细化与逐项解决思路

> 本文基于 `audits/check0824.md`（审计基线提交 `eaeeaf74`）的结论，把每一个
> P0/P1/P2/P3 问题拆解为可执行子项，并给出细致的解决思路：设计、实施步骤、
> 落点文件、验收标准、风险。写作时本地 HEAD 为 `3455044b`（晚于审计提交
> 5 个提交），第 0 节先对增量做核实校准，避免把已修的问题再列为缺口。
>
> 与 `check_detail0821.md` 的关系：0821 版聚焦"功能是否存在"，本文聚焦
> check0824 提出的更深一层问题——**语义等价、统一模型、外部效果 oracle**。
> 0821 已交付的项不再重复展开，只在相关小节引用。
>
> 外部技术情报（exa 检索，2026-08 时点）已逐项内联引用，本轮最重要的确认：
> ① libei 1.6 + liboeffis 是 RemoteDesktop portal 注入的成熟封装，
> `ConnectToEIS` 自 xdg-desktop-portal 1.17（2023 年中）即稳定；Xwayland
> 23.2+ 已把 XTEST 自动桥接到 libei（[Who-T 2026-07](http://who-t.blogspot.com/2026/07/libei-integrations-in-xdg-remotedesktop.html)）。
> ② **InputCapture portal 目前仅有 PointerBarrier 触发器**，没有"按需开始
> 捕获键盘"的触发器，因此它还不能作为 Hotstring/InputHook 的通用捕获通道
> （[flatpak/xdg-desktop-portal#714 讨论](https://github.com/flatpak/xdg-desktop-portal/pull/714)）。
> ③ AT-SPI 有 **`org.a11y.atspi.Cache.GetItems` 批量接口**，一次 D-Bus
> 往返可取整棵已实现树，是逐节点 `GetChildren` 之外的正解
> （[Ubuntu a11y 文档](https://documentation.ubuntu.com/desktop/en/latest/reference/accessibility/dbus/org.a11y.atspi.Cache/)）。
> ④ 剪贴板监听协议矩阵已明晰：KWin 6.6/Sway/Hyprland/niri/COSMIC 走
> `ext-data-control-v1`，Mutter 两者皆不支持（GNOME 侧只能走扩展），
> wl-clipboard 2.3.0 起支持 ext 变体（[wayland.app 支持矩阵](https://wayland.app/protocols/ext-data-control-v1)）。
> ⑤ 发布信任链的现代正解是 **GitHub Artifact Attestations（Sigstore）**，
> 公共仓库免费、免密钥管理，默认即 SLSA Build L2
> （[GitHub 文档](https://docs.github.com/en/actions/concepts/security/artifact-attestations)）。

---

## 0. 对 check0824 论断的本地核实（HEAD `3455044b` vs 审计基线 `eaeeaf74`）

| check0824 论断 | 本地核实结果 | 影响 |
|---|---|---|
| P1-4"失败 grab 仍进入 `sInstalled`，冲突后不重试" | **部分已修**：`3a75f6c1` 在 trap 检查前加了 `XSync(d, False)`（`core_hotkey_linux.cpp:1257`），并修正 `XNextRequest` serial 差一错误（`:1245`），跨进程 BadAccess 现在能被检出并报冲突错误，`multiscript_conflict` 场景转 PASS | 缺口收窄为两点：**(a)** `core_hotkey_linux.cpp:1283-1284` 仍把全部 pending（含失败者）插入 `sInstalled`，冲突方退出后不会自动重试；**(b)** 进程级全局 `XSetErrorHandler` 问题未动。§7 按此重定基线 |
| "audits 只有 0818-0821" | `audits/README.md` 未收录 check0824 与本文 | 交付时同步补一行索引 |
| 其余 P0-1…P3-1 的代码证据 | 逐一在 HEAD 复核：`input_backend.cpp:352` 单 `CurrentKind()` 调度、`core_capture_linux.cpp:50-52` 固定 128 缓冲、`core_hotkey_linux.cpp:412` scan-code 返回 0、`core_input_linux.cpp:858` 30ms keymap 借用、`core_ctrl_linux.cpp:97/445` 非 ASCII→`?`、`core_win_linux.cpp:266` XQueryTree、`core_com_dbus_linux.cpp:475` 无限期阻塞、`pack-finalize.sh:50-61` 临时密钥——**全部仍然成立** | 本文按 HEAD 行号锚定 |

---

# 第一部分 P0：架构性 / 核心语义问题

## 1. P0-1 输入 backend 没有统一事件与仲裁模型

### 1.1 缺失清单（细化为 6 个可关闭缺口）

| 编号 | 缺口 | 现状锚点（HEAD） |
|---|---|---|
| U1 | **没有统一 `InputEvent` 对象**：X11 用 `XEvent`，evdev 用 `input_event`，portal/GNOME 用 D-Bus 信号参数，字段互不兼容 | `core_capture_linux.cpp`（XEvent）、`core_evdev_linux.cpp`（input_event）、`input_backend_gnome_shell.cpp`（D-Bus） |
| U2 | **运行时仍是单 active-kind**：`Sync()/Dispatch()/Shutdown()` 全部 `switch (CurrentKind())`，`LinuxInputBackendRoute()` 的返回值没有对应的多 backend 并发注册器 | `input_backend.cpp:453/474/498/521/534` |
| U3 | **合成事件身份是进程本地启发式**：`LinuxSelfTrack()` 只记录本进程注入的 keycode+level，跨进程/跨 backend 无 provenance | `core_input_linux.cpp:488/836/1012` |
| U4 | **suppression 决策分散**：X11 由 grab 天然独占、evdev 由 EVIOCGRAB+回放、portal 由 compositor 消费，无统一 owner 概念 | 三个文件各自实现 |
| U5 | **无多脚本仲裁**：两个 ahk_core 进程各自直连 X/evdev/portal，冲突时只能撞错误 | P0-2、P1-3 的共同根因 |
| U6 | **capability 结构维度不足**：`AhkInputBackendCaps` 只有 9 个 bool，无法表达 scan_code / custom_combo / char_stream / synthetic_provenance / send_level | `input_backend.h:68-79` |

### 1.2 解决思路

**总体架构：分两阶段。阶段 A（本体内重构，1 个里程碑）先在单进程内建立统一
事件模型与真正的 per-hotkey 多路复用；阶段 B（跨进程 broker，第 2 个里程碑）
把捕获与仲裁抽到 `ahk-inputd` 单实例守护进程。不要试图一步到位——阶段 A 的
`InputEvent` 结构就是阶段 B 的 wire protocol 载荷，先在进程内验证字段设计。**

#### A. 统一 `AhkInputEvent` 结构（解 U1、U3 的进程内一半）

- **设计**：新建 `source/linux/core/input_event.h`，定义所有 backend 归一化后的事件：

  ```cpp
  struct AhkInputEvent {
      uint64_t  timestamp_us;     // CLOCK_MONOTONIC
      uint32_t  evdev_code;       // 物理层：KEY_* (evdev)；X11 keycode-8 反推
      uint16_t  vk;               // 逻辑层：Win32 VK（现有映射）
      uint16_t  sc;               // 物理层：AHK scan code（见 §3 三层模型）
      char32_t  text;             // 文本层：本事件产生的字符（无则 0）
      bool      is_release;
      uint8_t   source;           // PHYSICAL / SELF_INJECT / OTHER_INJECT / IME_COMMIT
      int8_t    send_level;       // 注入方声明的 SendLevel；物理=-1（视为最高）
      uint32_t  device_id;        // evdev 设备号或 XI2 sourceid
      uint32_t  origin_backend;   // AhkInputBackendKind
  };
  ```

- **实施步骤**：
  1. X11 路径：`core_capture_linux.cpp` 与 `core_hotkey_linux.cpp` 的事件入口处
     构造 `AhkInputEvent` 再进入匹配逻辑；`source` 判定用 XI2.1 raw event 的
     `sourceid` 是否等于 XTEST slave 设备 id（注意必须协商 **XI 2.1**，2.0 的
     raw event sourceid 恒为 0，这是历史 bug，见
     [inputproto 2.1 补丁](https://lists.x.org/pipermail/xorg-devel/2011-August/024396.html)）；
  2. evdev 路径：`core_evdev_linux.cpp:380` 附近读到 `input_event` 后立即归一化；
  3. portal/GNOME 路径：Activated/Deactivated 信号合成 press/release 对
     （text=0、device_id=0、source=PHYSICAL——因为 compositor 已经代表用户确认）；
  4. hotkey/hotstring/InputHook 匹配层全部改为只消费 `AhkInputEvent`。
- **M3-E 已交付**：`input_event.{h,cpp}` 定义 version 1 的统一事件，X11 raw/
  selected-grab、evdev、Portal、GNOME Shell入口归一化 timestamp、evdev_code、
  canonical VK/SC、text、release/repeat、source、send_level、device_id、origin；
  `HotkeyBackendGet().event_version` 与 `--diag` 可查询。可选
  `AHK_INPUT_EVENT_TRACE` 输出 JSONL oracle。Xvfb 证明本进程 SendLevel=3 为
  self_inject，独立 XTEST 为 other_inject且不能伪造 level；GNOME VM uinput
  证明 physical/evdev。匹配器分批消费该结构。
- **M3-M 已交付（真 per-hotkey mux）**：`LinuxInputBackendForHotkey()` 按每个
  hotkey 的 caps 需求（tilde/up/bare/wildcard/scXXX/custom combo/作为其他
  combo 的 prefix）路由到具体 backend；X11、portal、GNOME Shell、evdev 四个
  lane 可同时注册/泵送，各自只注册分配给自己的 hotkey。X11 desired/index/
  evdev 匹配器按 assigned 过滤；`HotkeyBackendGet().mux` 与 `--diag` 输出
  活动组合（如 `x11+evdev`）。运行时 Hotkey 开关差量启停 backend；BIF 注册前
  用 route 预测能力契约，注册后按真实 hotkey 重建 mux。场景 `input_mux`
  证明同一脚本内 XTEST F12→x11 与 uinput `a & b`→evdev 并发触发；`a_and_b`
  改为"路由成功或明确报错"。
- **落点文件**：`input_event.h/.cpp`；`core_capture_linux.cpp`、
  `core_evdev_linux.cpp`、`core_hotkey_linux.cpp`、`input_backend*.cpp`。
- **验收标准**：全量 doc-check 不回归；新增断言"XTEST 注入事件的
  `source==SELF_INJECT` 且 `device_id==XTEST slave id`"。
- **风险**：X11 事件入口分散（grab 事件 vs capture 事件两条路），归一化点
  必须放在 `LinuxDispatchHotkeys()`/capture pump 的最上游，否则出现双份逻辑。

#### B. 真 per-hotkey 多路复用器（解 U2）

- **设计**：现在 `LinuxInputBackendRoute()`（`input_backend.cpp:350-369`）算出
  了每个热键应走的 backend，但注册仍整体走 `CurrentKind()`。新增
  `InputMux` 层：维护 `map<HotkeyID, AhkInputBackendKind>`，`Sync()` 时按此表
  把热键分别注册到各自 backend；`Dispatch()` 轮询所有**有注册的** backend 而
  不是单个 CurrentKind。
- **实施步骤**：
  1. `Hotkey` 注册时调用 Route() 并把结果存进 mux 表（而不是丢弃返回值）；
  2. `LinuxInputBackendSync()` 改为遍历 mux 表分组 sync：X11 组走现有
     reconcile，portal 组走 `core_gshortcut_linux.cpp` 的 Bind，GNOME 组走
     extension Register，evdev 组更新匹配表；
  3. `LinuxInputBackendDispatch()` 依次 pump X11/portal/GNOME/evdev 四个事件源
     （各自已有非阻塞 pump，聚合即可）；
  4. `Shutdown()` 遍历全部曾激活的 backend。
- **落点文件**：`input_backend.cpp`（核心）、`core_hotkey_linux.cpp`
  （X11 注册入口）、`core_evdev_linux.cpp`。
- **验收标准**：一个脚本同时含 `F12::`（portal）与 `CapsLock & j::`（evdev），
  `--diag` 输出两条不同 backend 的注册记录且都能触发；现有单 backend 场景无回归。
- **风险**：同一物理按键被两个 backend 同时看到（如 evdev 捕获 + X11 grab）会
  双触发——mux 必须按 `evdev_code+timestamp` 去重窗口（阶段 B 的 broker 从根上
  解决，此处先做 5ms 去重兜底并记录诊断计数）。

#### C. `ahk-inputd` 单实例 broker（解 U4、U5，阶段 B）

- **设计**：参照 keyd 的架构（[rvaiya/keyd](https://github.com/rvaiya/keyd)：
  evdev 捕获 → 虚拟 uinput 设备输出 → IPC 客户端协议）。差异点：keyd 是
  remap 守护进程，ahk-inputd 是**多客户端事件分发 + suppression 仲裁**：
  1. 单实例（systemd socket activation + `flock`），独占 EVIOCGRAB；
  2. 客户端（每个 ahk_core 脚本）通过 UNIX socket 注册**订阅规则**
     （evdev_code 集合、修饰键谓词、want_suppress、input_level）；
  3. broker 对每个物理事件按注册顺序+级别做一次判定：任一客户端 suppress 则
     不回放，否则通过 uinput 虚拟键盘回放（回放设备打上固定 vendor/product id，
     参照 keyd 的 `0x0FAC`，libinput quirk 可识别）；
  4. 客户端断开（crash/kill）即刻清除其规则并 fail-open；
  5. 事件下发带 `AhkInputEvent` 全字段，`source/send_level` 由 broker 盖章
     （客户端注入也必须经 broker 的 uinput，从而获得不可伪造的 provenance——
     解 U3 的跨进程一半）。
- **实施步骤**：
  1. 把 `core_evdev_linux.cpp` 的捕获/回放逻辑提炼为 `tools/linux/inputd/`
     独立二进制（现有 udev/polkit 资产在 `tools/linux/permissions/` 复用）；
  2. 定义 wire protocol（长度前缀二进制帧，版本号开路）；
  3. `AHK_INPUT_BACKEND=evdev` 改为优先连接 broker socket，无 broker 时回退
     现有进程内 lane（保留单脚本零部署可用）；
  4. watchdog：broker 自带 `SIGALRM` 心跳，主循环卡死超 2s 自动释放全部 grab
     （现有 panic 键 `core_evdev_linux.cpp:480` 逻辑保留为最后手段）。
- **验收标准**：两个脚本同时 Hotstring/InputHook 且互不冲突；`kill -9` 任一
  客户端后 1s 内其 suppression 规则消失；`kill -9` broker 后键盘立即恢复
  （EVIOCGRAB 随 fd 关闭自动释放）。
- **M4-D 已交付（broker 守护进程）**：新增 `source/linux/inputd/inputd.c`
  独立 `ahk-inputd` 二进制（纯 C、无 X11/AHK 依赖）。单实例 flock、启动及
  每 1s 增量扫描键盘设备并 EVIOCGRAB（跳过自身回放设备，vendor 0x0FAC），
  UNIX socket 长度前缀二进制协议 v1（HELLO/SUBSCRIBE{code,suppress}/
  UNSUBSCRIBE/PING ↔ EVENT/ACK/PONG）。仲裁：任一在线客户端 want_suppress 某
  code 则抑制回放，否则经 uinput 回放；每个订阅客户端都收到事件帧。客户端
  崩溃/kill 立即清规则 fail-open；SIGALRM watchdog 卡死 2s 释放全部 grab；
  Backspace-Esc-Enter panic 保留。VM 上独立 oracle 全绿：HELLO/SUBSCRIBE
  ACK、F12 分发+回放、A 分发+抑制、kill -9 客户端后 1s 内规则消失、kill -9
  broker 后键盘立即可 grab。**未完成**：客户端（ahk_core）连接 broker 并
  消费事件帧的 M4-C、libei/InputCapture 上游未就绪的接收端。
- **M4-C 已交付（客户端接入 broker）**：新增 `core_inputd_client_linux.{h,cpp}`
  客户端：connect-first（在设备扫描前尝试，避免双路径重复处理），HELLO +
  SUBSCRIBE（从 Hotkey 表计算 EVDEV 分配键 + prefix 键码，tilde 清除
  suppress），Dispatch 消费 EVENT 帧并喂同一个 `HandleEvdevKey` 匹配器
  （combo/scXXX 全部复用），broker 断开自动回退进程内 lane。`mux` 路由、
  `LinuxInputBackendSync` 在 hotkey 变化时重推订阅。双脚本 VM oracle
  `run_inputd_client_oracle.sh` 通过：两个 ahk_core 同时连 broker、各自
  `a & b` / `c & d` 都触发且无 BadAccess；无 broker 时进程内回退场景
  （custom_combo_evdev）不回归。**尚未完成**：Hotstring/InputHook 的
  broker char_stream（X11 布局解码 + capture 喂入）与 libei/InputCapture
  上游未就绪的接收端。
- **M4-C2 已交付（broker char_stream）**：capture 激活（Hotstring/InputHook）
  且有效后端为 evdev 时，mux 激活 evdev/broker lane；订阅规则按 X11 布局枚举
  文本产生键（suppress=false，保持 M2-R 回删模型）；broker EVENT 经三层键
  模型解码喂 capture 引擎；XI2 raw 在 broker 模式下跳过 capture 防双计。
  VM oracle 通过：`:B0X*:pq` Hotstring 从 broker 分发的 uinput 事件触发。
  **纯 Wayland 无 X 布局源时 broker char_stream 不可用**（如实受限，等待
  compositor 布局接口）；libei/InputCapture 上游未就绪。
- **落点文件**：`core_inputd_client_linux.cpp`、`core_evdev_linux.cpp`、
  `core_hotkey_linux.cpp`、`input_backend.cpp`、
  `inputd/inputd_proto.h`（规则上限 1024）。
- **验收标准**：char-stream oracle 通过；X11 raw/headless/XWayland 全回归；
  scenario gate 通过。
- **修复复杂度：极高**（守护进程 + 协议 + 客户端 + char_stream 已就绪；
  纯 Wayland 布局源与 libei 留后续）

#### D. capability 结构版本化细化（解 U6）

- **设计**：`AhkInputBackendCaps` 增补字段并暴露版本：
  `scan_code`、`custom_combo`、`char_stream`（Hotstring/InputHook 可用）、
  `synthetic_provenance`（能区分注入者）、`send_level_gate`、`multi_owner`、
  `injection_unicode`。脚本侧 `HotkeyBackendGet()` 返回 Map 时带
  `caps_version` 键。
- **落点文件**：`input_backend.h:68-79`、`input_backend.cpp` 的 KindCaps 表、
  `core_mdfunc_linux.cpp` 的脚本 API、`docs-v2/docs/linux-port.htm` 矩阵。
- **验收标准**：X11 的 `scan_code=false/custom_combo=false` 如实上报（对照
  §3 完成后翻 true）；`tests/doccheck` 新增字段值断言 + 一条"行为 contract"
  断言（声明 `char_stream=true` 的 backend 必须真的能跑通一条 Hotstring）。
- **修复复杂度：低**，且应**最先做**——它是后续所有工作的诚实度量衡。

---

## 2. P0-2 X11 Hotstring/InputHook 全键 passive-grab 架构

### 2.1 缺失清单

| 编号 | 缺口 | 现状锚点 |
|---|---|---|
| G1 | 任意 Hotstring/InputHook 触发 keycode 8–255 全键 grab，第二个脚本 BadAccess | `core_capture_linux.cpp`（LinuxCaptureAddSpecs → 全键 spec）+ `core_hotkey_linux.cpp:1206` |
| G2 | 被 hold 的文本用 `XSendEvent` 转发，`send_event=True` 可被应用识别/拒收 | `core_capture_linux.cpp:177/200` |
| G3 | `sHeld[128]/sBuffer[128]` 硬上限，溢出即静默丢事件 | `core_capture_linux.cpp:50-52/563/673` |
| G4 | SendLevel 与 per-hotstring send-mode timing 未建模（注释自认） | `core_capture_linux.cpp:20` |

### 2.2 解决思路

**方向判定：X11 上存在一条不需要 grab 整键盘的正解——XI2 raw event 旁路
监听 + 仅对"确定要 suppress 的输出瞬间"做精准干预。**

#### A. 用 XI2.1 raw events 重做被动监听（解 G1 的"监听"半边）

- **设计**：Hotstring/InputHook 的**识别**只需要看键流，不需要拦截。改为
  `XISelectEvents(root, XIAllMasterDevices, XI_RawKeyPress|XI_RawKeyRelease)`
  ——raw event **即使设备被其他 client grab 也照常送达**（XI 2.1 语义，见
  [inputproto 2.1](https://lists.x.org/archives/xorg/2011-December/053680.html)），
  且不消费事件、无多客户端冲突。字符解析用 xkbcommon（§3）而不是 XLookupString。
- **代价与补偿**：raw 监听**不能**吃掉已发生的键——Hotstring 的
  auto-replace 需要"删掉已打出的 trigger 再发 replacement"。这正是 Windows
  AHK 缺省 `*0`（非即时）模式的行为：**等 end char 落地后 backspace 回删**。
  即时抑制（`*` 选项 + `b0` 以外的组合）退化为"极短窗口先出后删"，在
  capability 里如实标注 `char_stream=true, instant_suppress=false`。
- **实施步骤**：
  1. `core_capture_linux.cpp` 增加 XI2 lane：init 时 `XIQueryVersion(2,1)`
     协商（必须请求 2.1，否则 sourceid 恒 0）；
  2. 识别引擎改喂 raw 流，trigger 命中后走"N 次 Backspace + Send replacement"
     （Windows AHK 的标准 backspacing 算法可整段移植）；
  3. 现有全键 grab 路径保留为 `AHK_X11_CAPTURE=grab` 兼容开关（一个版本后默认
     切 raw，两个版本后删除）；
  4. `sHeld/sBuffer` 在 raw 模式下不再需要 hold 转发——buffer 改
     `std::wstring`，只留识别缓冲上限（对齐 Windows 的 `#Hotstring` buffer
     语义），解 G3。
- **M2-R 已交付**：X11/XWayland Hotstring 与 `InputHook("V")` 改为 XI2.1
  raw 流；Hotstring 不再安装全键 passive grabs/用 XSendEvent 伪造转发，而是
  原始事件先落地，匹配后发送精确 Backspace + replacement；C/*/O/X/B0、
  inside-word、HotIf、大小写/Unicode保留。抑制型 InputHook 暂保留兼容 grab，
  与 visible raw 路径显式分离。双进程 Hotstring、双 visible InputHook + 独立
  target/oracle 均通过，且修复 raw self-level mark 必须 FIFO 的重入次序 bug。
- **验收标准**：两个脚本各自带 Hotstring 同时运行、互不 BadAccess、都能触发
  （`tests/oracle/run_multiscript_hotstring_oracle.sh`）；应用收到的 trigger 键是**原始物理事件**
  （xterm + `xev` 断言 `send_event=False`），解 G2；对照断言：backspace 回删
  数 = trigger 长度（含 end char 处理与 `*`/`?`/`O`/`B0` 选项矩阵）。
- **风险**：回删可见闪烁——与 Windows AHK 行为一致，属可接受；密码框场景
  回删可能被应用吞掉——文档标注并提供 `#Hotstring NoBackspace` 逃生。

#### B. InputHook 的 suppress 选项分级（解 G1 的"抑制"半边）

- **设计**：`InputHook("S")`（全局抑制）在纯 raw 模式下无法实现。分级：
  - X11：`S` 选项自动升级为"raw 监听 + 动态 grab 仅 hook 存活期间"——但
    grab 范围从全键收缩为 **hook 的 EndKeys/KeyOpt 指定键集**（通常远小于全键盘）；
  - 无法收缩（`KeyOpt("{All}","S")`）时才回退全键 grab，并打运行时警告；
  - evdev/broker lane 可用时优先路由（天然支持全局抑制）。
- **落点文件**：`core_inputhook_linux.cpp`、`core_capture_linux.cpp`。
- **M2-L 已交付（X11 分级抑制）**：raw 始终观察全流，但只给实际需要
  suppression 的 canonical keycodes 安装 passive grabs；selected keys 由 grab
  喂 InputHook，raw 对其让位避免双计。`InputHook("V") + KeyOpt("{Enter}","S")`
  只 grab/suppress Enter，其他字符原始可见；运行时 KeyOpt S↔V 立即差量重建。
  默认 non-visible hook 只 grab当前 layout 能产文本的键而非 8..255，并在范围
  过大时打印收窄指引。该实现同时修复 Linux `vk_to_sc(vk,true)` 忽略
  secondary SC 导致 Enter/NumpadEnter、Delete/NumpadDel KeyOpt/EndKey 混淆。
- **验收标准**：`InputHook("V")` 零 grab；V + selected Enter S 只 grab
  Enter；独立 XGrabKey probe + target visibility + runtime reconfigure 全部通过。
- **修复复杂度：高**（已完成本体内 X11 分级；broker 路由留 M4）。

#### C. SendLevel 建模（解 G4，与 §5 联动）

- 在 §1-A 的 `AhkInputEvent.send_level` 落地后，capture 引擎按 Windows 规则
  过滤：**事件 level > hook 的 InputLevel 才可触发**；auto-replace 输出强制
  level 0（[AHK v2 SendLevel 文档](https://www.autohotkey.com/docs/v2/lib/SendLevel.htm)）。
  现有 `MinSendLevel` 判断（`core_capture_linux.cpp:541-546`)已有雏形，需把
  "本进程 self level"扩展为"事件携带 level"。

---

## 3. P0-3 关键 AHK 键盘语义缺失（scan code / custom combo / 布局）

### 3.1 缺失清单

| 编号 | 缺口 | 现状锚点 |
|---|---|---|
| K1 | `scXXX::` 注册被拒：`LinuxHotkeyKeycode()` 无 mVK 即返回 0 | `core_hotkey_linux.cpp:410-412` |
| K2 | `a & b::` 自定义前缀组合不支持（X11 backend 明确不 grab） | `core_hotkey_linux.cpp:34` 注释 |
| K3 | VK→keysym 映射硬编码 US 布局（`LinuxVkToKeysym`），shifted 字符表同样 US-only | `core_input_linux.cpp:56-140/345` |
| K4 | 无"物理 scan code → 逻辑键 → 文本"三层模型，remap/布局/IME 各自为战 | 架构级 |

### 3.2 解决思路

#### A. 定义三层键模型（K4，是 K1-K3 的公共地基）

- **设计**：
  1. **物理层 `sc`**：采用 evdev code 作为规范物理编号。AHK 的 `scXXX` 是
     Windows set-1 scan code——建立 set1↔evdev 静态映射表（内核
     `atkbd.c`/USB HID usage 表是权威来源，两者一一对应关系稳定）；X11 下
     `keycode = evdev_code + 8`（evdev ruleset 恒等式，xkb 的事实标准，见
     [wayland-devel 讨论](https://lists.x.org/archives/wayland-devel/2021-December/042056.html)）。
  2. **逻辑层 `vk`**：保留现有 Win32 VK；由 `sc + 当前布局` 推导，替换
     硬编码表——用 xkbcommon：`xkb_state_key_get_one_sym()` 取 keysym 再映射
     VK，布局无关键（F 键、修饰键、导航键）走静态表。
  3. **文本层 `text`**：`xkb_state_key_get_utf32()`（自动处理 Shift/AltGr/
     dead key 组合，见 [xkbcommon state API](https://xkbcommon.org/doc/current/group__state.html)）。
- **实施步骤**：
  1. 新建 `core_keymodel_linux.{h,cpp}`：持有 `xkb_context/keymap/state`；
     X11 从服务器取 keymap（`xkb_x11_keymap_new_from_device`），Wayland/evdev
     从 `wl_keyboard.keymap` fd 或系统 RMLVO 构建；监听 MappingNotify/
     layout-change 重建；
  2. `LinuxVkToKeysym/LinuxKeycodeForVk`（`core_input_linux.cpp:56-151`）改为
     查询 keymodel；发送方向"字符→(keycode,mods)"用
     `xkb_keymap_key_get_mods_for_level()` 反查（参照 `xkbcli how-to-type`
     的实现），替换 US shifted 硬编码表（K3）；
  3. `LinuxHotkeyKeycode()` 增加分支：`mSC` 存在时 `evdev = set1_to_evdev(mSC)`，
     X11 keycode = evdev+8，evdev lane 直接用（K1）；
  4. capability `scan_code` 翻 true（X11/evdev），portal/GNOME 保持 false。
- **M1-K 已交付（键模型与 scan code 地基）**：新增
  `core_keymodel_linux.*`，通过 xkbcommon-x11 读取服务器 active keymap/state，
  统一 evdev/set-1 SC、VK/keysym、UTF-32；X11/evdev explicit `scXXX` 已接入，
  InputHook callback 返回 canonical VK/SC，Send 对当前 layout 反查 none/Shift/
  AltGr/Shift+AltGr 的最小组合。外部 oracle 在受版本控制的 <=255-keycode
  AZERTY/AltGr fixture 上证明 `sc01E` 跨 layout 仍跟物理键、`SendText("€")`
  被独立 X11 client 读为 EuroSign + Mod5；GNOME VM uinput 也以 `sc01E` 触发。
  **未冒充完成**：X11 custom combo 仍 false；Hotstring 改 raw XI2 属 M2。另发现并
  消除旧 `setxkbmap` 测试假阳性：现代 xkeyboard-config 最大 keycode 708，
  X11 仅支持 255，xkbcomp 会 clipping 后留下 US map。
- **验收标准**：差分断言（见 §8）——`sc01E::` 在 US 与 AZERTY 布局下都触发
  同一物理键位；AltGr-only Unicode 由外部 client 读回；后续 M2 再补
  Hotstring layout 字符流。
- **风险**：xkbcommon 反查存在多解（同一字符多个键位/层级），需固定
  "最少修饰键优先"策略并与 Windows `VkKeyScan` 行为对照。

#### B. 自定义组合 `a & b`（K2）

- **设计**：custom combo 本质是"前缀键按下状态机 + 前缀键自身延迟判定
  （tap 出原键 / hold 作修饰）"。X11 路径：前缀键单独 grab（1 个键，而非全键盘），
  press 后进入 pending：后续 b 命中→触发组合并吞掉两者；超时/前缀 release→
  用 XTEST 补发前缀键（等价 Windows 行为：无 `~` 时前缀键默认丢失 tap，需
  对齐 `&` 的 ~/tap 语义矩阵）。evdev/broker 路径：状态机放 broker 判定更稳
  （CapsLock 双角色场景已有 `caps_dual_role` 场景可扩展）。
- **M1-C 第一层已交付（evdev）**：进程内 evdev lane 已实现 prefix down/up
  状态、custom combo 默认 wildcard、suffix key-up、prefix/suffix tilde、非标准
  prefix 默认抑制、标准 modifier/toggle 保留 native，以及“prefix 未用于组合时
  standalone hotkey 延迟到 release；用于组合则取消”语义；VK/SC prefix/suffix
  共用三层键模型。独立 uinput 设备矩阵逐项验收。`custom_combo=true` 只对 evdev
  上报；X11 仍 false/明确拒绝，待 M2 XI2 raw 流后实现，避免 passive-grab 假支持。
- **落点文件**：`core_hotkey_linux.cpp`（注册能力校验）、
  `core_evdev_linux.cpp`（状态机）；`hotkey.cpp` 上游解析已有
  `mModifierVK/mModifierSC`，未修改。
- **验收标准**：`CapsLock & j::Left` + 单独 tap CapsLock 保持切换大小写
  （加 `~CapsLock`）；`a & b::` 时快速打 "ab" 文本不误触（timing 阈值对照
  Windows 实测）。
- **修复复杂度：高**（K4 地基 + 状态机），建议顺序：A → B。

---

## 4. P0-4 原生 Wayland 缺少 Hotstring/InputHook 连续输入流

### 4.1 现实校准（哪些路是真的存在）

| 通道 | 能否给连续键流/字符流 | 现状（exa 复核 2026-08） |
|---|---|---|
| GlobalShortcuts portal | 否，只有注册键的 Activated/Deactivated | 平台定论，不再指望 |
| InputCapture portal + libei receiver | **能给完整键流**，但触发器只有 PointerBarrier（屏幕边缘），无"按需捕获"触发器 | [xdg-desktop-portal#714](https://github.com/flatpak/xdg-desktop-portal/pull/714) 中维护者明确：按需触发"in scope but not there" |
| RemoteDesktop portal + libei sender | 注入方向，非捕获 | 已稳定（1.17+） |
| evdev/EVIOCGRAB（→ §1-C broker） | 能，需权限 | 项目已有 lane |
| zwp_input_method_v2 `grab_keyboard` | **能拿全部键盘事件**（IME 专用独占 grab） | wlroots 系支持；一个 seat 只允许一个 IME，会与 fcitx/ibus 互斥（[wlroots 实现](https://wlroots.pages.freedesktop.org/wlroots/wlr/types/wlr_input_method_v2.h.html)） |
| GNOME extension | 理论可在 shell 内挂 key handler，但 GJS 层拿不到全局键流的稳定 API | 维持 shortcut-only 定位 |

### 4.2 解决思路

- **产品分层（立即做，文档+capability）**：
  1. 原生 Wayland 的 Hotstring/InputHook **唯一通用解是 evdev/broker lane**；
     capability `char_stream` 只在 evdev/broker 可用时为 true；
  2. 脚本含 Hotstring/InputHook 且会话为原生 Wayland、无 evdev 权限时，启动
     即给出**一条可操作的错误**（指向 `tools/linux/permissions/` 安装脚本），
     而不是静默不触发——把 P0-4 从"隐性坑"变成"显式权限决定"。
- **中期（跟踪上游，不自研）**：
  1. 在 xdg-desktop-portal 跟进/推动 InputCapture 的"application-triggered
     capture"触发器（上游讨论已认可 in-scope）；落地后 AHK 侧实现 libei
     receiver（`ei_setup_backend_fd()` 接 `InputCapture.ConnectToEIS`），
     即获得**免 root、compositor 授权**的键流——那才是 GNOME/KDE 上的终局方案；
  2. libei receiver 的事件归一化直接进 §1-A 的 `AhkInputEvent`（字段已预留
     `origin_backend`）。
- **不做**：不实现 `zwp_input_method_v2 grab_keyboard` 作为 Hotstring 通道——
  它与用户真实 IME 互斥（一个 seat 一个 IME），中文用户会直接失去输入法，
  与 §6 目标冲突。仅在文档中说明该路线被评估并否决的原因。
- **验收标准**：`--diag` 在 GNOME Wayland 无权限环境明确输出
  `char_stream: unavailable (need inputd / evdev permission)`；有 broker 时
  GNOME Wayland 跑通 `:*:btw::by the way`（现有 GNOME VM 场景扩展）。
- **修复复杂度：极高**（终局依赖上游），但"诚实分层 + evdev 引导"部分为**中**。

---

## 5. P0-5 SendLevel / InputLevel / synthetic-event 语义

### 5.1 缺失清单

| 编号 | 缺口 | 现状锚点 |
|---|---|---|
| L1 | 显式 `SendInput()` 自抑制，`SendMode("Input")+Send()` 不自抑制——同语义调用行为分叉 | `core_input_linux.cpp:435-451/474-476/1333` |
| L2 | self-track 是进程本地 keycode 日志，跨脚本无 provenance | `core_input_linux.cpp:488` |
| L3 | Hotstring auto-replace 输出未强制 level 0 | `core_capture_linux.cpp:20` |
| L4 | 无跨脚本 level 差分测试 | tests 全同进程 |

### 5.2 解决思路

- **对齐 Windows 规则（先写成规范文件再改码）**：Windows 语义只有一条主规则
  ——"**事件 send level > 接收 hook 的 input level 才触发**；默认都是 0 所以
  脚本自发事件默认不触发任何 hook 热键"（[SendLevel 文档](https://www.autohotkey.com/docs/v2/lib/SendLevel.htm)）。
  `SendInput` 的"卸钩"是 Windows 实现细节，效果上等价于"该批事件对本进程
  hook 不可见"。因此：
  1. **L1 修复**：`LinuxModeSuppressSelf()` 判定改为只看解析后的
     `sendlevel + mode`，`SendMode("Input")+Send()` 与 `SendInput()` 必须走
     同一分支（删除 `aExplicitSendInput` 双轨，`core_input_linux.cpp:444` 签名收敛）;
     Windows 上两者仅剩的差异（速度/中断性）在 Linux XTEST 上本就不存在，注释说明；
  2. **L3 修复**：capture 引擎触发 auto-replace 时把发送线程 level 强制 0
     （对照文档"Auto-replace hotstrings always generate keystrokes at level 0"）；
     非 auto-replace hotstring 线程初始 level = 其 InputLevel；
  3. **L2 分阶段**：进程内先用 §1-A 的 `send_level` 字段贯通（X11 注入无法带
     元数据，维持 keycode+时间窗启发式但把窗口收紧并计数漏配）；跨进程 provenance
     只有 §1-C broker 或 libei（compositor 视角天然区分 client）能根治——在
     capability `synthetic_provenance` 里如实区分 `heuristic/authoritative`；
  4. **L4 补测**：两进程场景——脚本 A `SendLevel 1` 发 "btw"，脚本 B
     `#InputLevel 0` 的 `::btw::` 必须触发；A level 0 时必须不触发。X11 下该
     测试当前会因启发式误差不稳定→标 known-fail 并作为 broker 验收 gate。
- **落点文件**：`core_input_linux.cpp`、`core_capture_linux.cpp`、
  `tests/scenarios/sendlevel_cross/`（新建）。
- **修复复杂度**：L1/L3 **中**；L2 完整解**极高**（同 §1-C）。

---

# 第二部分 P1：严重兼容性 / 可靠性问题

## 6. P1-1 Unicode 与 IME（含中文）

### 6.1 缺失清单

| 编号 | 缺口 | 现状锚点 |
|---|---|---|
| I1 | X11 非 ASCII 靠借 keycode 改**服务器全局** keymap + 固定 30ms 等待，自认有 MappingNotify race | `core_input_linux.cpp:808-858` |
| I2 | Wayland 兜底接管剪贴板粘贴，可能污染剪贴板/被 paste policy 拒绝 | `core_input_linux.cpp:885-923` |
| I3 | IME 状态查询只有 D-Bus owner + XKB group 近似 | `core_ime_linux.cpp` |
| I4 | 无 preedit/commit 流，中文 Hotstring/InputHook 缓冲区语义未定义 | 架构级 |

### 6.2 解决思路

#### A. X11 Unicode 注入改造（I1）

- 优先级顺序改为：
  1. **当前布局可直接产生的字符**（xkbcommon 反查，§3-A 已建）→ 普通 XTEST，
     覆盖绝大多数拉丁扩展字符，keymap 借用需求大幅下降；
  2. 仍需借 keycode 时：把"固定 30ms"改为**事件驱动**——发 `XChangeKeyboardMapping`
     后等自己收到对应 MappingNotify 再注入（XSync 保证服务器已应用；race 从
     概率性变确定性），用后立即还原并再等一次 MappingNotify；连续多字符批量
     共享一次借用窗口（现有代码已有批量意识，收紧等待即可）；
  3. 检测到 IBus/Fcitx 活跃且目标聊天类应用时提供 `A_SendUnicodeMode=paste|keymap|auto`。
- **验收**：`Send "你好😀"`（BMP 外 emoji 走 surrogate 检查）在 Xvfb +
  gedit AT-SPI 读回一致；并发 `xmodmap` 监听器不崩。

#### B. IME 集成（I3/I4，中文可用性的核心）

- **设计（监听优先，注入次之）**：
  1. **IBus**：连接 IBus 私有总线（`IBUS_ADDRESS`），监听全局
    `commit-text`/`update-preedit-text` 信号（[IBusInputContext 信号集](http://ibus.github.io/docs/ibus-1.4/IBusInputContext.html)）。
    拿到 commit 文本后：Hotstring buffer 按**已提交文本**追加（而不是拼音
    击键），preedit 期间冻结 buffer——这正是 Windows AHK 在 IME 下的近似行为；
  2. **Fcitx5**：同理走 `org.fcitx.Fcitx5` 的 InputContext1 `CommitString`
    信号（[Fcitx5 DBus Frontend](https://deepwiki.com/fcitx/fcitx5/4.3-dbus-frontend)）；
    `Controller1.CurrentInputMethod` 提供 `A_IME` 类查询（[DBus 接口 wiki](https://fcitx-im.org/wiki/DBus_Interface)）；
  3. InputHook 语义定案并写文档：物理键流照常喂 `KeyOpt`/EndKeys 判定，
    **字符流以 IME commit 为准**（Windows 行为的最合理映射）；`SendText`
    绕过 IME（XTEST 直注 keysym / broker uinput），文档标注差异；
  4. `core_ime_linux.cpp` 的 owner 探测保留为 fallback，新增
    `ImeStatus()` 富查询（engine 名、preedit 活跃布尔）。
- **落点文件**：`core_ime_linux.cpp` 重写、`core_capture_linux.cpp`
  （buffer 冻结/追加钩子）、新 `tests/scenarios/ime_hotstring_zh/`。
- **验收标准**：GNOME VM + IBus 拼音：输入 "nihao" + 空格提交"你好"后，
  `::你好::hello` 触发；preedit 中途 Esc 不污染 buffer；Backspace 在 preedit
  内不回退 buffer。
- **风险**：GTK/Qt 应用走各自 IM module 时 IBus 全局信号仍可见（ibus-daemon
  中转），但 Flatpak 沙箱内应用走 portal IM 时不可见——capability 标注。
- **修复复杂度：高。**

## 7. P1-4 X11 grab 冲突记录与全局 error handler（含 P1-2/P1-5 的调用纪律）

### 7.1 残余缺口（0 节已核实修复了检出路径）

| 编号 | 缺口 | 现状锚点 |
|---|---|---|
| E1 | BadAccess 后失败 spec 仍进 `sInstalled`，冲突方退出后永不重试 | `core_hotkey_linux.cpp:1283-1284`（`pending` 未按 `BadSerial()` 剔除） |
| E2 | `ScopedXErrorTrap` 临时覆盖进程级 `XSetErrorHandler`，与 GTK 并发不安全 | `core_hotkey_linux.cpp:400` 附近 |
| E3 | 只捕获**第一个** BadAccess（trap 存单个 code/serial），同批多个冲突漏记 | 同上 |

### 7.2 解决思路

- **E1（小修，立即做）**：`:1283` 循环插入前跳过 `pending[i].serial ==
  trap.BadSerial()` 的 spec；再加**定期重试**：冲突 spec 进 `sConflicted`
  集合，每次 `LinuxReconcileHotkeyGrabs()`（已由 hotkey 状态变化触发）或
  60s 定时重试一次，成功则清除并日志告知。
- **E3**：`ScopedXErrorTrap` 的存储从单值改 `std::vector<XErrorEvent>`，
  逐条对 serial 匹配 pending；同批 N 个冲突全部剔除并在错误消息里列全。
- **E2（纪律性修复）**：
  1. 项目所有 Xlib 调用收敛到主线程（现状基本如此，加断言
     `assert(main_thread)` 于 helper 入口固化）；
  2. error handler 在 init 时**安装一次**全局 dispatcher，内部查
     "当前活跃 trap 栈"（线程局部），不再运行时换装——GTK 的 handler 被
     我们永久接管，但 dispatcher 对非 trap 期间的错误转发给保存的前任
     handler，行为向后兼容。
- **验收标准**：场景 `multiscript_conflict` 扩展：holder 退出后 30s 内
  second 进程的热键**自动生效**（现在需要重启脚本）；ASan + 两进程反复
  注册/退出 100 轮无 crash。
- **修复复杂度：中。**

## 8. P1-6 测试体系：外部 oracle 与差分验证

### 8.1 缺失清单

| 编号 | 缺口 | 现状锚点 |
|---|---|---|
| T1 | sender/receiver 同源（自验证闭环） | `tests/doccheck/assert_hotkey.ahk` 等 |
| T2 | 无 Windows AHK v2.0.26 行为差分 oracle | 无 |
| T3 | headless sway/Xvfb ≠ GNOME/KDE 真桌面；skip 不 gate | `tests/scenarios/run_scenarios.sh` |
| T4 | soak 秒级；无 TSan/fuzz/hotplug/compositor restart | `.github/workflows/ci.yml` |
| T5 | 弱 oracle（如 SNI 场景匹配任意 StatusNotifierItem 字样） | `tests/scenarios/tray_sni/` |

### 8.2 解决思路

- **T1 外部注入器/记录器（性价比最高，先做）**：
  **M1-O 已交付第一层**：`tests/oracle/input_oracle.c` 是与 ahk_core 无共享
  代码的 XI2.1 JSONL recorder + XTEST injector；CI 双向验证“AHK Send → 外部
  down/up/sourceid/XTEST trace”和“外部 injector → AHK hotkey”。trace schema、
  verifier 与摘要作为 CI artifact；VM 物理层复用 `tools/linux/uinput-inject.c`
  （设备名刻意不含 AHK）和 evdev 场景。后续 Windows golden trace 仍属 T2，
  不因本层完成而冒充跨平台差分完成。
  1. 注入侧：测试容器里用 `uinput` 写一个 20 行的独立 C 注入器（或直接用
     `ydotool`/`wtype`），物理层面产生输入——AHK 的捕获栈从 X 服务器视角看到
     的是真键盘事件，打破"同一进程同一套错误约定"；
  2. 记录侧：独立 `evtest`/XI2 recorder 进程校验 AHK `Send` 产出的事件序列
     （键序、press/release 配对、时序），Control* 类断言改为读**目标应用**
     的 AT-SPI 状态而不是 AHK 自己的返回值。
- **T2 Windows 差分 trace suite**：
  1. 定义 trace 格式（JSONL：input op → 观察到的事件/状态序列）；
  2. 一台 Windows VM 上跑上游 AHK v2.0.26 + 同格式 recorder（AHK 脚本自身
     可产 trace：hook 回调记录 vk/sc/level/flags），生成**黄金 trace 库**入库
     `tests/differential/golden/`；
  3. Linux 侧同 harness 回放并 diff（字段白名单允许平台性差异：时间戳、
     device id）；纳入 CI 为非阻塞报告，成熟后按模块转 gate。
  4. 覆盖顺序按用户价值：Send 键序 → hotkey 触发矩阵（修饰/wildcard/up/~）→
     hotstring 选项矩阵 → InputHook → remap。
- **T3 真桌面矩阵**：GitHub runner 支持 KVM 嵌套的 job 跑 GNOME/KDE cloud
  image（项目已有 `vmctl.py` 资产）；每夜跑，白天 CI 维持 headless。skip
  改为**显式预算**：`SUPPORT_MATRIX.md` 声明每场景"必须运行的环境集合"，
  环境可用却 skip 视为失败。
- **T4 可靠性工况**：加 `-fsanitize=thread` 构建矩阵项（先只跑输入子集）；
  24h soak 移到夜跑 VM（每 5 分钟一轮 hotkey/hotstring/clipboard 混合负载，
  RSS 斜率 gate）；故障注入场景：`kill -STOP` compositor 3s、重启
  xdg-desktop-portal、evdev 设备 add/remove（`uinput` 动态建删）。
- **T5 oracle 收紧纪律**：场景 PASS 判据必须绑定**本进程身份**（bus name、
  PID、object path）；`run_scenarios.sh` 增加 lint：禁止裸 `grep` 通配判 PASS。
- **修复复杂度：中高**（T2 需要 Windows 基建，但一次投入长期复利）。

## 9. P1-2 / P1-5 / P2-1 Win*/Control*/AT-SPI 收敛（合并处理）

三者共享同一根因——"X11 窗口模型简化 + 虚拟状态冒充 + 同步 D-Bus"，合并为
一个工作流：

### A. X11 窗口枚举改 EWMH 优先（P1-2）

- **M5-A 已交付**：`LinuxEnumTopWindows` 优先读 root 的 `_NET_CLIENT_LIST`
  （EWMH，WM 维护的真实 client 集合，Z 序语义留给 `_NET_CLIENT_LIST_STACKING`
  后续）；属性缺失（无 WM 的 Xvfb）回退 XQueryTree + ICCCM `WM_STATE` 子树
  探测，且当 WM_STATE 过滤结果为空时**回退裸枚举**（保护无 WM_STATE 的
  未管理测试窗口，保持既有 1160 断言）。独立 oracle `x11_ewmh.c` +
  `run_ewmh_oracle.sh` 双路径验证：EWMH 模式下 B 被排除（a=1 b=0）、回退
  模式两者可见（a=1 b=1）；Xvfb 全量 doc-check 无新增失败。
- **落点文件**：`core_win_linux.cpp`（LinuxEnumTopWindows +
  WM_STATE 探测）、`tests/oracle/x11_ewmh.c`、`run_ewmh_oracle.sh`。
- **验收**：`_NET_CLIENT_LIST` 命中即用（oracle 证明）；无 WM 回退不回归
  （CI doc-check）；GNOME Xorg 实机对照留待真桌面 job（M6）。

### B. Control* 虚拟状态退场（P1-2）

- 原则改为"**真实效果或明确报错**"：
  1. 能通过 AT-SPI action/text/value 接口真实完成的（点击、设文本、读文本、
     选择列表项）→ 走 AT-SPI；
  2. 不能的（`ControlAddItem` 到外部应用、style 位操作）→ 抛
     `Error("NotSupported on Linux", -1)`，**删除** `core_ctrl_linux.cpp` 的
     shadow map get/set 通路（`:751-779` 等）；本项目自建 GTK GUI 的控件走
     GUI 对象内部通道不受影响；
  3. `ControlSend` 的"抢焦点"实现保留但更名语义：文档明确它等价
     `WinActivate + Send + 恢复`，并新增 `A_ControlSendMode=atspi|focus`——
     atspi 模式对支持 `Action`/`Text` 接口的控件免焦点操作。
- **验收**：对外部 GTK 应用 `ControlAddItem` 报 NotSupported 而不是虚假成功；
  `ControlClick` 在 Firefox（AT-SPI）后台窗口生效。

### C. AT-SPI 层重做：Cache 批量 + 异步 + UTF-8（P1-5、P2-1）

1. **UTF-8 修复（确定性 bug，立即）**：`core_ctrl_linux.cpp:97/445` 的
   逐字节 `<0x80?:'?'` 替换为完整 UTF-8→UTF-32 解码（项目内已有转换工具函数
   可复用），中文控件文本即刻可读；
2. **树遍历改批量**：逐节点 `GetChildren` 改为
   `org.a11y.atspi.Cache.GetItems`——一次往返拿全树（role/name/states），
   大应用的耗时从"节点数 × RTT"降为"单条大回复"（[Cache 接口](https://documentation.ubuntu.com/desktop/en/latest/reference/accessibility/dbus/org.a11y.atspi.Cache/)；
   [waydriver 实测同一结论](https://docs.rs/waydriver/latest/waydriver/atspi/fn.snapshot_tree_from_cache.html)）；
   需要 bounds 时对命中节点单独补 `Component.GetExtents`；
3. **异步化 + 预算**：所有 AT-SPI D-Bus 调用改 pending-call + 主循环集成，
   单调用 500ms、单查询总 2s 预算，超时返回部分结果 + `A_LastError`；
   防御 LibreOffice Calc 式 2^31 children（[上游已知脚枪](https://lists.freedesktop.org/archives/libreoffice/2024-June/092049.html)）
   ——子节点数超阈值即剪枝；
4. **WinTitle 限定**：先解析 WinTitle→PID/frame，再在对应 application
   accessible 子树内找 Control（`core_ctrl_linux.cpp:425-445` 现在忽略
   WinTitle），消除同名控件跨应用误命中；
5. **会话误判修复**：`LinuxCtrlSessionIsWayland()`（`core_ctrl_linux.cpp:52`）
   改为只信 `XDG_SESSION_TYPE`/`WAYLAND_DISPLAY` 组合，去掉按桌面名猜测。
- **修复复杂度**：C1/C5 **低**；A、B、C2-C4 **中高**。

## 10. P1-3 evdev 设备级 grab 的安全化

（依赖 §1-C broker，此处列 broker 前的独立加固）

1. **watchdog fail-open**：捕获线程心跳；主线程/回调阻塞超 2s，自动
   `EVIOCGRAB 0` 全部设备（恢复后可重新 grab）——把"脚本写了死循环"从
   "键盘全锁"降级为"热键暂时失效"；
2. **panic 键升级**：现有 Backspace→Escape→Enter（`core_evdev_linux.cpp:56`）
   保留，另加连按 Esc×5 的备用序列并在 grab 激活时打印两种逃生提示；
3. **权限引导**：`--diag` 检测 `/dev/input` 组权限与 udev 规则安装状态，
   给出发行版对应命令（`tools/linux/permissions/` 已有资产，补 CLI 出口）；
4. **回放顺序保证**：uinput 回放与抑制判定在同一线程串行执行，禁止 press
   回放越过在前的 release（现有 `:418` 回放路径加序列断言 + 单元测试）。
- **验收**：`kill -9` 脚本进程后（fd 自动关闭）键盘 <100ms 恢复；
  SIGSTOP 脚本 3s，watchdog 释放 grab，键盘可用。
- **修复复杂度：中。**

---

# 第三部分 P2/P3：重要但不阻塞核心

## 11. P2-2 GUI 静默弱化

1. 选项处理三分类落码：`supported / mapped(语义近似，warning) /
   unsupported(默认抛 ValueError)`；`#Warn GuiOptions off` 可降级为 warning
   ——默认行为从"静默跑偏"改为"早失败"；
2. `ActiveX` 控件：检测到即抛 `Error("ActiveX is not available on Linux")`，
   提供 `WebView` 控件作为显式替代（GTK WebKit2，独立 feature，不冒名）；
3. 桌面矩阵验证并入 §8-T3 夜跑（GNOME/KDE 下 GUI 截图对比断言，HiDPI
   `GDK_SCALE=2` 变体）。
- **修复复杂度：中。**

## 12. P2-3 DllCall/COM 的诚实化

1. 文档与运行时双标注：`DllCall` 检测 `.dll`/`user32\` 类调用→
   `Error("Windows DLL not available; this is Linux-native FFI", extra: 迁移指引)`；
   现有 lib*.so 启发式改写保留但打 `A_DllCallRewrite` 可查标志；
2. `ComObject("Excel.Application")` 等已知 Windows ProgID 前缀→专用错误
   （"COM automation 无 Linux 等价物；D-Bus proxy 用法见 docs"）；
3. **P1-5 联动**：`core_com_dbus_linux.cpp:475` 的 `-1` 超时改默认 25s
   （D-Bus 惯例）+ `ComDBusTimeout()` 可配，并给出 async 变体；
4. `Str` ABI 差异、参数上限写入 parity.tsv 与 linux-port.htm。
- **M0-C 实施状态**：Windows DLL/ProgID 的诚实错误与 Str/AStr/WStr、64
  参数上限的 parity/doc 口径已落地并有 4 条断言；不再存在 `.dll` 静默改写，
  因而未新增只为旧改写存在的 `A_DllCallRewrite`。第 3 项（25s 可配超时 +
  async 变体）未冒充完成，留在 M5 AT-SPI/D-Bus 异步预算批次。
- **修复复杂度：低-中。**

## 13. P2-4 发布信任链

1. **主路径**：CI 对全部 dist 产物出证（公共仓库免费，Sigstore 公共实例 +
   透明日志，SLSA Build L2；[GitHub Artifact Attestations](https://docs.github.com/en/actions/concepts/security/artifact-attestations)）。
   **M0-C 已实施**：按当前上游接口使用 `actions/attest@v4`（旧
   `attest-build-provenance` 自 v4 起只是 wrapper，新实现官方建议直接用
   `actions/attest`）；仅 release tag 或显式 workflow_dispatch 产生证明，随后
   用 `gh attestation verify` 对每个包和 CKSUMS 反向验收；
   README 提供 `gh attestation verify <file> --repo MonoEven/Autohotkey_Linux`
   验证命令；
2. **GPG 修正（M0-C 已实施）**：`pack.sh`/`pack-finalize.sh` 已删除
   “无密钥则现场生成”分支；无 `AHK_RELEASE_SIGNING_KEY` 时输出
   `UNSIGNED.txt`，不生成签名或随包公钥。有密钥时还必须提供
   `AHK_RELEASE_SIGNING_FINGERPRINT` 且导入结果精确匹配，否则构建失败；
   `SECURITY.md` 明示尚未配置长期 key/fingerprint，不能把历史随包公钥视为
   独立信任锚；
3. 包验收脚本增加"验证 attestation 存在"的 gate。
- **修复复杂度：低。**

## 14. P2-5 Clipboard 类型模型与跨桌面监听

1. **MIME 数据对象**：内部剪贴板模型改 `map<mime, bytes>`；`A_Clipboard`
   仍映射 text/plain;charset=utf-8，`ClipboardAll()` 序列化全部 MIME 对
   （X11 targets ↔ MIME 双向表）；
2. **监听矩阵**：X11 XFixes（已有）；GNOME 扩展 Meta.Selection（已有）；
   **KDE/wlroots 系补 `ext-data-control-v1` 监听 backend**——KWin 6.6/Sway/
   Hyprland/niri/COSMIC 均已支持（[wayland.app 矩阵](https://wayland.app/protocols/ext-data-control-v1)），
   vendored 协议 XML 进 `source/linux/wayland/protocols/`（项目已有同类
   vendoring 先例），老 compositor 回退 `zwlr_data_control`；Mutter 两者皆无
   →维持扩展路径；
3. 能力如实上报：`OnClipboardChange` 注册时若当前桌面无任何监听通道，抛错
   并提示（而非静默不触发）；
4. type 判定：owner 存在时按 targets 是否含 image/*、text/uri-list 分类
   （替换"近似 text"）。
- **修复复杂度：中高**（监听 backend 是主要工作量）。

## 15. P3-1 capability/注释/文档漂移

1. §1-D 的版本化 caps 落地后，`docs-v2/docs/linux-port.htm` 矩阵由
   `tools/linux/gen-capability-doc.sh` 从 KindCaps 表**生成**，禁手写；
2. 移除代码注释中把 `check0820/check_detail0821` 当设计规范的引用（审计文档
   是快照不是 spec）——设计依据迁到 `source/linux/README.md` 常青章节；
3. CI 校验：`verify_report_numbers`（已有）扩展为同时校验 caps 文档与
   KindCaps 表一致。
- **修复复杂度：低。**

---

# 第四部分 执行顺序与依赖图

沿用 check0824 第十节的优先级，落成可执行的里程碑序列（→ 表示依赖）：

```
M0 诚实化（1 周量级，全部低复杂度，立即可做）
   §1-D caps 细化 / §9-C1 UTF-8 修复 / §9-C5 会话判定 / §7-E1 冲突剔除
   §12 DllCall/COM 报错 / §13 attestation / §15 文档生成
M1 键模型 + 测试地基（并行两条线）
   §3-A xkbcommon 三层模型            §8-T1 外部注入/记录器
   §3-B custom combo → 依赖 §3-A      §8-T2 Windows 黄金 trace（基建）
M2 X11 捕获重构
   §2-A XI2.1 raw 监听 → 依赖 §3-A（字符解析）+ §1-A（事件结构）
   §2-B InputHook 分级 / §5-L1/L3 SendLevel 对齐
M3 统一事件 + 多路复用
   §1-A AhkInputEvent → §1-B InputMux → 依赖 M1/M2 落地字段
M4 broker 与 Wayland 终局
   §1-C ahk-inputd → 解 §4（evdev 通用捕获）+ §5-L2 + §10
   §4 libei receiver（跟踪上游 InputCapture 触发器进度）
M5 Win/Control/AT-SPI 收敛 + IME
   §9-A/B/C（可与 M2-M4 并行）        §6-B IBus/Fcitx 集成
M6 真桌面可靠性实验室
   §8-T3/T4 夜跑矩阵、24h soak、TSan、故障注入（随各里程碑逐步充实）
```

**首要提醒（与 check0824 结论一致）**：M1 的 §8-T1/T2（oracle）优先于任何
新 backend——没有外部 oracle，后续每一项重构都无法证明"变得更兼容"而不是
"变得不同"。

---

# 附录 A：外部技术情报索引

| 主题 | 结论 | 来源 |
|---|---|---|
| libei/EIS/liboeffis | RemoteDesktop portal `ConnectToEIS` 拿 EIS fd，libei 1.x 稳定；liboeffis 封装 D-Bus 会话 | [libei API](https://libinput.pages.freedesktop.org/libei/api/index.html)、[xdg-desktop-portal#762](https://github.com/flatpak/xdg-desktop-portal/pull/762) |
| Xwayland XTEST 桥接 | Xwayland 23.2+ 自动把 XTEST 转 libei 经 portal 授权，X 客户端无感 | [Who-T 2026-07](http://who-t.blogspot.com/2026/07/libei-integrations-in-xdg-remotedesktop.html) |
| InputCapture portal | 键流经 EIS 下发，但触发器目前仅 PointerBarrier，无按需捕获；上游认可扩展方向 | [xdg-desktop-portal#714](https://github.com/flatpak/xdg-desktop-portal/pull/714)、[portal 文档](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.InputCapture.html) |
| XI2 raw + sourceid | XI 2.1 起 raw event 全时送达（即使被 grab）且带 sourceid；2.0 sourceid 恒 0 | [inputproto 2.1 patch](https://lists.x.org/pipermail/xorg-devel/2011-August/024396.html) |
| xkbcommon | `xkb_state_key_get_utf32/get_one_sym` 做布局感知解析；`how-to-type` 展示 keysym→(keycode,mods) 反查 | [state API](https://xkbcommon.org/doc/current/group__state.html)、[wayland-devel](https://lists.x.org/archives/wayland-devel/2021-December/042056.html) |
| keyd 参考架构 | evdev grab + uinput 虚拟设备 + IPC；固定 vendor id 便于 libinput quirk | [rvaiya/keyd](https://github.com/rvaiya/keyd) |
| EWMH/ICCCM 枚举 | `_NET_CLIENT_LIST` 优先，WM_STATE 递归回退，虚拟根窗口注意 `_NET_VIRTUAL_ROOTS` | [EWMH 规范](https://specifications.freedesktop.org/wm/latest/ar01s03.html) |
| AT-SPI Cache | `Cache.GetItems` 批量取树 + AddAccessible/RemoveAccessible 增量维护 | [org.a11y.atspi.Cache](https://documentation.ubuntu.com/desktop/en/latest/reference/accessibility/dbus/org.a11y.atspi.Cache/) |
| IBus 信号 | InputContext `commit-text`/`update-preedit-text` 可订阅 | [IBusInputContext](http://ibus.github.io/docs/ibus-1.4/IBusInputContext.html) |
| Fcitx5 D-Bus | `Controller1.CurrentInputMethod`、InputContext1 `CommitString` | [Fcitx wiki](https://fcitx-im.org/wiki/DBus_Interface)、[DeepWiki](https://deepwiki.com/fcitx/fcitx5/4.3-dbus-frontend) |
| SendLevel 规则 | 事件 level > hook InputLevel 才触发；auto-replace 恒 level 0；hotstring buffer 收集除 0 外所有 level | [AHK v2 SendLevel](https://www.autohotkey.com/docs/v2/lib/SendLevel.htm)、[#InputLevel](https://www.autohotkey.com/docs/v2/lib/_InputLevel.htm) |
| zwp_input_method_v2 | `grab_keyboard` 可拿全键流但一个 seat 仅一个 IME，与真实输入法互斥——否决 | [wlroots 文档](https://wlroots.pages.freedesktop.org/wlroots/wlr/types/wlr_input_method_v2.h.html) |
| ext-data-control-v1 | KWin 6.6/Sway/Hyprland/niri/COSMIC/labwc/Mir 支持；Mutter/Weston 无；wlr 变体已弃用 | [wayland.app](https://wayland.app/protocols/ext-data-control-v1)、[wl-clipboard 提交](https://github.com/bugaevc/wl-clipboard/commit/091d6028b5c9db75ad36f9fceb0db3ee718045fa) |
| 发布证明 | GitHub Artifact Attestations（Sigstore、透明日志、SLSA L2 起步），`gh attestation verify` 消费 | [GitHub 文档](https://docs.github.com/en/actions/concepts/security/artifact-attestations) |
