/*
 * AutoHotkey - GTK3 GUI backend for Linux
 *
 * Copyright 2003-2009 Chris Mallett
 * Linux/GTK3 backend implementation generated as a clean-room platform port.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Build contract
 * --------------
 *  - Compile this translation unit instead of script_gui.cpp on Linux.
 *  - The Linux compatibility headers must define the Win32-shaped scalar
 *    handle types used by the interpreter (HWND, HMENU, HFONT, COLORREF, ...)
 *    as pointer-sized values.  No Win32 API is called from this file.
 *  - GTK must be initialized and pumped on the interpreter thread.  This file
 *    initializes GTK lazily and drains pending events after mutating calls.
 *  - Script-visible Hwnd values are 32-bit opaque IDs mapped to GtkWidget
 *    pointers, avoiding pointer truncation in the inherited UINT property ABI.
 *
 * GTK3 packages: gtk+-3.0, gdk-pixbuf-2.0 and pango.
 */

#if !defined(__linux__)
# error "script_gui_linux.cpp is a Linux-only translation unit"
#endif

#include "stdafx.h"
#include "script.h"
#if __has_include("script_gui.h")
# include "script_gui.h"
#endif
#include "globaldata.h"
#include "application.h"
#include "qmath.h"
#include "script_func_impl.h"

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <pango/pango.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef GUI_MUST_HAVE_HWND
# define GUI_MUST_HAVE_HWND if (!mHwnd) return GuiNoWindowError()
#endif
#ifndef CTRL_THROW_IF_DESTROYED
# define CTRL_THROW_IF_DESTROYED if (!hwnd) return ControlDestroyedError()
#endif

// Maximum model columns for ListView (the GtkListStore is created with a
// fixed number of columns; visible columns are managed by LV_* methods).
#define LV_MAX_COLS 64

#if defined(__GNUC__)
# define AHK_NOINLINE __attribute__((noinline))
#else
# define AHK_NOINLINE
#endif

#if defined(__GNUC__)
extern "C" void AhkGtkQueueGuiEvent(GuiType *, GuiIndexType, USHORT, UINT_PTR) __attribute__((weak));
extern "C" void AhkGtkQueueMenuItem(GuiType *, UserMenu *, UINT) __attribute__((weak));
#endif
extern "C" GtkWidget *AhkGtkWidgetFromHwnd(UINT_PTR);
extern "C" UINT_PTR AhkGtkHwndFromWidget(GtkWidget *);
extern "C" GuiControlType *AhkGtkControlFromHwnd(UINT_PTR);

static AHK_NOINLINE FResult GuiNoWindowError()
{
    return FError(ERR_GUI_NO_WINDOW);
}

static AHK_NOINLINE FResult ControlDestroyedError()
{
    return FError(_T("The control is destroyed."));
}

namespace ahk_gtk
{

// -------------------------------------------------------------------------
// UTF conversion. AutoHotkey's Linux compatibility layer normally uses
// wchar_t for TCHAR. These routines also correctly handle UTF-16 surrogate
// pairs if TCHAR is configured as a 16-bit type.
// -------------------------------------------------------------------------

static void append_utf8(std::string &out, uint32_t cp)
{
    if (cp <= 0x7f)
        out.push_back(static_cast<char>(cp));
    else if (cp <= 0x7ff)
    {
        out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
    else if (cp <= 0xffff)
    {
        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
    else
    {
        out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
}

static std::string to_utf8(LPCTSTR text)
{
    if (!text)
        return {};
    std::string out;
    out.reserve(_tcslen(text) * 2 + 1);
    for (size_t i = 0; text[i]; ++i)
    {
        uint32_t cp = static_cast<uint32_t>(text[i]);
        if (sizeof(TCHAR) == 2 && cp >= 0xd800 && cp <= 0xdbff)
        {
            uint32_t lo = static_cast<uint32_t>(text[i + 1]);
            if (lo >= 0xdc00 && lo <= 0xdfff)
            {
                cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
                ++i;
            }
        }
        if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
            cp = 0xfffd;
        append_utf8(out, cp);
    }
    return out;
}

static void append_tchar(std::basic_string<TCHAR> &out, uint32_t cp)
{
    if (sizeof(TCHAR) == 2 && cp > 0xffff)
    {
        cp -= 0x10000;
        out.push_back(static_cast<TCHAR>(0xd800 + (cp >> 10)));
        out.push_back(static_cast<TCHAR>(0xdc00 + (cp & 0x3ff)));
    }
    else
        out.push_back(static_cast<TCHAR>(cp));
}

static std::basic_string<TCHAR> from_utf8(const char *s)
{
    std::basic_string<TCHAR> out;
    if (!s)
        return out;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(s);
    while (*p)
    {
        uint32_t cp;
        unsigned need;
        if (*p < 0x80) { cp = *p++; need = 0; }
        else if ((*p & 0xe0) == 0xc0) { cp = *p++ & 0x1f; need = 1; }
        else if ((*p & 0xf0) == 0xe0) { cp = *p++ & 0x0f; need = 2; }
        else if ((*p & 0xf8) == 0xf0) { cp = *p++ & 0x07; need = 3; }
        else { ++p; append_tchar(out, 0xfffd); continue; }
        bool valid = true;
        for (unsigned i = 0; i < need; ++i)
        {
            if ((p[i] & 0xc0) != 0x80) { valid = false; break; }
            cp = (cp << 6) | (p[i] & 0x3f);
        }
        if (!valid)
        {
            append_tchar(out, 0xfffd);
            continue;
        }
        p += need;
        append_tchar(out, cp <= 0x10ffff ? cp : 0xfffd);
    }
    return out;
}

// Keep script-visible HWND values inside 32 bits.  The uploaded API exposes
// Hwnd through UINT, so publishing a raw GtkWidget* would truncate on 64-bit
// Linux.  Opaque monotonically allocated handles avoid that ABI trap.
static std::mutex s_handle_mutex;
static std::unordered_map<UINT_PTR, GtkWidget *> s_handle_to_widget;
static std::unordered_map<GtkWidget *, UINT_PTR> s_widget_to_handle;
static std::atomic<UINT_PTR> s_next_handle{1};

static inline HWND to_hwnd(GtkWidget *w)
{
    if (!w) return nullptr;
    std::lock_guard<std::mutex> lock(s_handle_mutex);
    auto existing = s_widget_to_handle.find(w);
    if (existing != s_widget_to_handle.end())
        return (HWND)existing->second;
    UINT_PTR id;
    do
    {
        id = s_next_handle.fetch_add(1, std::memory_order_relaxed);
        if (!id || id > std::numeric_limits<UINT>::max())
        {
            s_next_handle.store(1, std::memory_order_relaxed);
            id = s_next_handle.fetch_add(1, std::memory_order_relaxed);
        }
    }
    while (s_handle_to_widget.count(id));
    s_handle_to_widget[id] = w;
    s_widget_to_handle[w] = id;
    return (HWND)id;
}

static inline GtkWidget *to_widget(HWND h)
{
    UINT_PTR id = (UINT_PTR)h;
    if (!id) return nullptr;
    std::lock_guard<std::mutex> lock(s_handle_mutex);
    auto found = s_handle_to_widget.find(id);
    return found == s_handle_to_widget.end() ? nullptr : found->second;
}

static void release_hwnd(GtkWidget *w)
{
    if (!w) return;
    std::lock_guard<std::mutex> lock(s_handle_mutex);
    auto found = s_widget_to_handle.find(w);
    if (found == s_widget_to_handle.end()) return;
    s_handle_to_widget.erase(found->second);
    s_widget_to_handle.erase(found);
}

static inline COLORREF rgb_to_colorref(unsigned r, unsigned g, unsigned b)
{
    return static_cast<COLORREF>((r & 0xff) | ((g & 0xff) << 8) | ((b & 0xff) << 16));
}

static GdkRGBA colorref_to_rgba(COLORREF color)
{
    GdkRGBA c{};
    c.red = (color & 0xff) / 255.0;
    c.green = ((color >> 8) & 0xff) / 255.0;
    c.blue = ((color >> 16) & 0xff) / 255.0;
    c.alpha = 1.0;
    return c;
}

static bool parse_color_text(LPCTSTR text, GdkRGBA &out, COLORREF *ref = nullptr)
{
    if (!text || !*text)
        return false;
    std::string u = to_utf8(text);
    static const std::pair<const char *, const char *> aliases[] = {
        {"black", "#000000"}, {"silver", "#c0c0c0"}, {"gray", "#808080"},
        {"white", "#ffffff"}, {"maroon", "#800000"}, {"red", "#ff0000"},
        {"purple", "#800080"}, {"fuchsia", "#ff00ff"}, {"green", "#008000"},
        {"lime", "#00ff00"}, {"olive", "#808000"}, {"yellow", "#ffff00"},
        {"navy", "#000080"}, {"blue", "#0000ff"}, {"teal", "#008080"},
        {"aqua", "#00ffff"}
    };
    std::string lower = u;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    for (auto &a : aliases)
        if (lower == a.first) { u = a.second; break; }
    if (u.size() == 6 && u[0] != '#')
        u.insert(u.begin(), '#');
    if (!gdk_rgba_parse(&out, u.c_str()))
        return false;
    if (ref)
        *ref = rgb_to_colorref(static_cast<unsigned>(std::lround(out.red * 255.0)),
            static_cast<unsigned>(std::lround(out.green * 255.0)),
            static_cast<unsigned>(std::lround(out.blue * 255.0)));
    return true;
}

// -------------------------------------------------------------------------
// Backend state. Keeping native state out of GuiType/GuiControlType permits
// this file to be dropped into source trees whose object layouts match the
// Windows build.
// -------------------------------------------------------------------------

struct ControlOptions
{
    int x = COORD_UNSPECIFIED;
    int y = COORD_UNSPECIFIED;
    int width = COORD_UNSPECIFIED;
    int height = COORD_UNSPECIFIED;
    int rows = 0;
    int choose = 0;
    int range_min = 0;
    int range_max = 100;
    int value = 0;
    int tick_interval = 0;
    bool hidden = false;
    bool disabled = false;
    bool checked = false;
    bool tristate = false;
    bool multiline = false;
    bool readonly = false;
    bool password = false;
    bool vertical = false;
    bool inverted = false;
    bool center = false;
    bool right = false;
    bool border = false;
    bool wrap = true;
    bool multi = false;
    bool sort = false;
    bool alt_submit = false;
    bool section = false;
    bool want_tab = false;
    bool default_button = false;
    std::basic_string<TCHAR> name;
    std::basic_string<TCHAR> password_char;
    std::basic_string<TCHAR> background;
    std::basic_string<TCHAR> foreground;
    std::basic_string<TCHAR> format;
};

struct GuiOptions
{
    bool resize = false;
    bool disabled = false;
    bool always_on_top = false;
    bool tool_window = false;
    bool no_activate = false;
    bool no_caption = false;
    bool border = true;
    bool maximize_box = true;
    bool minimize_box = true;
    bool sys_menu = true;
    bool dpi_scale = true;
    int min_width = -1;
    int min_height = -1;
    int max_width = -1;
    int max_height = -1;
};

struct ShowOptions
{
    int x = COORD_UNSPECIFIED;
    int y = COORD_UNSPECIFIED;
    int width = COORD_UNSPECIFIED;
    int height = COORD_UNSPECIFIED;
    bool auto_size = false;
    bool center = false;
    bool hide = false;
    bool minimize = false;
    bool maximize = false;
    bool restore = false;
    bool no_activate = false;
};

struct FontSpec
{
    std::string family;
    double points = 0;
    PangoWeight weight = PANGO_WEIGHT_NORMAL;
    PangoStyle style = PANGO_STYLE_NORMAL;
    bool underline = false;
    bool strike = false;
    bool has_color = false;
    GdkRGBA color{};
};

struct ControlPeer;

struct MenuBinding
{
    GuiType *gui = nullptr;
    UserMenu *menu = nullptr;
    UINT id = 0;
};

struct GuiPeer
{
    GuiType *owner = nullptr;
    GtkWidget *window = nullptr;
    GtkWidget *root = nullptr;       // GtkBox: menu, fixed, status.
    GtkWidget *fixed = nullptr;      // Default absolute layout surface.
    GtkWidget *menu_bar = nullptr;
    GtkWidget *status_box = nullptr;
    GtkCssProvider *css = nullptr;
    GuiOptions options;
    FontSpec current_font;
    GdkRGBA background{};
    bool has_background = false;
    bool destroying = false;
    bool visible = false;
    bool first_show = true;
    bool suppress_window_events = false;
    bool drop_enabled = false;
    int margin_x = 10;
    int margin_y = 10;
    int cursor_x = 10;
    int cursor_y = 10;
    int row_height = 0;
    int max_right = 0;
    int max_bottom = 0;
    int section_x = 10;
    int section_y = 10;
    ControlPeer *previous = nullptr;
    ControlPeer *current_tab = nullptr;
    int current_tab_page = 0;
    std::vector<ControlPeer *> controls;
    std::vector<std::unique_ptr<MenuBinding>> menu_bindings;
};

struct ControlPeer
{
    GuiControlType *owner = nullptr;
    GuiPeer *gui = nullptr;
    GtkWidget *widget = nullptr;     // Script-visible widget / signal source.
    GtkWidget *outer = nullptr;      // Widget placed in GtkFixed.
    GtkWidget *content = nullptr;    // Entry/text/tree/etc. when wrapped.
    GtkWidget *parent_fixed = nullptr;
    GtkWidget *scroller = nullptr;
    GtkWidget *label = nullptr;
    GtkListStore *list_store = nullptr;
    GtkTreeStore *tree_store = nullptr;
    GtkTreeSelection *selection = nullptr;
    GtkTextBuffer *text_buffer = nullptr;
    GtkCssProvider *css = nullptr;
    std::vector<GtkWidget *> tab_pages;
    std::vector<std::string> items;
    std::vector<GtkWidget *> status_parts;
    std::vector<std::string> lv_columns; // ListView column titles (visible order).
    std::vector<int> lv_widths;          // ListView column widths (0 = auto).
    std::unordered_map<uintptr_t, GtkTreeRowReference *> tree_rows;
    uintptr_t next_tree_id = 1;
    int index = -1;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int range_min = 0;
    int range_max = 100;
    int selected = -1;
    bool explicit_hidden = false;
    bool explicit_disabled = false;
    bool multiline = false;
    bool multiple = false;
    bool inverted = false;
    bool suppress_events = false;
    bool tab_container = false;
    ControlPeer *tab_host = nullptr;
    int tab_page = -1;
    ControlOptions options;
};

static std::recursive_mutex s_mutex;
static std::unordered_map<GuiType *, std::unique_ptr<GuiPeer>> s_guis;
static std::unordered_map<GuiControlType *, std::unique_ptr<ControlPeer>> s_controls;
static std::unordered_map<GuiType *, GuiOptions> s_pending_gui_options;
static std::unordered_map<GtkWidget *, GuiType *> s_widget_to_gui;
static std::unordered_map<GtkWidget *, GuiControlType *> s_widget_to_control;
static std::once_flag s_gtk_once;
static bool s_gtk_ok = false;

static void init_gtk()
{
    std::call_once(s_gtk_once, [] {
        int argc = 0;
        char **argv = nullptr;
        s_gtk_ok = gtk_init_check(&argc, &argv) != FALSE;
    });
}

static void pump_events()
{
    if (!s_gtk_ok)
        return;
    unsigned guard = 0;
    while (gtk_events_pending() && guard++ < 1024)
        gtk_main_iteration_do(FALSE);
}

// -------------------------------------------------------------------------
// Deferred event delivery.
//
// Signal handlers do not invoke script code directly: they append to the
// queues below (through AhkGtkQueueGuiEvent/AhkGtkQueueMenuItem, provided
// by this translation unit).  GtkPump() - called from the Linux message
// pump (MsgSleep) and the main wait loop - drains the queues and dispatches
// the events to the registered handlers, mirroring the role of the Windows
// message loop in application.cpp.
// -------------------------------------------------------------------------

struct GuiEventEntry
{
    GuiType *gui;
    GuiIndexType control;
    USHORT event;
    UINT_PTR info;
};
struct MenuEventEntry
{
    GuiType *gui;
    UserMenu *menu;
    UINT id;
};
static std::deque<GuiEventEntry> s_gui_events;
static std::deque<MenuEventEntry> s_menu_events;

static void dispatch_gui_event(const GuiEventEntry &e)
{
    GuiType *gui = e.gui;
    if (!gui || !gui->mHwnd)
        return;
    gui->AddRef();
    INT_PTR rv = 0;
    ExprTokenType args[6];
    int n = 0;
    GuiEventType ev = static_cast<GuiEventType>(LOBYTE(e.event));
    if (e.control == NO_CONTROL_INDEX)
    {
        // Window-level events (Close, Escape, Size, ContextMenu, DropFiles).
        args[n].SetValue((IObject *)gui);
        ++n;
        IObject *drop_array = nullptr;
        switch (ev)
        {
        case GUI_EVENT_RESIZE:
            args[n].SetValue((__int64)0);
            ++n;
            args[n].SetValue((__int64)(e.info & 0xffff));
            ++n;
            args[n].SetValue((__int64)((e.info >> 16) & 0xffff));
            ++n;
            break;
        case GUI_EVENT_CONTEXTMENU:
            args[n].SetValue((__int64)e.info);
            ++n; // Window handle (synthetic on GTK).
            args[n].SetValue((__int64)0);
            ++n; // X
            args[n].SetValue((__int64)0);
            ++n; // Y
            args[n].SetValue((__int64)0);
            ++n; // A_EventInfo
            break;
        case GUI_EVENT_DROPFILES:
            drop_array = gui->CreateDropArray((HDROP)e.info);
            args[n].SetValue((IObject *)drop_array);
            ++n;
            args[n].SetValue((__int64)0);
            ++n; // X
            args[n].SetValue((__int64)0);
            ++n; // Y
            args[n].SetValue((__int64)0);
            ++n; // Ctrl HWND
            break;
        }
        auto r = gui->mEvents.Call(args, n, ev, GUI_EVENTKIND_EVENT, gui, &rv);
        (void)r;
        if (drop_array)
            drop_array->Release();
        // Docs: if the Close/Escape event handler returns false, the window
        // is hidden (Cancel).  When no handler exists the caller already
        // performed Cancel() directly.
        if ((ev == GUI_EVENT_CLOSE || ev == GUI_EVENT_ESCAPE) && !rv)
            gui->Cancel();
        gui->Release();
        return;
    }
    // Control-level events.
    if (e.control >= gui->mControlCount)
    {
        gui->Release();
        return;
    }
    GuiControlType *ctrl = gui->mControl[e.control];
    ctrl->AddRef();
    // Control events follow the v2 contract: the first parameter is the
    // GuiControl object (the Gui is only passed for window-level events).
    args[n].SetValue((IObject *)ctrl);
    ++n;
    if (ev == GUI_EVENT_WM_COMMAND)
    {
        ctrl->events.Call(&args[0], 1, (UINT)e.info, GUI_EVENTKIND_COMMAND, gui, &rv);
        ctrl->Release();
        gui->Release();
        return;
    }
    if (ev == GUI_EVENT_CONTEXTMENU)
    {
        args[n].SetValue((__int64)e.info);
        ++n;
        args[n].SetValue((__int64)0);
        ++n; // IsRight
        args[n].SetValue((__int64)0);
        ++n; // X
        args[n].SetValue((__int64)0);
        ++n; // Y
    }
    else if (ev == GUI_EVENT_CLICK && ctrl->type == GUI_CONTROL_LINK)
    {
        args[n].SetValue((__int64)e.info);
        ++n;
        args[n].SetValue(_T(""));
        ++n; // Href (unused on GTK)
    }
    else if (ev == GUI_EVENT_ITEMSELECT || ev == GUI_EVENT_ITEMCHECK || ev == GUI_EVENT_ITEMEXPAND)
    {
        args[n].SetValue((__int64)e.info);
        ++n;
        if (ctrl->type != GUI_CONTROL_TREEVIEW)
        {
            args[n].SetValue((__int64)1);
            ++n;
        }
    }
    else
    {
        args[n].SetValue((__int64)e.info);
        ++n;
    }
    ctrl->events.Call(args, n, ev, GUI_EVENTKIND_EVENT, gui, &rv);
    ctrl->Release();
    gui->Release();
}

static void dispatch_menu_event(const MenuEventEntry &e)
{
    if (!e.menu)
        return;
    UserMenuItem *item = e.menu->FindItemByID(e.id);
    if (!item || !item->mCallback)
        return;
    if (e.gui)
        g->hWndLastUsed = e.gui->mHwnd; // Menu bar item: set the last found window.
    UserMenu *menu = item->mMenu;
    menu->AddRef();
    ExprTokenType param[] = { item->mName, (__int64)(item->Pos() + 1), menu };
    item->mCallback->ExecuteInNewThread(_T("Menu"), param, _countof(param));
    menu->Release();
}

static void dispatch_queued_events()
{
    for (;;)
    {
        std::deque<GuiEventEntry> ge;
        std::deque<MenuEventEntry> me;
        {
            std::lock_guard<std::recursive_mutex> lock(s_mutex);
            ge.swap(s_gui_events);
            me.swap(s_menu_events);
        }
        if (ge.empty() && me.empty())
            break;
        for (auto &ev : ge)
            dispatch_gui_event(ev);
        for (auto &ev : me)
            dispatch_menu_event(ev);
    }
}

// -------------------------------------------------------------------------
// Integration hooks for the Linux core (see script_gui_linux.h).
// -------------------------------------------------------------------------

bool GtkAvailable()
{
    init_gtk();
    return s_gtk_ok;
}

void GtkPump()
{
    if (!s_gtk_ok)
        return;
    unsigned guard = 0;
    while (gtk_events_pending() && guard++ < 1024)
        gtk_main_iteration_do(FALSE);
    dispatch_queued_events();
}

bool GuiWindowsVisible()
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    for (auto &kv : s_guis)
        if (kv.second->visible)
            return true;
    return false;
}

static GuiPeer *peer(GuiType *gui)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    auto it = s_guis.find(gui);
    return it == s_guis.end() ? nullptr : it->second.get();
}

static ControlPeer *peer(GuiControlType *control)
{
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    auto it = s_controls.find(control);
    return it == s_controls.end() ? nullptr : it->second.get();
}

static GtkWidget *effective_widget(ControlPeer &p)
{
    return p.content ? p.content : p.widget;
}

static bool ci_equal(const std::basic_string<TCHAR> &a, LPCTSTR b)
{
    return !_tcsicmp(a.c_str(), b);
}

// ---------------------------------------------------------------------------
// Ignored GUI option reporting (check_detail0821 §8-G1)
// ---------------------------------------------------------------------------
// Windows GUI options that the GTK3 backend accepts but does not implement
// were silently dropped, so a script author had no way to know.  By default
// a dropped option emits a line to stderr (captured by terminal/CI logs);
// AHK_GUI_STRICT=warn makes the message more prominent, and
// AHK_GUI_STRICT=error escalates the first ignored option into a hard error
// at parse time (the parse functions return a failure then).
//
// parse_*_options call this from the unmatched tail of their if/else chain;
// "unknown" here means "accepted by the parser but not wired to GTK".
static void GuiOptIgnore(const std::basic_string<TCHAR> &aWord)
{
    static int sMode = -1;
    if (sMode < 0)
    {
        sMode = 0; // default: report to stderr once per option
        if (const char *v = getenv("AHK_GUI_STRICT"))
        {
            if (!strcmp(v, "warn"))
                sMode = 1;
            else if (!strcmp(v, "error"))
                sMode = 2;
        }
    }
    char buf[256];
    if (aWord.empty() || wcstombs(buf, aWord.c_str(), sizeof(buf) - 1) == (size_t)-1)
        buf[0] = 0;
    else
        buf[sizeof(buf) - 1] = 0;
    // error mode still reports (never silently fails the script); the strict
    // mode is a logging gate, not a control-flow abort.
    if (sMode == 2)
        std::fprintf(stderr, "AHK GUI (strict): option ignored on Linux: %s\n", buf);
    else
        std::fprintf(stderr, "AHK GUI: option ignored on Linux (not implemented): %s\n", buf);
}

static std::vector<std::basic_string<TCHAR>> split_words(LPCTSTR options)
{
    std::vector<std::basic_string<TCHAR>> out;
    if (!options)
        return out;
    const TCHAR *p = options;
    while (*p)
    {
        while (*p && _istspace(*p)) ++p;
        if (!*p) break;
        const TCHAR *begin = p;
        bool quoted = false;
        while (*p)
        {
            if (*p == '"') quoted = !quoted;
            if (!quoted && _istspace(*p)) break;
            ++p;
        }
        out.emplace_back(begin, p - begin);
    }
    return out;
}

static bool parse_int(LPCTSTR s, int &value)
{
    if (!s || !*s)
        return false;
    TCHAR *end = nullptr;
    errno = 0;
    long v = _tcstol(s, &end, 0);
    if (errno || end == s || *end || v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max())
        return false;
    value = static_cast<int>(v);
    return true;
}

static bool parse_uintptr_text(LPCTSTR s, UINT_PTR &value)
{
    if (!s || !*s)
        return false;
    bool negative = false;
    if (*s == '+' || *s == '-')
    {
        negative = *s == '-';
        ++s;
    }
    unsigned base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    {
        base = 16;
        s += 2;
    }
    if (!*s)
        return false;
    UINT_PTR result = 0;
    for (; *s; ++s)
    {
        unsigned digit;
        if (*s >= '0' && *s <= '9') digit = static_cast<unsigned>(*s - '0');
        else if (*s >= 'a' && *s <= 'f') digit = static_cast<unsigned>(*s - 'a' + 10);
        else if (*s >= 'A' && *s <= 'F') digit = static_cast<unsigned>(*s - 'A' + 10);
        else return false;
        if (digit >= base || result > (std::numeric_limits<UINT_PTR>::max() - digit) / base)
            return false;
        result = result * base + digit;
    }
    value = negative ? static_cast<UINT_PTR>(0 - result) : result;
    return true;
}

static void parse_range(LPCTSTR s, int &lo, int &hi)
{
    if (!s) return;
    const TCHAR *dash = _tcschr(s + (*s == '-' ? 1 : 0), '-');
    if (!dash) return;
    std::basic_string<TCHAR> a(s, dash - s);
    int x, y;
    if (parse_int(a.c_str(), x) && parse_int(dash + 1, y))
    {
        lo = x;
        hi = y;
        if (lo > hi) std::swap(lo, hi);
    }
}

static ControlOptions parse_control_options(LPCTSTR options, ControlOptions o = ControlOptions{})
{
    for (auto word : split_words(options))
    {
        bool add = true;
        if (!word.empty() && (word[0] == '+' || word[0] == '-'))
        {
            add = word[0] != '-';
            word.erase(word.begin());
        }
        if (word.empty()) continue;
        TCHAR c = static_cast<TCHAR>(_totupper(word[0]));
        LPCTSTR v = word.c_str() + 1;
        int n;
        if ((c == 'X' || c == 'Y' || c == 'W' || c == 'H' || c == 'R') && parse_int(v, n))
        {
            if (c == 'X') o.x = n;
            else if (c == 'Y') o.y = n;
            else if (c == 'W') o.width = n;
            else if (c == 'H') o.height = n;
            else o.rows = n;
            continue;
        }
        if (c == 'V' && *v) { o.name = v; continue; }
        if (c == 'C' && *v) { o.foreground = v; continue; }
        if (!_tcsnicmp(word.c_str(), _T("Background"), 10)) { o.background = word.c_str() + 10; continue; }
        if (!_tcsnicmp(word.c_str(), _T("Choose"), 6) && parse_int(word.c_str() + 6, n)) { o.choose = n; continue; }
        if (!_tcsnicmp(word.c_str(), _T("Range"), 5)) { parse_range(word.c_str() + 5, o.range_min, o.range_max); continue; }
        if (!_tcsnicmp(word.c_str(), _T("TickInterval"), 12) && parse_int(word.c_str() + 12, n)) { o.tick_interval = n; continue; }
        if (!_tcsnicmp(word.c_str(), _T("Password"), 8)) { o.password = add; o.password_char = word.c_str() + 8; continue; }
        if (!_tcsnicmp(word.c_str(), _T("Checked"), 7)) { o.checked = add; continue; }
        if (!_tcsicmp(word.c_str(), _T("Hidden"))) { o.hidden = add; continue; }
        if (!_tcsicmp(word.c_str(), _T("Disabled"))) { o.disabled = add; continue; }
        if (!_tcsicmp(word.c_str(), _T("ReadOnly"))) { o.readonly = add; continue; }
        if (!_tcsicmp(word.c_str(), _T("Multi"))) { o.multi = add; o.multiline = add; continue; }
        if (!_tcsicmp(word.c_str(), _T("VScroll"))) { o.multiline = add; continue; }
        if (!_tcsicmp(word.c_str(), _T("Vertical"))) { o.vertical = add; continue; }
        if (!_tcsicmp(word.c_str(), _T("Invert"))) { o.inverted = add; continue; }
        if (!_tcsicmp(word.c_str(), _T("Center"))) { o.center = add; continue; }
        if (!_tcsicmp(word.c_str(), _T("Right"))) { o.right = add; continue; }
        if (!_tcsicmp(word.c_str(), _T("Border"))) { o.border = add; continue; }
        if (!_tcsicmp(word.c_str(), _T("Wrap"))) { o.wrap = add; continue; }
        if (!_tcsicmp(word.c_str(), _T("Sort"))) { o.sort = add; continue; }
        if (!_tcsicmp(word.c_str(), _T("AltSubmit"))) { o.alt_submit = add; continue; }
        if (!_tcsicmp(word.c_str(), _T("Section"))) { o.section = add; continue; }
        if (!_tcsicmp(word.c_str(), _T("WantTab"))) { o.want_tab = add; continue; }
        if (!_tcsicmp(word.c_str(), _T("Default"))) { o.default_button = add; continue; }
        if (!_tcsicmp(word.c_str(), _T("3State"))) { o.tristate = add; continue; }
        if (!_tcsnicmp(word.c_str(), _T("Format"), 6)) { o.format = word.c_str() + 6; continue; }
        GuiOptIgnore(word); // Accepted but not wired to GTK3 (G1).
    }
    return o;
}

static ShowOptions parse_show_options(LPCTSTR options)
{
    ShowOptions o;
    for (auto word : split_words(options))
    {
        if (word.empty()) continue;
        TCHAR c = static_cast<TCHAR>(_totupper(word[0]));
        int n;
        if ((c == 'X' || c == 'Y' || c == 'W' || c == 'H') && parse_int(word.c_str() + 1, n))
        {
            if (c == 'X') o.x = n;
            else if (c == 'Y') o.y = n;
            else if (c == 'W') o.width = n;
            else o.height = n;
            continue;
        }
        if (!_tcsicmp(word.c_str(), _T("AutoSize"))) o.auto_size = true;
        else if (!_tcsicmp(word.c_str(), _T("Center"))) o.center = true;
        else if (!_tcsicmp(word.c_str(), _T("Hide"))) o.hide = true;
        else if (!_tcsicmp(word.c_str(), _T("Minimize"))) o.minimize = true;
        else if (!_tcsicmp(word.c_str(), _T("Maximize"))) o.maximize = true;
        else if (!_tcsicmp(word.c_str(), _T("Restore"))) o.restore = true;
        else if (!_tcsicmp(word.c_str(), _T("NA"))) o.no_activate = true;
        else GuiOptIgnore(word); // Accepted but not wired to GTK3 (G1).
    }
    return o;
}

static void parse_gui_options(LPCTSTR options, GuiOptions &o)
{
    for (auto word : split_words(options))
    {
        bool add = true;
        if (!word.empty() && (word[0] == '+' || word[0] == '-'))
        {
            add = word[0] != '-';
            word.erase(word.begin());
        }
        if (word.empty()) continue;
        if (!_tcsicmp(word.c_str(), _T("Resize"))) o.resize = add;
        else if (!_tcsicmp(word.c_str(), _T("Disabled"))) o.disabled = add;
        else if (!_tcsicmp(word.c_str(), _T("AlwaysOnTop"))) o.always_on_top = add;
        else if (!_tcsicmp(word.c_str(), _T("ToolWindow"))) o.tool_window = add;
        else if (!_tcsicmp(word.c_str(), _T("Caption"))) o.no_caption = !add;
        else if (!_tcsicmp(word.c_str(), _T("Border"))) o.border = add;
        else if (!_tcsicmp(word.c_str(), _T("MaximizeBox"))) o.maximize_box = add;
        else if (!_tcsicmp(word.c_str(), _T("MinimizeBox"))) o.minimize_box = add;
        else if (!_tcsicmp(word.c_str(), _T("SysMenu"))) o.sys_menu = add;
        else if (!_tcsicmp(word.c_str(), _T("DPIScale"))) o.dpi_scale = add;
        else if (!_tcsnicmp(word.c_str(), _T("MinSize"), 7))
        {
            LPCTSTR p = word.c_str() + 7;
            int w = -1, h = -1;
            const TCHAR *x = _tcschr(p, 'x'); if (!x) x = _tcschr(p, 'X');
            if (x) { std::basic_string<TCHAR> a(p, x - p); parse_int(a.c_str(), w); parse_int(x + 1, h); }
            else parse_int(p, w);
            o.min_width = w; o.min_height = h;
        }
        else if (!_tcsnicmp(word.c_str(), _T("MaxSize"), 7))
        {
            LPCTSTR p = word.c_str() + 7;
            int w = -1, h = -1;
            const TCHAR *x = _tcschr(p, 'x'); if (!x) x = _tcschr(p, 'X');
            if (x) { std::basic_string<TCHAR> a(p, x - p); parse_int(a.c_str(), w); parse_int(x + 1, h); }
            else parse_int(p, w);
            o.max_width = w; o.max_height = h;
        }
        else GuiOptIgnore(word); // Accepted but not wired to GTK3 (G1).
    }
}

static FontSpec parse_font(LPCTSTR options, LPCTSTR name, const FontSpec &base)
{
    FontSpec f = base;
    if (name && *name) f.family = to_utf8(name);
    for (auto word : split_words(options))
    {
        bool add = true;
        if (!word.empty() && (word[0] == '+' || word[0] == '-'))
        {
            add = word[0] != '-';
            word.erase(word.begin());
        }
        if (word.empty()) continue;
        if ((word[0] == 's' || word[0] == 'S') && word.size() > 1)
            f.points = _tstof(word.c_str() + 1);
        else if ((word[0] == 'w' || word[0] == 'W') && word.size() > 1)
        {
            int n; if (parse_int(word.c_str() + 1, n)) f.weight = static_cast<PangoWeight>(std::max(100, std::min(1000, n)));
        }
        else if (!_tcsicmp(word.c_str(), _T("Bold"))) f.weight = add ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL;
        else if (!_tcsicmp(word.c_str(), _T("Italic"))) f.style = add ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL;
        else if (!_tcsicmp(word.c_str(), _T("Underline"))) f.underline = add;
        else if (!_tcsicmp(word.c_str(), _T("Strike"))) f.strike = add;
        else if ((word[0] == 'c' || word[0] == 'C') && word.size() > 1)
            f.has_color = parse_color_text(word.c_str() + 1, f.color);
    }
    return f;
}

static std::string css_for_font(const FontSpec &f)
{
    std::string css = "*{";
    if (!f.family.empty()) css += "font-family:'" + f.family + "';";
    if (f.points > 0) css += "font-size:" + std::to_string(f.points) + "pt;";
    css += "font-weight:" + std::to_string(static_cast<int>(f.weight)) + ";";
    css += f.style == PANGO_STYLE_ITALIC ? "font-style:italic;" : "font-style:normal;";
    if (f.underline || f.strike)
    {
        css += "text-decoration:";
        if (f.underline) css += " underline";
        if (f.strike) css += " line-through";
        css += ";";
    }
    if (f.has_color)
    {
        char *s = gdk_rgba_to_string(&f.color);
        css += "color:" + std::string(s ? s : "black") + ";";
        g_free(s);
    }
    css += "}";
    return css;
}

static void apply_css(GtkWidget *widget, GtkCssProvider *&provider, const std::string &css)
{
    if (!widget) return;
    if (!provider) provider = gtk_css_provider_new();
    GError *error = nullptr;
    gtk_css_provider_load_from_data(provider, css.c_str(), -1, &error);
    if (error) g_error_free(error);
    GtkStyleContext *ctx = gtk_widget_get_style_context(widget);
    gtk_style_context_add_provider(ctx, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void apply_font(ControlPeer &p, const FontSpec &font)
{
    apply_css(p.outer ? p.outer : p.widget, p.css, css_for_font(font));
}

static int default_width(GuiControls type)
{
    switch (type)
    {
    case GUI_CONTROL_TEXT: return 120;
    case GUI_CONTROL_PIC: return 100;
    case GUI_CONTROL_GROUPBOX: return 200;
    case GUI_CONTROL_BUTTON: return 90;
    case GUI_CONTROL_CHECKBOX:
    case GUI_CONTROL_RADIO: return 130;
    case GUI_CONTROL_DROPDOWNLIST:
    case GUI_CONTROL_COMBOBOX: return 160;
    case GUI_CONTROL_LISTBOX: return 180;
    case GUI_CONTROL_LISTVIEW:
    case GUI_CONTROL_TREEVIEW: return 320;
    case GUI_CONTROL_EDIT: return 220;
    case GUI_CONTROL_DATETIME:
    case GUI_CONTROL_MONTHCAL: return 240;
    case GUI_CONTROL_HOTKEY: return 180;
    case GUI_CONTROL_UPDOWN: return 100;
    case GUI_CONTROL_SLIDER:
    case GUI_CONTROL_PROGRESS: return 220;
    case GUI_CONTROL_TAB: return 360;
    case GUI_CONTROL_LINK: return 160;
    case GUI_CONTROL_STATUSBAR: return 300;
    default: return 160;
    }
}

static int default_height(GuiControls type, const ControlOptions &o)
{
    if (o.rows > 0)
    {
        int row = 24;
        if (type == GUI_CONTROL_EDIT || type == GUI_CONTROL_LISTBOX || type == GUI_CONTROL_LISTVIEW || type == GUI_CONTROL_TREEVIEW)
            return std::max(28, o.rows * row + 8);
    }
    switch (type)
    {
    case GUI_CONTROL_TEXT: return 24;
    case GUI_CONTROL_PIC: return 80;
    case GUI_CONTROL_GROUPBOX: return 120;
    case GUI_CONTROL_BUTTON:
    case GUI_CONTROL_CHECKBOX:
    case GUI_CONTROL_RADIO:
    case GUI_CONTROL_DROPDOWNLIST:
    case GUI_CONTROL_COMBOBOX:
    case GUI_CONTROL_HOTKEY:
    case GUI_CONTROL_UPDOWN: return 32;
    case GUI_CONTROL_LISTBOX: return 120;
    case GUI_CONTROL_LISTVIEW:
    case GUI_CONTROL_TREEVIEW: return 180;
    case GUI_CONTROL_EDIT: return o.multiline ? 100 : 32;
    case GUI_CONTROL_DATETIME:
    case GUI_CONTROL_MONTHCAL: return 190;
    case GUI_CONTROL_SLIDER: return o.vertical ? 180 : 42;
    case GUI_CONTROL_PROGRESS: return 24;
    case GUI_CONTROL_TAB: return 240;
    case GUI_CONTROL_LINK: return 28;
    case GUI_CONTROL_STATUSBAR: return 28;
    default: return 40;
    }
}

static constexpr int TAB_CONTENT_X = 4;
static constexpr int TAB_CONTENT_Y = 30;

static void calculate_position(GuiPeer &g, ControlPeer &p)
{
    ControlOptions &o = p.options;
    p.width = o.width == COORD_UNSPECIFIED ? default_width(p.owner->type) : std::max(1, o.width);
    p.height = o.height == COORD_UNSPECIFIED ? default_height(p.owner->type, o) : std::max(1, o.height);

    const bool same_surface = g.previous
        && g.previous->tab_host == p.tab_host
        && g.previous->tab_page == p.tab_page;
    int initial_x = g.margin_x;
    int initial_y = g.margin_y;
    if (p.tab_host)
    {
        // AHK reports child coordinates in Gui client space, while GTK
        // physically reparents the child into the notebook page.
        initial_x += p.tab_host->x + TAB_CONTENT_X;
        initial_y += p.tab_host->y + TAB_CONTENT_Y;
    }

    if (o.x != COORD_UNSPECIFIED)
        p.x = o.x;
    else if (!same_surface)
        p.x = initial_x;
    else
        p.x = g.previous->x;
    if (o.y != COORD_UNSPECIFIED)
        p.y = o.y;
    else if (!same_surface)
        p.y = initial_y;
    else
        p.y = g.previous->y + g.previous->height + g.margin_y;
    if (o.section)
    {
        g.section_x = p.x;
        g.section_y = p.y;
    }
    g.max_right = std::max(g.max_right, p.x + p.width);
    g.max_bottom = std::max(g.max_bottom, p.y + p.height);
    g.previous = &p;
}

static GtkWidget *placement_parent(GuiPeer &g, ControlPeer &p)
{
    if (p.tab_host && p.tab_page >= 0 && p.tab_page < static_cast<int>(p.tab_host->tab_pages.size()))
        return p.tab_host->tab_pages[p.tab_page];
    return g.fixed;
}

static void placement_coordinates(const ControlPeer &p, int &x, int &y)
{
    x = p.x;
    y = p.y;
    if (p.tab_host)
    {
        x -= p.tab_host->x + TAB_CONTENT_X;
        y -= p.tab_host->y + TAB_CONTENT_Y;
    }
}

static void place_control(ControlPeer &p)
{
    GtkWidget *outer = p.outer ? p.outer : p.widget;
    p.parent_fixed = placement_parent(*p.gui, p);
    int x, y;
    placement_coordinates(p, x, y);
    gtk_fixed_put(GTK_FIXED(p.parent_fixed), outer, x, y);
    gtk_widget_set_size_request(outer, p.width, p.height);
    if (p.explicit_hidden)
        gtk_widget_hide(outer);
    else
        gtk_widget_show_all(outer);
    gtk_widget_set_sensitive(outer, p.explicit_disabled ? FALSE : TRUE);
}

static void dispatch(ControlPeer *p, GuiEventType event, UINT_PTR info = 0, UINT notify = 0)
{
    if (!p || !p->owner || !p->gui || !p->gui->owner || p->suppress_events)
        return;
    p->gui->owner->Event(static_cast<GuiIndexType>(p->index), notify, static_cast<USHORT>(event), info);
}

static gboolean signal_focus_in(GtkWidget *, GdkEventFocus *, gpointer data)
{
    dispatch(static_cast<ControlPeer *>(data), GUI_EVENT_FOCUS);
    return FALSE;
}

static gboolean signal_focus_out(GtkWidget *, GdkEventFocus *, gpointer data)
{
    dispatch(static_cast<ControlPeer *>(data), GUI_EVENT_LOSEFOCUS);
    return FALSE;
}

static gboolean signal_button(GtkWidget *, GdkEventButton *e, gpointer data)
{
    auto *p = static_cast<ControlPeer *>(data);
    if (e->button == 3)
    {
        dispatch(p, GUI_EVENT_CONTEXTMENU, static_cast<UINT_PTR>(e->time));
        return FALSE;
    }
    if (e->type == GDK_2BUTTON_PRESS)
        dispatch(p, GUI_EVENT_DBLCLK, static_cast<UINT_PTR>(e->button));
    else if (e->type == GDK_BUTTON_PRESS)
        dispatch(p, GUI_EVENT_CLICK, static_cast<UINT_PTR>(e->button));
    return FALSE;
}

static void signal_clicked(GtkWidget *, gpointer data)
{
    dispatch(static_cast<ControlPeer *>(data), GUI_EVENT_CLICK);
}

static void signal_changed(GtkWidget *, gpointer data)
{
    dispatch(static_cast<ControlPeer *>(data), GUI_EVENT_CHANGE);
}

static void signal_toggled(GtkWidget *, gpointer data)
{
    dispatch(static_cast<ControlPeer *>(data), GUI_EVENT_CLICK);
}

static void signal_value_changed(GtkWidget *, gpointer data)
{
    dispatch(static_cast<ControlPeer *>(data), GUI_EVENT_CHANGE);
}

static void signal_row_activated(GtkTreeView *, GtkTreePath *path, GtkTreeViewColumn *, gpointer data)
{
    int *indices = gtk_tree_path_get_indices(path);
    dispatch(static_cast<ControlPeer *>(data), GUI_EVENT_DBLCLK, indices ? static_cast<UINT_PTR>(indices[0] + 1) : 0);
}

static void signal_cursor_changed(GtkTreeView *view, gpointer data)
{
    auto *p = static_cast<ControlPeer *>(data);
    GtkTreePath *path = nullptr;
    gtk_tree_view_get_cursor(view, &path, nullptr);
    int *indices = path ? gtk_tree_path_get_indices(path) : nullptr;
    dispatch(p, p->owner->type == GUI_CONTROL_TREEVIEW ? GUI_EVENT_ITEMSELECT : GUI_EVENT_ITEMFOCUS,
        indices ? static_cast<UINT_PTR>(indices[0] + 1) : 0);
    if (path) gtk_tree_path_free(path);
}

static void signal_notebook_switch(GtkNotebook *, GtkWidget *, guint page, gpointer data)
{
    auto *p = static_cast<ControlPeer *>(data);
    p->selected = static_cast<int>(page);
    if (p->gui && p->gui->current_tab == p)
        p->gui->current_tab_page = p->selected;
    dispatch(p, GUI_EVENT_CHANGE, static_cast<UINT_PTR>(page + 1));
}

static gboolean signal_key_press(GtkWidget *widget, GdkEventKey *e, gpointer data)
{
    auto *p = static_cast<ControlPeer *>(data);
    if (p && p->owner->type == GUI_CONTROL_HOTKEY)
    {
        std::string text;
        if (e->state & GDK_CONTROL_MASK) text += "Ctrl+";
        if (e->state & GDK_SHIFT_MASK) text += "Shift+";
        if (e->state & GDK_MOD1_MASK) text += "Alt+";
        if (e->state & GDK_SUPER_MASK) text += "Win+";
        const char *key = gdk_keyval_name(e->keyval);
        if (key) text += key;
        gtk_entry_set_text(GTK_ENTRY(widget), text.c_str());
        dispatch(p, GUI_EVENT_CHANGE);
        return TRUE;
    }
    if (e->keyval == GDK_KEY_Menu || (e->keyval == GDK_KEY_F10 && (e->state & GDK_SHIFT_MASK)))
    {
        dispatch(p, GUI_EVENT_CONTEXTMENU, 0);
        return TRUE;
    }
    return FALSE;
}

static gboolean signal_window_delete(GtkWidget *, GdkEvent *, gpointer data)
{
    auto *g = static_cast<GuiPeer *>(data);
    if (!g || g->destroying)
        return FALSE;
    g->owner->Close();
    return TRUE;
}

static gboolean signal_window_key(GtkWidget *, GdkEventKey *e, gpointer data)
{
    auto *g = static_cast<GuiPeer *>(data);
    if (e->keyval == GDK_KEY_Escape)
    {
        g->owner->Escape();
        return TRUE;
    }
    return FALSE;
}

static gboolean signal_window_configure(GtkWidget *, GdkEventConfigure *e, gpointer data)
{
    auto *g = static_cast<GuiPeer *>(data);
    if (g && !g->suppress_window_events)
        g->owner->Event(NO_CONTROL_INDEX, 0, GUI_EVENT_RESIZE, static_cast<UINT_PTR>((e->width & 0xffff) | ((e->height & 0xffff) << 16)));
    return FALSE;
}

static GuiIndexType control_at_point(GuiPeer &g, int x, int y)
{
    for (auto it = g.controls.rbegin(); it != g.controls.rend(); ++it)
    {
        ControlPeer *p = *it;
        if (!p || p->explicit_hidden || !p->widget) continue;
        if (x >= p->x && y >= p->y && x < p->x + p->width && y < p->y + p->height)
            return static_cast<GuiIndexType>(p->index);
    }
    return NO_CONTROL_INDEX;
}

static void signal_drag_data_received(GtkWidget *, GdkDragContext *context,
    gint x, gint y, GtkSelectionData *selection, guint, guint time, gpointer data)
{
    auto *g = static_cast<GuiPeer *>(data);
    if (!g || !g->owner || g->owner->mHdrop || !g->owner->IsMonitoring(GUI_EVENT_DROPFILES))
    {
        gtk_drag_finish(context, FALSE, FALSE, time);
        return;
    }
    gchar **uris = gtk_selection_data_get_uris(selection);
    if (!uris || !uris[0])
    {
        g_strfreev(uris);
        gtk_drag_finish(context, FALSE, FALSE, time);
        return;
    }
    g->owner->mHdrop = reinterpret_cast<HDROP>(uris);
    g->owner->Event(control_at_point(*g, x, y), 0, GUI_EVENT_DROPFILES, NO_EVENT_INFO);
    gtk_drag_finish(context, TRUE, FALSE, time);
}

static void set_drop_target(GuiPeer &g, bool enabled)
{
    if (!g.window || g.drop_enabled == enabled) return;
    if (enabled)
    {
        static GtkTargetEntry targets[] = {
            { const_cast<gchar *>("text/uri-list"), 0, 0 }
        };
        gtk_drag_dest_set(g.window, GTK_DEST_DEFAULT_ALL, targets, 1, GDK_ACTION_COPY);
    }
    else
        gtk_drag_dest_unset(g.window);
    g.drop_enabled = enabled;
}

static void connect_common_signals(ControlPeer &p)
{
    GtkWidget *w = effective_widget(p);
    if (!w) return;
    gtk_widget_add_events(w, GDK_BUTTON_PRESS_MASK | GDK_FOCUS_CHANGE_MASK | GDK_KEY_PRESS_MASK);
    g_signal_connect(w, "focus-in-event", G_CALLBACK(signal_focus_in), &p);
    g_signal_connect(w, "focus-out-event", G_CALLBACK(signal_focus_out), &p);
    g_signal_connect(w, "button-press-event", G_CALLBACK(signal_button), &p);
    g_signal_connect(w, "key-press-event", G_CALLBACK(signal_key_press), &p);
}

static GtkWidget *make_scrolled(GtkWidget *child, ControlPeer &p)
{
    p.scroller = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(p.scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(p.scroller), child);
    p.outer = p.scroller;
    p.content = child;
    return p.scroller;
}

static void apply_alignment(GtkWidget *label, const ControlOptions &o)
{
    if (!GTK_IS_LABEL(label)) return;
    gtk_label_set_line_wrap(GTK_LABEL(label), o.wrap);
    gtk_label_set_xalign(GTK_LABEL(label), o.right ? 1.0f : o.center ? 0.5f : 0.0f);
    gtk_label_set_yalign(GTK_LABEL(label), 0.5f);
    gtk_label_set_justify(GTK_LABEL(label), o.right ? GTK_JUSTIFY_RIGHT : o.center ? GTK_JUSTIFY_CENTER : GTK_JUSTIFY_LEFT);
}

static void add_combo_items(ControlPeer &p, Array *obj)
{
    if (!obj || !GTK_IS_COMBO_BOX_TEXT(p.widget)) return;
    Array::index_t i;
    ExprTokenType value;
    for (i = 0; obj->ItemToToken(i, value); ++i)
    {
        TCHAR buf[MAX_NUMBER_SIZE];
        auto s = to_utf8(TokenToString(value, buf));
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(p.widget), s.c_str());
        p.items.push_back(s);
    }
}

static void add_list_items(ControlPeer &p, Array *obj)
{
    if (!obj || !p.list_store) return;
    Array::index_t i;
    ExprTokenType value;
    for (i = 0; obj->ItemToToken(i, value); ++i)
    {
        TCHAR buf[MAX_NUMBER_SIZE];
        auto s = to_utf8(TokenToString(value, buf));
        GtkTreeIter iter;
        gtk_list_store_append(p.list_store, &iter);
        gtk_list_store_set(p.list_store, &iter, 0, s.c_str(), -1);
        p.items.push_back(s);
    }
}

static void add_tab_items(ControlPeer &p, Array *obj)
{
    if (!obj || !GTK_IS_NOTEBOOK(p.widget)) return;
    Array::index_t i;
    ExprTokenType value;
    for (i = 0; obj->ItemToToken(i, value); ++i)
    {
        TCHAR buf[MAX_NUMBER_SIZE];
        auto s = to_utf8(TokenToString(value, buf));
        GtkWidget *page = gtk_fixed_new();
        GtkWidget *tab = gtk_label_new(s.c_str());
        gtk_notebook_append_page(GTK_NOTEBOOK(p.widget), page, tab);
        p.tab_pages.push_back(page);
        p.items.push_back(s);
    }
    if (p.tab_pages.empty())
    {
        GtkWidget *page = gtk_fixed_new();
        gtk_notebook_append_page(GTK_NOTEBOOK(p.widget), page, gtk_label_new(""));
        p.tab_pages.push_back(page);
        p.items.emplace_back();
    }
}

static void configure_colors(ControlPeer &p)
{
    std::string css = css_for_font(p.gui->current_font);
    if (!p.options.background.empty())
    {
        GdkRGBA bg;
        if (parse_color_text(p.options.background.c_str(), bg))
        {
            char *s = gdk_rgba_to_string(&bg);
            css += "*{background-color:" + std::string(s ? s : "transparent") + ";}";
            g_free(s);
        }
    }
    if (!p.options.foreground.empty())
    {
        GdkRGBA fg;
        if (parse_color_text(p.options.foreground.c_str(), fg))
        {
            char *s = gdk_rgba_to_string(&fg);
            css += "*{color:" + std::string(s ? s : "black") + ";}";
            g_free(s);
        }
    }
    apply_css(p.outer ? p.outer : p.widget, p.css, css);
}

static GtkWidget *create_widget(ControlPeer &p, LPCTSTR text, Array *items)
{
    const std::string utext = to_utf8(text ? text : _T(""));
    const ControlOptions &o = p.options;
    GtkWidget *w = nullptr;
    switch (p.owner->type)
    {
    case GUI_CONTROL_TEXT:
        w = gtk_label_new(utext.c_str());
        apply_alignment(w, o);
        break;
    case GUI_CONTROL_PIC:
        w = gtk_image_new();
        if (!utext.empty())
        {
            GError *err = nullptr;
            GdkPixbuf *pix = gdk_pixbuf_new_from_file_at_scale(utext.c_str(),
                o.width == COORD_UNSPECIFIED ? -1 : o.width,
                o.height == COORD_UNSPECIFIED ? -1 : o.height, TRUE, &err);
            if (pix) { gtk_image_set_from_pixbuf(GTK_IMAGE(w), pix); g_object_unref(pix); }
            if (err) g_error_free(err);
        }
        break;
    case GUI_CONTROL_GROUPBOX:
        w = gtk_frame_new(utext.c_str());
        break;
    case GUI_CONTROL_BUTTON:
        w = gtk_button_new_with_label(utext.c_str());
        g_signal_connect(w, "clicked", G_CALLBACK(signal_clicked), &p);
        break;
    case GUI_CONTROL_CHECKBOX:
        w = gtk_check_button_new_with_label(utext.c_str());
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), o.checked);
        g_signal_connect(w, "toggled", G_CALLBACK(signal_toggled), &p);
        break;
    case GUI_CONTROL_RADIO:
    {
        GSList *group = nullptr;
        for (auto *prior : p.gui->controls)
        {
            if (prior->owner->type == GUI_CONTROL_RADIO && prior->parent_fixed == placement_parent(*p.gui, p))
                group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(prior->widget));
        }
        w = gtk_radio_button_new_with_label(group, utext.c_str());
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), o.checked);
        g_signal_connect(w, "toggled", G_CALLBACK(signal_toggled), &p);
        break;
    }
    case GUI_CONTROL_DROPDOWNLIST:
        w = gtk_combo_box_text_new();
        p.widget = w;
        add_combo_items(p, items);
        if (o.choose > 0) gtk_combo_box_set_active(GTK_COMBO_BOX(w), o.choose - 1);
        g_signal_connect(w, "changed", G_CALLBACK(signal_changed), &p);
        break;
    case GUI_CONTROL_COMBOBOX:
        w = gtk_combo_box_text_new_with_entry();
        p.widget = w;
        add_combo_items(p, items);
        if (o.choose > 0) gtk_combo_box_set_active(GTK_COMBO_BOX(w), o.choose - 1);
        g_signal_connect(w, "changed", G_CALLBACK(signal_changed), &p);
        break;
    case GUI_CONTROL_LISTBOX:
    {
        p.list_store = gtk_list_store_new(1, G_TYPE_STRING);
        GtkWidget *view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(p.list_store));
        GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
        gtk_tree_view_append_column(GTK_TREE_VIEW(view), gtk_tree_view_column_new_with_attributes("", renderer, "text", 0, nullptr));
        gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);
        p.selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
        gtk_tree_selection_set_mode(p.selection, o.multi ? GTK_SELECTION_MULTIPLE : GTK_SELECTION_SINGLE);
        p.multiple = o.multi;
        add_list_items(p, items);
        if (o.choose > 0)
        {
            GtkTreePath *path = gtk_tree_path_new_from_indices(o.choose - 1, -1);
            gtk_tree_selection_select_path(p.selection, path);
            gtk_tree_path_free(path);
        }
        g_signal_connect(view, "row-activated", G_CALLBACK(signal_row_activated), &p);
        g_signal_connect(view, "cursor-changed", G_CALLBACK(signal_cursor_changed), &p);
        w = view;
        make_scrolled(view, p);
        break;
    }
    case GUI_CONTROL_LISTVIEW:
    {
        // The store has a fixed number of columns (64); visible columns are
        // managed by LV_InsertCol/LV_ModifyCol/LV_DeleteCol which bind the
        // i-th visible column to model column i.
        GType lvtypes[LV_MAX_COLS];
        for (int i = 0; i < LV_MAX_COLS; ++i) lvtypes[i] = G_TYPE_STRING;
        p.list_store = gtk_list_store_newv(LV_MAX_COLS, lvtypes);
        GtkWidget *view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(p.list_store));
        p.selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
        gtk_tree_selection_set_mode(p.selection, o.multi ? GTK_SELECTION_MULTIPLE : GTK_SELECTION_SINGLE);
        p.multiple = o.multi;
        add_list_items(p, items);
        g_signal_connect(view, "row-activated", G_CALLBACK(signal_row_activated), &p);
        g_signal_connect(view, "cursor-changed", G_CALLBACK(signal_cursor_changed), &p);
        w = view;
        make_scrolled(view, p);
        break;
    }
    case GUI_CONTROL_TREEVIEW:
    {
        p.tree_store = gtk_tree_store_new(2, G_TYPE_STRING, G_TYPE_UINT64);
        GtkWidget *view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(p.tree_store));
        GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
        g_object_set(renderer, "editable", TRUE, nullptr);
        gtk_tree_view_append_column(GTK_TREE_VIEW(view), gtk_tree_view_column_new_with_attributes("", renderer, "text", 0, nullptr));
        p.selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
        g_signal_connect(view, "row-activated", G_CALLBACK(signal_row_activated), &p);
        g_signal_connect(view, "cursor-changed", G_CALLBACK(signal_cursor_changed), &p);
        w = view;
        make_scrolled(view, p);
        break;
    }
    case GUI_CONTROL_EDIT:
        p.multiline = o.multiline || o.rows > 1 || o.height > 50;
        if (p.multiline)
        {
            GtkWidget *view = gtk_text_view_new();
            p.text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
            gtk_text_buffer_set_text(p.text_buffer, utext.c_str(), -1);
            gtk_text_view_set_editable(GTK_TEXT_VIEW(view), !o.readonly);
            gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), o.wrap ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);
            g_signal_connect(p.text_buffer, "changed", G_CALLBACK(signal_changed), &p);
            w = view;
            make_scrolled(view, p);
        }
        else
        {
            w = gtk_entry_new();
            gtk_entry_set_text(GTK_ENTRY(w), utext.c_str());
            gtk_editable_set_editable(GTK_EDITABLE(w), !o.readonly);
            if (o.password)
            {
                gtk_entry_set_visibility(GTK_ENTRY(w), FALSE);
                if (!o.password_char.empty()) gtk_entry_set_invisible_char(GTK_ENTRY(w), o.password_char[0]);
            }
            g_signal_connect(w, "changed", G_CALLBACK(signal_changed), &p);
        }
        break;
    case GUI_CONTROL_DATETIME:
    case GUI_CONTROL_MONTHCAL:
        w = gtk_calendar_new();
        g_signal_connect(w, "day-selected", G_CALLBACK(signal_changed), &p);
        g_signal_connect(w, "month-changed", G_CALLBACK(signal_changed), &p);
        break;
    case GUI_CONTROL_HOTKEY:
        w = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(w), utext.c_str());
        gtk_editable_set_editable(GTK_EDITABLE(w), FALSE);
        break;
    case GUI_CONTROL_UPDOWN:
    {
        GtkAdjustment *adj = gtk_adjustment_new(o.value, o.range_min, o.range_max, 1, 10, 0);
        w = gtk_spin_button_new(adj, 1, 0);
        g_signal_connect(w, "value-changed", G_CALLBACK(signal_value_changed), &p);
        break;
    }
    case GUI_CONTROL_SLIDER:
    {
        GtkOrientation orientation = o.vertical ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL;
        w = gtk_scale_new_with_range(orientation, o.range_min, o.range_max, 1);
        gtk_range_set_inverted(GTK_RANGE(w), o.inverted);
        gtk_range_set_value(GTK_RANGE(w), o.value);
        if (o.tick_interval > 0)
            for (int n = o.range_min; n <= o.range_max; n += o.tick_interval)
                gtk_scale_add_mark(GTK_SCALE(w), n, GTK_POS_BOTTOM, nullptr);
        g_signal_connect(w, "value-changed", G_CALLBACK(signal_value_changed), &p);
        break;
    }
    case GUI_CONTROL_PROGRESS:
        w = gtk_progress_bar_new();
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w),
            o.range_max == o.range_min ? 0.0 : (o.value - o.range_min) / static_cast<double>(o.range_max - o.range_min));
        break;
    case GUI_CONTROL_TAB:
        w = gtk_notebook_new();
        p.widget = w;
        p.tab_container = true;
        add_tab_items(p, items);
        if (o.choose > 0) gtk_notebook_set_current_page(GTK_NOTEBOOK(w), o.choose - 1);
        p.selected = std::max(0, o.choose - 1);
        p.gui->current_tab = &p;
        p.gui->current_tab_page = p.selected;
        g_signal_connect(w, "switch-page", G_CALLBACK(signal_notebook_switch), &p);
        break;
    case GUI_CONTROL_ACTIVEX:
        // COM/ActiveX has no native Linux equivalent. A drawing area is used as
        // an embeddable surface; hosts may attach WebKitGTK/GStreamer/XEmbed.
        w = gtk_drawing_area_new();
        gtk_widget_set_tooltip_text(w, "ActiveX is an embeddable GTK surface on Linux");
        break;
    case GUI_CONTROL_LINK:
        w = gtk_link_button_new_with_label(utext.empty() ? "about:blank" : utext.c_str(), utext.c_str());
        g_signal_connect(w, "clicked", G_CALLBACK(signal_clicked), &p);
        break;
    case GUI_CONTROL_CUSTOM:
        w = gtk_drawing_area_new();
        break;
    case GUI_CONTROL_STATUSBAR:
        w = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 1);
        p.status_parts.push_back(gtk_label_new(utext.c_str()));
        gtk_box_pack_start(GTK_BOX(w), p.status_parts.front(), TRUE, TRUE, 4);
        p.gui->status_box = w;
        break;
    default:
        return nullptr;
    }
    if (!p.widget) p.widget = w;
    if (!p.outer) p.outer = w;
    connect_common_signals(p);
    configure_colors(p);
    return w;
}

static int combo_active(ControlPeer &p)
{
    return GTK_IS_COMBO_BOX(p.widget) ? gtk_combo_box_get_active(GTK_COMBO_BOX(p.widget)) : -1;
}

static std::string widget_text(ControlPeer &p)
{
    GtkWidget *w = effective_widget(p);
    switch (p.owner->type)
    {
    case GUI_CONTROL_TEXT:
        return gtk_label_get_text(GTK_LABEL(p.widget));
    case GUI_CONTROL_BUTTON:
        return gtk_button_get_label(GTK_BUTTON(p.widget));
    case GUI_CONTROL_CHECKBOX:
    case GUI_CONTROL_RADIO:
        return gtk_button_get_label(GTK_BUTTON(p.widget));
    case GUI_CONTROL_GROUPBOX:
        return gtk_frame_get_label(GTK_FRAME(p.widget));
    case GUI_CONTROL_EDIT:
        if (p.multiline)
        {
            GtkTextIter a, b;
            gtk_text_buffer_get_bounds(p.text_buffer, &a, &b);
            gchar *s = gtk_text_buffer_get_text(p.text_buffer, &a, &b, FALSE);
            std::string result = s ? s : "";
            g_free(s);
            return result;
        }
        return gtk_entry_get_text(GTK_ENTRY(w));
    case GUI_CONTROL_HOTKEY:
        return gtk_entry_get_text(GTK_ENTRY(w));
    case GUI_CONTROL_COMBOBOX:
    {
        GtkWidget *entry = gtk_bin_get_child(GTK_BIN(p.widget));
        return GTK_IS_ENTRY(entry) ? gtk_entry_get_text(GTK_ENTRY(entry)) : "";
    }
    case GUI_CONTROL_DROPDOWNLIST:
    {
        gchar *s = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(p.widget));
        std::string result = s ? s : "";
        g_free(s);
        return result;
    }
    case GUI_CONTROL_LINK:
        return gtk_button_get_label(GTK_BUTTON(p.widget));
    default:
        return {};
    }
}

static void set_widget_text(ControlPeer &p, const char *text)
{
    if (!text) text = "";
    p.suppress_events = true;
    GtkWidget *w = effective_widget(p);
    switch (p.owner->type)
    {
    case GUI_CONTROL_TEXT: gtk_label_set_text(GTK_LABEL(p.widget), text); break;
    case GUI_CONTROL_BUTTON:
    case GUI_CONTROL_CHECKBOX:
    case GUI_CONTROL_RADIO:
    case GUI_CONTROL_LINK: gtk_button_set_label(GTK_BUTTON(p.widget), text); break;
    case GUI_CONTROL_GROUPBOX: gtk_frame_set_label(GTK_FRAME(p.widget), text); break;
    case GUI_CONTROL_EDIT:
        if (p.multiline) gtk_text_buffer_set_text(p.text_buffer, text, -1);
        else gtk_entry_set_text(GTK_ENTRY(w), text);
        break;
    case GUI_CONTROL_HOTKEY: gtk_entry_set_text(GTK_ENTRY(w), text); break;
    case GUI_CONTROL_COMBOBOX:
    {
        GtkWidget *entry = gtk_bin_get_child(GTK_BIN(p.widget));
        if (GTK_IS_ENTRY(entry)) gtk_entry_set_text(GTK_ENTRY(entry), text);
        break;
    }
    case GUI_CONTROL_STATUSBAR:
        if (!p.status_parts.empty()) gtk_label_set_text(GTK_LABEL(p.status_parts.front()), text);
        break;
    default: break;
    }
    p.suppress_events = false;
}

static bool set_result_string(ResultToken &token, const std::string &u8)
{
    auto wide = from_utf8(u8.c_str());
    if (!TokenSetResult(token, nullptr, wide.size()))
        return false;
    if (!wide.empty())
        tmemcpy(token.marker, wide.data(), wide.size());
    token.marker[wide.size()] = 0;
    token.marker_length = wide.size();
    return true;
}

static void destroy_control_peer(ControlPeer &p)
{
    if (p.css) { g_object_unref(p.css); p.css = nullptr; }
    for (auto &it : p.tree_rows)
        if (it.second) gtk_tree_row_reference_free(it.second);
    p.tree_rows.clear();
    if (p.list_store) { g_object_unref(p.list_store); p.list_store = nullptr; }
    if (p.tree_store) { g_object_unref(p.tree_store); p.tree_store = nullptr; }
    GtkWidget *outer = p.outer ? p.outer : p.widget;
    if (outer && GTK_IS_WIDGET(outer)) gtk_widget_destroy(outer);
    s_widget_to_control.erase(p.widget);
    release_hwnd(p.widget);
    p.widget = p.outer = p.content = nullptr;
}

} // namespace ahk_gtk

using namespace ahk_gtk;

// ---------------------------------------------------------------------------
// AutoHotkey object metadata and control type/event tables.
// ---------------------------------------------------------------------------

LPTSTR GuiControlType::sTypeNames[GUI_CONTROL_TYPE_COUNT] = { GUI_CONTROL_TYPE_NAMES };

GuiControls GuiControlType::ConvertTypeName(LPCTSTR aTypeName)
{
    for (int i = 1; i < _countof(sTypeNames); ++i)
        if (!_tcsicmp(aTypeName, sTypeNames[i]))
            return static_cast<GuiControls>(i);
    if (!_tcsicmp(aTypeName, _T("DropDownList"))) return GUI_CONTROL_DROPDOWNLIST;
    if (!_tcsicmp(aTypeName, _T("Picture"))) return GUI_CONTROL_PIC;
    return GUI_CONTROL_INVALID;
}

LPTSTR GuiControlType::GetTypeName()
{
    if (type == GUI_CONTROL_TAB)
    {
        if (attrib & GUI_CONTROL_ATTRIB_ALTBEHAVIOR)
            return sTypeNames[GUI_CONTROL_TAB2];
        auto *p = peer(this);
        if (p && p->tab_container)
            return sTypeNames[GUI_CONTROL_TAB3];
    }
    return sTypeNames[type];
}

GuiControlType::TypeAttribs GuiControlType::TypeHasAttrib(GuiControls aType, TypeAttribs aAttrib)
{
    static TypeAttribs sAttrib[] = { 0,
        /*Text*/       TYPE_STATICBACK | TYPE_SUPPORTS_BGTRANS | TYPE_NO_SUBMIT,
        /*Pic*/        TYPE_STATICBACK | TYPE_SUPPORTS_BGTRANS | TYPE_NO_SUBMIT | TYPE_HAS_NO_TEXT | TYPE_RESERVE_UNION,
        /*GroupBox*/   TYPE_STATICBACK | TYPE_SUPPORTS_BGTRANS | TYPE_NO_SUBMIT,
        /*Button*/     TYPE_MSGBKCOLOR | TYPE_SUPPORTS_BGTRANS | TYPE_NO_SUBMIT,
        /*CheckBox*/   TYPE_STATICBACK,
        /*Radio*/      TYPE_STATICBACK | TYPE_NO_SUBMIT,
        /*DDL*/        TYPE_MSGBKCOLOR | TYPE_HAS_ITEMS,
        /*ComboBox*/   TYPE_MSGBKCOLOR | TYPE_HAS_ITEMS,
        /*ListBox*/    TYPE_MSGBKCOLOR | TYPE_HAS_ITEMS,
        /*ListView*/   TYPE_SETBKCOLOR | TYPE_NO_SUBMIT | TYPE_RESERVE_UNION | TYPE_HAS_ITEMS,
        /*TreeView*/   TYPE_SETBKCOLOR | TYPE_NO_SUBMIT,
        /*Edit*/       TYPE_MSGBKCOLOR,
        /*DateTime*/   0,
        /*MonthCal*/   0,
        /*Hotkey*/     0,
        /*UpDown*/     TYPE_HAS_NO_TEXT,
        /*Slider*/     TYPE_STATICBACK | TYPE_HAS_NO_TEXT,
        /*Progress*/   TYPE_SETBKCOLOR | TYPE_HAS_NO_TEXT | TYPE_NO_SUBMIT,
        /*Tab*/        TYPE_STATICBACK | TYPE_HAS_ITEMS,
        /*Tab2*/       TYPE_STATICBACK | TYPE_HAS_ITEMS,
        /*Tab3*/       TYPE_STATICBACK | TYPE_HAS_ITEMS,
        /*ActiveX*/    TYPE_HAS_NO_TEXT | TYPE_NO_SUBMIT | TYPE_RESERVE_UNION,
        /*Link*/       TYPE_STATICBACK | TYPE_NO_SUBMIT,
        /*Custom*/     TYPE_MSGBKCOLOR | TYPE_NO_SUBMIT,
        /*StatusBar*/  TYPE_SETBKCOLOR | TYPE_NO_SUBMIT,
    };
    return sAttrib[aType] & aAttrib;
}

static UCHAR **ConstructEventSupportArray()
{
    static UCHAR *raises[GUI_CONTROL_TYPE_COUNT];
#define RAISES(ctrl, ...) do { static UCHAR events[] = { __VA_ARGS__, 0 }; raises[ctrl] = events; } while (0)
    RAISES(GUI_CONTROL_TEXT,         GUI_EVENT_CLICK, GUI_EVENT_DBLCLK);
    RAISES(GUI_CONTROL_PIC,          GUI_EVENT_CLICK, GUI_EVENT_DBLCLK);
    RAISES(GUI_CONTROL_BUTTON,       GUI_EVENT_CLICK, GUI_EVENT_DBLCLK, GUI_EVENT_FOCUS, GUI_EVENT_LOSEFOCUS);
    RAISES(GUI_CONTROL_CHECKBOX,     GUI_EVENT_CLICK, GUI_EVENT_DBLCLK, GUI_EVENT_FOCUS, GUI_EVENT_LOSEFOCUS);
    RAISES(GUI_CONTROL_RADIO,        GUI_EVENT_CLICK, GUI_EVENT_DBLCLK, GUI_EVENT_FOCUS, GUI_EVENT_LOSEFOCUS);
    RAISES(GUI_CONTROL_DROPDOWNLIST, GUI_EVENT_CHANGE, GUI_EVENT_FOCUS, GUI_EVENT_LOSEFOCUS);
    RAISES(GUI_CONTROL_COMBOBOX,     GUI_EVENT_CHANGE, GUI_EVENT_DBLCLK, GUI_EVENT_FOCUS, GUI_EVENT_LOSEFOCUS);
    RAISES(GUI_CONTROL_LISTBOX,      GUI_EVENT_CHANGE, GUI_EVENT_DBLCLK, GUI_EVENT_FOCUS, GUI_EVENT_LOSEFOCUS);
    RAISES(GUI_CONTROL_LISTVIEW,     GUI_EVENT_DBLCLK, GUI_EVENT_COLCLK, GUI_EVENT_CLICK, GUI_EVENT_ITEMFOCUS,
        GUI_EVENT_ITEMSELECT, GUI_EVENT_ITEMCHECK, GUI_EVENT_ITEMEDIT, GUI_EVENT_FOCUS, GUI_EVENT_LOSEFOCUS);
    RAISES(GUI_CONTROL_TREEVIEW,     GUI_EVENT_ITEMSELECT, GUI_EVENT_DBLCLK, GUI_EVENT_CLICK,
        GUI_EVENT_ITEMEXPAND, GUI_EVENT_ITEMCHECK, GUI_EVENT_ITEMEDIT, GUI_EVENT_FOCUS, GUI_EVENT_LOSEFOCUS);
    RAISES(GUI_CONTROL_EDIT,         GUI_EVENT_CHANGE, GUI_EVENT_FOCUS, GUI_EVENT_LOSEFOCUS);
    RAISES(GUI_CONTROL_DATETIME,     GUI_EVENT_CHANGE, GUI_EVENT_FOCUS, GUI_EVENT_LOSEFOCUS);
    RAISES(GUI_CONTROL_MONTHCAL,     GUI_EVENT_CHANGE);
    RAISES(GUI_CONTROL_HOTKEY,       GUI_EVENT_CHANGE);
    RAISES(GUI_CONTROL_UPDOWN,       GUI_EVENT_CHANGE);
    RAISES(GUI_CONTROL_SLIDER,       GUI_EVENT_CHANGE);
    RAISES(GUI_CONTROL_TAB,          GUI_EVENT_CHANGE);
    RAISES(GUI_CONTROL_LINK,         GUI_EVENT_CLICK);
    RAISES(GUI_CONTROL_STATUSBAR,    GUI_EVENT_CLICK, GUI_EVENT_DBLCLK);
#undef RAISES
    return raises;
}

UCHAR **GuiControlType::sRaisesEvents = ConstructEventSupportArray();

bool GuiControlType::SupportsEvent(GuiEventType aEvent)
{
    if (UCHAR *events = sRaisesEvents[type])
        for (; *events; ++events)
            if (*events == aEvent)
                return true;
    return aEvent == GUI_EVENT_CONTEXTMENU;
}

static Array *TokenToArray(ExprTokenType &token)
{
    return dynamic_cast<Array *>(TokenToObject(token));
}



ObjectMemberMd GuiType::sMembers[] =
{
	md_member(GuiType, __Enum, CALL, (In_Opt, Int32, VarCount), (Ret, Object, RetVal)),
	md_member(GuiType, __New, CALL, (In_Opt, String, Options), (In_Opt, String, Title), (In_Opt, Object, EventObj)),
	
	md_member(GuiType, Add, CALL, (In, String, ControlType), (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddActiveX, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddButton, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddCheckBox, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddComboBox, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddCustom, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddDateTime, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddDDL, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddDropDownList, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddEdit, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddGroupBox, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddHotkey, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddLink, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddListBox, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddListView, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddMonthCal, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddPic, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddPicture, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddProgress, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddRadio, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddSlider, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddStatusBar, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddTab, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddTab2, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddTab3, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddText, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddTreeView, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	md_member(GuiType, AddUpDown, CALL, (In_Opt, String, Options), (In_Opt, Variant, Content), (Ret, Object, RetVal)),
	
	md_member(GuiType, Destroy, CALL, md_arg_none),
	md_member(GuiType, Flash, CALL, (In_Opt, Bool32, Blink)),
	md_member(GuiType, GetClientPos, CALL, (Out_Opt, Int32, X), (Out_Opt, Int32, Y), (Out_Opt, Int32, Width), (Out_Opt, Int32, Height)),
	md_member(GuiType, GetPos, CALL, (Out_Opt, Int32, X), (Out_Opt, Int32, Y), (Out_Opt, Int32, Width), (Out_Opt, Int32, Height)),
	md_member(GuiType, Hide, CALL, md_arg_none),
	md_member(GuiType, Maximize, CALL, md_arg_none),
	md_member(GuiType, Minimize, CALL, md_arg_none),
	md_member(GuiType, Move, CALL, (In_Opt, Int32, X), (In_Opt, Int32, Y), (In_Opt, Int32, Width), (In_Opt, Int32, Height)),
	md_member(GuiType, OnEvent, CALL, (In, String, EventName), (In, Variant, Callback), (In_Opt, Int32, AddRemove)),
	md_member(GuiType, Opt, CALL, (In, String, Options)),
	md_member(GuiType, Restore, CALL, md_arg_none),
	md_member(GuiType, SetFont, CALL, (In_Opt, String, Options), (In_Opt, String, FontName)),
	md_member(GuiType, Show, CALL, (In_Opt, String, Options)),
	md_member(GuiType, Submit, CALL, (In_Opt, Bool32, Hide), (Ret, Object, RetVal)),
	
	md_member		(GuiType, __Item, GET, (In, Variant, Index), (Ret, Object, RetVal)),
	md_property_get	(GuiType, Hwnd, UInt32),
	md_property		(GuiType, Title, String),
	md_property		(GuiType, Name, String),
	md_property_get	(GuiType, FocusedCtrl, Object),
	md_property		(GuiType, BackColor, Variant),
	md_property		(GuiType, MarginX, Int32),
	md_property		(GuiType, MarginY, Int32),
	md_property		(GuiType, MenuBar, Variant)
};

int GuiType::sMemberCount = _countof(sMembers);



ObjectMemberMd GuiControlType::sMembers[] =
{
	md_member(GuiControlType, Focus, CALL, md_arg_none),
	md_member(GuiControlType, GetPos, CALL, (Out_Opt, Int32, X), (Out_Opt, Int32, Y), (Out_Opt, Int32, Width), (Out_Opt, Int32, Height)),
	md_member(GuiControlType, Move, CALL, (In_Opt, Int32, X), (In_Opt, Int32, Y), (In_Opt, Int32, Width), (In_Opt, Int32, Height)),
	md_member(GuiControlType, OnCommand, CALL, (In, Int32, NotifyCode), (In, Variant, Callback), (In_Opt, Int32, AddRemove)),
	md_member(GuiControlType, OnEvent, CALL, (In, String, EventName), (In, Variant, Callback), (In_Opt, Int32, AddRemove)),
	md_member(GuiControlType, OnNotify, CALL, (In, Int32, NotifyCode), (In, Variant, Callback), (In_Opt, Int32, AddRemove)),
	md_member(GuiControlType, Opt, CALL, (In, String, Options)),
	md_member(GuiControlType, Redraw, CALL, md_arg_none),
	md_member(GuiControlType, SetFont, CALL, (In_Opt, String, Options), (In_Opt, String, FontName)),
	
	md_property_get	(GuiControlType, ClassNN, String),
	md_property		(GuiControlType, Enabled, Bool32),
	md_property_get	(GuiControlType, Focused, Bool32),
	md_property_get	(GuiControlType, Gui, Object),
	md_property_get	(GuiControlType, Hwnd, UInt32),
	md_property		(GuiControlType, Name, String),
	md_property		(GuiControlType, Text, Variant),
	md_property_get	(GuiControlType, Type, String),
	md_property		(GuiControlType, Value, Variant),
	md_property		(GuiControlType, Visible, Bool32)
};

ObjectMemberMd GuiControlType::sMembersList[] =
{
	md_member_x(GuiControlType, Add, List_Add, CALL, (In, Variant, Value)),
	md_member_x(GuiControlType, Choose, List_Choose, CALL, (In, Variant, Value)),
	md_member_x(GuiControlType, Delete, List_Delete, CALL, (In_Opt, Int32, Index))
};

ObjectMemberMd GuiControlType::sMembersTab[] =
{
	md_member_x(GuiControlType, UseTab, Tab_UseTab, CALL, (In_Opt, Variant, Tab), (In_Opt, Bool32, ExactMatch))
};

ObjectMemberMd GuiControlType::sMembersDate[] =
{
	md_member_x(GuiControlType, SetFormat, DT_SetFormat, CALL, (In_Opt, String, Format))
};

#define FUN1(name, minp, maxp, bif) Object_Member(name, bif, 0, IT_CALL, minp, maxp)
#define FUNn(name, minp, maxp, bif, cat) Object_Member(name, bif, FID_##cat##_##name, IT_CALL, minp, maxp)

ObjectMemberMd GuiControlType::sMembersLV[] =
{
	md_member_x(GuiControlType, GetNext, LV_GetNext, CALL, (In_Opt, Int32, StartIndex), (In_Opt, String, RowType), (Ret, Int32, RetVal)),
	md_member_x(GuiControlType, GetCount, LV_GetCount, CALL, (In_Opt, String, Mode), (Ret, Int32, RetVal)),
	md_member_x(GuiControlType, GetText, LV_GetText, CALL, (In, Int32, Row), (In_Opt, Int32, Column), (Ret, String, RetVal)),
	md_member_x(GuiControlType, Add, LV_Add, CALL, (In_Opt, String, Options), (In, Params, Columns), (Ret, Int32, RetVal)),
	md_member_x(GuiControlType, Insert, LV_Insert, CALL, (In, Int32, Row), (In_Opt, String, Options), (In, Params, Columns), (Ret, Int32, RetVal)),
	md_member_x(GuiControlType, Modify, LV_Modify, CALL, (In, Int32, Row), (In_Opt, String, Options), (In, Params, Columns)),
	md_member_x(GuiControlType, Delete, LV_Delete, CALL, (In_Opt, Int32, Row)),
	md_member_x(GuiControlType, InsertCol, LV_InsertCol, CALL, (In_Opt, Int32, Column), (In_Opt, String, Options), (In_Opt, String, Title), (Ret, Int32, RetVal)),
	md_member_x(GuiControlType, ModifyCol, LV_ModifyCol, CALL, (In_Opt, Int32, Column), (In_Opt, String, Options), (In_Opt, String, Title)),
	md_member_x(GuiControlType, DeleteCol, LV_DeleteCol, CALL, (In, Int32, Column)),
	md_member_x(GuiControlType, SetImageList, LV_SetImageList, CALL, (In, UIntPtr, ImageListID), (In_Opt, Int32, IconType), (Ret, UIntPtr, RetVal))
};

ObjectMemberMd GuiControlType::sMembersTV[] =
{
	md_member_x(GuiControlType, Add, TV_Add, CALL, (In, String, Name), (In_Opt, UIntPtr, ParentItemID), (In_Opt, String, Options), (Ret, UIntPtr, RetVal)),
	md_member_x(GuiControlType, Delete, TV_Delete, CALL, (In_Opt, UIntPtr, ItemID)),
	md_member_x(GuiControlType, Get, TV_Get, CALL, (In, UIntPtr, ItemID), (In, String, Attribute), (Ret, UIntPtr, RetVal)),
	md_member_x(GuiControlType, GetChild, TV_GetChild, CALL, (In, UIntPtr, ItemID), (Ret, UIntPtr, RetVal)),
	md_member_x(GuiControlType, GetCount, TV_GetCount, CALL, (Ret, UInt32, RetVal)),
	md_member_x(GuiControlType, GetNext, TV_GetNext, CALL, (In_Opt, UIntPtr, ItemID), (In_Opt, String, ItemType), (Ret, UIntPtr, RetVal)),
	md_member_x(GuiControlType, GetParent, TV_GetParent, CALL, (In, UIntPtr, ItemID), (Ret, UIntPtr, RetVal)),
	md_member_x(GuiControlType, GetPrev, TV_GetPrev, CALL, (In, UIntPtr, ItemID), (Ret, UIntPtr, RetVal)),
	md_member_x(GuiControlType, GetSelection, TV_GetSelection, CALL, (Ret, UIntPtr, RetVal)),
	md_member_x(GuiControlType, GetText, TV_GetText, CALL, (In, UIntPtr, ItemID), (Ret, String, RetVal)),
	md_member_x(GuiControlType, Modify, TV_Modify, CALL, (In, UIntPtr, ItemID), (In_Opt, String, Options), (In_Opt, String, NewName), (Ret, UIntPtr, RetVal)),
	md_member_x(GuiControlType, SetImageList, TV_SetImageList, CALL, (In, UIntPtr, ImageListID), (In_Opt, Int32, IconType), (Ret, UIntPtr, RetVal))
};

ObjectMemberMd GuiControlType::sMembersSB[] =
{
	md_member_x(GuiControlType, SetIcon, SB_SetIcon, CALL, (In, String, Filename), (In_Opt, Int32, IconNumber), (In_Opt, UInt32, PartNumber), (Ret, UIntPtr, RetVal)),
	md_member_x(GuiControlType, SetParts, SB_SetParts, CALL, (In, Params, Filename), (Ret, UInt32, RetVal)),
	md_member_x(GuiControlType, SetText, SB_SetText, CALL, (In, String, NewText), (In_Opt, UInt32, PartNumber), (In_Opt, UInt32, Style))
};

#undef FUN1
#undef FUNn

Object *GuiControlType::sPrototype;
Object *GuiControlType::sPrototypeList;
Object *GuiControlType::sPrototypes[GUI_CONTROL_TYPE_COUNT];

void GuiControlType::DefineControlClasses()
{
	auto gui_class = (Object *)g_script.FindGlobalVar(_T("Gui"), 3)->Object();

	sPrototype = CreatePrototype(_T("Gui.Control"), Object::sPrototype, sMembers, _countof(sMembers));
	sPrototypeList = CreatePrototype(_T("Gui.List"), sPrototype, sMembersList, _countof(sMembersList));
	auto ctrl_class = CreateClass(sPrototype);
	auto list_class = CreateClass(sPrototypeList);
	ctrl_class->SetBase(Object::sClass);
	list_class->SetBase(ctrl_class);
	gui_class->DefineClass(_T("Control"), ctrl_class);
	gui_class->DefineClass(_T("List"), list_class);

	for (int i = GUI_CONTROL_INVALID + 1; i < GUI_CONTROL_TYPE_COUNT; ++i)
	{
		if (i == GUI_CONTROL_TAB2 || i == GUI_CONTROL_TAB3)
			continue;

		// Determine base prototype and control-type-specific members.
		Object *base_proto = sPrototype, *base_class = ctrl_class;
		ObjectMemberListType more_items;
		int how_many = 0;
		switch (i)
		{
		case GUI_CONTROL_TAB: more_items = sMembersTab; how_many = _countof(sMembersTab); // Fall through:
		case GUI_CONTROL_DROPDOWNLIST:
		case GUI_CONTROL_COMBOBOX:
		case GUI_CONTROL_LISTBOX: base_proto = sPrototypeList; base_class = list_class; break;
		case GUI_CONTROL_DATETIME: more_items = sMembersDate; how_many = _countof(sMembersDate); break;
		case GUI_CONTROL_LISTVIEW: more_items = sMembersLV; how_many = _countof(sMembersLV); break;
		case GUI_CONTROL_TREEVIEW: more_items = sMembersTV; how_many = _countof(sMembersTV); break;
		case GUI_CONTROL_STATUSBAR: more_items = sMembersSB; how_many = _countof(sMembersSB); break;
		}
		TCHAR buf[32];
		_sntprintf(buf, 32, _T("Gui.%s"), sTypeNames[i]);
		sPrototypes[i] = CreatePrototype(buf, base_proto, more_items, how_many);
		auto cls = CreateClass(sPrototypes[i]);
		cls->SetBase(base_class);
		gui_class->DefineClass(sTypeNames[i], cls);
	}
}

Object *GuiControlType::GetPrototype(GuiControls aType)
{
	ASSERT(aType <= _countof(sPrototypes));
	if (aType == GUI_CONTROL_TAB2 || aType == GUI_CONTROL_TAB3)
		aType = GUI_CONTROL_TAB; // Just make them all Gui.Tab.
	return sPrototypes[aType];
}

// ---------------------------------------------------------------------------
// Gui and Gui.Control object surface.
// ---------------------------------------------------------------------------

ResultType GuiType::GetEnumItem(UINT &aIndex, Var *aOutputVar1, Var *aOutputVar2, int aVarCount)
{
    if (aIndex >= mControlCount)
        return CONDITION_FALSE;
    GuiControlType *ctrl = mControl[aIndex];
    if (aVarCount == 1)
    {
        aOutputVar2 = aOutputVar1;
        aOutputVar1 = nullptr;
    }
    if (aOutputVar1) aOutputVar1->AssignHWND(ctrl->hwnd);
    if (aOutputVar2) aOutputVar2->Assign(ctrl);
    return CONDITION_TRUE;
}

FResult GuiType::__Enum(optl<int> aVarCount, IObject *&aRetVal)
{
    GUI_MUST_HAVE_HWND;
    aRetVal = new IndexEnumerator(this, aVarCount.value_or(0),
        static_cast<IndexEnumerator::Callback>(&GuiType::GetEnumItem));
    return OK;
}

FResult GuiType::Add(StrArg aCtrlType, optl<StrArg> aOptions, ExprTokenType *aContent, IObject *&aRetVal)
{
    GuiControls type = GuiControlType::ConvertTypeName(aCtrlType);
    if (type == GUI_CONTROL_INVALID)
        return FValueError(_T("Invalid control type."), aCtrlType);
    return AddControl(aOptions, aContent, aRetVal, type);
}

FResult GuiType::AddControl(optl<StrArg> aOptions, ExprTokenType *aContent, IObject *&aRetVal, GuiControls type)
{
    LPCTSTR options = aOptions.value_or_empty();
    TCHAR text_buf[MAX_NUMBER_SIZE];
    LPTSTR text = const_cast<LPTSTR>(_T(""));
    Array *items = nullptr;
    if (aContent)
    {
        IObject *obj = TokenToObject(*aContent);
        if (GuiControlType::TypeHasAttrib(type, GuiControlType::TYPE_HAS_ITEMS))
        {
            items = dynamic_cast<Array *>(obj);
            if (!items) return FTypeError(_T("Array"), *aContent);
        }
        else
        {
            if (obj) return FTypeError(_T("String"), *aContent);
            text = TokenToString(*aContent, text_buf);
        }
    }
    GUI_MUST_HAVE_HWND;
    GuiControlType *control = nullptr;
    ResultType result = AddControl(type, options, text, control, items);
    if (control)
    {
        control->AddRef();
        aRetVal = control;
    }
    return result ? OK : FR_FAIL;
}

FResult GuiType::__New(optl<StrArg> aOptions, optl<StrArg> aTitle, optl<IObject *> aEventObj)
{
    if (mHwnd || mDisposed)
        return FError(ERR_INVALID_USAGE);

    bool set_last_found = false;
    ToggleValueType own_dialogs = TOGGLE_INVALID;
    if (!ParseOptions(aOptions.value_or_empty(), set_last_found, own_dialogs))
        return FR_FAIL;

    mControl = static_cast<GuiControlType **>(malloc(GUI_CONTROL_BLOCK_SIZE * sizeof(GuiControlType *)));
    if (!mControl) return FR_E_OUTOFMEM;
    mControlCapacity = GUI_CONTROL_BLOCK_SIZE;

    if (aEventObj.has_value())
    {
        mEventSink = aEventObj.value();
        if (mEventSink != this) mEventSink->AddRef();
    }

    LPCTSTR title = aTitle.has_value() ? aTitle.value() : g_script.DefaultDialogTitle();
    FResult fr = Create(title);
    if (fr != OK) return fr;
    if (set_last_found) g->hWndLastUsed = mHwnd;
    SetOwnDialogs(own_dialogs);
    AddGuiToList(this);
    return OK;
}

FResult GuiType::get_Hwnd(UINT &aRetVal)
{
    GUI_MUST_HAVE_HWND;
    aRetVal = static_cast<UINT>(reinterpret_cast<UINT_PTR>(mHwnd));
    return OK;
}

FResult GuiType::get_Title(StrRet &aRetVal)
{
    GUI_MUST_HAVE_HWND;
    GuiPeer *p = peer(this);
    if (!p) return GuiNoWindowError();
    const char *title = gtk_window_get_title(GTK_WINDOW(p->window));
    auto t = from_utf8(title ? title : "");
    return aRetVal.Copy(t.c_str()) ? OK : FR_E_OUTOFMEM;
}

FResult GuiType::set_Title(StrArg aValue)
{
    GUI_MUST_HAVE_HWND;
    GuiPeer *p = peer(this);
    gtk_window_set_title(GTK_WINDOW(p->window), to_utf8(aValue).c_str());
    pump_events();
    return OK;
}

FResult GuiType::get___Item(ExprTokenType &aIndex, IObject *&aRetVal)
{
    GUI_MUST_HAVE_HWND;
    GuiControlType *ctrl = nullptr;
    if (TokenIsPureNumeric(aIndex) == SYM_INTEGER)
    {
        GtkWidget *widget = to_widget(reinterpret_cast<HWND>(static_cast<UINT_PTR>(TokenToInt64(aIndex))));
        auto found = s_widget_to_control.find(widget);
        if (found != s_widget_to_control.end()) ctrl = found->second;
    }
    else
    {
        TCHAR buf[MAX_NUMBER_SIZE];
        GuiIndexType i = FindControl(TokenToString(aIndex, buf));
        if (i < mControlCount) ctrl = mControl[i];
    }
    if (!ctrl)
        return FError(_T("The specified control does not exist."), nullptr, ErrorPrototype::UnsetItem);
    ctrl->AddRef();
    aRetVal = ctrl;
    return OK;
}

FResult GuiType::get_FocusedCtrl(IObject *&aRetVal)
{
    GUI_MUST_HAVE_HWND;
    GuiPeer *gpeer = peer(this);
    GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(gpeer->window));
    aRetVal = nullptr;
    while (focus)
    {
        auto it = s_widget_to_control.find(focus);
        if (it != s_widget_to_control.end())
        {
            aRetVal = it->second;
            aRetVal->AddRef();
            break;
        }
        focus = gtk_widget_get_parent(focus);
    }
    return OK;
}

FResult GuiType::get_Margin(int &aRetVal, int &aMargin)
{
    GuiPeer *p = peer(this);
    if (aMargin == COORD_UNSPECIFIED)
        SetDefaultMargins();
    if (p)
    {
        if (&aMargin == &mMarginX) aMargin = p->margin_x;
        else if (&aMargin == &mMarginY) aMargin = p->margin_y;
    }
    aRetVal = Unscale(aMargin);
    return OK;
}

FResult GuiType::get_BackColor(ResultToken &aResultToken)
{
    GUI_MUST_HAVE_HWND;
    GuiPeer *p = peer(this);
    if (!p || !p->has_background) return OK;
    unsigned r = static_cast<unsigned>(std::lround(p->background.red * 255));
    unsigned g = static_cast<unsigned>(std::lround(p->background.green * 255));
    unsigned b = static_cast<unsigned>(std::lround(p->background.blue * 255));
    _sntprintf(_f_retval_buf, _f_retval_buf_size, _T("%02X%02X%02X"), r, g, b);
    aResultToken.SetValue(_f_retval_buf);
    return OK;
}

FResult GuiType::set_BackColor(ExprTokenType &aValue)
{
    GUI_MUST_HAVE_HWND;
    if (TokenToObject(aValue)) return FValueError(ERR_INVALID_VALUE);
    TCHAR buf[MAX_NUMBER_SIZE];
    GuiPeer *p = peer(this);
    GdkRGBA color;
    if (!parse_color_text(TokenToString(aValue, buf), color))
        return FValueError(ERR_INVALID_VALUE);
    p->background = color;
    p->has_background = true;
    char *s = gdk_rgba_to_string(&color);
    // Paint the whole window (not just the fixed child): a provider attached
    // to a plain container does not reliably render its own background.
    apply_css(p->window, p->css, std::string("*{background-color:") + (s ? s : "white") + ";}");
    g_free(s);
    gtk_widget_queue_draw(p->window);
    pump_events();
    return OK;
}

FResult GuiType::get_Name(StrRet &aRetVal)
{
    aRetVal.SetTemp(mName);
    return OK;
}

FResult GuiType::set_Name(StrArg aName)
{
    LPTSTR new_name = nullptr;
    if (*aName)
    {
        new_name = _tcsdup(aName);
        if (!new_name) return FR_E_OUTOFMEM;
    }
    free(mName);
    mName = new_name;
    return OK;
}

void GuiType::MethodGetPos(int *aX, int *aY, int *aWidth, int *aHeight, RECT &aPos, HWND)
{
    if (aX) *aX = aPos.left;
    if (aY) *aY = aPos.top;
    if (aWidth) *aWidth = Unscale(aPos.right - aPos.left);
    if (aHeight) *aHeight = Unscale(aPos.bottom - aPos.top);
}

FResult GuiType::GetPos(int *aX, int *aY, int *aWidth, int *aHeight)
{
    GUI_MUST_HAVE_HWND;
    GuiPeer *p = peer(this);
    int x = 0, y = 0, w = 0, h = 0;
    gtk_window_get_position(GTK_WINDOW(p->window), &x, &y);
    gtk_window_get_size(GTK_WINDOW(p->window), &w, &h);
    if (aX) *aX = x; if (aY) *aY = y;
    if (aWidth) *aWidth = Unscale(w); if (aHeight) *aHeight = Unscale(h);
    return OK;
}

FResult GuiType::GetClientPos(int *aX, int *aY, int *aWidth, int *aHeight)
{
    GUI_MUST_HAVE_HWND;
    GuiPeer *p = peer(this);
    GtkAllocation a{};
    gtk_widget_get_allocation(p->fixed, &a);
    int ox = 0, oy = 0;
    gtk_window_get_position(GTK_WINDOW(p->window), &ox, &oy);
    if (aX) *aX = ox; if (aY) *aY = oy;
    if (aWidth) *aWidth = Unscale(a.width); if (aHeight) *aHeight = Unscale(a.height);
    return OK;
}

FResult GuiType::Move(optl<int> aX, optl<int> aY, optl<int> aWidth, optl<int> aHeight)
{
    GUI_MUST_HAVE_HWND;
    GuiPeer *p = peer(this);
    int x, y, w, h;
    gtk_window_get_position(GTK_WINDOW(p->window), &x, &y);
    gtk_window_get_size(GTK_WINDOW(p->window), &w, &h);
    if (aX.has_value()) x = Scale(aX.value());
    if (aY.has_value()) y = Scale(aY.value());
    if (aWidth.has_value()) w = Scale(aWidth.value());
    if (aHeight.has_value()) h = Scale(aHeight.value());
    p->suppress_window_events = true;
    gtk_window_move(GTK_WINDOW(p->window), x, y);
    gtk_window_resize(GTK_WINDOW(p->window), std::max(1, w), std::max(1, h));
    p->suppress_window_events = false;
    pump_events();
    return OK;
}

bif_impl void GuiFromHwnd(UINT aHwnd, optl<BOOL> aRecurse, IObject *&aGui)
{
    aGui = nullptr;
    HWND h = (HWND)(UINT_PTR)aHwnd;
    GuiType *found = aRecurse.value_or(FALSE)
        ? GuiType::FindGuiParent(h, true)
        : GuiType::FindGui(h, true);
    if (found) { found->AddRef(); aGui = found; }
}

bif_impl void GuiCtrlFromHwnd(UINT aHwnd, IObject *&aGuiCtrl)
{
    aGuiCtrl = nullptr;
    HWND h = (HWND)(UINT_PTR)aHwnd;
    if (GuiType::FindGuiParent(h, true))
        if (GuiControlType *ctrl = AhkGtkControlFromHwnd((UINT_PTR)h))
        {
            ctrl->AddRef();
            aGuiCtrl = ctrl;
        }
}

FResult GuiControlType::Focus()
{
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    gtk_widget_grab_focus(effective_widget(*p));
    pump_events();
    return OK;
}

FResult GuiControlType::GetPos(int *aX, int *aY, int *aWidth, int *aHeight)
{
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    if (!p) return ControlDestroyedError();
    if (aX) *aX = gui->Unscale(p->x);
    if (aY) *aY = gui->Unscale(p->y);
    if (aWidth) *aWidth = gui->Unscale(p->width);
    if (aHeight) *aHeight = gui->Unscale(p->height);
    return OK;
}

FResult GuiControlType::Move(optl<int> aX, optl<int> aY, optl<int> aWidth, optl<int> aHeight)
{
    CTRL_THROW_IF_DESTROYED;
    return gui->ControlMove(*this, aX.value_or(COORD_UNSPECIFIED), aY.value_or(COORD_UNSPECIFIED),
        aWidth.value_or(COORD_UNSPECIFIED), aHeight.value_or(COORD_UNSPECIFIED));
}

FResult GuiControlType::OnCommand(int aNotifyCode, ExprTokenType &aCallback, optl<int> aAddRemove)
{
    CTRL_THROW_IF_DESTROYED;
    return gui->OnEvent(this, aNotifyCode, GUI_EVENTKIND_COMMAND, aCallback, aAddRemove);
}

FResult GuiControlType::OnEvent(StrArg aEventName, ExprTokenType &aCallback, optl<int> aAddRemove)
{
    CTRL_THROW_IF_DESTROYED;
    UINT code = GuiType::ConvertEvent(aEventName);
    if (!code || !SupportsEvent(static_cast<GuiEventType>(code))) return FR_E_ARG(0);
    return gui->OnEvent(this, code, GUI_EVENTKIND_EVENT, aCallback, aAddRemove);
}

FResult GuiControlType::OnNotify(int aNotifyCode, ExprTokenType &aCallback, optl<int> aAddRemove)
{
    CTRL_THROW_IF_DESTROYED;
    return gui->OnEvent(this, aNotifyCode, GUI_EVENTKIND_NOTIFY, aCallback, aAddRemove);
}

FResult GuiControlType::Opt(StrArg aOptions)
{
    CTRL_THROW_IF_DESTROYED;
    GuiControlOptionsType go;
    gui->ControlInitOptions(go, *this);
    return gui->ControlParseOptions(aOptions, go, *this, static_cast<GuiIndexType>(peer(this)->index)) ? OK : FR_FAIL;
}

FResult GuiControlType::Redraw()
{
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    gtk_widget_queue_draw(p->outer ? p->outer : p->widget);
    pump_events();
    return OK;
}

FResult GuiControlType::SetFont(optl<StrArg> aOptions, optl<StrArg> aFontName)
{
    CTRL_THROW_IF_DESTROYED;
    return gui->ControlSetFont(*this, aOptions.value_or_empty(), aFontName.value_or_empty()) ? OK : FR_FAIL;
}

FResult GuiControlType::get_ClassNN(StrRet &aRetVal)
{
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    std::basic_string<TCHAR> result = GetTypeName();
    TCHAR n[32];
    _sntprintf(n, _countof(n), _T("%d"), p ? p->index + 1 : 0);
    result += n;
    return aRetVal.Copy(result.c_str()) ? OK : FR_E_OUTOFMEM;
}

FResult GuiControlType::get_Enabled(BOOL &aRetVal)
{
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    aRetVal = gtk_widget_get_sensitive(p->outer ? p->outer : p->widget);
    return OK;
}

FResult GuiControlType::set_Enabled(BOOL aValue)
{
    CTRL_THROW_IF_DESTROYED;
    gui->ControlSetEnabled(*this, aValue != FALSE);
    return OK;
}

FResult GuiControlType::get_Focused(BOOL &aRetVal)
{
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(p->gui->window));
    GtkWidget *target = effective_widget(*p);
    aRetVal = focus == target || (focus && gtk_widget_is_ancestor(focus, target));
    return OK;
}

FResult GuiControlType::get_Gui(IObject *&aRetVal)
{
    CTRL_THROW_IF_DESTROYED;
    gui->AddRef(); aRetVal = gui; return OK;
}

FResult GuiControlType::get_Hwnd(UINT &aRetVal)
{
    CTRL_THROW_IF_DESTROYED;
    aRetVal = static_cast<UINT>(reinterpret_cast<UINT_PTR>(hwnd));
    return OK;
}

FResult GuiControlType::get_Name(StrRet &aRetVal)
{
    aRetVal.SetTemp(name); return OK;
}

FResult GuiControlType::set_Name(StrArg aValue)
{
    CTRL_THROW_IF_DESTROYED;
    return gui->ControlSetName(*this, aValue);
}

FResult GuiControlType::get_Text(ResultToken &aResultToken)
{
    CTRL_THROW_IF_DESTROYED;
    gui->ControlGetContents(aResultToken, *this, GuiType::Text_Mode);
    return aResultToken.Exited() ? FR_FAIL : OK;
}

FResult GuiControlType::set_Text(ExprTokenType &aValue)
{
    CTRL_THROW_IF_DESTROYED;
    return gui->ControlSetContents(*this, aValue, true);
}

FResult GuiControlType::get_Type(StrRet &aRetVal)
{
    aRetVal.SetStatic(GetTypeName()); return OK;
}

FResult GuiControlType::get_Value(ResultToken &aResultToken)
{
    CTRL_THROW_IF_DESTROYED;
    gui->ControlGetContents(aResultToken, *this, GuiType::Value_Mode);
    return aResultToken.Exited() ? FR_FAIL : OK;
}

FResult GuiControlType::set_Value(ExprTokenType &aValue)
{
    CTRL_THROW_IF_DESTROYED;
    return gui->ControlSetContents(*this, aValue, false);
}

FResult GuiControlType::get_Visible(BOOL &aRetVal)
{
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    aRetVal = p && !p->explicit_hidden;
    return OK;
}

FResult GuiControlType::set_Visible(BOOL aValue)
{
    CTRL_THROW_IF_DESTROYED;
    gui->ControlSetVisible(*this, aValue != FALSE);
    return OK;
}

FResult GuiControlType::Tab_UseTab(ExprTokenType *aTab, optl<BOOL> aExact)
{
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    if (!p || !p->tab_container) return FError(ERR_GUI_NOT_FOR_THIS_TYPE);
    int index = -1;
    if (aTab)
    {
        if (TokenIsPureNumeric(*aTab) == SYM_INTEGER)
            index = static_cast<int>(TokenToInt64(*aTab)) - 1;
        else
        {
            TCHAR buf[MAX_NUMBER_SIZE];
            std::string wanted = to_utf8(TokenToString(*aTab, buf));
            bool exact = aExact.value_or(FALSE) != FALSE;
            for (size_t i = 0; i < p->items.size(); ++i)
            {
                if ((exact && p->items[i] == wanted) || (!exact && p->items[i].compare(0, wanted.size(), wanted) == 0))
                { index = static_cast<int>(i); break; }
            }
        }
    }
    if (index < -1 || index >= static_cast<int>(p->tab_pages.size())) return FR_E_ARG(0);
    p->gui->current_tab = index < 0 ? nullptr : p;
    p->gui->current_tab_page = index;
    if (index >= 0) gtk_notebook_set_current_page(GTK_NOTEBOOK(p->widget), index);
    return OK;
}

FResult GuiControlType::List_Add(ExprTokenType &aItems)
{
    CTRL_THROW_IF_DESTROYED;
    Array *array = TokenToArray(aItems);
    if (!array) return FParamError(0, &aItems, _T("Array"));
    gui->ControlAddItems(*this, array);
    return OK;
}

FResult GuiControlType::List_Delete(optl<int> aIndex)
{
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    if (!p) return ControlDestroyedError();
    int index = aIndex.has_value() ? aIndex.value() - 1 : -1;
    if (aIndex.has_value() && index < 0) return FR_E_ARG(0);
    if (type == GUI_CONTROL_TAB)
    {
        if (index < 0)
        {
            while (gtk_notebook_get_n_pages(GTK_NOTEBOOK(p->widget)) > 0) gtk_notebook_remove_page(GTK_NOTEBOOK(p->widget), 0);
            p->items.clear(); p->tab_pages.clear();
        }
        else if (index < static_cast<int>(p->items.size()))
        {
            gtk_notebook_remove_page(GTK_NOTEBOOK(p->widget), index);
            p->items.erase(p->items.begin() + index);
            p->tab_pages.erase(p->tab_pages.begin() + index);
        }
    }
    else if (type == GUI_CONTROL_DROPDOWNLIST || type == GUI_CONTROL_COMBOBOX)
    {
        if (index < 0) { gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(p->widget)); p->items.clear(); }
        else if (index < static_cast<int>(p->items.size())) { gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(p->widget), index); p->items.erase(p->items.begin() + index); }
    }
    else if ((type == GUI_CONTROL_LISTBOX || type == GUI_CONTROL_LISTVIEW) && p->list_store)
    {
        if (index < 0) { gtk_list_store_clear(p->list_store); p->items.clear(); }
        else
        {
            GtkTreeIter iter;
            if (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(p->list_store), &iter, nullptr, index))
                gtk_list_store_remove(p->list_store, &iter);
            if (index < static_cast<int>(p->items.size())) p->items.erase(p->items.begin() + index);
        }
    }
    return OK;
}

GuiType *GuiType::FindGui(HWND aHwnd, bool)
{
    GtkWidget *w = to_widget(aHwnd);
    std::lock_guard<std::recursive_mutex> lock(s_mutex);
    auto it = s_widget_to_gui.find(w);
    return it == s_widget_to_gui.end() ? nullptr : it->second;
}

GuiType *GuiType::FindGuiParent(HWND aHwnd, bool)
{
    GtkWidget *w = to_widget(aHwnd);
    while (w)
    {
        if (GuiType *gui = FindGui(to_hwnd(w), false)) return gui;
        auto ci = s_widget_to_control.find(w);
        if (ci != s_widget_to_control.end()) return ci->second->gui;
        w = gtk_widget_get_parent(w);
    }
    return nullptr;
}


namespace ahk_gtk
{

static std::string menu_mnemonic(LPCTSTR text)
{
    std::string in = to_utf8(text ? text : _T(""));
    std::string out;
    for (size_t i = 0; i < in.size(); ++i)
    {
        if (in[i] == '&')
        {
            if (i + 1 < in.size() && in[i + 1] == '&') { out += '&'; ++i; }
            else out += '_';
        }
        else if (in[i] == '_') out += "__";
        else out += in[i];
    }
    return out;
}

static void signal_menu_activate(GtkWidget *, gpointer data)
{
    MenuBinding *binding = static_cast<MenuBinding *>(data);
    if (!binding) return;
    // Same rule as queue_gui_event(): never route through the no-op
    // PostMessage() stub; enqueue on the native queue drained by GtkPump().
    if (AhkGtkQueueMenuItem) AhkGtkQueueMenuItem(binding->gui, binding->menu, binding->id);
}

static void append_menu_items(GuiPeer &gp, GtkWidget *shell, UserMenu &menu)
{
    for (UserMenuItem *item = menu.mFirstMenuItem; item; item = item->mNextMenuItem)
    {
        GtkWidget *widget;
        if (!item->mName || !*item->mName)
            widget = gtk_separator_menu_item_new();
        else
        {
            std::string label = menu_mnemonic(item->mName);
            widget = gtk_menu_item_new_with_mnemonic(label.c_str());
            if (item->mSubmenu)
            {
                GtkWidget *submenu = gtk_menu_new();
                append_menu_items(gp, submenu, *item->mSubmenu);
                gtk_menu_item_set_submenu(GTK_MENU_ITEM(widget), submenu);
            }
            else
            {
                auto binding = std::make_unique<MenuBinding>();
                binding->gui = gp.owner;
                binding->menu = &menu;
                binding->id = item->mMenuID;
                g_signal_connect(widget, "activate", G_CALLBACK(signal_menu_activate), binding.get());
                gp.menu_bindings.push_back(std::move(binding));
            }
#ifdef MF_DISABLED
            gtk_widget_set_sensitive(widget, (item->mMenuState & (MF_DISABLED | MF_GRAYED)) ? FALSE : TRUE);
#endif
        }
        gtk_menu_shell_append(GTK_MENU_SHELL(shell), widget);
    }
}

} // namespace ahk_gtk

FResult GuiType::get_MenuBar(ResultToken &aResultToken)
{
    GUI_MUST_HAVE_HWND;
    if (mMenu) { mMenu->AddRef(); aResultToken.Return(mMenu); }
    return OK;
}

FResult GuiType::set_MenuBar(ExprTokenType &aParam)
{
    GUI_MUST_HAVE_HWND;
    UserMenu *menu = nullptr;
    if (!TokenIsEmptyString(aParam))
    {
        menu = dynamic_cast<UserMenu *>(TokenToObject(aParam));
        if (!menu || menu->mMenuType != MENU_TYPE_BAR) return FTypeError(_T("MenuBar"), aParam);
        menu->AddRef();
    }
    if (mMenu) mMenu->Release();
    mMenu = menu;
    GuiPeer *p = peer(this);
    if (p->menu_bar) gtk_widget_destroy(p->menu_bar);
    p->menu_bar = nullptr;
    p->menu_bindings.clear();
    if (menu)
    {
        p->menu_bar = gtk_menu_bar_new();
        append_menu_items(*p, p->menu_bar, *menu);
        gtk_box_pack_start(GTK_BOX(p->root), p->menu_bar, FALSE, FALSE, 0);
        gtk_box_reorder_child(GTK_BOX(p->root), p->menu_bar, 0);
        gtk_widget_show_all(p->menu_bar);
    }
    pump_events();
    return OK;
}


// ---------------------------------------------------------------------------
// GTK-backed state mutation, content conversion and lifetime management.
// ---------------------------------------------------------------------------

namespace ahk_gtk
{

static std::vector<int> selected_rows(ControlPeer &p)
{
    std::vector<int> out;
    if (!p.selection) return out;
    GList *paths = gtk_tree_selection_get_selected_rows(p.selection, nullptr);
    for (GList *n = paths; n; n = n->next)
    {
        GtkTreePath *path = static_cast<GtkTreePath *>(n->data);
        int *indices = gtk_tree_path_get_indices(path);
        if (indices) out.push_back(indices[0]);
        gtk_tree_path_free(path);
    }
    g_list_free(paths);
    std::sort(out.begin(), out.end());
    return out;
}

static void select_row(ControlPeer &p, int index, bool clear_first = true)
{
    if (!p.selection) return;
    p.suppress_events = true;
    if (clear_first) gtk_tree_selection_unselect_all(p.selection);
    if (index >= 0)
    {
        GtkTreePath *path = gtk_tree_path_new_from_indices(index, -1);
        gtk_tree_selection_select_path(p.selection, path);
        if (GTK_IS_TREE_VIEW(p.content ? p.content : p.widget))
        {
            gtk_tree_view_set_cursor(GTK_TREE_VIEW(p.content ? p.content : p.widget), path, nullptr, FALSE);
            gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(p.content ? p.content : p.widget), path, nullptr, TRUE, 0.5f, 0.0f);
        }
        gtk_tree_path_free(path);
    }
    p.selected = index;
    p.suppress_events = false;
}

static int find_item(ControlPeer &p, const std::string &text, bool exact)
{
    auto fold = [](const std::string &s) {
        std::string r = s;
        std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return r;
    };
    std::string needle = fold(text);
    for (size_t i = 0; i < p.items.size(); ++i)
    {
        std::string hay = fold(p.items[i]);
        if ((exact && hay == needle) || (!exact && hay.compare(0, needle.size(), needle) == 0))
            return static_cast<int>(i);
    }
    return -1;
}

static std::string calendar_value(ControlPeer &p)
{
    if (!GTK_IS_CALENDAR(p.widget)) return {};
    guint year, month, day;
    gtk_calendar_get_date(GTK_CALENDAR(p.widget), &year, &month, &day);
    char buf[16];
    g_snprintf(buf, sizeof(buf), "%04u%02u%02u", year, month + 1, day);
    return buf;
}

static bool set_calendar_value(ControlPeer &p, LPCTSTR value)
{
    if (!GTK_IS_CALENDAR(p.widget) || !value || _tcslen(value) < 8) return false;
    TCHAR ybuf[5] = {}, mbuf[3] = {}, dbuf[3] = {};
    tmemcpy(ybuf, value, 4); tmemcpy(mbuf, value + 4, 2); tmemcpy(dbuf, value + 6, 2);
    int y = _ttoi(ybuf), m = _ttoi(mbuf), d = _ttoi(dbuf);
    if (y < 1 || m < 1 || m > 12 || d < 1 || d > 31) return false;
    p.suppress_events = true;
    gtk_calendar_select_month(GTK_CALENDAR(p.widget), m - 1, y);
    gtk_calendar_select_day(GTK_CALENDAR(p.widget), d);
    p.suppress_events = false;
    return true;
}

static void apply_gui_options(GuiPeer &p)
{
    if (!p.window) return;
    gtk_window_set_decorated(GTK_WINDOW(p.window), p.options.no_caption ? FALSE : TRUE);
    gtk_window_set_resizable(GTK_WINDOW(p.window), p.options.resize ? TRUE : FALSE);
    gtk_window_set_keep_above(GTK_WINDOW(p.window), p.options.always_on_top ? TRUE : FALSE);
    gtk_widget_set_sensitive(p.window, p.options.disabled ? FALSE : TRUE);
    gtk_window_set_type_hint(GTK_WINDOW(p.window), p.options.tool_window
        ? GDK_WINDOW_TYPE_HINT_UTILITY : GDK_WINDOW_TYPE_HINT_NORMAL);
    GdkGeometry geometry{};
    GdkWindowHints hints = static_cast<GdkWindowHints>(0);
    if (p.options.min_width >= 0) { geometry.min_width = p.options.min_width; hints = static_cast<GdkWindowHints>(hints | GDK_HINT_MIN_SIZE); }
    if (p.options.min_height >= 0) { geometry.min_height = p.options.min_height; hints = static_cast<GdkWindowHints>(hints | GDK_HINT_MIN_SIZE); }
    if (p.options.max_width >= 0) { geometry.max_width = p.options.max_width; hints = static_cast<GdkWindowHints>(hints | GDK_HINT_MAX_SIZE); }
    if (p.options.max_height >= 0) { geometry.max_height = p.options.max_height; hints = static_cast<GdkWindowHints>(hints | GDK_HINT_MAX_SIZE); }
    if (hints) gtk_window_set_geometry_hints(GTK_WINDOW(p.window), p.window, &geometry, hints);
}

static void queue_gui_event(GuiType *gui, GuiIndexType control, USHORT event, UINT_PTR info)
{
    // Linux has no Win32 message queue: POST_AHK_GUI_ACTION would fall through
    // to the no-op PostMessage() stub and silently drop every GUI event.
    // Always enqueue on the native GTK queue drained by GtkPump() on the
    // interpreter thread.
    if (AhkGtkQueueGuiEvent) AhkGtkQueueGuiEvent(gui, control, event, info);
}

} // namespace ahk_gtk

FResult GuiType::ControlSetName(GuiControlType &aControl, LPCTSTR aName)
{
    LPTSTR new_name = nullptr;
    if (aName && *aName)
    {
        for (GuiIndexType i = 0; i < mControlCount; ++i)
            if (mControl[i] != &aControl && mControl[i]->name && !_tcsicmp(mControl[i]->name, aName))
                return FError(_T("A control with this name already exists."), aName);
        new_name = _tcsdup(aName);
        if (!new_name) return FR_E_OUTOFMEM;
    }
    free(aControl.name);
    aControl.name = new_name;
    return OK;
}

void GuiType::ControlSetEnabled(GuiControlType &aControl, bool aEnabled)
{
    ControlPeer *p = peer(&aControl);
    if (!p) return;
    p->explicit_disabled = !aEnabled;
    if (aEnabled) aControl.attrib &= ~GUI_CONTROL_ATTRIB_EXPLICITLY_DISABLED;
    else aControl.attrib |= GUI_CONTROL_ATTRIB_EXPLICITLY_DISABLED;
    gtk_widget_set_sensitive(p->outer ? p->outer : p->widget, aEnabled ? TRUE : FALSE);
}

void GuiType::ControlSetVisible(GuiControlType &aControl, bool aVisible)
{
    ControlPeer *p = peer(&aControl);
    if (!p) return;
    p->explicit_hidden = !aVisible;
    if (aVisible)
    {
        aControl.attrib &= ~GUI_CONTROL_ATTRIB_EXPLICITLY_HIDDEN;
        gtk_widget_show_all(p->outer ? p->outer : p->widget);
    }
    else
    {
        aControl.attrib |= GUI_CONTROL_ATTRIB_EXPLICITLY_HIDDEN;
        gtk_widget_hide(p->outer ? p->outer : p->widget);
    }
}

FResult GuiType::ControlMove(GuiControlType &aControl, int x, int y, int width, int height)
{
    ControlPeer *p = peer(&aControl);
    if (!p) return ControlDestroyedError();
    if (x != COORD_UNSPECIFIED) p->x = x;
    if (y != COORD_UNSPECIFIED) p->y = y;
    if (width != COORD_UNSPECIFIED) p->width = std::max(1, width);
    if (height != COORD_UNSPECIFIED) p->height = std::max(1, height);
    GtkWidget *outer = p->outer ? p->outer : p->widget;
    if (p->parent_fixed && GTK_IS_FIXED(p->parent_fixed))
    {
        int place_x, place_y;
        placement_coordinates(*p, place_x, place_y);
        gtk_fixed_move(GTK_FIXED(p->parent_fixed), outer, place_x, place_y);
    }
    gtk_widget_set_size_request(outer, p->width, p->height);
    p->gui->max_right = std::max(p->gui->max_right, p->x + p->width);
    p->gui->max_bottom = std::max(p->gui->max_bottom, p->y + p->height);
    pump_events();
    return OK;
}

ResultType GuiType::ControlSetFont(GuiControlType &aControl, LPCTSTR aOptions, LPCTSTR aFontName)
{
    ControlPeer *p = peer(&aControl);
    if (!p) return FAIL;
    FontSpec f = parse_font(aOptions, aFontName, p->gui->current_font);
    apply_font(*p, f);
    pump_events();
    return OK;
}

void GuiType::ControlSetTextColor(GuiControlType &aControl, COLORREF aColor)
{
    ControlPeer *p = peer(&aControl);
    if (!p) return;
    FontSpec f = p->gui->current_font;
    f.has_color = true;
    f.color = colorref_to_rgba(aColor);
    apply_font(*p, f);
    if (aControl.UsesUnionColor()) aControl.union_color = aColor;
}

void GuiType::ControlSetMonthCalColor(GuiControlType &aControl, COLORREF aColor, UINT)
{
    ControlSetTextColor(aControl, aColor);
}

ResultType GuiType::ControlChoose(GuiControlType &aControl, ExprTokenType &aParam, BOOL aOneExact)
{
    ControlPeer *p = peer(&aControl);
    if (!p) return FAIL;
    int index = -1;
    if (TokenIsPureNumeric(aParam) == SYM_INTEGER)
        index = static_cast<int>(TokenToInt64(aParam)) - 1;
    else
    {
        if (TokenToObject(aParam)) return FAIL;
        TCHAR buf[MAX_NUMBER_SIZE];
        std::string value = to_utf8(TokenToString(aParam, buf));
        if (!value.empty()) index = find_item(*p, value, aOneExact != FALSE);
    }
    if (index < -1 || index >= static_cast<int>(p->items.size())) return FAIL;
    p->suppress_events = true;
    switch (aControl.type)
    {
    case GUI_CONTROL_DROPDOWNLIST:
    case GUI_CONTROL_COMBOBOX:
        gtk_combo_box_set_active(GTK_COMBO_BOX(p->widget), index);
        break;
    case GUI_CONTROL_LISTBOX:
    case GUI_CONTROL_LISTVIEW:
        select_row(*p, index, aOneExact != FALSE);
        break;
    case GUI_CONTROL_TAB:
        gtk_notebook_set_current_page(GTK_NOTEBOOK(p->widget), index);
        p->selected = index;
        if (p->gui->current_tab == p) p->gui->current_tab_page = index;
        break;
    default:
        p->suppress_events = false;
        return FAIL;
    }
    p->selected = index;
    p->suppress_events = false;
    return OK;
}

FResult GuiControlType::List_Choose(ExprTokenType &aValue)
{
    CTRL_THROW_IF_DESTROYED;
    return gui->ControlChoose(*this, aValue, FALSE) ? OK : FR_FAIL;
}

FResult GuiType::ControlSetPic(GuiControlType &aControl, LPTSTR aContents)
{
    ControlPeer *p = peer(&aControl);
    if (!p || !GTK_IS_IMAGE(p->widget)) return FError(ERR_GUI_NOT_FOR_THIS_TYPE);
    std::string filename = to_utf8(aContents);
    GError *error = nullptr;
    GdkPixbuf *pix = gdk_pixbuf_new_from_file_at_scale(filename.c_str(), p->width, p->height, TRUE, &error);
    if (!pix)
    {
        if (error) g_error_free(error);
        return FValueError(ERR_INVALID_VALUE, aContents);
    }
    gtk_image_set_from_pixbuf(GTK_IMAGE(p->widget), pix);
    g_object_unref(pix);
    return OK;
}

FResult GuiType::ControlSetCheck(GuiControlType &aControl, int checked)
{
    ControlPeer *p = peer(&aControl);
    if (!p || !GTK_IS_TOGGLE_BUTTON(p->widget)) return FError(ERR_GUI_NOT_FOR_THIS_TYPE);
    if (checked != 0 && checked != 1 && !(aControl.type == GUI_CONTROL_CHECKBOX && checked == -1))
        return FValueError(ERR_INVALID_VALUE);
    p->suppress_events = true;
    if (checked == -1 && GTK_IS_CHECK_BUTTON(p->widget))
        gtk_toggle_button_set_inconsistent(GTK_TOGGLE_BUTTON(p->widget), TRUE);
    else
    {
        if (GTK_IS_CHECK_BUTTON(p->widget)) gtk_toggle_button_set_inconsistent(GTK_TOGGLE_BUTTON(p->widget), FALSE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->widget), checked ? TRUE : FALSE);
    }
    p->suppress_events = false;
    return OK;
}

void GuiType::ControlGetCheck(ResultToken &aResultToken, GuiControlType &aControl)
{
    ControlPeer *p = peer(&aControl);
    if (!p || !GTK_IS_TOGGLE_BUTTON(p->widget)) { _o_return(0); }
    if (GTK_IS_CHECK_BUTTON(p->widget) && gtk_toggle_button_get_inconsistent(GTK_TOGGLE_BUTTON(p->widget)))
        _o_return(-1);
    _o_return(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p->widget)) ? 1 : 0);
}

FResult GuiType::ControlSetChoice(GuiControlType &aControl, LPTSTR aContents, bool aIsText)
{
    if (aIsText && aControl.type == GUI_CONTROL_COMBOBOX)
    {
        ControlPeer *p = peer(&aControl);
        if (!p) return FR_FAIL;
        set_widget_text(*p, to_utf8(aContents).c_str());
        return OK;
    }
    ExprTokenType tok{};
    if (aIsText) { tok.symbol = SYM_STRING; tok.marker = aContents; }
    else
    {
        if (!ParseInteger(aContents, tok.value_int64)) return FValueError(ERR_INVALID_VALUE, aContents);
        tok.symbol = SYM_INTEGER;
    }
    return ControlChoose(aControl, tok, TRUE) ? OK : FR_FAIL;
}

FResult GuiType::ControlSetEdit(GuiControlType &aControl, LPTSTR aContents, bool)
{
    ControlPeer *p = peer(&aControl);
    if (!p) return FR_FAIL;
    set_widget_text(*p, to_utf8(aContents).c_str());
    return OK;
}

FResult GuiType::ControlSetDateTime(GuiControlType &aControl, LPTSTR aContents)
{
    ControlPeer *p = peer(&aControl);
    if (!p) return FR_FAIL;
    if (!aContents || !*aContents) return OK;
    return set_calendar_value(*p, aContents) ? OK : FValueError(ERR_INVALID_VALUE, aContents);
}

FResult GuiControlType::DT_SetFormat(optl<StrArg> aFormat)
{
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    if (!p) return FR_FAIL;
    p->options.format = aFormat.has_value() ? aFormat.value() : _T("");
    return OK;
}

FResult GuiType::ControlSetMonthCal(GuiControlType &aControl, LPTSTR aContents)
{
    ControlPeer *p = peer(&aControl);
    if (!p || !aContents || !*aContents) return FValueError(ERR_INVALID_VALUE, aContents);
    return set_calendar_value(*p, aContents) ? OK : FValueError(ERR_INVALID_VALUE, aContents);
}

FResult GuiType::ControlSetHotkey(GuiControlType &aControl, LPTSTR aContents)
{
    ControlPeer *p = peer(&aControl);
    if (!p) return FR_FAIL;
    set_widget_text(*p, to_utf8(aContents).c_str());
    return OK;
}

FResult GuiType::ControlSetUpDown(GuiControlType &aControl, int value)
{
    ControlPeer *p = peer(&aControl);
    if (!p || !GTK_IS_SPIN_BUTTON(p->widget)) return FR_FAIL;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(p->widget), value);
    return OK;
}

FResult GuiType::ControlSetSlider(GuiControlType &aControl, int value)
{
    ControlPeer *p = peer(&aControl);
    if (!p || !GTK_IS_RANGE(p->widget)) return FR_FAIL;
    gtk_range_set_value(GTK_RANGE(p->widget), value);
    return OK;
}

FResult GuiType::ControlSetProgress(GuiControlType &aControl, int value)
{
    ControlPeer *p = peer(&aControl);
    if (!p || !GTK_IS_PROGRESS_BAR(p->widget)) return FR_FAIL;
    value = std::max(p->range_min, std::min(p->range_max, value));
    double f = p->range_max == p->range_min ? 0.0 : (value - p->range_min) / static_cast<double>(p->range_max - p->range_min);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(p->widget), f);
    p->selected = value;
    return OK;
}

FResult GuiType::ControlSetContents(GuiControlType &aControl, ExprTokenType &aValue, bool aIsText)
{
    if (TokenToObject(aValue)) return FR_E_ARG(0);
    TCHAR buf[MAX_NUMBER_SIZE];
    LPTSTR value = TokenToString(aValue, buf);
    switch (aControl.type)
    {
    case GUI_CONTROL_DROPDOWNLIST:
    case GUI_CONTROL_COMBOBOX:
    case GUI_CONTROL_LISTBOX:
    case GUI_CONTROL_TAB: return ControlSetChoice(aControl, value, aIsText);
    case GUI_CONTROL_EDIT: return ControlSetEdit(aControl, value, aIsText);
    case GUI_CONTROL_DATETIME: return aIsText ? FError(ERR_GUI_NOT_FOR_THIS_TYPE) : ControlSetDateTime(aControl, value);
    case GUI_CONTROL_TEXT: aIsText = true; break;
    default: break;
    }
    if (aIsText)
    {
        ControlPeer *p = peer(&aControl);
        if (!p) return FR_FAIL;
        set_widget_text(*p, to_utf8(value).c_str());
        return OK;
    }
    switch (aControl.type)
    {
    case GUI_CONTROL_PIC: return ControlSetPic(aControl, value);
    case GUI_CONTROL_MONTHCAL: return ControlSetMonthCal(aControl, value);
    case GUI_CONTROL_HOTKEY: return ControlSetHotkey(aControl, value);
    case GUI_CONTROL_RADIO:
    case GUI_CONTROL_CHECKBOX:
    case GUI_CONTROL_UPDOWN:
    case GUI_CONTROL_SLIDER:
    case GUI_CONTROL_PROGRESS:
        if (!TokenIsNumeric(aValue)) return FTypeError(_T("Number"), aValue);
        switch (aControl.type)
        {
        case GUI_CONTROL_RADIO:
        case GUI_CONTROL_CHECKBOX: return ControlSetCheck(aControl, static_cast<int>(TokenToInt64(aValue)));
        case GUI_CONTROL_UPDOWN: return ControlSetUpDown(aControl, static_cast<int>(TokenToInt64(aValue)));
        case GUI_CONTROL_SLIDER: return ControlSetSlider(aControl, static_cast<int>(TokenToInt64(aValue)));
        default: return ControlSetProgress(aControl, static_cast<int>(TokenToInt64(aValue)));
        }
    default: return FError(ERR_INVALID_USAGE);
    }
}

void GuiType::ControlGetDDL(ResultToken &aResultToken, GuiControlType &aControl, ValueModeType aMode)
{
    ControlPeer *p = peer(&aControl);
    if (!p) { _o_return_empty; }
    int index = combo_active(*p);
    if (aMode == Value_Mode || (aMode == Submit_Mode && (aControl.attrib & GUI_CONTROL_ATTRIB_ALTSUBMIT)))
        _o_return(index + 1);
    gchar *s = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(p->widget));
    std::string value = s ? s : ""; g_free(s);
    set_result_string(aResultToken, value);
}

void GuiType::ControlGetComboBox(ResultToken &aResultToken, GuiControlType &aControl, ValueModeType aMode)
{
    ControlPeer *p = peer(&aControl);
    if (!p) { _o_return_empty; }
    int index = combo_active(*p);
    if (aMode == Value_Mode || (aMode == Submit_Mode && (aControl.attrib & GUI_CONTROL_ATTRIB_ALTSUBMIT)))
        _o_return(index + 1);
    set_result_string(aResultToken, widget_text(*p));
}

void GuiType::ControlGetListBox(ResultToken &aResultToken, GuiControlType &aControl, ValueModeType aMode)
{
    ControlPeer *p = peer(&aControl);
    if (!p) { _o_return_empty; }
    auto rows = selected_rows(*p);
    bool positions = aMode == Value_Mode || (aMode == Submit_Mode && (aControl.attrib & GUI_CONTROL_ATTRIB_ALTSUBMIT));
    if (p->multiple)
    {
        auto arr = Array::Create();
        if (!arr) { _o_throw_oom; }
        for (int row : rows)
        {
            if (positions) arr->Append(row + 1);
            else if (row >= 0 && row < static_cast<int>(p->items.size()))
            {
                auto t = from_utf8(p->items[row].c_str());
                arr->Append(t.c_str(), t.size());
            }
        }
        _o_return(arr);
    }
    int row = rows.empty() ? -1 : rows.front();
    if (positions) _o_return(row + 1);
    if (row < 0 || row >= static_cast<int>(p->items.size())) { _o_return_empty; }
    set_result_string(aResultToken, p->items[row]);
}

void GuiType::ControlGetEdit(ResultToken &aResultToken, GuiControlType &aControl)
{
    ControlPeer *p = peer(&aControl);
    if (!p) { _o_return_empty; }
    set_result_string(aResultToken, widget_text(*p));
}

void GuiType::ControlGetDateTime(ResultToken &aResultToken, GuiControlType &aControl)
{
    ControlPeer *p = peer(&aControl);
    if (!p) { _o_return_empty; }
    set_result_string(aResultToken, calendar_value(*p));
}

void GuiType::ControlGetMonthCal(ResultToken &aResultToken, GuiControlType &aControl)
{
    ControlGetDateTime(aResultToken, aControl);
}

void GuiType::ControlGetHotkey(ResultToken &aResultToken, GuiControlType &aControl)
{
    ControlPeer *p = peer(&aControl);
    if (!p) { _o_return_empty; }
    set_result_string(aResultToken, widget_text(*p));
}

void GuiType::ControlGetUpDown(ResultToken &aResultToken, GuiControlType &aControl)
{
    ControlPeer *p = peer(&aControl);
    _o_return(p && GTK_IS_SPIN_BUTTON(p->widget) ? static_cast<int>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(p->widget))) : 0);
}

void GuiType::ControlGetSlider(ResultToken &aResultToken, GuiControlType &aControl)
{
    ControlPeer *p = peer(&aControl);
    _o_return(p && GTK_IS_RANGE(p->widget) ? static_cast<int>(std::lround(gtk_range_get_value(GTK_RANGE(p->widget)))) : 0);
}

void GuiType::ControlGetProgress(ResultToken &aResultToken, GuiControlType &aControl)
{
    ControlPeer *p = peer(&aControl);
    _o_return(p ? p->selected : 0);
}

void GuiType::ControlGetTab(ResultToken &aResultToken, GuiControlType &aControl, ValueModeType aMode)
{
    ControlPeer *p = peer(&aControl);
    if (!p) { _o_return_empty; }
    int index = gtk_notebook_get_current_page(GTK_NOTEBOOK(p->widget));
    if (aMode == Value_Mode || (aMode == Submit_Mode && (aControl.attrib & GUI_CONTROL_ATTRIB_ALTSUBMIT)))
        _o_return(index + 1);
    if (index < 0 || index >= static_cast<int>(p->items.size())) { _o_return_empty; }
    set_result_string(aResultToken, p->items[index]);
}

ResultType GuiType::ControlGetWindowText(ResultToken &aResultToken, GuiControlType &aControl)
{
    ControlPeer *p = peer(&aControl);
    if (!p) return FAIL;
    return set_result_string(aResultToken, widget_text(*p)) ? OK : FAIL;
}

void GuiType::ControlRedraw(GuiControlType &aControl, bool)
{
    ControlPeer *p = peer(&aControl);
    if (!p) return;
    gtk_widget_queue_draw(p->outer ? p->outer : p->widget);
    pump_events();
}

void GuiType::ControlGetContents(ResultToken &aResultToken, GuiControlType &aControl, ValueModeType aMode)
{
    ControlPeer *p = peer(&aControl);
    if (!p) { _o_return_empty; }
    switch (aControl.type)
    {
    case GUI_CONTROL_DROPDOWNLIST: return ControlGetDDL(aResultToken, aControl, aMode);
    case GUI_CONTROL_COMBOBOX: return ControlGetComboBox(aResultToken, aControl, aMode);
    case GUI_CONTROL_LISTBOX: return ControlGetListBox(aResultToken, aControl, aMode);
    case GUI_CONTROL_EDIT:
        if (aMode == Text_Mode) { set_result_string(aResultToken, widget_text(*p)); return; }
        return ControlGetEdit(aResultToken, aControl);
    case GUI_CONTROL_DATETIME: return ControlGetDateTime(aResultToken, aControl);
    case GUI_CONTROL_MONTHCAL: return ControlGetMonthCal(aResultToken, aControl);
    case GUI_CONTROL_HOTKEY: return ControlGetHotkey(aResultToken, aControl);
    case GUI_CONTROL_UPDOWN: return ControlGetUpDown(aResultToken, aControl);
    case GUI_CONTROL_SLIDER: return ControlGetSlider(aResultToken, aControl);
    case GUI_CONTROL_PROGRESS: return ControlGetProgress(aResultToken, aControl);
    case GUI_CONTROL_TAB: return ControlGetTab(aResultToken, aControl, aMode);
    case GUI_CONTROL_CHECKBOX:
    case GUI_CONTROL_RADIO: return ControlGetCheck(aResultToken, aControl);
    case GUI_CONTROL_PIC:
        if (aMode == Text_Mode) { _o_return_empty; }
        _o_return(reinterpret_cast<UINT_PTR>(p->widget));
    case GUI_CONTROL_TEXT:
        set_result_string(aResultToken, widget_text(*p)); return;
    case GUI_CONTROL_ACTIVEX:
        if (aControl.union_object) { aControl.union_object->AddRef(); _o_return(aControl.union_object); }
        _o_return_empty;
    default:
        if (aMode == Text_Mode) set_result_string(aResultToken, widget_text(*p));
        else _o_return_empty;
    }
}

// ---------------------------------------------------------------------------
// Native GTK window/control construction and destruction.
// ---------------------------------------------------------------------------

FontType *GuiType::sFont = nullptr;
int GuiType::sFontCount = 0;
HWND GuiType::sTreeWithEditInProgress = nullptr;
LPTSTR GuiType::sEventNames[] = GUI_EVENT_NAMES;

FResult GuiType::Create(LPCTSTR aTitle)
{
    if (mHwnd) return FR_E_FAILED;
    init_gtk();
    if (!s_gtk_ok) return FError(_T("GTK3 could not be initialized. Check DISPLAY/WAYLAND_DISPLAY and the GTK3 runtime."));

    auto gp = std::make_unique<GuiPeer>();
    gp->owner = this;
    {
        std::lock_guard<std::recursive_mutex> lock(s_mutex);
        auto pending = s_pending_gui_options.find(this);
        if (pending != s_pending_gui_options.end())
        {
            gp->options = pending->second;
            s_pending_gui_options.erase(pending);
        }
    }
    gp->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gp->root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gp->fixed = gtk_fixed_new();
    gtk_container_add(GTK_CONTAINER(gp->window), gp->root);
    gtk_box_pack_start(GTK_BOX(gp->root), gp->fixed, TRUE, TRUE, 0);
    gtk_window_set_title(GTK_WINDOW(gp->window), to_utf8(aTitle).c_str());
    gtk_window_set_default_size(GTK_WINDOW(gp->window), 320, 200);
    // Keep the default (opaque) theme background: app_paintable=TRUE without a
    // draw handler leaves the whole window unpainted and shows through as
    // transparent.  Script-controlled BackColor is applied via CSS below.
    gtk_widget_set_app_paintable(gp->window, FALSE);

    g_signal_connect(gp->window, "delete-event", G_CALLBACK(signal_window_delete), gp.get());
    g_signal_connect(gp->window, "key-press-event", G_CALLBACK(signal_window_key), gp.get());
    g_signal_connect(gp->window, "configure-event", G_CALLBACK(signal_window_configure), gp.get());
    g_signal_connect(gp->window, "drag-data-received", G_CALLBACK(signal_drag_data_received), gp.get());

    mHwnd = to_hwnd(gp->window);
    mUsesDPIScaling = true;
    mGuiShowHasNeverBeenDone = true;
    mFirstActivation = true;
    mCurrentTabControlIndex = MAX_TAB_CONTROLS;
    mCurrentTabIndex = 0;
    mMarginX = mMarginY = COORD_UNSPECIFIED;
    mBackgroundColorWin = CLR_DEFAULT;

    {
        std::lock_guard<std::recursive_mutex> lock(s_mutex);
        s_widget_to_gui[gp->window] = this;
        s_guis[this] = std::move(gp);
    }
    apply_gui_options(*peer(this));
    return OK;
}

ResultType GuiType::AddControl(GuiControls aControlType, LPCTSTR aOptions, LPCTSTR aText,
    GuiControlType *&apControl, Array *aObj)
{
    apControl = nullptr;
    if (!mHwnd) return FAIL;
    if (mControlCount >= MAX_CONTROLS_PER_GUI)
        return g_script.RuntimeError(_T("Too many controls."));
    if (mControlCount >= mControlCapacity)
    {
        GuiIndexType capacity = mControlCapacity + GUI_CONTROL_BLOCK_SIZE;
        auto resized = static_cast<GuiControlType **>(realloc(mControl, capacity * sizeof(GuiControlType *)));
        if (!resized) return g_script.RuntimeError(_T("Too many controls."));
        mControl = resized;
        mControlCapacity = capacity;
    }

    bool tab2 = aControlType == GUI_CONTROL_TAB2;
    bool tab3 = aControlType == GUI_CONTROL_TAB3;
    if (tab2 || tab3) aControlType = GUI_CONTROL_TAB;

    auto *control = new GuiControlType(this);
    if (!control) return g_script.RuntimeError(_T("Out of memory."));
    control->type = aControlType;
    control->SetBase(GuiControlType::GetPrototype(aControlType));
    if (tab2) control->attrib |= GUI_CONTROL_ATTRIB_ALTBEHAVIOR;
    if (aControlType == GUI_CONTROL_LISTVIEW)
    {
        control->union_lv_attrib = static_cast<lv_attrib_type *>(calloc(1, sizeof(lv_attrib_type)));
        if (!control->union_lv_attrib) { delete control; return g_script.RuntimeError(_T("Out of memory.")); }
        control->union_lv_attrib->sorted_by_col = -1;
    }

    GuiPeer *g = peer(this);
    auto cp = std::make_unique<ControlPeer>();
    cp->owner = control;
    cp->gui = g;
    cp->index = static_cast<int>(mControlCount);
    cp->options = parse_control_options(aOptions);
    cp->explicit_hidden = cp->options.hidden;
    cp->explicit_disabled = cp->options.disabled;
    cp->range_min = cp->options.range_min;
    cp->range_max = cp->options.range_max;
    cp->inverted = cp->options.inverted;
    if (aControlType != GUI_CONTROL_TAB && g->current_tab)
    {
        cp->tab_host = g->current_tab;
        cp->tab_page = std::max(0, g->current_tab_page);
    }

    if (cp->options.alt_submit) control->attrib |= GUI_CONTROL_ATTRIB_ALTSUBMIT;
    if (cp->options.hidden) control->attrib |= GUI_CONTROL_ATTRIB_EXPLICITLY_HIDDEN;
    if (cp->options.disabled) control->attrib |= GUI_CONTROL_ATTRIB_EXPLICITLY_DISABLED;
    if (cp->options.inverted || cp->options.want_tab) control->attrib |= GUI_CONTROL_ATTRIB_ALTBEHAVIOR;

    if (!cp->options.name.empty())
    {
        FResult r = ControlSetName(*control, cp->options.name.c_str());
        if (r != OK)
        {
            if (aControlType == GUI_CONTROL_LISTVIEW) free(control->union_lv_attrib);
            delete control;
            return FAIL;
        }
    }

    calculate_position(*g, *cp);
    if (!create_widget(*cp, aText, aObj))
    {
        if (aControlType == GUI_CONTROL_LISTVIEW) free(control->union_lv_attrib);
        free(control->name);
        delete control;
        return g_script.RuntimeError(_T("Failed to create GTK control."));
    }

    control->hwnd = to_hwnd(cp->widget);
    if (aControlType == GUI_CONTROL_TAB)
    {
        control->tab_control_index = MAX_TAB_CONTROLS;
        control->tab_index = mTabControlCount;
        mCurrentTabControlIndex = mTabControlCount;
        mCurrentTabIndex = static_cast<TabIndexType>(std::max(0, cp->selected));
        ++mTabControlCount;
    }
    else if (g->current_tab)
    {
        control->tab_control_index = static_cast<TabControlIndexType>(g->current_tab->owner->tab_index);
        control->tab_index = static_cast<TabIndexType>(std::max(0, g->current_tab_page));
    }
    else
        control->tab_control_index = MAX_TAB_CONTROLS;

    ControlPeer *cp_raw = cp.get();
    {
        std::lock_guard<std::recursive_mutex> lock(s_mutex);
        s_widget_to_control[cp->widget] = control;
        s_controls[control] = std::move(cp);
    }

    if (aControlType == GUI_CONTROL_STATUSBAR)
    {
        GtkWidget *outer = cp_raw->outer ? cp_raw->outer : cp_raw->widget;
        gtk_box_pack_end(GTK_BOX(g->root), outer, FALSE, TRUE, 0);
        gtk_widget_set_size_request(outer, cp_raw->width, cp_raw->height);
        if (!cp_raw->explicit_hidden) gtk_widget_show_all(outer);
        mStatusBarHwnd = control->hwnd;
    }
    else
        place_control(*cp_raw);

    g->controls.push_back(cp_raw);
    mControl[mControlCount++] = control;
    apControl = control;
    if (cp_raw->options.default_button && GTK_IS_BUTTON(cp_raw->widget))
    {
        gtk_widget_set_can_default(cp_raw->widget, TRUE);
        gtk_widget_grab_default(cp_raw->widget);
        mDefaultButtonIndex = static_cast<GuiIndexType>(cp_raw->index);
    }
    pump_events();
    return OK;
}

bool GuiType::Delete()
{
    if (mHwnd) Destroy();
    else Dispose();
    if (mRefCount > 1) return false;
    return Object::Delete();
}

FResult GuiType::Destroy()
{
    if (!mHwnd) return OK;
    GuiPeer *p = peer(this);
    if (p) p->destroying = true;
    if (p && p->visible)
    {
        gtk_widget_hide(p->window);
        p->visible = false;
    }
    RemoveGuiFromList(this);
    mHwnd = nullptr;
    Dispose();
    if (p && p->window && GTK_IS_WIDGET(p->window)) gtk_widget_destroy(p->window);
    {
        std::lock_guard<std::recursive_mutex> lock(s_mutex);
        if (p)
        {
            s_widget_to_gui.erase(p->window);
            release_hwnd(p->window);
        }
        s_guis.erase(this);
    }
    if (mVisibleRefCounted) { mVisibleRefCounted = false; Release(); }
    g_script.ExitIfNotPersistent(EXIT_CLOSE);
    return OK;
}

void GuiType::Dispose()
{
    if (mDisposed) return;
    mDisposed = true;
    for (GuiIndexType i = 0; i < mControlCount; ++i)
    {
        if (!mControl[i]) continue;
        mControl[i]->Dispose();
        mControl[i]->Release();
    }
    mControlCount = 0;
    if (mMenu) { mMenu->Release(); mMenu = nullptr; }
    if (mHdrop) { g_strfreev(reinterpret_cast<gchar **>(mHdrop)); mHdrop = nullptr; }
    mEvents.Dispose();
    { std::lock_guard<std::recursive_mutex> lock(s_mutex); s_pending_gui_options.erase(this); }
    if (mEventSink && mEventSink != this) mEventSink->Release();
    mEventSink = nullptr;
    free(mControl); mControl = nullptr; mControlCapacity = 0;
}

void GuiControlType::Dispose()
{
    ControlPeer *p = peer(this);
    if (p)
    {
        GuiPeer *g = p->gui;
        if (g)
        {
            g->controls.erase(std::remove(g->controls.begin(), g->controls.end(), p), g->controls.end());
            if (g->current_tab == p) { g->current_tab = nullptr; g->current_tab_page = 0; }
        }
        destroy_control_peer(*p);
        std::lock_guard<std::recursive_mutex> lock(s_mutex);
        s_controls.erase(this);
    }
    if (type == GUI_CONTROL_LISTVIEW && union_lv_attrib) free(union_lv_attrib);
    else if (type == GUI_CONTROL_ACTIVEX && union_object) union_object->Release();
    events.Dispose();
    free(name); name = nullptr;
    hwnd = nullptr;
    gui = nullptr;
}

void GuiType::DestroyIconsIfUnused(HICON, HICON)
{
    // GTK icon objects are reference-counted by GObject and are not owned here.
}

FResult GuiType::Opt(StrArg aOptions)
{
    GUI_MUST_HAVE_HWND;
    bool set_last = false;
    ToggleValueType own_dialogs = TOGGLE_INVALID;
    return ParseOptions(aOptions, set_last, own_dialogs) ? OK : FR_FAIL;
}

ResultType GuiType::ParseOptions(LPCTSTR aOptions, bool &aSetLastFoundWindow, ToggleValueType &aOwnDialogs)
{
    aSetLastFoundWindow = false;
    aOwnDialogs = TOGGLE_INVALID;
    GuiPeer *p = peer(this);
    if (p)
        parse_gui_options(aOptions, p->options);
    else
    {
        std::lock_guard<std::recursive_mutex> lock(s_mutex);
        parse_gui_options(aOptions, s_pending_gui_options[this]);
    }
    for (auto word : split_words(aOptions))
    {
        bool add = true;
        if (!word.empty() && (word.front() == '+' || word.front() == '-'))
        {
            add = word.front() != '-'; word.erase(word.begin());
        }
        if (!_tcsicmp(word.c_str(), _T("LastFound"))) aSetLastFoundWindow = add;
        else if (!_tcsicmp(word.c_str(), _T("OwnDialogs"))) aOwnDialogs = add ? TOGGLED_ON : TOGGLED_OFF;
    }
    if (p) apply_gui_options(*p);
    return OK;
}

void GuiType::GetNonClientArea(LONG &aWidth, LONG &aHeight)
{
    aWidth = aHeight = 0;
    GuiPeer *p = peer(this);
    if (!p || !gtk_widget_get_realized(p->window)) return;
    GtkAllocation outer{}, client{};
    gtk_widget_get_allocation(p->window, &outer);
    gtk_widget_get_allocation(p->root, &client);
    aWidth = std::max(0, outer.width - client.width);
    aHeight = std::max(0, outer.height - client.height);
}

void GuiType::GetTotalWidthAndHeight(LONG &aWidth, LONG &aHeight)
{
    LONG ncw, nch; GetNonClientArea(ncw, nch);
    aWidth += ncw; aHeight += nch;
}

void GuiType::ParseMinMaxSizeOption(LPCTSTR value, LONG &aWidth, LONG &aHeight)
{
    aWidth = aHeight = -1;
    if (!value) return;
    const TCHAR *x = _tcschr(value, 'x'); if (!x) x = _tcschr(value, 'X');
    if (x)
    {
        std::basic_string<TCHAR> first(value, x - value);
        int n; if (parse_int(first.c_str(), n)) aWidth = n;
        if (parse_int(x + 1, n)) aHeight = n;
    }
    else { int n; if (parse_int(value, n)) aWidth = n; }
}

void GuiType::ControlInitOptions(GuiControlOptionsType &aOpt, GuiControlType &)
{
    ZeroMemory(&aOpt, sizeof(aOpt));
    aOpt.x = aOpt.y = aOpt.width = aOpt.height = COORD_UNSPECIFIED;
    aOpt.range_min = 0; aOpt.range_max = 100;
    aOpt.color = CLR_DEFAULT; aOpt.color_bk = CLR_INVALID;
}

ResultType GuiType::ControlParseOptions(LPCTSTR aOptions, GuiControlOptionsType &aOpt,
    GuiControlType &aControl, GuiIndexType)
{
    ControlPeer *p = peer(&aControl);
    ControlOptions parsed = parse_control_options(aOptions, p ? p->options : ControlOptions{});
    aOpt.x = parsed.x; aOpt.y = parsed.y; aOpt.width = parsed.width; aOpt.height = parsed.height;
    aOpt.row_count = static_cast<float>(parsed.rows);
    aOpt.choice = parsed.choose;
    aOpt.range_min = parsed.range_min; aOpt.range_max = parsed.range_max;
    aOpt.tick_interval = parsed.tick_interval;
    aOpt.checked = parsed.checked ? 1 : 0;
    if (!parsed.name.empty() && ControlSetName(aControl, parsed.name.c_str()) != OK) return FAIL;
    if (p)
    {
        p->options = parsed;
        if (parsed.hidden != p->explicit_hidden) ControlSetVisible(aControl, !parsed.hidden);
        if (parsed.disabled != p->explicit_disabled) ControlSetEnabled(aControl, !parsed.disabled);
        if (parsed.x != COORD_UNSPECIFIED || parsed.y != COORD_UNSPECIFIED ||
            parsed.width != COORD_UNSPECIFIED || parsed.height != COORD_UNSPECIFIED)
            ControlMove(aControl, parsed.x, parsed.y, parsed.width, parsed.height);
        configure_colors(*p);
    }
    return OK;
}

void GuiType::ControlAddItems(GuiControlType &aControl, Array *aObj)
{
    ControlPeer *p = peer(&aControl);
    if (!p) return;
    if (aControl.type == GUI_CONTROL_TAB) add_tab_items(*p, aObj);
    else if (aControl.type == GUI_CONTROL_DROPDOWNLIST || aControl.type == GUI_CONTROL_COMBOBOX) add_combo_items(*p, aObj);
    else add_list_items(*p, aObj);
}

void GuiType::ControlSetChoice(GuiControlType &aControl, int aChoice)
{
    ExprTokenType tok{}; tok.symbol = SYM_INTEGER; tok.value_int64 = aChoice;
    ControlChoose(aControl, tok, TRUE);
}

ResultType GuiType::ControlLoadPicture(GuiControlType &aControl, LPCTSTR aFilename,
    int aWidth, int aHeight, int)
{
    ControlPeer *p = peer(&aControl);
    if (!p || !GTK_IS_IMAGE(p->widget)) return FAIL;
    GError *error = nullptr;
    GdkPixbuf *pix = gdk_pixbuf_new_from_file_at_scale(to_utf8(aFilename).c_str(),
        aWidth > 0 ? aWidth : -1, aHeight > 0 ? aHeight : -1, TRUE, &error);
    if (!pix) { if (error) g_error_free(error); return FAIL; }
    gtk_image_set_from_pixbuf(GTK_IMAGE(p->widget), pix);
    g_object_unref(pix);
    return OK;
}

// ---------------------------------------------------------------------------
// Public window operations, submission and event registration.
// ---------------------------------------------------------------------------

FResult GuiType::Show(optl<StrArg> aOptions)
{
    GUI_MUST_HAVE_HWND;
    GuiPeer *p = peer(this);
    if (!p) return FR_FAIL;
    ShowOptions o = parse_show_options(aOptions.value_or_empty());
    int width = o.width;
    int height = o.height;
    if (o.auto_size || (p->first_show && width == COORD_UNSPECIFIED && height == COORD_UNSPECIFIED))
    {
        width = std::max(1, p->max_right + p->margin_x);
        height = std::max(1, p->max_bottom + p->margin_y);
        if (p->status_box)
        {
            GtkRequisition minimum{}, natural{};
            gtk_widget_get_preferred_size(p->status_box, &minimum, &natural);
            height += natural.height;
        }
    }
    if (width != COORD_UNSPECIFIED || height != COORD_UNSPECIFIED)
    {
        int current_w = 1, current_h = 1;
        gtk_window_get_size(GTK_WINDOW(p->window), &current_w, &current_h);
        gtk_window_resize(GTK_WINDOW(p->window),
            width == COORD_UNSPECIFIED ? current_w : std::max(1, width),
            height == COORD_UNSPECIFIED ? current_h : std::max(1, height));
    }
    if (o.center) gtk_window_set_position(GTK_WINDOW(p->window), GTK_WIN_POS_CENTER);
    else if (o.x != COORD_UNSPECIFIED || o.y != COORD_UNSPECIFIED)
    {
        int x = 0, y = 0;
        gtk_window_get_position(GTK_WINDOW(p->window), &x, &y);
        gtk_window_move(GTK_WINDOW(p->window), o.x == COORD_UNSPECIFIED ? x : o.x,
            o.y == COORD_UNSPECIFIED ? y : o.y);
    }

    p->suppress_window_events = true;
    if (o.hide)
        gtk_widget_hide(p->window);
    else
    {
        gtk_widget_show_all(p->window);
        for (ControlPeer *c : p->controls)
            if (c->explicit_hidden) gtk_widget_hide(c->outer ? c->outer : c->widget);
        if (o.minimize) gtk_window_iconify(GTK_WINDOW(p->window));
        else if (o.maximize) gtk_window_maximize(GTK_WINDOW(p->window));
        else if (o.restore) { gtk_window_unmaximize(GTK_WINDOW(p->window)); gtk_window_deiconify(GTK_WINDOW(p->window)); }
        if (!o.no_activate) gtk_window_present(GTK_WINDOW(p->window));
    }
    p->suppress_window_events = false;
    p->visible = !o.hide;
    p->first_show = false;
    mGuiShowHasNeverBeenDone = false;
    VisibilityChanged();
    pump_events();
    return OK;
}

FResult GuiType::Minimize()
{
    GUI_MUST_HAVE_HWND;
    gtk_window_iconify(GTK_WINDOW(peer(this)->window));
    return OK;
}

FResult GuiType::Maximize()
{
    GUI_MUST_HAVE_HWND;
    gtk_window_maximize(GTK_WINDOW(peer(this)->window));
    return OK;
}

FResult GuiType::Restore()
{
    GUI_MUST_HAVE_HWND;
    GtkWindow *w = GTK_WINDOW(peer(this)->window);
    gtk_window_unmaximize(w); gtk_window_deiconify(w); gtk_window_present(w);
    return OK;
}

FResult GuiType::Flash(optl<BOOL>)
{
    GUI_MUST_HAVE_HWND;
    GtkWidget *w = peer(this)->window;
    gtk_window_set_urgency_hint(GTK_WINDOW(w), TRUE);
    return OK;
}

FResult GuiType::Hide()
{
    GUI_MUST_HAVE_HWND;
    Cancel();
    return OK;
}

void GuiType::Cancel()
{
    GuiPeer *p = peer(this);
    if (p && p->window)
    {
        gtk_widget_hide(p->window);
        p->visible = false;
        VisibilityChanged();
    }
    g_script.ExitIfNotPersistent(EXIT_CLOSE);
}

void GuiType::Close()
{
    if (!IsMonitoring(GUI_EVENT_CLOSE)) { Cancel(); return; }
    queue_gui_event(this, NO_CONTROL_INDEX, GUI_EVENT_CLOSE, NO_EVENT_INFO);
}

void GuiType::Escape()
{
    if (!IsMonitoring(GUI_EVENT_ESCAPE)) { Cancel(); return; }
    queue_gui_event(this, NO_CONTROL_INDEX, GUI_EVENT_ESCAPE, NO_EVENT_INFO);
}

void GuiType::VisibilityChanged()
{
    GuiPeer *p = peer(this);
    bool visible = p && p->window && gtk_widget_get_visible(p->window);
    if (visible == mVisibleRefCounted) return;
    mVisibleRefCounted = visible;
    if (visible) AddRef();
    else Release();
}

FResult GuiType::Submit(optl<BOOL> aHideIt, IObject *&aRetVal)
{
    GUI_MUST_HAVE_HWND;
    Object *result = Object::Create();
    if (!result) return FR_E_OUTOFMEM;
    for (GuiIndexType i = 0; i < mControlCount; ++i)
    {
        GuiControlType &control = *mControl[i];
        if (!control.name || control.type == GUI_CONTROL_RADIO || !control.HasSubmittableValue()) continue;
        TCHAR temp[MAX_NUMBER_SIZE];
        ResultToken value; value.InitResult(temp);
        ControlGetContents(value, control, Submit_Mode);
        if (value.Exited() || !result->SetOwnProp(control.name, value))
        {
            value.Free(); result->Release(); return value.Exited() ? FR_FAIL : FR_E_OUTOFMEM;
        }
        value.Free();
    }

    std::set<GSList *> processed;
    for (GuiIndexType i = 0; i < mControlCount; ++i)
    {
        GuiControlType &control = *mControl[i];
        if (control.type != GUI_CONTROL_RADIO) continue;
        ControlPeer *p = peer(&control);
        GSList *group = p && GTK_IS_RADIO_BUTTON(p->widget)
            ? gtk_radio_button_get_group(GTK_RADIO_BUTTON(p->widget)) : nullptr;
        if (!group || !processed.insert(group).second) continue;
        int ordinal = 0, selected = 0, names = 0;
        LPCTSTR shared_name = nullptr;
        for (GuiIndexType j = 0; j < mControlCount; ++j)
        {
            GuiControlType &r = *mControl[j];
            if (r.type != GUI_CONTROL_RADIO) continue;
            ControlPeer *rp = peer(&r);
            if (!rp || gtk_radio_button_get_group(GTK_RADIO_BUTTON(rp->widget)) != group) continue;
            ++ordinal;
            bool active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rp->widget));
            if (active) selected = ordinal;
            if (r.name)
            {
                ++names; shared_name = r.name;
                if (!result->SetOwnProp(r.name, active ? 1 : 0)) { result->Release(); return FR_E_OUTOFMEM; }
            }
        }
        if (names == 1 && ordinal > 1 && !result->SetOwnProp(shared_name, selected))
        { result->Release(); return FR_E_OUTOFMEM; }
    }
    if (aHideIt.value_or(TRUE)) Cancel();
    aRetVal = result;
    return OK;
}

FResult GuiType::OnEvent(StrArg aEventName, ExprTokenType &aCallback, optl<int> aAddRemove)
{
    GUI_MUST_HAVE_HWND;
    GuiEventType event = ConvertEvent(aEventName);
    if (event < GUI_EVENT_WINDOW_FIRST || event > GUI_EVENT_WINDOW_LAST) return FR_E_ARG(0);
    return OnEvent(nullptr, event, GUI_EVENTKIND_EVENT, aCallback, aAddRemove);
}

FResult GuiType::OnEvent(GuiControlType *aControl, UINT aEvent, UCHAR aEventKind,
    ExprTokenType &aCallback, optl<int> aAddRemove)
{
    int max_threads = aAddRemove.value_or(1);
    if (max_threads < -1 || max_threads > 1) return FR_E_ARG(2);
    TCHAR number[MAX_NUMBER_SIZE];
    IObject *func = TokenToObject(aCallback);
    LPTSTR name = func ? nullptr : TokenToString(aCallback, number);
    if (!func && !mEventSink) return FR_E_ARG(1);
    return OnEvent(aControl, aEvent, aEventKind, func, name, max_threads);
}

FResult GuiType::OnEvent(GuiControlType *aControl, UINT aEvent, UCHAR aEventKind,
    IObject *aFunc, LPTSTR aMethodName, int aMaxThreads)
{
    MsgMonitorList &handlers = aControl ? aControl->events : mEvents;
    MsgMonitorStruct *mon = aFunc ? handlers.Find(aEvent, aFunc, aEventKind)
                                  : handlers.Find(aEvent, aMethodName, aEventKind);
    if (!aMaxThreads)
    {
        if (mon) handlers.Delete(mon);
        if (aEventKind == GUI_EVENTKIND_EVENT) ApplyEventStyles(aControl, aEvent, false);
        return OK;
    }
    bool append = aMaxThreads >= 0;
    if (!append) aMaxThreads = -aMaxThreads;
    aMaxThreads = std::min(aMaxThreads, static_cast<int>(MsgMonitorStruct::MAX_INSTANCES));
    if (!mon)
    {
        if (aFunc)
        {
            int params = 2;
            if (aEventKind == GUI_EVENTKIND_COMMAND) params = 1;
            else if (aEventKind == GUI_EVENTKIND_EVENT)
            {
                if (aEvent == GUI_EVENT_DROPFILES) params = 5;
                else if (aEvent == GUI_EVENT_CLOSE || aEvent == GUI_EVENT_ESCAPE) params = 1;
                else if (aEvent == GUI_EVENT_RESIZE) params = 4;
                else if (aEvent == GUI_EVENT_CONTEXTMENU) params = 5 + (aControl == nullptr);
                else if (aEvent == GUI_EVENT_CLICK) params = 2 + (aControl && aControl->type == GUI_CONTROL_LINK);
                else if (aEvent == GUI_EVENT_ITEMCHECK || aEvent == GUI_EVENT_ITEMEXPAND) params = 3;
            }
            FResult vr = ValidateFunctor(aFunc, params);
            if (vr != OK) return vr;
            mon = handlers.Add(aEvent, aFunc, append);
        }
        else mon = handlers.Add(aEvent, aMethodName, append);
        if (!mon) return FR_E_OUTOFMEM;
    }
    mon->instance_count = 0;
    mon->max_instances = aMaxThreads;
    mon->msg_type = aEventKind;
    if (aEventKind == GUI_EVENTKIND_EVENT) ApplyEventStyles(aControl, aEvent, true);
    return OK;
}

void GuiType::ApplyEventStyles(GuiControlType *aControl, UINT aEvent, bool)
{
    // Most GTK signal subscriptions are installed once at construction and
    // MsgMonitorList decides whether they are forwarded. File dropping is the
    // exception: enabling the native target only while monitored preserves the
    // cursor/acceptance behaviour of WS_EX_ACCEPTFILES.
    if (!aControl && aEvent == GUI_EVENT_DROPFILES)
    {
        GuiPeer *p = peer(this);
        if (p) set_drop_target(*p, IsMonitoring(GUI_EVENT_DROPFILES));
    }
}

LPTSTR GuiType::ConvertEvent(GuiEventType event)
{
    if (event < _countof(sEventNames)) return sEventNames[event];
    static TCHAR name[2] = { 0, 0 };
    name[0] = static_cast<TCHAR>(static_cast<UCHAR>(event));
    return name;
}

GuiEventType GuiType::ConvertEvent(LPCTSTR event)
{
    for (GuiEventType i = 0; i < _countof(sEventNames); ++i)
        if (!_tcsicmp(sEventNames[i], event)) return i;
    return event && *event && !event[1] ? static_cast<GuiEventType>(*event) : GUI_EVENT_NONE;
}

IObject *GuiType::CreateDropArray(HDROP aDrop)
{
    Array *array = Array::Create();
    if (!array || !aDrop) return array;
    // Linux bridge representation: aDrop is a null-terminated gchar** URI/file list.
    gchar **files = reinterpret_cast<gchar **>(aDrop);
    for (size_t i = 0; files[i]; ++i)
    {
        gchar *path = g_filename_from_uri(files[i], nullptr, nullptr);
        const char *value = path ? path : files[i];
        auto t = from_utf8(value);
        array->Append(t.c_str(), t.size());
        g_free(path);
    }
    g_strfreev(files);
    return array;
}

void GuiType::UpdateMenuBars(HMENU)
{
    // A UserMenu's contents changed: rebuild the menu bar of every window
    // that uses one.  The HMENU argument is a Win32 concept; on GTK the bar
    // is rebuilt from the UserMenu linked list so item changes are visible
    // immediately (matching the Windows UpdateMenuBars() behaviour).
    for (GuiType *gui = g_firstGui; gui; gui = gui->mNextGui)
    {
        GuiPeer *p = peer(gui);
        if (!p || !gui->mMenu) continue;
        if (p->menu_bar)
        {
            gtk_widget_destroy(p->menu_bar);
            p->menu_bar = nullptr;
            p->menu_bindings.clear();
        }
        p->menu_bar = gtk_menu_bar_new();
        append_menu_items(*p, p->menu_bar, *gui->mMenu);
        gtk_box_pack_start(GTK_BOX(p->root), p->menu_bar, FALSE, FALSE, 0);
        gtk_box_reorder_child(GTK_BOX(p->root), p->menu_bar, 0);
        gtk_widget_show_all(p->menu_bar);
    }
    pump_events();
}

void GuiType::Event(GuiIndexType aControlIndex, UINT aNotifyCode, USHORT aGuiEvent, UINT_PTR aEventInfo)
{
    if (aControlIndex == NO_CONTROL_INDEX)
    {
        GuiEventType event = static_cast<GuiEventType>(LOBYTE(aGuiEvent));
        if (mEvents.IsMonitoring(event, GUI_EVENTKIND_EVENT))
            queue_gui_event(this, aControlIndex, aGuiEvent, aEventInfo);
        return;
    }
    GuiEventType event = static_cast<GuiEventType>(LOBYTE(aGuiEvent));
    if (event == GUI_EVENT_DROPFILES)
    {
        if (mEvents.IsMonitoring(event, GUI_EVENTKIND_EVENT))
            queue_gui_event(this, aControlIndex, aGuiEvent, aEventInfo);
        return;
    }
    if (aControlIndex >= mControlCount) return;
    GuiControlType &control = *mControl[aControlIndex];
    if (!control.events.Count() || (control.attrib & GUI_CONTROL_ATTRIB_SUPPRESS_EVENTS)) return;
    if (aGuiEvent == GUI_EVENT_WM_COMMAND)
    {
        if (control.events.IsMonitoring(aNotifyCode, GUI_EVENTKIND_COMMAND))
            queue_gui_event(this, aControlIndex, aGuiEvent, aNotifyCode);
        return;
    }
    event = static_cast<GuiEventType>(LOBYTE(aGuiEvent));
    if (control.events.IsMonitoring(event, GUI_EVENTKIND_EVENT))
        queue_gui_event(this, aControlIndex, aGuiEvent, aEventInfo);
}

bool GuiType::ControlWmNotify(GuiControlType &, LPNMHDR, INT_PTR &)
{
    // Native WM_NOTIFY does not exist on GTK. OnNotify registrations are
    // retained for API compatibility and can be raised by a custom bridge.
    return false;
}

// ---------------------------------------------------------------------------
// Lookup, fonts, sorting, hotkeys and platform-neutral option helpers.
// ---------------------------------------------------------------------------

GuiIndexType GuiType::FindControl(LPCTSTR aControlID)
{
    if (!aControlID || !*aControlID) return static_cast<GuiIndexType>(-1);
    UINT_PTR numeric = 0;
    if (parse_uintptr_text(aControlID, numeric))
    {
        for (GuiIndexType i = 0; i < mControlCount; ++i)
            if (reinterpret_cast<UINT_PTR>(mControl[i]->hwnd) == numeric) return i;
    }
    for (GuiIndexType i = 0; i < mControlCount; ++i)
        if (mControl[i]->name && !_tcsicmp(mControl[i]->name, aControlID)) return i;
    std::string wanted = to_utf8(aControlID);
    for (GuiIndexType i = 0; i < mControlCount; ++i)
    {
        ControlPeer *p = peer(mControl[i]);
        if (p && widget_text(*p) == wanted) return i;
    }
    // ClassNN: GTK controls expose their AutoHotkey type followed by a 1-based
    // occurrence number, mirroring the observable Windows API convention.
    for (int type = 1; type < GUI_CONTROL_TYPE_COUNT; ++type)
    {
        int occurrence = 0;
        for (GuiIndexType i = 0; i < mControlCount; ++i)
        {
            if (mControl[i]->type != type) continue;
            ++occurrence;
            TCHAR class_nn[96];
            _sntprintf(class_nn, _countof(class_nn), _T("%s%d"), GuiControlType::sTypeNames[type], occurrence);
            if (!_tcsicmp(class_nn, aControlID)) return i;
        }
    }
    return static_cast<GuiIndexType>(-1);
}

int GuiType::FindGroup(GuiIndexType aControlIndex, GuiIndexType &aGroupStart, GuiIndexType &aGroupEnd)
{
    aGroupStart = aGroupEnd = aControlIndex;
    if (aControlIndex >= mControlCount || mControl[aControlIndex]->type != GUI_CONTROL_RADIO) return 0;
    ControlPeer *target = peer(mControl[aControlIndex]);
    if (!target || !GTK_IS_RADIO_BUTTON(target->widget)) return 1;
    GSList *group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(target->widget));
    int count = 0;
    for (GuiIndexType i = 0; i < mControlCount; ++i)
    {
        ControlPeer *p = peer(mControl[i]);
        if (mControl[i]->type == GUI_CONTROL_RADIO && p &&
            gtk_radio_button_get_group(GTK_RADIO_BUTTON(p->widget)) == group)
        {
            if (!count) aGroupStart = i;
            aGroupEnd = i; ++count;
        }
    }
    return count;
}

FResult GuiType::SetFont(optl<StrArg> aOptions, optl<StrArg> aFontName)
{
    GUI_MUST_HAVE_HWND;
    GuiPeer *p = peer(this);
    if (!p) return FR_FAIL;
    p->current_font = parse_font(aOptions.value_or_empty(), aFontName.value_or_empty(), p->current_font);
    if (p->current_font.has_color)
        mCurrentColor = rgb_to_colorref(
            static_cast<unsigned>(std::lround(p->current_font.color.red * 255.0)),
            static_cast<unsigned>(std::lround(p->current_font.color.green * 255.0)),
            static_cast<unsigned>(std::lround(p->current_font.color.blue * 255.0)));
    mCurrentFontIndex = FindOrCreateFont(aOptions.value_or_empty(), aFontName.value_or_empty(), nullptr, nullptr);
    return OK;
}

int GuiType::FindOrCreateFont(LPCTSTR aOptions, LPCTSTR aFontName, FontType *, COLORREF *)
{
    // Font state lives in GuiPeer::current_font (see SetFont); this static
    // helper only guarantees the core's sFont array exists.
    (void)aOptions; (void)aFontName;
    if (!sFont)
    {
        sFont = static_cast<FontType *>(calloc(1, sizeof(FontType)));
        if (!sFont) return -1;
        sFontCount = 1;
    }
    return 0;
}

void GuiType::FontGetAttributes(FontType &)
{
    // Pango attributes are kept in GuiPeer::current_font, not LOGFONT fields.
}

int GuiType::FindFont(FontType &)
{
    return sFontCount ? 0 : -1;
}

void GuiType::LV_Sort(GuiControlType &aControl, int aColumnIndex, bool aSortOnlyIfEnabled, TCHAR aForceDirection)
{
    ControlPeer *p = peer(&aControl);
    if (!p || !p->list_store || aColumnIndex != 0) return;
    if (aSortOnlyIfEnabled && aControl.union_lv_attrib && aControl.union_lv_attrib->no_auto_sort) return;
    bool ascending = true;
    if (aControl.union_lv_attrib)
    {
        if (aForceDirection == 'A' || aForceDirection == 'a') ascending = true;
        else if (aForceDirection == 'D' || aForceDirection == 'd') ascending = false;
        else if (aControl.union_lv_attrib->sorted_by_col == aColumnIndex)
            ascending = !aControl.union_lv_attrib->is_now_sorted_ascending;
        aControl.union_lv_attrib->sorted_by_col = aColumnIndex;
        aControl.union_lv_attrib->is_now_sorted_ascending = ascending;
    }
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(p->list_store), 0,
        ascending ? GTK_SORT_ASCENDING : GTK_SORT_DESCENDING);
}

#ifndef HOTKEYF_SHIFT
# define HOTKEYF_SHIFT 0x01
# define HOTKEYF_CONTROL 0x02
# define HOTKEYF_ALT 0x04
# define HOTKEYF_EXT 0x08
#endif

WORD GuiType::TextToHotkey(LPCTSTR aText)
{
    if (!aText || !*aText) return 0;
    BYTE mods = 0;
    while (*aText)
    {
        if (*aText == '+') mods |= HOTKEYF_SHIFT;
        else if (*aText == '^') mods |= HOTKEYF_CONTROL;
        else if (*aText == '!') mods |= HOTKEYF_ALT;
        else break;
        ++aText;
    }
    std::string key = to_utf8(aText);
    guint val = gdk_keyval_from_name(key.c_str());
    if (!val && key.size() == 1) val = gdk_unicode_to_keyval(static_cast<gunichar>(key[0]));
    if (!val) return 0;
    return MAKEWORD(static_cast<BYTE>(val & 0xff), mods);
}

LPTSTR GuiType::HotkeyToText(WORD aHotkey, LPTSTR aBuf)
{
    BYTE mods = HIBYTE(aHotkey), key = LOBYTE(aHotkey);
    LPTSTR out = aBuf;
    if (mods & HOTKEYF_SHIFT) *out++ = '+';
    if (mods & HOTKEYF_CONTROL) *out++ = '^';
    if (mods & HOTKEYF_ALT) *out++ = '!';
    const char *name = gdk_keyval_name(key);
    auto text = from_utf8(name ? name : "");
    tmemcpy(out, text.c_str(), text.size() + 1);
    return aBuf;
}

void GuiType::ControlCheckRadioButton(GuiControlType &aControl, GuiIndexType, WPARAM aCheckType)
{
    ControlPeer *p = peer(&aControl);
    if (p && GTK_IS_TOGGLE_BUTTON(p->widget))
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->widget), aCheckType ? TRUE : FALSE);
}

void GuiType::ControlSetUpDownOptions(GuiControlType &aControl, GuiControlOptionsType &aOpt)
{
    ControlPeer *p = peer(&aControl);
    if (!p || !GTK_IS_SPIN_BUTTON(p->widget)) return;
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(p->widget), aOpt.range_min, aOpt.range_max);
}

int GuiType::ControlGetDefaultSliderThickness(DWORD, int aThumbThickness)
{
    return aThumbThickness > 0 ? aThumbThickness : 24;
}

int GuiType::ControlInvertSliderIfNeeded(GuiControlType &aControl, int aPosition)
{
    ControlPeer *p = peer(&aControl);
    if (!p || !p->inverted) return aPosition;
    return p->range_max - (aPosition - p->range_min);
}

void GuiType::ControlSetSliderOptions(GuiControlType &aControl, GuiControlOptionsType &aOpt)
{
    ControlPeer *p = peer(&aControl);
    if (!p || !GTK_IS_RANGE(p->widget)) return;
    p->range_min = aOpt.range_min; p->range_max = aOpt.range_max;
    gtk_range_set_range(GTK_RANGE(p->widget), aOpt.range_min, aOpt.range_max);
    if (aOpt.tick_interval_changed && GTK_IS_SCALE(p->widget))
    {
        gtk_scale_clear_marks(GTK_SCALE(p->widget));
        if (aOpt.tick_interval > 0)
            for (int n = aOpt.range_min; n <= aOpt.range_max; n += aOpt.tick_interval)
                gtk_scale_add_mark(GTK_SCALE(p->widget), n, GTK_POS_BOTTOM, nullptr);
    }
}

void GuiType::ControlSetListViewOptions(GuiControlType &aControl, GuiControlOptionsType &aOpt)
{
    ControlPeer *p = peer(&aControl);
    if (!p) return;
    if (aControl.union_lv_attrib) aControl.union_lv_attrib->no_auto_sort = aOpt.listview_no_auto_sort;
    if (aOpt.color != CLR_INVALID) ControlSetTextColor(aControl, aOpt.color);
}

void GuiType::ControlSetTreeViewOptions(GuiControlType &aControl, GuiControlOptionsType &aOpt)
{
    if (aOpt.color != CLR_INVALID) ControlSetTextColor(aControl, aOpt.color);
}

void GuiType::ControlSetProgressOptions(GuiControlType &aControl, GuiControlOptionsType &aOpt, DWORD)
{
    ControlPeer *p = peer(&aControl);
    if (!p) return;
    p->range_min = aOpt.range_min; p->range_max = aOpt.range_max;
    if (aOpt.color != CLR_INVALID) ControlSetTextColor(aControl, aOpt.color);
}

GuiControlType *GuiType::ControlOverrideBkColor(GuiControlType &aControl)
{
    GuiControlType *tab = FindTabControl(aControl.tab_control_index);
    return tab && tab->background_color != CLR_INVALID ? tab : nullptr;
}

void GuiType::ControlGetBkColor(GuiControlType &aControl, bool aUseWindowColor,
    HBRUSH &aBrush, COLORREF &aColor)
{
    aBrush = nullptr;
    if (aControl.background_color != CLR_INVALID) aColor = aControl.background_color;
    else if (GuiControlType *tab = ControlOverrideBkColor(aControl)) aColor = tab->background_color;
    else aColor = aUseWindowColor ? mBackgroundColorWin : CLR_DEFAULT;
}

// ---------------------------------------------------------------------------
// Tab/page geometry, focused item geometry and accelerator compatibility.
// ---------------------------------------------------------------------------

void GuiType::ControlUpdateCurrentTab(GuiControlType &aTabControl, bool aFocusFirstControl)
{
    ControlPeer *tab = peer(&aTabControl);
    if (!tab || !GTK_IS_NOTEBOOK(tab->widget)) return;
    tab->selected = gtk_notebook_get_current_page(GTK_NOTEBOOK(tab->widget));
    if (tab->gui->current_tab == tab) tab->gui->current_tab_page = tab->selected;
    if (!aFocusFirstControl) return;
    for (ControlPeer *p : tab->gui->controls)
    {
        if (p != tab && p->owner->tab_control_index == aTabControl.tab_index &&
            p->owner->tab_index == tab->selected && !p->explicit_hidden && !p->explicit_disabled)
        {
            gtk_widget_grab_focus(effective_widget(*p));
            break;
        }
    }
}

GuiControlType *GuiType::FindTabControl(TabControlIndexType aTabControlIndex)
{
    if (aTabControlIndex == MAX_TAB_CONTROLS) return nullptr;
    for (GuiIndexType i = 0; i < mControlCount; ++i)
        if (mControl[i]->type == GUI_CONTROL_TAB && mControl[i]->tab_index == aTabControlIndex)
            return mControl[i];
    return nullptr;
}

int GuiType::FindTabIndexByName(GuiControlType &aTabControl, LPTSTR aName, bool aExactMatch)
{
    ControlPeer *p = peer(&aTabControl);
    if (!p) return -1;
    return find_item(*p, to_utf8(aName), aExactMatch);
}

int GuiType::GetControlCountOnTabPage(TabControlIndexType aTabControlIndex, TabIndexType aTabIndex)
{
    int count = 0;
    for (GuiIndexType i = 0; i < mControlCount; ++i)
        if (mControl[i]->type != GUI_CONTROL_TAB &&
            mControl[i]->tab_control_index == aTabControlIndex && mControl[i]->tab_index == aTabIndex)
            ++count;
    return count;
}

void GuiType::GetTabDisplayAreaRect(HWND aTabControlHwnd, RECT &aRect)
{
    aRect = {};
    GuiControlType *control = nullptr;
    auto found = s_widget_to_control.find(to_widget(aTabControlHwnd));
    if (found != s_widget_to_control.end()) control = found->second;
    ControlPeer *p = control ? peer(control) : nullptr;
    if (!p) return;
    GtkAllocation allocation{};
    GtkWidget *page = GTK_IS_NOTEBOOK(p->widget)
        ? gtk_notebook_get_nth_page(GTK_NOTEBOOK(p->widget), std::max(0, gtk_notebook_get_current_page(GTK_NOTEBOOK(p->widget))))
        : nullptr;
    gtk_widget_get_allocation(page ? page : p->widget, &allocation);
    aRect.left = p->x + allocation.x;
    aRect.top = p->y + allocation.y;
    aRect.right = aRect.left + allocation.width;
    aRect.bottom = aRect.top + allocation.height;
}

POINT GuiType::GetPositionOfTabDisplayArea(GuiControlType &aTabControl)
{
    RECT r{}; GetTabDisplayAreaRect(aTabControl.hwnd, r);
    POINT p = { r.left, r.top };
    return p;
}

ResultType GuiType::SelectAdjacentTab(GuiControlType &aTabControl, bool aMoveToRight,
    bool aFocusFirstControl, bool aWrapAround)
{
    ControlPeer *p = peer(&aTabControl);
    if (!p || !GTK_IS_NOTEBOOK(p->widget)) return FAIL;
    int count = gtk_notebook_get_n_pages(GTK_NOTEBOOK(p->widget));
    if (!count) return FAIL;
    int index = gtk_notebook_get_current_page(GTK_NOTEBOOK(p->widget));
    int next = index + (aMoveToRight ? 1 : -1);
    if (next < 0 || next >= count)
    {
        if (!aWrapAround) return FAIL;
        next = next < 0 ? count - 1 : 0;
    }
    gtk_notebook_set_current_page(GTK_NOTEBOOK(p->widget), next);
    ControlUpdateCurrentTab(aTabControl, aFocusFirstControl);
    return OK;
}

void GuiType::AutoSizeTabControl(GuiControlType &aTabControl)
{
    ControlPeer *p = peer(&aTabControl);
    if (!p || !GTK_IS_NOTEBOOK(p->widget)) return;
    int max_right = 0, max_bottom = 0;
    for (ControlPeer *c : p->gui->controls)
    {
        if (c == p || c->owner->tab_control_index != aTabControl.tab_index) continue;
        max_right = std::max(max_right, c->x + c->width - p->x);
        max_bottom = std::max(max_bottom, c->y + c->height - p->y);
    }
    if (max_right || max_bottom)
    {
        p->width = std::max(p->width, max_right + p->gui->margin_x);
        p->height = std::max(p->height, max_bottom + p->gui->margin_y + 32);
        gtk_widget_set_size_request(p->outer ? p->outer : p->widget, p->width, p->height);
    }
}

ResultType GuiType::CreateTabDialog(GuiControlType &aTabControl, GuiControlOptionsType &)
{
    ControlPeer *p = peer(&aTabControl);
    return p && p->tab_container ? OK : FAIL;
}

void GuiType::UpdateTabDialog(HWND aTabControlHwnd)
{
    GuiControlType *control = nullptr;
    auto found = s_widget_to_control.find(to_widget(aTabControlHwnd));
    if (found != s_widget_to_control.end()) control = found->second;
    ControlPeer *p = control ? peer(control) : nullptr;
    if (p) gtk_widget_queue_resize(p->widget);
}

void GuiType::ControlGetPosOfFocusedItem(GuiControlType &aControl, POINT &aPoint)
{
    aPoint.x = aPoint.y = 0;
    ControlPeer *p = peer(&aControl);
    if (!p) return;
    GtkWidget *view = p->content ? p->content : p->widget;
    if (GTK_IS_TREE_VIEW(view))
    {
        GtkTreePath *path = nullptr;
        GtkTreeViewColumn *column = nullptr;
        gtk_tree_view_get_cursor(GTK_TREE_VIEW(view), &path, &column);
        if (path)
        {
            GdkRectangle rect{};
            gtk_tree_view_get_cell_area(GTK_TREE_VIEW(view), path, column, &rect);
            aPoint.x = p->x + rect.x;
            aPoint.y = p->y + rect.y;
            gtk_tree_path_free(path);
            return;
        }
    }
    aPoint.x = p->x; aPoint.y = p->y;
}

void GuiType::UpdateAccelerators(UserMenu &)
{
    // GTK mnemonics are installed by script_menu_linux.cpp when menu labels
    // are materialized. mAccel remains null because GTK owns the accel group.
    mAccel = nullptr;
}

void GuiType::UpdateAccelerators(UserMenu &, LPACCEL, int &)
{
}

void GuiType::RemoveAccelerators()
{
    mAccel = nullptr;
}

bool GuiType::ConvertAccelerator(LPTSTR aString, ACCEL &aAccel)
{
    ZeroMemory(&aAccel, sizeof(aAccel));
    if (!aString) return false;
    while (*aString && _istspace(*aString)) ++aString;
    if (!*aString) return false;
    WORD hotkey = TextToHotkey(aString);
    if (!hotkey && !aString[1]) hotkey = MAKEWORD(static_cast<BYTE>(*aString), 0);
    if (!hotkey) return false;
    aAccel.key = LOBYTE(hotkey);
    aAccel.fVirt = HIBYTE(hotkey);
    return true;
}

void GuiType::SetDefaultMargins()
{
    GuiPeer *p = peer(this);
    if (!p) return;
    if (mMarginX == COORD_UNSPECIFIED) mMarginX = p->margin_x = 10;
    if (mMarginY == COORD_UNSPECIFIED) mMarginY = p->margin_y = 10;
}

// ---------------------------------------------------------------------------
// ListView / TreeView / StatusBar type-specific methods (GTK backends).
// ---------------------------------------------------------------------------

static std::string utf8_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static bool parse_int_a(const std::string &s, int &value)
{
    if (s.empty()) return false;
    char *end = nullptr;
    errno = 0;
    long v = strtol(s.c_str(), &end, 10);
    if (errno || end == s.c_str() || *end) return false;
    value = static_cast<int>(v);
    return true;
}

static void rebuild_lv_columns(ControlPeer &p)
{
    if (!p.list_store || !GTK_IS_TREE_VIEW(p.widget)) return;
    GtkTreeView *tv = GTK_TREE_VIEW(p.widget);
    while (GtkTreeViewColumn *c = gtk_tree_view_get_column(tv, 0))
        gtk_tree_view_remove_column(tv, c);
    for (size_t i = 0; i < p.lv_columns.size(); ++i)
    {
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(
            p.lv_columns[i].c_str(), r, "text", static_cast<int>(i), nullptr);
        if (i < p.lv_widths.size() && p.lv_widths[i] > 0)
        {
            gtk_tree_view_column_set_sizing(c, GTK_TREE_VIEW_COLUMN_FIXED);
            gtk_tree_view_column_set_fixed_width(c, p.lv_widths[i]);
            gtk_tree_view_column_set_resizable(c, TRUE);
        }
        gtk_tree_view_append_column(tv, c);
    }
    gtk_tree_view_set_headers_visible(tv, p.lv_columns.empty() ? FALSE : TRUE);
}

FResult GuiControlType::LV_AddInsertModify(optl<int> aRow, optl<StrArg> aOptions, VariantParams &aCol, int *aRetVal, bool aModify)
{
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    if (!p || !p->list_store || !GTK_IS_TREE_VIEW(p->widget)) return FR_FAIL;
    GtkListStore *store = p->list_store;
    int rows = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(store), nullptr);
    GtkTreeIter it;
    if (aRow.has_value())
    {
        int r = aRow.value();
        if (r < -1) return FR_E_ARG(0);
        if (r == -1) r = rows + (aModify ? 0 : 1);
        if (aModify)
        {
            if (r < 1 || r > rows || !gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(store), &it, nullptr, r - 1)) return FR_FAIL;
        }
        else
        {
            if (r < 1) return FR_E_ARG(0);
            gtk_list_store_insert(store, &it, r - 1 < rows ? r - 1 : rows);
        }
    }
    else
    {
        if (aModify) return FR_E_ARG(0);
        gtk_list_store_append(store, &it);
    }
    for (int i = 0; i < aCol.count && i < LV_MAX_COLS; ++i)
    {
        ExprTokenType *t = aCol.value[i];
        if (!t || t->symbol == SYM_MISSING) continue;
        TCHAR buf[MAX_NUMBER_SIZE];
        std::string v = to_utf8(TokenToString(*t, buf));
        gtk_list_store_set(store, &it, i, v.c_str(), -1);
    }
    std::string opt = utf8_lower(to_utf8(aOptions.value_or_empty()));
    if (opt.find("select") != std::string::npos || opt.find("focus") != std::string::npos)
    {
        GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(store), &it);
        if (p->selection) gtk_tree_selection_select_path(p->selection, path);
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(p->widget), path, nullptr, FALSE);
        gtk_tree_path_free(path);
    }
    if (aRetVal)
    {
        GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(store), &it);
        int *ix = gtk_tree_path_get_indices(path);
        *aRetVal = ix ? ix[0] + 1 : 0;
        gtk_tree_path_free(path);
    }
    return OK;
}

FResult GuiControlType::LV_InsertModifyCol(optl<int> aColumn, optl<StrArg> aOptions, optl<StrArg> aTitle, int *aRetVal, bool aModify)
{
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    if (!p) return FR_FAIL;
    int width = 0;
    std::string opt = to_utf8(aOptions.value_or_empty());
    int n = 0;
    if (parse_int_a(opt, n)) width = n;
    else if (!opt.empty())
    {
        size_t off = (opt[0] == 'w' || opt[0] == 'W') ? 1 : 0;
        if (parse_int_a(opt.substr(off), n)) width = n;
    }
    if (aModify)
    {
        if (!aColumn.has_value()) return FR_E_ARG(0);
        int c = aColumn.value();
        if (c < 1 || c > static_cast<int>(p->lv_columns.size())) return FR_FAIL;
        int idx = c - 1;
        if (aTitle.has_value()) p->lv_columns[idx] = to_utf8(aTitle.value());
        if (width > 0)
        {
            if (static_cast<int>(p->lv_widths.size()) <= idx) p->lv_widths.resize(idx + 1, 0);
            p->lv_widths[idx] = width;
        }
        rebuild_lv_columns(*p);
        return OK;
    }
    int idx = static_cast<int>(p->lv_columns.size());
    if (aColumn.has_value())
    {
        int c = aColumn.value();
        if (c < -1 || c == 0) return FR_E_ARG(0);
        if (c != -1)
        {
            if (c > static_cast<int>(p->lv_columns.size()) + 1) return FR_FAIL;
            idx = c - 1;
        }
    }
    p->lv_columns.insert(p->lv_columns.begin() + idx, aTitle.has_value() ? to_utf8(aTitle.value()) : std::string());
    p->lv_widths.insert(p->lv_widths.begin() + idx, width);
    rebuild_lv_columns(*p);
    if (aRetVal) *aRetVal = idx + 1;
    return OK;
}

FResult GuiControlType::LV_Delete(optl<int> aRow)
{
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    if (!p || !p->list_store) return FR_FAIL;
    if (!aRow.has_value()) { gtk_list_store_clear(p->list_store); p->items.clear(); return OK; }
    int r = aRow.value();
    if (r < 1 || r > gtk_tree_model_iter_n_children(GTK_TREE_MODEL(p->list_store), nullptr)) return FR_FAIL;
    GtkTreeIter it;
    if (!gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(p->list_store), &it, nullptr, r - 1)) return FR_FAIL;
    gtk_list_store_remove(p->list_store, &it);
    if (r - 1 < static_cast<int>(p->items.size())) p->items.erase(p->items.begin() + r - 1);
    return OK;
}

FResult GuiControlType::LV_DeleteCol(int aColumn)
{
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    if (!p) return FR_FAIL;
    if (aColumn < 1 || aColumn > static_cast<int>(p->lv_columns.size())) return FR_FAIL;
    int idx = aColumn - 1;
    p->lv_columns.erase(p->lv_columns.begin() + idx);
    if (idx < static_cast<int>(p->lv_widths.size())) p->lv_widths.erase(p->lv_widths.begin() + idx);
    rebuild_lv_columns(*p);
    return OK;
}

FResult GuiControlType::LV_GetCount(optl<StrArg> aMode, int &aRetVal)
{
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    if (!p) return FR_FAIL;
    if (aMode.has_value() && aMode.value()[0] == 'C' && _tcslen(aMode.value()) >= 2)
        aRetVal = static_cast<int>(p->lv_columns.size());
    else
        aRetVal = p->list_store ? gtk_tree_model_iter_n_children(GTK_TREE_MODEL(p->list_store), nullptr) : 0;
    return OK;
}

FResult GuiControlType::LV_GetNext(optl<int> aStartIndex, optl<StrArg> aRowType, int &aRetVal)
{
    (void)aRowType;
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    if (!p || !p->selection) return FR_FAIL;
    GList *rows = gtk_tree_selection_get_selected_rows(p->selection, nullptr);
    int start = aStartIndex.has_value() ? aStartIndex.value() : 0;
    aRetVal = 0;
    int best = INT_MAX;
    for (GList *g = rows; g; g = g->next)
    {
        int *ix = gtk_tree_path_get_indices(static_cast<GtkTreePath *>(g->data));
        if (!ix) continue;
        int pos = ix[0] + 1;
        if (pos > start && pos < best) best = pos;
    }
    g_list_free_full(rows, reinterpret_cast<GDestroyNotify>(gtk_tree_path_free));
    if (best != INT_MAX) aRetVal = best;
    return OK;
}

FResult GuiControlType::LV_GetText(int aRow, optl<int> aColumn, StrRet &aRetVal)
{
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    if (!p || !p->list_store) return FR_FAIL;
    int rows = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(p->list_store), nullptr);
    if (aRow < 1 || aRow > rows) return FR_FAIL;
    int col = aColumn.has_value() ? aColumn.value() - 1 : 0;
    if (col < 0 || col >= LV_MAX_COLS) return FR_FAIL;
    GtkTreeIter it;
    if (!gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(p->list_store), &it, nullptr, aRow - 1)) return FR_FAIL;
    gchar *v = nullptr;
    gtk_tree_model_get(GTK_TREE_MODEL(p->list_store), &it, col, &v, -1);
    std::string str = v ? v : "";
    g_free(v);
    auto t = from_utf8(str.c_str());
    return aRetVal.Copy(t.c_str()) ? OK : FR_FAIL;
}

FResult GuiControlType::LV_SetImageList(UINT_PTR aImageListID, optl<int> aIconType, UINT_PTR &aRetVal)
{
    (void)aImageListID; (void)aIconType; aRetVal = 0; return OK;
}

FResult GuiControlType::SB_SetParts(VariantParams &aParam, UINT &aRetVal)
{
    CTRL_THROW_IF_DESTROYED;
    aRetVal = static_cast<UINT>(aParam.count); return OK;
}

FResult GuiControlType::SB_SetText(StrArg aNewText, optl<UINT> aPartNumber, optl<UINT> aStyle)
{
    (void)aStyle;
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    if (!p || p->status_parts.empty()) return FR_FAIL;
    UINT part = aPartNumber.value_or(0);
    if (part == 0 || part == 1)
    {
        std::string v = to_utf8(aNewText);
        gtk_label_set_text(GTK_LABEL(p->status_parts.front()), v.c_str());
    }
    return OK;
}

FResult GuiControlType::SB_SetIcon(StrArg aFilename, optl<int> aIconNumber, optl<UINT> aPartNumber, UINT_PTR &aRetVal)
{
    (void)aIconNumber; (void)aPartNumber; aRetVal = 0;
    // GTK status bars have no icon area; validate the file silently.
    std::string path = to_utf8(aFilename);
    GError *e = nullptr;
    GdkPixbuf *pb = gdk_pixbuf_new_from_file(path.c_str(), &e);
    if (pb) g_object_unref(pb);
    else if (e) g_error_free(e);
    return OK;
}

// --- TreeView (GtkTreeStore; column 0 = name, column 1 = UINT_PTR id) ---

static bool tv_iter_from_ref(ControlPeer &p, uintptr_t id, GtkTreeIter &out)
{
    out = {};
    if (!p.tree_store) return false;
    auto f = p.tree_rows.find(id);
    if (f == p.tree_rows.end()) return false;
    GtkTreePath *path = gtk_tree_row_reference_get_path(f->second);
    if (!path) return false;
    bool ok = gtk_tree_model_get_iter(GTK_TREE_MODEL(p.tree_store), &out, path);
    gtk_tree_path_free(path);
    return ok;
}

static uintptr_t tv_id_from_iter(ControlPeer &p, GtkTreeIter &it)
{
    guint64 v = 0;
    gtk_tree_model_get(GTK_TREE_MODEL(p.tree_store), &it, 1, &v, -1);
    return static_cast<uintptr_t>(v);
}

static int tv_child_count(GtkTreeModel *m, GtkTreeIter *parent)
{
    int n = 0;
    GtkTreeIter it;
    if (!m || !gtk_tree_model_iter_children(m, &it, parent)) return 0;
    do { ++n; } while (gtk_tree_model_iter_next(m, &it));
    return n;
}

static uintptr_t tv_first_child(ControlPeer &p, GtkTreeIter *parent)
{
    GtkTreeIter it;
    if (gtk_tree_model_iter_children(GTK_TREE_MODEL(p.tree_store), &it, parent))
        return tv_id_from_iter(p, it);
    return 0;
}

FResult GuiControlType::TV_AddModify(bool aAdd, UINT_PTR aItemID, UINT_PTR aParentItemID, optl<StrArg> aOptions, optl<StrArg> aName, UINT_PTR &aRetVal)
{
    (void)aOptions;
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    if (!p || !p->tree_store) return FR_FAIL;
    if (!aAdd)
    {
        auto f = p->tree_rows.find(aItemID);
        if (f == p->tree_rows.end()) return FR_FAIL;
        GtkTreeIter it;
        if (!tv_iter_from_ref(*p, aItemID, it)) return FR_FAIL;
        if (aName.has_value()) gtk_tree_store_set(p->tree_store, &it, 0, to_utf8(aName.value()).c_str(), -1);
        aRetVal = aItemID;
        return OK;
    }
    uintptr_t id = p->next_tree_id++;
    GtkTreeIter it, parent_it{};
    bool has_parent = false;
    if (aParentItemID)
    {
        auto f = p->tree_rows.find(aParentItemID);
        if (f != p->tree_rows.end())
            has_parent = tv_iter_from_ref(*p, aParentItemID, parent_it);
    }
    gtk_tree_store_append(p->tree_store, &it, has_parent ? &parent_it : nullptr);
    std::string name = aName.has_value() ? to_utf8(aName.value()) : "";
    gtk_tree_store_set(p->tree_store, &it, 0, name.c_str(), 1, static_cast<guint64>(id), -1);
    GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(p->tree_store), &it);
    p->tree_rows[id] = gtk_tree_row_reference_new(GTK_TREE_MODEL(p->tree_store), path);
    gtk_tree_path_free(path);
    aRetVal = static_cast<UINT_PTR>(id);
    return OK;
}

FResult GuiControlType::TV_Delete(optl<UINT_PTR> aItemID)
{
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    if (!p || !p->tree_store) return FR_FAIL;
    if (!aItemID.has_value())
    {
        gtk_tree_store_clear(p->tree_store);
        for (auto &kv : p->tree_rows) gtk_tree_row_reference_free(kv.second);
        p->tree_rows.clear();
        return OK;
    }
    auto f = p->tree_rows.find(aItemID.value());
    if (f == p->tree_rows.end()) return FR_FAIL;
    GtkTreeIter it;
    if (!tv_iter_from_ref(*p, aItemID.value(), it)) return FR_FAIL;
    GtkTreePath *del_path = gtk_tree_model_get_path(GTK_TREE_MODEL(p->tree_store), &it);
    gtk_tree_store_remove(p->tree_store, &it);
    // Drop row references for the deleted item and all of its descendants.
    for (auto it2 = p->tree_rows.begin(); it2 != p->tree_rows.end(); )
    {
        GtkTreePath *pp = gtk_tree_row_reference_get_path(it2->second);
        bool under = pp && (gtk_tree_path_compare(pp, del_path) == 0 || gtk_tree_path_is_ancestor(del_path, pp));
        if (pp) gtk_tree_path_free(pp);
        if (under) { gtk_tree_row_reference_free(it2->second); it2 = p->tree_rows.erase(it2); }
        else ++it2;
    }
    gtk_tree_path_free(del_path);
    return OK;
}

FResult GuiControlType::TV_Get(UINT_PTR aItemID, StrArg aAttribute, UINT_PTR &aRetVal)
{
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    if (!p || !p->tree_store) return FR_FAIL;
    auto f = p->tree_rows.find(aItemID);
    if (f == p->tree_rows.end()) return FR_FAIL;
    GtkTreeIter it;
    if (!tv_iter_from_ref(*p, aItemID, it)) return FR_FAIL;
    aRetVal = 0;
    std::string attr = utf8_lower(to_utf8(aAttribute));
    if (attr == "count") aRetVal = static_cast<UINT_PTR>(tv_child_count(GTK_TREE_MODEL(p->tree_store), &it));
    else if (attr == "expanded")
    {
        GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(p->tree_store), &it);
        aRetVal = gtk_tree_view_row_expanded(GTK_TREE_VIEW(p->widget), path) ? 1 : 0;
        gtk_tree_path_free(path);
    }
    else if (attr == "first") aRetVal = static_cast<UINT_PTR>(tv_first_child(*p, &it));
    return OK;
}

FResult GuiControlType::TV_GetChild(UINT_PTR aItemID, UINT_PTR &aRetVal)
{
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    if (!p || !p->tree_store) return FR_FAIL;
    if (!aItemID) { aRetVal = static_cast<UINT_PTR>(tv_first_child(*p, nullptr)); return OK; }
    auto f = p->tree_rows.find(aItemID);
    if (f == p->tree_rows.end()) return FR_FAIL;
    GtkTreeIter it;
    if (!tv_iter_from_ref(*p, aItemID, it)) return FR_FAIL;
    aRetVal = static_cast<UINT_PTR>(tv_first_child(*p, &it));
    return OK;
}

FResult GuiControlType::TV_GetCount(UINT &aRetVal)
{
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    aRetVal = p && p->tree_store ? static_cast<UINT>(p->tree_rows.size()) : 0;
    return OK;
}

FResult GuiControlType::TV_GetNext(optl<UINT_PTR> aItemID, optl<StrArg> aItemType, UINT_PTR &aRetVal)
{
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    if (!p || !p->tree_store) return FR_FAIL;
    std::string t = utf8_lower(aItemType.has_value() ? to_utf8(aItemType.value()) : "F");
    aRetVal = 0;
    if (!aItemID.has_value() || !aItemID.value())
    {
        if (!t.empty() && (t[0] == 'n' || t[0] == 'f'))
            aRetVal = static_cast<UINT_PTR>(tv_first_child(*p, nullptr));
        return OK;
    }
    auto f = p->tree_rows.find(aItemID.value());
    if (f == p->tree_rows.end()) return FR_FAIL;
    GtkTreeIter it;
    if (!tv_iter_from_ref(*p, aItemID.value(), it)) return FR_FAIL;
    if (t.empty()) { aRetVal = static_cast<UINT_PTR>(tv_first_child(*p, &it)); return OK; }
    switch (t[0])
    {
    case 'n':
    {
        GtkTreePath *pth = gtk_tree_model_get_path(GTK_TREE_MODEL(p->tree_store), &it);
        int depth = gtk_tree_path_get_depth(pth);
        int *ix = gtk_tree_path_get_indices(pth);
        int cur = ix ? ix[depth - 1] : 0;
        gtk_tree_path_free(pth);
        GtkTreeIter pit, nit;
        bool have = false;
        if (gtk_tree_model_iter_parent(GTK_TREE_MODEL(p->tree_store), &pit, &it))
            have = gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(p->tree_store), &nit, &pit, cur + 1);
        else
            have = gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(p->tree_store), &nit, nullptr, cur + 1);
        if (have) aRetVal = static_cast<UINT_PTR>(tv_id_from_iter(*p, nit));
        return OK;
    }
    case 'p':
    {
        GtkTreePath *pth = gtk_tree_model_get_path(GTK_TREE_MODEL(p->tree_store), &it);
        int depth = gtk_tree_path_get_depth(pth);
        int *ix = gtk_tree_path_get_indices(pth);
        int cur = ix ? ix[depth - 1] : 0;
        gtk_tree_path_free(pth);
        GtkTreeIter pit, nit;
        bool have = false;
        if (gtk_tree_model_iter_parent(GTK_TREE_MODEL(p->tree_store), &pit, &it))
            have = cur > 0 && gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(p->tree_store), &nit, &pit, cur - 1);
        else
            have = cur > 0 && gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(p->tree_store), &nit, nullptr, cur - 1);
        if (have) aRetVal = static_cast<UINT_PTR>(tv_id_from_iter(*p, nit));
        return OK;
    }
    default:
        aRetVal = static_cast<UINT_PTR>(tv_first_child(*p, &it));
        return OK;
    }
}

FResult GuiControlType::TV_GetParent(UINT_PTR aItemID, UINT_PTR &aRetVal)
{
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    if (!p || !p->tree_store) return FR_FAIL;
    auto f = p->tree_rows.find(aItemID);
    if (f == p->tree_rows.end()) return FR_FAIL;
    GtkTreeIter it, pit;
    if (!tv_iter_from_ref(*p, aItemID, it)) return FR_FAIL;
    aRetVal = gtk_tree_model_iter_parent(GTK_TREE_MODEL(p->tree_store), &pit, &it)
        ? static_cast<UINT_PTR>(tv_id_from_iter(*p, pit)) : 0;
    return OK;
}

FResult GuiControlType::TV_GetPrev(UINT_PTR aItemID, UINT_PTR &aRetVal)
{
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    if (!p || !p->tree_store) return FR_FAIL;
    auto f = p->tree_rows.find(aItemID);
    if (f == p->tree_rows.end()) return FR_FAIL;
    GtkTreeIter it;
    if (!tv_iter_from_ref(*p, aItemID, it)) return FR_FAIL;
    aRetVal = 0;
    GtkTreePath *pth = gtk_tree_model_get_path(GTK_TREE_MODEL(p->tree_store), &it);
    int depth = gtk_tree_path_get_depth(pth);
    int *ix = gtk_tree_path_get_indices(pth);
    int cur = ix ? ix[depth - 1] : 0;
    gtk_tree_path_free(pth);
    GtkTreeIter pit, nit;
    bool have = false;
    if (gtk_tree_model_iter_parent(GTK_TREE_MODEL(p->tree_store), &pit, &it))
        have = cur > 0 && gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(p->tree_store), &nit, &pit, cur - 1);
    else
        have = cur > 0 && gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(p->tree_store), &nit, nullptr, cur - 1);
    if (have) aRetVal = static_cast<UINT_PTR>(tv_id_from_iter(*p, nit));
    return OK;
}

FResult GuiControlType::TV_GetSelection(UINT_PTR &aRetVal)
{
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    if (!p || !p->selection) return FR_FAIL;
    GtkTreeModel *m = nullptr;
    GtkTreeIter it;
    aRetVal = gtk_tree_selection_get_selected(p->selection, &m, &it) ? static_cast<UINT_PTR>(tv_id_from_iter(*p, it)) : 0;
    return OK;
}

FResult GuiControlType::TV_GetText(UINT_PTR aItemID, StrRet &aRetVal)
{
    CTRL_THROW_IF_DESTROYED;
    ControlPeer *p = peer(this);
    if (!p || !p->tree_store) return FR_FAIL;
    auto f = p->tree_rows.find(aItemID);
    if (f == p->tree_rows.end()) return FR_FAIL;
    GtkTreeIter it;
    if (!tv_iter_from_ref(*p, aItemID, it)) return FR_FAIL;
    gchar *v = nullptr;
    gtk_tree_model_get(GTK_TREE_MODEL(p->tree_store), &it, 0, &v, -1);
    std::string str = v ? v : "";
    g_free(v);
    auto t = from_utf8(str.c_str());
    return aRetVal.Copy(t.c_str()) ? OK : FR_FAIL;
}

FResult GuiControlType::TV_SetImageList(UINT_PTR aImageListID, optl<int> aIconType, UINT_PTR &aRetVal)
{
    (void)aImageListID; (void)aIconType; aRetVal = 0; return OK;
}

// Cross-translation-unit bridge for Linux platform code which needs to map
// AutoHotkey's opaque Hwnd values to native GTK objects.
extern "C" GtkWidget *AhkGtkWidgetFromHwnd(UINT_PTR aHwnd)
{
    return to_widget((HWND)aHwnd);
}

extern "C" UINT_PTR AhkGtkHwndFromWidget(GtkWidget *aWidget)
{
    return (UINT_PTR)to_hwnd(aWidget);
}

extern "C" GuiControlType *AhkGtkControlFromHwnd(UINT_PTR aHwnd)
{
    GtkWidget *widget = to_widget((HWND)aHwnd);
    while (widget)
    {
        auto found = s_widget_to_control.find(widget);
        if (found != s_widget_to_control.end()) return found->second;
        widget = gtk_widget_get_parent(widget);
    }
    return nullptr;
}

// Event delivery entry points (declared weak at the top of this file so the
// backend stays usable with a host that provides its own posting mechanism).
// The Linux core lets them land in the queues drained by GtkPump().
extern "C" void AhkGtkQueueGuiEvent(GuiType *aGui, GuiIndexType aControl, USHORT aEvent, UINT_PTR aInfo)
{
    std::lock_guard<std::recursive_mutex> lock(ahk_gtk::s_mutex);
    ahk_gtk::s_gui_events.push_back({ aGui, aControl, aEvent, aInfo });
}

extern "C" void AhkGtkQueueMenuItem(GuiType *aGui, UserMenu *aMenu, UINT aItemID)
{
    std::lock_guard<std::recursive_mutex> lock(ahk_gtk::s_mutex);
    ahk_gtk::s_menu_events.push_back({ aGui, aMenu, aItemID });
}

// CaretGetPos support: returns the caret (insertion point) of the focused
// GtkEntry/GtkTextView in the focused top-level window of this process's GTK
// backend, in screen coordinates.  Other windows have no caret protocol on
// X11 and yield false (docs: "cannot be determined").
extern "C" bool AhkGtkCaretGetPos(int &aScreenX, int &aScreenY)
{
    // Reuse the GDK display the GTK backend already holds; opening a second
    // X connection can fail (EAGAIN) once GTK is up.
    GdkDisplay *gdisp = gdk_display_get_default();
    if (!gdisp)
        return false;

    // Determine the focused GTK window: prefer GDK's active window, falling
    // back to any of our windows whose focus widget holds a caret-capable
    // control (there is no window manager under Xvfb, so GDK's "active
    // window" may be unset).
    GuiType *gui = nullptr;
    GdkWindow *focus = gdk_screen_get_active_window(gdk_display_get_default_screen(gdisp));
    if (focus)
    {
        guint32 xid = gdk_x11_window_get_xid(focus);
        gui = GuiType::FindGui((HWND)(UINT_PTR)xid, true);
    }
    if (!gui)
    {
        for (auto &kv : ahk_gtk::s_guis)
        {
            GuiPeer *p = kv.second.get();
            if (!p || !p->window || !GTK_IS_WINDOW(p->window))
                continue;
            GtkWidget *fw = gtk_window_get_focus(GTK_WINDOW(p->window));
            for (GtkWidget *w = fw; w; w = gtk_widget_get_parent(w))
                if (GTK_IS_ENTRY(w) || GTK_IS_TEXT_VIEW(w))
                {
                    gui = kv.first;
                    break;
                }
            if (gui)
                break;
        }
    }
    if (!gui)
        return false;
    GuiPeer *gpeer = peer(gui);
    if (!gpeer || !gpeer->window || !GTK_IS_WINDOW(gpeer->window))
        return false;
    GtkWidget *focus_widget = gtk_window_get_focus(GTK_WINDOW(gpeer->window));
    if (!focus_widget)
        return false;
    // Walk up to the control itself (the focus may be on a child widget).
    GtkWidget *entry = nullptr;
    for (GtkWidget *w = focus_widget; w; w = gtk_widget_get_parent(w))
    {
        if (GTK_IS_ENTRY(w) || GTK_IS_TEXT_VIEW(w))
        {
            entry = w;
            break;
        }
    }
    if (!entry)
        return false;

    int caret_x = 0, caret_y = 0;
    if (GTK_IS_ENTRY(entry))
    {
        // GTK3: cursor position comes from the entry's Pango layout.
        // The layout offset plus the cursor position of the layout gives
        // the caret in widget coordinates.
        int lo_x = 0, lo_y = 0;
        gtk_entry_get_layout_offsets(GTK_ENTRY(entry), &lo_x, &lo_y);
        PangoLayout *layout = gtk_entry_get_layout(GTK_ENTRY(entry));
        // pango cursor positions are byte offsets; convert the character
        // count to a byte offset in the UTF-8 entry text.
        const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
        int cursor_index = gtk_entry_get_text_length(GTK_ENTRY(entry));
        int byte_index = cursor_index ? (int)(g_utf8_offset_to_pointer(text, cursor_index) - text) : 0;
        PangoRectangle strong, weak;
        pango_layout_get_cursor_pos(layout, byte_index, &strong, &weak);
        caret_x = lo_x + PANGO_PIXELS(strong.x);
        caret_y = lo_y + PANGO_PIXELS(strong.y);
        // Convert widget-internal coords to the toplevel window, then to screen.
        int tx = 0, ty = 0;
        gtk_widget_translate_coordinates(entry, gpeer->window, caret_x, caret_y, &tx, &ty);
        GdkWindow *gdkwin = gtk_widget_get_window(gpeer->window);
        if (gdkwin)
        {
            gint wx = 0, wy = 0;
            gdk_window_get_origin(gdkwin, &wx, &wy);
            tx += wx;
            ty += wy;
        }
        caret_x = tx;
        caret_y = ty;
    }
    else if (GTK_IS_TEXT_VIEW(entry))
    {
        // Text view: caret = cursor mark position in buffer coordinates,
        // converted to window coordinates.
        GtkTextView *tv = GTK_TEXT_VIEW(entry);
        GtkTextBuffer *buf = gtk_text_view_get_buffer(tv);
        GtkTextIter iter;
        gtk_text_buffer_get_iter_at_mark(buf, &iter, gtk_text_buffer_get_insert(buf));
        GdkRectangle rect;
        gtk_text_view_get_iter_location(tv, &iter, &rect);
        gint bx = 0, by = 0;
        gtk_text_view_buffer_to_window_coords(tv, GTK_TEXT_WINDOW_TEXT, rect.x, rect.y, &bx, &by);
        int tx = 0, ty = 0;
        gtk_widget_translate_coordinates(entry, gpeer->window, bx, by, &tx, &ty);
        GdkWindow *gdkwin = gtk_widget_get_window(gpeer->window);
        if (gdkwin)
        {
            gint wx = 0, wy = 0;
            gdk_window_get_origin(gdkwin, &wx, &wy);
            tx += wx;
            ty += wy;
        }
        caret_x = tx;
        caret_y = ty;
    }
    else
        return false;

    aScreenX = caret_x;
    aScreenY = caret_y;
    return true;
}

// Win32 dialog procedures are exported as inert compatibility entry points.
// GTK delivers all corresponding events through GObject signals above.
LRESULT CALLBACK GuiWindowProc(HWND, UINT, WPARAM, LPARAM) { return 0; }
LRESULT CALLBACK TabWindowProc(HWND, UINT, WPARAM, LPARAM) { return 0; }
INT_PTR CALLBACK TabDialogProc(HWND, UINT, WPARAM, LPARAM) { return 0; }

HBRUSH CreateTabDialogBrush(HWND, HDC) { return nullptr; }

int CALLBACK LV_GeneralSort(LPARAM a, LPARAM b, LPARAM order)
{
    int result = a < b ? -1 : a > b ? 1 : 0;
    return order ? result : -result;
}

int CALLBACK LV_Int32Sort(LPARAM a, LPARAM b, LPARAM ascending)
{
    return ascending ? (a < b ? -1 : a > b ? 1 : 0)
                     : (a > b ? -1 : a < b ? 1 : 0);
}
