/*
 * SQLite benchmark for the FreeBSD-libc-on-Unikraft port.
 *
 * Drives SQLite's C API against an in-memory database to measure, on this port:
 *   - correctness  : a fixed set of SQL operations checked against
 *                    independently computed expected values;
 *   - normal load  : time of a light workload (small insert + point lookups);
 *   - heavy load   : time of a bulk insert (in one transaction) + index build
 *                    + aggregates/filters + an unindexed text ORDER BY (sort);
 *   - memory       : peak heap use and allocation count (Unikraft ukalloc
 *                    statistics, CONFIG_LIBUKALLOC_IFSTATS=y).
 *
 * The same source is meant to be built against unikraft+musl as a baseline
 * (see EVALUATION.md) so the port can be compared with the musl libc.
 *
 * Notes on the metrics:
 *   - Timing uses ukplat_monotonic_clock() (guest-side, nanoseconds). The
 *     unikernel runs single-threaded to completion and the timed sections do
 *     no console I/O, so guest CPU time equals the wall time reported here.
 *   - An in-memory (":memory:") database is used so the figures reflect libc +
 *     SQLite CPU/heap behaviour, not a filesystem or block layer.
 */
#include <uk/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sqlite3.h>

#include <uk/plat/time.h>
#include <uk/alloc.h>

#ifndef BENCH_HEAVY_ROWS
#define BENCH_HEAVY_ROWS 100000
#endif
#ifndef BENCH_NORMAL_ROWS
#define BENCH_NORMAL_ROWS 1000
#endif
#ifndef BENCH_NORMAL_LOOKUPS
#define BENCH_NORMAL_LOOKUPS 1000
#endif

static sqlite3 *db;
static int checks_run, checks_ok;

/* ------------------------------------------------------------------ timing */
static uint64_t now_ns(void)
{
	return (uint64_t)ukplat_monotonic_clock();
}
static double ms_since(uint64_t t0)
{
	return (double)(now_ns() - t0) / 1e6;
}

/* ------------------------------------------------------------------ memory */
struct memsnap {
	long long cur;	  /* current bytes held by live allocations */
	long long peak;	  /* peak bytes ever held */
	unsigned long long allocs; /* cumulative number of allocations */
};
static void mem_read(struct memsnap *m)
{
	struct uk_alloc *a = uk_alloc_get_default();
#if CONFIG_LIBUKALLOC_IFSTATS
	m->cur	  = (long long)a->_stats.cur_mem_use;
	m->peak	  = (long long)a->_stats.max_mem_use;
	m->allocs = (unsigned long long)a->_stats.tot_nb_allocs;
#else
	(void)a;
	m->cur = m->peak = -1;
	m->allocs = 0;
#endif
}

/* ------------------------------------------------------------------ SQL helpers */
static int exec_sql(const char *sql)
{
	char *err = NULL;
	int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
	if (rc != SQLITE_OK) {
		printf("  SQL error: %s\n", err ? err : "(unknown)");
		sqlite3_free(err);
	}
	return rc;
}

/* run a query returning a single integer column */
static long long query_i64(const char *sql)
{
	sqlite3_stmt *st;
	long long v = 0;
	if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK)
		return -1;
	if (sqlite3_step(st) == SQLITE_ROW)
		v = sqlite3_column_int64(st, 0);
	sqlite3_finalize(st);
	return v;
}

static void check_i64(const char *name, long long got, long long want)
{
	int ok = (got == want);
	checks_run++;
	checks_ok += ok;
	printf("  %-22s : %lld == %lld  %s\n", name, got, want,
	       ok ? "OK" : "FAIL");
}

/* ------------------------------------------------------------------ workloads */
static double bench_normal(void)
{
	sqlite3_stmt *ins, *sel;
	int i;
	uint64_t t0 = now_ns();

	exec_sql("CREATE TABLE t(id INTEGER PRIMARY KEY, v INTEGER, s TEXT)");
	exec_sql("BEGIN");
	sqlite3_prepare_v2(db, "INSERT INTO t(v,s) VALUES(?,?)", -1, &ins, NULL);
	for (i = 0; i < BENCH_NORMAL_ROWS; i++) {
		char buf[24];
		snprintf(buf, sizeof(buf), "row-%08d", i);
		sqlite3_bind_int64(ins, 1, i);
		sqlite3_bind_text(ins, 2, buf, -1, SQLITE_TRANSIENT);
		sqlite3_step(ins);
		sqlite3_reset(ins);
	}
	sqlite3_finalize(ins);
	exec_sql("COMMIT");

	/* point lookups by primary key */
	sqlite3_prepare_v2(db, "SELECT v FROM t WHERE id=?", -1, &sel, NULL);
	for (i = 0; i < BENCH_NORMAL_LOOKUPS; i++) {
		sqlite3_bind_int64(sel, 1, (i % BENCH_NORMAL_ROWS) + 1);
		sqlite3_step(sel);
		sqlite3_reset(sel);
	}
	sqlite3_finalize(sel);

	return ms_since(t0);
}

static double bench_heavy(double *t_insert, double *t_index,
			  double *t_agg, double *t_sort)
{
	sqlite3_stmt *ins, *st;
	int i;
	uint64_t t_all = now_ns(), t0;

	exec_sql("CREATE TABLE b(id INTEGER PRIMARY KEY, v INTEGER, s TEXT)");

	/* (1) bulk insert in a single transaction */
	t0 = now_ns();
	exec_sql("BEGIN");
	sqlite3_prepare_v2(db, "INSERT INTO b(v,s) VALUES(?,?)", -1, &ins, NULL);
	for (i = 0; i < BENCH_HEAVY_ROWS; i++) {
		char buf[24];
		snprintf(buf, sizeof(buf), "row-%08d", i);
		sqlite3_bind_int64(ins, 1, i);
		sqlite3_bind_text(ins, 2, buf, -1, SQLITE_TRANSIENT);
		sqlite3_step(ins);
		sqlite3_reset(ins);
	}
	sqlite3_finalize(ins);
	exec_sql("COMMIT");
	*t_insert = ms_since(t0);

	/* (2) build a secondary index */
	t0 = now_ns();
	exec_sql("CREATE INDEX b_v ON b(v)");
	*t_index = ms_since(t0);

	/* (3) aggregates and filters (also feed the correctness checks) */
	t0 = now_ns();
	(void)query_i64("SELECT SUM(v) FROM b");
	(void)query_i64("SELECT COUNT(*) FROM b WHERE v >= 25000 AND v < 75000");
	(void)query_i64("SELECT COUNT(*) FROM b WHERE v % 2 = 0");
	*t_agg = ms_since(t0);

	/* (4) unindexed ORDER BY on TEXT over a bounded subset: a real in-memory
	 * sort (strcmp-heavy) whose working set stays well under the heap. (A
	 * full-table text sort buffers every row and saturates the 256 MB heap
	 * on this allocator -- see EVALUATION.md.) */
	t0 = now_ns();
	sqlite3_prepare_v2(db,
			   "SELECT s FROM b WHERE v < 10000 ORDER BY s DESC",
			   -1, &st, NULL);
	while (sqlite3_step(st) == SQLITE_ROW)
		; /* drain the sorted rows */
	sqlite3_finalize(st);
	*t_sort = ms_since(t0);

	return ms_since(t_all);
}

static void correctness(void)
{
	long long n = BENCH_HEAVY_ROWS;
	printf("[correctness]\n");
	check_i64("count",	  query_i64("SELECT COUNT(*) FROM b"), n);
	check_i64("sum",	  query_i64("SELECT SUM(v) FROM b"),
		  n * (n - 1) / 2);
	check_i64("min",	  query_i64("SELECT MIN(v) FROM b"), 0);
	check_i64("max",	  query_i64("SELECT MAX(v) FROM b"), n - 1);
	check_i64("range_25k_75k",
		  query_i64("SELECT COUNT(*) FROM b WHERE v >= 25000 AND v < 75000"),
		  50000);
	check_i64("even",	  query_i64("SELECT COUNT(*) FROM b WHERE v % 2 = 0"),
		  (n + 1) / 2);
	check_i64("text_exact",
		  query_i64("SELECT COUNT(*) FROM b WHERE s = 'row-00000007'"), 1);
	check_i64("text_glob",
		  query_i64("SELECT COUNT(*) FROM b WHERE s GLOB 'row-0000000?'"), 10);
	/* largest TEXT (lexicographic == numeric here) is the last row inserted */
	check_i64("sort_first_id",
		  query_i64("SELECT id FROM b ORDER BY s DESC LIMIT 1"), n);
	printf("correctness: %d/%d passed\n\n", checks_ok, checks_run);
}

int main(int argc, char *argv[])
{
	double t_normal, t_heavy, t_ins, t_idx, t_agg, t_sort;
	struct memsnap m_start, m_end;

	(void)argc;
	(void)argv;

	printf("=== SQLite benchmark (FreeBSD-libc port) ===\n");
	printf("sqlite_version : %s\n", sqlite3_libversion());
	printf("config         : heavy_rows=%d normal_rows=%d db=:memory:\n\n",
	       BENCH_HEAVY_ROWS, BENCH_NORMAL_ROWS);

	if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
		printf("sqlite3_open failed\n");
		return 1;
	}

	/* No filesystem is mounted, so keep temp B-trees/sorts off a temp file
	 * (SQLite's default temp_store=FILE would fail to open one). */
	exec_sql("PRAGMA temp_store = MEMORY");

	mem_read(&m_start);

	t_normal = bench_normal();
	t_heavy	 = bench_heavy(&t_ins, &t_idx, &t_agg, &t_sort);

	mem_read(&m_end);

	correctness();

	printf("[timing] (ms)\n");
	printf("  normal_total         : %.2f\n", t_normal);
	printf("  heavy_insert         : %.2f\n", t_ins);
	printf("  heavy_index          : %.2f\n", t_idx);
	printf("  heavy_aggregates     : %.2f\n", t_agg);
	printf("  heavy_text_sort      : %.2f\n", t_sort);
	printf("  heavy_total          : %.2f\n\n", t_heavy);

	printf("[memory] (bytes, ukalloc IFSTATS)\n");
	printf("  peak_mem_use         : %lld\n", m_end.peak);
	printf("  cur_mem_use_end      : %lld\n", m_end.cur);
	printf("  total_allocs         : %llu\n\n", m_end.allocs);

	sqlite3_close(db);

	printf("benchmark: %s\n",
	       (checks_run > 0 && checks_ok == checks_run) ? "PASS" : "FAIL");
	return 0;
}
