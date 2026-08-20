## 结论

这个仓库目前更准确的定位是：

> **AutoHotkey v2 解释器的 Linux 移植 + 较完整的 X11/XWayland 自动化后端 + 仍处于实验阶段的原生 Wayland 适配。**

它还不能被视为“Linux 上可以直接替代 Windows AutoHotkey 的成熟实现”。仓库自己也将最新的 `v2.0.26-linux.13` 标为 **technology preview**，并明确承认原生 Wayland 全局热键、Unicode 输入以及跨应用控制仍不成熟，且没有大规模外部用户验证。最新版本发布于 2026 年 8 月 19 日。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/tree/v2.0.26-linux.13))

按照一般用户对“Linux 版 AutoHotkey”的实际预期，我给出的综合判断是：

| 使用目标 | 当前满足程度 |
|---|---:|
| AHK v2 语言、文件、字符串、进程等非桌面逻辑 | **80%–90%** |
| X11/Xorg 下热键、发送输入、窗口自动化 | **65%–80%** |
| XWayland 环境中的传统桌面自动化 | **60%–75%** |
| 原生 Wayland 下简单全局快捷键 | **30%–50%** |
| 原生 Wayland 下完整 AHK 热键、热字串和输入模拟 | **10%–30%** |
| 现有 Windows AHK v2 脚本直接迁移 | **25%–60%**，取决于是否使用 Windows API |
| AHK v1 脚本迁移 | **接近 0%** |
| 跨发行版、跨桌面生产部署 | **30%–40%** |

因此，把它作为广义的“Linux AutoHotkey 替代品”，当前大约完成了 **45%–60%**。若固定使用 Xorg/XWayland，差距可能只有 20%–35%；若要求原生 GNOME/KDE Wayland、中文输入、热字串、窗口控件自动化，差距会达到 60%–80%。

---

# 实际需求与仓库能力的差距

## 1. AHK 语言核心：完成度较高，但“断言通过”不等于完整兼容

仓库宣称覆盖 367 个内建功能，并有 1063 个文档断言、27 个 headless 回归测试、15 个 Wayland 测试和 247 个 XWayland 测试通过。测试涵盖字符串、对象、文件、GUI、热键、热字串、InputHook、窗口和控件等模块，这部分工程量确实不小。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/tests/doccheck/CHECK_REPORT.md))

但这组数字需要正确理解：

- 它是项目自己根据文档编写的断言，不是官方 AutoHotkey 兼容测试套件。
- 很多断言只验证有限样例或错误路径，而不是复杂组合、并发、长时间运行和真实应用兼容性。
- CI 只有 Ubuntu 24.04，桌面测试主要运行在 Xvfb、headless sway 和 XWayland 中，并没有覆盖真实 GNOME、KDE Plasma、Fedora、Arch、Debian、不同输入法和不同 GPU 驱动。仓库自己的路线图也承认这些尚待测试。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/.github/workflows/ci.yml))

所以“1063/1063”更适合作为**内部回归覆盖率**，不能解释成“AutoHotkey 兼容度 100%”。

---

## 2. X11/XWayland：这是当前真正有实用价值的部分

在 X11/XWayland 下，仓库实现了：

- `XGrabKey`/`XGrabButton` 全局热键；
- `XTEST` 键鼠注入；
- 热字串和 InputHook 捕获；
- `Win*`、`Control*` 窗口和控件操作；
- GTK3 的 `Gui`、`GuiControl` 和菜单；
- Unicode keysym 输入；
- 截图、像素、图像搜索和窗口形状等功能。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/tests/doccheck/CHECK_REPORT.md))

固定在 Xorg，或允许程序通过 XWayland 操作 X11 应用时，它已经具备试用价值，适合：

- 自己控制环境的小工具；
- 简单热键启动程序；
- 文本扩展；
- 文件和进程自动化；
- 针对 X11 应用的窗口定位、点击和输入；
- 对失败容忍度较高的个人脚本。

不过，即使在 X11 下，它也还不是 Windows AutoHotkey 的无缝替代，后面提到的事件抑制、Unicode 映射、窗口语义和平台 API 差异仍然存在。

---

## 3. 原生 Wayland：与用户核心需求差距最大

这是这个项目最大的结构性问题。

### GNOME 后端仅支持 GNOME Shell 49

附带的 GNOME Shell 扩展在 `metadata.json` 中只声明：

```json
"shell-version": ["49"]
```

也就是说，默认情况下它不能安装到 GNOME 45、46、47、48、50 等版本。GNOME 官方文档明确说明，`shell-version` 决定扩展支持的 Shell 版本，现代 GNOME 默认会拒绝加载未声明兼容的扩展。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/v2.0.26-linux.13/extension/ahk-global-hotkeys%40autohotkey.org/metadata.json))

这使得仓库所说的“GNOME 原生 Wayland 后端”实际等价于：

> **Ubuntu 25.10 + GNOME 49 上经过验证的专用后端。**

仓库文档自己也只列出了 Ubuntu 25.10、GNOME 49 的端到端验证。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/v2.0.26-linux.13/docs-v2/docs/linux-port.htm))

对于需要支持 Ubuntu LTS、Fedora 不同版本或企业环境的项目，这个范围明显不够。

### GNOME 后端只支持“独占按下热键”

当前 GNOME 扩展只支持类似：

```ahk
1::
^1::
F12::
```

而以下常见 AHK 语义不支持：

- `~` 透传；
- `*` 通配；
- `a & b` 自定义组合；
- remap 重映射；
- 左右修饰键完整语义；
- `key up` 热键。

C++ 端明确忽略 `Deactivated`，扩展代码也明确说明这些功能不在当前范围。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/source/linux/core/input_backend_gnome_shell.cpp))

因此，它目前只能解决“按下某组合，运行一个动作”这一类简单快捷键，不能提供 AutoHotkey 用户习惯的底层键盘钩子语义。

### GNOME 原生 Wayland 下不能发送键鼠

仓库兼容矩阵明确写明，GNOME 没有其所需的虚拟键盘协议，因此：

- `Send`、`SendText` 的输入注入不可用；
- 鼠标模拟不可用；
- 中文、日文、Emoji 等 Unicode 发送会直接报错；
- 只能退回 XWayland/Xorg。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/v2.0.26-linux.13/docs-v2/docs/linux-port.htm))

换句话说，在当前主流的 GNOME Wayland 使用场景中，它只能监听一部分热键，却无法完成“热键触发后向当前应用输入内容”这一最常见的 AHK 工作流。

### 所有原生 Wayland 后端均不支持热字串和 InputHook

仓库文档直接说明，Hotstrings 和 InputHook 依赖 X11 grab 捕获机制，在 GNOME、wlroots 和其他原生 Wayland 后端下均不可用，只能使用 XWayland/Xorg。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/v2.0.26-linux.13/docs-v2/docs/linux-port.htm))

这意味着以下典型需求目前无法实现：

```ahk
::addr::某个完整地址
::sig::姓名 + 电话 + 邮箱
```

也无法完成：

- 监听用户输入字符；
- 根据输入上下文扩展文本；
- 捕获任意键序列；
- 实现键盘层、模态键盘或复杂 remap。

这已经不是细节兼容问题，而是核心能力缺失。

### 原生 Wayland 下没有窗口和控件自动化

`Win*`、`Control*` 等功能在 Wayland 下只能通过 XWayland 回退。真正的跨应用可访问性自动化应依赖 AT-SPI，但仓库明确标记为“尚未实现”。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/v2.0.26-linux.13/docs-v2/docs/linux-port.htm))

所以它目前不能稳定完成：

- 枚举所有原生 Wayland 窗口；
- 获取任意应用的标题、位置和控件；
- 读取 GTK、Qt 或 Electron 应用控件树；
- 设置文本框内容；
- 按控件名称点击按钮；
- 在原生 Wayland 应用之间进行可靠自动化。

---

## 4. XDG Global Shortcuts Portal 无法替代 AHK 键盘钩子

项目把 XDG Global Shortcuts Portal 作为通用 Wayland 回退方案，但 Portal 的设计目标不是提供任意低层输入捕获。

官方接口规定：

- 快捷键绑定在应用及 session 上；
- 绑定通常会出现用户配置或确认界面；
- 主要提供 Activated/Deactivated 信号；
- 它不是原始键盘事件流，也不能提供任意按键监听、透传、屏蔽、remap 或热字串。([Flatpak](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.impl.portal.GlobalShortcuts.html?utm_source=chatgpt.com))

因此，Portal 最多解决“应用级全局快捷键”需求，无法从架构上实现完整 AutoHotkey。

项目为了在 GNOME 上绕开确认界面，增加了 GNOME Shell 扩展，但又由此产生单一 GNOME 版本耦合和扩展维护问题。

---

# 代码实现中发现的具体风险

以下不是单纯“功能尚未实现”，而是当前代码中值得重点排查的问题。由于本次是静态审计，前两项应通过真实键盘和慢应用场景复现后再定性为正式 bug。

## P0/P1：X11 注入事件过滤可能误吞真实重复按键

代码为避免自己注入的键再次触发热键，维护了一个 8 项日志。判定条件只有：

- keycode 相同；
- 按下/释放阶段相同；
- 当前本地时间与注入时间相差不到 1 秒。

匹配后直接丢弃事件，而且匹配记录没有被消费或清除。更值得注意的是，前面的注释说是根据“server event time”判断，但结构体实际保存的是 `GetTickCount()`，判断也完全没有使用 `ev.xkey.time`。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/source/linux/core/core_hotkey_linux.cpp))

这可能产生以下情况：

1. 用户按下某键；
2. 程序为实现透传而注入同一键；
3. 注入副本被正确过滤；
4. 用户在一秒内再次真实按下同一键；
5. 第二次真实事件仍匹配旧记录，被错误过滤。

快速连按、长按重复、游戏输入、输入 `ll`、`oo` 之类双字符时都有可能遇到。代码注释认为真实重复“rare”，但对于打字和热字串捕获，这并不罕见。

建议改成：

- 使用 X server 事件时间或注入序列；
- 匹配一次后立即消费记录；
- 缩短窗口；
- 最好使用 XInput2 设备信息或独立注入标记；
- 增加快速重复相同键的端到端测试。

---

## P1：纯 Wayland Unicode 的剪贴板方案存在竞态和剪贴板破坏风险

纯 Wayland 下的非 ASCII 发送流程是：

1. 读取当前**文本**剪贴板；
2. 把待发送文本写入剪贴板；
3. 注入 `Ctrl+V`；
4. 固定休眠 60 毫秒；
5. 恢复原文本剪贴板。

如果原来没有可读文本，代码不会清空或恢复剪贴板，而是把刚刚发送的内容留在剪贴板中。整个流程也只保存文本，没有看到对图片、富文本或多 MIME 类型内容的保存。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/source/linux/core/core_input_linux.cpp))

存在几个实际问题：

- 应用繁忙、远程桌面、Electron 沙箱或大型文档中，60 毫秒可能不足；
- 应用可能在恢复之后才读取剪贴板，最终粘贴到旧内容；
- 原剪贴板是图片或富文本时可能被覆盖；
- 剪贴板管理器可能在这 60 毫秒内记录中间敏感文本；
- 用户同时复制内容时，会与自动恢复发生竞争；
- 密码、令牌等内容会短暂暴露给系统剪贴板。

这只能作为有限环境中的兼容降级，不应描述为可靠的 Unicode 输入实现。

更稳妥的方向是实现 text-input/IME 通道，或者至少：

- 保存并恢复完整 MIME 集；
- 等待目标应用实际请求 clipboard offer，而不是固定 sleep；
- 没有原剪贴板时恢复为空；
- 提供显式关闭剪贴板降级的选项；
- 对密码管理器和敏感输入场景警告。

---

## P1：X11 Unicode 临时键码映射存在跨进程竞争

当当前键盘布局不存在某个 Unicode 字符时，代码会：

1. 寻找一个没有绑定的备用 keycode；
2. 通过 `XChangeKeyboardMapping` 在整个 X server 上临时映射；
3. 注入按键；
4. 等待 30 毫秒；
5. 再恢复为 `NoSymbol`。

代码自己明确说明映射变化是 server-wide，而对应借用日志是 process-local。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/source/linux/core/core_input_linux.cpp))

这意味着两个 AHK 进程同时发送不同 Unicode 字符时，可能同时选择同一个备用 keycode：

- 进程 A 映射为“你”；
- 进程 B 改成“好”；
- A 的输入变成“好”；
- A 恢复为空；
- B 的后续输入丢失。

当前文件中没有看到跨进程租约、X selection 锁或其他仲裁机制。即使单进程测试全部通过，也不能证明并发多脚本安全。

另外，该映射会向其他所有 X 客户端广播 `MappingNotify`，可能引发键盘布局刷新和额外开销。代码已经为此加入 30 毫秒等待和特殊处理，说明这里本身就是竞态敏感区。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/source/linux/core/core_input_linux.cpp))

---

## P1：GNOME 扩展是强版本耦合组件

扩展直接调用 GNOME Shell 内部 API：

- `global.display.grab_accelerator()`；
- `Main.wm.allowKeybinding()`；
- Shell 内部的窗口管理白名单机制。

这些不是跨桌面稳定标准，而是 GNOME Shell 扩展 API。GNOME 官方也指出扩展需要随 Shell 版本变化进行适配。当前项目只声明 GNOME 49，升级或降级 GNOME 后都可能失效。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/v2.0.26-linux.13/extension/ahk-global-hotkeys%40autohotkey.org/extension.js))

这会带来持续维护成本：

- 每个 GNOME 大版本都需要测试和适配；
- Shell 崩溃、扩展重载时要确保热键释放；
- 发行版可能禁用或限制扩展；
- 企业桌面环境经常禁止用户安装 GNOME 扩展。

仓库对 owner 隔离、定向 D-Bus 信号和异常退出清理做了一些安全加固，这一点值得肯定，但并不能消除版本耦合。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/v2.0.26-linux.13/docs-v2/docs/linux-port.htm))

---

# 与 Windows AutoHotkey 的语义差距

即使不考虑 Wayland，现有 Windows 脚本也不能直接认为可以运行。

## 不支持 AHK v1

项目明确只支持 v2，v1 命令和迁移材料均不在范围内。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/tree/v2.0.26-linux.13))

大量历史 AHK 脚本仍使用 v1 语法，所以对这部分脚本而言，不是“部分兼容”，而是需要先完整改写到 v2。

## COM 并不是真正的 COM

项目将 `ComObject` 等名字映射到 D-Bus，但：

- 没有 `IUnknown`；
- 没有 `IDispatch`；
- 没有 Windows SafeArray；
- 没有 COM events；
- `ComObjArray` 不可用。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/tree/v2.0.26-linux.13))

这只能被视为“使用部分 AHK COM 风格 API 访问 D-Bus”，而不是兼容 Windows COM。任何依赖 Excel、Word、Outlook、Internet Explorer、WMI 或第三方 COM 组件的脚本都无法迁移。

## Win32 特性缺失

仓库明确不支持：

- Windows 注册表；
- 向其他窗口发送 Win32 message；
- `PostMessage`/`SendMessage` 的 Windows 语义；
- 加载 Windows DLL；
- 托盘通知；
- 编译脚本打包。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/tree/v2.0.26-linux.13))

`DllCall` 只能加载 Linux `.so`，脚本必须针对 Linux ABI、函数签名和依赖重新编写。

## 窗口和控件语义不可能完全相同

Windows AutoHotkey 操作 HWND、Win32 child window 和标准控件；Linux 版本在 X11 下依赖 X Window 层级，在 GTK3 GUI 中又使用 GTK 控件。

现代 GTK、Qt、Electron 应用不一定把内部按钮、输入框暴露成独立 X11 子窗口，因此即使函数名相同，`ControlClick`、`ControlSetText`、ClassNN 等能力也不会自然达到 Windows 水平。项目规划 AT-SPI 的原因正是为了弥补这一缺口，但该后端尚未实现。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/v2.0.26-linux.13/docs-v2/docs/linux-port.htm))

---

# 测试和工程管理问题

## 1. CI 覆盖面与支持范围不匹配

当前 CI 矩阵只有：

```yaml
runs-on: ubuntu-24.04
build: [core, asan]
```

图形环境主要是 Xvfb、sway headless 和 XWayland。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/.github/workflows/ci.yml))

至少缺少：

- Ubuntu GNOME 实机/虚拟机；
- KDE Plasma/KWin Wayland；
- Fedora；
- Debian；
- Arch；
- 不同 GNOME 版本；
- 多显示器及缩放；
- ibus、fcitx5；
- AMD/Intel/NVIDIA 驱动；
- ARM64；
- 长时间运行和并发多脚本；
- 安装、升级、降级和卸载端到端测试。

因此，文档中“Wayland 测试通过”实际主要意味着“在 headless sway 测试容器中通过”，不能外推到整个 Wayland 生态。

## 2. 缺少外部问题反馈闭环

仓库目前只有 1 个 star、0 fork、0 watcher，且 Issues 页面显示“issue creation is restricted”。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/tree/v2.0.26-linux.13))

这有几个后果：

- “0 个 open issue”不代表没有问题；
- 普通用户无法正常提交 bug；
- 没有公开的真实设备兼容反馈；
- 没有社区复现和交叉验证；
- 项目风险高度集中于单一维护者。

对于技术预览项目，限制 issue 创建尤其不利，因为最需要的正是多桌面、多发行版用户反馈。

## 3. 发布和回滚策略有矛盾

安装文档显示可以：

```bash
ahk --update 2.0.26-linux.8
```

进行指定版本升级或降级，但紧接着又说明旧 release 已被删除，只保留最新版本。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/tree/v2.0.26-linux.13))

这意味着实际无法依赖旧版本二进制进行回滚，也会削弱：

- 故障恢复；
- 构建可重复性；
- 版本差异排查；
- 企业固定版本部署；
- 供应链审计。

应该保留所有已发布版本，并提供哈希、签名和 changelog。

## 4. 打包范围过窄

文档目前只提供 amd64 的 `.deb` 和通用 tarball。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/tree/v2.0.26-linux.13))

尚未覆盖：

- RPM；
- Arch package；
- ARM64；
- Flatpak；
- AppImage；
- Snap；
- 发行版软件源；
- 可验证的软件签名。

这对个人试用影响有限，但与“普遍可安装的 Linux 自动化工具”还有明显距离。

## 5. 仓库内部文档存在陈旧和矛盾

`source/linux/README.md` 仍写着“X11 后端实现尚未开始”，而仓库实际上已经有大量 X11 实现。其 `source/linux/CMakeLists.txt` 也仍写着后端以后再添加、当前只有 header-only interface。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/tree/linux-port/source/linux))

仓库 About 仍写 1053/1053 测试，而当前 README 和报告是 1063/1063。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/tree/v2.0.26-linux.13))

这不影响程序直接运行，但说明：

- 开发速度快于文档整理；
- 内部结构有技术债；
- 新贡献者难以判断哪些文档可信；
- “全部完成”等描述需要更谨慎。

---

# 哪些部分做得不错

这个项目并非简单套壳，也不是只有 README 没有实现。值得肯定的部分包括：

1. **基于上游 AHK v2.0.26 源码移植**，而不是重新实现一个语法近似的解释器。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/tree/v2.0.26-linux.13))  
2. **X11 后端覆盖较广**，包括输入、窗口、控件、GUI、图像和热字串。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/tests/doccheck/CHECK_REPORT.md))  
3. **有 ASan 构建和自动回归测试**，而不是完全手工验证。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/.github/workflows/ci.yml))  
4. **文档总体上承认限制**，最新 README 没有把 technology preview 包装成正式稳定版。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/tree/v2.0.26-linux.13))  
5. GNOME D-Bus 后端考虑了 owner 隔离、定向信号、进程异常退出释放和批量注册，安全意识比早期原型要好。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/v2.0.26-linux.13/docs-v2/docs/linux-port.htm))  
6. 对未支持功能多数会明确报错，而不是静默返回假成功，这对调试很重要。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/tests/doccheck/CHECK_REPORT.md))  

所以它的基础并不差，主要问题是**仓库的实现边界与“AutoHotkey Linux”这个名称容易产生的用户预期不一致**。

---

# 建议的优先级

## 第一优先级：先定义产品边界

应明确选择其中一个方向：

### 方向 A：X11/XWayland 版 AutoHotkey

这是更接近当前代码现实的定位。重点打磨：

- X11 输入可靠性；
- 窗口和控件兼容；
- Unicode 并发；
- 安装和稳定性；
- 明确要求使用 Xorg/XWayland。

这个方向更容易在较短周期内达到稳定。

### 方向 B：真正的原生 Wayland 自动化平台

这需要补齐：

- evdev/uinput 输入 broker；
- polkit 和最小权限设计；
- 热键透传、抑制、up、组合和 remap；
- ibus/fcitx 或 text-input IME 集成；
- AT-SPI 控件自动化；
- GNOME、KDE、wlroots、COSMIC 的适配层；
- 完整安全审计。

仓库路线图已经提到 evdev/uinput 和 AT-SPI，但两者尚未启动。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/v2.0.26-linux.13/docs-v2/docs/linux-port.htm))

## 第二优先级：修复可靠性风险

建议立刻增加以下回归场景：

- 一秒内快速重复同一按键；
- `aaaa`、`hello`、双击键和长按；
- 两个 AHK 进程同时发送不同 Unicode；
- 原剪贴板为空、图片、HTML、超大文本；
- 慢应用延迟读取剪贴板；
- 脚本被 `kill -9`；
- GNOME Shell 重启和扩展升级；
- Portal 用户取消或拒绝授权；
- 键盘布局动态切换；
- ibus/fcitx 输入法激活状态。

## 第三优先级：建立真实社区验证

最重要的工程改进不是再增加几十条自有断言，而是：

- 开放 Issues；
- 发布兼容性报告模板；
- 建立桌面/发行版测试矩阵；
- 保留历史 release；
- 增加签名和校验值；
- 引入外部贡献者 review；
- 对输入监听和注入部分做独立安全审计。

---

## 最终判断

**适合现在使用的情况：**

- 固定使用 Xorg 或 XWayland；
- GNOME 49 Wayland（安装配套扩展后，`Hotkey()`/`1::`/`^1::`/`F12::`
  等排他热键零确认可用，扩展 disable→enable 自动重注册已实机验证）；
- 其他 Wayland 合成器（Portal 后端；拒绝/取消授权不崩溃、不触发、干净退出）；
- 主要写 AHK v2 新脚本；
- 以热键、文件、进程和简单窗口操作为主；
- 需要读/写入 GTK/Qt/Electron 控件文本或触发其 click（AT-SPI 最小路径，实机验证）；
- 能接受调试和自行提交补丁；
- 不把它用于关键生产流程。

**不适合当前直接采用的情况：**

- 要求 KDE Plasma 专属原生热键（Portal 兜底，未实机验证过 KDE）；
- 主要需求是中文热字串、输入法 preedit/commit 双向集成（状态检测
  `ImeGetState()` 已实现；preedit 读取、IME 切换、wlroots
  input-method-v2 文本投递仍是路线图——mutter 无 input-method 符号，
  sway 仅 v1 XML，原因已记录 docs/IME-Integration.md）；
- 需要复杂 remap、按键透传、`A & B` 和 key-up 的原生 Wayland
  捕获侧（仍需 ahk-inputd；权限制品已就绪）；
- 需要 AT-SPI 全深度自动化（任意窗口枚举/属性读写、Qt/Electron 矩阵）；
- 要迁移大量 Windows AHK 脚本；
- 依赖 COM、注册表、Win32 message 或 Windows DLL；
- 要跨发行版、跨版本批量部署；
- 对键盘输入丢失、剪贴板变化或错误操作零容忍。

最合理的当前评价是：

> **一个进展很快、X11 部分已经具有实用性、且本轮已把 GNOME Wayland 全局热键
> （扩展后端）、Portal 拒绝路径、AT-SPI 控件最小自动化、IME 状态检测和
> 剪贴板慢应用超时补齐并通过实机验证的原型；离“通用、可靠、原生 Wayland 的
> Linux AutoHotkey”仍有架构和工程距离（evdev 捕获侧、IME 双向、全深度
> AT-SPI、跨发行版矩阵）。**

---

## 附录：check0820 项交付状态对照表（round-37 结束）

| 项 | 实现 | 验证 | 记录 |
|---|---|---|---|
| aaaa/长按自动重复 | round-36 已实现 | assert_repeat 4/4（Xvfb 两连跑） | 透传 down-copy 被 passive grab 吸纳的背离：pt 套件 F11 双发权威覆盖 |
| kill -9 后热键释放（X11 实测） | round-36 已实现 | assert_hotkey_pt kill9 项 + 1093 绿 | X11 grab 随连接关闭自动释放 |
| 剪贴板多 MIME/大文本/慢消费者 | round-36 已实现；本轮增慢所有者超时 | assert_clipboard 13/13 + assert_clipboard_slow 4/4（Xvfb） | `AHK_CLIPBOARD_TIMEOUT_MS` 默认 2000 |
| 慢应用延迟读剪贴板（超时 env） | `AHK_CLIPBOARD_TIMEOUT_MS` | VM 实测：默认~1.8s 干净超时，5000 时等 2.5s 慢应答成功 | xclip_probe `--serve-delay` |
| 键盘布局动态切换 | round-36 已实现 | assert_layout 6/6（us→de→us） | — |
| Portal 取消/拒绝授权 | 既有 deny 处理（code!=0→错误，不绑定） | gnome_portal_deny.sh 4/4（GNOME VM） | 交互式 GNOME 对话框不可无头自动化，已如实记录 |
| GNOME 扩展 disable→enable 自动重注册（VM 实机） | 扩展 NameOwnerChanged→re-sync 机制；本轮修 Activated sender unique-name 校验 + 扩展 `import * as Config` | gnome_ext_reregister.sh 4/4（GNOME 49 VM，F12/^1 全链路） | 之前卡死根因：`Hotkey()` 从未到达非 X11 后端 |
| ibus/fcitx 激活态（检测+文档） | `ImeGetState()`（框架=总线 owner，组=XKB group） | VM "ibus\|0"；assert_ime 2/2 | preedit/切换未做，记录在 IME-Integration.md |
| IME/text-input wlroots input-method-v2 原型（sway 实测） | 客户端 scaffold（wm_input_method_v2_probe.c） | 未能 sway 实测：系统仅 v1 XML、无 v2 后端；mutter 无 input-method 符号 | 不可行原因如实记录（见 IME-Integration.md） |
| AT-SPI 控件自动化（Control* 最小路径） | ControlGetText/SetText/Click 走 org.a11y.atspi（读文本/设文本/DoAction） | gnome_atspi_e2e.sh 3/3（GNOME VM，GTK3 应用：读标签→点按钮→标签变化） | 修了 Control 参数索引与 XWayland 会话门 |
| 打包 AppImage/RPM | pack-appimage.sh/pack-rpm.sh 修复中；CI 待转绿 | CI f9a008cf 检测中 | 见发布节 |
| 权限模型（input 组/udev/polkit） | tools/linux/permissions/ 三件套 + 安装脚本 | VM 安装实测（udev 规则生效、mono 入 input 组、polkit 文件装入） | 面向未来 ahk-inputd |
| GNOME 版本耦合（扩展 disable/enable/重启） | disable→enable 已实测；shell 整体重启 re-sync 机制在 Dispatch 重连路径 | 实机 disable→enable 4/4 | 45-50 版本声明 + 未验证 major 警告 |
| 文档/计数同步 | CHECK_REPORT 1099、README/GOALS/ChangeLog/linux-port.htm 全部更新 | verify_report_numbers.sh 通过（x11=1099, wayland=17, xwayland=247） | 本表即 check0820 状态对照表 |
| 发布 v2.0.26-linux.15 | 待发布（保留 linux.14 资产 + 新签名） | 待 CI 全绿后执行 | 见发布节 |