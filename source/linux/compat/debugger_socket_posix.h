#pragma once
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <strings.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

using SOCKET = int;
using u_long = unsigned long;
#ifndef INVALID_SOCKET
#define INVALID_SOCKET (-1)
#endif
#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif
#ifndef FD_READ
#define FD_READ 0x01
#endif
#ifndef FD_CLOSE
#define FD_CLOSE 0x20
#endif
#ifndef MAKEWORD
#define MAKEWORD(a,b) ((uint16_t)(((uint8_t)(a)) | ((uint16_t)((uint8_t)(b)) << 8)))
#endif
struct WSADATA { int unused; };
inline int WSAStartup(uint16_t, WSADATA *) { return 0; }
inline int WSACleanup() { return 0; }
inline int closesocket(SOCKET s) { return close(s); }
inline int ioctlsocket(SOCKET s, long cmd, u_long *v) { return ioctl(s, cmd, v); }
inline int WSAAsyncSelect(SOCKET, void *, unsigned int, long) { return 0; }
inline char *_itoa(int value, char *buf, int radix)
{
    if (radix == 16) std::sprintf(buf, "%x", value);
    else std::sprintf(buf, "%d", value);
    return buf;
}
inline char *_i64toa(long long value, char *buf, int radix)
{
    if (radix == 16) std::sprintf(buf, "%llx", value);
    else std::sprintf(buf, "%lld", value);
    return buf;
}
inline char *_ultoa(unsigned long value, char *buf, int radix)
{
    if (radix == 16) std::sprintf(buf, "%lx", value);
    else std::sprintf(buf, "%lu", value);
    return buf;
}
inline char *_ui64toa(unsigned long long value, char *buf, int radix)
{
    if (radix == 16) std::sprintf(buf, "%llx", value);
    else std::sprintf(buf, "%llu", value);
    return buf;
}
inline long long _atoi64(const char *value)
{
    return std::strtoll(value, nullptr, 10);
}
#ifndef _stricmp
#define _stricmp strcasecmp
#endif
#ifndef _strnicmp
#define _strnicmp strncasecmp
#endif
