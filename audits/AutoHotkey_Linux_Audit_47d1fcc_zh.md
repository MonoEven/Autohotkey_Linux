# AutoHotkey_Linux 独立技术审计
## Commit-frozen / 真实用户兼容性 / Production readiness
审计日期：2026-09-05。审计对象：MonoEven/Autohotkey_Linux。

> **本报告只对应 commit `47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f`。**

**结论：整体是 useful for selected workloads，工程阶段是 advanced technology preview，而不是通用、跨桌面、全天候的 Windows AutoHotkey v2 替代品。**
语言、Linux 工具脚本、受控 X11 热键与部分 GUI/AT-SPI 工作负载已经有实际价值；复杂物理拦截、多设备、多脚本、原生 Wayland 跨桌面和全天运行，仍有代码问题与验证缺口。

## 阅读与证据边界

本审计读取了冻结 SHA 的源码、测试实现、工作流、公开 Actions 运行页和 VM 记录；没有把 README、旧 audit、截图或绿色 CI 直接当作兼容性证明。没有在本地完整克隆、构建或运行 ahk_core 全套测试：执行环境不具备项目的真实桌面、物理 input/uinput 设备，也不能通过容器网络取回仓库。公开 Actions 的总体成功状态已核查，未下载并逐条重验其全部原始 trace/artifact。因此“CI 执行证据”指具体 workflow 命令加对应 SHA 的成功运行，不等于本审计亲自执行，也不证明内部没有条件跳过。

**本审计实际执行了两个独立进程/Xvfb 实验**：Xlib I/O handler 返回后的退出行为；X11 clipboard ownership 检查与恢复之间的竞争。它们验证源代码所依赖的系统契约，不是伪装成当前 AHK runtime 的端到端复现。源码、运行脚本和日志随报告附带。

来源标为 [Sxx]，完整、commit 固定的地址在文末。未取得的证据写“未找到/未核实”，不等于功能必定不存在。所有分数是基于 ledger 的风险加权工程估计，不是实验统计、迁移成功概率或由函数/测试数量换算的百分比。

# 1. 审计快照与 Release → HEAD

| 字段 | 冻结结果 |
|---|---|
| 默认开发 branch / 当前审计 branch | linux-port / linux-port |
| HEAD full SHA | `47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f` |
| HEAD author/commit 时间 | 2026-09-05 23:37:42 +08:00，即 15:37:42 UTC |
| HEAD message | feat: add visual AHK syntax teaching studio |
| 最新 Release | AutoHotkey v2.0.26 for Linux — linux.21 |
| Release tag | v2.0.26-linux.21 |
| Release commit SHA | 77c12310213150652b0d27cdbf431b370d284259 |
| Release 发布时间 | 页面显示 2026-09-05 10:57；秒数与显示时区未独立取得，不补造 ISO 时间 |
| HEAD 相对 Release | ahead 2 / behind 0 |
| 审计中版本切换 | 没有；所有源码判断均保持冻结 SHA |

依据：[S01–S05]。Release 的分钟级发布时间是本次元数据精度缺口；它不影响已核实的提交祖先关系。

### 两个、也只有两个 release 后提交

| Commit | 修改内容 | 类型 | 用户兼容性影响 | Release gate 是否覆盖 |
|---|---|---|---|---|
| 90612f6545071f88758d7fd58c3aac86d1a3c253 | VSIX oracle 的预期版本对齐 0.2.1，增加 VM visual smoke 记录；4 files，+52/-9 | Test / Docs / Tooling evidence | 不修改 runtime 或扩展实现；影响验证可信度 | **不属于 linux.21 的 release evidence**。该 commit CI 成功；extension-host 脚本不是当前 CI 的必跑命令 |
| 47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f | Syntax Studio 示例、说明、5 张图片、源码 guard、CI 接入；9 files，+330 | Examples / Test / Docs / CI | 新 GUI 集成负载；不改变既有 C++ runtime 语义 | 只有 HEAD 的 source guard/现有回归覆盖；没有 release-qualified 的完整 UI 行为验收 |

**Release-proven 与 HEAD-added 必须分开**：Release tag 对应 Actions run 33961359281 成功；HEAD run 33975385717 也成功。二者不能合并成“教学工具和新版 VSIX VM 验证已进入 linux.21”。另一方面，两个提交没有修改 runtime C++，所以不应捏造“HEAD 引入了一批未经 release 验证的新底层输入代码”。[S01–S07]

Prompt 中的 linux.21、check0905、input safety、ClipboardAll、inputd readiness、Wayland recovery、libei、Windows differential、VS Code 和教学工具均能在本快照中定位；**能定位不等于它们已经达到同一成熟度**。

### Post-audit changes / 未纳入本次审计
没有将冻结后可能出现的任何新提交纳入评分；本报告不声称描述阅读时的动态最新分支。

# 2. 证据模型

| Axis | 等级解释 |
|---|---|
| Oracle | O0 文档声明；O1 内部断言/源码 guard；O2 项目控制的集成 oracle；O3 独立进程；O4 kernel/device/compositor/真实应用；O5 官方 Windows AHK 差分 |
| Environment | E0 静态；E1 unit/mock；E2 Xvfb/headless/protocol harness；E3 VM 桌面；E4 真实 compositor/application 环境；E5 物理输入/长时真实主机 |
| Freshness | F0 未绑定可核实 commit；F1 历史版本；F2 当前 Release；F3 当前 HEAD |

O 与 E 不能简单相乘排名：真实 Windows fixture 可以是 O5/F1；真实 GNOME VM 截图可以是 E3/F0；冻结源码可以是 F3/E0，但没有“已执行”的含义。

下文 **CI3** = HEAD 工作流中直接调用且公开运行成功；仍保留未审阅全部原始 artifact 的限定。**VM?** = 当前仓库保存的 VM 执行记录，但未找到足够的可验证 SHA/二进制 hash 绑定，保守记 F0。**设计** = 测试存在，不能从存在本身推出通过。历史 Windows fixture 另记 F1。来源 [S04–S07, S35, S46]。

# 3. Capability Claim Ledger

“Claim”是作者表达的能力范围的归纳，不是审计接受。路径省略 `source/linux/core/`；测试名省略 `tests/oracle/`；下表的证据等级只适用于该行所述的有限路径，不可横向推广到整个 backend。


| Capability | 当前 Claim | 实际代码路径 | Test | Test environment | Oracle / Freshness | 当前判断 | Confidence |
|---|---|---|---|---|---|---|---|
| language/runtime | v2 API 广覆盖 | 上游 parser/runtime + Linux core；[S08–11] | headless / doccheck | CI3，headless/Xvfb | O1–O2/E1–2/F3 | 最成熟层；不是整个 v2 语义已差分 | 中高 |
| Hotkey | 多 backend 热键 | core_hotkey；input_backend；[S18,22] | x11、pipeline、差分 | CI3 Xvfb/harness | O3/E2/F3；O5 fixture/F1 | X11 普通热键可用，accelerator 不等于 hook | 高 |
| HotIf | 动态条件 | pipeline；inputd client；[S17,21] | dynamic_decision | CI3 deadline harness | O2–3/E2/F3 | 有超时 fail-open；跨进程/原生窗口判定有限 | 中 |
| custom combination | prefix/keyup/pass-through | input_pipeline；evdev；[S13,21] | combo_pipeline | CI3 三种 pipeline 模式 | O2–3/E2/F3 | 丰富分支已实现；完整 Windows 行为矩阵未建立 | 中 |
| remapping | 原键抑制、替换事务 | pipeline；inputd；[S15,21] | remap_pipeline / arbitration | CI3 harness | O2–3/E2/F3 | 单路径有实证；双设备同码/多 owner 风险 | 中 |
| Hotstring | 字符流、选项、替换 | core_capture；[S23] | consumer、multiscript、差分、IBus | CI3；VM? | O3/E2/F3；O4/E3/F0 | X11 有用；不能推广到无捕获能力的 Wayland | 中高 |
| InputHook | key/char/end callbacks | core_capture；pipeline；[S21,23] | consumer、suppression、Windows F13 | CI3；IBus VM? | O3/E2/F3；O5/F1 | 事件路径真实存在；全局原生 Wayland 仍依赖特权 capture | 中 |
| Send | 多 transport | core_input；[S24] | 独立 X11 receiver、libei | CI3 | O3–4/E2/F3 | 实际注入有效；部分失败不是原子回滚 | 中高 |
| SendText | literal Unicode | core_input；clipboard；[S24,28] | keymodel Euro、文本测试 | CI3 Xvfb | O3/E2/F3 | 多条注入方式；paste fallback 的 clipboard 一致性不足 | 中 |
| SendEvent | Event 策略 | core_input；[S24] | sendlevel/policy | CI3 | O1–3/E2/F3 | 有独立策略；不等于 Windows transport | 中 |
| SendInput | Input 策略 | core_input；[S24] | policy / input traces | CI3 | O1–3/E2/F3 | 与 Event 共享 Linux 注入基础设施；原子性不同 | 中 |
| SendPlay | Play 适配 | core_input；[S24] | policy | CI3 | O1–2/E2/F3 | 兼容 API，不是 Windows journal/playback 实现 | 中 |
| SendLevel | synthetic level | input_semantics / event / broker；[S16,19,21] | sendlevel、provenance | CI3 | O2–3/E2/F3 | broker 权威标记优于 X11 推断；不宜宣称跨 backend 完全相同 | 中 |
| InputLevel | 消费过滤 | pipeline / capture；[S21,23] | policy / consumer | CI3 | O2–3/E2/F3 | 核心规则有测试；五脚本循环与重连矩阵缺失 | 中 |
| scan code | set-1→evdev/X11 | core_keymodel；[S25] | keymodel oracle | CI3 Xvfb AZERTY fixture | O3/E2/F3 | sc01E 物理语义已有独立证据；本地 replay 仍绕回 VK | 中高 |
| modifier semantics | LR/wildcard/repeat | hotkey/pipeline；[S21,22] | special/keymodel/combo | CI3 | O3/E2/F3 | 单流明显改进；跨设备 owner 不能只靠 bitset | 中 |
| X11 capture | grabs + XI2 | hotkey/capture；[S22,23] | raw/multiclient/XTEST | CI3 Xvfb | O3/E2/F3 | 当前最佳桌面路径；pass-through reinjection 有语义代价 | 高 |
| XWayland | X11 apps on Wayland | X11 paths + compositor bridge | doccheck 234、IBus VM | CI3；VM? | O2–4/E2–3/F3或F0 | X11 窗口子域可用，不代表 native targets 全可控 | 中高 |
| Wayland portal | registered accelerator | backend / portal shortcut adapter；[S18,35] | portal restart | CI3 私有 D-Bus portal | O3/E2/F3 | 明确属于 accelerator，不是 continuous raw hook | 中 |
| GNOME extension | Shell 热键 broker | extension.js；[S62] | VM/owner lifecycle | VM?；源码 | O2–4/E0–3/F0/F3 | sender ownership 做得好；hook 语义范围较窄 | 中 |
| evdev | 物理 capture/suppress | core_evdev；[S13] | fail-open source guard / hotplug 设计 | E0/CI3；物理 VM 缺设备 | O1/E0/F3 | preflight 已修；原始事件保真与停滞 fail-open 未闭合 | 高（源码） |
| uinput | kernel injection/replay | core_uinput；[S14] | replay failure / fixtures | CI3 harness；无本次实机 | O2–4/E2/F3，逐测试区分 | write 错误已检测；不保证已送出的按键可回滚 | 中 |
| ahk-inputd | 多客户端授权 broker | inputd.c / proto；[S15,16] | v2/injection/arbitration | CI3；特权 lane 另列 | O2–4/E2/F3 | 不只是 socket demo；session policy/物理长稳仍不足 | 中高 |
| libei/EIS | consented injection | input_backend_libei；[S26] | 独立 libeis receiver | CI3 protocol harness | O4/E2/F3 | sender/设备生命周期较成熟；真实 portal 授权未闭环 | 中高 |
| Clipboard | X11/WL 文本 | core_clipboard；[S28] | roundtrip / paste unit | CI3 Xvfb/headless | O2–3/E2/F3 | 日常文本可用；并发恢复有真实 race | 高（race） |
| ClipboardAll | rich snapshot | core_clipboard；[S28] | integrity guard / serializer | CI3 mixed unit/source | O1–3/E0–2/F3 | X11 rich；native Wayland 和临时 paste restore 仍文本化 | 高（范围） |
| OnClipboardChange | 所有权/内容通知 | clipboard/Wayland/GNOME | 场景与组合 soak | CI3；VM? | O2–3/E2/F3 | 存在；双脚本+重启+ABA 未完整验证 | 中 |
| IME | composition/commit integration | core_ime + capture；[S23,29] | state guard / framework oracles | CI3；VM? | O1–4/E0–3/F3/F0 | 已实现，不应再称“缺失”；lifecycle/Unicode 全矩阵不足 | 中 |
| IBus | libpinyin 中文输入 | core_ime；[S29] | run_ibus_ime_oracle | GNOME VM，GTK XWayland | O4/E3/F0 | 有真实应用证据；不是 native Wayland IME 全覆盖 | 中高 |
| Fcitx5 | D-Bus protocol | core_ime；[S29] | fcitx5_ime_protocol | CI3 独立模拟服务 | O3/E2/F3 | protocol verified；real daemon/desktop/Flatpak 未证明 | 中 |
| Win* | window API 适配 | core_win；[S30] | EWMH / doccheck | CI3 Xvfb；部分 VM | O2–3/E2/F3 | X11 强于 native Wayland；窗口身份/API 不能跨平台照搬 | 中 |
| Control* | X11 + accessibility | core_ctrl；[S31] | target toolkit oracles | VM? GTK/Qt/Java/LO | O4/E3/F0 | 目标应用接口决定上限，不能按函数实现率估计 | 中 |
| AT-SPI | Text/Action/Selection/Value/Table | core_atspi；[S32] | GUI host matrix | VM? | O4/E3/F0 | 多 toolkit 的目标侧效果值得肯定；不是全接口全应用 | 中高 |
| GUI | GTK AHK Gui | Linux GUI adaptation；[S59,60] | doccheck / Syntax Studio | CI3；VM截图 | O1–4/E0–3/F3/F0 | 复杂控件集已能展示和交互；缺完整 UI 行为自动化 | 中 |
| Menu | menu 适配 | GUI/menu adaptation | doccheck / GUI workload | CI3 Xvfb | O1–2/E2/F3 | API useful；事件排序和跨桌面焦点尚非 O5 | 中 |
| tray | XEmbed/SNI 等适配 | desktop integrations；[S11,12] | KDE SNI 场景 skip | X11 CI；KDE缺环境 | O1–3/E2/F0/F3 | 不能由托盘对象创建推出 GNOME/KDE 可见可交互 | 中低 |
| D-Bus/COM adaptation | COM-like object interface | D-Bus adapter；[S07,11,35] | bounded recovery oracle | CI3 独立服务 | O3/E2/F3 | intent useful；Windows COM 对象本身不兼容 | 中高 |
| DllCall | Linux ABI/libffi | core_dllcall；[S33] | headless/doccheck | CI3 | O1–2/E1–2/F3 | Linux .so 可用；Win32 DLL 代码不是 unchanged migration | 中 |
| package/install | 多格式发布 | tools/linux / CI；[S07,51,52] | install/run/uninstall | CI3 containers | O3/E2/F3；Release F2 | deb/tar strongest；AUR/Flatpak pins 落后 | 中高 |
| --pack | standalone payload | core_pack；[S34] | pack acceptance | CI3 containers | O2–3/E2/F3 | happy path 已验；缺资源有越界风险，libei-off template 改变能力 | 高（源码） |
| debugger / VS Code | DBGp/DAP/VSIX | extension / adapter；[S54–58] | DBGp / DAP / extension-host | CI3 + VM? | O3/E2/F3；O4/E3/F0 | 实际可用；evaluate 是 property lookup，不是完整任意表达式 | 中高 |
| crash recovery | generation/reconnect/reconcile | broker/Wayland/backend；[S17,18,27] | restart / health | CI3 headless | O3–4/E2/F3 | 单组件恢复可验证；Xlib fatal path 和组合状态未闭环 | 中 |
| compositor restart | same process recovery | core_wayland；[S27] | m7 restart | CI3 headless sway，DISPLAY unset | O4/E2/F3 | 真有重启恢复；不能覆盖 X11、portal UI、libei、tray、GUI 全栈 | 中高 |
| multi-script | shared broker/raw capture | inputd/pipeline/capture | arbitration / two-script | CI3 | O3/E2/F3 | 已超过“能连接”；五角色长期混合仍缺证据 | 中 |
| long-running stability | mixed soak | runtime / soak；[S48] | CI 30 s，VM 5 min 记录 | E2；VM? | O2–3/E2–3/F3/F0 | 没有可核实 1/8/24/72 h 成功证据 | 高（证据缺口） |

**本账本的核心判断**：实现广度已经很高，验证深度极不均匀。X11 独立输入 oracle、libeis receiver、真实 IBus/AT-SPI 应用记录，不能被压成“只有 mock”；反过来，它们也不能为物理输入、多桌面授权或长期一致性背书。


# 4. check0905：Closure review，不复制旧问题

下表把用户指定的 23 项作为固定分母。“Closed”只表示**原窄缺陷的代码意图和相应回归层已闭合**，不等于 physical-host 或 production-qualified。修复提交映射来自 closure 记录，再与当前代码交叉检查；没有对所有提交逐项 bisect 复现。依据 [S06, S13–18, S21, S28–29, S49, S07]。

| finding / 原问题 | 后续 fix commit | 当前代码与测试 | 判断 | 新风险或范围边界 |
|---|---|---|---|---|
| in-process device filtering：非键盘被抓取 | 98f6e6b0 | EV_KEY + KEY_A/ENTER，排除 AHK virtual；source guard | Closed | 复合键鼠设备仍需单独评估；不等于所有 raw event 都保真 |
| uinput preflight：先 grab 后发现不能 replay | 98f6e6b0 | 先验证回放能力再 grab；guard | Closed | 运行中失效、SIGSTOP、挂起是不同问题 |
| replay write failure：未 fail-open | 98f6e6b0 | 检测失败，释放 local grabs，degraded | Closed | 只有实际进入 write 的键；未映射键被跳过不是 write failure |
| inputd v1/v2 capability parity | da648e68 | 共用 privileged policy；v2/legacy protocol cases | Closed | 新 client 接旧 daemon 仍是 observe-only 语义退化 |
| suppression authorization | da648e68 | uid/root/daemon-owner 判断；跨 UID 否认 | Closed | root daemon 下普通 input group 不等于获得 suppress |
| SYN_DROPPED | 417c396e | broker 重同步/释放；state guard | Partial | local evdev 仍跳过非 EV_KEY；物理丢事件测试不足 |
| per-device physical state | 417c396e | broker 物理 owner 数组按设备拆分 | Partial | reducer seat bitset、replacement/output owner 未端到端按设备建模 |
| stable device identity | c52e4c00 | broker 稳定 identity；guard | Mostly | local slot identity 和跨设备重连仍需真实验证 |
| injection phase validation | 0f878035 | DOWN/UP/REPEAT 状态约束；negative oracle | Closed | 这是合法性检查，不是 application rollback |
| incomplete transaction commit | 0f878035 | count/commit 检查与清理 | Closed | 第 N 次失败前的副作用不可撤销；多 owner held-key 仍需测试 |
| ClipboardAll bounds | a1432c26 / 934a4733 | 序列化/反序列化限制 | Partial | 多 MIME 获取过程总预算并非从第一字节就受同一限制 |
| strict UTF-8 | a1432c26 / 934a4733 | 严格解码、拒绝非法序列 | Closed | 不代表所有 IME/GUI/Send 路径同样验证过完整 corpus |
| MIME validation | a1432c26 / 934a4733 | MIME 校验与格式过滤 | Closed | owner 在逐 MIME 读取间变化仍会影响快照一致性 |
| INCR | a1432c26 / 934a4733 | 接收端分块处理存在 | Partial | 大 payload 发送仍需 INCR sender；双向不能合并评价 |
| Wayland paste cancellation | a1432c26 / 934a4733 | cancellation/ownership 标记 | Mostly | cancel、断线、并发回调的 lifetime 组合未证明 |
| clipboard restore CAS | a1432c26 / 934a4733 | restore 前检查 owner；unit oracle | Partial | check 与 set 不是原子操作；独立实验可覆盖第三方新复制 |
| bounded D-Bus calls | 37b31965 / 38440354 / 4e200a73 | budget/pending-call/pump；独立延迟服务 | Mostly | 并非每个初始化 roundtrip/服务重建都包含在已有 fault lane |
| callback/reentrancy protection | 同上 | IME immutable queue、AT-SPI nested EBUSY | Mostly | teardown/service churn/并发 status buffer 的组合仍需专项 race test |
| systemd Type=notify | 同上 | service Type=notify + READY 状态 | Closed | READY 不等于获得 replay/suppress 能力 |
| degraded readiness | 同上 | health/degraded 信息可区分 | Mostly | 消费方是否把 degradation 变成明确错误仍需端到端验收 |
| suppression-capability routing | 2f41c2fc | CapsSatisfy + source guard | Partial | 静态 backend 能力不等于当前 negotiated grant |
| XWayland required-gate | 6d517c38 | required lane 的失败处理收紧 | Closed | 不覆盖 native KDE/GNOME UI |
| documentation test totals | d9d609d7 | README/docs/CHECK_REPORT 同步检查 | Closed | 旧数字漂移已修；新增 drift 是 AUR/Flatpak 版本 pin |

**计数：23 项中 Closed 12、Mostly closed 5、Partially closed 6；没有把这 23 项中的任何一项认作“完全没修”。** 按原 audit 的七组汇总，则是 fully 2、mostly 2、partial 3；分母不同不能混用。没有足够证据把任何一组标为 production-qualified。新识别风险不反向抹掉已有修复。

## Confirmed Improvements Since Previous Audits

真正值得提高评价的进展包括：抓取前 preflight；协议版本统一授权；broker 的设备状态和重同步设计；injection phase/count 的否认测试；X11 rich ClipboardAll 的校验层；IME 提交排队及有界 D-Bus；实际 libeis receiver 故障场景；Windows 外部输入差分；同 PID Wayland/broker/IDE 重连；XWayland gate 与文档总数修复。[S06, S13–17, S26, S28–29, S35–47]

以下说法不再适合当前快照：“没有 libei”“ClipboardAll 只会处理文本”“IME 没实现”“inputd 只是多客户端连接”“所有测试都是实现自证”“文档测试总数仍然漂移”。

### Closure Quality

| Historical issue | Code fixed | Regression | Independent oracle | Real host | Long-term | Closure quality |
|---|---|---|---|---|---|---|
| grab 前无 replay preflight | 是 | source guard | 相关 broker fault，不等价 local 全路径 | 未取得 local 物理证明 | 无 | guarded |
| v1/v2 授权不一致 | 是 | negative protocol | 独立客户端/不同 UID | 不依赖 GUI；物理授权 UX 未闭环 | 无 | integration-verified |
| broker SYN_DROPPED/per-device | broker 是 | guard + 部分状态 oracle | 部分 | 双物理键盘未取得 | 无 | guarded / 部分 integration |
| injection phase/count | 是 | 明确负例 | 独立协议端 | 应用实际 held state 仍有限 | 无 | integration-verified |
| rich snapshot 校验 | X11 是 | serializer/source guard | 部分 X11 selection | rich 全格式跨应用未取得 | 无 | guarded |
| 原始 paste owner overwrite | 部分 | unit | 本审计独立 race 发现缺口 | 未取得 | 无 | 不能标 Closed |
| D-Bus blocking/reentrancy | 主要路径是 | 延迟/嵌套调用 | 独立服务与 Qt marker | VM 记录 | 无 | integration-verified |
| IME preedit/commit | 是 | protocol + IBus oracle | GTK/IBus 目标 | VM，hash freshness不足 | 无 | real-host evidence，非 F3-qualified |
| Wayland restart | 是 | headless restart | sway 实际绑定触发 | headless，不是实际桌面全栈 | 无 | integration-verified |
| VS Code DBGp/DAP | 是 | 外部 IDE/DAP client | 有 | VM visual/host 记录 | 无 | integration-verified |
| XWayland required gate / docs | 是 | CI guard | 运行页 | 不适用 | 不适用 | guarded |

# 5. 输入安全：fail-open 的闭环是否成立

**最坏情况下仍不能排除用户失去正常键盘输入。** 原因不是旧的“uinput 不可写仍然先 grab”没有修，而是 live-but-stopped 的抓取持有者、local raw replay 漏洞，以及输出已按下状态的恢复尚未端到端闭合。本审计没有在物理主机上复现锁键，因此将最高风险列为 **P0 candidate / Requires runtime validation**，不是声称已证明所有用户会锁死。

SIGKILL 与 SIGSTOP 必须分开：进程死亡会关闭 fd；进程停止但 fd 仍存活不会自动解除 grab。inputd 的进程内 SIGALRM 紧急释放能改善运行中卡住的某些情况，却不能在进程被冻结时执行；service 没有独立 watchdog lease 作为完整替代。local evdev 也不能靠同一个被阻塞的事件循环证明 fail-open。[S13–15, S49]

| Failure case | local evdev/uinput | inputd | X11 / portal / GNOME / libei | 当前结论 |
|---|---|---|---|---|
| 启动时 /dev/uinput 不可写 | preflight 拒绝 grab | 健康/回放能力降级 | 非 evdev 路径另判 | 旧危险路径修复；需正常用户安装实测 |
| 第 N 次 write 失败 | 已有 ungrab；原始映射缺失不走此分支 | fail-open/事务清理 | libei 能失效事务但不能回滚应用 | 必须验证 target 的 key-up，不只看 ACK |
| device unplug，尤其 Ctrl held | prune/清理部分状态 | per-device cleanup | X11 hierarchy 处理 | 物理状态与 virtual output held state 双重 oracle 缺失 |
| SYN_DROPPED | 非 EV_KEY 被跳过 | broker 有重同步/释放处理 | 不适用或由上层服务器处理 | backend parity 未完成 |
| broker SIGKILL | fallback 风险需评估 | kernel close grabs；client reset/reconnect | 不适用 | 有重连 oracle；全过程无重放重复尚需组合测试 |
| client SIGKILL / script crash | own fd 关闭 | owner 清理/lease | accelerator owner cleanup | 独立 broker 测试较强；多脚本输出引用计数仍需验 |
| holder SIGSTOP / 进程内永久停滞 | 无独立释放保证 | 内部 alarm 无法覆盖 SIGSTOP | X11 抢占的快捷键可能不可用；不是全局 raw grab | **物理 fail-open release blocker 候选** |
| reconnect halfway / daemon restart | 避免与 fallback 同时接管 | generation/reset/resubscribe | 按 lane 重建 | 需断在每一个状态边界的 fault schedule |
| compositor restart | evdev 不等于 compositor state 已恢复 | 下游应用状态未知 | headless WL 已验；Xlib fatal；EIS/session 分开 | “socket 回来”不等于脚本继续正确 |
| permission revoked / EIS disconnect | 已打开 fd 的权限语义不能靠 chmod 推断 | 更新 grant/health 必须重路由 | libei pause/revoke/terminal failure | 真实 portal revoke 未证明 |
| broken pipe / stalled peer | 不适用 | bounded framing/backpressure；peer dead cleanup | D-Bus budget | 协议层较成熟，不证明物理恢复 |
| logout / power / suspend-resume | fd/设备/held state 全要重新对账 | session policy 不完整 | token、clipboard、tray、GUI 都受影响 | 没有完整证据 |

不要把 portal 或 GNOME accelerator 称为会 EVIOCGRAB 的 hook：它们的失败范围通常是已注册快捷键/会话，不是物理键盘整流。libei 是注入通道；其主要风险是残留合成按键和失败后重复发送，而不是它本身抓住物理键盘。

### 必须新增的 physical-host fault matrix
两个物理键盘 + 鼠标；同一键跨设备交叠；held Ctrl 拔掉一只键盘；SYN_DROPPED；grab 前/后禁用 uinput；daemon/client SIGKILL；daemon SIGSTOP；脚本阻塞于 DllCall；重启时保持键按下；suspend/resume。由独立 evdev recorder、compositor/目标应用 recorder 同时记录 down/up、repeat、device、延迟，另有不受被测进程控制的恢复通路。

# 6. ahk-inputd：安全边界与多客户端语义

## 6.1 授权
已检查 socket mode/ownership、SO_PEERCRED、UID、v1/v2 grant。root:input 0660 使 input 组可以访问 socket，但 **连接权限 ≠ SUPPRESS/EXCLUSIVE/INJECT 权限**。共用策略仅允许 root 或 daemon 同 UID 获得高危操作。root system service 下，普通 input 组用户主要获得观察能力。这修好了降级绕过，但留下真实部署的权限 UX：不能通过让所有用户脚本以 root 运行来“解决”。未看到完整 active logind session/seat 授权流程。[S15–17, S49–50]

v2 帧长、状态、sequence、nonce 与 capability 拒绝是实质防护；不同协议版本共用政策是应明确承认的改进。尚需旧 daemon/新 client、新 daemon/旧 client 的真正二进制交叉升级测试，而不是仅用当前 daemon 模拟两个 wire version。

## 6.2 A suppress / B observe / C remap / D InputHook
已有 arbitration oracle 不仅检查连接：覆盖 observe、suppress、多角色、冲突、preemption、crash/lease 和跨 UID 拒绝。kernel virtual-device fixture 与纯 socket fake 也不是同一种证据。未取得逐条 raw trace 时不把所有 lane 提升成“本审计验证了真实设备”。[S38–40]

设计上，观察者应看到原始 physical A；原始 A 是否到应用取决于获胜的 suppression/replacement 决定，而不是每个 script 各自 replay。获胜 remap 才产生 B；synthetic B 是否进入其他消费者，还取决于订阅、来源分类与 SendLevel/InputLevel。这个模型比各 script 独立 grab 更有确定性。题设没有指定 A 与 C 的优先级/注册先后，因此不能不加条件地断言谁获胜；必须先固定 arbitration 输入，再比较唯一确定的目标事件序列。

但是 **D 的真实 AHK InputHook 与上述 A/B/C 同时运行，再加 E clipboard watcher**，尚不是已有 broker 小型客户端测试的同义词。需要验收输出表，而不是只数 callback：
- physical A 的每个 phase，各 client 谁看见、谁拥有抑制；
- 应用 A/B 的实际下/上事件；
- synthetic B 是否被重新触发；
- owner crash、lease expire、重连后唯一输出与无残留。

## 6.3 未完全解决的共享状态
broker physical bookkeeping 已按设备拆分，但 reducer 的 held/repeat/keyup ownership 仍以 seat+key 位图为中心；replacement rule 也有单一 active transaction 状态。两个设备同时按同码键，不等价于一个设备 autorepeat。**modifier snapshot 可能修正部分 Ctrl 场景，不能简单断言所有 Ctrl 都必然出错；非 modifier 同码 held/repeat 与 replacement owner 缺少引用计数的问题仍然成立。** 应将 state key 明确分为 physical device/key、seat aggregate、synthetic transaction/key，再定义映射。[S15, S17, S21]

## 6.4 注入“事务”不是数据库事务
phase/count/commit 校验已修；但事件在 commit 前可以已到应用。第 N 个失败时，前 N−1 个产生的文本、快捷键或点击不能撤销。正确契约应是：不重复重放已接受部分、尽力平衡 held keys、明确报告 partial delivery/unknown delivery；不应承诺 atomic rollback。还需全局 synthetic held-owner 计数，避免一个事务关闭释放另一个事务仍需保持的同键。[S15–16, S24, S26, S39]


# 7. Capability routing、normalized input、键盘语义

## 7.1 四种能力必须分开

| backend | 注册 accelerator | 连续 raw/character stream | 任意键抑制/组合键 | 注入 | 真实限制 |
|---|---|---|---|---|---|
| X11 | 是 | XI2/raw + layout/IME 组合 | 有实现，pass-through 依赖 reinjection | XTEST | synthetic provenance 部分是推断；断线恢复另审 |
| XWayland | 对 X 子域有效 | 不自动等于 native Wayland 全局流 | 受 compositor/X 子域影响 | 对 X 目标最可靠 | XWayland 存在不是全桌面 hook |
| GlobalShortcuts portal | 是 | 不是 arbitrary character/raw capture | 不具备通用 wildcard/sc/custom prefix hook | 不是其职责 | 用户授权与 backend 支持决定 |
| GNOME extension | Shell accelerator | 不提供通用完整 hook 流 | 声明范围有限 | 不是通用注入器 | owner 管理不等于 Windows hook parity |
| evdev / inputd | 不依赖窗口快捷键注册 | raw physical stream；字符另需布局/IME | 取决于权限与 replay readiness | uinput/broker | 最高安全风险，也是原生全局输入的重要路径 |
| libei / RemoteDesktop | 不是 hotkey capture | sender 不解决全局捕获 | 不解决全局 suppression | consented injection | EIS 授权、设备能力、暂停/撤销决定可用性 |
| XDG InputCapture | 本快照未找到完整集成证据 | 不能从 libei sender 推导已支持 | 应单独设计与验证 | 与 RemoteDesktop 区分 | 不是本项目当前通用 hook 的既成证明 |

依据 [S18–19, S22–27, S35–37, S62]。Portal/GNOME 允许 compositor 消费其注册快捷键，仍不等于能抑制任意 physical stream。

## 7.2 per-hotkey mux
当前 `CapsSatisfy` 确实检查 suppression、wildcard、physical sc、keyup、combo、level 等需求；因此旧的“完全不做能力选择”不能沿用。但它主要对静态 backend 描述检查，实际 grant、degraded health、replay 是否可用还在其他状态里。client 在无 SUPPRESS grant 或 v1 fallback 时将 subscribe 的 suppress 清零，是具体的跨层契约断点。[S17–18]

需要区分三类用户结果：**clear error** 最安全；**explicit adaptation** 必须告诉用户原键仍会到目标；**silent weakening** 对 remap/导航层很危险。本审计确认代码存在静态/动态能力分裂，尚未端到端证明每种调用最终都无告警，因此不把“所有路径静默降级”写成已证事实。

推荐：`effective_caps = implementation ∩ current_grant ∩ current_devices ∩ replay_health`；每个注册固定 required semantics；generation 变化时原子撤销旧 route，重新协商、验证后再启用。没有合格 route 就 fail-open 并报告精确错误，而不是为了 callback 能触发牺牲 suppression。

## 7.3 normalized event 是否唯一事实来源

| 字段/语义 | 当前观察 | 评价 |
|---|---|---|
| version/backend/source/origin | event/schema 与 trace 有定义 | 并非所有来源同样权威 |
| physical code / logical VK / sc | 新 key model 有明确区分 | local replay 仍将 rawcode 转 VK 再注入 |
| text | event + IME commit queue | commit 不完整经过同一个 PipelineAccept；单 struct 不是唯一事实来源 |
| down/up/repeat | event + reducer | 跨设备相同 key 的 down 可能与 repeat 混淆 |
| timestamp/sequence | 多时钟/sequence 来源 | 不能把 X 时间、单调时间、broker order 当成同一 clock |
| device/seat | 字段存在，broker identity 改进 | local slot identity、seat 位图不足以替代 per-device state |
| synthetic/script/SendLevel | broker envelope 相对权威 | X11 device/time-window 分类不能提供等价认证 |
| InputLevel | consumer/registration policy | 不是物理事件自带的统一属性 |
| generation/session | context 分离记录 | X11 有固定 generation；IME 不是同一 session reset 契约 |
| suppression owner / transaction | decision/context 中记录 | 不在所有 backend 以同一方式保留 |

依据 [S17, S19–23]。一个重要反证检查：曾怀疑 inputd generation 变化会残留 combo；继续追踪后发现 `Disconnect()` 调用完整 domain reset，因此**撤回“普通 broker reconnect 必然残留旧 combo”这一结论**。自动 generation 分支的不同清理范围值得测试，但不是已证普通重连 bug。

## 7.4 scan code / layout

| 范围 | 最佳证据 | 仍缺什么 |
|---|---|---|
| X11 sc01E、AZERTY/QWERTY logical switch | 独立 XTEST + Xvfb `-noreset` fixture；物理 sc 不随字符改变 | 真实多 layout、runtime group switch 的长时交叠 |
| vkXX / key names | key model + doccheck | VK 的 Windows 意义与 Linux keysym 不可机械等价 |
| LR modifiers / wildcard | XI2 side tracking、special oracle | 多物理设备、远程桌面、其他输入注入者交叠 |
| AltGr/Level3 | 独立 EuroSign receiver | dead key、compose、复杂 OEM/numpad/media 组合 |
| evdev / inputd | set-1↔evdev mapping | local replay 不能重新走 layout-dependent VK；原始 repeat 保留 |
| native Wayland | compositor/keymap/权限相关 | GNOME/KDE 实际 layout、consent、native target |
| libei injection | keymap/capability/设备协商 | 真实 portal 的非 US、dead key、held layout change |

**物理 sc 的捕获已改进，但“捕获 sc 正确”不能证明“回放 sc 仍正确”。** [S13–14, S25–26, S42]

## 7.5 custom combo / remap coverage matrix

| 语义 | 当前测试/实现证据 | 官方 Windows v2.0.26 对照 | 判断 |
|---|---|---|---|
| a & b | pipeline combo | 未见该 oracle 的现场 Windows trace | 有集成验证，不是 O5 |
| a & b up | 有 key-up 分支 | 未建立完整对照 | 需要外部输入 + target event 序列 |
| ~a & b | 有 passthrough case | 未见完整对照 | 测 prefix 是否只通过一次 |
| *a & b / modifier prefix | 有 modifier/pipeline 相关案例 | 部分 wildcard 的 O5 是普通 hotkey，不是完整 combo | 不可借普通 wildcard 证明 combo parity |
| shared prefix / prefix alone | 已有 dedicated cases | 无完整对照 | 比早期仅 a&b 成熟 |
| prefix hold / autorepeat / wrong second | 已有部分案例 | 未建立 timing/交叠基线 | 仍需物理 repeat rate |
| overlapping combo / rapid rollover | 未找到充分独立矩阵 | 缺 | 验证缺口 |
| scan-code prefix | 有 dedicated case | 缺完整跨 layout 对照 | 捕获链有进展 |
| HotIf + combo + remap | 分模块有测试 | 缺组合差分 | 最值得增加的语义中心场景 |
| multi-script / multi-keyboard | broker 部分仲裁；不同测试 | 缺完整对照 | 不应生产背书 |

已有 combo oracle 运行 active/mirror/legacy，并覆盖多个分支；代码注释里的 “Windows golden” 不能替代有版本/hash 的官方运行产物。[S41, S46–47]

# 8. libei、portal、Wayland recovery 与桌面拆分

## 8.1 libei 成熟到哪层
`input_backend_libei` 不是占位实现：存在 RemoteDesktop 会话建立接口、seat/device capability、keymap/resume、按键/指针发送、暂停/设备替换、held state cleanup、generation 和失败状态。独立 libeis receiver oracle 包含延迟设备、pointer-first、capability rejection、replacement、mid-press pause 等。应给 **O4/E2/F3（CI-summary 绑定）**，不是 O1。[S26, S36]

但调用 liboeffis 建会话不等于 real portal 链路已验收：

| 链路 | 当前最佳证据 | 未闭合环节 |
|---|---|---|
| CreateSession→SelectDevices→Start→ConnectToEIS | 源码/库调用与 harness | 真实 GNOME/KDE consent UI、deny/retry |
| seat/capability/keymap/resumed device | 独立 receiver | 不同 compositor 的实际设备组合 |
| inject→pause→replace→disconnect | 独立 receiver fault | 真实应用最终字符/点击与 modifier cleanup |
| revoke→reauthorize | terminal/failure 路径 | 用户重新授权恢复、无 storm |
| restore token/persistence | 未找到完整 production workflow | logout/login、portal restart、token 失效/撤销 |
| text injection | 受 libei 版本及 capability 约束 | 各发行版实际库版本/目标应用行为 |

本地 independent EIS receiver 不是 GNOME/KDE portal E2E。更不能据此推导任意全局 Hotstring/InputHook capture 已完成。InputCapture 仍是独立的实现/验证项目。

## 8.2 compositor restart
headless sway oracle 杀死并重建 compositor，保持同一 AHK PID，并由 sway 自己的绑定 marker 确认恢复后的注入有效。这是**用户可观察 outcome 的有效证据**，不只是 socket 重连。[S37]

但该 lane 明确 unset DISPLAY、关闭 libei：没有覆盖 Xlib、真实 portal session、EIS 授权、clipwatch、tray、GTK GUI 同时存活。`core_win` 的 XIO handler 返回 0 并不能让 libX11 安全继续。本审计的独立 Xvfb probe 实际得到退出码 1；这直接反驳“安装了返回型 handler 就能恢复”的系统假设，AHK 整体复现仍需实际二进制测试。[S27, S30；本地 probes]

恢复验收应检查注册只有一份、旧 generation 被丢弃、原本 held 的修饰键有确定处理、没有重复 callback、clipboard/tray/GUI/portal 各自恢复；不能只看重新连接计数。

## 8.3 桌面分开定级

| 工作流 | GNOME Wayland | KDE Plasma Wayland | wlroots / sway | 其他 compositor |
|---|---|---|---|---|
| 普通 launcher hotkey | extension/portal 可行，VM 部分记录 | 实现路径存在，缺真实 KWin 授权证据 | protocol/evdev 可行，headless sway 最强 | 未验证 |
| suppressing hotkey / combo / remap | 不能靠 accelerator 冒充 hook；需要有效 evdev/inputd 路径 | 同样需要独立能力/权限验收 | 特权 capture 可实现，物理安全未 qualified | 未验证 |
| Hotstring / InputHook | XWayland + IBus 有价值；native arbitrary capture 非已证 | real desktop 缺 | 取决于 evdev/inputd/字符模型 | 未验证 |
| Send / pointer | libei 源码与 receiver；真实 consent gap | real portal/libei gap | virtual keyboard/pointer headless 协议较强 | 不可由 sway 推广 |
| clipboard / watcher | text 工作流；native rich 缺口 | Qt native、manager/bridge 未证 | data-control/bridge 因 compositor 配置而异 | 未验证 |
| IME | IBus libpinyin GTK-XWayland VM | real Fcitx/Qt native 未证 | real host 中文链路未证 | 未验证 |
| Win* / AT-SPI | X11 窗口与可访问应用路径；不是通用 native Win32 | real KWin/native Qt 工作流缺 | 部分 compositor protocol 与目标应用决定 | 未验证 |
| tray / GUI | GUI/部分 VM；Shell 扩展与托盘环境另判 | real SNI 缺；不是“代码没有” | 协议与桌面配置决定 | 未验证 |
| session/extension/compositor restart | 有组件级测试；完整链路缺 | 完整 host gap | headless same-PID restart 已验 | 无证据 |

GNOME 结论：**advanced technology preview / selected workloads**。KDE：**verification gap 很大，不应自动归因 implementation missing**。wlroots：**protocol 路径明显更成熟，但只对已测 sway/headless 设置有效**。XWayland：**X 子域实用，不是对整个 Wayland 桌面的兼容层承诺**。


# 9. Clipboard 是一致性系统，不只是字符串 API

## 9.1 已实现与缺口
X11 ClipboardAll 的多 MIME、checksum、strict UTF-8、MIME validation、大小限制及 INCR 接收都是真实代码改进；不能继续套用“只有文本”的旧评价。**但 rich 能力没有自动延伸到 native Wayland；临时 paste 保存/恢复的仍是文本，不是 rich snapshot。** [S28]

| 数据/场景 | 当前判断 | 所需验收 |
|---|---|---|
| text/plain、empty、中文 | 常规路径较成熟 | 空与“不存在”分别测试；目标文本逐字节核对 |
| HTML / PNG / URI list / custom MIME | X11 snapshot 有实现 | 外部 owner→AHK→外部 consumer 各 MIME hash 相同 |
| native Wayland rich | 当前路径文本化 | 实现多 MIME offer/snapshot/restore 或明确报不支持 |
| huge data | 序列化边界存在 | 获取时累计内存/时间预算；不能读完全部 formats 才拒绝 |
| malformed MIME / UTF-8 | 严格校验有改进 | port decoder 的直接负例，而非只在上游库被拒绝 |
| slow owner / owner exit | 超时/失败处理存在 | 禁止将陈旧内部缓存冒充新快照成功 |
| INCR | 接收支持 | 大 payload 发送路径也须分块，不能以收端证明双向 |
| owner 在多 MIME 读取间改变 | 未见事务级 snapshot pinning 的充分证明 | 全 snapshot 属于同一 generation，否则放弃/重试 |
| temporary paste / cancellation | owner 检查与取消逻辑存在 | 保留 rich 数据，且不得覆盖期间的新复制 |
| 两个 AHK script 同时 paste | 未充分验证 | 仲裁、撤销、timeout、counterpart crash 的确定结果 |
| compositor restart | 组件逻辑存在 | offer/owner/watch 全部作废与重建，无 stale pointer |

## 9.2 CAS race：本审计实际验证
顺序：用户 A→AHK 放 B→AHK 检查“仍是我的 owner”→独立进程复制 C→AHK 恢复 A。因为检查和写入分离，最后 C 被覆盖。随附 probe 用两个独立 X11 连接/进程和 barrier 产生确定交错，日志 `check_was_ours=1; foreign_copy_before_restore=1; foreign_copy_preserved=0`。**这证明系统操作不是 CAS，不是已经运行整个 AHK 的声称。**

owner identity 不等于 ownership generation；同一窗口的新复制还会产生 ABA。建议使用可由服务器验证的时间戳/generation 条件、内容和 owner 生命周期追踪；无法保证原子保护时采取保守“不自动恢复”，不要长时间抓住整个 X server 来模拟锁。目标验收：在每一个检查/写入交错点插入外部复制 C，最终均不覆盖 C；两脚本、超时、取消、owner exit 同样成立。

## 9.3 X11 ↔ Wayland bridge 三层责任
A：port 拥有 X selection；B：port 拥有 Wayland offer；C：compositor/clipboard manager 桥接。C 不存在不自动是 port bug，但用户必须知道跨目标会失败。

| 方向 | 责任边界 | 当前审计结论 |
|---|---|---|
| X app → Wayland app | compositor/manager bridge | 不能由 X11 roundtrip 推导通过 |
| Wayland app → X app | 同上 | 需逐桌面真实应用测试 |
| AHK X11 → native Wayland target | port A + C + target | XWayland 在场也不保证 rich 全格式同步 |
| AHK Wayland → X target | port B + C + target | 同样需要 native offer 和 bridge 验证 |

建议用两个非 AHK 应用先做环境基线；基线失败归环境能力，基线成功而 AHK 路径失败再归 port。

# 10. IME、Unicode、线程和 listener 生命周期

## 10.1 IBus 的真实进步
真实 IBus/libpinyin oracle 在 GNOME VM 中运行 GTK Entry，**目标明确是 GTK IBus 模块 + XWayland/X11**。它不只是“能打你好”：检查 Hotstring 命中、InputHook OnChar、preedit decoy、取消、Backspace、目标文本和 callback 内状态。IME commit 现在排队到安全 dispatch 点，并持有必要对象引用；所以不能把早期递归 D-Bus 崩溃作为已确认仍然存在的当前问题。[S23, S29, S43]

| 生命周期 | 当前最佳证据 | 判断 |
|---|---|---|
| preedit start/update→commit | real IBus GTK target | 有实际证据 |
| cancel / Backspace | IBus oracle | 已覆盖，不应继续声称没做 |
| 一次 commit 只触发一次 OnChar/Hotstring | 有针对性 oracle | 值得肯定；不同字长/多脚本仍需扩大 |
| callback 内 ImeStatus | 真实 IBus oracle 与新 queue | 旧源码指针问题已改善 |
| focus/application/engine change | 有状态追踪 | 完整切换矩阵未取得 |
| 多次 commit、候选选择、长 composition、中文/英文切换 | 部分路径 | 不能由 nihao+Space 单条推导全覆盖 |
| listener teardown / framework disappear/reconnect | 需要专项 fault suite | 不能用 input TSan lane 替代 |

锁的存在不是 race-free 的证明。需要测 status query 与 state update 交错、callback active 时 shutdown、DBus owner change、nested call、snapshot buffer lifetime。当前没有足够证据把这些疑点写成可利用 UAF 或必现 race；应保持 **Requires runtime validation / Hypothesis**。

## 10.2 Fcitx5 分层
protocol implementation：有。独立模拟 D-Bus CI：有。real daemon、真实候选/preedit、native Wayland app、中文整段输入、Flatpak app：未取得充分当前证据。**这是 implemented-but-insufficiently-proven，不是“Fcitx5 完整支持”，也不是“完全没做”。** [S44, S53]

## 10.3 Unicode corpus 覆盖
仓库 corpus 包含 NFC/NFD、CJK、Devanagari、Arabic、skin tone、variation selector、ZWJ family、regional indicator 和 supplementary plane；比“你好”成熟得多。但 oracle 主要检查若干 trace code values 和一次 ZWJ Hotstring，而不是全部模块的完整文本等价；非法 UTF-8 可在 producer 的 libdbus 层被拒绝，这不能验证 port 自己的 decoder。补充平面在 trace 中允许 surrogate halves，也不能称为统一 UTF-32 scalar 契约已验证。[S63]

| corpus | InputHook/Hotstring | SendText | Clipboard | GUI | D-Bus/UTF-8 |
|---|---|---|---|---|---|
| BMP 中文 | 有 IBus/协议证据 | 常规文本路径 | 常规路径 | GTK 文本 | 有 |
| emoji / supplementary | 协议 trace 部分 | 未见逐目标完整 matrix | 需字节等价 | 渲染/编辑未充分 | 有部分 trace，不等于 codepoint 契约完整 |
| combining / decomposed | corpus 有 | 未充分 | normalization 保留待验 | caret/delete 行为待验 | 部分 |
| ZWJ / VS16 | 一次完整 hotstring match | 未充分 | multi-MIME/roundtrip待验 | grapheme 编辑待验 | 部分 |
| Arabic/Hebrew | Arabic corpus 有，Hebrew未充分 | bidi outcome 未充分 | 原文保存待验 | bidi/caret未充分 | 部分 |
| newline / mixed-language / long text | 完整生命周期矩阵缺 | 长文本与 paste 风险 | 大数据/超时需验 | IME/focus/reflow需验 | 长 commit 截断与状态 query需验 |

建议同一 corpus 按 **bytes、Unicode scalar、grapheme、目标控件最终字符串、事件次数**分别比对；不要为了让测试通过，将编码层的 surrogate fragment 解释成完整 scalar 语义。

# 11. Win*、Control*/AT-SPI、GUI 与开发工具

## 11.1 Win* 的环境边界

| API 范围 | X11 | XWayland | GNOME/KDE native Wayland |
|---|---|---|---|
| WinExist/WinActive/WinGetList | EWMH/Xlib 路径、oracle | X 窗口子集 | 无证据证明与 Win32 同等枚举/激活 |
| title/class/PID/process | X 属性及进程映射 | X 目标有效 | 原生 compositor/AT-SPI 提供的信息与身份模型不同 |
| WinGetText | target/X/AT-SPI 路径决定 | toolkit 决定 | 不是可以读取任意窗口文本的通用接口 |
| move/resize/activate | WM 请求与目标策略 | compositor 接受程度 | 没有跨 GNOME/KDE 通用 Win32 对等行为 |
| minimize/maximize/restore | X WM 支持范围 | X 子域 | 需各桌面后端/用户策略 |
| close/kill | window close 与进程终止不同 | 同左 | process kill 可实现，不等于 WinClose 语义 |
| always-on-top/transparency | WM/compositor 属性 | 部分 | 不可从 API 存在推导 native 任意窗口可控 |
| WinWait/Group* | 建立在枚举/条件上 | X 子域 | 上游枚举不足会传递到 wait/group 语义 |

本审计确认 EWMH 有实际路径，不把 API 目录当成完备证明。native Wayland 缺少统一跨桌面窗口操控接口属于**部分平台边界**；没有实现已存在的桌面扩展、错误提示不足或把 success 当成动作成功，则属于 port 问题。[S30–32]

## 11.2 Toolkit × accessibility interface
`✓`=记录中有具体目标效果；`△`=部分/元数据；`?`=未找到充分证据；`生态`=目标自身不暴露/不正确实现接口。VM 记录 freshness 未统一绑定 HEAD，不自动记 F3。[S32, S35, S45]

| Toolkit / app | Text / EditableText | Action | Selection | Value | Table | caret/tree/list/checkbox/combo 等 |
|---|---|---|---|---|---|---|
| GTK3 | ✓ | ✓ | 部分 | ? | ? | 标题作用域/编辑控件有证据；非全接口 |
| Qt6 | ✓ Unicode | ✓ | ✓ QList | ✓ slider target marker | ? | list/slider较强，其余逐控件待验 |
| Java Swing | ✓ | ✓ | ✓ JList | 生态：advertised writable 但忽略 Set；port 用 readback 报 EIO | ? | 不能把 target Value bug 计作核心 port 实现缺失 |
| LibreOffice | 部分 | ✓ CSV Import OK | ? | ? | △ Calc 巨大表格 metadata；virtual cells 不完整 | 宏观 table dimensions≠单元格自动化完成 |
| Firefox | ? | ? | ? | ? | ? | 未找到与上述 host matrix 同等级的完整流程证据 |
| Chromium | ? | ? | ? | ? | ? | 同上 |
| Electron / VS Code | 可访问 document 存在 | 部分 | ? | ? | ? | Monaco source text 仅 U+FFFC 等占位：目标 accessibility tree 边界 |

有界 pending-call、WinTitle 作用域、cache/GetChildren fallback、独立 Qt 控件改变和 Java readback failure 是真实成熟度，不应被忽略。但“ControlClick 返回成功”必须由应用状态变化证实。

## 11.3 Syntax Studio 是集成负载，不是 production GUI oracle
HEAD 示例确实组合了 ListBox、TreeView、Edit、DropDownList、StatusBar、按钮、文件/进程执行、定时器、事件和练习代码编辑，足以显示 GUI 已超出简易窗口 demo。[S59–60]

需要降格解释的部分：CI guard 查源码形状和图片文件，不点击 UI；截图只证明记录时布局可显示。`+Resize` 加固定坐标不是自动响应式布局；未见对应 Size 事件统一重排。50ms selection watcher 加 event timer 的组合需要检查重复刷新、快速筛选、销毁时未执行 timer；IME/focus、child process failure、并发实例临时文件等尚无完整独立 UI oracle。不能由有图推导所有练习在本 HEAD 自动验收过。[S59–61]

## 11.4 VS Code / DBGp / DAP

| 能力 | 当前证据与边界 |
|---|---|
| VSIX install/activation/syntax/run/stop/tasks/diagnostics | 扩展实现 + extension-host oracle + VM 记录；HEAD CI npm/package 不等于完整 host oracle 每次必跑 |
| breakpoints/continue/step/stack/scopes/variables/paging | 外部 DBGp IDE 与 DAP client 驱动真实 runtime，是有价值的集成证据 |
| evaluate | adapter 用 property_get 名称查找；不是完整任意表达式 evaluator |
| exceptions | caught/uncaught filter 与部分 exception stop 测试；并非全部异常对象/边界 |
| pause | 有 idle/tight-running 路径；idle frame 状态明确，不应伪造用户栈 |
| detach/reconnect/IDE crash survival | 有同 PID 外部断开重连测试，不能继续称“未考虑” |
| VM visual smoke | 看得见 IDE/调试界面，不证明协议全部合法 |
| SetVariable/restart/完整 exception info | capability 中并非全支持；API 不支持应明确呈现 |

开发工具成熟度高于整体 runtime 的最弱环节，但工具好用不能替输入安全或全天稳定背书。[S54–58, S35]


# 12. Windows differential、SUPPORT_MATRIX 与 release gates

## 12.1 Windows differential coverage map
Windows baseline 是官方 v2.0.26 x64，记录 archive/executable/trace hash，2026-08-25 收集并有三次一致运行；Linux 使用独立 XTEST producer，而不是让 port 自己 Send 给自己。这是有价值的 O5。**Windows fixture 是 F1；HEAD Linux comparison 是 F3；不能把两端都写成现场 HEAD Windows run。** [S46–47]

| Semantic area | 当前 Windows oracle cases/范围 | Linux env | 外部输入 | 比较的 observable | 覆盖判断 |
|---|---|---|---|---|---|
| language/runtime | 0 个广泛语言差分 corpus | — | — | 无完整值/异常/对象序列比较 | 大缺口 |
| ordinary hotkey | a/Shift+a、Ctrl+F11 等少量轨迹 | Xvfb | 是 | callback/trace | 已覆盖窄行为 |
| repeat / key-up | F12 up 等，非完整 repeat-rate 矩阵 | Xvfb | 是 | phase/callback | 部分 |
| wildcard | F10 + Shift 场景 | Xvfb | 是 | trigger | 不能推广全部 modifiers/layout |
| tilde | 未见完整独立官方 case | — | — | — | 缺 |
| custom combo / remap | 当前 pipeline tests 不等于官方差分 | harness | 部分 | 项目定义预期 | O5 缺 |
| A_ThisHotkey / A_PriorHotkey | ThisHotkey 及相关本地场景 | Xvfb | 是 | 字符串/trace | PriorHotkey 全序列对照不足 |
| HotIf | 没有完整官方动态条件 matrix | harness另有测试 | 部分 | deadline/fail-open | O5 缺 |
| Hotstring | B0*、大小写正反、inside-word正反、O+space | Xvfb | 是 | trigger/name/text 相关轨迹 | 当前较集中的 O5 区域 |
| InputHook | F13 的 down/char/up/VK/SC | Xvfb | 是 | 有序事件 | 远非完整 InputHook lifecycle |
| Send / SendLevel/InputLevel | Linux 有外部 receiver/policy，官方面不广 | Xvfb/harness | 部分 | 部分事件 | O5 仍窄 |
| timing / errors / GUI ordering / window behavior | 未见系统性官方 case | — | — | — | 大缺口 |

当前约为 **9 个逻辑 scope、7 组场景**，不能按计数换算 Windows 兼容率。语义族集中在若干键盘触发与文本捕获；相对于整个 AHK 语言、GUI、Win/Control、错误和时序 surface，只能说是**低个位数比例量级的覆盖直觉，置信度低、没有可审计分母**。本报告不把它当统计百分比。

### Oracle contamination
独立 XTEST 输入是优点；Windows fixture 的 hash/版本钉住优于手工预期文件。剩余问题：fixture 的 OS build/layout/采集代码版本信息不够完整；历史 trace 不会自动暴露 Linux 新架构未覆盖的语义；pipeline 三模式相同不代表三者都等于 Windows；官方采集需周期性重跑并保存原始输出，不能只保留“expected”摘要。

## 12.2 SUPPORT_MATRIX 实际统计
冻结文件有 **24 行：pass 5，skip 3，not-run 16**；全部环境标签是 x11。5 个 pass 是 clipboard roundtrip、high-frequency input、basic hotkey、This/Prior、long-press；3 个 skip 是 CapsLock remap、Flatpak target、KDE SNI。没有“unsupported”明确行。[S12]

| 分类 | 可确认数量 | 解释 |
|---|---:|---|
| pass + 可证明 real desktop/physical host | 0 | 表没有足够的 host/commit provenance |
| pass + x11 环境 | 5 | 与 CI Xvfb 执行方式相符，但表自身未绑定生成 run |
| not-run | 16 | 当前表未跑；不能断言历史从未执行 |
| skip because environment unavailable | 3 | 明确不能计成兼容性通过 |
| unsupported | 0 个明确标签 | 不表示没有不支持的功能 |
| scenario exists but execution 未取得 | 至少上述 not-run/skip | 不可计成 verified |

矩阵标注由 runner 生成，但未给可核实的生成 SHA/run/time。**文件位于 HEAD 不等于结果是 HEAD 新跑的。** 此表也不囊括所有外部 oracle；不能反向声称项目只有五个测试通过。

## 12.3 Release gate 与 HEAD gate

| Gate | 当前工作流/公开运行证据 | 能保障什么，不能保障什么 |
|---|---|---|
| core/headless/doccheck | 直接调用；Release tag、HEAD run 成功 | 有限回归；API assert 不等于真实目标行为 |
| ASan | 独立 build lane | 已覆盖路径的内存错误；不是全桌面/全插件 fuzz |
| TSan | 窄输入相关 lane，libei 关闭 | 不能背书 IME、所有 D-Bus、libei 线程 |
| Windows differential | HEAD 调用 Linux trace comparison | 窄历史 fixture 的差分 |
| Wayland / XWayland / no-XWayland | headless lanes，required behavior 修正 | compositor protocol，不是 GNOME/KDE 完整环境 |
| libei | independent receiver | sender/protocol，不是 real portal consent |
| inputd protocol/injection/arbitration | 直接命令 + source guards 混合 | 每条 oracle 需看 producer/receiver；不能统称实机 |
| distro containers | 多发行版 build/pack acceptance | 依赖/ABI/启动；不是所有桌面 UI |
| packages / checksums | install/run/remove 与 checksum checks | 主要 deb/tar；其他包不同深度 |
| attestation | tag 条件路径；branch 跳过 | 能加强产物来源，不能证明功能 |
| OpenPGP | 有签名则 pin fingerprint；否则明确 unsigned | 不能把“unsigned 被显式声明”写成有签名 |
| VSIX | npm test/package + raw DBGp/DAP | extension-host oracle 不是当前必跑 gate |
| Syntax Studio | HEAD 新增 source guard | 不是独立 UI 行为 gate |
| long soak | 30 秒 CI | 不是 24h release guarantee |

**哪些失败真正阻止 Release？** 已确认直接命令失败会让所属 job 失败；package job 依赖 build/TSan。未核实分支保护、人工发布权限或独立发布规则是否要求所有 job 成功，且不同 job 不全是 package 的依赖。因此只能证明“这些 failure 阻止相应 CI/package 路径”，不能证明维护者无法手工发布 Release。手工环境 lane、SKIP、未接入的 extension-host/physical/KDE/24h 显然不是目前可靠 release blocker。[S04–07]

# 13. Security、packaging 与升级兼容性

## 13.1 安全边界
| 边界 | 当前正面证据 | 剩余风险/验证 |
|---|---|---|
| inputd socket / peer | root:input 0660、SO_PEERCRED、共用 v1/v2 policy | active session/seat、普通用户授权 UX；input group 本身是敏感权限 |
| malformed / stalled clients | frame bounds、capability deny、状态/sequence、非阻塞/配额等 | fuzz 每个 transition；输出被打断时 target 的 held state |
| synthetic provenance | broker stamping 比客户端自报更强 | X11 heuristics 不具同级认证；不能以 SendLevel 当安全隔离 |
| D-Bus | 关键路径 bounded pending calls / reentrancy guards | service name owner 更替、恶意签名/结构、spoofing，各接口需 audit/fuzz |
| GNOME Shell extension | sender 绑定 registration、owner scoped unregister/clear、owner disappearance cleanup | batch/resource quotas、Shell restart 与版本矩阵 |
| packed resource | 有 footer/资源格式与部分边界 | 缺文件导致向量不同步、整数加法溢出、错误路径需要修 |
| sandbox / deployment | manifest 与 service hardening | manifest 存在不代表拥有 host input/IME/AT-SPI 所需接口 |

未发现足够依据声称当前存在可利用的跨 UID privilege escalation。相反，v1/v2 统一授权和 GNOME sender ownership 是明确改善。**安全评价不等于“没发现 exploit，所以安全”。** [S15–17, S32, S34, S49–53, S62]

## 13.2 --pack 新识别的源码问题
`LinuxPackExecutable()` 读取资源时只为成功读取者 push `res_data`，但之后按全部 `sources.size()` 索引 `res_data[i]`。任一 FileInstall 源缺失/读失败会破坏一一对应，产生越界访问；读失败分支还存在重复 close。footer 的 `slen+rlen` 和后续长度运算需使用无溢出的减法式检查。**这是当前源码可确认的问题，不是本审计已运行完整 pack 二进制的声称。** [S34]

另外，libei-enabled runtime 的 portable pack 使用同版本 **libei-off template**。这能解决 DT_NEEDED portability，但 packed executable 不自动具有原 runtime 的 libei 能力。需要在 pack 时记录/提示 capability loss，并测试依赖该能力的脚本明确拒绝，而非部署后才失败。

## 13.3 包格式逐项
| 格式 | 当前证据 | 部署判断 |
|---|---|---|
| deb | build、内容、install/run/uninstall、pack acceptance | 最有实用依据；真实桌面权限/service 升级仍需测试 |
| RPM | 产物/内容/依赖、container相关验证 | 不等于真实 Fedora/KDE 桌面完整工作流 |
| tarball | install/update/remove、文件保留/清理测试 | useful；自定义位置、root service 仍需验 |
| AppImage | 提取与 bundled library/template 检查 | portable UI/runtime 不自动安装 privileged integration |
| AUR | PKGBUILD **仍 pin linux.18**，checksum SKIP | 不能作为本 HEAD 的部署证据；要升级 recipe、校验和与 clean makepkg |
| Flatpak | manifest **仍 pin linux.20** | 没有完整 host inputd/uinput/IME 访问验收；不是已证明可用的 sandbox 产品 |
| --pack | 正常资源路径 acceptance | 先修 missing-resource/overflow；明确 libei-off |
| VSIX | 0.2.1 package、protocol、host/VM records | developer tooling 可用；version upgrade/IDE crash 需独立 gate |

Flatpak manifest 的 X11/Wayland、portal、a11y、DRI 权限不等于 `/dev/uinput`、broker socket 和所有 IME framework 可访问。它目前表达的是部署意图，不能推导 sandbox 中任意桌面自动化可行。[S07, S34, S51–58]

### 升级/降级
现有 package “update”测试含**同版本重新安装**，不能等于 linux.20→21 的完整升级。v2 handshake→v1 fallback 是 wire compatibility 设计，但新 client 在 v1 路径 observe-only，语义能力会变化。应以真正历史二进制交叉测试：old/new client × old/new daemon × held-key/restart/partial transaction；固定 socket inode、服务路径、配置/env vars、旧文件删除与 rollback；packed executables 单独记录嵌入版本，不能假设系统 runtime 升级会修复旧包内副本。[S17, S51]

**文档结论**：README、linux-port docs、CHECK_REPORT 的旧测试数字漂移已经修复。当前新 drift 是 AUR18/Flatpak20 对比 Release21/HEAD，而不是机械重复旧的“1170 不一致”。[S08–11, S52–53]

# 14. Long-running 与多脚本：目前证据的上限

当前可定位最长记录是 VM **五分钟、1408 matching rounds、warm RSS +148KB**；CI mixed soak 为 **30 秒**。24h 计划被取消，不是一次通过。未找到可核实 1h、8h、24h、72h 成功产物。这证明“尚未给出长稳证据”，不证明一定会内存泄漏。[S35, S48]

最低生产验收不应只看进程没崩。建议外部 oracle 连续记录：
lost/duplicate events、每个 device/key/transaction 的 down/up balance、stuck modifiers/buttons、callback count drift、fd/RSS/CPU/thread count、reconnect count、queue depth、latency p50/p95/p99。将 idle、burst、输入与 GUI/IME/clipboard 并发、compositor/broker 重启、sleep/wake 都纳入。

五脚本一等测试：A global hotkeys；B Hotstrings；C InputHook；D remap/custom combo；E clipboard watcher。加入相同注册冲突、synthetic recursion、InputLevel、不同 SendLevel、kill 任一脚本、broker restart、compositor restart。已有两脚本/raw capture 和 A/B/C broker 仲裁不能替代这套测试，但也不应被说成“没有多脚本测试”。


# 15. 当前 Issue Register

下面的“新识别”不意味着一定由 Release→HEAD 两个提交引入；除非能由提交差异证明，不归因于某个 fix。Confirmed source、独立系统 probe 和完整 AHK runtime reproduction 始终分开。


## I01 — P0 candidate：持有 grab 的进程仍存活但停止时，fail-open 没有独立保证
**Status**：Requires runtime validation（机制明确；未在当前 AHK + 物理键盘复现）  
**Current HEAD evidence**：local evdev 的释放依赖自身执行；inputd 的 SIGALRM 仍在同一进程；systemd unit 没有构成独立输入租约 watchdog。  
**Relevant code path**：core_evdev_linux.cpp；inputd.c 的 on_sig/主循环；ahk-inputd.service.in [S13, S15, S49]  
**Relevant test**：check_evdev_failopen_source、replay_failure、crash/lease oracle；没有等价的物理 holder SIGSTOP 验收。  
**Evidence level**：源码 E0/F3；关联 O2–O3/E2 测试不能升级为此 fault 的 E5  
**User impact**：可能使被抓取设备无法给应用送入正常键盘事件；不能接受作为默认不可绕过的生产输入层。  
**Trigger**：启用物理 suppression 后 SIGSTOP、进程级冻结或无法到达自身恢复代码的故障。  
**Why existing tests might miss it**：SIGKILL 会关 fd，与 SIGSTOP 保留 fd 不是同一个 fault；write failure 测试也不覆盖停止进程。  
**Suggested fix**：将抓取租约与健康监督放到独立执行主体；无外部监督时禁用高风险抓取或保留独立恢复路径。  
**Acceptance test**：两个物理键盘 + 鼠标；抓取者 STOP 后在规定上界内恢复输入；独立目标无 stuck key；CONT 不重复 replay；禁止依赖被测进程发出恢复操作。  
**Complexity**：L（架构、安全与物理测试）  
**Platform-inherent?** partial：EVIOCGRAB 的内核契约是平台属性，缺乏独立恢复是产品设计问题


## I02 — P1：local evdev raw replay 不保真，SYN_DROPPED 与未映射键不走既有修复
**Status**：Confirmed（源码）；具体设备用户效果需 runtime validation  
**Current HEAD evidence**：local loop 跳过非 EV_KEY；经 VK 转换再 replay；未映射 VK 不发送；repeat value 2 被布尔化。  
**Relevant code path**：core_evdev_linux.cpp replay loop；core_uinput_linux.cpp [S13–14]  
**Relevant test**：fail-open source guard；keymodel oracle 测捕获与 Send，不等价 local raw replay。  
**Evidence level**：O1/E0/F3；真实应用重放结果未独立执行  
**User impact**：媒体/OEM 键丢失、repeat 改义、物理布局回放漂移；复合键鼠设备风险需验证。  
**Trigger**：被抓取设备产生未映射 key、SYN_DROPPED、repeat 或复合事件。  
**Why existing tests might miss it**：窄用例只使用常见字母；检测 write failure 不会发现根本没调用 write 的事件。  
**Suggested fix**：以原始 type/code/value 保真转发，显式处理 SYN_DROPPED/不支持事件；不能保真时先解除 grab。  
**Acceptance test**：独立 producer→本地抓取→独立 kernel/应用 recorder，比较包括媒体键、OEM、repeat、SYN_DROPPED 的完整序列。  
**Complexity**：M–L  
**Platform-inherent?** no


## I03 — P1：设备级修复尚未贯穿 reducer 与 replacement/output ownership
**Status**：High probability；seat 位图/单 rule transaction 为 Confirmed source facts  
**Current HEAD evidence**：broker physical 数组已按设备分离，但 reducer held/repeat/keyup owner 不是 per-device 引用计数；replacement rule 保存单一 active 状态。  
**Relevant code path**：input_pipeline.cpp SeatState/Accept；inputd.c arb_rule [S15, S21]  
**Relevant test**：arbitration/combo/remap oracle；未见两设备同码交叠与跨事务同输出 key 的完整 target 验收。  
**Evidence level**：O1/E0/F3；相关 O3/E2/F3  
**User impact**：错误 repeat、提前 key-up、替换 owner 覆盖；modifier snapshot 可修正部分情形，不能泛称全部 Ctrl 必错。  
**Trigger**：两键盘同键交叠，或两脚本/事务输出同一 held key。  
**Why existing tests might miss it**：单键盘顺序事件与多个客户端同时连接不触发该状态别名。  
**Suggested fix**：physical(device,key)、seat aggregate、synthetic(transaction,key) 分层；reference-count/owner set 与 disconnect 平衡。  
**Acceptance test**：A1 down、A2 down、A1 up、A2 up；两种释放顺序都正确；同 B 输出事务分别关闭不提前释放；unplug/crash 同测。  
**Complexity**：L  
**Platform-inherent?** no


## I04 — P1：mux 静态 capability 与真实授权/健康状态存在契约断层
**Status**：Confirmed code split；是否所有用户入口静默降级 Requires runtime validation  
**Current HEAD evidence**：CapsSatisfy 检查静态能力；client 无 SUPPRESS grant/v1 时订阅降为 observe。  
**Relevant code path**：input_backend.cpp CapsSatisfy；core_inputd_client_linux.cpp SendSubscribeFrame [S17–18]  
**Relevant test**：check_input_caps_source；v1/v2 permission oracle；缺 root daemon + 普通用户真实脚本结果判定。  
**Evidence level**：O1/E0/F3，相关 O3/E2/F3  
**User impact**：需要 suppress 的导航/remap 可能只获得 trigger/observe；旧 daemon 混用有语义退化。  
**Trigger**：普通 input 用户连接 root daemon、fallback v1、运行中权限/health 改变。  
**Why existing tests might miss it**：server 正确拒绝并不能证明 client 清楚告诉用户；静态 source guard 不覆盖有效能力交集。  
**Suggested fix**：用 effective capabilities 路由；不满足原语义明确错误/显式 opt-in adaptation，不能自动把 suppress 请求改成普通观察而仍宣称成功。  
**Acceptance test**：新旧 client/server 和权限四象限；每个需要 suppress 的脚本要么获证保留语义，要么明确失败且原输入无损通过。  
**Complexity**：M  
**Platform-inherent?** no


## I05 — P1：clipboard restore 的 owner check 不是 CAS，会覆盖检查之后的新复制
**Status**：Confirmed（独立 X11 契约 probe）；完整 ahk_core 交错复现尚未执行  
**Current HEAD evidence**：检查 ownership 与 SetText/XSetSelectionOwner 分离；两个独立进程实验中 C 被覆盖。  
**Relevant code path**：core_clipboard_linux.cpp paste restore [S28]；随附 clipboard_owner_race_probe.c  
**Relevant test**：pasterestore unit oracle；未模拟真实 X server 上第三方夹入 check/set 之间。  
**Evidence level**：O4/E2；关联 F3 源码模式，不冒充 AHK F3 E2E  
**User impact**：丢失用户最新 clipboard；若剪贴板是唯一数据副本，需按 P0 数据丢失风险处理。  
**Trigger**：AHK 临时粘贴期间其他应用或 script copy；同 owner 新一代内容造成 ABA。  
**Why existing tests might miss it**：unit 比较 owner token 时未把检查与真实服务器写入拆开交错。  
**Suggested fix**：使用可验证 ownership generation/条件；没有可靠原子约束时保守不 restore；内容/owner/generation 协同判断。  
**Acceptance test**：每个交错点插入外部 C，永不覆盖 C；同窗口再 copy、两脚本、取消、timeout、restart 都保持。  
**Complexity**：M–L  
**Platform-inherent?** partial：基础协议无通用 CAS，自动恢复策略由项目负责


## I06 — P1：Xlib fatal I/O handler 返回不能提供重连恢复
**Status**：Confirmed（Xlib 契约及本地 probe）；当前 AHK 整体影响 High probability  
**Current HEAD evidence**：core_win 返回型 XIO handler 与 X.Org 契约冲突；libX11 1.8.12/Xvfb 实验 handler 返回后 exit 1。  
**Relevant code path**：core_win_linux.cpp LinuxXIOErrorHandler [S30, S64]；xio_return_probe.c  
**Relevant test**：m7 Wayland restart 明确 unset DISPLAY、禁用 libei，不覆盖该路径。  
**Evidence level**：O4/E2 + F3 source linkage  
**User impact**：X server/XWayland 连接断开可能直接终止依赖它的 runtime，而非恢复脚本状态。  
**Trigger**：实际使用 Xlib connection 后 server death；compositor restart 带走 XWayland。  
**Why existing tests might miss it**：pure Wayland restart 回归走不同库；handler 安装的 source assertion 无法验证库退出语义。  
**Suggested fix**：定义 X11 fatal policy：隔离连接/进程或受控退出重启，不能从失效 Display 继续；明确 script state 是否可恢复。  
**Acceptance test**：同 PID 保持或有明示 supervisor 重启契约；独立 X server kill/restart；GUI/clipboard/hotkey 同时运行，无死 Display 与重复注册。  
**Complexity**：L  
**Platform-inherent?** partial


## I07 — P1/P2：ClipboardAll 跨 backend、paste restore、获取预算与 INCR sender 不完整
**Status**：Confirmed source scope；大数据目标失败需 runtime validation  
**Current HEAD evidence**：X11 rich 与 native Wayland text 分支不同；临时 paste 只保存 text；获取/发送生命周期尚未完整受约束。  
**Relevant code path**：core_clipboard_linux.cpp GetAll/SetAll/paste/ServeRequest [S28]  
**Relevant test**：integrity source guard、serializer/roundtrip；缺 rich target 全流程与大 INCR 双向。  
**Evidence level**：O1/E0/F3；部分 O3/E2  
**User impact**：rich MIME 丢失、大 payload 失败、内存/延迟增长；不是单纯显示问题。  
**Trigger**：HTML/PNG 多格式 clipboard 后临时 SendText paste；很多大 MIME；slow/changing owner。  
**Why existing tests might miss it**：文本 roundtrip 与 serialized blob 校验看不到获取前的累计成本和恢复后 formats 缺失。  
**Suggested fix**：统一 rich snapshot 事务；获取时累计 budget；完整 INCR sender；native WL 明确支持或拒绝。  
**Acceptance test**：独立 owner/consumer 的全部 MIME hashes；内存和 deadline 上界；owner change 时整包重试/放弃而非混合。  
**Complexity**：L  
**Platform-inherent?** partial


## I08 — P1：--pack 缺失资源时 vector 对应关系破坏；恶意 footer 算术需加固
**Status**：Confirmed（静态控制流）；未执行当前 runtime pack reproduction  
**Current HEAD evidence**：成功资源才进入 res_data，写 blob 却遍历全部 sources；存在越界和重复 close；长度加法式检查有溢出风险。  
**Relevant code path**：core_pack_linux.cpp LinuxPackExecutable/LinuxPackFooter [S34]  
**Relevant test**：pack/FileInstall acceptance happy path；缺 missing/read-error/malformed-footer ASan cases。  
**Evidence level**：O1/E0/F3  
**User impact**：打包崩溃/错误产物/资源错配；不据此声称已有 privilege escalation。  
**Trigger**：任一 FileInstall 源不存在、不可读，或伪造长度 footer。  
**Why existing tests might miss it**：所有测试资源都存在且尺寸合理；API happy path 无法覆盖失败收集逻辑。  
**Suggested fix**：资源元组单一 vector、fail-fast；checked arithmetic；RAII fd；临时输出+原子替换。  
**Acceptance test**：首/中/末资源缺失、权限拒绝、读取中断、UINT64 边界；ASan/UBSan clean；失败不得覆盖已有输出。  
**Complexity**：S–M  
**Platform-inherent?** no


## I09 — P2：IME 生命周期/Unicode oracle 的断言边界仍不足
**Status**：Requires runtime validation；潜在 race/teardown 是 Hypothesis，不当作已证 UAF  
**Current HEAD evidence**：callback queue 修复真实；status/snapshot/owner churn 与 long composition 没有完整并发证明；Unicode trace 检查窄。  
**Relevant code path**：core_ime_linux.cpp/core_capture_linux.cpp；Unicode oracle [S23, S29, S63]  
**Relevant test**：IBus real GTK、Fcitx mock、state guard；TSan input lane 非完整 IME lane。  
**Evidence level**：O3/E2/F3；O4/E3/F0 VM record  
**User impact**：极端 focus/reconnect/长 commit 下重复、遗漏或状态不一致的风险尚未排除。  
**Trigger**：callback 内 status query、shutdown active listener、framework restart、很长补充平面/ZWJ commit。  
**Why existing tests might miss it**：稳定 daemon + 短文本绕开 lifetime/decoder/codepoint 契约；producer 拒绝非法 UTF-8 可能掩盖 port decoder。  
**Suggested fix**：明确单线程所有权/immutable snapshots、generation-bound listener；独立 byte-for-byte 全 corpus 与线程 sanitizer lane。  
**Acceptance test**：实 IBus/Fcitx、native/Flatpak targets；每个 commit exactly once；cancel 不污染；TSan/ASan teardown clean。  
**Complexity**：M–L  
**Platform-inherent?** partial


## I10 — P2：real portal、KDE、Fcitx/Flatpak 环境证据不足
**Status**：Confirmed verification gap（在本次检索范围），不是一律 implementation missing  
**Current HEAD evidence**：libeis receiver、headless sway、GNOME XWayland 证据不能替代真实 KDE 和 native portal/UI。  
**Relevant code path**：input_backend_libei、desktop/IME adapters；[S26–27, S43–45, S53]  
**Relevant test**：独立 receiver / protocol / VM subset；缺全链路 consent→revoke→reauthorize→persistence。  
**Evidence level**：最佳 O4/E2/F3；目标环境所需 E3–E5 缺失  
**User impact**：普通用户安装后权限提示、发送、剪贴板、输入法、托盘的实际可用性未知。  
**Trigger**：未经验证的 compositor/portal/Qt/native app 配置。  
**Why existing tests might miss it**：private test service 与 headless compositor 没有真实用户授权 UI 和应用安全策略。  
**Suggested fix**：版本化 desktop acceptance farm，保存 compositor/portal/app/library/commit hashes 与原始 traces。  
**Acceptance test**：GNOME/KDE 用户身份完成授权/拒绝/撤销/重启；真实 Qt/GTK target 核对文本/点击；Fcitx native+Flatpak。  
**Complexity**：L（环境投入）  
**Platform-inherent?** partial


## I11 — P2：部署配方与升级语义未形成同一版本契约
**Status**：Confirmed pins/test-scope；所有用户部署结果需 runtime validation  
**Current HEAD evidence**：AUR18、Flatpak20；package update 含同版本重装；pack template libei-off。  
**Relevant code path**：PKGBUILD、org.autohotkey.AHK.yml、verify-packages、pack runtime [S34, S51–53]  
**Relevant test**：container install/run/remove，未建立全部 old/new client-daemon/rollback matrix。  
**Evidence level**：O1–O3/E0–2/F3  
**User impact**：用户以为装 HEAD 实际得到旧版/不同 capability；服务和 runtime 混装后语义改变。  
**Trigger**：AUR/Flatpak安装、跨版本升级/降级、旧 packed executable、privileged service 版本不同。  
**Why existing tests might miss it**：同版本重装不改变 wire/schema/permission policy；AppImage 提取不安装系统服务。  
**Suggested fix**：版本单一来源生成 recipes；真实版本交叉测试；安装后 capability diagnostics；明确 pack feature manifest。  
**Acceptance test**：N−1/N 升级、降级、卸载、运行中 held key、安全 rollback；包安装后的普通用户脚本完整验收。  
**Complexity**：M  
**Platform-inherent?** no


## I12 — P1 evidence blocker：没有五角色多脚本全天运行及组合恢复证据
**Status**：Confirmed evidence gap；不等于已证长时不稳定  
**Current HEAD evidence**：CI 30s/VM5min；完整五脚本、两键盘、recovery schedule、24–72h 产物未取得。  
**Relevant code path**：run_mixed_soak.sh；broker/pipeline/lifecycle [S15, S21, S35, S48]  
**Relevant test**：mixed soak、two-script/oracles 是基础，不是全天 power-user corpus。  
**Evidence level**：O2–O3/E2/F3；VM E3/F0；所需 E5 缺  
**User impact**：无法承诺全天无 lost/duplicate、stuck modifier、资源漂移和恢复误触发。  
**Trigger**：用户每天多小时、多脚本、多 backend，伴随真实桌面生命周期。  
**Why existing tests might miss it**：短时间、固定时序与单负载不能覆盖累积漂移及低概率交错。  
**Suggested fix**：建立可复跑 1/8/24/72h 分层 release qualification，不只看 alive/RSS。  
**Acceptance test**：外部计数无失衡；p99 latency/queue/fd/thread/RSS 受限；定时 kill/revoke/restart/suspend 故障后语义恢复。  
**Complexity**：L（测试与运行维护）  
**Platform-inherent?** no


## I13 — P3：HEAD 教学 GUI 与 VSIX 验证存在 gate/行为边界
**Status**：Confirmed test scope；GUI 交互缺陷需要 runtime validation  
**Current HEAD evidence**：Syntax Studio 是 source/screenshot guard；VSIX host script 不在必跑 gate；evaluate 不提供任意表达式。  
**Relevant code path**：syntax_studio、source guard、debugAdapterCore、CI [S07, S56–61]  
**Relevant test**：source grep、npm/DAP、VM visual；缺完整自动 UI workflow。  
**Evidence level**：O1/E0/F3；O3/E2/F3；VM E3/F0  
**User impact**：教学工具 resize/focus/timer/子进程等未自动证明；用户可能误解调试器能力。  
**Trigger**：快速选课/筛选/resize、IME练习、多实例；请求不支持的 debug 功能。  
**Why existing tests might miss it**：截图不执行生命周期；protocol oracle 不等于每次真实 extension-host 行为。  
**Suggested fix**：独立 UI automation，准确 capability 文档；extension-host gate；事件/定时器统一状态更新。  
**Acceptance test**：安装 VSIX 后实际 break/step/stop/reconnect；教学 GUI resize/筛选/IME/运行失败/关闭无残留进程。  
**Complexity**：M  
**Platform-inherent?** no


# 16. 真实风格 AHK Migration Corpus：15 个验收脚本设计

这是一套**建议验收设计与迁移预判，不是已经执行的 15-script pass rate**。每个脚本都应保留官方 Windows v2.0.26 baseline，并用独立输入 producer 和目标应用 recorder 验证。路径、可执行文件名与目标窗口 class 的 Linux 调整不视为语言语义失败。

U=unchanged；T=trivial Linux adaptation；M=moderate rewrite；R=major rewrite；X=在现有平台/API 下无法 unchanged 实现；?=关键环境未经证明。`†`表示尤其需要安全/可靠性验收，不建议直接作为全天依赖。GNOME/KDE 列按 native Wayland 工作流评估，不借用 XWayland 成功冒充 native 成功。

| Script | 核心语义/目标 | X11 | XWayland | GNOME native | KDE native | wlroots | Migration effort |
|---|---|---|---|---|---|---|---|
| 01 app_launcher.ahk | Ctrl+Alt 热键启动/聚焦应用 | T | T | T/? | T/? | T/? | 改进程路径、窗口身份、权限 |
| 02 caps_navigation.ahk | Caps 前缀 + hjkl，tap-alone | M† | M† | M/R† | M/R† | M† | 需要可靠 suppression、prefix、物理层 |
| 03 vim_remap.ahk | 应用内 remap，modifier保持 | T/M† | M† | M/R† | M/R† | M† | 语义中心路径，不能只比字符 |
| 04 text_expansion.ahk | 多 Hotstring，大小写/终止字符 | U/T | U/T | M/? | M/? | M/? | capture与replacement backend是关键 |
| 05 chinese_expansion.ahk | 拼音commit→中文Hotstring | T | T（IBus证据最强） | M/? | M/? | M/? | 真实 IME/target 生命周期待验 |
| 06 app_hotif.ahk | 窗口条件、失焦立即pass-through | T | T/M | R/? | R/? | M/R | WinActive身份与动态抑制 |
| 07 command_palette.ahk | InputHook collect/end/cancel/GUI | U/T† | T/M† | M/R? | M/R? | M? | native全局capture不可从portal推导 |
| 08 clipboard_history.ahk | watcher去重、选择恢复文本 | T† | T† | M† | M/?† | M† | 并发ownership和重启需要补强 |
| 09 rich_clipboard.ahk | text+HTML+PNG+URI snapshot/restore | M† | M† | R/X | R/X | R/X | 当前native rich缺口；X11 paste也会文本化 |
| 10 window_tiler.ahk | 列表、定位、resize、restore | T | T/M | R/X | R/X | R/M | desktop-specific API，不是通用Win32 |
| 11 launcher_gui.ahk | 控件/resize/搜索/启动/定时器 | T | T | T/M? | T/M? | T/M? | GUI本身较实用，目标/依赖仍需适配 |
| 12 form_automation.ahk | Text/EditableText/Action/Value | M | M | M/R | M/R? | M/R? | toolkit accessibility决定成功 |
| 13 browser_a11y.ahk | 导航、表单、按钮、读回 | M/R? | M/R? | M/R? | M/R? | M/R? | 真实浏览器应用流程缺强oracle |
| 14 editor_automation.ahk | 编辑文本、选区、命令、读回 | M/R | M/R | M/R | M/R? | M/R? | Monaco源码不可由占位AT-SPI文档读取 |
| 15 power_user_suite.ahk | A热键+B扩展+CInputHook+Dremap+Eclipboard | M† | M† | R/?† | R/?† | M/R† | 多脚本仲裁/全天/重启是当前最大综合门槛 |

### 每个脚本必须验收什么
| Script | 驱动/故障序列 | 不由被测 runtime 自己决定的 oracle |
|---|---|---|
| 01 | 连续按、重复按、应用已开/未开、焦点变化 | 进程 PID、目标焦点/窗口数 |
| 02 | 前缀单击、长按、wrong second、rollover、掉线 | 原键是否到目标、方向键精确 down/up |
| 03 | Ctrl/Shift held、remap recursion、目标切换 | 目标实际按键流及选区/文本 |
| 04 | 大小写、单词中、结束符、rapid input | 非 AHK 编辑器最终文本 |
| 05 | preedit更新、取消、候选、Backspace、focus change | IME commit日志 + GTK/Qt target文本 + callbacks exactly once |
| 06 | HotIf true/false交错、耗时条件、deadline | 原键在false/timeout时无损到目标 |
| 07 | cancel/end/timeout、重复启动、GUI关闭、Unicode | buffer字节/事件序列与独立input trace |
| 08 | 外部复制A/B/C、两脚本、owner exit | 不丢最新C、通知不重复、不自激循环 |
| 09 | 多MIME、32MiB边界、slow owner、INCR、restart | 每个format hash与ownership generation |
| 10 | 多窗口、同名、不同PID、WM拒绝请求 | WM/compositor实际geometry/state，而不是API返回值 |
| 11 | 缩放/resize、筛选、键盘焦点、IME、child failure | 独立UI自动化、可访问控件内容、子进程/文件 |
| 12 | GTK/Qt/Java/LO同义控件、readonly/timeout | 各应用自己的marker/readback，明确errno |
| 13 | Firefox/Chromium登录外测试表单、隐藏/disabled元素 | 浏览器DOM或应用自记录结果，不用port自读代替全部oracle |
| 14 | VS Code/其他编辑器同内容、选择、Undo/redo | 保存文件diff、选区、命令结果；Monaco访问缺口需替代渠道 |
| 15 | 24h、五脚本、多键盘、kill/restart/revoke/suspend | 外部事件计数、down/up balance、latency和资源指标 |

### 语义种子（设计，不声称通过）
01 使用 `^!F12::Run(...)` 与 WinActivate；02 使用 `CapsLock & h/j/k/l` 及 prefix-alone；03 使用 `#HotIf WinActive(...)` 和 remap；04 使用 `:*:;addr::...`；05 使用中文 commit 作为 Hotstring 输入；06 在目标窗口切换时测 HotIf；07 用 InputHook 的 OnChar/EndKey/timeout；08 用 OnClipboardChange 去重；09 用 ClipboardAll 保存和恢复；10 用 WinGetList/WinMove；11 用 Gui/ListBox/TreeView/Edit/Timer；12 用 ControlSetText/ControlClick/Value；13、14 明确区分 accessibility 与应用专用 API；15 用独立五进程启动这些负载，不在一个进程内假装多脚本。

# 17. 两种兼容、用户画像与迁移判断

**Semantic Compatibility**：同一 AHK script 在官方 Windows v2.0.26 和 Linux 上产生接近的可观察行为，包含事件、时序、错误、副作用及恢复。  
**Intent Compatibility**：允许改用 Linux 进程、D-Bus、AT-SPI 或 compositor 接口，最终完成同一个自动化目标。  
Windows COM→Linux D-Bus 通常是 semantic incompatible，但可以 intent useful；DllCall 可调用 Linux .so，不代表 Windows DLL 二进制或 Win32 API 参数可照搬。[S11, S33]

| Profile | 当前是否值得使用 | 应限定的使用边界 |
|---|---|---|
| A 语言/文件/进程脚本 | **值得** | 改Linux路径/命令；避开Windows COM/DLL假设；自行验收数据写入 |
| B X11 hotkey+Send | **值得试用并逐脚本验收** | 普通快捷键、目标应用文本、启动器；保留原生热键/退出通路 |
| C X11 power user | **谨慎** | Hotstring/InputHook单流可用；复杂combo/remap/双键盘不作无监督依赖 |
| D GNOME Wayland普通用户 | **选定工作流值得** | launcher、GUI、AT-SPI、XWayland文本路径；别假设所有全局hook可迁移 |
| E KDE Wayland普通用户 | **应谨慎试验** | 实现不等于real KWin/portal/SNI已证明；不宜首选作为全天唯一runtime |
| F wlroots power user | **适合能自行诊断权限和协议的用户** | 已测sway/headless基础好；实桌面与物理层仍要验 |
| G 中文/IME用户 | **IBus+已测GTK/XWayland最有依据** | Fcitx/native/Flatpak/候选复杂生命周期仍谨慎 |
| H Control*/应用自动化用户 | **按toolkit和具体应用选择** | GTK/Qt有限流程有依据；Java Value/Calc virtual cells/Monaco需替代方案 |
| I 多脚本全天power user | **不建议作为唯一、无监护依赖** | 缺24h/多设备/组合恢复证据，且有当前代码问题 |

### Windows AHK v2 脚本迁移“成功率”
没有代表真实用户总体的迁移样本集，不能诚实给出统计成功率。下面是**主导迁移类别**，不是伪造的百分比分布：

| 脚本类型 | unchanged | minor changes | major rewrite | impossible / platform-specific |
|---|---|---|---|---|
| simple language scripts | 纯语言子集常可 | Linux路径/命令常见 | Windows专用集成 | 依赖不存在的Windows组件时 |
| ordinary hotkeys | X11部分可 | 最常见：进程/键名/权限适配 | 原生Wayland特殊语义 | compositor不提供所需能力时 |
| advanced keyboard automation | 不能一般保证 | 仅受控backend | 常需重构捕获/仲裁/权限 | 当前无安全有效capture时 |
| Hotstring/InputHook-heavy | X11部分可 | 目标/IME适配 | native跨桌面常见 | 无全局charstream时不能原样 |
| window automation | 纯X11子集 | X目标窗口标识 | native桌面专用接口 | 无等效控制接口时 |
| Control*/accessibility | 少 | 同接口toolkit部分 | 大量Windows控件/COM脚本 | target不暴露所需数据时 |
| Chinese/IME | 已测狭窄路径可能 | IBus目标配置 | Fcitx/native/复杂生命周期 | 不能据现有证据保证 |
| multi-script power user | 不建议承诺 | 小型无冲突组合 | 生命周期/全局状态通常需要调整 | 全天保证目前不可给出 |

“不可原样迁移”不一定等于“用户意图不可能实现”；应优先把操作改成目标应用 API，而不是不断叠加键盘注入。

# 18. Compatibility Pyramid 与评分

评分基准：0=没有可用路径；50=部分真实工作流可用但主要边界仍在；75=广泛受控场景可用且有独立验证；90=跨环境、故障和升级证据强。Implementation 看实现广度与闭环；Verification 看证据覆盖；Real-world compatibility 看实际迁移与操作成本。非抽样统计，分数之间不宜比较一两分。

| Layer | Implementation | Verification | Real-world compatibility |
|---|---:|---:|---:|
| L1 AHK language/runtime | 91 | 80 | 86 |
| L2 Linux utility runtime | 87 | 75 | 81 |
| L3 basic desktop automation | 84 | 73 | 75 |
| L4 advanced keyboard semantics | 79 | 64 | 58 |
| L5 application/UI automation | 81 | 64 | 58 |
| L6 cross-desktop Wayland | 67 | 46 | 47 |
| L7 multi-script/lifecycle | 74 | 49 | 46 |
| L8 production runtime | 67 | 43 | 48 |

下面的 Evidence confidence 是对“该维度已被充分理解/验证”的综合置信度估计；不是某项源码事实真假的概率。Security、Packaging、VS Code、Long-running 的 Windows semantic score 无统一意义，记 —，不硬造数字。


| Dimension | Implementation | Evidence confidence | Semantic compatibility | Real-world usability |
|---|---:|---:|---:|---:|
| Language/runtime | 91 | 80 | 85 | 88 |
| X11 | 85 | 79 | 75 | 79 |
| XWayland | 80 | 72 | 66 | 68 |
| GNOME Wayland | 67 | 54 | 43 | 55 |
| KDE Wayland | 60 | 29 | 39 | 43 |
| wlroots | 75 | 65 | 53 | 62 |
| Hotkey | 87 | 79 | 76 | 79 |
| HotIf | 79 | 67 | 66 | 66 |
| Custom combo | 79 | 63 | 61 | 56 |
| Remap | 78 | 63 | 63 | 54 |
| Hotstring | 84 | 72 | 70 | 69 |
| InputHook | 80 | 67 | 64 | 62 |
| Send | 86 | 76 | 63 | 75 |
| SendLevel/InputLevel | 78 | 63 | 57 | 60 |
| scan-code/layout | 83 | 74 | 69 | 68 |
| libei | 80 | 69 | 53 | 48 |
| evdev/uinput | 76 | 55 | 65 | 44 |
| inputd | 83 | 68 | 68 | 50 |
| Unicode | 84 | 65 | 73 | 75 |
| IME | 76 | 57 | 56 | 55 |
| Clipboard | 85 | 69 | 65 | 72 |
| ClipboardAll | 76 | 56 | 52 | 57 |
| Win* | 81 | 65 | 56 | 63 |
| Control*/AT-SPI | 80 | 64 | 41 | 58 |
| GUI/Menu | 85 | 67 | 62 | 73 |
| Tray | 80 | 58 | 63 | 65 |
| DllCall | 88 | 68 | 35 | 73 |
| D-Bus/COM adaptation | 82 | 69 | 20 | 74 |
| Multi-script | 74 | 54 | 52 | 47 |
| Crash recovery | 72 | 54 | 51 | 48 |
| Security | 72 | 58 | — | 52 |
| Packaging | 79 | 68 | — | 68 |
| VS Code tooling | 84 | 74 | — | 78 |
| Long-running reliability | 65 | 32 | — | 42 |
| Production readiness | 67 | 48 | 57 | 52 |


DllCall 和 D-Bus 的 semantic 分低，主要是 Windows DLL/COM 的平台语义不可直接移植，不等于 Linux .so/D-Bus 功能质量同样低。KDE 实现分显著高于证据分，刻意区分 verification gap 与 implementation gap。

## 六个总体数字（0–100%，独立估计，非上表平均）
| 指标 | Score | Confidence | ± uncertainty | Top 3 limiting factors |
|---|---:|---|---:|---|
| A API Surface Coverage | 90% | 中高 | ±5 | Linux适配不等于Windows对象；backend分支不同；已登记API不保证闭环 |
| B Semantic Compatibility with Windows AHK v2 | 63% | 中 | ±12 | O5范围窄；hook/Send/窗口平台差异；多设备/生命周期语义 |
| C Intent Compatibility on Linux | 76% | 中 | ±10 | 目标应用accessibility；权限/桌面专用接口；改写与部署成本 |
| D Real-world Desktop Automation Capability | 65% | 中低 | ±13 | native跨桌面不均匀；复杂输入/clipboard风险；真实目标矩阵有限 |
| E Evidence / Verification Completeness | 60% | 中 | ±10 | 物理/真实KDE等缺口；VM freshness；短soak与组合矩阵不足 |
| F Production Readiness | 48% | 中 | ±12 | fail-open与一致性未闭环；全天多脚本未qualified；升级/授权/恢复证据不足 |

A 是工程可用 API 面的估计；仓库登记的 370/370 只能支撑“表面覆盖广”，不是 A 的直接公式。总体 production 48 采用风险门槛，故不必等于维度表各列平均值。


# 19. 当前最大风险、证据缺口与成熟度偏差

## 当前五个最值得担心的问题
1. **物理拦截的最后一层 fail-open**：preflight 已修，但 live-but-stopped holder、raw replay 和已注入 held state 仍可能影响真实输入。
2. **共享状态跨设备/脚本/backend 的语义一致性**：per-device broker 改进尚未贯穿 reducer、replacement 与 output ownership。
3. **clipboard 一致性**：非原子 restore、rich restore 文本化、跨 backend/big-data 行为可能造成用户数据丢失或误恢复。
4. **组合生命周期恢复**：pure Wayland restart 通过不能解决 Xlib fatal、portal授权、EIS held state、GUI/tray/clipboard 同时恢复。
5. **全天依赖与普通用户部署契约不足**：有效权限、旧新 daemon/runtime、不同 package capability 和 24h 多脚本尚未形成可验证承诺。

这些排序不是沿用旧 audit 的 P0 清单。`--pack` 越界是值得立即修复的确定问题，但对“每天全天运行的输入 runtime”的总体风险权重低于上述五类。

## 五个最大的 evidence gap
物理双键盘/鼠标的 fault injection；真实 KDE/KWin/native Qt/SNI；GNOME/KDE libei consent/revoke/persistence；真实 Fcitx5/native/Flatpak IME；五角色多脚本长稳及真实应用迁移 corpus。已有协议、VM 和较短测试的价值保留，不把缺口写成“完全没做”。

## 最多五种“完成度幻觉”
| 类型 | 当前容易发生的误读 | 正确解释 |
|---|---|---|
| Test illusion | 1170 assertions、三 pipeline 模式一致→Windows parity | oracle预期和语义族覆盖仍有限 |
| Environment/backend illusion | GNOME VM/XWayland 或 headless sway→所有Wayland | native KDE/Fcitx/portal各自需要证据 |
| Semantic illusion | Hotkey callback或Send ACK成功→原键抑制/目标输入正确 | 必须由独立目标观察原键、替换键和phase |
| Recovery illusion | generation增加/socket回来→脚本恢复 | held keys、注册、GUI/clipboard/portal都要对账 |
| Packaging illusion | deb/AppImage/Flatpak文件存在→普通用户可完整自动化 | 权限、sandbox、broker、版本与可选library仍决定能力 |

这不是指 README 必然夸大：README 把项目定位为 technology preview，很多局限也有诚实说明。主要危险在于外部读者将不同证据层合并。

## 成熟度被低估的五处
**独立 XTEST/Windows fixture** 确实降低自验证污染；**libeis receiver 的设备/暂停/替换 fault cases** 已超过“有 libei API”；**broker 多角色仲裁和 legacy 授权否认** 是真正安全工程；**IBus取消/Backspace与GTK目标、Qt/Java readback** 有可观察应用结果；**DBGp/DAP外部客户端及同PID重连** 是实际开发工具链，不只是截图。[S35–47, S54–58]

# 20. 最终 Evidence Gap 表

| Capability | Implementation status | Best evidence | Missing evidence | User risk |
|---|---|---|---|---|
| real evdev/uinput | 主要路径有实现；local residual defects | 源码+protocol/kernel-virtual fixture设计/CI | 物理故障闭环、holder STOP、write后失败target状态 | 输入不可用/残留key |
| physical multi-keyboard | broker部分按设备；reducer/output不足 | source+单设备/多client | 同码交叠、held unplug、真实两键盘 | 错误repeat/keyup/owner |
| real KDE | 多通用适配路径存在 | protocol/headless/静态 | KWin、portal UI、SNI、native Qt实际workflow | 部署后不可用或功能弱化 |
| real Fcitx5 | protocol implemented | 独立D-Bus service/E2 | real daemon、候选、focus/engine切换、native targets | 漏/重commit、语义不一致 |
| Flatpak IM/automation | manifest落后且权限受限 | static manifest | 实际build/run/host IM/AT-SPI/input路径 | 装得上不代表能操作 |
| InputCapture | 未找到完整可用集成路径 | roadmap/claims级别 | 实现与真实compositor验收 | native全局capture不能据libei推导 |
| GNOME/KDE libei | sender/device处理较成熟 | O4 independent EIS receiver/E2 | consent、deny、revoke、重新授权、token persistence | 注入中断/权限UX/恢复不确定 |
| 24h/72h soak | 有短soak harness | 30s CI、5min VM记录 | 带故障与外部指标的长时运行 | 长尾race/资源漂移未排除 |
| five-role multi-script | 多client+两脚本测试 | O3/E2 | 五类AHK同时、重启、level、clipboard、长时 | 互相干扰和重复/丢事件 |
| application automation | GTK/Qt/Java/LO部分已验证 | 真实VM目标marker | Firefox/Chromium、Monaco替代路径、更多控件/布局 | API成功≠业务完成 |
| rich clipboard | X11有，native/text fallback不足 | serializer/selection tests | 所有MIME外部hash、原子恢复、INCR发送、大数据预算 | 内容丢失/大对象失败 |
| IME lifecycle | queue/locking修复有价值 | IBus XWayland VM + protocol | callback中shutdown、owner churn、长Unicode、race suite | 极端情况下状态/事件失衡 |
| upgrade compatibility | v1/v2设计、安装测试 | 当前二进制多协议+同版重装 | 真正历史client/daemon交叉升级降级 | 授权/能力变化与混版行为 |
| HEAD Syntax Studio / VSIX host | 示例和工具实现存在 | source guard、protocol、VM记录 | HEAD绑定的完整UI/extension-host gate | 新示例/工具回归未自动阻断 |

# 21. 下一步 Top 10：按用户影响与风险消减排序

排序参考“用户影响×语义中心性×风险降低×跨backend复用×证据缺口÷成本”；没有编造精确乘积。

| Priority | 当前问题 / 为什么现在 | 推荐路线 | Acceptance criteria | 最有价值的 oracle | Complexity |
|---|---|---|---|---|---|
| 1 | physical fail-open：涉及用户还能否输入 | 独立grab租约/watchdog；raw事件保真；默认禁用未qualified路径 | 所有故障点bounded恢复，无stuck physical/synthetic key | 双物理键盘+鼠标+独立kernel和应用recorder | L |
| 2 | 多设备/多事务状态是多个功能的共同根 | device/seat/transaction三层owner set，禁止错误合并 | 同码交叠、cross-script output、disconnect严格balance | 独立双device producer+target recorder | L |
| 3 | Clipboard存在可证明的并发覆盖 | rich snapshot统一；generation条件；保守restore；累计budget/INCR | 外部C永不被覆盖，各MIME hash保留，预算有界 | 两独立clipboard owner+真实GTK/Qt consumer | L |
| 4 | 有效capability与正常用户安装脱节 | negotiated/health-aware mux；清楚permission UX | required suppression不静默退化，old daemon有明确结果 | 普通UID/root daemon矩阵+真实AHK脚本 | M |
| 5 | 长稳是全天power user判断的主要盲区 | 五角色corpus+故障调度+指标系统，先1h再8/24/72h | 无lost/duplicate/unbalanced，资源与p99达标 | 外部事件ledger，真实应用而非自报counter | L |
| 6 | pack越界确定、修复成本较低 | 单一resource tuple、checked lengths、RAII、atomic output | missing/read-fail/malformed footer ASan/UBSan clean | 独立恶意文件生成器+exit/output验证 | S–M |
| 7 | 恢复目前是组件集合而非全栈契约 | 明确Xlib fatal policy；generation统一；portal/EIS/clipboard/GUI一起恢复 | same-PID或明确restart契约，零重复注册/残留 | 真实compositor kill/restart+held-key目标观察 | L |
| 8 | GNOME/KDE/Fcitx真实环境不足 | 桌面VM/host矩阵，绑定SHA/库/OS/应用版本 | consent/revoke/persist、nativeQt/GTK、Fcitx/Flatpak达标 | 真portal UI+目标应用自身输出 | L |
| 9 | 官方diff集中少数触发场景 | 扩充combo/remap/HotIf/level/error/GUIordering，独立输入 | 两平台同语义trace，raw基线与版本/hash可复查 | 官方AHK2.0.26现场Windows runner | M–L |
| 10 | 发布/升级/工具链缺单一承诺 | recipes同源生成、old/new二进制矩阵、pack能力manifest、host gates | N−1↔N升级降级/卸载；VSIX host及教学GUI可复验 | 干净发行版安装+真实IDE/UI自动化 | M |

# 22. 明天要称为 Beta：明确 release blockers

**范围先决条件**：可以先定义“X11/selected-workload Beta”，把未经qualified的 raw capture/native Wayland 标成 opt-in experimental。不能在没有补证据时直接升级成“跨桌面 Linux AHK v2 Beta”并保持模糊范围。

- [ ] 修复 I02 local raw replay/SYN_DROPPED 和 I08 pack 越界；加独立失败场景回归。
- [ ] I01 physical fail-open 在实机通过；否则 Beta 默认禁止危险 suppression 路径，而不是仅写免责声明。
- [ ] I03 per-device/replacement/output held ownership 有确定语义和双设备/多script oracle。
- [ ] I04 权限不足和旧协议不得静默削弱 required semantics；普通用户安装后的 capability 诊断可操作。
- [ ] I05 clipboard并发复制C不被覆盖；rich restore不足须修复或明确禁用该自动fallback。
- [ ] X11/XWayland fatal disconnect 有准确、已测试的失败/重启契约；不再以返回型handler假装恢复。
- [ ] 至少一套声明支持环境完成五角色24h soak，外部事件/资源/延迟指标公开；72h作为后续生产门槛。
- [ ] 实际 N−1↔N runtime/daemon 升级降级、卸载、running transaction/held-key状态验收。
- [ ] Release artifacts 绑定SHA、build options、libraries、checksums/attestation；公开哪些jobs真正required，SKIP不能满足必需能力。
- [ ] 若Beta范围包含native GNOME/KDE：真实portal consent/deny/revoke/reauthorize、native目标、clipboard、tray、IME必须通过相应host gates；否则明确排除。
- [ ] HEAD新教学GUI/VSIX host证据分别绑定commit与二进制，不用source guard/截图代替交互验收。

这不是要求每个 Linux 桌面都零差异才能 Beta，而是要求**声明范围内没有已知关键安全/一致性缺陷，失败行为确定，并有相称证据**。

# 23. 最终工程判断

**当前水平**：整体 useful for selected workloads；工程阶段 advanced technology preview。语言/Linux utility 层最接近可日常依赖；X11普通热键、Send、文本和GUI适合逐脚本验收后使用。XWayland适用于X子域，native GNOME、KDE、wlroots必须分别评价。项目距离“有用”已经很近甚至已经达到；距离“Windows power user迁移一套脚本后无需关心backend、权限、composition和生命周期地全天依赖”仍有显著距离。

**普通用户现在可以信任的范围**：Linux文件/进程工具脚本、受控X11应用启动器、普通快捷键和文本发送、自己验收过的GTK/Qt简单表单与GUI工具、以IBus/XWayland为限定环境的部分中文输入辅助、当前VS Code/DBGp/DAP支持范围内的开发调试。对于剪贴板有价值数据，暂不依赖自动临时paste的无损恢复承诺。

**目前仍不应无条件依赖的范围**：全键盘接管、两物理键盘/多个脚本交叠remap与custom combo、rich clipboard并发恢复、native GNOME/KDE任意应用全局Hotstring/InputHook、未验证的Fcitx/Flatpak目标、跨compositor/session重启保持全部状态、五脚本全天无人监督运行、Windows COM/Win32 DLL/控件脚本的原样迁移。

**最重要的一句话**：熟悉 Windows AutoHotkey v2 的 power user 今天可以把这个 HEAD 当作“受控 X11 和 Linux 工具工作负载的实用 runtime”，但还不应把它当作“跨桌面、全键盘接管、多脚本全天运行且故障后语义不变的唯一自动化基础设施”；改变这一判断，最需要的不是继续增加函数，而是修复 raw replay、跨设备/事务状态、clipboard CAS/富格式恢复和 pack 错误路径，并补齐物理 fail-open、真实 portal/IME/桌面、跨版本升级和五脚本24–72小时的独立目标侧证据。

# 附录 A. 本审计实际实验

运行时间：2026-09-05T16:19:25Z；libX11 1.8.12；Xvfb；无真实物理input设备。详见 `probes/results.txt`、两个 `.c` 文件与 `run_probes.sh`。结果：
```
scope=isolated_libX11_contract_probes_not_repository_runtime
check_was_ours=1
foreign_copy_before_restore=1
foreign_copy_preserved=0
xio_client_exit=1
READY
HANDLER_CALLED: returning 0
```
clipboard probe 中的 A/B/C 是 ownership 交错的抽象：它直接验证另一进程的新 selection owner 被夺回，并未实现完整 MIME payload 传输。结合当前 restore 源码可推导内容覆盖风险，但不把它包装成 ahk_core 的实际文本恢复测试。

这些结果支持 I05/I06 的系统机制判断；**不声称本审计执行了整个冻结版本的 AHK test suite**。

# 附录 B. 冻结来源索引

下列仓库内容链接全部使用冻结 SHA；Release/Actions 运行页是外部执行元数据。GitHub页面可能以后不可用，因此 `snapshot.json` 另存已核实元数据。当前审计没有将全部远程源码和Actions artifact镜像到附件，不能把本报告当成完整离线源码归档。


**[S01]** 冻结的 HEAD — https://github.com/MonoEven/Autohotkey_Linux/commit/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f


**[S02]** 最新 Release 页面 — https://github.com/MonoEven/Autohotkey_Linux/releases/tag/v2.0.26-linux.21


**[S03]** Release 后提交 90612f6 — https://github.com/MonoEven/Autohotkey_Linux/commit/90612f6545071f88758d7fd58c3aac86d1a3c253


**[S04]** HEAD CI run — https://github.com/MonoEven/Autohotkey_Linux/actions/runs/33975385717


**[S05]** Release tag CI run — https://github.com/MonoEven/Autohotkey_Linux/actions/runs/33961359281


**[S06]** audits/check0905.md — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/audits/check0905.md


**[S07]** .github/workflows/ci.yml — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/.github/workflows/ci.yml


**[S08]** README.md — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/README.md


**[S09]** docs-v2/docs/linux-port.htm — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/docs-v2/docs/linux-port.htm


**[S10]** tests/doccheck/CHECK_REPORT.md — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tests/doccheck/CHECK_REPORT.md


**[S11]** MODULE_MATRIX.md — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/MODULE_MATRIX.md


**[S12]** tests/scenarios/SUPPORT_MATRIX.md — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tests/scenarios/SUPPORT_MATRIX.md


**[S13]** source/linux/core/core_evdev_linux.cpp — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/source/linux/core/core_evdev_linux.cpp


**[S14]** source/linux/core/core_uinput_linux.cpp — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/source/linux/core/core_uinput_linux.cpp


**[S15]** source/linux/inputd/inputd.c — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/source/linux/inputd/inputd.c


**[S16]** source/linux/inputd/inputd_proto.h — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/source/linux/inputd/inputd_proto.h


**[S17]** source/linux/core/core_inputd_client_linux.cpp — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/source/linux/core/core_inputd_client_linux.cpp


**[S18]** source/linux/core/input_backend.cpp — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/source/linux/core/input_backend.cpp


**[S19]** source/linux/core/input_event.h — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/source/linux/core/input_event.h


**[S20]** source/linux/core/input_pipeline.h — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/source/linux/core/input_pipeline.h


**[S21]** source/linux/core/input_pipeline.cpp — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/source/linux/core/input_pipeline.cpp


**[S22]** source/linux/core/core_hotkey_linux.cpp — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/source/linux/core/core_hotkey_linux.cpp


**[S23]** source/linux/core/core_capture_linux.cpp — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/source/linux/core/core_capture_linux.cpp


**[S24]** source/linux/core/core_input_linux.cpp — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/source/linux/core/core_input_linux.cpp


**[S25]** source/linux/core/core_keymodel_linux.cpp — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/source/linux/core/core_keymodel_linux.cpp


**[S26]** source/linux/core/input_backend_libei.cpp — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/source/linux/core/input_backend_libei.cpp


**[S27]** source/linux/core/core_wayland_linux.cpp — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/source/linux/core/core_wayland_linux.cpp


**[S28]** source/linux/core/core_clipboard_linux.cpp — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/source/linux/core/core_clipboard_linux.cpp


**[S29]** source/linux/core/core_ime_linux.cpp — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/source/linux/core/core_ime_linux.cpp


**[S30]** source/linux/core/core_win_linux.cpp — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/source/linux/core/core_win_linux.cpp


**[S31]** source/linux/core/core_ctrl_linux.cpp — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/source/linux/core/core_ctrl_linux.cpp


**[S32]** source/linux/core/core_atspi_linux.cpp — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/source/linux/core/core_atspi_linux.cpp


**[S33]** source/linux/core/core_dllcall_linux.cpp — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/source/linux/core/core_dllcall_linux.cpp


**[S34]** source/linux/core/core_pack_linux.cpp — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/source/linux/core/core_pack_linux.cpp


**[S35]** tests/oracle/README.md — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tests/oracle/README.md


**[S36]** tests/oracle/run_input_libei_oracle.sh — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tests/oracle/run_input_libei_oracle.sh


**[S37]** tests/oracle/run_m7_wayland_restart_oracle.sh — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tests/oracle/run_m7_wayland_restart_oracle.sh


**[S38]** tests/oracle/run_inputd_arbitration_oracle.sh — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tests/oracle/run_inputd_arbitration_oracle.sh


**[S39]** tests/oracle/run_inputd_injection_oracle.sh — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tests/oracle/run_inputd_injection_oracle.sh


**[S40]** tests/oracle/inputd_test_fixture.c — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tests/oracle/inputd_test_fixture.c


**[S41]** tests/oracle/run_input_combo_pipeline_oracle.sh — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tests/oracle/run_input_combo_pipeline_oracle.sh


**[S42]** tests/oracle/run_keymodel_oracle.sh — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tests/oracle/run_keymodel_oracle.sh


**[S43]** tests/oracle/run_ibus_ime_oracle.sh — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tests/oracle/run_ibus_ime_oracle.sh


**[S44]** tests/oracle/run_fcitx5_ime_protocol_oracle.sh — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tests/oracle/run_fcitx5_ime_protocol_oracle.sh


**[S45]** tests/oracle/GUI_HOST_MATRIX.md — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tests/oracle/GUI_HOST_MATRIX.md


**[S46]** tests/differential/README.md — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tests/differential/README.md


**[S47]** tests/differential/run_linux_trace.sh — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tests/differential/run_linux_trace.sh


**[S48]** tests/oracle/run_mixed_soak.sh — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tests/oracle/run_mixed_soak.sh


**[S49]** tools/linux/systemd/ahk-inputd.service.in — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tools/linux/systemd/ahk-inputd.service.in


**[S50]** tools/linux/systemd/ahk-inputd.socket — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tools/linux/systemd/ahk-inputd.socket


**[S51]** tools/linux/verify-packages.sh — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tools/linux/verify-packages.sh


**[S52]** tools/linux/PKGBUILD — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tools/linux/PKGBUILD


**[S53]** tools/linux/org.autohotkey.AHK.yml — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tools/linux/org.autohotkey.AHK.yml


**[S54]** extensions/vscode-ahk-linux/package.json — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/extensions/vscode-ahk-linux/package.json


**[S55]** extensions/vscode-ahk-linux/extension.js — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/extensions/vscode-ahk-linux/extension.js


**[S56]** extensions/vscode-ahk-linux/lib/debugAdapterCore.js — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/extensions/vscode-ahk-linux/lib/debugAdapterCore.js


**[S57]** tests/oracle/run_vscode_extension_oracle.sh — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tests/oracle/run_vscode_extension_oracle.sh


**[S58]** tests/oracle/VSCODE_VM_VISUAL.md — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tests/oracle/VSCODE_VM_VISUAL.md


**[S59]** examples/gui/syntax_studio.ahk — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/examples/gui/syntax_studio.ahk


**[S60]** examples/gui/SYNTAX_STUDIO.md — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/examples/gui/SYNTAX_STUDIO.md


**[S61]** tests/oracle/check_syntax_studio_source.sh — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tests/oracle/check_syntax_studio_source.sh


**[S62]** extension/ahk-global-hotkeys@autohotkey.org/extension.js — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/extension/ahk-global-hotkeys@autohotkey.org/extension.js


**[S63]** tests/oracle/run_p2_10_unicode_oracle.sh — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tests/oracle/run_p2_10_unicode_oracle.sh


**[S64]** X.Org XSetIOErrorHandler contract — https://xorg.freedesktop.org/archive/X11R6.8.0/doc/XSetIOErrorHandler.3.html


**[S65]** Windows differential manifest — https://github.com/MonoEven/Autohotkey_Linux/blob/47d1fcc2873004f03ba2ee6cc2b1e6e4cada320f/tests/differential/golden/windows-v2.0.26-x64.manifest.json

# 附录 C. 用户审计要求导航
| 原要求 | 本报告位置 |
|---|---|
| 0–2 冻结、sanity、delta | §1 |
| 3–5 证据、ledger、axis | §2–3 |
| 6、52–53 修复closure | §4 |
| 7、51 新风险/issue格式 | §15 |
| 8、37 输入安全/实机 | §5 |
| 9、15 broker/事务 | §6 |
| 10–14 capability/mux/event/layout/combo | §7 |
| 16–22 libei/InputCapture/恢复/桌面 | §8 |
| 23–24 clipboard/bridge | §9 |
| 25–28 IME/线程/Fcitx/Unicode | §10 |
| 29–32 Win/Control/GUI/VSCode | §11 |
| 33–36 differential/matrix/gates | §12 |
| 38–39 长稳/多脚本 | §14 |
| 40–44 security/packages/upgrades/docs/API | §13、§18 |
| 45–47 corpus/两种兼容/画像 | §16–17 |
| 48–50 pyramid/详细评分/总体数字 | §18 |
| 54–56 当前风险/幻觉/低估 | §19 |
| 57 Top 10 | §21 |
| 58 十项最终问题 | §1、4、17–19、22–23 |
| 59 Evidence Gap | §20 |
| 60 最终原则 | 全文，尤其§2与§23 |
