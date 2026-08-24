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
#include "core_debugger_linux.h"
#include "core_hotkey_linux.h"
#include "core_pack_linux.h"
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
// Environment diagnostic (ahk_core --diag): prints a one-shot snapshot for
// issue reports (check_detail0821 §12).  Implemented in core_platform_stubs.cpp.
extern "C" int LinuxRunDiagnostic();
// Parity query (ahk_core --parity <FuncName>): prints the parity level + note
// (check_detail0821 §13).  Implemented in core_parity_linux.cpp.
extern "C" int LinuxRunParity(const char *aName);

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
	bool restart_mode = argc > 1 && !strcmp(argv[1], "/restart");
	char debugger_host[256] = "";
	char debugger_port[16] = "9000";
	if (restart_mode)
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
#ifdef CONFIG_DEBUGGER
	else if (argc > 1 && (!strcmp(argv[1], "--debug") || !strncmp(argv[1], "--debug=", 8)))
	{
		const char *spec = nullptr;
		if (argv[1][7] == '=')
		{
			spec = argv[1] + 8;
			script_arg = 2;
		}
		else if (argc >= 4)
		{
			spec = argv[2];
			script_arg = 3;
		}
		if (!spec || !*spec || script_arg >= argc)
		{
			std::fprintf(stderr, "AutoHotkey Linux: --debug needs host:port and a script path.\n");
			return 1;
		}
		const char *colon = strrchr(spec, ':');
		if (colon)
		{
			size_t host_len = (size_t)(colon - spec);
			if (!host_len || host_len >= sizeof(debugger_host) || !colon[1])
				return 1;
			memcpy(debugger_host, spec, host_len);
			debugger_host[host_len] = '\0';
			snprintf(debugger_port, sizeof(debugger_port), "%s", colon + 1);
		}
		else
			snprintf(debugger_host, sizeof(debugger_host), "%s", spec);
	}
#endif

	// The script path comes from the command line, or from the packed
	// payload when no script argument was given (check_detail0821 §5-M6 / R4).
	const char *run_path = nullptr;
	char packed_tmp[80] = "";
	if (argc < 2)
	{
		// A packed binary (ahk_core --pack outfile script.ahk) runs with no
		// script argument: extract the embedded script to a temp file and
		// load it.
		if (LinuxIsPacked())
		{
			char sbuf[4 << 20];
			size_t slen = LinuxExtractPackedScript(sbuf, sizeof(sbuf));
			if (slen > 0)
			{
				snprintf(packed_tmp, sizeof(packed_tmp), "/tmp/ahk_packed_%ld.ahk", (long)getpid());
				FILE *f = fopen(packed_tmp, "wb");
				if (f)
				{
					fwrite(sbuf, 1, slen, f);
					fclose(f);
					g_LinuxPacked = true;
					run_path = packed_tmp;
				}
			}
			if (!run_path)
			{
				std::fprintf(stderr, "AutoHotkey Linux: packed payload missing or corrupt.\n");
				return 1;
			}
		}
		else
		{
			std::printf("AutoHotkey Linux (v2 port)\nUsage: ahk_core script.ahk [args...]\n");
			return 1;
		}
	}
	else
		run_path = argv[script_arg];

	// Version/help switches (Linux convention; the Windows build has no
	// --version either, but installers and users expect one here).
	if (argc > 1 && (!strcmp(argv[1], "--version") || !strcmp(argv[1], "-v")))
	{
		std::printf("AutoHotkey v2.0.26 Linux port (X11/Wayland)\n");
		return 0;
	}
	if (argc > 1 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")))
	{
		std::printf("AutoHotkey Linux (v2 port)\n"
			"Usage: ahk_core script.ahk [args...]\n"
			"  --version, -v      print the version and exit\n"
#ifdef CONFIG_DEBUGGER
			"  --debug host:port script.ahk [args...]  connect to a DBGp IDE and break before auto-exec\n"
#endif
			"  --diag             print an environment diagnostic and exit\n"
			"  --parity FuncName  print a function's parity level (P1-P4) + note and exit\n"
			"  --help, -h         show this help\n"
			"Displays: X11 when DISPLAY is set (incl. XWayland), native\n"
			"Wayland otherwise (xdg-shell + virtual input protocols).\n");
		return 0;
	}
	if (argc > 1 && !strcmp(argv[1], "--diag"))
		return LinuxRunDiagnostic();
	if (argc > 1 && !strcmp(argv[1], "--parity") && argc >= 3)
		return LinuxRunParity(argv[2]);
	if (argc > 1 && !strcmp(argv[1], "--parity"))
	{
		std::fprintf(stderr, "AutoHotkey Linux: --parity needs a function name.\n");
		return 1;
	}
	// Packed-script support (check_detail0821 §5-M6 / R4): ahk_core --pack
	// outfile script.ahk embeds the script behind the runtime; the produced
	// executable runs it when invoked without a script argument.
	if (argc > 1 && !strcmp(argv[1], "--pack"))
	{
		if (argc != 4)
		{
			std::fprintf(stderr, "AutoHotkey Linux: --pack needs an output file and a script.\n");
			return 1;
		}
		if (!LinuxPackExecutable(argv[2], argv[3]))
			return 1;
		std::printf("packed: %s\n", argv[2]);
		return 0;
	}

	wchar_t wpath[4096];
	if (mbstowcs(wpath, run_path, 4095) == (size_t)-1)
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
	int args_start = restart_mode ? argc : script_arg + 1;
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

#ifdef CONFIG_DEBUGGER
	if (debugger_host[0])
	{
		LinuxDebuggerConfigure(debugger_host, debugger_port);
		if (!LinuxDebuggerInitialConnect())
			return 1;
	}
#endif

	Hotkey::ManifestAllHotkeysHotstringsHooks();
	// LSan cleanliness: SimpleHeap keeps oversized allocations (e.g. a
	// FileRead of a large file) in a static side list so they stay
	// reachable for the whole process (the pool is process-lived by
	// design, exactly like the block pool).  No exit hook is needed: the
	// OS reclaims them when the process terminates.
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