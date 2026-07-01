/*
 * Networking Stage 0 on the FreeBSD-libc Unikraft port.
 *
 * Proves the BSD socket API works end-to-end through the port: the FreeBSD libc
 * header/inet layer composes with Unikraft's posix-socket syscalls and lwip's
 * TCP/IP stack. Two self-tests over the loopback interface (127.0.0.1):
 *   (1) UDP sendto/recvfrom round-trip  -> the data path (send + receive)
 *   (2) TCP socket/bind/listen/getsockname -> the passive-open control path
 * Both are single-threaded and self-contained (no external host needed); the
 * UDP recv uses SO_RCVTIMEO so a failure prints FAIL instead of hanging.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define TEST_PORT 4242

static int test_udp_loopback(void)
{
	int rs = -1, ss = -1, ok = 0;
	struct sockaddr_in a;
	struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
	const char *msg = "freebsd-libc+lwip";
	char buf[64];
	ssize_t n;

	rs = socket(AF_INET, SOCK_DGRAM, 0);
	ss = socket(AF_INET, SOCK_DGRAM, 0);
	if (rs < 0 || ss < 0) {
		printf("  udp: socket() failed (rs=%d ss=%d errno=%d)\n", rs, ss, errno);
		goto out;
	}

	/* bind the receiver to 127.0.0.1:TEST_PORT */
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons(TEST_PORT);
	a.sin_addr.s_addr = inet_addr("127.0.0.1");
	if (bind(rs, (struct sockaddr *)&a, sizeof(a)) != 0) {
		printf("  udp: bind() failed (errno=%d)\n", errno);
		goto out;
	}
	/* don't block forever if the datagram never arrives */
	setsockopt(rs, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	if (sendto(ss, msg, strlen(msg), 0,
		   (struct sockaddr *)&a, sizeof(a)) != (ssize_t)strlen(msg)) {
		printf("  udp: sendto() failed (errno=%d)\n", errno);
		goto out;
	}

	n = recvfrom(rs, buf, sizeof(buf) - 1, 0, NULL, NULL);
	if (n < 0) {
		printf("  udp: recvfrom() failed/timed out (errno=%d)\n", errno);
		goto out;
	}
	buf[n] = '\0';
	ok = (n == (ssize_t)strlen(msg)) && (strcmp(buf, msg) == 0);
	printf("  udp recvfrom got \"%s\" (%zd bytes)\n", buf, n);
out:
	if (rs >= 0) close(rs);
	if (ss >= 0) close(ss);
	return ok;
}

static int test_tcp_passive(void)
{
	int s, ok = 0;
	struct sockaddr_in a;
	socklen_t alen = sizeof(a);

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0) {
		printf("  tcp: socket() failed (errno=%d)\n", errno);
		return 0;
	}
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons(0);            /* ephemeral port */
	a.sin_addr.s_addr = inet_addr("127.0.0.1");
	if (bind(s, (struct sockaddr *)&a, sizeof(a)) != 0) {
		printf("  tcp: bind() failed (errno=%d)\n", errno);
		goto out;
	}
	if (listen(s, 1) != 0) {
		printf("  tcp: listen() failed (errno=%d)\n", errno);
		goto out;
	}
	if (getsockname(s, (struct sockaddr *)&a, &alen) != 0) {
		printf("  tcp: getsockname() failed (errno=%d)\n", errno);
		goto out;
	}
	printf("  tcp passive open on 127.0.0.1:%u\n", ntohs(a.sin_port));
	ok = 1;
out:
	close(s);
	return ok;
}

#define HTTP_PORT 8080

/* Minimal HTTP/1.1 server: bind 0.0.0.0:8080, accept in a loop, read the
 * request (drained, not parsed), write a fixed text response, close. The
 * external client (host curl) drives accept(), so a single thread suffices. */
static int http_server(void)
{
	int ls, cs;
	struct sockaddr_in a;
	int one = 1;
	unsigned long served = 0;

	ls = socket(AF_INET, SOCK_STREAM, 0);
	if (ls < 0) {
		printf("http: socket() failed (errno=%d)\n", errno);
		return -1;
	}
	setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons(HTTP_PORT);
	a.sin_addr.s_addr = htonl(INADDR_ANY);   /* all interfaces */
	if (bind(ls, (struct sockaddr *)&a, sizeof(a)) != 0) {
		printf("http: bind() failed (errno=%d)\n", errno);
		close(ls);
		return -1;
	}
	if (listen(ls, 8) != 0) {
		printf("http: listen() failed (errno=%d)\n", errno);
		close(ls);
		return -1;
	}
	printf("http: listening on 0.0.0.0:%d (FreeBSD libc + lwip)\n", HTTP_PORT);

	for (;;) {
		char req[1024];
		char resp[512];
		const char *body;
		int blen, rlen;
		ssize_t n;

		cs = accept(ls, NULL, NULL);
		if (cs < 0) {
			printf("http: accept() failed (errno=%d)\n", errno);
			continue;
		}
		/* read (and ignore) the request line/headers */
		n = recv(cs, req, sizeof(req) - 1, 0);
		if (n > 0)
			req[n] = '\0';

		served++;
		body = "Hello from FreeBSD libc on Unikraft!\n";
		blen = (int)strlen(body);
		rlen = snprintf(resp, sizeof(resp),
				"HTTP/1.1 200 OK\r\n"
				"Content-Type: text/plain\r\n"
				"Content-Length: %d\r\n"
				"Connection: close\r\n"
				"\r\n"
				"%s", blen, body);
		send(cs, resp, (size_t)rlen, 0);
		close(cs);
		printf("http: served request #%lu\n", served);
	}
	/* not reached */
}

int main(int argc, char *argv[])
{
	int udp, tcp;
	(void)argc; (void)argv;

	printf("netdemo: BSD sockets on FreeBSD libc + lwip\n");
	udp = test_udp_loopback();
	tcp = test_tcp_passive();
	printf("netdemo:\n");
	printf("  UDP loopback round-trip        : %s\n", udp ? "OK" : "FAIL");
	printf("  TCP passive open (bind/listen) : %s\n", tcp ? "OK" : "FAIL");
	printf("netdemo: %d/2 passed\n", udp + tcp);

	/* Stage 1: serve HTTP to an external host client. */
	http_server();
	return 0;
}
