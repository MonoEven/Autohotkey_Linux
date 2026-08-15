// Linux platform stubs for GUI/hook/input/clipboard symbols that are not
// yet ported.  They let the core interpreter link on Linux; real X11/GTK
// implementations will replace them later.
#include "../../stdafx.h"
#include "../../application.h"
#include "../../clipboard.h"
#include "../../hook.h"
#include "../../keyboard_mouse.h"
#include "../../script.h"
#include "../../input_object.h"
#include "../../WinGroup.h"

// --- application/message pump ---
bool MsgSleep(int, MessageMode) { return true; }
bool MsgMonitor(HWND, UINT, WPARAM, LPARAM, MSG *, LRESULT &aMsgReply) { aMsgReply = 0; return false; }
void InitNewThread(int, bool, bool, bool) {}
void ResumeUnderlyingThread() {}
BOOL IsInterruptible() { return 1; }
VOID CALLBACK MsgBoxTimeout(HWND, UINT, UINT_PTR, DWORD) {}
VOID CALLBACK RefreshInterruptibility(HWND, UINT, UINT_PTR, DWORD) {}

// --- keyboard / mouse / hook ---
void AddRemoveHooks(HookType, bool) {}
void ChangeHookState(Hotkey **, int, HookType, HookType) {}
bool HookAdjustMaxHotkeys(Hotkey **&, int &, int) { return false; }
HookType GetActiveHooks() { return (HookType)0; }
void GetHookStatus(LPTSTR aBuf, int aBufSize) { if (aBuf && aBufSize > 0) aBuf[0] = 0; }
void WaitHookIdle() {}
void SendKeys(LPCTSTR, SendRawModes, SendModes, HWND) {}
void KeyEvent(KeyEventTypes, vk_type, sc_type, HWND, bool, UINT) {}
void KeyEventMenuMask(KeyEventTypes, DWORD) {}
void SetKeyHistoryMax(int) {}
ToggleValueType ToggleKeyState(vk_type, ToggleValueType aToggleValue) { return aToggleValue; }
void SetModifierLRState(modLR_type, modLR_type, HWND, bool, bool, UINT) {}
modLR_type GetModifierLRState(bool) { return 0; }
modLR_type KeyToModifiersLR(vk_type, sc_type, bool *) { return 0; }
modLR_type ConvertModifiers(mod_type) { return 0; }
mod_type ConvertModifiersLR(modLR_type) { return 0; }
LPTSTR ModifiersLRToText(modLR_type, LPTSTR aBuf) { if (aBuf) aBuf[0] = 0; return aBuf; }
sc_type TextToSC(LPCTSTR, bool *) { return 0; }
vk_type TextToVK(LPCTSTR, modLR_type *, bool, bool, HKL) { return 0; }
bool TextToVKandSC(LPCTSTR, vk_type &aVK, sc_type &aSC, modLR_type *, HKL) { aVK = 0; aSC = 0; return false; }
LPTSTR GetKeyName(vk_type, sc_type, LPTSTR aBuf, int aBufSize, LPTSTR aDefault) { if (aBuf && aBufSize > 0) aBuf[0] = 0; return aDefault ? aDefault : aBuf; }
sc_type vk_to_sc(vk_type, bool) { return 0; }
vk_type sc_to_vk(sc_type) { return 0; }

// --- clipboard ---
ResultType Clipboard::Open() { return FAIL; }
HANDLE Clipboard::GetClipboardDataTimeout(UINT, BOOL *) { return nullptr; }
ResultType Clipboard::Close(LPTSTR) { return OK; }

// --- COM ---
void DefineComPrototypeMembers() {}
BIF_DECL(ComValue_Call) { (void)aParam; (void)aParamCount; }
BIF_DECL(ComObject_Call) { (void)aParam; (void)aParamCount; }
BIF_DECL(ComObjArray_Call) { (void)aParam; (void)aParamCount; }

// --- object/runtime ---
Object *Object::DefineMetadataMembers(Object *aObj, LPCTSTR, ObjectMemberMd *, int) { return aObj; }
LPCTSTR RegExMatch(LPCTSTR, LPCTSTR) { return _T(""); }
void RegExMatchObject::Invoke(ResultToken &, int, int, ExprTokenType *aParam[], int) { (void)aParam; }
LPTSTR GetExitReasonString(ExitReasons) { return _T(""); }
void *GetDllProcAddress(LPCTSTR, HMODULE *) { return nullptr; }
DWORD GetProcessName(DWORD, LPTSTR aBuf, DWORD aBufSize, bool) { if (aBuf && aBufSize) aBuf[0] = 0; return 0; }

// --- script ---
LPTSTR Script::CurrentFile() { return _T(""); }
LineNumberType Script::CurrentLine() { return 0; }
ResultType Script::DoRunAs(LPTSTR, LPCTSTR, bool, WORD, PROCESS_INFORMATION &, bool &aSuccess, HANDLE &, DWORD &) { aSuccess = false; return FAIL; }
UserMenu *Script::FindMenu(HMENU) { return nullptr; }
Func *Script::GetBuiltInMdFunc(LPTSTR) { return nullptr; }

// --- window/group ---
WindowSpec *WinGroup::IsMember(HWND, ScriptThreadSettings &) { return nullptr; }
HKEY Line::RegConvertRootKeyType(LPTSTR) { return nullptr; }
LPTSTR Var::ObjectToText(LPTSTR, LPTSTR aBuf, int aBufSize) { if (aBuf && aBufSize > 0) aBuf[0] = 0; return aBuf; }
void Util_WinKill(HWND) {}
FResult ControlGetClassNN(HWND, HWND, LPTSTR aBuf, int aBufSize) { if (aBuf && aBufSize > 0) aBuf[0] = 0; return OK; }

// --- InputObject ---
Object *InputObject::Create() { return nullptr; }
Object *InputObject::sPrototype = nullptr;
ObjectMemberMd InputObject::sMembers[] = {};
int InputObject::sMemberCount = 0;

// --- GuiType ---
bool GuiType::Delete() { delete this; return true; }
FResult GuiType::Destroy() { return OK; }
void GuiType::DestroyIconsIfUnused(HICON, HICON) {}
GuiType *GuiType::FindGui(HWND, bool) { return nullptr; }
int GuiType::FindOrCreateFont(LPCTSTR, LPCTSTR, FontType *, COLORREF *) { return 0; }
FontType *GuiType::sFont = nullptr;
int GuiType::sFontCount = 0;
ObjectMemberMd GuiType::sMembers[] = {};
int GuiType::sMemberCount = 0;

// --- GuiControlType ---
void GuiControlType::DefineControlClasses() {}

// --- UserMenu ---
UserMenu::UserMenu(MenuTypeType) : Object() {}
UserMenu::~UserMenu() {}
void UserMenu::Dispose() {}
ResultType UserMenu::AppendStandardItems() { return OK; }
ResultType UserMenu::EnableStandardOpenItem(bool) { return OK; }
ResultType UserMenu::Display(bool, int, int) { return OK; }
UserMenuItem *UserMenu::FindItemByID(UINT) { return nullptr; }
ObjectMemberMd UserMenu::sMembers[] = {};
int UserMenu::sMemberCount = 0;