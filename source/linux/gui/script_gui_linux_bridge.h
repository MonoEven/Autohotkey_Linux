#pragma once

/*
 * Optional bridge declarations for script_gui_linux.cpp.
 * Include this from Linux-only platform files after AutoHotkey core types and
 * GTK3 have been declared.
 */

#include <gtk/gtk.h>
#include <stdint.h>

class GuiType;
class GuiControlType;
class UserMenu;

extern "C" GtkWidget *AhkGtkWidgetFromHwnd(uintptr_t hwnd);
extern "C" uintptr_t AhkGtkHwndFromWidget(GtkWidget *widget);
extern "C" GuiControlType *AhkGtkControlFromHwnd(uintptr_t hwnd);

/*
 * Define these in the Linux application/event-loop translation unit only when
 * POST_AHK_GUI_ACTION and POST_AHK_USER_MENU are not available as macros.
 * Use the exact AutoHotkey typedefs in the definitions:
 *
 * extern "C" void AhkGtkQueueGuiEvent(
 *     GuiType *gui, GuiIndexType control, USHORT event, UINT_PTR info);
 *
 * extern "C" void AhkGtkQueueMenuItem(
 *     GuiType *gui, UserMenu *menu, UINT item_id);
 */
