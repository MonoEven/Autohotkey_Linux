## 结论

这个仓库**不是概念验证或空壳项目**。它已经把 AutoHotkey v2 的解释器、X11 自动化、部分 Wayland 输入、GTK3 GUI、D-Bus、AT-SPI、安装打包和 CI 做到了相当深的程度。仓库当前明确将自身定位为 **technology preview**，最新发布版是 `v2.0.26-linux.15`，发布于 **2026 年 8 月 20 日**。([GitHub](https://github.com/MonoEven/Autohotkey_Linux))

但它与“真正的 Linux 版 AutoHotkey”之间还有明显差距。核心问题不是“函数有没有同名入口”，而是：

> **很多函数虽然能够调用，但底层语义、作用范围、权限模型和 Windows AutoHotkey 并不等价。**

如果需求只是“在 X11 下写一些新的 AHK v2 热键、热字串、窗口和键鼠脚本”，它已经比较接近可用；如果需求是“让现有 Windows AHK 脚本不修改直接迁移到任何 Linux 桌面”，差距仍然很大。

本次评估主要基于仓库源码页面、CI、发布文件、能力矩阵以及仓库自己的弱化实现审计。由于当前执行环境无法直接拉取仓库，我没有独立完成本地编译和运行复现，因此仓库所称的测试通过数字仍应视为项目方结果，而不是第三方复验结果。

---

## 差距大概有多少

以下是工程判断，不是仓库官方百分比。

| 实际需求                                         | 当前完成度估算 | 主要差距 |
| ------------------------------------------------ | -------------: | -------: |
| 纯 AHK v2 语言、文件、字符串、对象、定时器等脚本 |        85%–95% |   5%–15% |
| X11/XWayland 下新写的热键、键鼠、窗口自动化      |        75%–85% |  15%–25% |
| Wayland 下普通热键和文本自动化                   |        50%–65% |  35%–50% |
| 跨 GNOME、KDE、Sway、不同发行版的统一桌面自动化  |        45%–60% |  40%–55% |
| Windows AHK v2 脚本“基本不改直接运行”            |        30%–45% |  55%–70% |
| Windows AHK v1 脚本兼容                          |        接近 0% | 90% 以上 |
| 企业或生产环境长期部署                           |        40%–55% |  45%–60% |

仓库只支持 AHK v2，不支持 v1；而且它自己也承认 Wayland 全局热键、跨应用控件自动化和 Unicode 输入仍然“年轻”。([GitHub](https://github.com/MonoEven/Autohotkey_Linux))

---

# 已经做得比较好的部分

## 1. 不是重写一个小语言，而是基于官方 AHK v2 源码移植

仓库是从官方 AutoHotkey v2.0.26 分叉出来的，不是像一些 Linux AHK 项目那样重新实现一小部分语法。因此，表达式、对象、函数、异常、文件、正则、日期时间等语言行为，理论上更容易保持兼容。目前官方 AutoHotkey 最新稳定版也仍然是 v2.0.26，所以暂时没有明显的上游版本落后问题。([GitHub](https://github.com/MonoEven/Autohotkey_Linux))

## 2. X11 后端已经有实际内容

它声称并提供了以下能力：

- `Win*` 窗口操作；
- `Control*` 控件操作；
- XGrabKey/XGrabButton 全局热键；
- 热字串和 InputHook；
- XTEST 键鼠模拟；
- 屏幕、像素、ImageSearch；
- GTK3 的 Gui 和 Menu；
- 剪贴板、对话框、ToolTip、窗口形状。

这些功能并不只是 README 列表，测试目录里确实有相应模块和独立测试脚本。([GitHub](https://github.com/MonoEven/Autohotkey_Linux))

## 3. 测试和发布工程比一般个人移植项目完善

CI 至少包含：

- 普通构建；
- ASan 构建；
- headless 回归；
- Xvfb 文档检查；
- Sway 下纯 Wayland 和 XWayland 测试；
- deb、RPM、AppImage、tarball 打包；
- SHA-256 校验；
- OpenPGP 签名验证；
- 安装、运行和卸载验证。

这一点值得肯定。很多类似项目只有“能编译”，这个项目已经尝试覆盖构建、运行、打包和更新链路。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/.github/workflows/ci.yml))

## 4. 项目作者至少意识到了“同名不等于兼容”

仓库包含专门的 `AUDIT_2026_WEAKENED.md`，主动记录哪些功能只是近似、模拟或弱化实现。这比只展示绿色测试数字要诚实，也是本次审查中最有价值的文件。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/tests/doccheck/AUDIT_2026_WEAKENED.md))

---

# 主要问题

## P0：Wayland 并没有真正统一解决

这是最根本的差距。

Linux 桌面现在不能只看 X11，但 Wayland 的安全模型本身就不允许任意程序像 Windows AHK 那样全局监听和注入输入。因此仓库目前采用的是多条不一致的通路：

- GNOME Shell 扩展；
- XDG Global Shortcuts Portal；
- wlroots 虚拟键盘协议；
- `/dev/uinput`；
- XWayland 回退；
- 剪贴板粘贴回退。

这些方案在 GNOME、KDE、Sway 和其他 compositor 上的行为并不等价。

仓库文档明确表示，GNOME Shell 后端目前主要支持独占热键；`~` 透传、完整 remap、`a & b` 组合键、完整通配符以及 key-up 消费都没有完整实现。Portal 在某些环境下还要求有效的应用身份，裸命令行进程可能被拒绝。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/docs-v2/docs/linux-port.htm))

这意味着以下常见 AHK 用法在 Wayland 上不能假设成立：

```ahk
CapsLock::Esc
~F1::SomeFunction()
a & b::SomeFunction()
*LWin::SomeFunction()
F1 up::SomeFunction()
```

即使某个版本在 GNOME 49 上测试成功，也不能自动推出在 GNOME 46、KDE Plasma、Hyprland、COSMIC 或不同 Portal 实现上行为一致。

### 实际影响

如果用户只是需要几个 `Ctrl+Alt+X` 式快捷键，问题可能不大。

如果用户需要：

- 键位重映射；
- 修饰键状态机；
- 游戏或低延迟输入；
- 按键透传；
- 按住和释放逻辑；
- 多键组合；
- 全局热字串；

Wayland 下的差距仍然非常明显。

---

## P0：Send 系列函数只是“能发送”，并非 AutoHotkey 等价实现

仓库自己的审计指出：

- `Send`
- `SendEvent`
- `SendInput`
- `SendPlay`

实际上都进入同一条 XTEST 发送路径。

因此 Windows AHK 中不同发送模式的时序、缓冲、脚本忙碌时行为、事件来源和兼容性差异并没有复刻。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/tests/doccheck/AUDIT_2026_WEAKENED.md))

这对简单脚本影响不大：

```ahk
SendText "hello"
```

但对依赖时序和输入层级的脚本影响很大，例如：

```ahk
SendMode "Input"
SetKeyDelay -1
SendLevel 2
```

这些设置可能被接受，甚至测试通过，但不代表具有 Windows 上相同的可观察行为。

---

## P0：低级键盘、鼠标 Hook 语义不完整

审计指出 `InstallKeybdHook` 和 `InstallMouseHook` 目前主要只是记录状态，并没有安装与 Windows 类似的低级 Hook。`KeyWait` 也主要依赖轮询，因此物理状态、逻辑状态和防抖细节只是近似。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/tests/doccheck/AUDIT_2026_WEAKENED.md))

这会影响：

- `A_PriorKey`、物理按键历史等依赖；
- 复杂 remap；
- 按键抑制；
- SendLevel/InputLevel；
- 自己发送的事件与真实键盘事件区分；
- 高速输入和重复键；
- 多设备键盘、鼠标识别。

仓库已经实现了不少捕获状态机，但距离 AHK 的完整 Hook 模型仍有较大差距。

---

## P0：OnClipboardChange 注册了，但曾经完全不会触发

仓库自己的弱化审计明确记录，`OnClipboardChange` 的注册和注销虽然可调用，但底层剪贴板监听曾是 no-op，因此回调不会运行。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/tests/doccheck/AUDIT_2026_WEAKENED.md))

这是典型的“API 存在，但主要用途缺失”。

它也暴露了测试口径的问题：如果测试只检查“注册没有报错”，就可能把一个功能标成实现；但用户实际需要的是“剪贴板变化时确实收到事件”。

在正式采用前，需要重新确认最新 linux.15 是否已真正修复这一点。当前公开审计内容仍将其列为弱化项。

---

## P0：现有 Windows AHK 脚本并不能直接迁移

即使只考虑 AHK v2，很多 Windows 脚本依赖的其实不是语言，而是 Windows 平台能力：

- COM；
- Excel、Word、Outlook 自动化；
- Windows Registry；
- Win32 消息；
- `SendMessage`、`PostMessage`、`OnMessage`；
- DLL 和 Windows ABI；
- 托盘图标；
- Windows 控件模型；
- 编译为独立 EXE。

这些功能在这个仓库里要么不存在，要么被替换成完全不同的 Linux 概念。

例如：

- COM 被替换为 D-Bus，不是 IDispatch/IUnknown；
- 注册表被模拟为用户目录下的文本存储；
- Windows DLL 不能加载，只能调用 `.so`；
- 没有 Win32 消息；
- `TrayTip`、`TraySetIcon` 不可用；
- 编译脚本打包不可用。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/docs-v2/docs/linux-port.htm))

所以以下 Windows 脚本不会因为“ComObject 这个名字存在”就能迁移：

```ahk
excel := ComObject("Excel.Application")
excel.Visible := true
```

Linux 版的 `ComObject("service")` 实际是在访问 D-Bus 服务，这是一套完全不同的对象模型。

---

## P1：Wayland Unicode 输入存在安全和用户体验问题

纯 Wayland 下，非 ASCII 文本可能采用：

1. 临时替换系统剪贴板；
2. 发送 `Ctrl+V`；
3. 等待目标读取；
4. 恢复原剪贴板。

仓库文档也警告，这会涉及敏感文本和密码管理器。某些环境还需要 `/dev/uinput` 权限或 root/input group。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/docs-v2/docs/linux-port.htm))

潜在问题包括：

- 覆盖用户剪贴板；
- 与剪贴板管理器竞争；
- 目标应用未及时消费；
- 密码管理器检测到剪贴板变化；
- 沙箱应用拒绝粘贴；
- `Ctrl+V` 在终端、编辑器或自定义键位中行为不同；
- 键盘布局不是标准 Ctrl+V；
- 中间过程被其他应用读取。

作为 fallback 可以接受，但不能称为与 Windows `SendText` 等价。

---

## P1：跨应用控件自动化深度不足

Wayland 下主要依赖 AT-SPI，并且当前只展示了最小路径：

- 获取文本；
- 设置文本；
- 点击动作。

这依赖目标应用正确暴露可访问性树。GTK、Qt 和 Electron 的具体表现也会随应用、版本和启动参数不同。仓库的验证主要是 GTK3 示例，不足以证明 LibreOffice、浏览器、IDE、Electron、Java、游戏等应用的普遍兼容性。([GitHub](https://github.com/MonoEven/Autohotkey_Linux))

X11 下控件自动化也有弱化：

- enabled、checked、style 等部分状态来自本地 shadow 数据；
- 如果外部应用自己改变状态，可能读不到真实值；
- `MenuSelect` 实际不可用；
- `CaretGetPos` 主要对 GTK 文本控件有效。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/tests/doccheck/AUDIT_2026_WEAKENED.md))

这说明它更接近：

> “能够操作部分 X11/GTK 应用”

而不是：

> “像 Windows AHK 一样普遍操作桌面应用控件”。

---

## P1：GTK3 GUI 是子集，不是完整 AHK GUI

仓库已经实现相当多 GTK3 控件，这一点是真的。但审计也明确表示：

- 部分字体、颜色、边距、Resize、Min/Max 选项可能被接受但忽略；
- 部分事件变体未完整覆盖；
- ActiveX 等 Windows 控件没有对应；
- 菜单和句柄语义与 Win32 不同。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/tests/doccheck/AUDIT_2026_WEAKENED.md))

因此已有的复杂 Windows AHK GUI 脚本通常需要重新调整布局、事件和选项。

另外，项目使用 GTK3，而不是 GTK4。GTK3 当前仍可用，但从长期维护角度看，未来还会增加一轮 GUI 技术栈迁移成本。

---

## P1：“1099/1099”容易造成错误预期

1099 个断言通过是积极信号，但不能解释为“99.7% AutoHotkey 兼容”。

原因有三个。

### 1. 断言粒度不是用户场景粒度

一个函数可能有十几个重要语义，但测试只调用一次。例如：

- `InstallKeybdHook()` 不报错；
- `OnClipboardChange()` 注册成功；
- `SendInput()` 能输出字符；

都可能产生绿色断言，但主要语义并未完整实现。

仓库审计自己就列出了“调用存在但弱化”的项目。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/tests/doccheck/AUDIT_2026_WEAKENED.md))

### 2. 测试主要由同一个项目维护者编写

没有独立兼容测试套件，也没有看到将同一批脚本同时在 Windows 官方 AHK 与 Linux 移植版运行后逐项比较的完整差分框架。

当前报告主要是“文档条目 → 自建 `.ahk` 脚本 → 自建期望输出”。这个方式有价值，但容易只验证已知实现路径。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/tests/doccheck/CHECK_REPORT.md))

### 3. CI 环境覆盖有限

CI 的主要系统是单一的 `ubuntu-24.04` runner，图形环境通过 Xvfb、Sway、XWayland、Weston 等虚拟环境构造。没有形成 Fedora、Arch、openSUSE、不同 glibc、不同 GTK、不同 GNOME/KDE 版本的矩阵。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/.github/workflows/ci.yml))

所以它证明的是：

> 在项目定义的 Ubuntu CI 环境和项目测试用例中可以通过。

而不是：

> 在常见 Linux 桌面环境中全面兼容。

---

## P1：文档和状态口径有漂移

仓库主 README 当前写的是 1099/1099，但其他文件中仍能看到不同数字或过期描述，例如：

- `MODULE_MATRIX.md` 标题附近仍写 1069/1069；
- 顶层 CMake 注释还说 GUI/hotkey backend “still in progress”；
- 部分迁移计划仍以早期阶段列表展示。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/MODULE_MATRIX.md))

虽然部分文档后来已经更新，但这些不一致说明：

- 状态更新依赖人工；
- “完成”定义不断变化；
- 旧审计、矩阵和 README 之间不容易判断哪个是权威来源。

对用户来说，会导致“README 说支持、审计说弱化、模块矩阵又是旧数字”的认知混乱。

建议只保留一个机器生成的能力清单，其余文档从该数据源生成。

---

## P1：代码结构存在较重的兼容层技术债

`source/linux/stdafx_linux.h` 有 3366 行、约 81 KB，它承担了大量 Win32 类型、宏和兼容行为。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/source/linux/stdafx_linux.h))

这种方案的好处是能快速编译大量原始 AHK 代码；坏处是：

- 容易产生“编译通过但语义为空”的 shim；
- Windows API 名字仍遍布 Linux 逻辑；
- 平台边界不清晰；
- 很难判断调用最终进入真实 Linux 实现还是兼容桩；
- 上游合并冲突会不断增加；
- 新维护者理解成本很高。

仓库设计文档原本希望建立明确的平台抽象接口，但当前形态仍然有较强的 Win32 模拟层痕迹。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/source/linux/README.md))

这也是为什么会出现 `AddClipboardFormatListener` 形式存在，但 Linux 实际无事件的情况。

---

## P1：社区验证几乎为空

仓库目前显示：

- 1 个 star；
- 0 个 fork；
- 0 个公开 issue；
- 0 个 pull request。

这并不表示质量差，但表示几乎没有外部使用反馈和多维护者验证。([GitHub](https://github.com/MonoEven/Autohotkey_Linux))

对于一个涉及：

- 全局输入；
- GNOME Shell 扩展；
- uinput 权限；
- D-Bus；
- X11；
- Wayland；
- 安装和自动更新；

的项目，单维护者和低外部验证是非常大的长期风险。

特别是“0 issue”不能解释成“没有问题”。结合仓库刚刚发布、用户量很小，更合理的解释是尚未形成真实用户反馈池。

---

## P2：功能名称可能产生误导

有些 Linux 替代实现保留了 Windows 函数名：

- Registry 函数实际是文本文件存储；
- COM 实际是 D-Bus；
- `SendInput` 实际不是 Windows SendInput；
- Style 状态部分可能是 shadow；
- Sound 接口依赖 pactl/amixer；
- Callback ABI 有简化。

这种做法有利于语法兼容，但容易让用户误认为语义兼容。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/tests/doccheck/AUDIT_2026_WEAKENED.md))

建议明确区分：

1. **兼容实现**：与 Windows 行为高度一致；
2. **平台适配实现**：实现相同目标，但行为不同；
3. **模拟实现**：只提供本项目内部状态；
4. **占位或错误实现**：明确不支持。

当前的 ✅、⚠️、❌ 还不够细。

---

# 最需要补的内容

## 第一优先级：建立真实需求测试，而不是继续增加函数计数

建议创建 30–50 个完整用户场景，例如：

1. CapsLock/Esc 双角色键；
2. `a & b` 组合热键；
3. key-down、长按、key-up；
4. 普通热字串、中英文热字串；
5. 多脚本并发；
6. VS Code、Firefox、Chromium、LibreOffice、终端自动化；
7. GNOME、KDE、Sway；
8. 多显示器、不同缩放比例；
9. 多键盘布局和输入法；
10. 剪贴板管理器并存；
11. 锁屏、休眠、重新登录；
12. GNOME Shell 扩展重载；
13. 应用沙箱和 Flatpak；
14. 脚本崩溃后按键是否释放；
15. 高频输入和 CPU 占用。

每个场景都应该给出：

- X11 结果；
- GNOME Wayland 结果；
- KDE Wayland 结果；
- wlroots 结果；
- 已知限制；
- 是否需要 root、扩展或 Portal 确认。

---

## 第二优先级：把 Wayland 支持拆成明确等级

不应该只写“Wayland supported”。

建议定义：

### Wayland Level 1：普通快捷键

- 单键或修饰组合；
- 通过 Portal 或 GNOME 扩展；
- 不保证透传和 remap。

### Wayland Level 2：文本输入

- ASCII；
- Unicode；
- 剪贴板 fallback；
- 明确说明安全风险。

### Wayland Level 3：完整输入代理

- evdev 捕获；
- uinput 注入；
- remap；
- key-up；
- 多键组合；
- 需要系统服务和权限。

目前 Level 1 部分可用，Level 2 条件可用，Level 3 仍不完整。

---

## 第三优先级：解决 API“假兼容”

应该优先处理这些高价值差异：

- `OnClipboardChange` 真正事件监听；
- `SendInput`、`SendEvent`、`SendPlay` 不再全部归一；
- 扫描码热键；
- `A & B` 前缀组合；
- key-up 热键；
- SendLevel/InputLevel；
- 外部控件状态真实读取；
- 更完整的 AT-SPI 控件查找；
- MenuSelect 或明确删除误导；
- 编译脚本或可部署 bundle；
- 系统托盘和通知。

---

## 第四优先级：扩大 CI

最低限度建议加入：

- Ubuntu LTS；
- Fedora；
- Arch；
- GCC 和 Clang；
- GNOME Wayland；
- KDE Wayland；
- Sway/wlroots；
- GTK3 不同版本；
- 无 XWayland 场景；
- Flatpak 或受限环境。

当前 CI 工程本身不错，但平台矩阵远不足以证明“Linux 桌面兼容”。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/.github/workflows/ci.yml))

---

# 是否值得继续用

## 适合使用的情况

可以考虑用于：

- 你愿意专门为 Linux 编写 AHK v2 脚本；
- 目标环境固定为 X11/XWayland；
- 主要是热键、热字串、键鼠、文件和窗口操作；
- 可以接受逐脚本测试和改写；
- 不依赖 Windows COM、注册表、消息和 DLL；
- 项目内部试用或个人自动化。

## 不建议直接依赖的情况

暂时不建议直接用于：

- 将大量现有 Windows AHK 脚本无修改迁移；
- 通用 Wayland 键位重映射；
- 企业桌面大规模部署；
- 强依赖 Office COM；
- 对输入时序和抑制要求很高；
- 需要兼容所有 GNOME/KDE/发行版组合；
- 安全敏感的密码、凭据或剪贴板自动化；
- 需要长期无人维护运行的关键业务。

---

# 最终判断

我会把这个仓库定义为：

> **一个进展非常快、实现深度明显超过普通原型的 AutoHotkey v2 Linux 技术预览版，但还不是 Windows AutoHotkey 的 Linux 等价品。**

它目前最可信的定位是：

> **“X11 上较强、Wayland 上按桌面环境分层可用的 AHK v2 风格自动化运行时。”**

而不是：

> **“现有 AutoHotkey 脚本的跨平台直接替代。”**

对于固定 X11 环境，它距离实际使用可能只差 **15%–25%**；对于通用 Wayland 自动化，大约还差 **35%–50%**；对于 Windows AHK 脚本无修改迁移，差距更接近 **55%–70%**。最大的风险依次是 Wayland 输入模型、弱化语义、跨应用自动化、测试外部有效性和单维护者可持续性。