#include "../../stdafx.h"
#include "../../globaldata.h"
#include "core_debugger_linux.h"

#ifdef CONFIG_DEBUGGER

#include <csignal>
#include <cstdio>
#include <cstring>

static char sDebuggerHost[256] = "";
static char sDebuggerPort[16] = "9000";
static volatile sig_atomic_t sReconnectRequested = 0;

static void LinuxDebuggerReconnectSignal(int)
{
	sReconnectRequested = 1;
}

void LinuxDebuggerConfigure(const char *aHost, const char *aPort)
{
	snprintf(sDebuggerHost, sizeof(sDebuggerHost), "%s", aHost ? aHost : "");
	snprintf(sDebuggerPort, sizeof(sDebuggerPort), "%s", aPort && *aPort ? aPort : "9000");
	if (!sDebuggerHost[0])
		return;
	struct sigaction action {};
	action.sa_handler = LinuxDebuggerReconnectSignal;
	sigemptyset(&action.sa_mask);
	action.sa_flags = SA_RESTART;
	sigaction(SIGUSR2, &action, nullptr);
}

static bool LinuxDebuggerConnectAndBreak()
{
	if (!sDebuggerHost[0] || g_Debugger.IsConnected())
		return false;
	if (g_Debugger.Connect(sDebuggerHost, sDebuggerPort) != DEBUGGER_E_OK)
	{
		fprintf(stderr, "AutoHotkey Linux: failed to connect to DBGp IDE at %s:%s.\n",
			sDebuggerHost, sDebuggerPort);
		return false;
	}
	// The IDE receives <init>, configures features/breakpoints and decides
	// whether to run, detach or stop. This blocks only in normal thread context.
	g_Debugger.Break();
	return true;
}

bool LinuxDebuggerInitialConnect()
{
	return LinuxDebuggerConnectAndBreak();
}

bool LinuxDebuggerReconnectPending()
{
	return sReconnectRequested && sDebuggerHost[0];
}

void LinuxDebuggerPump()
{
	if (g_Debugger.IsConnected())
	{
		if (g_Debugger.HasPendingCommand())
			g_Debugger.ProcessCommands();
		else if (g_Debugger.ConnectionClosed())
		{
			// An IDE which crashes while the script is running/idle must not leave
			// the debuggee blocked or terminate it. Disconnect clears protocol
			// buffers/state; SIGUSR2 may attach a new listener later.
			g_Debugger.Disconnect();
			fprintf(stderr, "AutoHotkey DBGp: IDE connection closed; script continues detached.\n");
		}
	}
	// A reconnect signal received while a healthy IDE is still attached is a
	// no-op, not an armed reconnect for some future detach.
	if (sReconnectRequested && g_Debugger.IsConnected())
		sReconnectRequested = 0;
	if (sReconnectRequested && !g_Debugger.IsConnected())
	{
		sReconnectRequested = 0;
		LinuxDebuggerConnectAndBreak();
	}
}

#endif
