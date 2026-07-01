/*
 * App-local glue for the OpenSSL libcrypto smoke test.
 *
 * These symbols are referenced by OpenSSL's socket/syslog/console/file BIO and
 * init code, all of which are compiled into libcrypto unconditionally but are
 * never reached by a SHA-256 hash. They live HERE (in the app), not in
 * lib-freebsd-libc's shared glue/stubs.c, because they are OpenSSL-shaped: the
 * other app on this libc (sqlite) neither references nor provides them, and
 * putting them in the shared glue would risk multi-definition there. The
 * linker confirmed all of these are otherwise undefined, so defining them in
 * this app's link is safe.
 *
 * Each is a graceful no-op/failure. To actually support networking one would
 * wire lwip's resolver (LWIP_SOCKET/LWIP_DNS) to these names instead.
 */

#include <stdio.h>      /* FILE, fdopen */
#include <stddef.h>     /* NULL */
#include <errno.h>
#include <termios.h>    /* struct termios, tcgetattr/tcsetattr */
#include <syslog.h>     /* openlog/closelog prototypes */
#include <netdb.h>      /* hostent/servent/addrinfo + resolver prototypes */

/* --- name/service resolution: no resolver in this build --- */
struct hostent *gethostbyname(const char *name)
{
	(void)name;
	h_errno = HOST_NOT_FOUND;
	return NULL;
}

int getaddrinfo(const char *node, const char *service,
		const struct addrinfo *hints, struct addrinfo **res)
{
	(void)node; (void)service; (void)hints;
	if (res)
		*res = NULL;
	return EAI_SYSTEM;
}

void freeaddrinfo(struct addrinfo *res)
{
	(void)res;
}

const char *gai_strerror(int errcode)
{
	(void)errcode;
	return "name resolution not supported";
}

int getnameinfo(const struct sockaddr *sa, socklen_t salen,
		char *host, socklen_t hostlen,
		char *serv, socklen_t servlen, int flags)
{
	(void)sa; (void)salen; (void)host; (void)hostlen;
	(void)serv; (void)servlen; (void)flags;
	return EAI_SYSTEM;
}

/* --- syslog BIO: no system logger on a unikernel --- */
void openlog(const char *ident, int option, int facility)
{
	(void)ident; (void)option; (void)facility;
}

void closelog(void)
{
}

/* --- terminal control (UI password prompt): no tty --- */
int tcgetattr(int fd, struct termios *t)
{
	(void)fd; (void)t;
	errno = ENOTTY;
	return -1;
}

int tcsetattr(int fd, int action, const struct termios *t)
{
	(void)fd; (void)action; (void)t;
	errno = ENOTTY;
	return -1;
}

/*
 * atexit: a unikernel halts rather than returning from main, so cleanup
 * handlers would never fire anyway. Accept the registration and drop it.
 */
int atexit(void (*func)(void))
{
	(void)func;
	return 0;
}

/* file BIO from an fd: stdio fdopen was deferred in the stdio port --- */
FILE *fdopen(int fd, const char *mode)
{
	(void)fd; (void)mode;
	errno = ENOSYS;
	return NULL;
}
