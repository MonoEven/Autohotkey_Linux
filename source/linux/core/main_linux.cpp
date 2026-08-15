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
#include <cstdio>
#include <cstdlib>
#include <cwchar>

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::printf("AutoHotkey Linux (v2 port)\nUsage: ahk_core script.ahk [args...]\n");
		return 1;
	}

	wchar_t wpath[4096];
	if (mbstowcs(wpath, argv[1], 4095) == (size_t)-1)
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

	LineNumberType load_result = g_script.LoadFromFile(wpath);
	if (load_result == LOADING_FAILED)
		return 1;
	if (!load_result)
		return 0;

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
	g_script.ExitApp(exec_result == FAIL ? EXIT_ERROR : EXIT_EXIT);
	return 0;
}