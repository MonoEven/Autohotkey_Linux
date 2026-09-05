## 结论

**这个仓库已经不是“只能跑几个示例”的概念验证，但距离“普通用户装上就能长期替代 Windows AutoHotkey”仍有明显差距。最大的缺口不是函数数量，而是输入故障安全、普通用户权限链，以及原生 Wayland 和真实应用场景的完整验证。**

本次核对的基准是 **2026 年 9 月 4 日发布的 `v2.0.26-linux.20`，提交 `64c51f7`**。这很重要：新版已经修复或重构了不少旧审计中的问题，不能照搬旧报告。项目也明确将自身定位为 **technology preview，而非完整 Win32 等价实现**。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/releases/tag/v2.0.26-linux.20))

以下是对源码、发行说明、能力矩阵、测试设计和 CI 配置的审查，**不是本地编译或物理设备实机验收**。我会区分“源码确认的缺口”和“需要实测确认的后果”。

## 一、和实际需求差多少？

你没有给出具体脚本和桌面环境，因此我按常见使用目标判断。**不建议用一个“完成度百分比”概括这个项目——不同场景的差距非常大。**

| 实际需求 | 我的判断 | 主要差距 |
|---|---|---|
| 用 AHK v2 写文件、字符串、对象、定时器、进程类脚本 | **差距较小，值得试用** | 核心语言已有较广覆盖，但路径、系统调用和平台相关行为仍需调整。 |
| X11 下使用常规热键、文本展开、窗口操作 | **差距中等，可在受控场景使用** | 已有较完整实现；仍应逐个验收关键脚本，不能把 API 存在当作所有边界行为一致。 |
| GNOME/KDE 原生 Wayland 下完整实现重映射、按键抑制、InputHook、文本展开 | **差距较大** | 快捷键注册、完整输入捕获、输入注入是不同能力；各后端并不等价。 |
| 中文输入、复杂编辑器、办公应用中的可靠自动化 | **差距中到大，强依赖具体应用** | 输入法真实桌面覆盖和无障碍接口存在明确限制。 |
| 原封不动迁移依赖 Windows DLL、COM、窗口消息的旧脚本 | **不是简单补缺，而是需要改写** | Linux 原生适配不是 Windows 行为兼容层；AHK v1 也不在支持范围。 |

上述判断依据仓库的语言与平台边界、输入后端能力定义，以及已公开的应用兼容限制；“差距大小”是我的工程判断，不是仓库提供的统计指标。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/v2.0.26-linux.20/README.md))

公平地说，新版已经有不少实质工作：统一输入语义、broker 协议与多客户端仲裁、恢复状态管理、多格式剪贴板、Unicode/IME 加固等，不能再笼统说它“全是桩函数”。但这些成果仍需要沿着**用户实际走的整条路径**验收，而不仅是分别测试各个组件。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/releases/tag/v2.0.26-linux.20))

---

## 二、我认为应该优先解决的问题

### 1. P0：进程内 evdev 兜底路径仍有吞输入风险

**这是本次审查最值得优先处理的源码问题。**

位置主要在：

`source/linux/core/core_evdev_linux.cpp`

新版修复了独立 `ahk-inputd` 的回放安全，但还有另一条路径：broker 连接不上，或重连超时后，运行时会转入**进程内直接读取设备的 fallback**。这条路径仍然会先独占设备，再在需要时尝试回放。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/v2.0.26-linux.20/source/linux/core/core_evdev_linux.cpp))

这里有两个具体缺口：

**首先，没有严格筛选键盘设备。** `ScanDevices()` 枚举 `/dev/input/event*`，`OpenDevice()` 主要排除自身虚拟设备名称，然后直接尝试 `EVIOCGRAB`。没有看到按设备能力确认“这是键盘”的检查；读取循环却只处理 `EV_KEY`，忽略其他事件类型。因此，在具备访问权限时，**鼠标、触控板也存在被误独占、运动事件不回放的风险**。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/v2.0.26-linux.20/source/linux/core/core_evdev_linux.cpp))

**其次，回放失败没有触发自动释放。** 第 965–969 行调用 `LinuxUinputKeyEvent()`，但不检查返回值。若设备已经被独占，而 `/dev/uinput` 不可用，未被热键消费的普通按键也不能可靠送回桌面。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/v2.0.26-linux.20/source/linux/core/core_evdev_linux.cpp))

下层还有错误传播问题：在 `core_uinput_linux.cpp` 中，写入函数不返回成功状态，外层 `LinuxUinputKeyEvent()` 调用后仍返回 `true`；底层写失败可能被上层当作成功。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/v2.0.26-linux.20/source/linux/core/core_uinput_linux.cpp))

**影响边界：**这不是说所有 X11 使用都会吞键，而是发生在“进入 evdev 直连兜底、设备可读、独占成功，但回放不可靠”等条件下。源码路径可以确认，具体设备上的表现尚未实测。

**建议：**在补齐设备筛选、回放预检、错误上传和失败自动释放之前，宁可让兜底路径只观察、不独占。输入工具首先必须保证“自己坏了，用户仍然能正常操作电脑”。

### 2. P1：root broker 与普通用户的能力授权存在断层，旧协议又能绕过同类限制

这里同时有**可用性问题**和**权限一致性问题**。

在 `inputd.c:1055–1064`，v2 只给 root 或与 daemon 同 UID 的客户端授予 `SUPPRESS / EXCLUSIVE / INJECT`。因此，**root broker + 普通用户脚本**这种部署中，普通用户即使能连接 socket，也只能得到观察能力。仓库提供的系统 socket 使用 `root:input`、`0660`，所以“有连接权限”和“有抑制权限”并不是同一回事。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/v2.0.26-linux.20/source/linux/inputd/inputd.c))

更关键的是客户端处理：`core_inputd_client_linux.cpp:381–386` 在拿不到 `SUPPRESS` 时，会把规则的抑制标志改成零。也就是说，**原本要求拦截原键的热键，会被降级为观察式订阅**；动态抑制规则也受权限条件限制。这容易造成“脚本触发了，但原按键也进了应用”的语义偏差。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/v2.0.26-linux.20/source/linux/core/core_inputd_client_linux.cpp))

另一方面，旧版 v1 的订阅处理 `inputd.c:1555–1582` 接受抑制标志，却没有同等 UID/能力检查。因此，同一个已经获准连接 socket 的用户，可能通过 v1 请求到 v2 拒绝的抑制行为。**这是本地授权策略不一致，不是已经证明的远程漏洞或 root 提权。**([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/v2.0.26-linux.20/source/linux/inputd/inputd.c))

现有协议测试分别覆盖了 v1 共存和 v2 权限拒绝，但不能替代“同一非特权用户经两个协议申请同一能力”的交叉验证。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/v2.0.26-linux.20/tests/oracle/run_inputd_v2_protocol_oracle.sh))

**建议：**先明确系统服务究竟允许哪些用户、哪些会话获得哪些能力，再让所有协议共用授权逻辑。请求抑制却只能观察时，应明确报告能力不足，而不是自动改变脚本含义。不要用“把所有脚本都以 root 运行”解决这个问题。

### 3. P1：输入队列溢出后的状态恢复不完整，多设备状态也值得重点复测

在 broker 的设备读取循环中，`inputd.c:2358` 直接跳过所有非 `EV_KEY` 事件，因此也跳过了 `SYN_DROPPED`。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/v2.0.26-linux.20/source/linux/inputd/inputd.c))

这不是普通的无关事件。Linux 内核明确规定：收到 `SYN_DROPPED` 后，应丢弃事件直到下一个 `SYN_REPORT`，然后通过 `EVIOCG*` 查询重新同步设备状态。它表示用户态读取队列已经溢出，不能继续相信此前维护的状态。([Kernel.org](https://www.kernel.org/doc/html/v6.12/input/event-codes.html))

**缺少这一步，丢失的 key-up 可能留下错误的修饰键或抑制状态。**这是源码缺口推导出的风险；我没有实机复现“Ctrl 卡住”。

另一个待验证点是：broker 的物理事务和抑制状态以 keycode 为索引，相关更新没有区分设备。双键盘同时按住同一键时，按下/释放归属可能发生混淆。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/v2.0.26-linux.20/source/linux/inputd/inputd.c))

**建议：**把“队列溢出后恢复”“两把键盘同时按 Ctrl”“按住修饰键拔设备”做成事件级回归。热插拔测试通过，并不自动证明持键状态也正确恢复。

### 4. 功能差距：原生 Wayland 还不能按“完整 AHK 输入环境”理解

这里需要把三件事严格分开：

**能注册快捷键，不等于能捕获完整键盘流；能发送输入，也不等于能读取输入。**

仓库自己的 `input_caps.def` 就显示：portal、GNOME 后端与 evdev 的能力并不相同，前两者缺少完整字符流及多项按键语义；libei 则是注入路径，不能据此推定 InputHook 或输入捕获已经解决。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/v2.0.26-linux.20/source/linux/core/input_caps.def))

还有一个明确的实现边界：broker 客户端构建字符流订阅时依赖 `LinuxX11Display()` 和 X11 布局信息。源码注释也明确说明，**没有可用 X11 布局来源的纯 Wayland 会话仍受限**。这会直接影响热字符串和可见 InputHook，而不是仅影响诊断输出。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/v2.0.26-linux.20/source/linux/core/core_inputd_client_linux.cpp))

因此，“Wayland 下有测试通过”不能直接推广为以下完整工作流已经成立：

> 普通用户登录原生 GNOME/KDE，使用中文输入法，在原生应用中展开热字符串、重映射按键；随后锁屏、撤销授权、恢复会话，脚本仍保持正确行为。

这是建议采用的验收场景，不是我声称已经观察到的失败案例。

### 5. 真实中文输入与应用自动化的覆盖，仍明显窄于接口覆盖

仓库并不是完全没有输入法实测：IBus/libpinyin 有真实虚拟机证据。不过，已记录的关键测试包括 **GNOME 会话中的 XWayland GTK Entry**，这不能代表所有原生 Wayland 应用。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/v2.0.26-linux.20/docs/IME-Integration.md))

Fcitx5 目前主要是协议覆盖，Flatpak/portal 输入法可见性仍需专门主机验证。应用控制方面，仓库还明确记录了 Calc 虚拟单元格接口未完成、VS Code 的 Monaco 源码内容不可访问等边界。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/v2.0.26-linux.20/README.md))

这意味着：

**“Control/AT-SPI 接口已实现”与“能稳定操作用户手里的应用”之间，还有一层真实兼容性工程。**

建议先选定少数目标组合，例如一个 GNOME 环境、一个 KDE 环境，以及浏览器、编辑器、办公应用各一个，再验证完整任务：输入中文、切换焦点、替换文本、读取控件、复制富文本、恢复剪贴板。不要继续用接口数量代替场景完成度。

---

## 三、为什么全绿测试还不足以消除这些担忧？

### 测试数量代表断言，不代表兼容功能数量

当前测试报告中的 X11/headless 总数是 **1170 项断言**，其中也包括未实现边界、错误行为和严格兼容模式等检查。这些测试很有价值，但有些证明的是“正确拒绝不支持的操作”，不是“已实现 Windows 同等功能”。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/v2.0.26-linux.20/tests/doccheck/CHECK_REPORT.md))

Windows 差分测试的方向也正确：使用官方 Windows 版本的结果，并以独立输入工具驱动 Linux 端，而不是仅靠自身 Send 自测。但测试说明仍把物理透传、组合键/重映射、SendInput/SendPlay 时序和更广泛的热字符串选项列为后续覆盖。**目前有对照证据，不等于已经获得全面等价证据。**([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/v2.0.26-linux.20/tests/differential/README.md))

### 测试环境和真实桌面之间仍有距离

四发行版容器矩阵、headless 回归、Xvfb、协议故障测试，都不能替代 GNOME/KDE 上的真实输入、权限和会话生命周期测试。CI 中也能看到大量测试运行于这些受控环境。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/v2.0.26-linux.20/.github/workflows/ci.yml))

长期稳定性方面，新版明确说明：**24 小时 soak 没有完成，目前保留的是 CI 30 秒和 VM 5 分钟证据。**这不能推出存在内存泄漏，但也不足以证明全天候桌面常驻可靠。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/releases/tag/v2.0.26-linux.20))

我更关心的下一批指标是：是否丢键、是否重复触发、是否有未平衡的按下/释放、恢复耗时、输入延迟尾部，以及多脚本争用时的确定性。

### 文档本身已经出现版本漂移

同一个 `linux.20` 标签中：

| 文件 | 写出的 X11/headless 断言数 |
|---|---:|
| `README.md` | 1145 |
| `docs-v2/docs/linux-port.htm` | 1143 |
| `tests/doccheck/CHECK_REPORT.md` | 1170 |

这三个数字可以直接在文件中核对。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/v2.0.26-linux.20/README.md))

这不说明测试造假，但说明**发布状态、测试证据和用户文档尚未形成可靠的单一事实来源**。特别是主页还把旧数字标为 authoritative，容易影响用户判断。

建议把版本、测试总数、已验证环境和已知限制，从同一份机器可读清单生成，并绑定 commit、环境和运行日期。

---

## 四、我建议的修复顺序

| 优先级 | 先做什么 | 验收标准 |
|---|---|---|
| **第一批：输入安全** | 修进程内 evdev 的设备筛选、回放预检、失败自动释放、uinput 错误上传；统一 v1/v2 授权 | broker 不可用、uinput 权限不足、运行中写失败时，普通键盘和鼠标仍可操作；同一用户不能通过切换协议获得不同权限 |
| **第二批：状态与语义** | 补 `SYN_DROPPED` 重同步、多键盘持键管理；禁止无提示的抑制→观察降级 | 双键盘、热拔插、队列溢出、重连后没有幽灵修饰键；脚本要求无法满足时明确失败 |
| **第三批：真实场景与发布可信度** | 固定 GNOME/KDE、IBus/Fcitx5 和目标应用组合；扩大 Windows 差分与长稳测试；自动生成支持矩阵 | 不仅单个 API 通过，完整用户任务也通过，并能准确说明哪些环境未验证 |

以上是我的优先级建议，不是仓库现有计划。

## 最终判断

**相对于“Linux 上运行 AHK v2 语言和一批桌面自动化功能”，这个项目已经有相当扎实的成果。相对于“普通用户放心地作为全天候主力输入自动化工具”，还没有到位。**

对使用者，我会建议从**纯语言脚本和范围明确的 X11 自动化**开始逐项验收；在上述安全缺口关闭前，不宜把未经验证的 evdev 兜底当作主力桌面的全局输入接管方案。

对维护者，下一阶段最有价值的不是再增加几十个“已实现函数”，而是补上三条完整链路：**输入失败不影响用户操作、普通用户权限与脚本语义一致、真实 Wayland/中文/应用场景可重复验收。**这三项改善，比测试总数继续增长更能缩小与实际需求的距离。