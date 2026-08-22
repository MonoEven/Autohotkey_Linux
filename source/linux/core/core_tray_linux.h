// core_tray_linux.h -- tray icon / notification API (check_detail0821 §5-M5).
#pragma once
#include "../../stdafx.h"

// TrayTip -> org.freedesktop.Notifications.Notify.  Never a critical error.
bool LinuxTrayNotify(const wchar_t *aTitle, const wchar_t *aText);

// TraySetIcon: register a StatusNotifierItem (org.kde.StatusNotifierItem +
// com.canonical.dbusmenu) with the desktop's watcher and set its icon name.
// Best effort: no watcher / no session bus is a silent no-op.  aIconFile is
// used as a themed icon name when it is one; a path is reduced to its
// basename without extension.
bool LinuxTraySetIcon(const wchar_t *aIconFile);

// Pump the SNI D-Bus service (serve properties / dbusmenu requests).  Called
// from LinuxInputBackendDispatch on every main-loop pass.
void LinuxTrayDispatch();
