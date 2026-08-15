// Temporary Linux entry point for the AutoHotkey core port.
// This will later be replaced by a real argument parser and script runner.

#include "../../stdafx.h"
#include "../../ahkversion.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <map>
#include <algorithm>

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
				std::string script(buf, bytes_read);
				std::map<std::string, std::string> vars;
				size_t pos = 0;
				while (pos < script.size())
				{
					size_t eol = script.find('\n', pos);
					if (eol == std::string::npos)
						eol = script.size();
					std::string line = script.substr(pos, eol - pos);
					pos = eol + 1;
					// Trim whitespace.
					size_t start = line.find_first_not_of(" \t\r");
					if (start == std::string::npos)
						continue;
					size_t end = line.find_last_not_of(" \t\r");
					line = line.substr(start, end - start + 1);
					if (line.empty() || line[0] == '#')
						continue;
					// Variable assignment: name := "value"
					size_t assign = line.find(":=");
					if (assign != std::string::npos)
					{
						std::string name = line.substr(0, assign);
						name.erase(name.find_last_not_of(" \t") + 1);
						std::string value = line.substr(assign + 2);
						value.erase(0, value.find_first_not_of(" \t"));
						if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
							value = value.substr(1, value.size() - 2);
						vars[name] = value;
						continue;
					}
					// MsgBox
					if (line.rfind("MsgBox", 0) == 0)
					{
						std::string arg = line.substr(6);
						arg.erase(0, arg.find_first_not_of(" \t"));
						if (arg.size() >= 2 && arg.front() == '"' && arg.back() == '"')
							arg = arg.substr(1, arg.size() - 2);
						else if (vars.find(arg) != vars.end())
							arg = vars[arg];
						std::printf("%s\n", arg.c_str());
					}
				}
				CloseHandle(h);
				return 0;
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
