/* inputd_client.c -- protocol v1 test client for ahk-inputd.
 *
 * Usage: inputd_client SOCKET RULES [--timeout-ms N]
 *   RULES = comma-separated "code:suppress" (e.g. "88:0,30:1").
 *   Prints "ACK HELLO ok=<v>", "ACK SUBSCRIBE count=<n>" and one line per
 *   received event: "EVENT code=N value=V ts=T".  Exits on EOF, SIGTERM or
 *   timeout.  This is an INDEPENDENT implementation (no AHK code) so it can
 *   prove the wire protocol.
 */
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

#define PROTO_VERSION 1u
#define C2S_HELLO 1u
#define C2S_SUBSCRIBE 2u
#define S2C_EVENT 1u
#define S2C_ACK 2u
#define S2C_PONG 3u

static volatile sig_atomic_t g_run = 1;
static void on_sig(int s) { (void)s; g_run = 0; }

static int write_full(int fd, const void *buf, size_t n)
{
	size_t sent = 0;
	while (sent < n)
	{
		ssize_t r = write(fd, (const char *)buf + sent, n - sent);
		if (r < 0) { if (errno == EINTR) continue; return -1; }
		sent += (size_t)r;
	}
	return 0;
}

static int read_full(int fd, void *buf, size_t n)
{
	size_t got = 0;
	while (got < n)
	{
		ssize_t r = read(fd, (char *)buf + got, n - got);
		if (r == 0) return 0;
		if (r < 0) { if (errno == EINTR) continue; return -1; }
		got += (size_t)r;
	}
	return 1;
}

int main(int argc, char **argv)
{
	if (argc < 3)
	{
		fprintf(stderr, "usage: %s SOCKET RULES [--timeout-ms N]\n", argv[0]);
		return 2;
	}
	const char *socket_path = argv[1];
	const char *rules = argv[2];
	long timeout_ms = 30000;
	for (int i = 3; i < argc; ++i)
		if (!strcmp(argv[i], "--timeout-ms") && i + 1 < argc)
			timeout_ms = atol(argv[++i]);

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
	if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
	{
		fprintf(stderr, "inputd_client: connect %s: %s\n", socket_path, strerror(errno));
		return 1;
	}
	signal(SIGTERM, on_sig);
	signal(SIGINT, on_sig);
	struct timeval tv = { 3, 0 };
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	/* HELLO: u32 len + payload[cmd + ver4]; len=5 */
	unsigned char hello[4 + 1 + 4];
	unsigned int hello_len = 5;
	memcpy(hello, &hello_len, 4);
	hello[4] = C2S_HELLO;
	unsigned int ver = PROTO_VERSION;
	memcpy(hello + 5, &ver, 4);
	if (write_full(fd, hello, sizeof(hello)) != 0) return 1;
	unsigned char ack[6];
	if (read_full(fd, ack, 6) != 1 || ack[0] != S2C_ACK) return 1;
	printf("ACK HELLO ok=%u proto=%u\n", ack[1], (unsigned int)((unsigned char)ack[2] | ((unsigned char)ack[3] << 8) | ((unsigned char)ack[4] << 16) | ((unsigned char)ack[5] << 24)));
	fflush(stdout);

	/* SUBSCRIBE: u32 len + payload[cmd + count4 + rules]; len=1+4+count*5 */
	struct { unsigned int code; unsigned char suppress; } rs[128];
	int count = 0;
	char *copy = strdup(rules);
	char *tok = strtok(copy, ",");
	while (tok && count < 128)
	{
		unsigned int code = (unsigned int)atoi(tok);
		unsigned char suppress = strchr(tok, ':') && *(strchr(tok, ':') + 1) == '1' ? 1 : 0;
		rs[count].code = code;
		rs[count].suppress = suppress;
		++count;
		tok = strtok(NULL, ",");
	}
	free(copy);
	unsigned int sub_len = 1 + 4 + (unsigned int)count * 5;
	unsigned char *sub = malloc(4 + sub_len);
	memcpy(sub, &sub_len, 4);
	sub[4] = C2S_SUBSCRIBE;
	memcpy(sub + 5, &(unsigned int){ (unsigned int)count }, 4);
	for (int i = 0; i < count; ++i)
	{
		memcpy(sub + 9 + (size_t)i * 5, &rs[i].code, 4);
		sub[9 + (size_t)i * 5 + 4] = rs[i].suppress;
	}
	if (write_full(fd, sub, 4 + sub_len) != 0) return 1;
	free(sub);
	if (read_full(fd, ack, 6) != 1 || ack[0] != S2C_ACK) return 1;
	printf("ACK SUBSCRIBE ok=%u count=%u\n", ack[1], (unsigned int)((unsigned char)ack[2] | ((unsigned char)ack[3] << 8) | ((unsigned char)ack[4] << 16) | ((unsigned char)ack[5] << 24)));
	fflush(stdout);

	struct pollfd pfd = { fd, POLLIN, 0 };
	while (g_run)
	{
		int pr = poll(&pfd, 1, 200);
		if (pr == 0) { if (timeout_ms > 0) { timeout_ms -= 200; if (timeout_ms <= 0) break; } continue; }
		if (pr < 0) break;
		unsigned char frame[14];
		int r = read_full(fd, frame, sizeof(frame));
		if (r != 1) break;
		if (frame[0] == S2C_EVENT)
		{
			unsigned int code;
			long long ts;
			memcpy(&code, frame + 1, 4);
			memcpy(&ts, frame + 6, 8);
			printf("EVENT code=%u value=%u ts=%lld\n", code, (unsigned)frame[5], ts);
			fflush(stdout);
		}
		else if (frame[0] == S2C_PONG)
			printf("PONG\n"), fflush(stdout);
	}
	close(fd);
	return 0;
}
