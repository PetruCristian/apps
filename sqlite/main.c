#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

/*
 * The per-subsystem libc self-tests that used to live (disabled) here have been
 * moved to their own app: apps/tests/. This file is now just the SQLite demo —
 * it exercises SQLite itself running on the FreeBSD-libc port. The original
 * disabled block is preserved in main.c.old.
 */

/*
 * Exercises SQLite itself: a handful of common SQL functions run through the
 * FreeBSD-libc port. Each scalar case runs a SELECT, reads column 0 as text and
 * compares against the answer known in advance; a final case builds a real
 * table and checks an aggregate. Uses an in-memory database.
 */
static void test_sql(void)
{
	sqlite3 *db = NULL;
	sqlite3_stmt *st;
	char *err = NULL;
	int pass = 0, n, trc = 0;
	long sum = -1;
	int tok;

	struct { const char *sql; const char *want; } cases[] = {
		{ "SELECT upper('abc')",              "ABC"      },
		{ "SELECT lower('UNIKRAFT')",         "unikraft" },
		{ "SELECT length('hello')",           "5"        },
		{ "SELECT substr('unikraft',1,4)",    "unik"     },
		{ "SELECT abs(-7)",                   "7"        },
		{ "SELECT 2+3*4",                     "14"       },
		{ "SELECT replace('a.b.c','.','/')",  "a/b/c"    },
		{ "SELECT trim('  hi  ')",            "hi"       },
		{ "SELECT max(3,9,5)",                "9"        },
		{ "SELECT round(3.14159,2)",          "3.14"     },
		{ "SELECT typeof(42)",                "integer"  },
		{ "SELECT coalesce(NULL,'x')",        "x"        },
	};

	printf("SQL functions (via SQLite on FreeBSD libc):\n");
	if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
		printf("  sqlite3_open(:memory:) FAILED: %s\n", sqlite3_errmsg(db));
		printf("sql: 0/0 passed\n");
		sqlite3_close(db);
		return;
	}

	n = sizeof(cases) / sizeof(cases[0]);
	for (int i = 0; i < n; i++) {
		const char *got = "(null)";
		int ok = 0;
		st = NULL;
		if (sqlite3_prepare_v2(db, cases[i].sql, -1, &st, NULL) == SQLITE_OK &&
		    sqlite3_step(st) == SQLITE_ROW) {
			const unsigned char *t = sqlite3_column_text(st, 0);
			got = t ? (const char *)t : "(null)";
			ok = strcmp(got, cases[i].want) == 0;
		}
		pass += ok;
		printf("  %-32s = %-9s want=%-9s %s\n",
		       cases[i].sql, got, cases[i].want, ok ? "OK" : "MISMATCH");
		sqlite3_finalize(st);
	}
	printf("sql scalar: %d/%d passed\n", pass, n);

	/* A real table: CREATE / INSERT / aggregate query. */
	trc |= sqlite3_exec(db, "CREATE TABLE t(n INTEGER)", NULL, NULL, &err);
	trc |= sqlite3_exec(db, "INSERT INTO t VALUES (10),(20),(30)", NULL, NULL, &err);
	st = NULL;
	if (sqlite3_prepare_v2(db, "SELECT sum(n) FROM t", -1, &st, NULL) == SQLITE_OK &&
	    sqlite3_step(st) == SQLITE_ROW)
		sum = (long)sqlite3_column_int64(st, 0);
	sqlite3_finalize(st);
	tok = (trc == SQLITE_OK && sum == 60);
	printf("  %-32s = %-9ld want=%-9d %s\n",
	       "CREATE/INSERT/SELECT sum(n)", sum, 60, tok ? "OK" : "MISMATCH");
	printf("sql table: %d/1 passed\n", tok ? 1 : 0);

	sqlite3_free(err);
	sqlite3_close(db);
}

int main(int argc, char *argv[]) {
	(void)argc; (void)argv;
	printf("SQLite Version: %s\n", sqlite3_libversion());
	test_sql();
	return 0;
}
