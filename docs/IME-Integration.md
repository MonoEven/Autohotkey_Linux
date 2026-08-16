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

Wayland text-input support (Phase 3) is substantial and depends on
compositor capabilities; it should be driven by user demand.

## Tracking

- [ ] X11: XIM open + event filter in the hotkey/input path
- [ ] X11: IME state variable (open/closed, preedit text)
- [ ] X11: IME toggle via group-switch keysym injection
- [ ] Wayland: evaluate zwp_text_input_v3 for Send text delivery
- [ ] Wayland: evaluate zwp_input_method_v2 for IME state

See `CHECK_REPORT.md` for the module status; this assessment does not
change the implemented-function matrix (IME support is a Linux-specific
extension, not part of the v2 API).
