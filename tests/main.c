/*
 * apps/tests — FreeBSD-libc-on-Unikraft subsystem coverage harness.
 *
 * These are the per-subsystem self-tests that used to live (disabled) inside
 * apps/sqlite/main.c. They were moved here so the SQLite app stays a focused
 * demo and this app becomes the regression/coverage artifact for the port:
 * every ported subsystem is exercised at boot and reports PASS/FAIL, with a
 * grand total at the end. None of these tests touch SQLite — the harness
 * depends on lib-freebsd-libc alone.
 *
 * nolibc/FreeBSD printf has no %f/%g on the default (non-STDIO) path, so the
 * floating-point tests compare results bit-exactly or via verdicts rather than
 * printing the values.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <fnmatch.h>
#include <err.h>
#include <errno.h>
#include <glob.h>
#include <libgen.h>
#include <time.h>
#include <regex.h>
#include <stringlist.h>
#include <fmtmsg.h>
#include <arpa/inet.h>

/* Running tally across all subsystems, filled in by each test via record(). */
static int g_pass, g_total;
static void record(int pass, int total) { g_pass += pass; g_total += total; }

/*
 * Exercises libc strtod() directly to prove the FreeBSD gdtoa port works.
 * The previous hand-rolled strtod stub could not parse hex floats (returned 0)
 * and was not correctly rounded; gdtoa is. nolibc's printf has no %f/%g, so we
 * compare the result bit-exactly against the compiler's own double literal and
 * print only the IEEE-754 bit pattern (hex) and an OK/MISMATCH verdict.
 */
static uint64_t bits(double d)
{
	uint64_t u;
	memcpy(&u, &d, sizeof(u));
	return u;
}

static void test_strtod(void)
{
	struct { const char *in; double expect; } cases[] = {
		{ "0x1p4",            16.0 },   /* hex float: old stub gave 0 */
		{ "0x1.8p3",          12.0 },   /* hex float with fraction    */
		{ "1.5e3",            1500.0 },
		{ "0.1",              0.1 },
		{ "-2.5",             -2.5 },
		{ "3.14159265358979", 3.14159265358979 },
	};
	int pass = 0, n = sizeof(cases)/sizeof(cases[0]);
	printf("strtod checks (bit-exact vs compiler literal):\n");
	for (int i = 0; i < n; i++) {
		double v = strtod(cases[i].in, NULL);
		int ok = bits(v) == bits(cases[i].expect);
		pass += ok;
		printf("  %-18s got=0x%016llx want=0x%016llx %s\n",
		       cases[i].in,
		       (unsigned long long)bits(v),
		       (unsigned long long)bits(cases[i].expect),
		       ok ? "OK" : "MISMATCH");
	}
	printf("strtod: %d/%d passed\n", pass, n);
	record(pass, n);
}

/*
 * Exercises the FreeBSD getopt_long() port (lib/libc/stdlib/getopt_long.c +
 * getopt.c) on a synthetic argv, so the code runs at boot independent of the
 * real kernel command line. Parses a short option with argument (-n 42), a
 * GNU long option (--verbose), and confirms the leftover non-option operand
 * (file.db) lands at optind. getopt_long reports bad options via our warnx()
 * glue + _getprogname(); this test stays on the happy path.
 */
static void test_getopt(void)
{
	char *av[] = { "apptests", "-n", "42", "--verbose", "file.db", NULL };
	int ac = (int)(sizeof(av) / sizeof(av[0])) - 1;
	static const struct option longopts[] = {
		{ "verbose", no_argument,       NULL, 'v' },
		{ "number",  required_argument, NULL, 'n' },
		{ NULL,      0,                 NULL,  0  },
	};
	int number = 0, verbose = 0, c;

	optind = 1;	/* reset the parser (static state) for a clean run */
	while ((c = getopt_long(ac, av, "n:v", longopts, NULL)) != -1) {
		switch (c) {
		case 'n': number = atoi(optarg); break;
		case 'v': verbose = 1; break;
		default: break;
		}
	}

	const char *operand = (optind < ac) ? av[optind] : "(none)";
	int ok = (number == 42) && verbose && !strcmp(operand, "file.db");
	printf("getopt_long: -n=%d --verbose=%d operand=%s %s\n",
	       number, verbose, operand, ok ? "OK" : "MISMATCH");
	record(ok, 1);
}

/*
 * Exercises the FreeBSD fnmatch() port (lib/libc/gen/fnmatch.c) — the first
 * file of the gen easy half. Covers literals, the *, ? and [...] meta
 * characters, a [[:digit:]] character class (proves the C-locale wctype/
 * iswctype glue runs), FNM_PATHNAME (slash must match slash) and FNM_CASEFOLD.
 * Each case carries its expected return (0 == match, FNM_NOMATCH otherwise).
 */
static void test_fnmatch(void)
{
	struct { const char *pat; const char *str; int flags; int expect; } cases[] = {
		{ "file.db",      "file.db",   0,            0           },
		{ "*.db",         "file.db",   0,            0           },
		{ "*.db",         "file.txt",  0,            FNM_NOMATCH },
		{ "fi?e.db",      "file.db",   0,            0           },
		{ "[fg]ile.db",   "file.db",   0,            0           },
		{ "[!fg]ile.db",  "file.db",   0,            FNM_NOMATCH },
		{ "[[:digit:]]*", "3rows",     0,            0           },
		{ "[[:digit:]]*", "rows",      0,            FNM_NOMATCH },
		{ "a/b",          "a/b",       FNM_PATHNAME, 0           },
		{ "a*b",          "a/b",       FNM_PATHNAME, FNM_NOMATCH },
		{ "FILE.DB",      "file.db",   FNM_CASEFOLD, 0           },
	};
	int pass = 0, n = sizeof(cases)/sizeof(cases[0]);
	printf("fnmatch checks:\n");
	for (int i = 0; i < n; i++) {
		int got = fnmatch(cases[i].pat, cases[i].str, cases[i].flags);
		int ok = (got == cases[i].expect);
		pass += ok;
		printf("  fnmatch(%-13s,%-9s,%d)=%d want=%d %s\n",
		       cases[i].pat, cases[i].str, cases[i].flags,
		       got, cases[i].expect, ok ? "OK" : "MISMATCH");
	}
	printf("fnmatch: %d/%d passed\n", pass, n);
	record(pass, n);
}

/*
 * Exercises the FreeBSD err(3) family (lib/libc/gen/err.c). Only the
 * non-exiting members are run at boot: warnx (no errno suffix), warn (appends
 * strerror(errno)) and warnc (appends strerror(explicit code)). err()/errx()
 * are link-verified only — they call exit(), which would halt the unikernel.
 * Output goes to stderr (fd 2 < 3 -> Unikraft console). These have no machine
 * verdict (they print, they don't return a value), so they count 1/1 for the
 * fact that the family links and emits without crashing.
 */
static void test_err(void)
{
	printf("err(3) family (warn variants -> stderr -> console):\n");
	warnx("warnx: plain message, code=%d", 7);
	errno = EACCES;
	warn("warn: with current errno");
	warnc(ENOENT, "warnc: explicit code");
	printf("err(3): warnx/warn/warnc emitted above\n");
	record(1, 1);
}

/*
 * Proves the errno bridge. FreeBSD libc reaches errno through *__error();
 * nolibc exposes errno as a TLS int. For the two worlds to agree they must
 * name the SAME storage:
 *   (1) a FreeBSD stdlib source (strtoll, on overflow) sets errno = ERANGE via
 *       the plain errno macro -> proves FreeBSD code reaches nolibc's errno;
 *   (2) the glue int *__error(void){ return &errno; } must hand back &errno
 *       (same address, same value) -> proves a future FreeBSD source that reads
 *       or writes *__error() shares that very errno.
 */
extern int *__error(void);

static void test_errno(void)
{
	int ok_write, ok_addr, ok_val;

	errno = 0;
	(void)strtoll("999999999999999999999999", NULL, 10); /* > LLONG_MAX */
	ok_write = (errno == ERANGE);
	ok_addr  = (__error() == &errno);
	ok_val   = (*__error() == errno);

	printf("errno bridge:\n");
	printf("  strtoll overflow -> errno=%d (ERANGE=%d) : %s\n",
	       errno, ERANGE, ok_write ? "OK" : "FAIL");
	printf("  __error()==&errno : %s ; *__error()==errno (%d) : %s\n",
	       ok_addr ? "OK" : "FAIL", *__error(), ok_val ? "OK" : "FAIL");
	printf("  strerror(errno)=\"%s\"\n", strerror(errno));
	printf("errno: %d/3 passed\n", ok_write + ok_addr + ok_val);
	record(ok_write + ok_addr + ok_val, 3);
}

/*
 * Exercises the FreeBSD gen/basename.c + dirname.c port. These are the modern
 * POSIX.1-2008 in-place variants — they edit and return within the caller's
 * buffer — so each case runs on a fresh mutable copy.
 */
static int chk_bn(const char *in, const char *want)
{
	char buf[64]; strcpy(buf, in);
	const char *got = basename(buf);
	int ok = !strcmp(got, want);
	printf("  basename(%-10s)=%-6s want=%-6s : %s\n", in, got, want, ok ? "OK" : "FAIL");
	return ok;
}
static int chk_dn(const char *in, const char *want)
{
	char buf[64]; strcpy(buf, in);
	const char *got = dirname(buf);
	int ok = !strcmp(got, want);
	printf("  dirname(%-11s)=%-6s want=%-6s : %s\n", in, got, want, ok ? "OK" : "FAIL");
	return ok;
}
static void test_libgen(void)
{
	int ok = 0, n = 0;
	printf("libgen (basename/dirname):\n");
	ok += chk_bn("/usr/lib", "lib"); n++;
	ok += chk_bn("/usr/",    "usr"); n++;
	ok += chk_bn("usr",      "usr"); n++;
	ok += chk_bn("/",        "/");   n++;
	ok += chk_dn("/usr/lib", "/usr"); n++;
	ok += chk_dn("/usr/",    "/");    n++;
	ok += chk_dn("usr",      ".");    n++;
	ok += chk_dn("/",        "/");    n++;
	printf("libgen: %d/%d passed\n", ok, n);
	record(ok, n);
}

/*
 * Exercises the real long-double strtold — stdlib/strtold.c -> gdtoa
 * machdep_ldisx.c (80-bit extended) -> strtorx_l. The discriminating case is
 * c2: "1.0000000000000000005" differs from 1.0 by 5e-19, which is below
 * double's epsilon (~2.2e-16) but above 80-bit long double's (~1.1e-19). The
 * old glue bridge `(long double)strtod(...)` would round it to exactly 1.0 and
 * fail c2; the real extended path keeps it strictly > 1.0. nolibc printf has no
 * %Lf, so we print comparison verdicts, not the values.
 */
static void test_strtold(void)
{
	char *end;
	int c1, c2, c3;

	c1 = (strtold("3.5", &end) == 3.5L && *end == '\0');
	c2 = (strtold("1.0000000000000000005", NULL) - 1.0L > 0.0L);
	c3 = (strtold("0x1p4", NULL) == 16.0L);

	printf("strtold (real 80-bit long double):\n");
	printf("  strtold(\"3.5\")==3.5L & endptr : %s\n", c1 ? "OK" : "FAIL");
	printf("  strtold(\"1.000...0005\")>1.0L (sub-double precision) : %s\n",
	       c2 ? "OK" : "FAIL");
	printf("  strtold(\"0x1p4\")==16.0L (hex float) : %s\n", c3 ? "OK" : "FAIL");
	printf("strtold: %d/3 passed\n", c1 + c2 + c3);
	record(c1 + c2 + c3, 3);
}

/*
 * Exercises the FreeBSD stdtime/strftime.c port. Uses a fixed struct tm (Fri
 * 2026-06-12 14:30:00) so the test is independent of any clock or timezone.
 * %A/%B exercise the C-locale name table (timelocal.c); the rest are the
 * numeric conversions via sprintf_l.
 */
static int chk_sf(const struct tm *tm, const char *fmt, const char *want)
{
	char buf[64];
	size_t n = strftime(buf, sizeof(buf), fmt, tm);
	int ok = (n > 0 && !strcmp(buf, want));
	printf("  strftime(%-12s)=%-21s want=%-21s : %s\n", fmt, buf, want, ok ? "OK" : "FAIL");
	return ok;
}
static void test_strftime(void)
{
	struct tm tm = {0};
	tm.tm_year = 2026 - 1900; tm.tm_mon = 5; tm.tm_mday = 12;
	tm.tm_hour = 14; tm.tm_min = 30; tm.tm_sec = 0;
	tm.tm_wday = 5; tm.tm_yday = 162; tm.tm_isdst = 0;

	int ok = 0, n = 0;
	printf("strftime (FreeBSD stdtime):\n");
	ok += chk_sf(&tm, "%Y-%m-%d", "2026-06-12"); n++;
	ok += chk_sf(&tm, "%H:%M:%S", "14:30:00"); n++;
	ok += chk_sf(&tm, "%A", "Friday"); n++;
	ok += chk_sf(&tm, "%B", "June"); n++;
	ok += chk_sf(&tm, "%Y-%m-%dT%H:%M:%S", "2026-06-12T14:30:00"); n++;
	printf("strftime: %d/%d passed\n", ok, n);
	record(ok, n);
}

/*
 * Exercises the FreeBSD regex port (lib/libc/regex/*). Covers: extended-syntax
 * compile, match / no-match, case sensitivity + REG_ICASE, and subexpression
 * capture via regmatch_t offsets.
 */
static void test_regex(void)
{
	regex_t re, rei, reg;
	regmatch_t m[3];
	int ok = 0, n = 0;
	printf("regex (FreeBSD POSIX regex):\n");

	regcomp(&re, "^[a-z]+[0-9]+$", REG_EXTENDED);
	int c1 = (regexec(&re, "abc123", 0, NULL, 0) == 0);          /* match */
	int c2 = (regexec(&re, "abc", 0, NULL, 0) == REG_NOMATCH);   /* no match */
	int c3 = (regexec(&re, "ABC123", 0, NULL, 0) == REG_NOMATCH);/* case-sensitive */
	regfree(&re);
	printf("  /^[a-z]+[0-9]+$/  abc123=%s abc=%s ABC123=%s\n",
	       c1?"match":"no", c2?"no":"match", c3?"no":"match");
	ok += c1 + c2 + c3; n += 3;

	regcomp(&rei, "^[a-z]+[0-9]+$", REG_EXTENDED | REG_ICASE);
	int c4 = (regexec(&rei, "ABC123", 0, NULL, 0) == 0);         /* ICASE match */
	regfree(&rei);
	printf("  REG_ICASE         ABC123=%s : %s\n", c4?"match":"no", c4?"OK":"FAIL");
	ok += c4; n++;

	regcomp(&reg, "([a-z]+)([0-9]+)", REG_EXTENDED);
	int c5 = 0;
	if (regexec(&reg, "xx_abc123_yy", 3, m, 0) == 0) {
		/* m[1]="abc" at [3,6), m[2]="123" at [6,9) */
		c5 = (m[1].rm_so == 3 && m[1].rm_eo == 6 &&
		      m[2].rm_so == 6 && m[2].rm_eo == 9);
	}
	regfree(&reg);
	printf("  subexpr capture   abc@[%ld,%ld) 123@[%ld,%ld) : %s\n",
	       (long)m[1].rm_so, (long)m[1].rm_eo, (long)m[2].rm_so, (long)m[2].rm_eo,
	       c5?"OK":"FAIL");
	ok += c5; n++;

	printf("regex: %d/%d passed\n", ok, n);
	record(ok, n);
}

/*
 * Exercises the real FreeBSD gen/{getprogname,setprogname}.c. getprogname()
 * returns __progname (default "unikraft" from glue); setprogname() stores the
 * basename of its argument. This also confirms the err(3)/getopt machinery now
 * reads a real, settable program name rather than a glue constant.
 */
static void test_progname(void)
{
	int ok = 0, n = 0;
	printf("progname (getprogname/setprogname):\n");

	int c1 = !strcmp(getprogname(), "unikraft");    /* glue default */
	printf("  getprogname() default = %-10s : %s\n", getprogname(), c1?"OK":"FAIL");
	ok += c1; n++;

	setprogname("/usr/bin/tests");                  /* stores basename */
	int c2 = !strcmp(getprogname(), "tests");
	printf("  after setprogname()   = %-10s : %s\n", getprogname(), c2?"OK":"FAIL");
	ok += c2; n++;

	setprogname("unikraft");                        /* restore for later output */
	printf("progname: %d/%d passed\n", ok, n);
	record(ok, n);
}

/*
 * Exercises the FreeBSD glob() port (lib/libc/gen/glob.c). Unlike fnmatch
 * (pure string), glob walks the filesystem, so this creates a few files in the
 * automounted ramfs (CONFIG_LIBRAMFS) and globs them: the directory walk goes
 * through vfscore opendir/readdir/closedir + nolibc stat/lstat, and each
 * component match goes through the now-ported fnmatch. The third case
 * (GLOB_NOCHECK on a non-matching pattern) is filesystem-independent — it must
 * return the literal pattern. (Needs STACK_SIZE_PAGE_ORDER>=6 — see .config.)
 */
static void test_glob(void)
{
	const char *files[] = { "/g_aaa.db", "/g_bbb.db", "/g_ccc.txt" };
	int created = 0;
	for (unsigned i = 0; i < sizeof(files)/sizeof(files[0]); i++) {
		FILE *f = fopen(files[i], "w");
		if (f) { fputs("x\n", f); fclose(f); created++; }
	}
	printf("glob checks (ramfs: %d/3 test files created):\n", created);

	glob_t g;
	int rc, ok1, ok2, ok3;

	rc = glob("/g_*.db", 0, NULL, &g);
	ok1 = (rc == 0 && g.gl_pathc == 2 &&
	       !strcmp(g.gl_pathv[0], "/g_aaa.db") &&
	       !strcmp(g.gl_pathv[1], "/g_bbb.db"));
	printf("  glob(/g_*.db) rc=%d pathc=%zu -> %s, %s : %s\n", rc, g.gl_pathc,
	       g.gl_pathc > 0 ? g.gl_pathv[0] : "(none)",
	       g.gl_pathc > 1 ? g.gl_pathv[1] : "(none)", ok1 ? "OK" : "MISMATCH");
	globfree(&g);

	rc = glob("/g_*.txt", 0, NULL, &g);
	ok2 = (rc == 0 && g.gl_pathc == 1 && !strcmp(g.gl_pathv[0], "/g_ccc.txt"));
	printf("  glob(/g_*.txt) rc=%d pathc=%zu -> %s : %s\n", rc, g.gl_pathc,
	       g.gl_pathc > 0 ? g.gl_pathv[0] : "(none)", ok2 ? "OK" : "MISMATCH");
	globfree(&g);

	rc = glob("/g_zzz*", GLOB_NOCHECK, NULL, &g);
	ok3 = (rc == 0 && g.gl_pathc == 1 && !strcmp(g.gl_pathv[0], "/g_zzz*"));
	printf("  glob(/g_zzz*,NOCHECK) rc=%d pathc=%zu -> %s : %s\n", rc, g.gl_pathc,
	       g.gl_pathc > 0 ? g.gl_pathv[0] : "(none)", ok3 ? "OK" : "MISMATCH");
	globfree(&g);

	printf("glob: %d/3 passed\n", ok1 + ok2 + ok3);
	record(ok1 + ok2 + ok3, 3);
}

/*
 * Exercises the rand48(3) PRNG family — real FreeBSD gen/*rand48*.c, the 48-bit
 * LCG. Asserts properties rather than exact values:
 * (1) drand48() output lies in [0,1) AND actually varies (a stuck 0.0 would
 *     catch a broken ldexp, since drand48->erand48 builds the double via ldexp);
 * (2) the generator is deterministic under srand48() reseeding;
 * (3) nrand48() with caller-supplied state is deterministic and in [0,2^31).
 */
static void test_rand48(void)
{
	int ok = 0, n = 0;

	double d0 = drand48(), d1 = drand48();
	int c_range = (d0 >= 0.0 && d0 < 1.0 && d1 >= 0.0 && d1 < 1.0);
	int c_vary  = (d0 != d1);             /* a stuck ldexp would make these 0.0 */

	srand48(12345); long a = lrand48();
	srand48(12345); long b = lrand48();
	int c_det = (a == b && a >= 0);

	unsigned short s1[3] = {1, 2, 3};
	unsigned short s2[3] = {1, 2, 3};
	long r1 = nrand48(s1);
	long r2 = nrand48(s2);
	int c_nr = (r1 == r2 && r1 >= 0 && r1 < (1L << 31));

	printf("rand48 (PRNG family):\n");
	printf("  drand48() in [0,1)            : %s\n", c_range ? "OK" : "FAIL");
	printf("  drand48() varies (ldexp live) : %s\n", c_vary ? "OK" : "FAIL");
	printf("  srand48() deterministic       : %s\n", c_det ? "OK" : "FAIL");
	printf("  nrand48() deterministic+range : %s\n", c_nr ? "OK" : "FAIL");
	ok = c_range + c_vary + c_det + c_nr; n = 4;
	printf("rand48: %d/%d passed\n", ok, n);
	record(ok, n);
}

/*
 * fmtcheck(3): returns its first arg if that format string is type-compatible
 * with the template (second arg), else returns the template. Verified by
 * pointer identity (which string came back).
 */
static void test_fmtcheck(void)
{
	const char *f1 = "%d", *t1 = "%d";        /* compatible -> f1 */
	const char *f2 = "%s here", *t2 = "%s there"; /* compatible -> f2 */
	const char *f3 = "%d", *t3 = "%s";        /* mismatch  -> t3 */

	int c1 = (fmtcheck(f1, t1) == f1);
	int c2 = (fmtcheck(f2, t2) == f2);
	int c3 = (fmtcheck(f3, t3) == t3);

	printf("fmtcheck:\n");
	printf("  compatible returns input     : %s\n", c1 ? "OK" : "FAIL");
	printf("  compatible (with args) input : %s\n", c2 ? "OK" : "FAIL");
	printf("  mismatch returns template    : %s\n", c3 ? "OK" : "FAIL");
	printf("fmtcheck: %d/3 passed\n", c1 + c2 + c3);
	record(c1 + c2 + c3, 3);
}

/*
 * getbsize(3): with no $BLOCKSIZE in the (empty) environment, defaults to a
 * 512-byte block. Asserts the default block size and a non-NULL header string.
 */
static void test_getbsize(void)
{
	int hdrlen = -1;
	long blocksize = 0;
	char *hdr = getbsize(&hdrlen, &blocksize);

	int c1 = (hdr != NULL);
	int c2 = (blocksize == 512);
	int c3 = (hdrlen >= 0);

	printf("getbsize:\n");
	printf("  header non-NULL              : %s\n", c1 ? "OK" : "FAIL");
	printf("  default blocksize == 512     : %s (got %ld)\n", c2 ? "OK" : "FAIL", blocksize);
	printf("  headerlen set                : %s\n", c3 ? "OK" : "FAIL");
	printf("getbsize: %d/3 passed\n", c1 + c2 + c3);
	record(c1 + c2 + c3, 3);
}

/*
 * stringlist(3): the StringList growable container. sl_add stores the pointer
 * (no copy), so string literals are used. sl_free(sl, 0) frees the list but not
 * the (literal) strings.
 */
static void test_stringlist(void)
{
	StringList *sl = sl_init();
	int c0 = (sl != NULL);
	int c1 = 0, c2 = 0, c3 = 0;
	if (c0) {
		sl_add(sl, "alpha");
		sl_add(sl, "beta");
		c1 = (sl->sl_cur == 2);
		c2 = (sl_find(sl, "beta") != NULL);
		c3 = (sl_find(sl, "gamma") == NULL);
		sl_free(sl, 0);
	}
	printf("stringlist:\n");
	printf("  sl_init / sl_add (count==2)  : %s\n", c1 ? "OK" : "FAIL");
	printf("  sl_find hit                  : %s\n", c2 ? "OK" : "FAIL");
	printf("  sl_find miss -> NULL         : %s\n", c3 ? "OK" : "FAIL");
	printf("stringlist: %d/3 passed\n", c1 + c2 + c3);
	record(c1 + c2 + c3, 3);
}

/*
 * fmtmsg(3): a no-output classification (MM_NULLMC) must return MM_OK and emit
 * nothing; an MM_PRINT classification writes a formatted line to stderr and also
 * returns MM_OK. We assert the return codes (the stderr line is visible on the
 * console above this verdict).
 */
static void test_fmtmsg(void)
{
	int c1 = (fmtmsg(MM_NULLMC, MM_NULLLBL, MM_NOSEV, MM_NULLTXT,
	                 MM_NULLACT, MM_NULLTAG) == MM_OK);
	int c2 = (fmtmsg(MM_PRINT | MM_APPL | MM_RECOVER, "FBSDPORT:test",
	                 MM_INFO, "fmtmsg self-test", "no action needed",
	                 "FBSDPORT-1") == MM_OK);
	printf("fmtmsg:\n");
	printf("  no-output classification=OK  : %s\n", c1 ? "OK" : "FAIL");
	printf("  MM_PRINT to stderr=OK        : %s\n", c2 ? "OK" : "FAIL");
	printf("fmtmsg: %d/2 passed\n", c1 + c2);
	record(c1 + c2, 2);
}

/*
 * Exercises the FreeBSD lib/libc/inet round-trips: text -> binary -> text.
 * Pure conversion routines with no syscalls, so they run with no socket layer.
 */
static void test_inet(void)
{
	struct in_addr a4;
	unsigned char a6[16];
	char buf[64];
	int pass = 0;

	/* inet_pton(v4) -> inet_ntop(v4) */
	if (inet_pton(AF_INET, "192.0.2.33", &a4) == 1 &&
	    inet_ntop(AF_INET, &a4, buf, sizeof(buf)) != NULL &&
	    strcmp(buf, "192.0.2.33") == 0)
		pass++;

	/* inet_pton(v6) -> inet_ntop(v6), with :: compression preserved */
	if (inet_pton(AF_INET6, "2001:db8::1", a6) == 1 &&
	    inet_ntop(AF_INET6, a6, buf, sizeof(buf)) != NULL &&
	    strcmp(buf, "2001:db8::1") == 0)
		pass++;

	/* inet_addr -> inet_ntoa */
	a4.s_addr = inet_addr("127.0.0.1");
	if (a4.s_addr != INADDR_NONE &&
	    strcmp(inet_ntoa(a4), "127.0.0.1") == 0)
		pass++;

	/* inet_aton */
	if (inet_aton("10.20.30.40", &a4) == 1 &&
	    strcmp(inet_ntoa(a4), "10.20.30.40") == 0)
		pass++;

	/* inet_nsap_addr (hex text -> binary) -> inet_nsap_ntoa (binary -> text) */
	{
		unsigned char nb[8];
		char ntext[64];
		unsigned nlen = inet_nsap_addr("0x47000580ffff", nb, sizeof(nb));
		if (nlen == 6 &&
		    inet_nsap_ntoa((int)nlen, nb, ntext) != NULL &&
		    strcmp(ntext, "0x47.0005.80FF.FF") == 0)
			pass++;
	}

	printf("inet:\n");
	printf("  inet_pton/ntop IPv4 round-trip : %s\n", pass >= 1 ? "OK" : "FAIL");
	printf("  inet_pton/ntop IPv6 round-trip : %s\n", pass >= 2 ? "OK" : "FAIL");
	printf("  inet_addr/inet_ntoa            : %s\n", pass >= 3 ? "OK" : "FAIL");
	printf("  inet_aton/inet_ntoa            : %s\n", pass >= 4 ? "OK" : "FAIL");
	printf("  inet_nsap_addr/inet_nsap_ntoa  : %s\n", pass >= 5 ? "OK" : "FAIL");
	printf("inet: %d/5 passed\n", pass);
	record(pass, 5);
}

/* gdtoa number->string side: g_dfmt/g_ffmt format shortest round-trip decimal.
 * gdtoaimp.h renames the public symbols with a __ prefix (callers normally get
 * the unprefixed names via gdtoa.h's #defines); we call the real symbols. */
extern char *__g_dfmt(char *, double *, int, size_t);
extern char *__g_ffmt(char *, float *, int, size_t);
#define g_dfmt __g_dfmt
#define g_ffmt __g_ffmt

static void test_gdtoa_fmt(void)
{
	/* Round-trip: value -> g_*fmt text -> real strtod/strtof -> bit-exact? */
	static const double dv[] = { 1.5, 0.25, 100.5, 0.1, 3.141592653589793 };
	static const float  fv[] = { 1.5f, 0.25f, 2.5f, 0.1f };
	char buf[64];
	int dp = 0, fp = 0;
	unsigned i;

	for (i = 0; i < sizeof(dv) / sizeof(dv[0]); i++) {
		double d = dv[i];
		if (g_dfmt(buf, &d, 0, sizeof(buf)) != NULL &&
		    strtod(buf, NULL) == d)
			dp++;
	}
	for (i = 0; i < sizeof(fv) / sizeof(fv[0]); i++) {
		float f = fv[i];
		if (g_ffmt(buf, &f, 0, sizeof(buf)) != NULL &&
		    strtof(buf, NULL) == f)
			fp++;
	}
	/* Show one sample rendering so the text form is visible at boot. */
	{ double s = 3.141592653589793; g_dfmt(buf, &s, 0, sizeof(buf)); }
	printf("gdtoa_fmt:\n");
	printf("  g_dfmt round-trip (double)     : %d/5\n", dp);
	printf("  g_ffmt round-trip (float)      : %d/4\n", fp);
	printf("  g_dfmt(pi) = %s\n", buf);
	printf("gdtoa_fmt: %d/9 passed\n", dp + fp);
	record(dp + fp, 9);
}

/*
 * Real environment via Unikraft posix-environ (LIBPOSIX_ENVIRON=y): a
 * compiled-in PATH plus runtime setenv/getenv/unsetenv.
 */
static void test_environ(void)
{
	int pass = 0;
	const char *path = getenv("PATH");          /* compiled-in "PATH=/bin" */
	if (path != NULL && strcmp(path, "/bin") == 0)
		pass++;
	if (setenv("FBSDPORT", "ok", 1) == 0) {
		const char *v = getenv("FBSDPORT");
		if (v != NULL && strcmp(v, "ok") == 0)
			pass++;
	}
	if (setenv("FBSDPORT", "changed", 1) == 0) {
		const char *v = getenv("FBSDPORT");
		if (v != NULL && strcmp(v, "changed") == 0)  /* overwrite=1 honoured */
			pass++;
	}
	if (unsetenv("FBSDPORT") == 0 && getenv("FBSDPORT") == NULL)
		pass++;

	printf("environ:\n");
	printf("  getenv(PATH) compiled-in       : %s\n", pass >= 1 ? "OK" : "FAIL");
	printf("  setenv/getenv round-trip       : %s\n", pass >= 2 ? "OK" : "FAIL");
	printf("  setenv overwrite               : %s\n", pass >= 3 ? "OK" : "FAIL");
	printf("  unsetenv                       : %s\n", pass >= 4 ? "OK" : "FAIL");
	printf("environ: %d/4 passed\n", pass);
	record(pass, 4);
}

int main(int argc, char *argv[])
{
	(void)argc; (void)argv;

	printf("=== FreeBSD-libc-on-Unikraft subsystem coverage tests ===\n\n");

	test_strtod();
	test_getopt();
	test_fnmatch();
	test_err();
	test_errno();
	test_libgen();
	test_strtold();
	test_strftime();
	test_regex();
	test_progname();
	test_glob();
	test_rand48();
	test_fmtcheck();
	test_getbsize();
	test_stringlist();
	test_fmtmsg();
	test_inet();
	test_gdtoa_fmt();
	test_environ();

	printf("\n=== TOTAL: %d/%d checks passed (%s) ===\n",
	       g_pass, g_total,
	       (g_pass == g_total) ? "ALL OK" : "FAILURES PRESENT");
	return (g_pass == g_total) ? 0 : 1;
}
