// Linux implementation of the Sound* built-ins (SoundGetMute/SoundGetVolume/
// SoundGetName/SoundGetInterface + SoundSetMute/SoundSetVolume).
//
// Upstream (lib/sound.cpp) uses the Windows Core Audio (IMMDevice) COM API.
// Linux has no such API, so this backend drives the system mixer through
// `pactl` (PulseAudio / PipeWire, both provide pactl) with `amixer` (ALSA)
// as a fallback, following the same external-tool strategy used by
// SoundPlay (paplay/aplay) elsewhere in this port.
//
// Semantics kept per docs-v2:
//   SoundGetMute(Component, Device)   -> 1/0
//   SoundGetVolume(Component, Device) -> 0-100
//   SoundGetName(Component, Device)   -> device display name
//   SoundSetMute(NewSetting, ...)     -> NewSetting 1/0/-1 (toggle)
//   SoundSetVolume(NewSetting, ...)   -> NewSetting 0-100 or +/-N (relative)
//   SoundGetInterface(IID, ...)       -> COM interface pointer; Linux has no
//                                        COM audio interfaces, returns 0.
// The Component/Device arguments are accepted for compatibility; when given,
// they are matched against the sink/source names/indices reported by pactl.
// If no mixer tool or audio server is available the functions raise OSError
// (matching SoundPlay's behaviour on missing players).

#include "../../stdafx.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../script_func_impl.h"

#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>

// narrow (UTF-8) -> wide (SimpleHeap-backed, persistent).
static LPTSTR LinuxSoundWide(const std::string &aNarrow)
{
	if (aNarrow.empty())
		return _T("");
	std::wstring w;
	size_t len = mbstowcs(nullptr, aNarrow.c_str(), 0);
	if (len == (size_t)-1)
		return _T("");
	w.resize(len);
	mbstowcs(&w[0], aNarrow.c_str(), len + 1);
	LPTSTR p = (LPTSTR)SimpleHeap::Alloc((w.size() + 1) * sizeof(TCHAR));
	tmemcpy(p, w.c_str(), w.size() + 1);
	return p;
}

// wide -> narrow (UTF-8).
static std::string LinuxSoundNarrow(const wchar_t *aWide)
{
	std::string out;
	if (!aWide)
		return out;
	size_t len = wcstombs(nullptr, aWide, 0);
	if (len == (size_t)-1)
		return out;
	out.resize(len);
	wcstombs(&out[0], aWide, len + 1);
	return out;
}

// Run a command and capture its stdout into aOut (best effort).
static int LinuxRunCapture(const std::string &aCommand, std::string &aOut)
{
	aOut.clear();
	FILE *p = popen(aCommand.c_str(), "r");
	if (!p)
		return -1;
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), p)) > 0)
		aOut.append(buf, n);
	return pclose(p) == 0 ? 0 : -1;
}

// Pick the mixer backend; returns the pactl/amixer binary path or "".
static const char *LinuxSoundTool()
{
	if (access("/usr/bin/pactl", X_OK) == 0)
		return "/usr/bin/pactl";
	if (access("/usr/bin/amixer", X_OK) == 0)
		return "/usr/bin/amixer";
	return nullptr;
}

// Parse "Name", "Name:Index", or "Index" (1-based) into name+index.
static void LinuxSoundParseDevice(const wchar_t *aSpec, std::string &aName, int &aIndex)
{
	aName.clear();
	aIndex = 0;
	if (!aSpec || !*aSpec)
		return;
	std::string s = LinuxSoundNarrow(aSpec);
	auto colon = s.rfind(':');
	if (colon != std::string::npos && colon + 1 < s.size())
	{
		bool numeric = true;
		for (size_t i = colon + 1; i < s.size(); ++i)
			if (s[i] < '0' || s[i] > '9')
			{
				numeric = false;
				break;
			}
		if (numeric)
		{
			aName = s.substr(0, colon);
			aIndex = atoi(s.c_str() + colon + 1);
			return;
		}
	}
	if (!s.empty())
	{
		bool numeric = true;
		for (char c : s)
			if (c < '0' || c > '9')
			{
				numeric = false;
				break;
			}
		if (numeric)
		{
			aIndex = atoi(s.c_str());
			return;
		}
	}
	aName = s;
}

// Extract a number like "65%" from a line of pactl/amixer output.
static bool LinuxSoundParsePercent(const std::string &aLine, int &aValue)
{
	auto pct = aLine.find('%');
	if (pct == std::string::npos)
		return false;
	size_t start = pct;
	while (start > 0 && (isdigit((unsigned char)aLine[start - 1]) || aLine[start - 1] == ' ' || aLine[start - 1] == '[' || aLine[start - 1] == ':'))
		--start;
	int v = atoi(aLine.c_str() + start);
	if (v < 0)
		v = 0;
	if (v > 100)
		v = 100;
	aValue = v;
	return true;
}

// Shell-quote a string for use in a command line.
static std::string LinuxSoundQuote(const std::string &s)
{
	std::string q = "'";
	for (char c : s)
	{
		if (c == '\'')
			q += "'\\''";
		else
			q += c;
	}
	q += "'";
	return q;
}

// ------------------------------------------------------------- BIF_Sound --

BIF_DECL(BIF_Sound)
{
	// Which of the six Sound functions called us (upstream FID scheme).
	int callee = _f_callee_id; // FID_SoundGetVolume..FID_SoundSetMute
	bool is_set = callee == FID_SoundSetVolume || callee == FID_SoundSetMute;

	// NewSetting for the Set variants (param 0), like upstream.
	LPTSTR new_setting = nullptr;
	TCHAR setting_buf[64];
	int param_base = 0;
	if (is_set)
	{
		new_setting = ParamIndexToString(0, setting_buf);
		if (!IsNumeric(new_setting, TRUE, FALSE, TRUE))
			_f_throw_param(0);
		param_base = 1;
	}

	// Component (param 1 for Set, param 0 otherwise); Device follows.
	// Both are optional; use the optional-parameter helpers so omitted
	// arguments read as "" instead of indexing past aParam.
	TCHAR comp_buf[256], dev_buf[256];
	LPTSTR comp = ParamIndexToOptionalString(param_base, comp_buf);
	LPTSTR dev = ParamIndexToOptionalString(param_base + 1, dev_buf);

	if (callee == FID_SoundGetInterface)
	{
		// Linux has no COM audio interfaces; return 0 (null) per the docs'
		// "interface not found" behaviour.
		aResultToken.SetValue((__int64)0);
		return;
	}

	const char *tool = LinuxSoundTool();
	if (!tool)
	{
		aResultToken.Error(_T("No audio mixer tool (pactl/amixer) is installed."), _T(""), ErrorPrototype::OS);
		return;
	}

	std::string name, out;
	int index = 0;
	LinuxSoundParseDevice(dev, name, index);

	bool use_amixer = strstr(tool, "amixer") != nullptr;

	if (use_amixer)
	{
		// ALSA fallback: only the default mixer control is supported.
		//   amixer sget Master  -> "Mono: Playback 65 [65%] [on]"
		std::string cmd = tool;
		cmd += " sget Master";
		if (LinuxRunCapture(cmd, out) != 0)
		{
			aResultToken.Error(_T("The sound device could not be accessed."), _T(""), ErrorPrototype::OS);
			return;
		}
		if (callee == FID_SoundGetName)
		{
			aResultToken.SetValue(_T("Master"));
			return;
		}
		int val = 0;
		LinuxSoundParsePercent(out, val);
		bool on = out.find("[on]") != std::string::npos;
		if (is_set)
		{
			if (callee == FID_SoundSetMute)
			{
				int setting = ATOI(new_setting);
				std::string scmd = tool;
				scmd += setting ? " sset Master mute" : " sset Master unmute";
				if (LinuxRunCapture(scmd, out) != 0)
					aResultToken.Error(_T("The sound setting could not be changed."), _T(""), ErrorPrototype::OS);
				return;
			}
			int setting = ATOI(new_setting);
			if (*new_setting == '+' || *new_setting == '-')
				setting = val + setting;
			if (setting < 0)
				setting = 0;
			if (setting > 100)
				setting = 100;
			std::string scmd = tool;
			scmd += " sset Master " + std::to_string(setting) + "%";
			if (LinuxRunCapture(scmd, out) != 0)
				aResultToken.Error(_T("The sound setting could not be changed."), _T(""), ErrorPrototype::OS);
			return;
		}
		if (callee == FID_SoundGetMute)
			aResultToken.SetValue(on ? 1 : 0);
		else if (callee == FID_SoundGetVolume)
			aResultToken.SetValue(val);
		return;
	}

	// ---- pactl backend (PulseAudio / PipeWire) ----
	// List sinks (playback devices) and sources (recording devices).
	std::string list_cmd = tool;
	list_cmd += " list sinks";
	std::string sinks;
	LinuxRunCapture(list_cmd, sinks);
	std::string sources;
	list_cmd = tool;
	list_cmd += " list sources";
	LinuxRunCapture(list_cmd, sources);

	// Choose sink/source section based on component name heuristics:
	// "Line in"/"Mic" -> source; anything else -> sink.
	std::string section = sinks;
	std::string section_name = "sink";
	if (comp && *comp)
	{
		std::string comp_n = LinuxSoundNarrow(comp);
		if (comp_n.find("in") != std::string::npos || comp_n.find("ic") != std::string::npos
			|| comp_n.find("ord") != std::string::npos)
		{
			section = sources;
			section_name = "source";
		}
	}

	if (section.empty())
	{
		aResultToken.Error(_T("The sound device could not be accessed."), _T(""), ErrorPrototype::OS);
		return;
	}

	// Find the target sink/source name: the default when no device was
	// given, otherwise the first section whose Name contains the given
	// name (or whose index matches).
	std::string target;
	if (!name.empty() || index > 0)
	{
		int cur_index = 0;
		size_t pos = 0;
		while ((pos = section.find("Name:", pos)) != std::string::npos)
		{
			++cur_index;
			size_t eol = section.find('\n', pos);
			std::string line = section.substr(pos, eol - pos);
			size_t nm = line.find('<');
			size_t ne = line.find('>');
			if (nm != std::string::npos && ne != std::string::npos)
			{
				std::string devname = line.substr(nm + 1, ne - nm - 1);
				if ((index > 0 && cur_index == index) || (!name.empty() && devname.find(name) != std::string::npos))
				{
					target = devname;
					break;
				}
			}
			pos = eol;
		}
		if (target.empty())
		{
			aResultToken.Error(_T("The specified sound device was not found."), _T(""), ErrorPrototype::OS);
			return;
		}
	}
	else
	{
		std::string dcmd = tool;
		dcmd += " get-default-" + section_name;
		LinuxRunCapture(dcmd, out);
		target = out;
		while (!target.empty() && (target.back() == '\n' || target.back() == '\r'))
			target.pop_back();
		if (target.empty())
		{
			aResultToken.Error(_T("The sound device could not be accessed."), _T(""), ErrorPrototype::OS);
			return;
		}
	}

	// Run "pactl <verb> [args]" with the resolved target as first argument.
	auto pactl = [&](const std::string &verb, const std::string &args) -> int
	{
		std::string cmd = tool;
		cmd += " " + verb + " " + LinuxSoundQuote(target);
		if (!args.empty())
			cmd += " " + args;
		return LinuxRunCapture(cmd, out);
	};

	if (callee == FID_SoundGetName)
	{
		aResultToken.SetValue(LinuxSoundWide(target));
		return;
	}

	if (callee == FID_SoundGetMute)
	{
		if (pactl("get-mute", "") != 0)
		{
			aResultToken.Error(_T("The sound device could not be accessed."), _T(""), ErrorPrototype::OS);
			return;
		}
		aResultToken.SetValue(out.find("yes") != std::string::npos ? 1 : 0);
		return;
	}

	if (callee == FID_SoundGetVolume)
	{
		if (pactl("get-volume", "") != 0)
		{
			aResultToken.Error(_T("The sound device could not be accessed."), _T(""), ErrorPrototype::OS);
			return;
		}
		int val = 0;
		LinuxSoundParsePercent(out, val);
		aResultToken.SetValue(val);
		return;
	}

	// Set variants.
	int setting = ATOI(new_setting);
	if (callee == FID_SoundSetMute)
	{
		if (setting == -1)
		{
			if (pactl("get-mute", "") != 0)
			{
				aResultToken.Error(_T("The sound device could not be accessed."), _T(""), ErrorPrototype::OS);
				return;
			}
			setting = out.find("yes") == std::string::npos ? 1 : 0;
		}
		if (pactl("set-mute", setting ? "1" : "0") != 0)
			aResultToken.Error(_T("The sound setting could not be changed."), _T(""), ErrorPrototype::OS);
		return;
	}

	// SoundSetVolume: absolute or relative.
	if (setting < 0)
		setting = 0;
	if (setting > 100)
		setting = 100;
	if (pactl("set-volume", std::to_string(setting) + "%") != 0)
		aResultToken.Error(_T("The sound setting could not be changed."), _T(""), ErrorPrototype::OS);
}
