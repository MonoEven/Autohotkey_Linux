# AutoHotkey Linux 化迁移目标

> 目标随进展持续更新。基线：AutoHotkey v2.0.26，分支：`linux-port`。
> **状态：核心目标已完成并发布（最新 v2.0.26-linux.13：Unicode 文本发送 + 输入后端加固，doc-check core+ASan 1063/1063，回归 27/27，Wayland 15/15，XWayland 247/247，CI 全绿；项目定位为 technology preview；站点文档为英文单语，中文概览页已移除）。**

## 总目标

让 AutoHotkey v2 在 Linux 桌面上可运行：优先 X11（含 XWayland），并提供原生 Wayland 后端。

✅ **已完成**：X11 后端全功能 + 原生 Wayland 后端 + XWayland 回退，已打包发布。

## 里程碑目标

### M1：构建与基础环境 ✅
- [x] 创建 `Autohotkey_Linux` 目录并克隆 v2.0.26
- [x] 创建 `linux-port` 分支
- [x] 在 WSL Ubuntu 24.04 安装 CMake/Ninja/X11 开发库
- [x] CMake 脚手架可配置、可构建
- [x] 提供 Linux 下可用的 `stdafx.h` / Win32 兼容层（初版）

### M2：核心解释器可在 Linux 编译 ✅
- [x] 核心模块（script/var/util/TextIO/error 等）通过 Linux 编译
- [x] 提供 Linux 命令行入口 `main()`，替代 `_tWinMain`（含 `--version`/`--help`）
- [x] 能解析并执行不依赖 GUI/热键的脚本（MsgBox/变量赋值/算术表达式）
- [x] 建立回归测试 `tests/run_tests.sh`（27 项，常规与 ASan 构建全部通过）
- [x] 建立 CI（`.github/workflows/ci.yml`，常规 + ASan 双构建 + 全量 doc-check + Wayland，见 M5）

### M3：平台抽象与 X11 后端 ✅
- [x] 平台层：未采用 `PlatformAbstraction.h` 抽象类，而是按模块直接实现 Linux 后端
      （`source/linux/core/core_*_linux.cpp`），效果等同且更贴合移植版结构
- [x] 实现 X11 后端（Xlib + XTest + XRandR/Xinerama + XGrabKey 热键 + XGrabButton 鼠标热键）
- [x] 热键、Send、窗口枚举/操作、剪贴板、像素/显示器、对话框、ToolTip、
      ImageSearch、WinSetRegion 全部可用（doc-check **1063/1063** 断言通过，
      含 DllCall 29 项、D-Bus COM 18 项与 GTK3 GUI/Menu 26 项）
- [x] Unicode 文本发送（round-34，check0819 审查）：`SendText`/`Send`/热字串替换
      非 ASCII 字符 X11/XWayland 经 keysym 传输（借键码临时重映射，xdotool 方案），
      纯 Wayland 走受控剪贴板粘贴回退（wlroots），无注入路径时明确报错不静默丢字符

### M4：GUI 与系统集成 ✅（按 Linux 实际情况落地）
- [x] 消息框/输入框/文件选择对话框：真实 X11 对话框（`source/linux/gui/x11_gui.cpp`），
      无 DISPLAY 时回退控制台/stdin；`AHK_MSGBOX_AUTOCLOSE_MS` 测试钩子供自动化套件
- [x] 注册表：**不移植**（Windows 专属），相关函数按文档抛出明确错误
- [x] **COM：改用 D-Bus 实现**（`core_com_dbus_linux.cpp`）：`ComObject("service")`
      创建总线服务代理，方法调用/属性访问映射 D-Bus，`ComValue` 包装类型化值；
      `ComObjGet`/`ComObjType`/`ComObjValue`/`ComObjFlags` 可用，
      `ComObjQuery`/`ComObjConnect` 按文档报错、`ComObjArray` 有意未实现（18 项断言）
- [x] **DllCall：实现 .so 动态库调用**（`core_dllcall_linux.cpp`）：
      dlopen/dlsym + libffi，全类型支持、`&Var` 输出参数、HRESULT 报错（29 项断言）
- [x] **复杂 GUI：GTK3 后端**（`source/linux/gui/script_gui_linux.cpp`）：
      `Gui`/`GuiControl` 的窗口、布局、属性、事件与 Text/Edit/Button/CheckBox/
      Radio/DDL/Combo/ListBox/ListView/TreeView/Slider/Progress/UpDown/Tab/
      MonthCal/StatusBar/GroupBox/Link/Pic 等控件；`Submit`/`OnEvent`/`Move`/
      `GetPos`/`Opt` 可用。`Menu`/`MenuBar` 由 `script_menu_linux.cpp` 实现；
      metadata 成员调用以 libffi 替代 Windows DynaCall（26 项 Xvfb 断言）
- [x] 原生 Wayland 后端：xdg-shell 窗口、虚拟键盘/指针、wlr-screencopy 抓屏
      （无 X11 时亦可运行；XWayland 回退 247 断言 + 纯 Wayland 15 断言通过）
- [x] GNOME Shell 后端加固（round-34，check0819 审查）：Activated/Deactivated 严格区分、
      match rule 限定 sender/path/member、信号定向发给 owner、ClearOwner 无参数用调用者
      sender、热键 ID owner-scoped、RegisterMany/UnregisterMany 批量、事件队列动态化+溢出计数

### M5：完善与发布 ✅
- [x] 移植测试用例：27 项回归 + 1063 项 doc-check（Xvfb，含 GUI/未移植错误行为/覆盖补全断言）
      + Wayland/XWayland 套件
- [x] Wayland 兼容性：原生 Wayland 后端 + XWayland 回退（sway headless 验证）
- [x] 打包：`tools/linux/pack.sh` 生成 **tar.gz + .deb**；CLI 安装器
      `install.sh` 与 GUI 安装器 `install-gui.sh`（zenity/yad）
- [x] 发布与清理：历经 **v2.0.26-linux.1/.2/.3**（归档）→ **.4/.5**（归档）→
      **linux.6**（覆盖补全，994 doc-check，归档）→ **linux.7**（Reload + 热键审计第一批 +
      GIF/CUR/JPEG 图像 + 鼠标热键，1026 doc-check）→ **linux.8**（round-31 左右/通配/哈希/XI2，
      1036 doc-check，归档）→ **linux.9**（round-32 热字串真实现 + 文档站修复，
      1047 doc-check；清理旧包，1→8 全部更新点汇总）→ **linux.10**（round-33 InputHook
      实时按键采集，1053 doc-check）→ **linux.11**（统一输入后端骨架+Portal 冻结）→
      **linux.12**（GNOME Shell backend 产品化，launcher `--` 命令 + deb postrm）→
      **linux.13**（round-34：Unicode 文本发送 + GNOME D-Bus 加固 + 配置解析修正，
      1063 doc-check）；旧包已从 Release 清理，latest 收敛为 linux.13
- [x] 文档：官方 v2 文档镜像重建为 Linux 移植版（删除 v1 迁移内容，
      新增 linux-port.htm；DllCall/COM 页面含 Linux 可运行示例；
      25 个平台专属页面标注 Linux note），GitHub Pages 发布：
      https://monoeven.github.io/Autohotkey_Linux/
- [x] pages 站点修复：语言切换仅中英文且不跳出本站（删除指向第三方镜像站点的链接）、
      新增随移植版维护的中文页 `docs-v2/docs/zh.htm`、安装说明改为 Linux 实际安装方式
      （apt .deb / tar.gz + install.sh / 源码构建）

## 当前推进重点

全部里程碑目标已完成。仓库状态：

- **语言**：仅 AutoHotkey v2（v1 语法不支持，v1 迁移文档已从站点移除）
- **后端**：X11（含 XWayland）优先；无 X11 时原生 Wayland
- **函数**：**367/370** 个函数已实现；3 个有意未实现（ComObjArray/TrayTip/TraySetIcon）
      抛明确错误（见 `tests/doccheck/worklist.tsv`）；完整逐模块校验见
      `tests/doccheck/CHECK_REPORT.md`
- **热键/热字串/InputHook**：
  - 热键（round-29~31）：独立热键 X 连接、GrabSpec 差量同步 + 解除抓取、条件透传、
    BadAccess 冲突报错、动态 modifier map、XKB auto-repeat、Wayland 键码表、
    鼠标热键（XGrabButton）、左右修饰键与通配 `*`、哈希索引、XI2 raw 观察器
  - **热字串**（round-32）：真实触发（全键捕获引擎，C/*/O/X 选项、大小写跟随、
    HotIf 条件；触发词抑制并发送替换/回调），xkeycap 独立客户端验证
  - **InputHook 采集**（round-33）：捕获引擎实时喂键（缓冲/结束键 EndChar/EndKey/
    匹配表 Match/退格撤销/输入抑制）；**OnChar/OnKeyDown/OnKeyUp 通知回调
    （round-34）经排队 + 主循环派发接入**（Windows 语义参数；Unicode 字符流含 CJK）
  - Reload 真重启（round-28，退出原因 Reload，回归 t26_reload）
- **Unicode 文本与中文输入**（round-34,check0819）：
  - SendText/Send 非 ASCII 字符 X11/XWayland 经 keysym 传输（借键码临时重映射，
    xdotool 同款）；纯 Wayland（wlroots）走受控剪贴板粘贴回退；无注入路径明确报错
  - 热字串 Unicode 触发词（中文"你好"端到端匹配并替换）+ InputHook OnChar Unicode 字符流
- **GNOME Shell 后端加固**（round-34,check0819）：Activated/Deactivated 严格区分、
  match rule 限定 sender/path/member、信号定向发给 owner、ClearOwner 无参数用调用者
  sender、热键 ID owner-scoped、RegisterMany/UnregisterMany 批量、事件队列动态化+溢出计数
- **配置解析修正**（round-34）：AHK_FORCE_GLOBAL_SHORTCUTS 只把 1/true/yes/on 当真；
  未知 AHK_INPUT_BACKEND 值清晰启动警告
- **报告一致性校验**（round-34）：`tests/doccheck/verify_report_numbers.sh` 从 expect
  文件重算全部发布数字（X11 合计/XWayland/Wayland/表行）并与 CHECK_REPORT.md 比对，
  CI 强制——"847"“235 vs 247”类的文档漂移不再可能
- **CI**：GitHub Actions 常规 + ASan 双构建 + 依赖安装韧性加固（apt 重试/超时/
      azure 镜像回退，应对 runner 端 apt 源抖动）+ verify_report_numbers 校验步骤
- **定位**：项目整体标注为 **technology preview**（README/linux-port.htm），
  并引用参考项目 **AHK_X11**（phil294，Crystal 版 Linux AHK v1，X11-only）于致谢

## 后续增强（已完成）

- [x] **GitHub Actions CI**（`.github/workflows/ci.yml`）：常规 + ASan 双构建，
      跑 `tests/run_tests.sh`、`run_check.sh --xvfb`、`wayland_run.sh`
      （纯 Wayland + XWayland），package job 构建并检查 tar.gz/.deb/AppImage/rpm；
      doc-check 去掉 continue-on-error；依赖安装抗 apt 镜像故障
- [x] **更多发行版包**：`tools/linux/pack-appimage.sh`（AppImage）、
      `pack-rpm.sh`（rpm）、`PKGBUILD`（Arch）
- [x] **系统剪贴板**：`core_clipboard_linux.cpp` —— X11 CLIPBOARD selection
      （读写、跨进程经 xclip 验证）与 Wayland wl_data_device（数据源/offer）；
      无显示时保留进程内回退
- [x] **DllCall（.so 动态库）**：`core_dllcall_linux.cpp` —— dlopen/dlsym +
      libffi，全类型/输出参数/HRESULT，29 项断言（含真实 libc/libm 调用）
- [x] **COM 改用 D-Bus**：`core_com_dbus_linux.cpp` —— ComObject/ComValue/
      ComObj* 映射到桌面总线，18 项断言（真实 org.freedesktop.DBus 调用）
- [x] **输入法集成评估**：`docs/IME-Integration.md`（XIM / Wayland text-input
      分阶段方案、热键与 preedit 交互、推荐路径）
- [x] **Reload 真正实现**（round-28）：`/restart /script /pid` 协议 + SIGTERM
      → EXIT_RELOAD（OnExit reason="Reload"），回归 t26_reload；修复
      GetExitReasonString 空串桩（OnExit 的 ExitReason 此前恒为空）
- [x] **热键审计第一批**（round-29，check0818.md）：独立热键 X 连接、
      GrabSpec 差量同步 + XUngrabKey（Off 解除抓取）、条件透传（Async
      抓取 + XTEST 重注入，8 槽注入日志防循环）、BadAccess 冲突报错、
      注册后立即 Reconcile、动态 modifier map + MappingNotify、XKB
      detectable auto-repeat、Wayland 键码显式表；`assert_hotkey_pt`
      独立前台客户端测试（抑制/透传/解除抓取/HotIf-false）；CI 去掉
      doc-check 的 continue-on-error
- [x] **鼠标热键**（round-30）：XGrabButton 左/右/中/X1/X2/滚轮，`~`/HotIf-false/Off
      （解除抓取 + XTEST 重注入；按住时注入 press 会被服务器吞掉）确定性透传
- [x] **图像格式补齐**（round-30）：GIF（手写 LZW）/CUR/JPEG（libjpeg）解码，
      连同既有 ICO/PNG/BMP/PPM
- [x] **左右修饰键 + 通配 + 哈希 + XI2 观察器**（round-31）：`<^a`/`>^a`（XI2 raw
      事件观察器判侧，抓取只匹配掩码、事件按物理键码判侧，错侧透传）、通配 `*`
      （展开到主修饰组合全集）、`assert_hotkey_lr`/哈希索引唯一决议（1000 热键）
- [x] **热字串触发**（round-32）：全键捕获引擎 hold/flush/match，
      C/*/O/X 选项、大小写跟随、HotIf、端字符；XSendEvent 直投焦点窗口不重入
      抓取；`assert_hotstring` 11 断言（xkeycap 独立客户端）
- [x] **InputHook 按键采集**（round-33）：捕获引擎实时喂键——缓冲/结束键/
      匹配/上限/退格撤销/输入抑制；`CollectChar` 补全上游逻辑、`GetEndReason`
      返回 EndChar；`assert_inputhook` 6 断言（xkeycap 独立客户端）
- [x] **InputHook OnChar/OnKeyDown/OnKeyUp 通知**（round-34）：捕获引擎只排队
       通知（native 派发现场调脚本回调会重入挂起），主循环/MsgSleep 派发时经
       `LinuxCaptureDispatchInputNotifies` 触发（Windows 语义:key → (VK,SC)、
       char → 字符;SC=X11 keycode）；Unicode 字符（中文 keysym）经转换送至
       OnChar；`assert_inputhook` 6→10 断言
- [x] **热字串 Unicode 触发词**（round-34）：`LinuxCaptureChar` keysym→Unicode
       转换（Latin-1/Unicode keysym 区），中文触发词经 SendText 借键码注入
       端到端匹配并替换；`assert_hotstring` 11→13 断言
- [x] **Unicode 文本发送**（round-34,check0819 P0-1）：`LinuxSendChar`
       非 ASCII 不再静默丢弃——X11/XWayland keysym 传输（借键码
       XChangeKeyboardMapping 临时重映射+xdotool 式还原）、纯 Wayland 剪贴板
       粘贴回退（wlroots）、无注入路径亮错报字符；热字串替换路径同样接入；
       `xkeycap` 加 MappingNotify 刷新解析 Unicode keysym
- [x] **GNOME Shell 后端 D-Bus 加固**（round-34,check0819 P0-2/P0-3）：
       Activated/Deactivated 严格区分、match rule 限定 sender/path/member、
       信号定向发给 owner、ClearOwner 无参数用调用者 sender、热键 ID
       owner-scoped、RegisterMany/UnregisterMany 批量、事件队列动态化+
       溢出计数
- [x] **配置解析修正**（round-34,check0819 P0-4）：`AHK_FORCE_GLOBAL_SHORTCUTS`
       只认 1/true/yes/on；未知 `AHK_INPUT_BACKEND` 值清晰启动警告
- [x] **报告一致性机器校验**（round-34,check0819 P2）：`verify_report_numbers.sh`
       从 expect 文件重算全部发布数字并比对 CHECK_REPORT.md，接入 CI
- [x] **XDG Global Shortcuts Portal**（round-34）：`core_gshortcut_linux.*` 实现
      org.freedesktop.portal.GlobalShortcuts（v1）客户端——纯 Wayland 会话全局
      热键直接经合成器注册/触发（GNOME 45+/KDE）；正确处理 CreateSession 的
      Request/Response 异步句柄、dash-free token（规避 &lt;=1.20.x 的 SEGV）、
      a(sa{sv}) 格式与 app_id（GNOME 后端需要合法应用身份；裸 CLI 会因空
      app_id 被丢弃，需从 .desktop/app scope 运行或用 Xorg/XWayland 会话）；
      已在 Ubuntu 25.10 + GNOME 49 上打通会话/绑定/Activated 全链路
- [x] **统一输入后端接口**（round-35）：`input_backend.h/.cpp` 抽象
      Register/Unregister/Enable/Disable/capabilities/事件回调骨架，形成
      x11/portal/gnome-shell/evdev 四 backend；`AHK_INPUT_BACKEND=auto|x11|
      portal|gnome-shell|evdev` 自动选择（X11 会话→X11；GNOME Wayland+扩展→
      gnome-shell；其他 Wayland→portal；evdev 暂报 not installed）；
      `AHK_FORCE_GLOBAL_SHORTCUTS=1` 语义保留=明确要求 Portal backend
      （不被偷改）；Portal 后端冻结为稳定基线/KDE 回退
- [x] **GNOME Shell extension backend**（round-35）：`extension/` 极薄 D-Bus
      broker（`io.github.autohotkey.GlobalHotkeys1`：Register/Unregister/
      ClearOwner + Activated/Deactivated），Mutter 侧
      `grab_accelerator + Main.wm.allowKeybinding` 零确认裸键抓取（已实测
      GNOME 49 上裸 `1`/`a`/`space` 全部 ACTIVATED，5/5；缺 allowKeybinding
      会被 `_filterKeybinding` 白名单静默过滤——源码级确认）；C++ 侧
      `input_backend_gnome_shell.*`（libdbus 客户端：NameHasOwner 探测、
      差量注册/注销、Activated 队列→新线程触发热键、ClearOwner/peer-vanished
      双保险释放）；接入 LinuxHotkeyStateChanged 与主循环 dispatch；
      **实机 E2E 全链路验证**（Ubuntu 25.10 + GNOME 49）：启动零授权窗口、
      物理 `1` → Activated → 回调 fired、suppression（编辑器收不到键）、
      退出后按键恢复（unregister）、kill -9 后 grab 自动释放（fail-open）、
      `^1`/`F12`/按住 repeat（~30ms 间隔）触发、Deactivated 信号中继
      （`1 up::` 的信号源就绪）、多 script 同键冲突（先注册者持有，后者被拒）、
      扩展 disable→enable 后 AHK 自动重注册恢复（NameOwnerChanged →
      sNeedResync → Dispatch 重同步）
- [x] **pages 站点维护**：语言切换仅中英文 + 中文页 `docs/zh.htm` + 安装指南
      改为 Linux 实际方式（`docs/howto/Install.htm`）
- [x] **站点文档收敛为英文单语**：移除中文概览页 `docs/zh.htm`（含站点语言
      切换器中的 zh 项、首页中文链接），文档站点只保留英文；README 相应
      更新（本轮）

## 后续候选增强（未实现）

- [ ] **evdev/uinput 低层输入 broker**（`ahk-inputd`：EVIOCGRAB + uinput replay，
      完整 AHK 输入语义 `~1::`/remap/`A & B` 到原生 Wayland；权限模型 =
      input 组 / polkit 规则（io.github.autohotkey.inputd）；设计文档见
      linux-port.htm "Compatibility matrix and roadmap"）
- [ ] 扫描码热键与 `A & B` 前缀（需按键缓冲状态机/统一事件流；注册时已能力校验拒绝）
- [ ] GNOME Shell backend 能力边界补全：`~1::` passthrough、`1 up::`（依赖
      accelerator-deactivated 实测）、完整 `*` wildcard、多 script 同键冲突策略、
      shell restart 后 runtime 重连重注册（状态真相在 runtime，扩展只是投影）
- [ ] 输入法实际集成（Phase 1：XIM 事件过滤 + IME 状态变量；Unicode 热字串/
      InputHook 已在 keysym 层可用）
- [ ] Wayland text-input（zwp_text_input_v3）Send 文本投递（当前纯 Wayland 走
      剪贴板粘贴回退）
- [ ] AT-SPI 控件自动化后端（真实跨应用 GTK/Qt/Electron 控件操作，经
      accessibility tree；无实现）
- [ ] 桌面兼容矩阵扩展（CI 增加 Fedora/Debian/Arch 容器构建 + GNOME/KDE 实机）
