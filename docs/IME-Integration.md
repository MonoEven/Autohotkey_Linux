# Input Method (IME) Integration Assessment

Status: **evaluated — not implemented yet**

This document assesses how the AutoHotkey v2 Linux port can integrate with
input method frameworks (ibus / fcitx), and what would be required to
implement it.  It is an engineering assessment, not a promise of a
specific timeline.

## Background

Input method frameworks on Linux provide CJK and other complex-script
input.  Two layers matter for an automation tool like AutoHotkey:

1. **Reading the composition state** — e.g. knowing whether the IME is in
   Chinese/Japanese mode, and what the current preedit (composed) text is.
   Windows AutoHotkey exposes this through `A_IME` (not in v2 core) and
   IME messages (`WM_IME_*`); scripts use it to switch input modes or to
   avoid triggering hotkeys while composing.
2. **Sending text** — `Send()` must deliver *composed* characters.  The
   port already handles this: `Send` uses the virtual keyboard with the
   correct keysyms for composed text (and on Wayland, `SendInput`-style
   text is delivered via `zwp_virtual_keyboard_v1` key events).
3. **Triggering IME state** — scripts occasionally toggle the IME
   (equivalent of Windows `IME_GETOPEN` / `IME_SETOPEN`), e.g. to force
   English mode before typing.

## Current state of the port

- **X11/XWayland**: the port uses `XTest` for key/button injection and
  `XGrabKey` for hotkeys.  IME composition happens in the focused client;
  AutoHotkey has no role in it.  `Send` of Unicode text uses the UTF-8
  keysym path (`XKeysymToKeycode` fallback to `SendString` via XTest
  `XTestFakeKeyEvent` per-character or `XTestFakeRelativeMotionEvent`
  for mouse).
- **Wayland**: the port's virtual keyboard (`zwp_virtual_keyboard_v1`)
  sends evdev keycodes; composed text input via the
  `zwp_text_input_v3` / `zwp_input_method_v2` protocols is **not** used.
- There is **no IME state API** (`A_IME` or equivalent) in the port.

## What implementing IME support would involve

### Phase 1: IME state query (X11, moderate effort)

- **XIM (X Input Method)**: connect to the X input method server via
  `XOpenIM`/`XCreateIC` and use `XmbLookupString` on key events to see
  preedit state.  Requires an event filter (`XFilterEvent`) so that
  composed keys are not treated as hotkey candidates while the IME
  consumes them.  This is the classic approach used by X11 toolkits.
- Expose a read-only variable (e.g. `A_IME` on Linux) reporting whether
  the IME is open and, optionally, the current preedit text.  The
  preedit is per-focus-window state; a script would query it for its own
  ToolTip/window or the active X window (via `XGetICValues`).

### Phase 2: IME toggling (X11, small effort)

- Send the standard XIM key events for IME toggle:
  `XSendEvent` of the configured IME toggle keysym, or use
  `XkbLockModifiers`/`XkbLockGroup` for group-based IMEs (common for
  ibus with `Group 2` mapped to the IME).  Many ibus setups respond to
  `ISO_Next_Group` keysym injection.

### Phase 3: Wayland text input (larger effort)

- Implement `zwp_text_input_v3` (or the deprecated v1) client support in
  the Wayland backend: enable text input on the focused surface,
  receive `preedit_string`/`commit_string` events, and deliver `Send`
  text through `zwp_text_input_v3.set_string`/`set_preedit` instead of
  virtual-keyboard keycodes.
- IME state query on Wayland would come from `zwp_input_method_v2`
  events (available on ibus/fcitx5 with the input-method protocol);
  this is compositor-dependent and harder to make portable.

### Phase 4: Hotkey/IME interaction (cross-cutting)

- The hotkey engine must suppress hotkey matches while a preedit is
  active (Windows behaves this way with `IME` in `Hotkey` options).
  On X11 this means routing key events through `XFilterEvent` before
  matching `XGrabKey` grabs — but note that *grabbed* keys bypass the
  normal event path, so a filtered (non-grabbed) key handling mode
  would be needed, or the IME must be excluded from hotkey keycodes.

## Recommendation

The most valuable, low-risk piece is **Phase 1 + 2 on X11**:

- Add `XOpenIM`/`XFilterEvent` integration so composed input is not
  misinterpreted by hotkeys (this also fixes a real current gap: an IME
  composition key sequence could trigger an unrelated hotkey).
- Expose a small `A_IME`-like variable (Linux extension) for state
  query, and an `IME`-toggle helper via group switch key injection.

## Wayland wlroots input-method-v2 prototype (check0820)

Status: **prototype sketched; live E2E blocked - honest reason recorded.**

A client scaffold for the wlroots input-method v2 protocol is kept at
`tools/linux/wm_input_method_v2_probe.c` (not compiled into ahk_core).
It documents the exact v2 manager bind + preedit/commit listener shape
AHK's Wayland Send path would use (replacing virtual-keyboard keycodes
with committed preedit text).

Why a live sway verification was not possible in this VM/CI matrix
(measured, not assumed):

1. **sway 1.10 / wlroots 0.18** on this system links the v2 manager
   symbols (`wlr_input_method_manager_v2_create`,
   `wlr_text_input_manager_v3_*`), so the wlroots layer is *capable* of
   serving the protocol.  But the installed protocol definitions only
   include `input-method-unstable-v1.xml` (no v2 XML), and this sway
   build does not run an ibus/fcitx input-method backend end-to-end.
2. **GNOME Shell / Mutter link NO input-method symbols at all**.  A
   string scan of `/usr/bin/gnome-shell` and a `libmutter` share for
   `input-method`, `text-input`, `zwp`, `zwlr` names returns nothing.
   Mutter does not implement the wlroots input-method-v2 (or
   text-input-v3) wire protocol; GNOME input methods go through mutter's
   internal ibus/IM-context integration, which is a different, private
   surface.  A wlroots input-method-v2 **client** therefore cannot be
   exercised against a GNOME session at all, and the zwp protocol is not
   a portable path for the GNOME desktop.
3. Neither the standalone sway in CI/VM nor GNOME runs with both the v2
   protocol exported AND an input-method backend providing preedit text,
   so end-to-end delivery could not be demonstrated in this round.

This is the concrete, recorded reason this sub-item is a scaffold, not
an integrated feature (Phase 3 in the assessment above).

## Implemented: IME state and committed-text capture

`ImeGetState()` retains its compact compatible form:

```
ImeGetState()  ->  "ibus|0"     ibus running, XKB group 0
                  "fcitx5|2"    fcitx5 running, XKB group 2
                  "none|-1"     no framework and no X display
```

`ImeStatus()` returns an Object with live fields:

- `Framework`: `ibus`, `fcitx5` or `none`;
- `Engine`: for example `libpinyin`;
- `Preedit`: 1 only while composition is active;
- `Listening`: whether the commit listener is connected;
- `Scope`: `eavesdrop`, `state-only` or `none`;
- `XkbGroup`, `Commits`, `PreeditEvents`, `LastCommit`.

### IBus

The runtime resolves the standard `IBUS_ADDRESS`/XDG config address, opens a
private registered bus connection and installs an explicit eavesdrop match for
`org.freedesktop.IBus.InputContext`. This is necessary because toolkit context
signals are unicast to their owning client. The same match observes FocusIn/
FocusOut method calls; once a focused object path is known, commits from other
contexts are ignored. It parses serialized `IBusText` from
`UpdatePreeditText` and `CommitText`, and reads `GlobalEngine` without linking
libibus.

The listener starts automatically when Hotstring/InputHook capture is active (or
explicitly when ImeStatus is queried). The GNOME 49 VM has IBus 1.5.32 +
libpinyin. An independent XWayland GTK Entry
uses the real GTK IBus module while xdotool injects `nihao+space`:

- preedit transitions reach the listener;
- commit is exactly `你好`;
- dynamic Hotstring `你好` fires;
- InputHook MatchList ends with `Input="你好"` and OnChar reports exactly `你好`;
- a prior `abc+Escape` preedit cancellation leaves no `abc` in InputHook or the
  target Entry;
- normalized trace contains exactly two `source=ime_commit, origin=ibus` Unicode
  events (U+4F60/U+597D).

XI2/evdev physical events still drive KeyDown/KeyOpt/EndKey. With a composing
engine, a physical OnChar candidate is held for at most 500ms so the toolkit's
first preedit signal can win the cross-bus race; if no preedit/commit follows it
is delivered normally. When preedit appears, the already-drained trailing
phonetic token and queued candidate callbacks are rolled back and the character
buffer freezes; a 200ms tail window absorbs commit/cancel keys when IBus and XI2
arrive in the opposite order. Cancellation leaves it unchanged, while CommitText
is the only text appended. This matches the useful Windows IME approximation.

### Fcitx5

The listener implements the documented session-bus
`org.fcitx.Fcitx.InputContext1` `CommitString`, `UpdateFormattedPreedit` and
`CurrentIM` signals, plus `Controller1.CurrentInputMethod`. CI runs an independent
D-Bus protocol producer and proves preedit `nihao`, commit `你好`, Hotstring firing
and normalized `origin=fcitx5` events. This is deliberately labelled protocol
coverage: a real Fcitx5 desktop is not installed in the current VM, so desktop
E2E remains open.

### Explicit remaining boundaries

- The listener observes commit/preedit; it does not impersonate an IME engine or
  write foreign applications' preedit state.
- Toggling the user's IME is not exposed. Sending group-switch shortcuts would
  mutate user desktop state and is not equivalent to Windows per-window IME open.
- `zwp_input_method_v2` remains unsuitable as a portable backend: one engine per
  seat, no Mutter implementation, and conflict with the user's real IME.
- Flatpak/portal IM contexts can hide toolkit-private signals; status then remains
  usable but commit capture is capability-limited and must not be claimed.

## Tracking

- [x] framework/XKB query (`ImeGetState`)
- [x] rich engine/preedit/listener status (`ImeStatus`)
- [x] IBus cross-process preedit/commit → Hotstring/InputHook
- [x] Fcitx5 InputContext1 protocol path (CI protocol oracle)
- [ ] real Fcitx5 desktop E2E
- [ ] Flatpak/portal IM visibility matrix
- [ ] IME toggle (no semantically safe cross-desktop design yet)
