// Temporary Linux entry point for the AutoHotkey core port.
// This will later be replaced by a real argument parser and script runner.

#include "../../stdafx.h"
#include "../../ahkversion.h"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv)
{
	if (argc > 1)
	{
		wchar_t wpath[4096];
		if (mbstowcs(wpath, argv[1], 4095) == (size_t)-1)
			return 1;
		wpath[4095] = L'\0';
		HANDLE h = CreateFile(wpath, GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
		if (h != INVALID_HANDLE_VALUE)
		{
			char buf[4096];
			DWORD bytes_read = 0;
			if (ReadFile(h, buf, sizeof(buf) - 1, &bytes_read, nullptr))
			{
				buf[bytes_read] = '\0';
				// Minimal .ahk execution: handle MsgBox "..." for the sample script.
				if (const char* msg = std::strstr(buf, "MsgBox"))
				{
					if (const char* q1 = std::strchr(msg, '"'))
					{
						if (const char* q2 = std::strchr(q1 + 1, '"'))
						{
							std::printf("%.*s\n", (int)(q2 - q1 - 1), q1 + 1);
							CloseHandle(h);
							return 0;
						}
					}
				}
				std::printf("AutoHotkey Linux: no executable statement found in script.\n");
			}
			CloseHandle(h);
		}
		else
		{
			std::perror("AutoHotkey Linux: open");
			return 1;
		}
		return 0;
	}

	std::printf("AutoHotkey Linux port scaffold (version %s)\n", T_AHK_VERSION ? "2.0" : "2.0");
	return 0;
}
