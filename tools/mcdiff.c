/*
 * mcdiff.c  --  Monte-Carlo per-run CSV differ for the Booster Lander Simulator.
 *
 * Joins two per-run CSVs on the run index and reports what changed between a
 * baseline sweep and a candidate sweep: which runs flipped landed<->crashed,
 * how the crash-cause mix migrated, and how the landed-cohort touchdown
 * distributions (td_v, td_lat) shifted.
 *
 * USAGE
 *   mcdiff base.csv cand.csv [--pad 26] [--json]
 *
 *   --pad P   on-pad lateral threshold in metres (td_lat <= P is on-pad).
 *             Default 26.0 (matches project convention).
 *   --json    emit machine-readable JSON instead of the text report.
 *
 * INPUT FORMAT (verified, from `booster-core --headless --out f.csv`)
 *   header: seed,scenario,run,verdict,fault,td_v,td_lat,td_tilt,settled_tilt,
 *           fuel,max_qbar,peak_qdot,t_total,max_crush
 *   verdict: 0-3 = landed grades (PERFECT/GOOD/HARD/...), 4 = TIPPED, 5 = CRASHED
 *   fault  : 0 = none, 1 = FUEL, (STRUCT/THERMAL/LOC also exist)
 *
 * CAUSE BUCKETS (project taxonomy)
 *   landed   : verdict in {0,1,2,3}
 *   fuel     : not landed AND fault == 1                       (fuel-out)
 *   off-pad  : not landed, fault != 1, td_lat >  pad           (missed the pad)
 *   too-hard : not landed, fault != 1, td_lat <= pad, td_v > 6 (came in hot on-pad)
 *   other    : any remaining non-landed (e.g. TIPPED, soft on-pad crash)
 *
 * JOIN SEMANTICS
 *   Rows are matched by their `run` field. The report covers the intersection
 *   of run indices present in BOTH files; if the sets differ, a warning names
 *   the count and a few example run ids that were dropped from each side.
 *
 * ROBUSTNESS
 *   Missing/unopenable files -> error to stderr, exit 2.
 *   Malformed lines (too few fields / non-numeric key columns) -> skipped and
 *   counted; the count is surfaced in both text and JSON output.
 *
 * BUILD (MSVC, from a VS2022 x64 Native Tools prompt)
 *   cl.exe /O2 /W4 /nologo mcdiff.c
 *
 * C only, zero external dependencies, C89/C99-clean.
 */

#define _CRT_SECURE_NO_WARNINGS   /* strcpy/strncpy are bounds-checked by hand */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ROWS   200000   /* generous; sweeps are <= a few thousand runs */
#define TD_V_HARD  6.0      /* td_v above this on-pad => "too-hard" bucket  */
#define FAULT_FUEL 1

/* ---- cause buckets -------------------------------------------------------*/
enum { B_LANDED = 0, B_OFFPAD, B_TOOHARD, B_FUEL, B_OTHER, N_BUCKET };
static const char *BUCKET_NAME[N_BUCKET] = {
    "landed", "off-pad", "too-hard", "fuel", "other"
};

/* One parsed per-run record (only the fields we actually use). */
typedef struct {
    int    run;
    int    verdict;
    int    fault;
    double td_v;
    double td_lat;
} Row;

typedef struct {
    Row    *rows;
    int     n;
    long    skipped;      /* malformed lines skipped */
    char    scenario[64];
    int     seed;
} Table;

/* ---- small helpers -------------------------------------------------------*/

/* Classify a run into a cause bucket given the on-pad pad radius. */
static int bucket_of(const Row *r, double pad)
{
    if (r->verdict >= 0 && r->verdict <= 3) return B_LANDED;
    if (r->fault == FAULT_FUEL)             return B_FUEL;
    if (r->td_lat > pad)                    return B_OFFPAD;
    if (r->td_v  > TD_V_HARD)               return B_TOOHARD;
    return B_OTHER;
}

static int is_landed_bucket(int b) { return b == B_LANDED; }

/* Emit a JSON string literal for `str`, escaping the characters JSON requires
 * (notably backslash -- Windows paths are full of them). Prints the surrounding
 * quotes too. Keeps our --json output valid for strict parsers. */
static void jstr(const char *str)
{
    const char *p;
    putchar('"');
    for (p = str; *p; p++) {
        switch (*p) {
            case '\\': fputs("\\\\", stdout); break;
            case '"':  fputs("\\\"", stdout); break;
            case '\n': fputs("\\n",  stdout); break;
            case '\r': fputs("\\r",  stdout); break;
            case '\t': fputs("\\t",  stdout); break;
            default:   putchar(*p);           break;
        }
    }
    putchar('"');
}

/*
 * Parse one CSV data line into `out`. Returns 1 on success, 0 if the line is
 * malformed (too few columns or the key columns are non-numeric). We tokenise a
 * local copy so the caller's buffer is untouched, and we read exactly the
 * columns we need by position: run(2) verdict(3) fault(4) td_v(5) td_lat(6).
 */
static int parse_row(const char *line, Row *out)
{
    char buf[1024];
    char *tok, *ctx = NULL;
    int  col = 0;
    int  have_run = 0, have_verdict = 0, have_fault = 0;
    int  have_tdv = 0, have_tdlat = 0;

    /* Copy defensively; skip absurdly long lines rather than overflow. */
    if (strlen(line) >= sizeof(buf)) return 0;
    strcpy(buf, line);

    /* strtok_s is the MSVC-safe reentrant tokeniser. */
    tok = strtok_s(buf, ",", &ctx);
    while (tok) {
        switch (col) {
            case 2: out->run     = atoi(tok);      have_run = 1;     break;
            case 3: out->verdict = atoi(tok);      have_verdict = 1; break;
            case 4: out->fault   = atoi(tok);      have_fault = 1;   break;
            case 5: out->td_v    = atof(tok);      have_tdv = 1;     break;
            case 6: out->td_lat  = atof(tok);      have_tdlat = 1;   break;
            default: break;
        }
        tok = strtok_s(NULL, ",", &ctx);
        col++;
    }

    if (!(have_run && have_verdict && have_fault && have_tdv && have_tdlat))
        return 0;
    return 1;
}

/* Does this look like the header row (non-numeric first field)? */
static int looks_like_header(const char *line)
{
    /* header begins with "seed"; data begins with a digit or sign. */
    return (line[0] == 's' || line[0] == 'S');
}

/*
 * Load a per-run CSV into `t`. On file-open failure prints to stderr and
 * returns 0. Malformed data lines are skipped and counted in t->skipped.
 */
static int load_table(const char *path, Table *t)
{
    FILE *f = NULL;
    char  line[1024];
    int   first = 1;

    if (fopen_s(&f, path, "r") != 0 || !f) {
        fprintf(stderr, "mcdiff: cannot open '%s'\n", path);
        return 0;
    }

    t->rows = (Row *)malloc(sizeof(Row) * MAX_ROWS);
    if (!t->rows) {
        fprintf(stderr, "mcdiff: out of memory\n");
        fclose(f);
        return 0;
    }
    t->n = 0;
    t->skipped = 0;
    t->scenario[0] = '\0';
    t->seed = -1;

    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        /* strip trailing CR/LF */
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        if (len == 0) continue;

        if (first && looks_like_header(line)) { first = 0; continue; }
        first = 0;

        if (t->n >= MAX_ROWS) {
            fprintf(stderr, "mcdiff: row cap %d reached in '%s'; truncating\n",
                    MAX_ROWS, path);
            break;
        }

        {
            Row r;
            if (parse_row(line, &r)) {
                /* capture scenario+seed once, from the first good row */
                if (t->seed < 0) {
                    char scbuf[1024];
                    char *tk, *cx = NULL;
                    int  c = 0;
                    strcpy(scbuf, line);
                    tk = strtok_s(scbuf, ",", &cx);
                    while (tk) {
                        if (c == 0) t->seed = atoi(tk);
                        else if (c == 1) {
                            strncpy(t->scenario, tk, sizeof(t->scenario) - 1);
                            t->scenario[sizeof(t->scenario) - 1] = '\0';
                        }
                        tk = strtok_s(NULL, ",", &cx);
                        c++;
                    }
                }
                t->rows[t->n++] = r;
            } else {
                t->skipped++;
            }
        }
    }

    fclose(f);
    return 1;
}

/* Find a row by run index; linear scan (tables are small). NULL if absent. */
static const Row *find_run(const Table *t, int run)
{
    int i;
    for (i = 0; i < t->n; i++)
        if (t->rows[i].run == run) return &t->rows[i];
    return NULL;
}

/* ---- distribution stats for a landed cohort ------------------------------*/

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

typedef struct { double mean, p50, p95; int n; } Stats;

/* Percentile via nearest-rank on a sorted copy (v must be pre-sorted). */
static double pct(const double *v, int n, double p)
{
    int idx;
    if (n <= 0) return 0.0;
    idx = (int)(p * (n - 1) + 0.5);
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;
    return v[idx];
}

static Stats stats_of(double *v, int n)
{
    Stats s; int i; double sum = 0.0;
    s.n = n; s.mean = s.p50 = s.p95 = 0.0;
    if (n <= 0) return s;
    for (i = 0; i < n; i++) sum += v[i];
    s.mean = sum / n;
    qsort(v, (size_t)n, sizeof(double), cmp_double);
    s.p50 = pct(v, n, 0.50);
    s.p95 = pct(v, n, 0.95);
    return s;
}

/* ---- flip record ---------------------------------------------------------*/
typedef struct {
    int    run;
    int    b_base, b_cand;      /* bucket on each side */
    double bv, bl, cv, cl;      /* base td_v/lat, cand td_v/lat */
} Flip;

/* ==========================================================================*/

int main(int argc, char **argv)
{
    const char *base_path = NULL, *cand_path = NULL;
    double pad = 26.0;
    int    json = 0;
    int    i;

    Table  base, cand;
    int    matrix[N_BUCKET][N_BUCKET];   /* base-bucket x cand-bucket */
    int    base_landed_n = 0, cand_landed_n = 0;
    int    joined = 0;
    int    only_base = 0, only_cand = 0;
    int    ex_base[8], ex_cand[8], neb = 0, nec = 0;

    Flip  *to_landed = NULL, *to_crashed = NULL;
    int    n_to_landed = 0, n_to_crashed = 0;

    /* landed-cohort samples for distribution deltas */
    double *b_ldv = NULL, *b_ldl = NULL, *c_ldv = NULL, *c_ldl = NULL;
    int     nbld = 0, ncld = 0;
    Stats   sb_v, sb_l, sc_v, sc_l;

    /* ---- parse args ---- */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else if (strcmp(argv[i], "--pad") == 0 && i + 1 < argc) {
            pad = atof(argv[++i]);
        } else if (strncmp(argv[i], "--pad=", 6) == 0) {
            pad = atof(argv[i] + 6);
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "mcdiff: unknown option '%s'\n", argv[i]);
            return 2;
        } else if (!base_path) {
            base_path = argv[i];
        } else if (!cand_path) {
            cand_path = argv[i];
        } else {
            fprintf(stderr, "mcdiff: unexpected extra argument '%s'\n", argv[i]);
            return 2;
        }
    }

    if (!base_path || !cand_path) {
        fprintf(stderr,
            "usage: mcdiff base.csv cand.csv [--pad 26] [--json]\n");
        return 2;
    }

    if (!load_table(base_path, &base)) return 2;
    if (!load_table(cand_path, &cand)) { free(base.rows); return 2; }

    /* ---- build the transition matrix + flip lists over the intersection --*/
    memset(matrix, 0, sizeof(matrix));

    to_landed  = (Flip *)malloc(sizeof(Flip) * (base.n ? base.n : 1));
    to_crashed = (Flip *)malloc(sizeof(Flip) * (base.n ? base.n : 1));
    b_ldv = (double *)malloc(sizeof(double) * (base.n ? base.n : 1));
    b_ldl = (double *)malloc(sizeof(double) * (base.n ? base.n : 1));
    c_ldv = (double *)malloc(sizeof(double) * (cand.n ? cand.n : 1));
    c_ldl = (double *)malloc(sizeof(double) * (cand.n ? cand.n : 1));

    /* landed counts + landed-cohort samples, computed per side independently */
    for (i = 0; i < base.n; i++) {
        int b = bucket_of(&base.rows[i], pad);
        if (is_landed_bucket(b)) {
            base_landed_n++;
            b_ldv[nbld]   = base.rows[i].td_v;
            b_ldl[nbld++] = base.rows[i].td_lat;
        }
    }
    for (i = 0; i < cand.n; i++) {
        int b = bucket_of(&cand.rows[i], pad);
        if (is_landed_bucket(b)) {
            cand_landed_n++;
            c_ldv[ncld]   = cand.rows[i].td_v;
            c_ldl[ncld++] = cand.rows[i].td_lat;
        }
    }

    /* intersection walk (drive off base's run ids) */
    for (i = 0; i < base.n; i++) {
        int run = base.rows[i].run;
        const Row *cr = find_run(&cand, run);
        if (!cr) {
            if (neb < 8) ex_base[neb] = run;
            neb++; only_base++;
            continue;
        }
        {
            int bb = bucket_of(&base.rows[i], pad);
            int cb = bucket_of(cr, pad);
            matrix[bb][cb]++;
            joined++;

            /* verdict-flip detection: landed <-> not-landed */
            if (is_landed_bucket(bb) && !is_landed_bucket(cb)) {
                Flip *fp = &to_crashed[n_to_crashed++];
                fp->run = run; fp->b_base = bb; fp->b_cand = cb;
                fp->bv = base.rows[i].td_v; fp->bl = base.rows[i].td_lat;
                fp->cv = cr->td_v;          fp->cl = cr->td_lat;
            } else if (!is_landed_bucket(bb) && is_landed_bucket(cb)) {
                Flip *fp = &to_landed[n_to_landed++];
                fp->run = run; fp->b_base = bb; fp->b_cand = cb;
                fp->bv = base.rows[i].td_v; fp->bl = base.rows[i].td_lat;
                fp->cv = cr->td_v;          fp->cl = cr->td_lat;
            }
        }
    }
    /* runs only present in candidate */
    for (i = 0; i < cand.n; i++) {
        if (!find_run(&base, cand.rows[i].run)) {
            if (nec < 8) ex_cand[nec] = cand.rows[i].run;
            nec++; only_cand++;
        }
    }

    /* ---- distribution deltas (landed cohorts, whole-file) ---- */
    sb_v = stats_of(b_ldv, nbld);
    sb_l = stats_of(b_ldl, nbld);
    sc_v = stats_of(c_ldv, ncld);
    sc_l = stats_of(c_ldl, ncld);

    /* ======================= OUTPUT ======================================= */
    if (json) {
        int r, c, k;
        printf("{\n");
        printf("  \"base\": {\"path\": "); jstr(base_path);
        printf(", \"rows\": %d, \"skipped\": %ld, \"scenario\": ",
               base.n, base.skipped); jstr(base.scenario);
        printf(", \"seed\": %d, \"landed\": %d},\n", base.seed, base_landed_n);
        printf("  \"cand\": {\"path\": "); jstr(cand_path);
        printf(", \"rows\": %d, \"skipped\": %ld, \"scenario\": ",
               cand.n, cand.skipped); jstr(cand.scenario);
        printf(", \"seed\": %d, \"landed\": %d},\n", cand.seed, cand_landed_n);
        printf("  \"pad\": %.3f,\n", pad);
        printf("  \"joined\": %d, \"only_base\": %d, \"only_cand\": %d,\n",
               joined, only_base, only_cand);

        printf("  \"buckets\": [");
        for (k = 0; k < N_BUCKET; k++)
            printf("\"%s\"%s", BUCKET_NAME[k], (k < N_BUCKET-1) ? ", " : "");
        printf("],\n");

        printf("  \"matrix\": [\n");
        for (r = 0; r < N_BUCKET; r++) {
            printf("    [");
            for (c = 0; c < N_BUCKET; c++)
                printf("%d%s", matrix[r][c], (c < N_BUCKET-1) ? ", " : "");
            printf("]%s\n", (r < N_BUCKET-1) ? "," : "");
        }
        printf("  ],\n");

        printf("  \"flips_to_landed\": [\n");
        for (k = 0; k < n_to_landed; k++) {
            Flip *fp = &to_landed[k];
            printf("    {\"run\": %d, \"from\": \"%s\", \"to\": \"%s\", "
                   "\"base_td_v\": %.3f, \"base_td_lat\": %.3f, "
                   "\"cand_td_v\": %.3f, \"cand_td_lat\": %.3f}%s\n",
                   fp->run, BUCKET_NAME[fp->b_base], BUCKET_NAME[fp->b_cand],
                   fp->bv, fp->bl, fp->cv, fp->cl,
                   (k < n_to_landed-1) ? "," : "");
        }
        printf("  ],\n");

        printf("  \"flips_to_crashed\": [\n");
        for (k = 0; k < n_to_crashed; k++) {
            Flip *fp = &to_crashed[k];
            printf("    {\"run\": %d, \"from\": \"%s\", \"to\": \"%s\", "
                   "\"base_td_v\": %.3f, \"base_td_lat\": %.3f, "
                   "\"cand_td_v\": %.3f, \"cand_td_lat\": %.3f}%s\n",
                   fp->run, BUCKET_NAME[fp->b_base], BUCKET_NAME[fp->b_cand],
                   fp->bv, fp->bl, fp->cv, fp->cl,
                   (k < n_to_crashed-1) ? "," : "");
        }
        printf("  ],\n");

        printf("  \"landed_dist\": {\n");
        printf("    \"td_v\":  {\"base\": {\"n\": %d, \"mean\": %.4f, "
               "\"p50\": %.4f, \"p95\": %.4f}, \"cand\": {\"n\": %d, "
               "\"mean\": %.4f, \"p50\": %.4f, \"p95\": %.4f}, "
               "\"d_mean\": %.4f, \"d_p50\": %.4f, \"d_p95\": %.4f},\n",
               sb_v.n, sb_v.mean, sb_v.p50, sb_v.p95,
               sc_v.n, sc_v.mean, sc_v.p50, sc_v.p95,
               sc_v.mean - sb_v.mean, sc_v.p50 - sb_v.p50, sc_v.p95 - sb_v.p95);
        printf("    \"td_lat\": {\"base\": {\"n\": %d, \"mean\": %.4f, "
               "\"p50\": %.4f, \"p95\": %.4f}, \"cand\": {\"n\": %d, "
               "\"mean\": %.4f, \"p50\": %.4f, \"p95\": %.4f}, "
               "\"d_mean\": %.4f, \"d_p50\": %.4f, \"d_p95\": %.4f}\n",
               sb_l.n, sb_l.mean, sb_l.p50, sb_l.p95,
               sc_l.n, sc_l.mean, sc_l.p50, sc_l.p95,
               sc_l.mean - sb_l.mean, sc_l.p50 - sb_l.p50, sc_l.p95 - sb_l.p95);
        printf("  },\n");

        printf("  \"summary\": {\"base_landed\": %d, \"base_n\": %d, "
               "\"cand_landed\": %d, \"cand_n\": %d, "
               "\"flips_to_landed\": %d, \"flips_to_crashed\": %d, "
               "\"net\": %d}\n",
               base_landed_n, base.n, cand_landed_n, cand.n,
               n_to_landed, n_to_crashed, n_to_landed - n_to_crashed);
        printf("}\n");
    } else {
        int r, c, k;

        printf("=== mcdiff ===\n");
        printf("base: %s  (%d runs, scenario=%s seed=%d, landed=%d, %ld skipped)\n",
               base_path, base.n, base.scenario, base.seed, base_landed_n,
               base.skipped);
        printf("cand: %s  (%d runs, scenario=%s seed=%d, landed=%d, %ld skipped)\n",
               cand_path, cand.n, cand.scenario, cand.seed, cand_landed_n,
               cand.skipped);
        printf("pad radius (on-pad if td_lat <= P): %.2f m\n", pad);

        if (base.scenario[0] && cand.scenario[0] &&
            strcmp(base.scenario, cand.scenario) != 0) {
            printf("WARNING: scenario mismatch (base=%s cand=%s) -- "
                   "cohorts may not be comparable.\n",
                   base.scenario, cand.scenario);
        }
        if (only_base || only_cand) {
            printf("WARNING: run sets differ; joining on intersection of %d.\n",
                   joined);
            if (only_base) {
                printf("         %d run(s) only in base (e.g.", only_base);
                for (k = 0; k < neb && k < 8; k++) printf(" %d", ex_base[k]);
                printf("%s)\n", (only_base > 8) ? " ..." : "");
            }
            if (only_cand) {
                printf("         %d run(s) only in cand (e.g.", only_cand);
                for (k = 0; k < nec && k < 8; k++) printf(" %d", ex_cand[k]);
                printf("%s)\n", (only_cand > 8) ? " ..." : "");
            }
        }
        printf("\n");

        /* --- verdict flips --- */
        printf("-- verdict flips (joined runs) --\n");
        printf("crashed -> landed : %d\n", n_to_landed);
        for (k = 0; k < n_to_landed; k++) {
            Flip *fp = &to_landed[k];
            printf("   run %-5d  %-8s -> %-8s   "
                   "td_v %6.3f->%6.3f  td_lat %8.3f->%8.3f\n",
                   fp->run, BUCKET_NAME[fp->b_base], BUCKET_NAME[fp->b_cand],
                   fp->bv, fp->cv, fp->bl, fp->cl);
        }
        printf("landed -> crashed : %d\n", n_to_crashed);
        for (k = 0; k < n_to_crashed; k++) {
            Flip *fp = &to_crashed[k];
            printf("   run %-5d  %-8s -> %-8s   "
                   "td_v %6.3f->%6.3f  td_lat %8.3f->%8.3f\n",
                   fp->run, BUCKET_NAME[fp->b_base], BUCKET_NAME[fp->b_cand],
                   fp->bv, fp->cv, fp->bl, fp->cl);
        }
        printf("\n");

        /* --- cause-transition matrix --- */
        printf("-- cause-transition matrix (rows=base, cols=cand) --\n");
        printf("%-10s", "base\\cand");
        for (c = 0; c < N_BUCKET; c++) printf("%9s", BUCKET_NAME[c]);
        printf("%9s\n", "|sum");
        for (r = 0; r < N_BUCKET; r++) {
            int rsum = 0;
            printf("%-10s", BUCKET_NAME[r]);
            for (c = 0; c < N_BUCKET; c++) { printf("%9d", matrix[r][c]); rsum += matrix[r][c]; }
            printf("%9d\n", rsum);
        }
        printf("%-10s", "|sum");
        for (c = 0; c < N_BUCKET; c++) {
            int csum = 0;
            for (r = 0; r < N_BUCKET; r++) csum += matrix[r][c];
            printf("%9d", csum);
        }
        printf("%9d\n\n", joined);

        /* --- distribution deltas --- */
        printf("-- landed-cohort distribution (whole-file) --\n");
        printf("%-8s %-6s %10s %10s %10s\n", "metric", "side", "mean", "p50", "p95");
        printf("%-8s %-6s %10.4f %10.4f %10.4f\n", "td_v",  "base", sb_v.mean, sb_v.p50, sb_v.p95);
        printf("%-8s %-6s %10.4f %10.4f %10.4f\n", "",      "cand", sc_v.mean, sc_v.p50, sc_v.p95);
        printf("%-8s %-6s %10.4f %10.4f %10.4f\n", "",      "delta",
               sc_v.mean - sb_v.mean, sc_v.p50 - sb_v.p50, sc_v.p95 - sb_v.p95);
        printf("%-8s %-6s %10.4f %10.4f %10.4f\n", "td_lat","base", sb_l.mean, sb_l.p50, sb_l.p95);
        printf("%-8s %-6s %10.4f %10.4f %10.4f\n", "",      "cand", sc_l.mean, sc_l.p50, sc_l.p95);
        printf("%-8s %-6s %10.4f %10.4f %10.4f\n", "",      "delta",
               sc_l.mean - sb_l.mean, sc_l.p50 - sb_l.p50, sc_l.p95 - sb_l.p95);
        printf("(base landed n=%d, cand landed n=%d)\n\n", sb_v.n, sc_v.n);

        /* --- one-line summary --- */
        printf("SUMMARY: base %d/%d -> cand %d/%d landed  |  "
               "+%d crashed->landed, -%d landed->crashed  |  net %+d\n",
               base_landed_n, base.n, cand_landed_n, cand.n,
               n_to_landed, n_to_crashed, n_to_landed - n_to_crashed);
    }

    free(base.rows); free(cand.rows);
    free(to_landed); free(to_crashed);
    free(b_ldv); free(b_ldl); free(c_ldv); free(c_ldl);
    return 0;
}
