// Packed-script support (check_detail0821 §5-M6 / R4): --pack + self-extract.
#pragma once
#include "../../stdafx.h"
#include <vector>
#include <string>

// True when this process started from a packed binary (A_IsCompiled = 1).
extern bool g_LinuxPacked;

// Check whether the given file's tail carries the pack footer.  Returns the
// script length, the resources length and the resources region offset.
bool LinuxPackFooter(const char *aPath, size_t &aScriptLen, size_t &aResLen, size_t &aResOff);

// True when the running executable is packed.
bool LinuxIsPacked();

// ahk_core --pack outfile script.ahk.
bool LinuxPackExecutable(const char *aOut, const char *aScript);

// Extract the packed script into a caller buffer (NUL-terminated); 0 on error.
size_t LinuxExtractPackedScript(char *aBuf, size_t aBufCap);

// Look up a packed FileInstall resource by its embedded source name.
bool LinuxPackGetResource(const char *aName, std::vector<unsigned char> &aOut);

// --- packed-runtime capability manifest (Audit47 §13.2) --------------------
//
// A packed executable embeds the runtime it was built from plus a
// machine-readable record of that runtime's optional capabilities.  Without
// it, a capability difference (today: the libei-enabled interactive runtime
// being packed with the feature-off ahk_core_pack template) is only
// discovered after deployment, when a script that needs the missing lane
// fails at run time.  The manifest makes the difference visible at pack time
// (a loud notice) and at run time (--diag / --pack-info).
struct LinuxPackRuntimeInfo
{
	std::string port_version; // AHK_LINUX_RELEASE_VERSION of that binary.
	bool libei = false;       // built with consented libei/EIS injection.
	bool portal = false;      // built with libportal support.
};

// Compile-time facts about the binary this code is running in.
LinuxPackRuntimeInfo LinuxPackSelfInfo();

// Ask another ahk_core-compatible binary for its facts by executing
// "<aExe> --pack-info".  Returns false with aErr describing the concrete
// failure; a template that cannot report its capabilities is never assumed to
// match this runtime.
bool LinuxPackProbeRuntime(const char *aExe, LinuxPackRuntimeInfo &aOut, std::string &aErr);

// Format/parse the key=value manifest embedded in a packed executable.
std::string LinuxPackManifestText(const LinuxPackRuntimeInfo &aPacker,
	const LinuxPackRuntimeInfo &aTemplate);
bool LinuxPackParseManifest(const std::string &aText, LinuxPackRuntimeInfo &aOut);

// Manifest of the running packed executable (false when this process is not
// packed, or predates the manifest).
bool LinuxPackReadManifest(std::string &aOut);

// Name of the reserved resource entry carrying the manifest.  It starts with
// SOH so it can never collide with a real FileInstall source path.
#define AHK_PACK_MANIFEST_NAME "\x01""ahk-pack-manifest"
