// Linux entry point for the AutoHotkey core interpreter.
//
// It uses the real Script/LoadFromFile/AutoExecSection pipeline.  GUI/hotkey
// platform functions are currently stubs; simple scripts using MsgBox print to
// the console via the Linux MessageBox fallback.

#include "../../stdafx.h"
#include "../../globaldata.h"
#include "../../script.h"
#include "../../application.h"
#include "../../hotkey.h"
#include "../../SimpleHeap.h"
#include "core_timer_linux.h"
#include "core_hotkey_linux.h"
#include "../gui/script_gui_linux.h"
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <clocale>
#include <csignal>
#include <unistd.h>

// Reload support (see core_platform_stubs.cpp): the SIGTERM flag is turned
// into ExitApp(EXIT_RELOAD) by the wait loops.
extern "C" void LinuxInstallRestartHandler();
extern "C" bool LinuxRestartRequested();

int main(int argc, char** argv)
{
	// Honour the environment locale so that wcstombs/mbstowcs can convert
	// non-ASCII text (UTF-8) instead of failing in the default "C" locale.
	setlocale(LC_CTYPE, "");

	LinuxInstallRestartHandler();

	// Reload protocol: "/restart /script <path> [/pid <pid>]" (launched by
	// BIF_Linux_Reload).  After the script has loaded successfully the new
	// instance signals the old process so it exits through EXIT_RELOAD.
	int script_arg = 1;
	pid_t restart_old_pid = 0;
	if (argc > 1 && !strcmp(argv[1], "/restart"))
	{
		for (int i = 2; i + 1 < argc; i += 2)
		{
			if (!strcmp(argv[i], "/script"))
				script_arg = i + 1;
			else if (!strcmp(argv[i], "/pid"))
				restart_old_pid = (pid_t)atol(argv[i + 1]);
		}
		if (script_arg >= argc)
		{
			std::fprintf(stderr, "AutoHotkey Linux: invalid /restart arguments.\n");
			return 1;
		}
	}

	if (argc < 2)
	{
		std::printf("AutoHotkey Linux (v2 port)\nUsage: ahk_core script.ahk [args...]\n");
		return 1;
	}

	// Version/help switches (Linux convention; the Windows build has no
	// --version either, but installers and users expect one here).
	if (!strcmp(argv[1], "--version") || !strcmp(argv[1], "-v"))
	{
		std::printf("AutoHotkey v2.0.26 Linux port (X11/Wayland)\n");
		return 0;
	}
	if (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))
	{
		std::printf("AutoHotkey Linux (v2 port)\n"
			"Usage: ahk_core script.ahk [args...]\n"
			"  --version, -v   print the version and exit\n"
			"  --help, -h      show this help\n"
			"Displays: X11 when DISPLAY is set (incl. XWayland), native\n"
			"Wayland otherwise (xdg-shell + virtual input protocols).\n");
		return 0;
	}

	wchar_t wpath[4096];
	if (mbstowcs(wpath, argv[script_arg], 4095) == (size_t)-1)
	{
		std::fprintf(stderr, "AutoHotkey Linux: invalid script path encoding.\n");
		return 1;
	}
	wpath[4095] = L'\0';

	// Minimal early-init equivalent of _tWinMain/EarlyAppInit().
	UpdateWorkingDir();
	g_WorkingDirOrig = SimpleHeap::Alloc(g_WorkingDir.GetString());
	global_init(*g);
	Object::CreateRootPrototypes();
	g_script.mIsReadyToExecute = true;

	// Script::Init() (which normally sets mFileSpec/mFileDir/mFileName) is not
	// used on Linux; replicate its path handling so that A_ScriptName,
	// A_ScriptDir, A_ScriptFullPath and the startup working directory work.
	{
		TCHAR full_path[T_MAX_PATH];
		DWORD full_len = GetFullPathName(wpath, _countof(full_path), full_path, nullptr);
		if (!full_len)
			return 1; // Invalid/too-long script path.
		g_script.mFileSpec = SimpleHeap::Alloc(full_path);
		LPTSTR filename_marker;
		if ((filename_marker = _tcsrchr(full_path, '/')))
		{
			g_script.mFileDir = SimpleHeap::Alloc(full_path, (size_t)(filename_marker - full_path));
			++filename_marker;
		}
		else
		{
			g_script.mFileDir = g_WorkingDirOrig;
			filename_marker = full_path;
		}
		g_script.mFileName = SimpleHeap::Alloc(filename_marker);
	}

	// A_AhkPath: the executable's own path (Script::Init normally fills mOurEXE).
	{
		char narrow[4096];
		ssize_t n = readlink("/proc/self/exe", narrow, sizeof(narrow) - 1);
		if (n > 0)
		{
			narrow[n] = '\0';
			wchar_t wide[4096];
			if (mbstowcs(wide, narrow, 4095) != (size_t)-1)
			{
				wide[4095] = L'\0';
				g_script.mOurEXE = SimpleHeap::Alloc(wide);
				LPTSTR slash = _tcsrchr(wide, '/');
				if (slash)
					g_script.mOurEXEDir = SimpleHeap::Alloc(wide, (size_t)(slash - wide));
			}
		}
	}

	// A_Args: the command-line parameters after the script path (mirrors
	// _tWinMain in AutoHotkey.cpp).  In /restart mode the reload protocol
	// arguments are consumed here and must not leak into A_Args.
	int args_start = (script_arg != 1) ? argc : 2;
	if (Var *var = g_script.FindOrAddVar(_T("A_Args"), 6, VAR_DECLARE_GLOBAL))
	{
		TCHAR *wide_args[256];
		int wide_count = 0;
		for (int i = args_start; i < argc && wide_count < 255; ++i)
		{
			wchar_t *wa = (wchar_t *)malloc((strlen(argv[i]) + 1) * sizeof(wchar_t));
			mbstowcs(wa, argv[i], strlen(argv[i]) + 1);
			wide_args[wide_count++] = wa;
		}
		auto args = Array::FromArgV(wide_args, wide_count);
		for (int i = 0; i < wide_count; ++i)
			free(wide_args[i]);
		if (args)
			var->AssignSkipAddRef(args);
	}

	LineNumberType load_result = g_script.LoadFromFile(wpath);
	if (load_result == LOADING_FAILED)
		return 1;
	if (!load_result)
		return 0;

	// Reload: the script loaded successfully, so tell the old process to
	// exit (it does so through EXIT_RELOAD; OnExit runs with ExitReason
	// "Reload").  If loading had failed we would have returned above
	// without signalling, keeping the old script alive (upstream
	// semantics).
	if (restart_old_pid > 0)
		kill(restart_old_pid, SIGTERM);

	// LoadFromFile() resets mIsReadyToExecute to false.  On Windows the ready
	// flag is set by InitForExecution() after the windows/tray icon are created;
	// on Linux we have no windows yet, so mark the script ready right before
	// running the auto-execute section.  This makes Script::ExitApp() use
	// mPendingExitCode (0 for a clean run) instead of CRITICAL_ERROR (2).
	g_script.mIsReadyToExecute = true;

	Hotkey::ManifestAllHotkeysHotstringsHooks();
	ResultType exec_result = g_script.AutoExecSection();
	if (exec_result == FAIL && !g_script.mPendingExitCode)
		g_script.mPendingExitCode = CRITICAL_ERROR;
	// Establish the hotkey grabs immediately (check0818 P1-1: do not wait
	// for the first X event to reconcile) when the script has hotkeys.
	if (Hotkey::sHotkeyCount)
		LinuxReconcileHotkeyGrabs();
	// If the script is persistent, has enabled timers, or has a visible
	// GUI window, keep running the Linux main loop (which fires due timers,
	// pumps GTK events and waits for the window to close) until ExitApp is
	// requested.  GUI windows alone keep a script alive, like the Windows
	// message pump.
	if (!g_script.mPendingExitCode && (g_script.IsPersistent() || g_script.mTimerEnabledCount || ahk_gtk::GuiWindowsVisible()))
		LinuxRunMainLoop();
	if (LinuxRestartRequested())
		g_script.ExitApp(EXIT_RELOAD);
	else
		g_script.ExitApp(exec_result == FAIL ? EXIT_ERROR : EXIT_EXIT);
	return 0;
}