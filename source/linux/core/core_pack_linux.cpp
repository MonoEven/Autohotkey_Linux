// Packed-script support (check_detail0821 §5-M6 / R4): the Ahk2Exe-equivalent
// "trailing payload" layout -- [runtime ELF][script bytes][len:uint64le][magic].
// ahk_core --pack outfile script.ahk produces a self-contained executable
// that, when run without a script argument, reads its own /proc/self/exe tail,
// finds the magic, extracts the script and runs it (A_IsCompiled = 1).
#include "../../stdafx.h"
#include "core_pack_linux.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#define AHK_PACK_MAGIC "AHK2ELFX1" // 8 bytes, also the footer's tail.

bool g_LinuxPacked = false; // True when this process started from a packed binary.

static bool ReadAllFd(int aFd, std::vector<unsigned char> &aOut)
{
	unsigned char buf[65536];
	ssize_t n;
	while ((n = read(aFd, buf, sizeof(buf))) > 0)
		aOut.insert(aOut.end(), buf, buf + n);
	return n == 0;
}

// Does the given file's tail carry the pack footer?  Returns the script
// length + the offset of the script bytes.
bool LinuxPackFooter(const char *aPath, size_t &aScriptLen, size_t &aScriptOff)
{
	int fd = open(aPath, O_RDONLY);
	if (fd < 0)
		return false;
	struct stat st;
	if (fstat(fd, &st) != 0 || st.st_size < (off_t)(8 + 8 + 8))
	{
		close(fd);
		return false;
	}
	// Read the tail: [len:8][magic:8].
	unsigned char tail[16];
	ssize_t n = pread(fd, tail, sizeof(tail), st.st_size - (off_t)sizeof(tail));
	close(fd);
	if (n != (ssize_t)sizeof(tail) || memcmp(tail + 8, AHK_PACK_MAGIC, 8) != 0)
		return false;
	uint64_t len = 0;
	for (int i = 0; i < 8; ++i)
		len |= (uint64_t)tail[i] << (8 * i);
	if (len > (uint64_t)st.st_size || len > (64u << 20))
		return false; // Sanity: the script payload must fit + be bounded.
	aScriptLen = (size_t)len;
	aScriptOff = (size_t)(st.st_size - (off_t)(8 + 8 + len));
	return true;
}

bool LinuxIsPacked()
{
	size_t len = 0, off = 0;
	return LinuxPackFooter("/proc/self/exe", len, off);
}

// --pack outfile script.ahk: copy the runtime, append the script + footer.
bool LinuxPackExecutable(const char *aOut, const char *aScript)
{
	// Read the runtime (ourselves).
	int src = open("/proc/self/exe", O_RDONLY);
	if (src < 0)
	{
		fprintf(stderr, "AutoHotkey Linux: cannot read own executable.\n");
		return false;
	}
	std::vector<unsigned char> runtime;
	if (!ReadAllFd(src, runtime))
	{
		close(src);
		return false;
	}
	close(src);
	// Read the script.
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
	// Write outfile = runtime + script + [len:8][magic:8].
	int out = open(aOut, O_WRONLY | O_CREAT | O_TRUNC, 0755);
	if (out < 0)
	{
		fprintf(stderr, "AutoHotkey Linux: cannot write '%s'.\n", aOut);
		return false;
	}
	size_t written = 0;
	auto w = [&](const void *p, size_t n) {
		const unsigned char *b = (const unsigned char *)p;
		while (n > 0)
		{
			ssize_t k = write(out, b, n);
			if (k <= 0)
				return false;
			b += k;
			n -= (size_t)k;
		}
		written += (size_t)(b - (const unsigned char *)p);
		return true;
	};
	if (   !w(runtime.data(), runtime.size())
		|| !w(script.data(), script.size()))
	{
		close(out);
		unlink(aOut);
		return false;
	}
	uint64_t len = script.size();
	unsigned char lenb[8];
	for (int i = 0; i < 8; ++i)
		lenb[i] = (unsigned char)(len >> (8 * i));
	if (!w(lenb, 8) || !w(AHK_PACK_MAGIC, 8))
	{
		close(out);
		unlink(aOut);
		return false;
	}
	close(out);
	return true;
}

// Extract the packed script into a caller buffer.  Returns the length.
size_t LinuxExtractPackedScript(char *aBuf, size_t aBufCap)
{
	size_t len = 0, off = 0;
	if (!LinuxPackFooter("/proc/self/exe", len, off) || len + 1 > aBufCap)
		return 0;
	int fd = open("/proc/self/exe", O_RDONLY);
	if (fd < 0)
		return 0;
	ssize_t n = pread(fd, aBuf, (size_t)len, (off_t)off);
	close(fd);
	if (n != (ssize_t)len)
		return 0;
	aBuf[len] = '\0';
	return len;
}