## 结论

按“**能否作为 Linux 上真正的 AutoHotkey 替代品**”来衡量，这个仓库目前更接近：

> **一个功能覆盖面很大的 AHK v2 Linux 实验性移植版，X11 场景已有较强可用性，但原生 Wayland、中文输入、真实跨应用控件自动化和生产稳定性仍有明显缺口。**

我的综合判断：

- 作为 **AHK v2 语言解释器和 Linux 脚本运行时**：完成度约 **70%–80%**
- 作为 **X11/XWayland 下的快捷键与简单桌面自动化工具**：约 **60%–75%**
- 作为 **现代原生 Wayland 下的 AutoHotkey 替代品**：约 **25%–40%**
- 作为 **直接运行现有 Windows AHK 脚本的兼容层**：约 **20%–40%**
- 作为 **可推荐给普通用户长期生产使用的正式产品**：约 **30%–40%**
- 综合实际需求覆盖度：大约 **45% 左右，合理区间 40%–55%**

仓库宣称的“367/370 函数”“1053/1053 断言”并不是假的，但这些数字更接近“作者定义的 API 和文档断言覆盖度”，不能直接转换成“真实 AHK 脚本兼容率”或“真实 Linux 桌面自动化成功率”。([GitHub](https://github.com/MonoEven/Autohotkey_Linux))

---

## 按真实使用需求评分

| 实际需求 | 当前判断 | 主要原因 |
|---|---:|---|
| AHK v2 语法、对象、字符串、文件、流程控制 | **8/10** | 解释器直接基于 AHK v2.0.26，基础语言和大量内置函数已经接通 |
| 文件、进程、网络、正则、日期等一般脚本 | **7/10** | 普通非桌面自动化脚本大多可用，但部分 API 是 Linux 重新定义的语义 |
| X11 全局热键、鼠标和简单输入模拟 | **6.5/10** | XGrabKey、XRecord、XTest 等已有较完整实现，但仍需真实复杂脚本验证 |
| 原生 Wayland 全局热键 | **3.5/10** | GNOME 49 有扩展后端，其他桌面主要依赖 Portal；透传、重映射、组合前缀等未完成 |
| 中文、日文、Unicode 文本输入和热字串 | **1/10** | 当前 `SendText` 路径会直接丢弃所有大于 ASCII 0x7E 的字符 |
| 原生 Wayland 窗口和控件自动化 | **2.5/10** | 缺少跨客户端窗口/控件控制的完整路径，XWayland 能力不能覆盖原生 Wayland 应用 |
| 外部应用控件操作 | **4/10** | 测试中不少 Edit/List 等能力使用“虚拟状态”，并非真实 GTK/Qt/Electron 应用的无障碍控件自动化 |
| Windows AHK 脚本迁移 | **3/10** | 无 v1、Windows Registry、Win32 Message、Windows DLL 和原生 COM |
| 安装、升级和发行版支持 | **4/10** | 主发布物偏 amd64 Debian/Ubuntu；RPM/AppImage 在 CI 中甚至允许失败 |
| 工程成熟度和外部验证 | **3/10** | 核心 Wayland 后端刚加入即发布，使用者和外部反馈非常少，Issue 创建还受到限制 |

---

# 已确认的高优先级问题

## 1. 中文和所有非 ASCII `SendText` 实际上会被静默丢弃

这是目前与中文用户实际需求差距最大的问题。

当前 `LinuxSendChar()` 中明确写了：

```cpp
KeySym ks = (KeySym)(unsigned int)aChar;
if (ks > 0x7E)
    return; // Non-ASCII
```

而 `SendText`、普通文本发送以及可能依赖这条发送路径的热字串替换，都会逐字符调用这个函数。因此：

```ahk
SendText "你好"
```

大概率不会输出任何中文，而且不会抛出明确错误。Emoji、日文、韩文、俄文、带重音字符同样受影响。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/source/linux/core/core_input_linux.cpp))

这会直接破坏 AutoHotkey 最常见的实际用途之一：

- 中文短语扩展
- 邮件模板
- 地址、账号、客服话术
- 多语言文本输入
- IME 辅助
- Unicode 符号输入

仓库自己也把“输入法实际集成”和 Wayland `text-input-v3` 文本投递列为未完成项目。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/GOALS.md))

**建议将此问题定为 P0。** 即使暂时无法正确输入，也应该抛出明确异常，而不是静默丢字符。

---

## 2. GNOME 后端没有区分 Activated 和 Deactivated

GNOME 扩展会发送两种信号：

- `Activated`
- `Deactivated`

但 C++ 接收端只检查 D-Bus interface，没有检查 `member` 是哪一种。代码在把事件放入普通热键队列后，仅写了一句：

```cpp
(void)member; // Activated vs Deactivated: v1 only consumes Activated.
```

也就是说，注释说“只消费 Activated”，实际代码却没有过滤 `Deactivated`。扩展端又确实同时广播两种信号。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/linux-port/source/linux/core/input_backend_gnome_shell.cpp))

直接后果是：

- 普通按下热键和 key-up 信号无法区分；
- 如果 Mutter 对一次按键同时产生 Activated 和 Deactivated，普通热键可能触发两次；
- 当前还不能可靠实现 `1 up::`；
- 发布说明里“Deactivated 信号源就绪”并不等于完整语义已经正确接入。

这是一个明确的逻辑错误，不只是功能未完成。

---

## 3. GNOME 扩展的 `ClearOwner` 缺少调用者身份验证

`UnregisterAsync` 会验证：

```js
g.owner === invocation.get_sender()
```

但 `ClearOwnerAsync` 接受调用者传入的任意 `owner` 字符串，然后直接删除该 owner 的全部热键，没有检查：

```js
invocation.get_sender() === owner
```

因此，同一个桌面会话中的另一个 D-Bus 客户端，只要知道目标 unique bus name，就可以清除其他 AHK 进程的热键注册。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/linux-port/extension/ahk-global-hotkeys%40autohotkey.org/extension.js))

这更像是**同一用户会话内的隔离和拒绝服务缺陷**，不应夸大成远程安全漏洞，但设计上确实不正确。

更合理的接口是：

```text
ClearOwner()
```

由扩展直接使用 `invocation.get_sender()`，不要由调用者提交 owner 身份。

---

## 4. D-Bus 信号订阅过宽，未校验发送者、对象路径和信号名称

运行时的 match rule 只有：

```text
type='signal', interface='io.github.autohotkey.GlobalHotkeys1'
```

没有限制：

- sender 必须是扩展当前拥有的 well-known name；
- object path 必须匹配；
- member 必须是 Activated 或 Deactivated。

随后处理器也只检查 interface 和第一个字符串 ID。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/linux-port/source/linux/core/input_backend_gnome_shell.cpp))

因此，同一会话中的其他进程如果获得或猜到 ID，理论上可以伪造同 interface 的信号触发 AHK 热键。这至少是一个需要修复的安全加固缺口。

---

## 5. 热键 ID 由进程内指针地址生成，跨进程没有命名空间

当前 ID 是：

```cpp
snprintf(id, sizeof(id), "ahk_%p", (void *)aHk);
```

但 GNOME 扩展使用一个全局 `_grabs` Map，并且只按 `id` 判断是否已存在。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/linux-port/source/linux/core/input_backend_gnome_shell.cpp))

这意味着两个 AHK 进程如果碰巧生成相同指针地址：

- 第二个进程会被当成 ID 冲突而拒绝注册；
- 广播信号也没有按 owner 定向发送；
- 多进程之间存在误匹配或错误拒绝的风险。

ID 应至少使用：

```text
<DBus unique owner>/<monotonic registration id>
```

或者随机 UUID，而不是裸指针地址。

---

## 6. GNOME 热键事件队列固定为 64 个，溢出后直接丢事件

代码中使用：

```cpp
#define GS_MAX_PENDING 64
```

达到 64 个待处理事件后，后续信号直接返回，没有日志、计数或背压处理。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/linux-port/source/linux/core/input_backend_gnome_shell.cpp))

高频按键、按键重复、脚本回调较慢或桌面暂时卡顿时，就可能发生不可观察的事件丢失。对于键盘宏和输入自动化，“偶尔丢一次”通常比明确失败更难排查。

---

## 7. D-Bus 注册是同步阻塞的，单个热键最长等待 10 秒

GNOME 后端每次 Register/Unregister 都使用：

```cpp
dbus_connection_send_with_reply_and_block(..., 10000, ...)
```

后端探测也可能同步等待 3 秒。多个热键逐个注册时，如果扩展或 D-Bus 服务异常，启动或重新同步可能阻塞很长时间。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/linux-port/source/linux/core/input_backend_gnome_shell.cpp))

实际需求通常是几十甚至几百个热键。这种同步逐个调用的架构不适合大规模注册，应该改成异步调用、批量注册或至少降低和汇总超时。

---

## 8. `AHK_FORCE_GLOBAL_SHORTCUTS=0` 仍然会强制 Portal

代码使用的是：

```cpp
if (getenv("AHK_FORCE_GLOBAL_SHORTCUTS"))
    return AhkInputBackendKind::PORTAL;
```

它只判断变量是否存在，不判断值。因此下面这些都会启用 Portal：

```bash
AHK_FORCE_GLOBAL_SHORTCUTS=0
AHK_FORCE_GLOBAL_SHORTCUTS=false
AHK_FORCE_GLOBAL_SHORTCUTS=no
```

虽然文档写的是 `=1`。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/linux-port/source/linux/core/input_backend.cpp))

此外，未知的 `AHK_INPUT_BACKEND` 值虽然会记录错误，却继续静默进入 auto selection，而不是启动失败。这可能造成用户以为自己强制选择了某个后端，实际运行的是另一个后端。

---

# 与真实 AutoHotkey 需求的结构性差距

## 1. Wayland 后端目前只有“全局快捷键的一部分”，不是完整 AHK 输入语义

GNOME 49 扩展当前只支持 exclusive hotkeys，例如：

```ahk
1::
^1::
F12::
```

明确未支持：

```ahk
~1::       ; 透传
1 up::     ; 完整 key-up 语义
*1::       ; 完整 wildcard
a & b::    ; 自定义组合前缀
CapsLock::Ctrl  ; 重映射
```

evdev/uinput broker 也只是未来路线。项目自己的 GOALS 和发布说明都明确承认这些边界。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/releases))

因此它当前还不适合这些典型用途：

- CapsLock/Esc/Ctrl 重映射
- Vim 风格键层
- 鼠标键盘组合层
- 按住键时执行、松开时恢复
- 不阻断原键的监听
- 任意组合键和状态机
- 键盘布局层与游戏快捷键

这恰恰是很多人使用 AutoHotkey 的核心原因。

---

## 2. GNOME 方案只验证了 GNOME 49，不是通用 Wayland 方案

最新发布说明明确将扩展定位为“GNOME 49 Wayland”，实机验证环境是 Ubuntu 25.10 + GNOME 49。KDE 和其他 Wayland 桌面回退到 XDG Global Shortcuts Portal。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/releases))

所以目前的实际兼容性是：

- GNOME 49：有专用扩展，但能力有限；
- 其他 GNOME 版本：未见完整兼容矩阵；
- KDE Plasma：主要依赖 Portal；
- Sway、Hyprland、wlroots：主要是测试用协议和 compositor-specific 路径；
- 原生 Wayland 应用窗口自动化：远未达到 X11 的水平。

不能把“支持 Wayland”理解成“各发行版、各桌面、各原生 Wayland 应用均可像 Windows AHK 一样自动化”。

---

## 3. 367/370 函数不等于 99.2% 脚本兼容

函数名存在，只说明入口已经实现或重新定义；不代表它与 Windows AHK 的观察行为相同。

例如仓库明确说明：

- COM 被重新定义成 D-Bus proxy；
- `.dll` 被替换成 `.so`；
- 没有 Windows Registry；
- 没有跨 Windows 窗口的 Win32 Message；
- 无 `TrayTip` 和 `TraySetIcon`；
- v1 语法完全不支持。([GitHub](https://github.com/MonoEven/Autohotkey_Linux))

所以一个普通 Windows 脚本如果用了：

```ahk
RegRead
DllCall("user32.dll", ...)
ComObject("Excel.Application")
PostMessage
OnMessage
TraySetIcon
```

即使项目总体函数统计非常高，也不能直接迁移。

真正需要统计的不是“函数入口数量”，而是：

1. 原语义兼容；
2. 有 Linux 等价语义；
3. 仅实现占位或虚拟语义；
4. 明确不支持；
5. 能否自动化真实第三方应用。

---

## 4. `Control*` 测试不能证明真实应用控件自动化能力

测试报告明确提到 Edit/List 模块使用了“**虚拟编辑/列表状态**”。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/tests/doccheck/CHECK_REPORT.md))

这意味着某些测试验证的是：

> 项目内部维护的模拟控件状态是否按预期变化。

而不是：

> 是否真的能读取和修改 Firefox、Chrome、VS Code、LibreOffice、GTK、Qt 或 Electron 应用中的真实控件。

Linux 上要做跨应用控件自动化，通常需要：

- AT-SPI / accessibility tree；
- GTK/Qt accessibility bridge；
- 浏览器自动化协议；
- 应用专属 D-Bus API；
- 或图像识别兜底。

当前项目没有展示一个成熟的通用 AT-SPI 控件后端。因此，宣称“Control* 完整”容易高估真实可用性。

---

## 5. “统一输入后端”实际上还不是统一架构

头文件直接说明：

> X11 路径仍深度集成在 `core_hotkey_linux.cpp` 中，不通过新的 input backend interface；当选择 X11 时，统一后端的 Sync/Dispatch 基本为空操作。([GitHub](https://raw.githubusercontent.com/MonoEven/Autohotkey_Linux/linux-port/source/linux/core/input_backend.h))

这意味着目前更像是：

- 原有 X11 实现；
- Portal 实现；
- GNOME 扩展实现；
- 未来 evdev 占位；

由一个选择器拼接在一起，而不是共享同一套事件模型。

后续很容易出现：

- X11 和 Wayland 热键解析语义不同；
- key-up、wildcard、passthrough 在不同后端分别实现；
- bug 修复需要修改多套路径；
- Hotstring、InputHook、remap 无法复用统一事件流。

仓库自己把 InputHook 回调、扫描码、组合前缀等问题都归因于“统一事件流仍未完成”。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/GOALS.md))

---

# 测试和质量指标的问题

## 1. 1053 是“断言数量”，不是 1053 个独立功能

1053 断言中包括：

- 数学 44 条；
- 字符串 47 条；
- Hotkey 只有 10 条；
- 热键透传只有 8 条；
- InputHook 只有 6 条；
- GUI 32 条；
- 不支持函数正确报错也被算作通过。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/tests/doccheck/CHECK_REPORT.md))

例如，“某函数明确抛出未实现错误”也可以贡献通过断言。这对于保证行为可预测有意义，但不能被解释为功能已经实现。

---

## 2. Wayland 测试只有 13 条，而且报告数字自相矛盾

报告中写：

- Wayland 模式：13 条；
- 下一行却写“合计 Wayland：847”；
- 其他位置又写 XWayland 235 或 247。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/tests/doccheck/CHECK_REPORT.md))

`847` 很明显是文档或生成过程中的错误。这至少说明：

- 报告没有经过严谨的一致性校验；
- 宣传数字可能来自不同提交或不同统计口径；
- 测试报告不应只靠人工维护。

应让 CI 自动生成最终统计，不允许 README、Release、GOALS 和 CHECK_REPORT 分别手写数字。

---

## 3. 测试环境过于集中

CI 固定在 Ubuntu 24.04，主要使用：

- headless；
- Xvfb；
- sway headless；
- XWayland；
- ASan。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/.github/workflows/ci.yml))

这些测试适合回归，但不能替代：

- GNOME 多版本；
- KDE Plasma；
- Fedora；
- Arch；
- Debian stable；
- 不同 GTK/Qt 版本；
- 不同键盘布局；
- 中文输入法；
- 多显示器、缩放和 HiDPI；
- 真实浏览器/Electron/Office 应用；
- ARM64。

尤其是最新 GNOME 后端的实机测试并没有成为公开的自动化 CI 矩阵。

---

## 4. 发布包 CI 没有真正安装并运行

Package job 的行为主要是：

```bash
bash pack.sh
bash pack-appimage.sh || true
bash pack-rpm.sh || true
tar tzf ... | head
dpkg-deb --info ...
ls *.AppImage *.rpm || true
```

问题包括：

- AppImage 构建失败不会让 CI 失败；
- RPM 构建失败不会让 CI 失败；
- AppImage/RPM 不存在也不会失败；
- `.deb` 只读取 metadata，没有安装；
- tar 只列出文件；
- 没有执行安装后的 `ahk --version`；
- 没有运行示例脚本；
- 没有卸载和升级测试。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/blob/linux-port/.github/workflows/ci.yml))

因此“package CI 全部通过”只能证明 tar 和 deb 基本能生成，不足以证明发布物可以正常安装和运行。

---

# 项目成熟度风险

最新的统一输入后端、Portal 和 GNOME Shell extension 是在 2026 年 8 月 18–19 日集中加入的，正式 linux.12 发布也在 8 月 19 日，之后同一天还有安装器、launcher、deb 清理和文档修复提交。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/commits/linux-port/))

这说明项目开发速度很快，但也意味着这些关键功能缺少：

- 长时间 soak test；
- 大量用户环境验证；
- 多版本桌面兼容反馈；
- 外部安全审查；
- 多维护者代码审查。

仓库目前只有 1 个 star、0 个 fork、0 个公开 PR，Issue 页面没有开放问题，而且“创建 Issue 受到限制”。没有 Issue 不能证明没有问题，更可能说明外部使用和反馈仍非常有限。([GitHub](https://github.com/MonoEven/Autohotkey_Linux))

另外，`source/linux/README.md` 还写着“X11 后端尚未开始”，与当前项目宣传完全冲突，说明内部架构文档已经明显过时。([GitHub](https://github.com/MonoEven/Autohotkey_Linux/tree/linux-port/source/linux))

---

# 最值得优先修复的顺序

## P0：影响基本可用性或正确性

1. **实现 Unicode 文本发送**
   - X11 至少支持完整 Unicode；
   - Wayland 接入 `text-input-v3`、输入法或受控剪贴板粘贴路径；
   - 不支持时必须明确报错，不能静默丢字符。

2. **修复 GNOME D-Bus 逻辑**
   - 严格区分 Activated/Deactivated；
   - match rule 限制 sender、path、member；
   - 信号定向发送给 owner，而不是全局广播；
   - `ClearOwner` 使用调用者 sender，不接受任意 owner 参数；
   - ID 改为 owner-scoped UUID/计数器。

3. **处理队列和同步阻塞**
   - 动态或环形事件队列；
   - 溢出统计和警告；
   - 异步批量注册；
   - 避免每个热键 10 秒同步阻塞。

4. **修复配置解析**
   - 只将 `1/true/yes/on` 解释为真；
   - 未知 backend 值直接失败或给出清晰启动警告。

## P1：决定能否成为真正的 Linux AHK

1. 完成 evdev/uinput broker，并设计清晰的权限模型；
2. 支持 passthrough、remap、key-up、custom combo、wildcard；
3. 完成 InputHook `OnChar` / `OnKeyDown`；
4. 支持 IME 和中文热字串；
5. 建立 AT-SPI 后端，真正操作外部 GTK/Qt/Electron 控件；
6. 建立 GNOME、KDE、Sway 等真实桌面兼容矩阵。

## P2：工程和发布质量

1. `.deb/.rpm/AppImage/tar` 全部执行安装—运行—升级—卸载测试；
2. 增加 ARM64；
3. 增加 Fedora、Debian、Arch 等环境；
4. 自动生成兼容性和测试报告，消除 13/847、235/247 等冲突；
5. 开放 Issue，建立 bug template 和最小复现模板；
6. 将项目状态从“Official Release”调整为更符合现状的“Technology Preview / Alpha”，直到真实兼容矩阵建立。

---

## 最终定位建议

目前不宜将它描述为：

> “Linux 上完整可用的 AutoHotkey。”

更准确的定位是：

> “基于 AutoHotkey v2 的 Linux 原生移植实验，核心语言覆盖较广，X11 下可用于部分桌面自动化，Wayland 热键、Unicode 输入和跨应用控件操作仍处于早期阶段。”

对于下面这些需求，它已经值得试用：

- 运行 AHK v2 的普通计算、文件和流程脚本；
- X11 下的简单全局快捷键；
- X11 下的鼠标和键盘宏；
- 自己编写、专门针对该移植版的新脚本；
- 对失败和平台差异有调试能力的开发者。

对于下面这些需求，不建议直接依赖：

- 中文热字串和中文文本扩展；
- 完整键盘重映射；
- KDE/任意 Wayland 桌面的统一行为；
- 自动化原生 Wayland 第三方窗口；
- 直接迁移大量 Windows AHK 脚本；
- 企业或无人值守生产自动化；
- 对按键绝不丢失、绝不重复有严格要求的场景。

以上是基于当前代码、CI、发布说明和提交记录的静态审查；我没有在 GNOME 49、KDE 和真实中文输入法环境中独立运行该二进制，因此运行时表现仍需通过真实桌面兼容测试进一步确认。