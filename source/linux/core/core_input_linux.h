// Declarations of the X11 input-module BIFs (implemented in
// core_input_linux.cpp) for use by the LMD function table.
#pragma once

#include "../../abi.h"
#include <X11/Xlib.h>

BIF_DECL(BIF_Linux_Send);
BIF_DECL(BIF_Linux_SendEvent);
BIF_DECL(BIF_Linux_SendInput);
BIF_DECL(BIF_Linux_SendPlay);
BIF_DECL(BIF_Linux_SendText);
BIF_DECL(BIF_Linux_MouseMove);
BIF_DECL(BIF_Linux_MouseClick);
BIF_DECL(BIF_Linux_MouseClickDrag);
BIF_DECL(BIF_Linux_MouseGetPos);
BIF_DECL(BIF_Linux_KeyWait);
BIF_DECL(BIF_Linux_BlockInput);
BIF_DECL(BIF_Linux_InstallKeybdHook);
BIF_DECL(BIF_Linux_InstallMouseHook);
BIF_DECL(BIF_Linux_SetCapsLockState);
BIF_DECL(BIF_Linux_SetNumLockState);
BIF_DECL(BIF_Linux_SetScrollLockState);

// Accessors for the control module (core_ctrl_linux.cpp).
void LinuxFakeButtonEvent(Display *d, unsigned int aButton, bool aDown);
void LinuxFakeMotionEvent(Display *d, int aX, int aY);
void LinuxSendKeysString(Display *d, const wchar_t *aKeys);
void LinuxSendCharsString(Display *d, const wchar_t *aKeys);
bool LinuxButtonFromNameEx(const wchar_t *aName, unsigned int &aBtn);
KeyCode LinuxKeycodeForVkEx(Display *d, vk_type aVK);

// Unicode keysym helpers (shared with the InputHook/hotstring capture
// engine): find-or-borrow a keycode for a Unicode keysym.  When a spare
// keycode was remapped, *aRemapped is set and the caller MUST call
// LinuxUnicodeRestore() after the key events have been sent (the mapping is
// server-wide and must be reverted as quickly as possible).  Returns 0 when
// neither an existing mapping nor a spare keycode is available.
KeySym LinuxCharToKeySym(wchar_t aChar);
KeyCode LinuxUnicodeKeycode(Display *d, KeySym aKeysym, bool &aRemapped);
void LinuxUnicodeRestore(Display *d, KeyCode aKeycode);

// Consume the OLDEST borrow-log entry for aKeycode and return its keysym
// (NoSymbol when the keycode was never borrowed).  The capture engine calls
// this when it processes a key event whose keycode was transiently remapped
// by the Send engine: borrow order equals event order, so consuming FIFO
// maps each event to the keysym that was installed when it was generated
// (the server mapping may already have been reverted by then).
KeySym LinuxConsumeBorrowedKeySym(KeyCode aKeycode);

// True while a Unicode borrow was made recently (< 500 ms).  The hotkey
// backend skips the MappingNotify-triggered full grab rebuild during this
// window: borrows broadcast MappingNotify but only retarget a spare keycode
// (grab targets/modifier slots are unaffected), and rebuilding thousands of
// capture grabs per borrow floods the X connection.
bool LinuxBorrowRecent();
