# check_detail0821 — 缺失项细化与逐项解决思路

> 本文基于 `check0821.md` 的评审结论,把其中每一个"缺失/弱化"论断拆解为可执行的
> 子项,并为每个子项给出细致的解决思路(设计、实施步骤、落点文件、验收标准、
> 风险)。所有"现状"均已对照本地仓库源码核实(linux-port 工作区,README 显示
> v2.0.26-linux.15、doc-check 1099/1099),不再只依赖 GitHub 页面快照。
>
> 优先级沿用 check0821:P0 = 决定"是不是 AutoHotkey"的结构性差距;
> P1 = 决定"能不能放心用"的质量差距;P2 = 认知/口径问题。
>
> **rev2(exa 深化)**:关键解决思路已逐项经外部检索佐证/修正,引用以行内链接
> 给出,汇总见文末"附录 A:外部技术情报与参考实现"。本轮最重要的四个修正:
> ① GNOME Wayland 注入的正解是 **libei/EIS(RemoteDesktop portal ConnectToEIS)**,
> 且 Xwayland 23.2+ 已自动把 XTEST 桥接到 libei;② **GNOME 48 起自带
> GlobalShortcuts portal 后端**,自研扩展的定位需要重述;③ 剪贴板监听协议已升格为
> **ext-data-control-v1**(KWin 6.6 也支持,Mutter 仍不支持,GNOME 侧应走扩展内
> Meta.Selection 信号);④ X11 侧"用 XI2 raw 事件区分 XTEST 注入"必须协商
> **XI 2.1**(2.0 的 sourceid 恒为 0,是历史 bug)。

---

## 0. 对 check0821 论断的本地核实与修正

写解决方案前先校准事实。check0821 有 4 处与当前源码状态有出入,细化时按
**当前实际状态**处理:

| check0821 论断 | 本地核实结果 | 影响 |
|---|---|---|
| "OnClipboardChange 需重新确认 linux.15 是否已修复" | **确认未修复**:`source/linux/stdafx_linux.h:2419` 的 `AddClipboardFormatListener` 仍是恒返回的 no-op,`source/script.cpp:753` 的 `EnableClipboardListener` 调它后事件链断裂 | §4 的方案按"从零实现监听"设计 |
| GNOME 扩展只声明 GNOME 49(承自 check0820) | `extension/ahk-global-hotkeys@autohotkey.org/metadata.json` 已声明 `["45","46","47","48","49","50"]`,但**只有 49 实机验证过**,其余版本仅"声明 + 运行时警告" | 缺口从"不能装"变为"装得上但未验证",§1.2-C 方案聚焦验证矩阵 |
| Wayland 剪贴板粘贴回退是"固定 60ms sleep + 仅文本" | `core_input_linux.cpp:770-807` 已改为等待消费 + 还原 + `AHK_WAYLAND_PASTE=0` 可关闭,`AHK_CLIPBOARD_TIMEOUT_MS` 默认 2000 | 缺口收窄为"多 MIME 往返、事件驱动等待、敏感场景策略",见 §6 |
| evdev/uinput 只是路线图 | `core_evdev_linux.cpp`(捕获,LISTEN/SUPPRESS 双模)与 `core_uinput_linux.cpp`(回放)骨架已存在,`AHK_INPUT_BACKEND=evdev` 可强制;`tools/linux/permissions/` 已有 udev 规则、polkit policy、安装脚本 | §1 的 Level 3 方案从"新建"改为"在现有 lane 上补齐" |

仍然成立的关键事实(逐条已核实):

- 四个 Send 模式仍走同一条 XTEST 路径(`core_input_linux.cpp:745-748`);
- `InstallKeybdHook/InstallMouseHook` 仅记录布尔(`core_input_linux.cpp:1150-1162`);
- `KeyWait` 是 `XQueryKeymap` 轮询;
- SendLevel 可设置但捕获引擎不建模(`core_capture_linux.cpp:20` 注释明示);
- 扫描码热键与 `A & B` 前缀组合仍拒绝注册(注册时报错,不假实现);
- `MODULE_MATRIX.md:4`、`MODULE_MATRIX.md:112` 仍写 1069/1069,README 已是 1099 —— 文档漂移实锤;
- CI 仍是单 `ubuntu-24.04` runner、`build: [core, asan]` 矩阵(`.github/workflows/ci.yml:17-19`);
- 注册表是 `~/.config/autohotkey-registry.txt` 单文件(`core_builtin_stubs.cpp:697-700`);
- COM 即 D-Bus(`core_com_dbus_linux.cpp`),`ComObjArray/ComObjQuery/ComObjConnect` 抛错;
- `TrayTip/TraySetIcon` 明确报未移植;MenuSelect 抛 TargetError(`core_mdfunc.cpp:1005`);
- 控件 enabled/checked/style 走本地 shadow(`core_ctrl_linux.cpp`);
- AT-SPI 仅最小路径(读文本/设文本/点击,`core_atspi_linux.cpp`,约 14KB)。

---

# 第一部分 P0 级缺失细化与解决思路

## 1. P0:Wayland 输入模型没有真正统一

### 1.1 缺失清单(细化)

把 check0821 的"多条不一致通路"拆成可以逐项关闭的 8 个缺口:

| 编号 | 缺口 | 现状锚点 |
|---|---|---|
| W1 | **`~` 透传、`*` 通配、`a & b`、remap、key-up 消费**在 GNOME Shell/Portal 后端全部缺失 | `input_backend_gnome_shell.cpp` 只消费 `Activated`;`AhkInputBackendCaps` 里 portal/gnome-shell 的 `passthrough/wildcard/key_up` 为 false |
| W2 | **热字串与 InputHook 在所有原生 Wayland 后端不可用**(依赖 X11 grab 捕获) | `core_capture_linux.cpp` 仅挂 X 连接;evdev lane 未接热字串/InputHook |
| W3 | **GNOME 无注入通路**:无 zwp_virtual_keyboard(mutter 明确不实现,推荐 libei),Send/鼠标模拟原生态不可用 | `LinuxWaylandCanInjectKeys()` 能力查询;docs linux-port.htm 矩阵。**注:本轮查证后此项有 libei 与 XWayland-桥接两条新解,见 §1.2-D** |
| W4 | **Portal 后端身份/确认问题**:**valid app-id 已是上游硬性要求**;绑定会弹确认 UI;Deactivated→key-up 语义未映射 | `core_gshortcut_linux.cpp`(已冻结,不再加 GNOME 特例) |
| W5 | **GNOME 扩展定位需重述**:45-50 仅声明、只有 49 实测;**且 GNOME 48+ 已自带 GlobalShortcuts portal 后端**,扩展价值收敛为"零确认 + 动态注册 + 覆盖 45-47" | `extension/metadata.json`;见 §1.2-C |
| W6 | **evdev lane 不完整**:无布局感知(keycode→字符),无 remap,无多客户端仲裁,SUPPRESS 需 input 组权限且无引导 UX,无 panic 逃生键 | `core_evdev_linux.{h,cpp}`、`core_uinput_linux.cpp` |
| W7 | **KDE/Hyprland/COSMIC 从未实机验证**,行为矩阵是推断 | check0820 附录:"KDE 未实机验证" |
| W8 | **脚本侧无能力查询 API**:脚本无法在运行时问"当前后端支持 key-up 吗",只能撞注册错误 | `AhkInputBackendCaps` 是 C++ 内部结构,未暴露给脚本 |

### 1.2 解决思路

**总体架构:承认"Wayland 没有单一答案",把三层通路做成显式的能力阶梯,
按热键逐条路由,而不是按会话整体选后端。**

```
Level 1  合成器协作层   GNOME 扩展(零确认) / GlobalShortcuts portal(GNOME48+/KDE)
                        —— 独占简单热键
Level 2  文本与注入层   ① wlroots: virtual-keyboard + 私有 keymap(直注,最优)
                        ② 任意合成器: libei / RemoteDesktop portal(免 root,只能发 keycode)
                        ③ XWayland: XTEST(Xwayland 23.2+ 自动经 libei 走 portal)
                        ④ 兜底: 受控剪贴板粘贴  ⑤ 未来: IBus engine(任意 Unicode)
                        —— Send / SendText
Level 3  内核输入层     evdev 捕获 + uinput 注入(ahk-inputd,需权限)
                        —— 透传 / remap / a&b / key-up / 热字串
                        (InputCapture portal 暂无按键触发器,不能替代,见 D2)
```

三层不是"择一",而是**按能力逐热键/逐调用路由**:同一脚本里
`F12::`(L1 portal)、`SendText "你好"`(L2①或④)、`CapsLock::Esc`(L3)
可以同时走三条不同通路。

#### A. 逐热键路由 + 能力协商(解 W1、W8,先做,收益最大)

- **设计**:现有 `LinuxInputBackendKind()` 是"会话一个后端"。改为
  **注册时逐热键决策**:解析热键标志(`~`/`*`/`&`/`up`/裸键),对照各可用后端的
  `AhkInputBackendCaps`,选第一个满足的后端;都不满足时保持现行为
  (注册即抛 OSError,绝不静默假注册)。
- **实施步骤**:
  1. `input_backend.cpp` 增加 `LinuxInputBackendRoute(Hotkey*) -> Kind`,
     输入为热键的修饰/标志位向量;
  2. `core_hotkey_linux.cpp` 的注册入口改调路由器;每个 Hotkey 记录其归属 lane,
     Off/Suspend/析构时按 lane 反注册;
  3. 暴露脚本 API:新增内建函数 `HotkeyBackendGet([KeyName])` 返回对象
     `{backend:"gnome-shell", suppress:true, passthrough:false, ...}`
     (字段即 `AhkInputBackendCaps`),外加 `A_HotkeyBackend` 只读变量表示默认 lane;
  4. docs-v2 增加能力矩阵页,由 caps 结构生成(见 §10 单一数据源)。
- **验收**:`assert_hotkey` 新增断言 —— 在 sway 下同一脚本同时注册 `F12::`
  (portal lane)与 `~F11::`(evdev lane),两者都触发;GNOME 无 evdev 权限时
  `~F11::` 注册抛出含"backend capability"字样的 OSError。
- **风险**:同一物理键被两个 lane 观察时会双触发 —— 路由器必须保证一个键
  只归一个 lane;evdev lane 活跃时应把同键的 portal 注册降级掉。

#### B. ahk-inputd:把 evdev lane 补成完整 Level 3(解 W2、W6,核心工程)

- **设计**(对标 keyd/kanata 的成熟模型):
  - 独立小守护进程 `ahk-inputd`(新目录 `source/linux/inputd/`),root 或
    `input`+`uinput` 组权限,systemd system service + socket activation;
  - 客户端(ahk 进程)经 `/run/ahk-inputd.sock` 连接,SO_PEERCRED 鉴别 +
    polkit 授权(`tools/linux/permissions/io.github.autohotkey.inputd.policy`
    已备好,动作名沿用 `io.github.autohotkey.inputd`);
  - 协议(长度前缀的 JSON 或 flat struct):
    `REGISTER {id, keycode/mods, flags: suppress|passthrough|up|wildcard|prefix}`、
    `REMAP {from, to}`、`CAPTURE_ON/OFF`(热字串/InputHook 的按键流订阅)、
    事件回推 `FIRED {id, phase}` / `KEYEV {code, value, ts}`;
  - 守护进程对键盘设备 `EVIOCGRAB`,匹配命中→只发 `FIRED` 不回放;
    未命中→原样写入唯一的 uinput 虚拟键盘(现 `core_uinput_linux.cpp` 逻辑上移);
    `a & b` 前缀、长按/双击等状态机放在守护进程内(单一事件流,天然有序,
    这正是 X11 双连接方案做不到 `A & B` 的根因的解法);
  - **fail-open 三保险**:fd 关闭内核自动放 grab(已有);守护进程侧对每客户端
    心跳,断连即撤其表项;**panic 组合键**强制释放所有 grab 并停止 remap ——
    必须做,键盘全抓死是这类工具最大的实际风险。先例:[keyd](https://github.com/rvaiya/keyd)
    的 panic 序列是 `backspace+escape+enter` 三键同按,且在事件进状态机**之前**
    对非虚拟设备原始码判定(`src/evloop.c` 的 `panic_check`)—— 照抄这个时序设计,
    保证 remap 引擎自身出错时逃生键依然有效。
- **布局感知**(热字串/InputHook 需要 keycode→字符):
  - 守护进程只处理 keycode;字符翻译放客户端,用 **libxkbcommon**:
    优先从 `org.freedesktop.locale1`/当前会话 XKB 配置构造 keymap,
    X11/XWayland 存在时直接用 X 的 keymap 校准;
  - 已知限制如实写文档:合成器内切布局(如 GNOME 每窗口布局)守护进程无法
    感知,提供 `AHK_XKB_LAYOUT=de` 显式覆盖。
- **实施步骤**:
  1. 抽取 `core_evdev_linux.cpp` 的扫描/匹配循环进 `inputd` 主体,保留进程内
     模式作为无守护进程时的降级(现 LISTEN/SUPPRESS 行为不回退);
  2. 客户端桥:`core_evdev_linux.cpp` 改为优先连 socket,连不上再走进程内;
  3. 热字串/InputHook 接入:`CAPTURE_ON` 后 `KEYEV` 流经 xkbcommon 翻译喂给
     现有 `LinuxCaptureFeedInput`/hotstring 缓冲(复用 X11 已实现的状态机,
     只换事件源 —— 这是工作量最省的路径);
  4. remap:`CapsLock::Esc` 这类纯键→键映射直接下发 `REMAP` 给守护进程,
     延迟最低且对所有合成器一致;
  5. 打包:deb/RPM/AppImage 附带 unit 文件与 `install_permissions.sh` 集成,
     安装时询问是否启用(默认不启用,尊重最小权限);
  6. 安全文档:能读全键盘流=键盘记录器级权限,必须单独一节写清威胁模型、
     polkit 授权粒度、如何审计(`journalctl -u ahk-inputd` 记录客户端 pid/uid)。
- **验收**:
  - sway、GNOME、KDE(VM)三环境同一脚本:`CapsLock::Esc`、`~F1::`、
    `a & b::`、`F1 up::`、`*LWin::` 全部行为一致;
  - `kill -9` 脚本后 3 秒内正常打字不受影响(fail-open 断言);
  - panic 键端到端测试;
  - 两个脚本并发注册冲突热键,后者收到明确错误。
- **风险**(含同类工具实证过的坑):
  1. 权限引导是最大流失点 —— 无权限时错误消息必须直接给出
     `sudo .../install_permissions.sh` 命令。[ydotool 的 issue 池](https://github.com/ReimuNotMoe/ydotool/issues/210)
     证明两个高频翻车点:udev 规则必须带 `OPTIONS+="static_node=uinput"` 且
     **规则安装后要重载 uinput 模块才生效**(仓库现有
     `tools/linux/permissions/60-ahk-uinput.rules` 需核对这两点);socket 应放
     `$XDG_RUNTIME_DIR` 而非 `/tmp`;
  2. n-key rollover/组合媒体键设备兼容性需要设备白名单(`EVIOCGBIT` 检查
     EV_KEY 且有字母区);
  3. keyd 实证的两个副作用要写进文档:虚拟键盘设备会让 libinput 的
     disable-while-typing 失效(需 quirk 把虚拟设备标记为 internal),
     `setxkbmap`/`xset` 级别的用户设置在虚拟设备创建时可能丢失
     ([keyd#183](https://github.com/rvaiya/keyd/issues/183));
  4. 竞品共存:用户机器上若已跑 keyd/kanata/ydotoold,双重 EVIOCGRAB 会互相
     拿不到设备 —— 启动时检测同类 grab 持有者并明确报错,不要静默降级。

#### C. GNOME 扩展与 Portal 的边界重划(解 W4、W5)

- **重大情报(改变结论)**:**GNOME 48 起 xdg-desktop-portal-gnome 自带
  GlobalShortcuts portal 后端**,GNOME 官方发布说明明确"apps 可以注册系统级全局
  快捷键,GNOME 48 完整支持"([GNOME 48 release notes](https://release.gnome.org/48/developers/)、
  [xdg-desktop-portal-gnome 48.rc NEWS:"Add global shortcuts portal backend"](https://gitlab.gnome.org/GNOME/xdg-desktop-portal-gnome/-/raw/gnome-48/NEWS))。
  这意味着自研扩展的价值区间被压缩到很明确的三块,必须据此重述文档:
  1. **GNOME 45-47**:portal 后端不存在 → 扩展是唯一的原生热键通路;
  2. **GNOME 48+**:portal 可用,但 `BindShortcuts` 会**弹用户配置/确认对话框**
     ([portal 文档](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.GlobalShortcuts.html));
     扩展的差异化价值只剩"零确认 + 动态增删热键"—— 这正是 AHK 脚本
     `Hotkey()` 运行时注册的核心体验;
  3. 企业禁扩展环境 → portal 成为主路径,扩展不可用。
  据此,`AHK_INPUT_BACKEND=auto` 的 GNOME 分支应改为:扩展在 → 扩展;
  扩展不在且 GNOME≥48 → portal;扩展不在且 GNOME<48 → 明确报错并给安装指引
  (现行 auto 策略在 GNOME 45-47 无扩展时会选 portal 却注册不上,是隐性坑)。
- **Portal 身份问题已升级为硬性要求**:上游
  [xdg-desktop-portal NEWS](https://github.com/flatpak/xdg-desktop-portal/blob/main/NEWS.md)
  记录 "**Require a valid AppID from apps to use the Global Shortcuts Portal** (#1817)"
  —— 裸命令行进程不再是"可能被拒",而是**规范要求必须有 app-id**。因此:
  1. 安装脚本必须落 `~/.local/share/applications/org.autohotkey.ahk.desktop`
     (或系统级 `/usr/share/applications/`),并保证进程的 app-id 与之匹配;
  2. `ahk --diag`(§12)增加一项"portal app-id 可解析性"探测,把这个失败模式
     从"神秘的绑定失败"变成一行诊断结论。
- **Deactivated → key-up 有规范依据**:GlobalShortcuts portal 明确定义
  `Activated`/`Deactivated` 成对信号(且 1.21 起两个信号都带 activation token),
  因此把 `Deactivated` 映射到 AHK 的 `key up` 变体是**规范内行为**,不是 hack;
  实现时按后端能力位开启(§1.2-A 的 caps.key_up),KDE/GNOME 各自实测确认。
- **版本验证矩阵**(扩展侧仍要做):`vmctl.py` 增加 GNOME 45/46/47/48/50 模板,
  每次发布跑 `gnome_ext_reregister.sh` 四步(注册→触发→disable/enable→再触发);
  未验证版本在扩展运行时警告注明 "declared, not verified"。
- **验收**:发布说明的 GNOME 表格改为三列 —— 版本 / 后端(扩展或 portal)/
  是否实机验证;GNOME 48 VM 上额外验证"卸载扩展后 portal 路径仍可用"。

#### D. GNOME 注入缺口:libei/EIS 是正解(解 W3,本轮最大修正)

check0821 与前一版方案都把 uinput 当作 GNOME 下唯一注入通路 —— 这个判断已过时。

- **事实**:mutter 明确**不打算**实现 `virtual-keyboard-unstable-v1`,官方推荐
  改用 **libei**([mutter#1974 的上游答复](https://github.com/atx/wtype/issues/22));
  libei 已作为传输层集成进 **XDG RemoteDesktop portal(`ConnectToEIS`)**与
  **InputCapture portal**,自 xdg-desktop-portal 1.17(2023 年中)起可用,
  GNOME/KDE 均有实现([Peter Hutterer 的整合说明](http://who-t.blogspot.com/2026/07/libei-integrations-in-xdg-remotedesktop.html)、
  [libei 文档](https://libinput.pages.freedesktop.org/libei/))。
- **免 root、跨合成器**:这是 libei 相对 uinput 的决定性优势 —— 不需要 input 组、
  不需要守护进程、不需要 polkit;由 portal 负责授权,合成器保留随时终止会话的
  能力,GNOME 还会显示"屏幕正被远程控制"指示。Chromium 的远程桌面已经切到
  这条路([chromium commit](https://github.com/chromium/chromium/commit/1fccb06f36d05361baab49cc1443af6f1c57ecc9))。
- **两条落地路径,建议都做**:
  1. **零代码路径(立刻可用,应写进文档)**:**Xwayland 23.2+ 已把 XTEST 自动
     桥接到 libei** —— X 客户端调 XTEST,Xwayland 代为向 RemoteDesktop portal
     申请授权并转成 libei 事件,**合成器无需额外支持,客户端无需改代码**
     (同上 who-t 文章)。也就是说:在 GNOME Wayland 下以 XWayland 方式运行本
     移植版,现有 XTEST 发送路径可能**已经能注入到整个桌面**(而非仅 X 客户端)。
     这是一个必须立即在 GNOME 48/49 VM 上验证的假设 —— 若成立,W3 的用户可感知
     缺口在文档层面即可大幅收窄。验证脚本:`gnome_xtest_libei.sh`(XWayland 里
     `SendText` → 焦点给原生 Wayland 应用 gedit → 断言文本到达)。
  2. **原生路径(中期)**:新增 `AHK_WAYLAND_INJECT=libei` 后端
     (`source/linux/core/core_libei_linux.cpp`):用 **liboeffis** 完成
     RemoteDesktop portal 握手(它把 DBus session/request 复杂度封成小 API),
     拿到 EIS fd 后以 sender context 发键鼠;portal 1.21+ 支持**会话持久化**
     (restore token),因此不必每次连接都弹授权 —— 这是把它做成默认后端的前提。
- **必须记入的限制**:libei **只能发 keycode,keymap 由 EIS 端(合成器)决定**,
  客户端只能发当前 keymap 里存在的键;mutter 的 Wayland 后端对
  `NotifyKeyboardKeysym` 不做 X11 那种"临时映射再还原"的兜底,keysym 不在 keymap
  里就直接失败([GNOME discourse 讨论](https://discourse.gnome.org/t/injecting-arbitrary-unicode-characters/24799))。
  **结论:libei 解决"能不能注入",不解决"任意 Unicode"** —— 后者仍需 §6 的
  IME 路径(该讨论里 GNOME 开发者给出的推荐做法同样是"连 IBus 并注册为 engine")。
- **优先级**:路径 1(验证 + 文档)进 R1;路径 2(libei 后端)进 R3,与
  ahk-inputd 并列而非替代 —— 两者定位不同:libei = 免权限注入(sender),
  inputd = 捕获/抑制/remap(portal 侧至今无对应能力,见下)。

#### D2. 为什么 InputCapture portal 现在**还不能**替代 ahk-inputd

- InputCapture portal 虽然能"从合成器收输入",但其触发条件目前**只有指针屏障
  (pointer barriers)**:应用申请 Zones、在屏幕边缘设屏障,光标越界才激活捕获
  ([portal 文档](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.InputCapture.html))。
  这套语义服务于 InputLeap 式跨机共享,**没有"按下某键就把该键交给我"的触发器**;
  上游 PR 讨论里也明确"按需捕获需要新增 trigger 类型,目前没有"
  ([xdg-desktop-portal#714](https://github.com/flatpak/xdg-desktop-portal/pull/714))。
- 因此 Level 3(透传/抑制/remap/热字串)**在可预见期内只能靠 evdev+uinput**;
  KWin 与 xdg-desktop-portal-kde 已实现 InputCapture(2024 年 5 月合并),
  GNOME 侧亦有实现,所以应当**跟踪"键盘触发器"提案**并在文档路线图里写明:
  一旦上游支持按键触发,Level 3 即可获得一条免 root 的合规路径。
- 行动项:在 `docs-v2/docs/wayland-levels.htm`(§15)为 Level 3 标注两种实现
  ("evdev/inputd:今天可用,需权限" vs "InputCapture portal:上游能力待补"),
  避免读者误以为项目忽视了官方通路。

#### E. 逐合成器行为矩阵(解 W7)

- 见 §14 场景化测试与 §16 CI 矩阵:KDE Plasma(X11 包已有,Wayland VM 待建)、
  Hyprland(wlroots 系,虚拟键盘应同 sway,需验证 v1/v2 协议版本)列入
  发布前必跑清单;结果写进自动生成的 SUPPORT_MATRIX(§10)。

---

## 2. P0:Send 系列只是"能发送",不是等价实现

### 2.1 缺失清单(细化)

| 编号 | 缺口 | 现状锚点 |
|---|---|---|
| S1 | SendEvent/SendInput/SendPlay 共用一条 XTEST 路径,无模式差异 | `core_input_linux.cpp:745-748` |
| S2 | `SetKeyDelay`/`SetMouseDelay`/PressDuration 不参与发送节奏 | 同上(wrapper 无延迟参数化) |
| S3 | SendInput 的"批量原子 + 发送期间自身 hook 不消费"语义缺失 | 无对应机制 |
| S4 | SendLevel/InputLevel 不建模:自发事件无级别标记,InputHook 的 `I` 选项、`#InputLevel` 无效 | `core_capture_linux.cpp:20` 明示 |
| S5 | 注入事件识别用 8 槽日志:本地 GetTickCount、1 秒窗口、匹配不消费 → 可能误吞 1 秒内的真实重复按键 | check0820 P0/P1 分析,`core_hotkey_linux.cpp` 注入日志 |
| S6 | `BlockInput` 无实现(Windows 上 Send 期间可阻断用户输入) | — |

### 2.2 解决思路

#### A. 先修 S5(正确性 bug,量小价值高)

- 注入日志改为:记录 **X server 时间戳 + 期望序列号**(注入后 `XSync` 取
  `NextRequest`/事件 `time`),匹配条件加 `ev.xkey.time` 距离 ≤200ms;
  **匹配即消费该槽**;连击场景专项断言(`assert_repeat` 已有 4 项,新增
  "透传注入后 300ms 内真实二击必须到达"项)。
- 根治方向:按 **XI2 raw 事件的 `sourceid`** 区分注入/物理,不再需要时间窗启发式。
  X server 为每个 master 硬编码一对 XTEST 从设备("Virtual core XTEST keyboard"/
  "…XTEST pointer"),`XTestFakeKeyEvent` 产生的事件其源设备恒为它
  ([Xorg 输入模型](http://who-t.blogspot.com/2010/07/input-event-processing-in-x.html)、
  [Xext/xtest.c](https://github.com/XQuartz/xorg-server/blob/master/Xext/xtest.c))。
  现有 XI2 观察器(左右修饰键判侧用)已在,加一次设备身份解析即可。
  **两个必须写进实现的坑**:
  1. **必须协商 XI 2.1**:`XIRawEvent.sourceid` 在 XI 2.0 里是历史 bug,
     恒为 0([libXi 修复记录](https://lists.x.org/archives/xorg-devel/2011-July/024095.html)、
     [协议补丁](https://lists.x.org/pipermail/xorg-devel/2011-August/024396.html));
     `XIQueryVersion` 必须请求 ≥2.1 并检查返回值,拿不到就回退时间窗方案;
  2. **稳健的身份判定**:不要按设备名字符串匹配(本地化/驱动差异),优先读
     XTEST 设备属性 `XI_PROP_XTEST_DEVICE`(server 会给这两个设备打该属性),
     启动时枚举一次并缓存 deviceid,遇 `XI_HierarchyChanged` 重新枚举。
   **已实施(R2 §3)**:`core_hotkey_linux.cpp` 协商 XI 2.1(请求 2.1 并检查
   返回值,<2.1 时禁用 tap 回退时间窗)、经 `XI_PROP_XTEST_DEVICE`(注意
   实际是 8-bit 单字节属性,需兼容 fmt==8)枚举 XTEST 设备并缓存,raw 事件
   处理器读 `re->sourceid` 记录 {keycode, phase, is_xtest} 环形 tap,抓取事件
   在 passthru/self 匹配前先分类:**仅当明确 PHYSICAL(非 XTEST 源)时才跳过
   抑制匹配**——物理真实按键永不落入过期注入标记(修 S5);tap 未命中(未知)
   或 XI<2.1 时保持原时间窗启发式(安全回退)。`--diag` 报 xi2-sourceid /
   xi2-xtest-dev。`XI_HierarchyChanged` 必须用 `XIAllDevices`(0) 选择,
   `XIAllMasterDevices` 会触发 BadValue(minor 46,已在 Xvfb 实证)。
   **验证边界(诚实登记)**:XTEST 设备检测与 sourceid 前提在 Xvfb 与
   GNOME Xwayland 会话均实证(XTEST 注入事件 sourceid==XTEST 设备);
   Xvfb 下所有事件都是 XTEST(无物理设备),故门控在此恒为"注入",行为与
   改动前一致(全量 doc-check 无回归);"物理按键不被过期标记吞掉"的端到端
   路径需要真实键盘,当前仅"原理级"(门控只在明确 PHYSICAL 时拦截)+
   sourceid 语义实证,未做真机 E2E。此路径落地后 S4 的 X11 侧也有了可靠地基。

#### B. 拆分发送模式(S1、S2)

- `LinuxSendWrapper` 增加 mode 参数,三条策略:
  - **SendEvent**:每键 `XTestFakeKeyEvent` 后按 `SetKeyDelay` 延迟、
    PressDuration 控制 down-up 间隔(默认 10ms 与 Windows 对齐);
  - **SendInput**:整串预编译为事件数组,发送期间 (a) 不 sleep,
    (b) 暂停本进程 capture 引擎消费(对齐 Windows"SendInput 期间卸载 hook",
    自发内容不触发自家热字串/热键),结束后恢复;
  - **SendPlay**:X11 无 journal 等价物 —— 实现为"SendEvent + Play 系列延迟
    (`SetKeyDelay , , Play`)",文档明确标注"适配实现:无 journal,注入层级
    与 Event 相同",并在 parity 分级(§13)标为"平台适配"。
- **验收**:`assert_input` 新增时序断言:xkeycap 记录事件到达时间戳,
  `SetKeyDelay 50` 时相邻事件间隔 ≥45ms;`SendInput` 100 键串在 Xvfb 下
  总耗时 <200ms 且中途无 capture 消费。

#### C. SendLevel/InputLevel 建模(S4)

- 进程内:注入日志(A 修复后)每条带 `level = g->SendLevel`;capture 引擎
  收到事件先查注入日志 —— 命中则事件携带该 level,InputHook 按 `MinSendLevel`
  过滤,热键按 `#InputLevel` 判定是否可被自发事件触发。
- 跨进程(两个 AHK 脚本互发):X11 侧在根窗口挂私有 property
  `_AHK_SEND_ANNOUNCE`(level+keycode+serial 环形记录),对方进程 capture 时
  查询;evdev/inputd 侧天然可行 —— 注入走守护进程时由守护进程打 level 标记。
  先做进程内(覆盖绝大多数用例),跨进程放 R3(§17)。
- **验收**:移植 Windows 语义样例 —— `SendLevel 1` + `#InputLevel 0` 的热键
  不被自发触发;`SendLevel 2` + InputHook `I1` 能收到。

#### D. BlockInput(S6)

- X11:`XGrabKeyboard/XGrabPointer` 同步抓取实现 `BlockInput "On"`
  (Send 期间抓取,发完释放;注意与热键抓取的互斥,复用独立连接);
  Wayland:仅 inputd 模式可实现(EVIOCGRAB 全键盘),否则报能力错误。
  标注"平台适配"。

---

## 3. P0:低级键盘/鼠标 Hook 语义不完整

### 3.1 缺失清单(细化)

| 编号 | 缺口 | 现状锚点 |
|---|---|---|
| H1 | `InstallKeybdHook/InstallMouseHook` 只记布尔,不建立任何事件观察 | `core_input_linux.cpp:1150-1162` |
| H2 | 物理/逻辑键状态不分:`GetKeyState(key,"P")` 与逻辑态同源 | KeyWait/GetKeyState 皆 XQueryKeymap |
| H3 | `KeyWait` 轮询,高频场景丢边沿、CPU 空转 | 审计 §2.1 |
| H4 | `A_PriorKey`/`KeyHistory`/`A_TimeIdlePhysical`/`A_TimeIdleKeyboard` 无真实数据 | — |
| H5 | 自发事件 vs 物理事件区分(与 S4/S5 同根) | — |
| H6 | 多设备(多键盘/多鼠标)无识别,更谈不上 per-device 热键 | evdev lane 有设备 fd 但不上报身份 |

### 3.2 解决思路

**核心设计:把"hook"重定义为统一事件观察层(event tap),X11 用 XI2 raw
事件(优于 XRecord:免扩展废弃风险、自带 sourceid),Wayland 用 inputd 的
KEYEV 流;`InstallKeybdHook` 的语义 = 启用该观察层。**

- 实施步骤:
  1. 新建 `core_eventtap_linux.cpp`:订阅 XI2 `XI_RawKeyPress/RawKeyRelease/
     RawButtonPress/...`(root window, all master+slave devices),维护:
     - `g_PhysicalKeyState[256]` 位图(raw 事件 = 物理层,XTEST 注入事件按
       sourceid 归入逻辑层 —— H2、H5 一并解决);
     - 环形 KeyHistory(keycode、vk、up/down、时间、来源 P/A、窗口),
       `KeyHistory` BIF 输出格式对齐 Windows;
     - `A_PriorKey`、`A_TimeIdlePhysical` 直接从该层取数;
  2. `InstallKeybdHook/InstallMouseHook` 改为 tap 的启用/禁用开关(计数式,
     热字串/InputHook 活跃时隐式启用 —— 与 Windows 行为一致);
  3. `KeyWait` 改事件驱动:tap 层提供
     `LinuxWaitKeyEdge(vk, down|up, timeout)`(条件变量),无 tap 权限或
     纯 Wayland 无 inputd 时回退现行轮询并在 `A_KeybdHookInstalled` 类
     状态里如实报 0;
  4. 多设备:tap 记录 sourceid→设备名,先暴露只读 `A_LastKeyDevice`;
     per-device 热键列为远期(Windows AHK 本身也不支持,不追加负担)。
- **验收**:`assert_hook` 新套件 —— 物理按键(xdotool 无法测物理,用
  xkeycap+XTEST 分层断言:XTEST 事件必须落逻辑层不落物理层);
  `KeyWait "F5", "T0.5"` 在 tap 模式下 CPU 采样 <1%;KeyHistory 输出含
  最近 8 键且方向正确。
- **风险**:XI2 raw 事件在部分远程 X/老服务器缺失 —— 保留 XQueryKeymap
  轮询降级路径,能力如实上报。

---

## 4. P0:OnClipboardChange 注册了但不会触发

### 4.1 缺失细化

- 断链点唯一且明确:`script.cpp:753 → AddClipboardFormatListener(no-op,
  stdafx_linux.h:2419)`,`AHK_CLIPBOARD_CHANGE` 消息永远不会投递;
- 需要覆盖三种环境:X11/XWayland、wlroots 系 Wayland、GNOME Wayland;
- 需要对齐的 Windows 语义:回调参数 Type(0 空/1 文本/2 非文本)、
  脚本自身修改剪贴板也触发、注册使脚本 persistent(后者已实现)。

### 4.2 解决思路

- **X11(主路径,先做)**:
  1. `core_clipboard_linux.cpp` 增加 XFixes 监听:
     `XFixesSelectSelectionInput(dpy, root, CLIPBOARD,
     XFixesSetSelectionOwnerNotifyMask | XFixesSelectionClientCloseNotifyMask)`,
     挂在剪贴板专用 X 连接(热键连接隔离的教训已有,不共享);
  2. 事件到达 → 请求 `TARGETS` 判型(含 UTF8_STRING→1,否则 2,
     无 owner→0)→ 投递 `AHK_CLIPBOARD_CHANGE` 进主循环,由既有
     `mOnClipboardChange` 派发(上游代码零改动);
  3. 自身 `A_Clipboard :=` 设置也会引发 owner 变化 → 与 Windows"自改也触发"
     一致,顺便免去去重逻辑;文档注明与 Windows 相同的重入注意事项。
- **X11 参考实现**:[clipnotify](https://github.com/cdown/clipnotify/blob/master/clipnotify.c)
  正是这套最小逻辑(约 80 行:`XFixesSelectSelectionInput` + `XNextEvent`),
  可直接对照;XFixes 规范也说明了该扩展存在的原因就是"取代轮询"
  ([fixesproto §6 Selection Tracking](https://www.x.org/releases/current/doc/fixesproto/fixesproto.txt))。
  注意 `SelectionNotify` 的 `subtype` 区分 SetSelectionOwner /
  SelectionWindowDestroy / SelectionClientClose 三种成因,AHK 的 Type=0(空)
  应由后两者或 owner=None 推出。
- **Wayland:协议已换代,按三级实现**:
  1. **`ext_data_control_manager_v1`(首选)**:wlr-data-control 已被
     wayland-protocols 收编,官方标注 wlr 版**已废弃、不建议生产使用**
     ([wayland.app 协议页](https://wayland.app/protocols/wlr-data-control-unstable-v1));
     ext 版支持面已很广 —— KWin 6.6、Sway 1.11、Hyprland、niri、COSMIC、
     labwc、Jay、Treeland 等均已实现
     ([ext-data-control-v1 支持矩阵](https://wayland.app/protocols/ext-data-control-v1))。
     **这条同时把 KDE 从"无方案"变成"原生可用"**,是相对上一版的实质升级。
  2. **`zwlr_data_control_manager_v1`(兼容回退)**:老版本 sway/wlroots 仍只有
     wlr 版;实现上照 [wl-clipboard 的双协议处理](https://github.com/bugaevc/wl-clipboard/commit/091d6028b5c9db75ad36f9fceb0db3ee718045fa)
     —— 优先 ext,回退 wlr,两者接口几乎同构,代码可共用。
  3. **GNOME/Mutter**:两个 data-control 协议**都不支持**(见上述支持矩阵中
     Mutter 一列全为 x),这是 mutter 的长期立场
     ([mutter#524](https://gitlab.gnome.org/GNOME/mutter/-/issues/524))。
     因此 GNOME 侧的正解不是轮询,而是**复用本仓库已有的 GNOME Shell 扩展**:
     在扩展里 `global.display.get_selection()` 连接 **`owner-changed` 信号**,
     用 `St.Clipboard` 按 MIME 优先级链(`text/plain;charset=utf-8` →
     `UTF8_STRING` → `text/plain` → `STRING`)读取,再经 D-Bus 定向发给注册者。
     完整先例见 [Clipman 的架构文档](https://github.com/MohammedEl-sayedAhmed/clipman/blob/main/ARCHITECTURE.md)
     与[作者的实现说明](https://mammar.pages.dev/blog/clipman-clipboard-manager-wayland-gnome/)
     (该文明确指出:轮询 `wl-paste` 会带来闪烁与漏拷贝,监听 Meta.Selection 才是正解)。
     - **合规提醒**:若将扩展发布到 extensions.gnome.org,`St.Clipboard` 直接
       访问会触发 Shexli 的 `EGO-A-005 (manual_review)` 人工复审标记 ——
       这是"需人工看一眼"的门槛而非拒绝,提前在提交说明里解释用途即可。
     - 无扩展的 GNOME 会话:才降级为哈希轮询(默认 1s,`AHK_CLIPBOARD_POLL_MS`
       可调),并如实标"模拟实现"。
- **能力查询**:`LinuxClipboardWatchKind()` 返回
  `xfixes | ext-data-control | wlr-data-control | gnome-extension | poll | none`,
  供 `ahk --diag`、文档生成器与断言口径统一引用(§10/§13)。
- **验收**:`assert_clipboard` 新增:子进程 `xclip -selection clipboard`
  外部改剪贴板 → 回调 1 次且 Type=1;`ClipboardAll` 二进制场景 Type=2;
  sway 纯 Wayland 下 `wl-copy` 触发(走 ext 或 wlr);GNOME VM 上装扩展后
  外部 `wl-copy` 触发、卸载扩展后降级轮询仍触发(延迟断言放宽到 2s)。
  CI 断言从"注册不报错"升级为"事件到达",正好回应 check0821 对测试口径的批评。
- **工作量**:X11 部分 1-2 天级,是全清单里**性价比最高的单项**,建议第一个做;
  ext-data-control 因有 wl-clipboard 可对照,约 2-3 天;GNOME 扩展信号复用现有
  扩展骨架,约 1-2 天。

  **已实施(R2 §4,GNOME 部分)**:X11 侧已由 R1-1 XFixes 完成。GNOME 侧:
  扩展(`extension/ahk-global-hotkeys@autohotkey.org/extension.js`)监听
  `Meta.Selection` 的 `owner-changed` 信号(实证 GNOME 49 上
  `Meta.SelectionType.CLIPBOARD` 为 `undefined`,按 Mutter 枚举序固定为 1),
  wl-copy 改剪贴板时经会话总线广播 `ClipboardChanged(type)`;运行时侧
  `core_clipboard_linux.cpp` 在纯 Wayland(无 X11)下 `LinuxClipboardWatchStart`
  检测扩展总线名并挂会话总线 filter+match rule(共享连接已有 portal 的 match
  rules,广播信号必须显式 add_match 才会送达——实证),`LinuxInputBackendDispatch`
  每轮泵 `LinuxClipboardDispatchWayland`,收到信号即触发 OnClipboardChange(Type 1/0)。
  **VM 端到端实证**:脚本注册 OnClipboardChange → `wl-copy` → 回调以 Type=1 触发
  (clean 代码 2 次触发);X11/XFixes 路径不变,全量 doc-check 无回归。
  **ext-data-control(sway/KDE)未交付(诚实登记)**:已实现 ext-data-control-v1
  与 zwlr_data_control_manager_v1 两个监听器(wayland-scanner 生成协议代码、
  data_control_device 的 selection 事件→OnClipboardChange)并能编译,但 sway
  1.10 headless 实测**收不到任何 data-device selection 事件**。exa 检索佐证
  (wayland 协议规范/emersion 博客/wlroots 源码+issue):
  ① 核心 `wl_data_device.selection` **只在客户端有键盘焦点时发送**——解释本
  端口 A_Clipboard 在 sway 下读外部剪贴板失败的根因(无焦点表面);
  ② `zwlr_data_control_device` 是**正确的剪贴板监听协议**(绑定即发首个
  selection、新 selection 都发,无需焦点),wl-paste --watch(同样 v2 绑定+
  get_data_device)能收到,而本端口连接**连首个 selection(nil) 都收不到**;
  ③ 派发诊断:主循环每次 `wl_display_read_events`=0、`wl_display_dispatch_pending`
  派发 0——fd 从未有 selection 数据;全库无自定义 event queue(排除"事件排到
  非默认队列漏派发")。
  **决定性实验(2026-08 第三轮)**:把端口派发临时改成与 wl-paste 完全一致的
  简单 `wl_display_dispatch`(阻塞)、并把 wl_data_device_manager 绑定从 v3
  改成 v1(对齐 wl-paste)——**仍收不到 selection**。为此写了独立最小 C 客户端
  (wayland-scanner 生成 zwlr 头,忠实复刻 wl-paste 的绑定:seat v2 + zwlr
  manager v2 + core ddm + shm + compositor + get_data_device + roundtrip +
  简单派发)——**同样收不到 selection**(sway 1.10 只广告 zwlr,无 ext)。
  **终局实验(2026-08 第四轮)**:并排跑 wl-paste --watch 与复刻客户端于同一
  sway 会话、双向 WAYLAND_DEBUG——wl-paste 的 device 收到 selection(nil)+
  offer,复刻客户端 get_data_device 发出后 **sway 零响应**。随后把复刻补成
  wl-paste 的**完整全局绑定**(补 shm/compositor/xdg_wm_base/xdg_activation/
  zwp_primary_selection,seat v2,core ddm v1)+ roundtrip + 简单派发——
  **仍收不到 selection**。wl-clipboard 2.2 源码(wayland.c/device.c/device-
  manager.c/registry.c)确认:watch 用 zwlr data_control device + 简单
  `while (wl_display_dispatch)` 循环,非 primary 时 `device_supports_selection`
  立即返回 1(无 roundtrip),无 popup surface——与复刻完全一致。
  **最终结论(诚实)**:问题既不在端口、也不在可复现的协议用法——wl-paste
  与复刻客户端的协议序列完全一致,但 sway 1.10 headless 只对 wl-paste 二进制
  响应 selection。并排复核:headless sway 的 seat 状态机不可靠——有时
  capabilities 0→2→3 缓慢演进(约 10s),有时始终停在 0;等 seat 就绪再建
  data_control device 亦无效。疑点收敛到 **wl-clipboard 2.2 的构建/库细节**
  (两个都链 libwayland-client.so.0.24.0,排除库版本;疑其 seat 绑定版本 >=2
  的 `wl_seat.name` 事件处理或初始化顺序),或 sway headless seat 状态机怪癖
  (需在**有输入设备的真实 sway 会话**复核,VM 无此条件)。
  已回退 ext-data-control。此问题同时阻塞"A_Clipboard 在 sway 下读外部剪贴板"。
  **未做(诚实登记)**:无扩展的 GNOME 会话降级轮询。
  **陷阱记录**:GNOME 扩展模块跨 disable/enable 有缓存,改 JS 需整 shell 重启
  (SIGQUIT 会弄崩 Wayland 会话,只能整机 reboot);`owner-changed` 在
  X11→Wayland 桥接路径不触发,需 Wayland 原生写入(wl-copy)。

---

## 5. P0:Windows 脚本迁移断层(COM/注册表/消息/DLL/托盘/编译)

### 5.1 缺失细化与逐项解决思路

这一组的正确策略不是"把 Windows 能力搬来",而是**逐项定性:替代实现 /
明确不做 + 迁移工具**。每项给出定性与方案:

| 编号 | 缺口 | 定性 | 解决思路 |
|---|---|---|---|
| M1 | COM/Office 自动化(IDispatch/SafeArray/事件) | **明确不做等价**,做"高价值替代" | ① 文档把 `ComObject("bus")` 正名为 D-Bus 绑定并给独立教程;② 补 LibreOffice **UNO 桥**示例库(`ComObject` 风格包装 UNO 的 D-Bus/socket 接口,覆盖"打开表格改单元格"高频需求);③ `ComObjArray/Query/Connect` 保持明确抛错(现状正确);④ 迁移 lint(见 M7)识别 `Excel.Application` 并提示替代方案 |
| M2 | 注册表 = 单文件 INI | **模拟实现,补语义边界** | ① hive 分离(HKLM→`/etc/autohotkey/registry.txt` 只读镜像,写入抛权限错误,对齐"非管理员写 HKLM 失败"的 Windows 体验);② 类型保真(REG_BINARY/QWORD/MULTI_SZ 序列化格式);③ `RegCreateKey` 深层键、枚举顺序对齐;④ 提供 `ahk --import-reg file.reg` 迁移工具;⑤ parity 标签"模拟" |
| M3 | SendMessage/PostMessage/OnMessage | **AHK 进程间可用,Win32 语义不做** | 用 X11 ClientMessage(私有 atom `_AHK_MESSAGE`,携带 msg/wparam/lparam)实现 **AHK↔AHK 脚本消息互通**(这是脚本生态里 OnMessage 的主要现实用途之一);对任意外部窗口的 Win32 消息维持文档化的 0 返回;Wayland 下经 D-Bus 私有名 `org.autohotkey.Script-<pid>` 同能力 |
| M4 | DllCall 只能 `.so`、Callback 浮点不封送 | **平台适配,补 ABI 保真** | ① 引入 **libffi** 重写 `core_dllcall_linux.cpp`/`core_callback_linux.cpp` 的参数封送:float/double 按 SysV ABI 正确入寄存器,CallbackCreate 支持浮点参数与返回(消审计"UINT_PTR 槽"简化);② `.dll` 路径给出明确错误+提示改 `.so`;③ 常用 shim 库文档:`libc.so.6`、`libX11` 示例集 |
| M5 | 托盘图标/TrayTip | **替代实现(标准协议)** | ① 实现 **StatusNotifierItem**(org.kde.StatusNotifierItem + com.canonical.dbusmenu);② `TrayTip` → `org.freedesktop.Notifications.Notify`(libnotify 语义,全桌面通用);③ 无 SNI host 时降级:TrayTip 仍走通知,SetIcon 报能力错误;④ 替换现"未移植"报错为真实现,worklist NOT_IMPL -3。详见下方 M5 补注 |
| M6 | 编译为独立可执行 | **替代实现(自解包)** | `ahk --pack main.ahk -o mytool`:复制 runtime ELF + 追加 `[magic][tar(脚本+FileInstall 资源)][footer]`;启动时读 `/proc/self/exe` 检测 footer 自加载;另提供 `--pack-appimage` 复用 `tools/linux/pack-appimage.sh`。详见下方 M6 补注 |
| M7 | 迁移可行性不可预判 | **新工具** | `ahk --lint-windows script.ahk`:静态扫描 AST,输出报告 —— 每个调用点标 parity 等级(§13 的四级),Windows 专有项给出替代建议(TrayTip→通知、Reg→文件、COM→D-Bus/UNO);CI 里对 examples/ 全量跑,报告零回归 |

**优先序建议**:M5(托盘,用户可见度最高)→ M4(libffi,正确性)→ M6(打包,
部署刚需)→ M3 → M2 → M1/M7(文档与工具贯穿全程)。

### 5.2 M5 补注:托盘的宿主现实与实现选型(exa 核实)

- **各桌面的 SNI 宿主情况**(决定"能不能显示"):
  - **KDE Plasma**:原生宿主,StatusNotifierItem 本就是 KDE 提出的规范;
  - **sway/wlroots**:`swaybar` 内建 tray,同时注册 `org.freedesktop.
    StatusNotifierWatcher` 与 `org.kde.StatusNotifierWatcher` 两个 watcher
    ([swaybar/tray/tray.c](https://github.com/swaywm/sway/blob/03483ff3707a358d935e451d39748e58c205ce8a/swaybar/tray/tray.c));
    Waybar 亦有 tray 模块 —— 因此**两个 well-known 名字都要尝试注册**;
  - **GNOME**:Shell 本身**没有**托盘,必须靠
    [AppIndicator/KStatusNotifierItem 扩展](https://github.com/ubuntu/gnome-shell-extension-appindicator)
    (Ubuntu 预装,其他发行版需用户自行安装)。因此 GNOME 下 `TraySetIcon`
    的失败是**环境缺宿主**而非移植缺陷 —— 错误消息必须这么写,并给出扩展链接。
- **实现选型:直接实现 SNI + dbusmenu,不要链 libayatana-appindicator**。
  理由:libayatana-appindicator 近期重写为 Gio menus,**偏离了 SNI 规范的
  `com.canonical.dbusmenu` 约定,导致多个 bar 实现的菜单失效**
  ([Waybar#4467](https://github.com/Alexays/Waybar/issues/4467))。自己按规范
  实现 dbusmenu(仓库已有成熟的 D-Bus 层 `core_com_dbus_linux.cpp` 与 GTK 菜单
  模型 `script_menu_linux.cpp`,桥接成本可控)可避开这轮生态动荡。
- **验收**:KDE VM、sway(swaybar)、GNOME+AppIndicator 扩展 三处各断言:
  图标出现 → `A_TrayMenu` 项可点 → 点击回调进脚本 → `TrayTip` 弹通知;
  无宿主环境断言"报可读的能力错误且不崩溃"。

  **已实施(R2 §5-M5,GNOME/AppIndicator 验证)**:`core_tray_linux.cpp`
  自实现 SNI(org.kde.StatusNotifierItem + com.canonical.dbusmenu,不链
  libayatana-appindicator):
  - `TraySetIcon` 注册 StatusNotifierItem(well-known 名
    `org.kde.StatusNotifierItem-<pid>-1`,私有 D-Bus 连接 + 对象路径
    /StatusNotifierItem 与 /MenuBar),图标名从文件路径取 basename 去扩展名;
  - 默认菜单(com.canonical.dbusmenu)含 Pause/Suspend/Reload/Exit,点击经
    `Event` → 调 `PauseCurrentThread`/`ToggleSuspendState`/`ExitApp`;
  - 无 watcher/无会话总线 → 静默 no-op(与 TrayTip 一致,能力降级而非报错);
  - **A_TrayMenu(脚本自定义菜单)已交付**:`BIV_TrayMenu` 去桩+惰性创建(上游
    Script::Init 的 CreateWindowEx 在 Linux 不达,需在首次访问时
    `new UserMenu(MENU_TYPE_POPUP)`——实证 mTrayMenu 为 NULL 时 `Type(A_TrayMenu)`
    段错误),SNI dbusmenu 改渲染 `g_script.mTrayMenu` 的项(A_TrayMenu.Add 的
    自定义项,含 enabled/checked/separator),点击经 `SniFireUserItem` 调用项回调
    (A_ThisMenuItem/Pos/Menu 参数,对齐 script_menu_linux.cpp 的 FireMenuItem);
    A_TrayMenu 为空时回退默认 Pause/Suspend/Reload/Exit。
  **VM 协议级实证**:watcher 注册、GetAll/Get 属性(IconName='custom')、
  GetLayout(4 项)、点击 Exit → 进程退出,全部通过;A_TrayMenu 实证:
  `A_TrayMenu.Add("Hello AHK",Cb)` + `Add("Quit",Cb)` → GetLayout 显示两项 →
  点击 id=1 → 回调 `hello:Hello AHK:1` → 点击 id=2 → 退出。
  **泄漏修复(重要)**:必须用 **well-known 名**注册——ayatana watcher 的
  清理依赖 item 的 NameWatcher(仅当 uniqueId===service,即 well-known 形式
  才建立);用 unique-name+path 形式注册时 Gio.DBusProxy 检测不到名字消失,
  **进程退出后图标永远残留**(实测 GNOME VM 上堆积 34+ 个)。well-known 注册
  在进程退出 ~2s 内被 watcher 清理,已实测确认。
  **dbusmenu 两个实证坑**:① 子项必须是 `av`(每个子项用 VARIANT 包裹
  (ia{sv}av)),直接用 (ia{sv}av) 会让 libdbus 在 GetLayout 时 abort(核心转储);
  ② `Properties.Get` 返回单个 variant `v` 而非 dict entry,否则 watcher
  探测属性时连接被 daemon 断开。
  **swaybar 宿主实证(已补)**:headless sway 1.10 带 bar 配置起 swaybar →
  swaybar 注册 `org.kde.StatusNotifierWatcher` + `StatusNotifierHost-<pid>`;
  TraySetIcon 脚本的 SNI 名注册成功,IconName='applications-system',
  GetLayout 显示 A_TrayMenu 自定义项("Sway Item"),退出后 well-known 名被
  watcher 清理(false)。**SNI 现已在 GNOME/AppIndicator + swaybar 双宿主验证**。
  **图标 pixmap(已实现)**:TraySetIcon 给图片文件路径时,用 gdk_pixbuf 读入并
  转 ARGB32(宿主原生字节序),经 `IconPixmap`(a(iiay))在 GetAll/Get 暴露;
  主题名(非文件)保持仅 IconName,宿主回退。实证:16x16 PNG → Get 返回
  [(16,16,[ARGB32 字节])],首像素 A=0x0a 半透明黑、次像素灰——字节序正确。
  两个 libdbus 封送坑:dict 键的 `append_basic` 必须传 `&key`(char**);固定数组
  `append_fixed_array` 必须传 `&数组指针`(指针的指针),否则 memcpy/strlen 崩。
  **多实例(已实证)**:两个 TraySetIcon 脚本各自注册独立的
  `org.kde.StatusNotifierItem-<pid>-1` 服务 + 各自图标(IconName 不同)+ 各自
  A_TrayMenu,点击各自菜单 id=1 分别触发各自回调(one/two 独立)——互不干扰
  (宿主按服务名识别,Id 相同 'autohotkey' 不影响)。KDE 宿主未实测。
  **未做(诚实登记)**:KDE Plasma 宿主未实测(VM 无 KDE);
  Attention/AttentionIcon 等扩展(AHK 无对应触发 API)。

### 5.3 M6 补注:打包格式对齐 Ahk2Exe 的实际做法

- Ahk2Exe 的真实机制是:复制 `AutoHotkeySC.bin`(裁剪版解释器),把预处理并
  合并 `#Include` 后的脚本文本**作为 RCDATA 资源(资源名 `>AUTOHOTKEY SCRIPT<`,
  基于 .exe 时用 ID `#1`)写入 PE**,运行时从资源加载而非文件系统
  ([Ahk2Exe Compiler.ahk](https://github.com/AutoHotkey/Ahk2Exe/blob/master/Compiler.ahk)、
  [社区解释](https://www.autohotkey.com/boards/viewtopic.php?t=49527))。
  即:**没有字节码,只是"解释器 + 内嵌脚本资源"**。
- 因此 Linux 侧的等价物就是"ELF + 附加段/尾部载荷",语义完全对得上,
  文档可以直说"与 Windows 的编译是同一种东西(内嵌脚本),不是编译成机器码"——
  顺带消除用户对性能的误解。实现细节两选一:
  1. **追加载荷**(推荐):`[runtime ELF][tar: 主脚本+FileInstall 资源][magic+len footer]`,
     启动读 `/proc/self/exe` 尾部;实现简单、对 strip/签名友好;
  2. **ELF section**:用 `objcopy --add-section .ahkscript=...`,更"正式"但对
     后续压缩/签名链更挑剔。
- 同时要实现的配套:`A_IsCompiled`、`FileInstall` 的解包语义、
  `--pack-appimage`(复用 `tools/linux/pack-appimage.sh`)。
- **验收**:打包物在**无 ahk 安装**的 debian:12 容器里跑 doccheck 子集全绿;
  `A_IsCompiled=1`;`FileInstall` 资源可正确落盘。

---

# 第二部分 P1 级缺失细化与解决思路

## 6. P1:Wayland Unicode 输入的安全与体验

### 6.1 缺失细化(在 §0 修正后的真实缺口)

| 编号 | 缺口 |
|---|---|
| U1 | 粘贴回退只保存/还原**文本**目标;原剪贴板为图片/富文本时被破坏 |
| U2 | 等待消费仍有上限竞态:慢应用超时后内容可能粘到旧值;剪贴板管理器在窗口期记录敏感文本 |
| U3 | wlroots 明明有更优解(virtual-keyboard 自带 keymap 上传)却在用剪贴板回退 |
| U4 | 无 IME 通路:mutter 无 input-method 协议符号、sway 仅 v1 XML(已记录 docs/IME-Integration.md);中文输入法场景整体缺位 |
| U5 | 敏感场景策略只有全局 `AHK_WAYLAND_PASTE=0`,脚本级/调用级不可控 |

### 6.2 解决思路

- **U3 优先(治本,消灭大部分回退场景)**:zwp_virtual_keyboard_v1 的
  keymap 是客户端上传的 —— 发送 Unicode 时**动态构造私有 xkb keymap**,
  全程不碰剪贴板、不影响其他客户端(keymap 是 per-virtual-keyboard 的,
  **没有 X11 那个 server-wide 竞态**)。落点:`core_wayland_linux.cpp` 注入路径。
  - **参考实现已存在且可直接对照**:[wtype](https://github.com/atx/wtype)
    就是这么做的 —— 维护 `keycode → (keysym, wchar)` 表,遇到新字符就
    `xkb_utf32_to_keysym()` 追加一项,把整张表写成 xkb_keymap 文本
    (`xkb_keycodes` 段 `<K1>=9, <K2>=10…`,`xkb_types`/`xkb_compatibility`
    直接 `include "complete"`,`xkb_symbols` 段逐键写 keysym 名),
    经临时文件 fd 由 `zwp_virtual_keyboard_v1.keymap()` 上传后再发键
    ([main.c 的 `upload_keymap`/`get_key_code_by_wchar`](https://github.com/atx/wtype/blob/d71be3a7/main.c))。
  - **比 wtype 更优的一点**:wtype 是一次性进程,退出即销毁虚拟键盘;AHK 是常驻
    进程,应做**增量 keymap**(缓存已映射字符,仅在出现新字符时重传 keymap,
    并在 keymap 变更后 `wl_display_roundtrip` 确保合成器已接收再发键),
    避免每次 `SendText` 都全量重传。
  - **Unicode keysym 规则**:码点 ≥0x100 时 keysym = `0x01000000 + cp`
    ([xkbcommon keymap 文本格式](https://xkbcommon.org/doc/current/keymap-text-format-v1-v2.html)),
    与本仓库 X11 侧已用的规则一致,两条路径可共用码点→keysym 转换函数。
  - **适用范围**:仅 wlroots 系(sway/Hyprland/river/niri…);**GNOME 与 KDE
    都不提供 virtual-keyboard 协议**(mutter 明确推荐改用 libei,见 §1.2-D),
    因此 U3 落地后仍需保留剪贴板回退给这两家,除非走 §1.2-D 的 libei/IME 路径。
    sway CI 可直接断言 `SendText "你好"` 端到端。
- **U1**:剪贴板回退保留(GNOME/KDE 仍需要),保存/还原改为**全 MIME 往返**:
  枚举 offer 的全部 mime,逐一读出暂存(上限如 8MB,超限则只存文本并警告),
  还原时重建多 mime data_source;X11 侧 `assert_clipboard` 已有多 MIME 设施
  可复用为 Wayland 断言。实现时优先用 §4 落地的 `ext_data_control_v1` 通道读写,
  以免走核心 `wl_data_device` 路径 —— 后者要求客户端有焦点表面,这正是
  wl-clipboard 在 GNOME 下必须"造隐形窗口窃取焦点"的原因,已知会引发一连串
  quirk(GNOME 还有焦点窃取防护),不适合常驻自动化工具。
- **U2**:等待从"定时"改"事件":自建 data_source 的 `send()` 回调即"已被
  消费"信号,收到后再延迟一拍还原;超时路径行为改为**还原并抛 OSError**
  (宁可失败也不留脏剪贴板),错误文本含"increase AHK_CLIPBOARD_TIMEOUT_MS"。
- **U5**:加脚本级开关 `A_ClipboardPasteFallback := 0|1`(线程局部,默认承接
  env),`SendText` 文档加"密码请勿经剪贴板回退"警告框;粘贴回退发生时
  OutputDebug 记录一条(可审计)。
- **U4(长期):IBus 引擎是 GNOME 侧唯一"官方"文本注入通道**。本轮检索给出了
  权威佐证:GNOME 开发者在讨论"远程桌面如何注入任意 Unicode"时的结论正是
  "**目前最佳做法是让宿主进程连接 IBus、注册为 engine,并在需要输入时把自己设为
  当前 engine**",同时明确 libei 只能发 keycode、且 mutter 的 Wayland 后端不会像
  X11 那样为 keysym 临时分配 keycode
  ([GNOME discourse](https://discourse.gnome.org/t/injecting-arbitrary-unicode-characters/24799))。
  据此把 U4 拆成可执行的两步:
  1. **R4 交付调研 + 原型**:`ahk-ibus-engine` 最小实现(注册 engine、
     `commit_text()` 发送任意字符串、发完切回原 engine),先在 GNOME VM 手工验证;
  2. **风险如实记录**:切换当前 engine 会打断用户正在进行的输入法组合;
     必须限定在"脚本显式请求 Unicode 发送且无其他通路"时启用,并提供
     `AHK_IME_INJECT=0` 关闭。
  - wlroots 侧继续跟踪 input-method-v2(现状:sway 仅 v1 XML、mutter 无
    input-method 符号,已记录 `docs/IME-Integration.md`,结论不变)。
  - 该讨论还提到 GNOME 有意让远程桌面接口未来能承担 IME 角色(由合成器代理
    text-input 协议)—— 属于"值得跟踪但不可依赖"的上游方向,写进路线图即可。

---

## 7. P1:跨应用控件自动化深度不足

### 7.1 缺失细化

| 编号 | 缺口 | 现状锚点 |
|---|---|---|
| C1 | AT-SPI 只有"读文本/设文本/点击"最小路径,无树查询、无 ClassNN 概念、无等待机制 | `core_atspi_linux.cpp` |
| C2 | X11 控件 enabled/checked/style 读 shadow,外部应用自改状态读不到 | `core_ctrl_linux.cpp` |
| C3 | 目标应用 a11y 未开启时静默无结果(GTK/Qt/Electron/Java 各有开启开关) | — |
| C4 | 无 Qt/Electron/LibreOffice/Firefox/Java 兼容矩阵,GTK3 示例代表性不足 | check0821 §P1 |
| C5 | `MenuSelect` 报错、`CaretGetPos` 仅 GTK 自绘控件 | `core_mdfunc.cpp:1005`、`AhkGtkCaretGetPos` |

### 7.2 解决思路

- **C1 → AT-SPI 深化(分两期)**:
  - 一期(查询):实现树遍历 + 匹配 —— `ControlGetHwnd/ControlGetText/
    ControlClick` 的 Control 参数支持 `role` + 序号定位(把 AT-SPI role
    映射成 ClassNN 风格:`push button`→`Button1/2/...`,按深度优先序编号,
    与 Windows ClassNN 的心智一致);优先用 `org.a11y.atspi.Collection.GetMatches`
    (一次 D-Bus 往返),无 Collection 的应用回退递归 `GetChildren` + 进程内
    缓存(树版本用 children-changed 事件失效);
  - 二期(状态与坐标):`ControlGetEnabled/Checked/Visible` 改读 AT-SPI
    StateSet(**同时解决 C2 的 Wayland 侧**);`ControlGetPos` 走 Component
    extents;`WinWait/ControlWait` 类等待接 `object:state-changed` 事件而非
    轮询。
- **C2 → X11 shadow 降级为最后手段**:读取顺序改为
  AT-SPI(若应用可达)→ X 属性/WM hints(enabled 可从 `WM_HINTS.input`、
  checked 无标准则如实报能力错误)→ shadow(仅自建 GUI);每级来源在
  `ControlGetStyle` 类函数文档标注,消灭"读到假值"。
- **C3 → 自动开启 + 诊断(本轮把速查表核实并补全)**:
  - **总线级门控**:AT-SPI 桥接由 D-Bus 属性 `org.a11y.Status.IsEnabled` 控制,
    默认在无障碍未启用/无头会话下为 false,**此时所有工具包都不会发布可访问性树**;
    GNOME 侧由 `org.gnome.desktop.interface toolkit-accessibility` 这个 GSettings
    键触发 `at-spi-bus-launcher` 自启
    ([at-spi2-core bus/README](https://github.com/GNOME/at-spi2-core/blob/main/bus/README.md))。
    实现:检测到该属性为 false 时,`ahk --diag` 明确报告并给出开启命令;
    自动开启需用户确认(改全局无障碍开关属于系统级副作用,不应静默执行),
    开启后记录原值以便回滚。
  - **应用级速查表(已核实,直接进文档)**:
    | 目标 | 需要的开关 |
    |---|---|
    | GTK 原生应用 | 无需额外开关(桥接默认开) |
    | Chromium/Electron(VS Code、Slack、Discord…) | 启动参数 `--force-renderer-accessibility`,或环境变量 `ACCESSIBILITY_ENABLED=1`;否则只暴露 application→frame 骨架,查控件恒返回 0 个 |
    | Firefox | 环境变量 `MOZ_ACCESSIBILITY_ATK2=1`(旧方案 `GNOME_ACCESSIBILITY=1` 已被 `accessibility.force_disabled=-1` 取代,且该 pref 在 Linux 上直到 FF108 才修好) |
    | Qt | `QT_ACCESSIBILITY=1` / `QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1` |
    | Java | 需装 ATK 桥接(如 `java-atk-wrapper`) |
    | 兜底 | `GTK_MODULES=gail:atk-bridge`、`GNOME_ACCESSIBILITY=1` |
    (依据:[xa11y 的 API quirks 汇总](https://xa11y.dev/explanation/accessibility-quirks/)、
    [ArchWiki Accessibility](https://wiki.archlinux.org/title/Accessibility)、
    [Chromium 无障碍文档](https://www.chromium.org/developers/design-documents/accessibility/);
    VS Code 曾长期不接受该参数,见 [microsoft/vscode#84833](https://github.com/microsoft/vscode/issues/84833) ——
    因此 VS Code 场景要在矩阵里单独标注版本。)
  - **产品化建议**:`ControlGetText` 等函数在目标应用**可见但 a11y 树为空**时,
    不要返回空串,而是抛出带诊断的错误:"target exposes no accessibility tree;
    Chromium/Electron apps need --force-renderer-accessibility (see docs)"——
    把生态里最常见的"静默失败"变成一次性可解决的问题。
  - `ahk --diag`(§12)输出:`org.a11y.Status.IsEnabled`、a11y 总线地址、
    当前焦点应用的 toolkit 与其树深度(0 即判定为未开启)。
- **C4 → 兼容矩阵驱动开发**:固定 8 个矩阵应用(gtk3-demo、gedit/GTK4、
  kcalc/Qt5、kate/Qt6、VS Code/Electron、LibreOffice Writer、Firefox、
  SwingSet/Java),每应用 3 断言(找到控件树、读文本、点按钮),挂 VM 夜跑;
  结果表自动生成进 docs(§10)。矩阵先跑出来,再按失败项排修复优先级 ——
  不预设"都要通"。
- **C5**:`MenuSelect` 走 AT-SPI menu role 路径实现(GTK/Qt 菜单在 a11y 树
  可见、可 DoAction),实现后撤销"不可用"文档;`CaretGetPos` 二级来源接
  AT-SPI Text caret-offset + extents(覆盖非 GTK 应用),仍不可得时保持
  文档化失败。

---

## 8. P1:GTK3 GUI 是子集

### 8.1 缺失细化

| 编号 | 缺口 |
|---|---|
| G1 | 选项"接受但忽略"不可见:字体/颜色/边距/Resize/MinMax 等静默丢弃,脚本作者无从得知 |
| G2 | OnEvent 变体不全(DropFiles、ContextMenu、部分 Ctrl 事件待逐一核对) |
| G3 | ActiveX/自绘类控件无对应(架构性,不做) |
| G4 | 菜单/HMENU 句柄语义与 Win32 不同(`MenuFromHandle` 恒空) |
| G5 | GTK3 生命周期风险:GTK4 迁移成本未规划;`script_gui_linux.cpp` 已 193KB 单文件 |

### 8.2 解决思路

- **G1(先做,小改大收益)**:
  1. 选项解析器改造:每个被解析但无实现的选项统一走
    `GuiOptUnsupported(name)` —— 默认 OutputDebug 一条,
    `AHK_GUI_STRICT=warn|error` 时升级为警告对话框/抛错;
  2. 由解析器代码**自动生成选项支持表**(脚本扫 `script_gui_linux.cpp` 的
    option switch,产出 supported/ignored/error 三态 TSV),挂进文档生成
    (§10),从此"子集边界"有权威清单;
  3. 高频被忽略项排序实现:字体(Pango font description)、前景/背景色
    (GtkCssProvider per-widget)、`+Resize/-MaxSize/MinSize`
    (`gtk_window_set_geometry_hints`)、边距(margin 属性)—— 这四类覆盖
    绝大多数真实脚本的 GUI 差异。
- **G2**:对照官方 GuiOnEvent 文档列全事件×控件矩阵,缺失项按
  "有 GTK 信号对应→实现;无→文档 N/A"二分;DropFiles
  (`gtk_drag_dest_set` + uri-list)与 ContextMenu(`button-press` 3 键/
  `popup-menu` 键)优先。
- **G4**:`MenuFromHandle` 维持空串 + 文档;Menu 对象自身句柄语义在
  dbusmenu 桥(§5-M5)落地后统一说明。
- **G5(债务控制)**:
  1. 先把 `script_gui_linux.cpp` 按控件族拆文件(edit/list/button/
     container/window,机械移动不改逻辑),bridge 头(`script_gui_linux_bridge.h`)
     作为唯一跨界面;
  2. 新代码禁用 GTK3-only 已废弃 API(编译加 `GDK_DISABLE_DEPRECATED`
     于新文件);
  3. GTK4 迁移**不启动**,但建立"迁移摩擦清单":CI 加一个
     `-DGTK4_PROBE=ON` 的编译探测 job,统计不可编译点数量趋势,
     等 GTK3 出现 EOL 信号再决策 —— 把未知风险变成有仪表的风险。

---

## 9. P1:"1099/1099"的测试口径问题

### 9.1 缺失细化

| 编号 | 缺口 |
|---|---|
| T1 | 断言粒度=调用成功,不代表语义等价(审计自己承认是"采样式") |
| T2 | 无差分基准:同一脚本没有在官方 Windows AHK 上跑出"金标准输出"做对比 |
| T3 | 测试与实现同作者,验证已知路径的偏置 |
| T4 | 数字对外传播容易被读成"99.7% 兼容" |

### 9.2 解决思路

- **T2 → 差分测试框架(结构性解法,优先)**:
  1. 新目录 `tests/diff/`:挑选纯语言/文件/字符串/对象/日期/正则域的脚本
     (GUI 与输入域除外),每脚本产出规范化 stdout(路径、换行、locale
     归一化);
  2. CI 加 `windows-latest` job:下载官方 AutoHotkey v2.0.26 便携版,跑同
     一批脚本存 `expected_windows/*.txt` 工件;Linux job 跑移植版,
     **逐字节 diff**;
  3. 差异三分类:真 bug(修)、平台必然差异(写入 per-test 豁免文件并
     文档化)、测试不可移植(改写);豁免文件本身就是"已知语义差异
     权威清单"的雏形,喂给 §13 的 parity 标签;
  4. **Wine 只能做语言层冒烟,不能当 GUI/输入基准**:社区长期实践表明官方 AHK
     在 Wine 下"很多东西能跑"(FileRead/IniRead/Menu/MsgBox/ToolTip/剪贴板等),
     但同时存在成片的行为偏差 —— 主题化按钮文字对齐错误、图片 alpha 通道被忽略、
     菜单不响应字母快捷键、MsgBox 不置前、IniWrite 吞空行,且 Wine 不严格遵循
     所设 Windows 版本的行为
     ([AHK 论坛的移植实践帖](https://www.autohotkey.com/boards/viewtopic.php?style=17&f=22&t=65552))。
     **结论**:Wine 只用于 `tests/diff/` 中"纯语言/字符串/对象/正则/日期"子集的
     PR 级快反馈;GUI、输入、窗口、剪贴板域的金标准**必须**来自 `windows-latest`
     runner 上的官方 AHK,并在框架里用 `basis: wine|windows` 字段标注每条基准的
     来源,避免把 Wine 的 bug 当成移植版的 bug(反之亦然)。
- **T1 → 语义深度分层计数**:worklist.tsv 加 `depth` 列
  (call-ok / behavior-sampled / behavior-diffed / scenario-covered),
  README 的数字改为分层报告:"1099 断言(其中 diffed N,scenario M)"——
  同时解决 T4 的传播口径。
- **T3 → 外部语料回归**:收集公开 AHK v2 脚本语料(awesome-AutoHotkey 中
  明确 v2 的库,遵许可 vendor 进 `tests/corpus/`),先做**解析层全量**
  (load 不执行,报语法/未实现函数统计),再挑无副作用者进执行层;
  每版本发布"语料兼容率"趋势,替代单一断言数字。
- **T4 → 传播规范**:README 数字旁固定一句话口径("在项目定义的 Ubuntu
  CI 环境与项目自建断言下的通过率,非 Windows 兼容度"),已在 §0 核实
  README 有类似弱化,但要把同一句同步进 release notes 模板与 About。

---

## 10. P1:文档与状态口径漂移

### 10.1 缺失细化

- 实锤漂移点:`MODULE_MATRIX.md:4`/`:112` = 1069,README = 1099;
  顶层 CMake 注释 "still in progress";`source/linux/README.md` 早期措辞;
  GitHub About 曾滞后(check0820 记录 1053)。
- 根因:数字与状态由人工在 ≥6 个文件里重复维护
  (README/GOALS/MODULE_MATRIX/CHECK_REPORT/linux-port.htm/About)。

### 10.2 解决思路

1. **单一数据源**:`tests/doccheck/status.json` 由测试运行器生成
   (断言数、各套件数、backend caps、Wayland level 结果、parity 统计);
2. **文档生成器** `tools/gen_status_docs.py`:模板渲染 README 的状态块
   (用 `<!-- STATUS:BEGIN/END -->` 标记区)、MODULE_MATRIX 全表、
   linux-port.htm 能力矩阵、SUPPORT_MATRIX.md;
3. **CI 门禁**:现有 `verify_report_numbers.sh` 扩展为
   "重新生成 + `git diff --exit-code`",漂移即红;About 无法自动化,写进
   `RELEASING.md` 发布清单勾选项;
4. 陈旧叙述一次性清理:`source/linux/README.md`、CMake 注释改为指向
   生成文档的一行链接,不再手写状态;
5. **快速止血(本轮就做)**:先手工把 MODULE_MATRIX 的 1069→1099 改对,
   再上生成器 —— 避免修架构期间继续误导。

---

## 11. P1:stdafx_linux.h 兼容层技术债

### 11.1 缺失细化

- 3366 行/86KB 单头文件承载:Win32 类型、宏、以及**行为 shim**三类混体;
- 危险模式已发生过实害:`AddClipboardFormatListener` no-op → OnClipboardChange
  静默失效(§4);无法系统回答"还有多少个这样的沉默桩";
- 平台边界不清 → 上游合并冲突、贡献者理解成本。

### 11.2 解决思路

1. **shim 普查(先量化,再治理)**:写 `tools/audit_shims.py` 扫
   `stdafx_linux.h` 全部 inline 定义,交叉 grep 上游代码调用点,产出
   `SHIM_CENSUS.tsv`:每个 shim 标注四类 —— `real`(转发到真实实现)/
   `adapt`(语义近似)/`noop-safe`(确无副作用)/`noop-risky`
   (调用方依赖其效果);`noop-risky` 项逐个建 backlog(OnClipboardChange
   就是该类的第 1 号样本);
2. **运行时哨兵**:debug/ASan 构建里给 `noop-*` shim 注入首调用日志宏
   `AHK_SHIM_TRAP(name)`,doc-check 全量跑完后断言"触发清单 ⊆ 已知豁免
   清单"—— 新代码路径踩到沉默桩会立刻红,堵住"编译过=实现了"的漏洞;
3. **分层拆分(机械重构)**:`stdafx_linux.h` →
   `win32_types.h`(纯类型/常量,无行为)+ `win32_shims.h`(行为桩,
   每条带 census 标签注释)+ 真实实现全部下沉 `core_*_linux.cpp`;
   头文件行数纳入 CI 趋势指标(只许降不许升);
4. **边界规约**:`PlatformAbstraction.h`(现仅 2.3KB)扩为正式接口层的
   落点 —— 规则:新功能不得新增行为 shim,必须走 `core_*` 实现 + 上游调用
   点条件编译;写进 CONTRIBUTING;
5. **上游同步预案**:建 `upstream` 分支跟踪官方 v2 tag,rebase 脚本 +
   冲突热点清单(shim 普查表顺带产出"上游文件×shim 依赖"矩阵),把未来
   v2.0.27 合并成本从未知变为清单化。

---

## 12. P1:社区验证几乎为空

### 12.1 缺失细化

- 1 star/0 fork/0 issue,且 issue 创建受限(check0820 核实)—— 反馈通道
  本身关闭;
- 单维护者:输入注入/扩展/守护进程这类安全敏感组件无第二双眼睛;
- 发布策略矛盾:`--update` 支持指定版本回滚,但 linux.15 之前的 release
  资产已删除(git tag 尚在),二进制回滚实际不可用;
- 无环境诊断工具,用户就算想报 bug 也难提供有效信息。

### 12.2 解决思路

1. **打开反馈通道(零成本,立刻)**:开放 Issues;两套模板
   (bug:必填 `ahk --diag` 输出;compat report:发行版/DE/后端/场景清单
   打勾表),discussion 板归集"能不能跑我的脚本"类问题;
2. **`ahk --diag` 诊断命令**(新,~1 天):输出版本、会话类型
   (X11/Wayland+合成器名)、活跃输入后端与 caps、扩展/portal/inputd
   可达性、a11y 总线状态、uinput 权限、GTK/glibc 版本 —— 一条命令生成
   可粘贴的环境快照,是所有远程 debug 的地基;
3. **发布策略修正**:自 linux.15 起承诺保留全部 release(README 已写,
   执行住);每版附 SHA-256 + OpenPGP 签名(已有)+ CHANGELOG 段落;
   `--update` 文档写明可回滚窗口;
4. **分发面扩大换用户**:AUR(`tools/linux/PKGBUILD` 已有,发布到 AUR)、
   Flathub beta、OBS(openSUSE Build Service 顺带产多发行版 RPM)——
   每个渠道都是真实环境反馈源;
5. **定向征募**:AutoHotkey 官方论坛 Linux 板 + r/AutoHotkey 发 technology
   preview 征测帖,附 compat report 模板链接;明确征 KDE/Fedora/Arch 用户
   (正是 CI 盲区);
6. **安全审计外包**:extension、粘贴回退、未来 ahk-inputd 三件套整理成
   威胁模型文档后,邀请外部 review(哪怕是论坛资深用户),审计结论进 docs。

---

# 第三部分 P2 与横向工程

## 13. P2:功能命名与语义分级

### 13.1 缺失细化

- `✅/⚠️/❌` 三态不足以表达"Registry 是文件模拟""SendInput 不是 SendInput"
  这类差异;用户在函数页看不到"这在 Linux 上到底是什么"。

### 13.2 解决思路

1. **四级 parity 分类法落库**:`worklist.tsv` 加 `parity` 列 ——
   `P1 compatible`(行为高度一致)/`P2 adapted`(同目标不同行为:D-Bus COM、
   .desktop 快捷方式、SNI 托盘)/`P3 simulated`(仅进程内状态:注册表文件、
   shadow style)/`P4 unavailable`(明确报错:ComObjArray、Win32 消息投递);
   初始值由 AUDIT_2026_WEAKENED.md §2 逐条映射(审计已把料备齐,一次
   编目即可);
2. **文档自动打标**:§10 生成器往每个函数页头部注入 parity 徽章 + 一句话
   差异说明(来自 tsv 的 note 列);linux-port.htm 的总矩阵按四级重排;
3. **运行时可查**:`ahk --parity FuncName` 打印级别与说明;脚本内
   `A_ParityLevel(FuncName)`(或并入 §1 的 caps API)供防御式脚本自检;
4. **严格模式**:`AHK_STRICT_PARITY=warn|error` —— P3/P4 函数首次调用时
   警告/抛错,迁移 Windows 脚本时开启即可把"假兼容"变成显式清单
   (与 §5-M7 lint 互补:一个静态一个动态)。

## 14. 场景化验收测试(check0821 第一优先级的落地设计)

### 14.1 缺失细化

- 现有断言以"函数"为轴;check0821 要求的 30-50 个"用户场景×桌面环境"
  矩阵不存在;15 个种子场景无一有跨环境结果记录。

### 14.2 解决思路

1. **场景清单格式**:`tests/scenarios/<id>.yaml`:
   `{id, title, script, envs: {x11, gnome-wl, kde-wl, sway}, expect:
   pass|partial|unsupported(reason), needs: [uinput|extension|portal-confirm]}`;
   runner `run_scenarios.sh` 按当前环境执行可测子集,产出 JSON 结果;
2. **15 个种子场景的具体测法**(节选关键设计,全部可自动化或半自动化):

| 场景 | 测法要点 |
|---|---|
| CapsLock/Esc 双角色 | inputd remap + xkeycap 断言 tap 出 Esc、hold 出修饰;无 inputd 环境 expect=unsupported |
| `a & b` 组合 | evdev lane 状态机;X11 下 expect=unsupported(现注册即报错,断言错误文本) |
| key-down/长按/up | assert_repeat 扩展:长按 1s 事件计数区间断言 |
| 中英文热字串 | X11:现有 assert_hotstring;Wayland:依赖 inputd 捕获,分级 expect |
| 多脚本并发 | 两进程注册不同热键 + 同热键冲突错误断言;并发 SendText Unicode(X11 借键码竞态回归) |
| VS Code/Firefox/LibreOffice/终端 | AT-SPI 矩阵(§7-C4)复用;终端用 vte 探针 |
| GNOME/KDE/Sway | vmctl.py 三 VM 夜跑同一场景集 |
| 多显示器/缩放 | Xvfb 双屏 + `xrandr --scale`;断言 MonitorGet/鼠标坐标换算 |
| 多布局/输入法 | assert_layout 已有 us↔de;加 fcitx5 激活态下热字串行为记录 |
| 剪贴板管理器并存 | VM 装 copyq 后跑粘贴回退,断言还原且 copyq 历史含/不含中间值(结果如实记录进文档) |
| 锁屏/休眠/重登录 | VM:loginctl lock-session 后热键静默、解锁恢复;GNOME 扩展在 unlock 后 re-sync 断言 |
| 扩展重载 | 已有 gnome_ext_reregister.sh,纳入场景清单 |
| Flatpak 沙箱应用 | VM 装 Flatpak 目标,AT-SPI 经 portal 的可达性 + SendText 到沙箱窗口 |
| 崩溃后按键释放 | 已有 kill9 断言(X11);inputd 版补 evdev grab 释放断言 |
| 高频输入/CPU | soak:10 分钟 20Hz Send + 热字串监听,采样 CPU<5%、RSS 平稳、无事件丢失计数 |

3. **结果即文档**:runner 输出汇入 `status.json` → 生成 `SUPPORT_MATRIX.md`
   (场景×环境×等级),替代形容词式的"supported"。

## 15. Wayland 支持分级的发布口径

### 15.1 缺失细化

- check0821 已给三级定义;仓库缺的是把它变成**发布口径与代码事实的绑定**。

### 15.2 解决思路

- Level 定义进 `docs-v2/docs/wayland-levels.htm`,判据可执行化:
  - L1 独占热键:`caps.global_hotkeys && caps.suppress`;
  - L2 文本输入:`LinuxWaylandCanInjectKeys() || paste_fallback`,细分
    L2a(直注)/L2b(剪贴板回退,带安全告警);
  - L3 完整代理:inputd 活跃且 `caps.passthrough && caps.key_up &&
    caps.wildcard`(含 remap/组合);
- 启动时把当前会话的判定结果写进 `A_WaylandLevel`(如 "L1+L2b"),
  `ahk --diag` 同步输出;文档矩阵按 GNOME/KDE/wlroots × L1/L2/L3 填
  **实测值**(§14 的 VM 结果),未实测标 "declared";
- 所有对外表述(README/release notes)禁用裸词 "Wayland supported",
  必须带 Level —— 写进发布清单。

## 16. CI 矩阵扩展

### 16.1 缺失细化

- 单 ubuntu-24.04、单 GCC、Xvfb/sway 虚拟环境;无 Fedora/Arch/Debian、
  无 Clang、无真实 GNOME/KDE 会话、无 no-XWayland、无 ARM64、无长跑。

### 16.2 解决思路(按成本梯度)

1. **容器矩阵(便宜,先做)**:GitHub ubuntu runner 上以 container 跑
   `fedora:41 / archlinux / debian:12 / ubuntu:22.04` 四目标 ×
   {构建 + headless 回归 + Xvfb doc-check 子集};glibc/GTK 版本差异
   自然覆盖;
2. **编译器矩阵**:`build: [core, asan]` 扩 `compiler: [gcc, clang]`
   (clang 只跑 core,控制时长);
3. **no-XWayland job**:sway 配置 `xwayland disable`,跑 Wayland 套件 ——
   验证"纯 Wayland 降级路径不崩、报错清晰"(现在没有任何 job 证明这点);
4. **VM 夜跑(核心增量)**:GitHub 的 **x86_64** Linux runner 已开放 KVM ——
   官方在 2024-04 宣布 2-vCPU 起的 GitHub-hosted Linux runner 均可用硬件加速,
   但需要先给 runner 用户加 kvm 组权限
   ([GitHub Changelog](https://github.blog/changelog/2024-04-02-github-actions-hardware-accelerated-android-virtualization-now-available/)):
   ```yaml
   - name: Enable KVM
     run: |
       echo 'KERNEL=="kvm", GROUP="kvm", MODE="0666", OPTIONS+="static_node=kvm"' \
         | sudo tee /etc/udev/rules.d/99-kvm4all.rules
       sudo udevadm control --reload-rules
       sudo udevadm trigger --name-match=kvm
   ```
   在此基础上 `vmctl.py` 驱动 GNOME(48/49)与 KDE Plasma 两台无头 VM,跑
   §14 场景集 + 扩展/portal/AT-SPI/libei 端到端;每夜 + 发布前必跑,PR 不阻塞。
5. **ARM64(降级预期)**:`ubuntu-24.04-arm` runner 可做**编译 + headless 回归**,
   但**不能跑 KVM VM** —— Azure 的 ARM 实例尚不支持嵌套虚拟化,GitHub 官方回复
   "not yet available in Azure"([actions/runner-images#14062](https://github.com/actions/runner-images/issues/14062))。
   因此 ARM64 上不要规划图形/VM 场景,只做可移植性(编译告警、对齐、endian 无关性)
   与 headless 语言层回归。
6. **soak job(每周)**:§14 高频场景 30 分钟版 + ASan/LSan,盯内存与
   fd 泄漏趋势;
7. **打包安装矩阵**:现有 install/run/uninstall 验证扩展到容器四目标
   (deb 在 debian 系、RPM 在 fedora、AUR PKGBUILD 在 arch 容器 makepkg)。

---

## 17. 路线图汇总(建议执行顺序)

按"先止血 → 先可见 → 后攻坚"排序,R 为轮次概念(与仓库 round 节奏对齐):

| 轮次 | 内容 | 对应章节 | 性质 |
|---|---|---|---|
| R1(止血/quick wins) | MODULE_MATRIX 数字修正;OnClipboardChange X11(XFixes);注入日志 server-time+消费修复;GuiOpt 忽略告警;shim 普查脚本 + 哨兵;`ahk --diag`;开放 Issues;**GNOME 48 portal 后端探测 + auto 策略修正**;**XWayland-XTEST-经-libei 假设验证** | §10、§4、§2-A、§8-G1、§11、§12、§1-C、§1-D | 多为 1-3 天级单项 |
| R2(语义与可见性) | SendEvent/SendInput 拆分 + KeyDelay;SendLevel 进程内建模;event-tap(**XI 2.1** sourceid)+KeyWait 事件化;parity 四级编目 + 文档生成器 + CI 门禁;**ext-data-control 剪贴板监听 + GNOME 扩展 Meta.Selection 信号**;托盘 SNI + 通知(自实现 dbusmenu) | §2-B/C、§3、§13、§10、§4、§5-M5 | 中型,X11/KDE 语义补齐 |
| R3(Wayland 攻坚) | 逐热键路由 + caps 脚本 API;ahk-inputd MVP(独占/透传/up/**keyd 式 panic 键**);wlroots 私有 keymap Unicode 直注(**对照 wtype**);**libei 注入后端(liboeffis + 会话持久化)**;差分测试框架(**Wine 仅语言层**);容器 CI + no-XWayland;场景 runner + 种子 15 场景 | §1-A/B/D、§6-U3、§9、§16、§14 | 大型,含守护进程 |
| R4(深度与生态) | inputd remap/`a&b`/热字串捕获;AT-SPI 二期 + 8 应用矩阵(**含各 toolkit 开关**);libffi ABI;`--pack` 打包(**Ahk2Exe 同构**);KDE/GNOME VM 夜跑(**x86 KVM;ARM 不做 VM**);AUR/Flathub 分发;**IBus engine 原型** | §1-B、§7、§5-M4/M6、§16、§12、§6-U4 | 大型,矩阵驱动 |

**exa 深化后新增/改变优先级的条目**(相对 rev1):

| 条目 | 变化 | 原因 |
|---|---|---|
| XWayland → XTEST → libei 验证 | **新增到 R1** | 若成立,GNOME Wayland 注入缺口在文档层面即可大幅收窄,零开发成本 |
| GNOME 48 portal 后端 | **新增到 R1** | auto 策略在 GNOME 45-47 无扩展时会错选 portal;且 48+ 削弱了扩展的必要性,文档需重述 |
| portal app-id | **提升为必做** | 上游已把 valid AppID 列为 GlobalShortcuts 的硬性要求,不再是"可能被拒" |
| ext-data-control-v1 | **替换 wlr-data-control** | wlr 版官方标注废弃;ext 版支持面含 KWin 6.6,让 KDE 从"无方案"变"原生可用" |
| GNOME 剪贴板监听 | **从轮询改为扩展信号** | Mutter 永不支持 data-control;Meta.Selection `owner-changed` 是唯一正解且有成熟先例 |
| XI 2.1 协商 | **新增实现约束** | XI 2.0 的 `sourceid` 恒为 0,不协商版本会让整个"区分注入/物理"方案静默失效 |
| libei 注入后端 | **新增到 R3** | 免 root、跨合成器、GNOME/KDE 均已实现,优先级高于把 uinput 当 GNOME 唯一解 |
| InputCapture portal | **明确标注"暂不可用"** | 仅有指针屏障触发器,无按键触发;Level 3 短期只能靠 evdev |
| ARM64 VM 场景 | **从计划中移除** | Azure ARM 实例无嵌套虚拟化,GitHub 官方确认 KVM 不可用 |
| libayatana-appindicator | **明确不采用** | 其重写为 Gio menus 后偏离 SNI 规范,破坏多数 bar 的菜单实现 |

三条不变式贯穿全程(比任何单项都重要):

1. **不许沉默弱化**:新桩必须 `AHK_SHIM_TRAP` 或注册时报错 ——
   OnClipboardChange 类事故只允许发生这一次;
2. **数字必须机器生成**:任何人工维护的状态数字都会再次漂移;
3. **能力必须可查询**:脚本、CLI、文档三处的能力口径同源(caps/parity/
   level 一套数据),"同名不等于兼容"从警告变成可编程的事实。

---

## 附录 A:外部技术情报与参考实现(exa 深化结果)

按主题归档本轮检索到的一手依据,供实现时直接查阅。**"对本文的影响"一列标注
该条情报是否修正了 rev1 的方案**。

### A.1 Wayland 输入注入与捕获

| 来源 | 关键事实 | 对本文的影响 |
|---|---|---|
| [libei 官方文档](https://libinput.pages.freedesktop.org/libei/) / [API 首页](https://libinput.pages.freedesktop.org/libei/api/index.html) | libei=客户端库,libeis=合成器端(EIS),liboeffis=RemoteDesktop portal 的 DBus 封装;支持 sender(注入)与 receiver(捕获)两种 context | §1.2-D 新增 libei 注入后端方案 |
| [who-t: libei 在两个 portal 中的整合](http://who-t.blogspot.com/2026/07/libei-integrations-in-xdg-remotedesktop.html) | libei 自 xdg-desktop-portal **1.17(2023 中)**起进入 RemoteDesktop 与 InputCapture;**1.21.0 起支持会话持久化**;**Xwayland 23.2.0+ 自动把 XTEST 转成 libei 并代为走 portal,合成器无需额外支持** | §1.2-D 路径 1(零代码验证)与路径 2(原生后端)均源自此 |
| [liboeffis API](https://libinput.pages.freedesktop.org/libei/api/group__liboeffis.html) | 一个小 API 完成"连 session bus → 开 RemoteDesktop session → SelectDevices → ConnectToEIS → 拿 fd" | 实现选型:不手写 DBus 握手 |
| [GNOME discourse:注入任意 Unicode](https://discourse.gnome.org/t/injecting-arbitrary-unicode-characters/24799) | **libei 只能发 keycode,keymap 由 EIS 端决定**;mutter 的 Wayland 后端不像 X11 那样为 keysym 临时分配 keycode;GNOME 开发者推荐"连 IBus 注册为 engine" | §1.2-D 的限制段 + §6-U4 的 IBus 方案有了权威依据 |
| [wtype#22 中的上游答复](https://github.com/atx/wtype/issues/22) | mutter **不打算**实现 virtual-keyboard 协议,上游明确推荐改用 libei;KDE 同样不提供 | §1.2-D 的前提;§6-U3 适用范围收敛为 wlroots 系 |
| [InputCapture portal 文档](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.InputCapture.html) / [引入 PR #714](https://github.com/flatpak/xdg-desktop-portal/pull/714) | 触发器**目前只有指针屏障**;上游讨论确认"按需捕获需要新增 trigger 类型,目前没有";KEYBOARD 能力位存在 | §1.2-D2:Level 3 短期只能靠 evdev,但要跟踪该提案 |
| [KWin 输入捕获实现](https://invent.kde.org/plasma/kwin/-/merge_requests/5742) / [xdg-desktop-portal-kde](https://invent.kde.org/plasma/xdg-desktop-portal-kde/-/merge_requests/284) | KDE 侧 InputCapture 已于 2024-05 合并 | 说明 portal 路线在 KDE 已有基础设施 |
| [Chromium 改用 libei 注入](https://github.com/chromium/chromium/commit/1fccb06f36d05361baab49cc1443af6f1c57ecc9) | 生产级项目已切到 libei(先试 libei,失败回退) | 佐证方案成熟度与"能力探测 + 回退"的实现模式 |

### A.2 全局快捷键 Portal 与 GNOME 扩展

| 来源 | 关键事实 | 对本文的影响 |
|---|---|---|
| [GNOME 48 开发者说明](https://release.gnome.org/48/developers/) | GNOME 48 起应用可注册系统级全局快捷键,**GNOME 48 完整支持** | §1.2-C 重划扩展与 portal 的边界 |
| [xdg-desktop-portal-gnome 48.rc NEWS](https://gitlab.gnome.org/GNOME/xdg-desktop-portal-gnome/-/raw/gnome-48/NEWS) | "Add global shortcuts portal backend" | 同上,精确到版本 |
| [GlobalShortcuts portal 文档](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.GlobalShortcuts.html) | `BindShortcuts` **通常会弹配置/确认对话框**;一个 session 只能 bind 一次;`Activated`/`Deactivated` 成对 | 扩展价值=零确认+动态注册;Deactivated→key-up 有规范依据 |
| [xdg-desktop-portal NEWS.md](https://github.com/flatpak/xdg-desktop-portal/blob/main/NEWS.md) | "**Require a valid AppID** from apps to use the Global Shortcuts Portal (#1817)";新增 `ConfigureShortcuts`;信号带 activation token | app-id 从"建议"升级为"硬性要求",进 `--diag` 检查项 |

### A.3 剪贴板监听

| 来源 | 关键事实 | 对本文的影响 |
|---|---|---|
| [ext-data-control-v1 支持矩阵](https://wayland.app/protocols/ext-data-control-v1) | KWin 6.6、Sway 1.11、Hyprland、niri、COSMIC、labwc、Jay、Treeland 均实现;**Mutter 为 x(不支持)** | §4 主协议改为 ext 版;KDE 由"无方案"变"原生可用" |
| [wlr-data-control 协议页](https://wayland.app/protocols/wlr-data-control-unstable-v1) | 官方标注**已废弃,不建议生产使用**,推荐改用 ext 版 | §4 把 wlr 版降级为兼容回退 |
| [wl-clipboard 增加 ext 支持的提交](https://github.com/bugaevc/wl-clipboard/commit/091d6028b5c9db75ad36f9fceb0db3ee718045fa) | 两协议接口同构,优先 ext、回退 wlr;二者都避免了"焦点窃取"技巧 | §4/§6-U1 的实现模式与代码复用策略 |
| [mutter#524](https://gitlab.gnome.org/GNOME/mutter/-/issues/524) | Mutter 长期不支持 data-control 类协议 | GNOME 侧必须另寻通路 |
| [Clipman 架构文档](https://github.com/MohammedEl-sayedAhmed/clipman/blob/main/ARCHITECTURE.md) / [作者实现说明](https://mammar.pages.dev/blog/clipman-clipboard-manager-wayland-gnome/) | GNOME 正解:扩展里 `global.display.get_selection()` 连 **`owner-changed`**,用 `St.Clipboard` 按 MIME 优先级链读取后经 D-Bus 转发;轮询 `wl-paste` 会导致闪烁与漏拷贝;EGO 审核会对 `St.Clipboard` 打 `EGO-A-005` 人工复审标记 | §4 的 GNOME 方案由"轮询"改为"扩展信号",并预警上架审核 |
| [clipnotify 源码](https://github.com/cdown/clipnotify/blob/master/clipnotify.c) / [fixesproto §6](https://www.x.org/releases/current/doc/fixesproto/fixesproto.txt) | X11 侧最小实现约 80 行;`SelectionNotify.subtype` 区分三种成因 | §4 的 X11 路径可直接对照实现 |

### A.4 输入事件来源识别(X11)

| 来源 | 关键事实 | 对本文的影响 |
|---|---|---|
| [who-t: X 的输入事件处理](http://who-t.blogspot.com/2010/07/input-event-processing-in-x.html) | server 为每个 master 硬编码一对 XTEST 从设备("Virtual core XTEST keyboard/pointer");XTEST 事件恒由它们产生 | §2-A 的根治方案前提 |
| [Xext/xtest.c](https://github.com/XQuartz/xorg-server/blob/master/Xext/xtest.c) | server 给 XTEST 设备打 `XI_PROP_XTEST_DEVICE` 属性,并提供 `IsXTestDevice()` | 身份判定改用属性而非设备名匹配 |
| [libXi 修复 #34240](https://lists.x.org/archives/xorg-devel/2011-July/024095.html) / [协议补丁](https://lists.x.org/pipermail/xorg-devel/2011-August/024396.html) | `XIRawEvent.sourceid` 在 **XI 2.0 恒为 0**(历史 bug),**XI 2.1 起才有效** | §2-A/§3 必须 `XIQueryVersion` 协商 ≥2.1 并做回退 |

### A.5 evdev/uinput 守护进程工程实践

| 来源 | 关键事实 | 对本文的影响 |
|---|---|---|
| [keyd README](https://github.com/rvaiya/keyd) / [keyd.scdoc](https://github.com/rvaiya/keyd/blob/master/docs/keyd.scdoc) | panic 序列 `backspace+escape+enter`;客户端-服务器模型支持运行时改键映射;跨 X/Wayland/TTY 一致 | §1.2-B 的 panic 键与 IPC 设计 |
| [keyd evloop.c](https://github.com/rvaiya/keyd/blob/master/src/evloop.c) | `panic_check` 在事件进状态机**之前**、且只对非虚拟设备判定;inotify 监控热插拔;poll 单线程多 fd;`dev->is_virtual` 防自环 | §1.2-B 的事件循环与逃生键时序照此设计 |
| [keyd#183(见 keyd 文档注记)](https://github.com/rvaiya/keyd/issues/183) | 创建虚拟设备会让 `setxkbmap`/`xset` 设置丢失;libinput 的 disable-while-typing 需 quirk 把虚拟设备标为 internal | §1.2-B 风险清单 3 |
| [ydotool 非 root 配置 PR](https://github.com/ReimuNotMoe/ydotool/pull/315) / [udev 规则问题 #210](https://github.com/ReimuNotMoe/ydotool/issues/210) | udev 规则须带 `OPTIONS+="static_node=uinput"`;**规则装好后要重载 uinput 模块才生效**;socket 应放 `$XDG_RUNTIME_DIR`;历史上还踩过 systemd 253 的回归 | §1.2-B 风险清单 1(需核对本仓库现有 `60-ahk-uinput.rules`) |

### A.6 文本输入(Unicode)

| 来源 | 关键事实 | 对本文的影响 |
|---|---|---|
| [wtype main.c](https://github.com/atx/wtype/blob/d71be3a7/main.c) | 维护 `keycode→(keysym,wchar)` 表,按需追加并生成完整 xkb_keymap 文本上传给虚拟键盘;`xkb_types/compatibility` 直接 include "complete" | §6-U3 的实现范式 |
| [wtype(1) man](https://man.archlinux.org/man/extra/wtype/wtype.1.en) | 进程退出即释放所有按下的键/修饰键(虚拟键盘对象销毁) | 常驻进程需自行保证异常时释放 |
| [xkbcommon keymap 文本格式](https://xkbcommon.org/doc/current/keymap-text-format-v1-v2.html) | Unicode keysym = `0x01000000 + 码点`(0x100..0x10FFFF) | §6-U3 与 X11 侧共用转换 |

### A.7 托盘 / 无障碍 / CI / 打包

| 来源 | 关键事实 | 对本文的影响 |
|---|---|---|
| [swaybar tray.c](https://github.com/swaywm/sway/blob/03483ff3707a358d935e451d39748e58c205ce8a/swaybar/tray/tray.c) | swaybar 同时注册 freedesktop 与 kde 两个 StatusNotifierWatcher | §5.2:两个 well-known 名字都要尝试 |
| [GNOME AppIndicator 扩展](https://github.com/ubuntu/gnome-shell-extension-appindicator) | GNOME Shell 本身无托盘,需该扩展 | §5.2:GNOME 下托盘失败是环境缺宿主 |
| [Waybar#4467](https://github.com/Alexays/Waybar/issues/4467) | libayatana-appindicator 重写为 Gio menus,**偏离 SNI 的 dbusmenu 规范**,破坏多数实现 | §5.2:自实现 dbusmenu,不链该库 |
| [xa11y 无障碍 quirks](https://xa11y.dev/explanation/accessibility-quirks/) / [ArchWiki](https://wiki.archlinux.org/title/Accessibility) / [Chromium 无障碍文档](https://www.chromium.org/developers/design-documents/accessibility/) | `org.a11y.Status.IsEnabled` 是总闸;Chromium/Electron 需 `--force-renderer-accessibility` 或 `ACCESSIBILITY_ENABLED=1`;Firefox 需 `MOZ_ACCESSIBILITY_ATK2=1`;Qt/Java 各有开关 | §7-C3 的速查表与"不要静默返回空串" |
| [at-spi2-core bus/README](https://github.com/GNOME/at-spi2-core/blob/main/bus/README.md) | `org.gnome.desktop.interface toolkit-accessibility` 触发 a11y 总线自启 | §7-C3 的会话级检测 |
| [microsoft/vscode#84833](https://github.com/microsoft/vscode/issues/84833) | VS Code 曾长期不接受该 Electron 参数 | §7-C4 矩阵中 VS Code 需标注版本 |
| [GitHub KVM 公告](https://github.blog/changelog/2024-04-02-github-actions-hardware-accelerated-android-virtualization-now-available/) | x86_64 Linux runner(2vCPU 起)可用 KVM,需加 kvm 组 udev 规则 | §16 步骤 4 的可行性与配置片段 |
| [actions/runner-images#14062](https://github.com/actions/runner-images/issues/14062) | **ARM runner 无 KVM**,Azure ARM 实例暂无嵌套虚拟化,GitHub 官方确认 | §16 步骤 5 降级预期 |
| [Ahk2Exe Compiler.ahk](https://github.com/AutoHotkey/Ahk2Exe/blob/master/Compiler.ahk) / [社区解释](https://www.autohotkey.com/boards/viewtopic.php?t=49527) | 编译=复制 `AutoHotkeySC.bin` + 把合并后的脚本写入 RCDATA(`>AUTOHOTKEY SCRIPT<`,基于 exe 时 ID `#1`),**无字节码** | §5.3 的 `--pack` 设计与文档口径 |
| [AHK 论坛:Wine 下移植实践](https://www.autohotkey.com/boards/viewtopic.php?style=17&f=22&t=65552) | Wine 下大量功能可用但 GUI/主题/alpha/菜单/IniWrite 等存在成片偏差 | §9-T2:Wine 仅作语言层冒烟基准 |

### A.8 检索未能证实、需实机验证的假设

以下三条是本轮检索**指向但未直接证实**的关键假设,必须在 VM 实测后才能写进
用户文档(在此显式登记,避免日后被当成已验证事实):

1. **XWayland 23.2+ 的 XTEST→libei 桥接,在 GNOME 48/49 上对本移植版是否
   开箱生效**(涉及 portal 授权弹窗时机、是否需要 app-id、被拒后的降级行为);
   **已实测(R1-7,GNOME 49 VM / Ubuntu 24.04 / Xwayland 24.1.6):不成立** ——
   该发行版的 Xwayland 包未编入 libei(ldd 与 strings 均无 libei/EIS 符号),
   实测 XTEST 只能到达 XWayland 窗口(正向对照:xwin_helper X 窗口收到全部
   SendText 键事件),无法到达聚焦的原生 Wayland 窗口(ptyxis 读取循环无输入),
   且全程无 RemoteDesktop/ConnectToEIS portal 流量。零代码路径需要发行版
   Xwayland 带 libei(如 Fedora 或 `--enable-libei` 自编译),已在
   linux-port.htm 记为"未验证/不可用的发行版依赖项";原生 libei 后端仍归 R3。
   验证脚本:`tests/doccheck/gnome_xtest_libei.sh`。
   **附带观察(非本项验收,已作 R2 跟进项登记)**:R1-7 测试中发现,经 SSH 起动、
   以 XWayland 连接的进程创建 GTK3 Gui 会挂起(GDK_BACKEND 默认与强制 x11 均复现;
   xwininfo 显示 mutter 已为窗口建 frame,进程阻塞在 X socket 的 poll 上,且
   SIGTERM 处理器在 GTK init 卡住时不生效,需 SIGKILL)。此现象是否需要"从
   GNOME 会话内正常终端运行"复现、是否波及真实用户,尚未实机确认 —— 需 R2 跟进
   (区分 SSH 环境伪象与真实 GUI-under-XWayland 缺陷),不得直接写入用户文档。
2. **GNOME 48+ 的 GlobalShortcuts portal 后端与本项目 portal lane 的实际兼容性**
   (`BindShortcuts` 一次性限制与 AHK 运行时 `Hotkey()` 动态注册的冲突程度);
   **已实测(R1-6,GNOME 49 VM):后端存在且可探测** —— `--diag` 的
   `portal-global-shortcuts : yes (version 1)` 与 `gnome-major : 49`、
   `portal-app-id-resolvable : yes` 均为功能探针实测;扩展卸载后 auto 策略正确
   落到 portal 且不误报错误(GNOME≥48 后端在总线即放行);GNOME<48/无后端场景
   以死总线仿真,得到带安装指引的明确报错。`BindShortcuts` 一次性限制对
   `Hotkey()` 动态注册的影响程度仍需 R2 的逐热键路由实测确认(本项目 portal lane
   当前为"首次全量注册,变更重绑",未做逐键增删)。
3. **ext-data-control-v1 在 KWin 6.6 上的剪贴板监听行为**是否与 sway 一致
   (尤其 primary selection 与大文本分块读取)。
