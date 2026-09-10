// Packed-script support (check_detail0821 §5-M6 / R4): the Ahk2Exe-equivalent
// "trailing payload" layout --
//   [runtime ELF][resources: (name\0 size:8 data)*][script bytes]
//   [script_len:8][resources_len:8][magic "AHK2ELFX1":8]
// ahk_core --pack outfile script.ahk produces a self-contained executable
// that, when run without a script argument, reads its own /proc/self/exe tail,
// extracts the script and runs it (A_IsCompiled = 1).  FileInstall resources
// referenced by the script are embedded and extracted on demand.
#include "../../stdafx.h"
#include "core_pack_linux.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>

#define AHK_PACK_MAGIC "AHK2ELFX1" // 8 bytes, also the footer's tail.

bool g_LinuxPacked = false; // True when this process started from a packed binary.

static bool ReadAllFd(int aFd, std::vector<unsigned char> &aOut)
{
	aOut.clear();
	unsigned char buf[65536];
	for (;;)
	{
		ssize_t n = read(aFd, buf, sizeof(buf));
		if (n == 0)
			return true;
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0)
			return false;
		aOut.insert(aOut.end(), buf, buf + n);
	}
}

static void AppendU64(std::vector<unsigned char> &aOut, uint64_t v)
{
	for (int i = 0; i < 8; ++i)
		aOut.push_back((unsigned char)(v >> (8 * i)));
}

// Does the given file's tail carry the pack footer?  Returns the script + the
// resources lengths (bytes) and the offset of the resources region.
bool LinuxPackFooter(const char *aPath, size_t &aScriptLen, size_t &aResLen, size_t &aResOff)
{
	int fd = open(aPath, O_RDONLY);
	if (fd < 0)
		return false;
	struct stat st;
	if (fstat(fd, &st) != 0 || st.st_size < (off_t)(8 + 8 + 8 + 8))
	{
		close(fd);
		return false;
	}
	unsigned char tail[24];
	ssize_t n = pread(fd, tail, sizeof(tail), st.st_size - (off_t)sizeof(tail));
	close(fd);
	if (n != (ssize_t)sizeof(tail) || memcmp(tail + 16, AHK_PACK_MAGIC, 8) != 0)
		return false;
	uint64_t file_size = (uint64_t)st.st_size;
	uint64_t slen = 0, rlen = 0;
	for (int i = 0; i < 8; ++i)
	{
		slen |= (uint64_t)tail[i] << (8 * i);
		rlen |= (uint64_t)tail[8 + i] << (8 * i);
	}
	const uint64_t footer_size = sizeof(tail);
	if (slen > file_size - footer_size || rlen > file_size - footer_size - slen
		|| slen + rlen > (64u << 20)
		|| slen > (uint64_t)SIZE_MAX || rlen > (uint64_t)SIZE_MAX)
		return false; // Sanity: bounded payload, checked without wraparound.
	aScriptLen = (size_t)slen;
	aResLen = (size_t)rlen;
	aResOff = (size_t)(file_size - footer_size - slen - rlen);
	return true;
}

bool LinuxIsPacked()
{
	size_t sl = 0, rl = 0, ro = 0;
	return LinuxPackFooter("/proc/self/exe", sl, rl, ro);
}

// Scan a script's text for FileInstall("source", ...) and collect the source
// paths (a simple string scan; the first quoted literal after FileInstall()).
static void LinuxCollectFileInstallSources(const std::vector<unsigned char> &aScript, std::vector<std::string> &aSources)
{
	const char *p = (const char *)aScript.data();
	size_t n = aScript.size();
	for (size_t i = 0; i + 13 <= n; ++i)
	{
		if (memcmp(p + i, "FileInstall", 11) != 0)
			continue;
		size_t j = i + 11;
		while (j < n && (p[j] == ' ' || p[j] == '\t'))
			++j;
		if (j >= n || p[j] != '(')
			continue;
		++j;
		while (j < n && (p[j] == ' ' || p[j] == '\t'))
			++j;
		if (j >= n || (p[j] != '"' && p[j] != '\''))
			continue;
		char quote = p[j++];
		std::string src;
		for (; j < n && p[j] != quote; ++j)
			src += p[j];
		if (!src.empty())
		{
			bool seen = false;
			for (auto &s : aSources)
				if (s == src)
					seen = true;
			if (!seen)
				aSources.push_back(src);
		}
	}
}

// --pack outfile script.ahk: copy the runtime, embed the FileInstall resources
// + the script, append the two lengths + the magic.
bool LinuxPackExecutable(const char *aOut, const char *aScript)
{
	const char *runtime_path = "/proc/self/exe";
#ifdef HAVE_LIBEI
	// A DT_NEEDED libei runtime cannot be copied into a truly standalone ELF:
	// the dynamic loader resolves dependencies before embedded resources can be
	// extracted. Release packages therefore ship a same-version, feature-off
	// pack template beside ahk_core. Source builds may name it explicitly.
	char sibling[PATH_MAX] = { 0 };
	const char *configured = getenv("AHK_PACK_RUNTIME");
	if (configured && *configured)
		runtime_path = configured;
	else
	{
		ssize_t n = readlink("/proc/self/exe", sibling, sizeof(sibling) - 1);
		if (n > 0)
		{
			sibling[n] = 0;
			char *slash = strrchr(sibling, '/');
			if (slash)
				snprintf(slash + 1, (size_t)(sibling + sizeof(sibling) - slash - 1),
					"ahk_core_pack");
		}
		if (!sibling[0] || access(sibling, X_OK) != 0)
		{
			fprintf(stderr, "AutoHotkey Linux: this libei-enabled runtime needs "
				"the bundled ahk_core_pack template (or AHK_PACK_RUNTIME) to "
				"produce a standalone executable.\n");
			return false;
		}
		runtime_path = sibling;
	}
#endif
	int src = open(runtime_path, O_RDONLY);
	if (src < 0)
	{
		fprintf(stderr, "AutoHotkey Linux: cannot read pack runtime '%s'.\n",
			runtime_path);
		return false;
	}
	std::vector<unsigned char> runtime;
	if (!ReadAllFd(src, runtime))
	{
		close(src);
		return false;
	}
	close(src);
	int sfd = open(aScript, O_RDONLY);
	if (sfd < 0)
	{
		fprintf(stderr, "AutoHotkey Linux: cannot read script '%s'.\n", aScript);
		return false;
	}
	std::vector<unsigned char> script;
	if (!ReadAllFd(sfd, script))
	{
		close(sfd);
		return false;
	}
	close(sfd);
	// Collect + read the FileInstall resources as synchronized name/data tuples.
	std::vector<std::string> sources;
	LinuxCollectFileInstallSources(script, sources);
	struct PackedResource
	{
		std::string name;
		std::vector<unsigned char> data;
	};
	std::vector<PackedResource> resources;
	resources.reserve(sources.size());
	for (auto &s : sources)
	{
		int rfd = open(s.c_str(), O_RDONLY | O_CLOEXEC);
		if (rfd < 0)
		{
			fprintf(stderr, "AutoHotkey Linux: cannot read FileInstall resource '%s': %s.\n",
				s.c_str(), strerror(errno));
			return false;
		}
		PackedResource resource;
		resource.name = s;
		bool ok = ReadAllFd(rfd, resource.data);
		int saved_errno = errno;
		close(rfd);
		if (!ok)
		{
			fprintf(stderr, "AutoHotkey Linux: failed reading FileInstall resource '%s': %s.\n",
				s.c_str(), strerror(saved_errno));
			return false;
		}
		resources.push_back(std::move(resource));
	}
	// Capability manifest (Audit47 §13.2): record the runtime that produced
	// this executable and the template it embeds, so a capability difference
	// is visible here instead of only at some later run time.  The template is
	// probed rather than assumed: a template that cannot report its
	// capabilities is an explicit error, never a guess.
	LinuxPackRuntimeInfo packer = LinuxPackSelfInfo();
	LinuxPackRuntimeInfo tmpl = packer;
	if (strcmp(runtime_path, "/proc/self/exe") != 0)
	{
		std::string probe_error;
		if (!LinuxPackProbeRuntime(runtime_path, tmpl, probe_error))
		{
			fprintf(stderr, "AutoHotkey Linux: cannot determine the capabilities of "
				"the pack template '%s': %s\n", runtime_path, probe_error.c_str());
			return false;
		}
	}
	if (tmpl.port_version != packer.port_version)
		fprintf(stderr, "AutoHotkey Linux: pack template '%s' is release %s while this "
			"runtime is %s; the packed executable embeds the template's release.\n",
			runtime_path, tmpl.port_version.c_str(), packer.port_version.c_str());
	if (packer.libei && !tmpl.libei)
		fprintf(stderr, "AutoHotkey Linux: this runtime was built with consented "
			"libei/EIS injection but the pack template was not, so the packed "
			"executable will report libei as unavailable (check "
			"HotkeyBackendGet().libei_build_enabled, or use --pack-info).\n");
	{
		std::string manifest = LinuxPackManifestText(packer, tmpl);
		PackedResource meta;
		meta.name = AHK_PACK_MANIFEST_NAME;
		meta.data.assign(manifest.begin(), manifest.end());
		resources.push_back(std::move(meta));
	}
	// Build the resources blob: (name\0 size:8 data)*.
	std::vector<unsigned char> res;
	for (const auto &resource : resources)
	{
		res.insert(res.end(), resource.name.begin(), resource.name.end());
		res.push_back(0);
		AppendU64(res, resource.data.size());
		res.insert(res.end(), resource.data.begin(), resource.data.end());
	}
	std::string temp_path = std::string(aOut) + ".tmp." + std::to_string((long long)getpid());
	int out = open(temp_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0755);
	if (out < 0)
	{
		fprintf(stderr, "AutoHotkey Linux: cannot create temporary pack '%s': %s.\n",
			temp_path.c_str(), strerror(errno));
		return false;
	}
	bool output_ok = true;
	auto w = [&](const void *p, size_t n) {
		const unsigned char *b = (const unsigned char *)p;
		while (n > 0)
		{
			ssize_t k = write(out, b, n);
			if (k < 0 && errno == EINTR)
				continue;
			if (k <= 0)
				return false;
			b += k;
			n -= (size_t)k;
		}
		return true;
	};
	if (!w(runtime.data(), runtime.size())
		|| !w(res.data(), res.size())
		|| !w(script.data(), script.size()))
		output_ok = false;
	uint64_t slen = script.size(), rlen = res.size();
	unsigned char fb[24];
	for (int i = 0; i < 8; ++i) fb[i] = (unsigned char)(slen >> (8 * i));
	for (int i = 0; i < 8; ++i) fb[8 + i] = (unsigned char)(rlen >> (8 * i));
	memcpy(fb + 16, AHK_PACK_MAGIC, 8);
	if (output_ok && !w(fb, sizeof(fb)))
		output_ok = false;
	int saved_errno = 0;
	if (!output_ok)
		saved_errno = errno;
	if (output_ok && fsync(out) != 0)
	{
		output_ok = false;
		saved_errno = errno;
	}
	if (output_ok && fchmod(out, 0755) != 0)
	{
		output_ok = false;
		saved_errno = errno;
	}
	close(out);
	if (!output_ok || rename(temp_path.c_str(), aOut) != 0)
	{
		if (output_ok)
			saved_errno = errno;
		unlink(temp_path.c_str());
		fprintf(stderr, "AutoHotkey Linux: failed to finalize packed executable '%s': %s.\n",
			aOut, strerror(saved_errno));
		return false;
	}
	return true;
}

// Extract the packed script into a caller buffer (NUL-terminated); 0 on error.
size_t LinuxExtractPackedScript(char *aBuf, size_t aBufCap)
{
	size_t slen = 0, rlen = 0, roff = 0;
	if (!LinuxPackFooter("/proc/self/exe", slen, rlen, roff) || slen + 1 > aBufCap)
		return 0;
	int fd = open("/proc/self/exe", O_RDONLY);
	if (fd < 0)
		return 0;
	ssize_t n = pread(fd, aBuf, (size_t)slen, (off_t)(roff + rlen));
	close(fd);
	if (n != (ssize_t)slen)
		return 0;
	aBuf[slen] = '\0';
	return slen;
}

// Look up a packed FileInstall resource by its embedded source name.  Writes
// the data into aOut; returns false if not found or not packed.
bool LinuxPackGetResource(const char *aName, std::vector<unsigned char> &aOut)
{
	size_t slen = 0, rlen = 0, roff = 0;
	if (!LinuxPackFooter("/proc/self/exe", slen, rlen, roff) || rlen == 0)
		return false;
	int fd = open("/proc/self/exe", O_RDONLY);
	if (fd < 0)
		return false;
	size_t end = roff + rlen;
	size_t pos = roff;
	bool found = false;
	while (pos < end)
	{
		// name (NUL-terminated) | size:8 | data
		std::string name;
		char c;
		for (;;)
		{
			if (pos >= end)
				goto done;
			if (pread(fd, &c, 1, (off_t)pos) != 1)
				goto done;
			++pos;
			if (c == '\0')
				break;
			name += c;
		}
		unsigned char sz[8];
		if (pos + 8 > end || pread(fd, sz, 8, (off_t)pos) != 8)
			goto done;
		pos += 8;
		uint64_t dlen = 0;
		for (int i = 0; i < 8; ++i)
			dlen |= (uint64_t)sz[i] << (8 * i);
		if (pos + dlen > end)
			goto done;
		if (name == aName)
		{
			aOut.resize((size_t)dlen);
			if (dlen > 0 && pread(fd, aOut.data(), (size_t)dlen, (off_t)pos) != (ssize_t)dlen)
				goto done;
			found = true;
			goto done;
		}
		pos += (size_t)dlen;
	}
done:
	close(fd);
	return found;
}

// --- packed-runtime capability manifest (Audit47 §13.2) --------------------

LinuxPackRuntimeInfo LinuxPackSelfInfo()
{
	LinuxPackRuntimeInfo info;
#ifdef AHK_LINUX_RELEASE_VERSION
	info.port_version = AHK_LINUX_RELEASE_VERSION;
#else
	info.port_version = "unknown";
#endif
#ifdef HAVE_LIBEI
	info.libei = true;
#endif
#ifdef AHK_LIBPORTAL_VERSION
	info.portal = true;
#endif
	return info;
}

std::string LinuxPackManifestText(const LinuxPackRuntimeInfo &aPacker,
	const LinuxPackRuntimeInfo &aTemplate)
{
	// Line-based key=value: readable by scripts, --diag and shell oracles
	// without pulling a JSON parser into the runtime.
	std::string out;
	auto line = [&out](const char *aKey, const std::string &aValue) {
		out += aKey;
		out += '=';
		out += aValue;
		out += '\n';
	};
	auto flag = [&line](const char *aKey, bool aValue) {
		line(aKey, aValue ? "1" : "0");
	};
	line("schema", "1");
	line("port_version", aTemplate.port_version);
	flag("libei", aTemplate.libei);
	flag("portal", aTemplate.portal);
	line("packer_port_version", aPacker.port_version);
	flag("packer_libei", aPacker.libei);
	flag("packer_portal", aPacker.portal);
	return out;
}

static bool ManifestValue(const std::string &aText, const char *aKey, std::string &aOut)
{
	const std::string prefix = std::string(aKey) + "=";
	size_t pos = 0;
	while (pos <= aText.size())
	{
		size_t end = aText.find('\n', pos);
		if (end == std::string::npos)
			end = aText.size();
		if (aText.compare(pos, prefix.size(), prefix) == 0)
		{
			aOut = aText.substr(pos + prefix.size(), end - pos - prefix.size());
			return true;
		}
		if (end == aText.size())
			break;
		pos = end + 1;
	}
	return false;
}

bool LinuxPackParseManifest(const std::string &aText, LinuxPackRuntimeInfo &aOut)
{
	std::string value;
	if (!ManifestValue(aText, "schema", value) || value != "1")
		return false;
	if (!ManifestValue(aText, "port_version", aOut.port_version))
		return false;
	if (!ManifestValue(aText, "libei", value))
		return false;
	aOut.libei = (value == "1");
	if (!ManifestValue(aText, "portal", value))
		return false;
	aOut.portal = (value == "1");
	return true;
}

bool LinuxPackProbeRuntime(const char *aExe, LinuxPackRuntimeInfo &aOut, std::string &aErr)
{
	if (!aExe || !*aExe)
	{
		aErr = "empty template path";
		return false;
	}
	// popen() runs the template through a shell, so a path containing a quote
	// (or any character that would change the command) is refused instead of
	// being quoted cleverly.
	if (strpbrk(aExe, "\"'`$;&|<>()*?\\\n"))
	{
		aErr = "template path contains shell metacharacters";
		return false;
	}
	std::string cmd = std::string("\"") + aExe + "\" --pack-info 2>/dev/null";
	FILE *pipe = popen(cmd.c_str(), "r");
	if (!pipe)
	{
		aErr = "cannot execute the template (popen failed)";
		return false;
	}
	std::string text;
	char buf[512];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), pipe)) > 0)
		text.append(buf, n);
	int rc = pclose(pipe);
	if (rc != 0)
	{
		aErr = "template exited non-zero from --pack-info";
		return false;
	}
	if (!LinuxPackParseManifest(text, aOut))
	{
		aErr = "template --pack-info did not report schema=1 with port_version/libei/portal "
			"(an older runtime cannot declare its capabilities)";
		return false;
	}
	return true;
}

bool LinuxPackReadManifest(std::string &aOut)
{
	std::vector<unsigned char> data;
	if (!LinuxPackGetResource(AHK_PACK_MANIFEST_NAME, data) || data.empty())
		return false;
	aOut.assign(data.begin(), data.end());
	return true;
}
