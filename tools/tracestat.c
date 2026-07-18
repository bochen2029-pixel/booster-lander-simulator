/*
 * tracestat.c  --  feature extractor for booster-core `--verbose` traces.
 *
 * Parses a single-run verbose dump (0.5 s cadence telemetry) and surfaces the
 * events an analyst looks for by hand: when the landing burn lit, whether/where
 * vertical velocity was arrested, how far the vehicle climbed afterwards (the
 * min-throttle climb-trap signature), the fuel margin at the end, the final
 * approach quality, and the phase timeline.
 *
 * USAGE
 *   tracestat trace.txt [--json]
 *
 *   --json   emit machine-readable JSON instead of the text report.
 *
 * INPUT FORMAT (verified, from `booster-core --run ... --verbose`)
 *   line 1 : "scenario=entry seed=42 run=14  h0=61085 m  vz0=-1482.9 m/s"
 *   lines  : "  t= 12.00 h= 45791.0 vz= -992.8 thr=1.00 tilt= 2.08 lat= ..."
 *            "          ... vrad= -50.1 qbar= 869 wperp= 0.00 m= 45468 ph=2"
 *   Every telemetry line is a set of `key=value` tokens (whitespace between the
 *   `=` and the number varies). We locate each field by its key, so the parser
 *   is insensitive to column spacing and to extra/re-ordered keys.
 *
 * FEATURES EMITTED
 *   ignition   : first sample where thr jumps > IGN_DTHR above ~0 while phase
 *                has reached the landing burn (ph >= PH_LANDING). Reports
 *                t, h, vz, lat, vrad. This is the landing-burn light-up (the
 *                entry-deorbit burn at the top is deliberately not counted --
 *                see the ph gate).
 *   arrest     : first sample with vz >= 0 at/after ignition. Reports t, h.
 *                (In a climb-trap this fires low, then the vehicle re-ascends.)
 *   max_climb  : max(h) reached at/after arrest, minus arrest h, plus the peak
 *                positive vz after arrest. A large value here is the climb-trap
 *                fingerprint (arrest near the pad -> balloon back up on min
 *                throttle -> burn dry).
 *   fuel_margin: last m (dry-frozen mass proxy). If m stops changing before the
 *                run ends, the burn ran the tanks dry -- we report the freeze
 *                time and the sample count spent frozen.
 *   final_appr : over the last FINAL_WINDOW seconds: mean |vrad|, and the lat
 *                trend (last lat - first lat in the window) -- converging (<0)
 *                or diverging (>0) toward the pad.
 *   phases     : every ph transition with its timestamp and the h/vz there.
 *
 * ROBUSTNESS
 *   Missing/unopenable file -> error to stderr, exit 2.
 *   The header line and any non-telemetry line (no `t=` token) are skipped.
 *   A line missing a required key is skipped and counted.
 *
 * BUILD (MSVC, from a VS2022 x64 Native Tools prompt)
 *   cl.exe /O2 /W4 /nologo tracestat.c
 *
 * C only, zero external dependencies.
 */

#define _CRT_SECURE_NO_WARNINGS   /* header strncpy is length-capped by hand */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_SAMP     20000   /* a run is a few hundred samples */
#define MAX_PHASES   64
#define IGN_DTHR     0.5     /* thr jump that counts as ignition            */
#define IGN_THR_LOW  0.05    /* "engine effectively off" threshold          */
#define PH_LANDING   4       /* landing-burn phase gate for ignition        */
#define FINAL_WINDOW 5.0     /* seconds for the final-approach summary       */
#define FREEZE_GND_H 10.0    /* mass freeze above this h => fuel-out (m)     */
#define CLIMB_TRAP_M 50.0    /* re-ascent above this after arrest => trap    */

typedef struct {
    double t, h, vz, thr, tilt, lat, vrad, qbar, wperp, m;
    int    ph;
    int    have_m;   /* was an 'm=' present on this line? */
} Samp;

typedef struct { double t, h, vz; int ph; } PhaseEvt;

/* Emit a JSON string literal for `str`, escaping backslash/quote/control chars
 * so --json stays valid for strict parsers (Windows paths, header line). */
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
 * Extract the numeric value following "<key>=" in `line`. Skips whitespace
 * between '=' and the number, so "t=  12.00" and "thr=1.00" both work.
 * Returns 1 and writes *out on success, 0 if the key is absent.
 * Matches only a key that starts at a word boundary (preceded by start,
 * space, or tab) so "m=" does not match inside "qbar=" etc.
 */
static int getkey(const char *line, const char *key, double *out)
{
    size_t klen = strlen(key);
    const char *p = line;

    while ((p = strstr(p, key)) != NULL) {
        int boundary = (p == line) || p[-1] == ' ' || p[-1] == '\t';
        if (boundary && p[klen] == '=') {
            const char *q = p + klen + 1;
            char *end = NULL;
            double v;
            while (*q == ' ' || *q == '\t') q++;
            v = strtod(q, &end);
            if (end != q) { *out = v; return 1; }
            return 0;
        }
        p += klen;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    int   json = 0;
    int   i;
    FILE *f = NULL;
    char  line[2048];

    Samp     *s = NULL;
    int       n = 0;
    long      skipped = 0;
    PhaseEvt  phases[MAX_PHASES];
    int       nph = 0;

    char      hdr[512];
    int       have_hdr = 0;

    /* derived features */
    int    ign_i = -1;                 /* index of ignition sample */
    int    arrest_i = -1;              /* index of first vz>=0 at/after ign */
    double peak_h_after = 0.0, peak_vz_after = 0.0;
    int    peak_h_i = -1;
    int    fuel_frozen_i = -1;         /* first index where m stops changing */
    long   frozen_samples = 0;
    double m_final = 0.0;
    int    have_m_final = 0;
    int    fuel_out = 0;               /* freeze happened airborne (dry tanks) */

    /* ---- args ---- */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) json = 1;
        else if (argv[i][0] == '-') {
            fprintf(stderr, "tracestat: unknown option '%s'\n", argv[i]);
            return 2;
        } else if (!path) path = argv[i];
        else { fprintf(stderr, "tracestat: extra arg '%s'\n", argv[i]); return 2; }
    }
    if (!path) {
        fprintf(stderr, "usage: tracestat trace.txt [--json]\n");
        return 2;
    }

    if (fopen_s(&f, path, "r") != 0 || !f) {
        fprintf(stderr, "tracestat: cannot open '%s'\n", path);
        return 2;
    }

    s = (Samp *)malloc(sizeof(Samp) * MAX_SAMP);
    if (!s) { fprintf(stderr, "tracestat: out of memory\n"); fclose(f); return 2; }

    /* ---- parse ---- */
    while (fgets(line, sizeof(line), f)) {
        double t;
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        if (len == 0) continue;

        /* telemetry lines have a 't=' token; everything else is header/noise */
        if (!getkey(line, "t", &t)) {
            if (!have_hdr && strstr(line, "scenario=")) {
                strncpy(hdr, line, sizeof(hdr) - 1);
                hdr[sizeof(hdr) - 1] = '\0';
                have_hdr = 1;
            }
            continue;
        }

        if (n >= MAX_SAMP) {
            fprintf(stderr, "tracestat: sample cap %d reached; truncating\n",
                    MAX_SAMP);
            break;
        }

        {
            Samp r;
            double v;
            int ok = 1;
            r.t = t;
            ok &= getkey(line, "h",    &r.h);
            ok &= getkey(line, "vz",   &r.vz);
            ok &= getkey(line, "thr",  &r.thr);
            /* tilt/lat/vrad/qbar/wperp are optional-but-usually-present */
            r.tilt  = getkey(line, "tilt",  &v) ? v : 0.0;
            r.lat   = getkey(line, "lat",   &v) ? v : 0.0;
            r.vrad  = getkey(line, "vrad",  &v) ? v : 0.0;
            r.qbar  = getkey(line, "qbar",  &v) ? v : 0.0;
            r.wperp = getkey(line, "wperp", &v) ? v : 0.0;
            r.have_m = getkey(line, "m", &r.m);
            if (!r.have_m) r.m = 0.0;
            {
                double phv;
                r.ph = getkey(line, "ph", &phv) ? (int)phv : -1;
            }
            if (!ok) { skipped++; continue; }
            s[n++] = r;
        }
    }
    fclose(f);

    if (n == 0) {
        fprintf(stderr, "tracestat: no telemetry samples parsed from '%s'\n", path);
        free(s);
        return 2;
    }

    /* ---- phase timeline ---- */
    {
        int prev = -9999;
        for (i = 0; i < n; i++) {
            if (s[i].ph != prev && s[i].ph != -1) {
                if (nph < MAX_PHASES) {
                    phases[nph].t = s[i].t; phases[nph].h = s[i].h;
                    phases[nph].vz = s[i].vz; phases[nph].ph = s[i].ph;
                    nph++;
                }
                prev = s[i].ph;
            }
        }
    }

    /* ---- ignition: first thr jump > IGN_DTHR from ~0, at ph >= PH_LANDING ---- */
    for (i = 1; i < n; i++) {
        if (s[i].ph >= PH_LANDING &&
            s[i-1].thr <= IGN_THR_LOW &&
            (s[i].thr - s[i-1].thr) > IGN_DTHR) {
            ign_i = i;
            break;
        }
    }
    /* Fallback: if the phase gate never let a from-zero jump through (e.g. the
     * burn was already lit as ph crossed into landing), take the first sample
     * in ph >= PH_LANDING that has meaningful thrust. */
    if (ign_i < 0) {
        for (i = 0; i < n; i++) {
            if (s[i].ph >= PH_LANDING && s[i].thr > IGN_THR_LOW) { ign_i = i; break; }
        }
    }

    /* ---- arrest: first vz >= 0 at/after ignition ---- */
    if (ign_i >= 0) {
        for (i = ign_i; i < n; i++) {
            if (s[i].vz >= 0.0) { arrest_i = i; break; }
        }
    }

    /* ---- max climb after arrest ---- */
    if (arrest_i >= 0) {
        peak_h_after = s[arrest_i].h;
        peak_h_i = arrest_i;
        peak_vz_after = 0.0;
        for (i = arrest_i; i < n; i++) {
            if (s[i].h > peak_h_after) { peak_h_after = s[i].h; peak_h_i = i; }
            if (s[i].vz > peak_vz_after) peak_vz_after = s[i].vz;
        }
    }

    /* ---- fuel: final m, and freeze detection ----
     * Mass freezes for two very different reasons: the tanks ran DRY in flight
     * (fuel-out), or the vehicle LANDED and the engine cut post-touchdown. We
     * detect the terminal freeze, then classify it by the altitude at freeze:
     * frozen while airborne (h > FREEZE_GND_H) => fuel-out; frozen at/near the
     * ground => normal post-landing engine cut. */
    {
        double last_m = 0.0; int have_last = 0;
        for (i = 0; i < n; i++) {
            if (!s[i].have_m) continue;
            m_final = s[i].m; have_m_final = 1;
            if (have_last && s[i].m == last_m) {
                if (fuel_frozen_i < 0) fuel_frozen_i = i;
            } else {
                /* mass changed again -> not a terminal freeze; reset */
                fuel_frozen_i = -1;
            }
            last_m = s[i].m; have_last = 1;
        }
        if (fuel_frozen_i >= 0) {
            for (i = fuel_frozen_i; i < n; i++)
                if (s[i].have_m && s[i].m == m_final) frozen_samples++;
            /* airborne freeze (well above ground) is the fuel-out fingerprint */
            fuel_out = (s[fuel_frozen_i].h > FREEZE_GND_H) ? 1 : 0;
        }
    }

    /* ---- final approach (last FINAL_WINDOW seconds) ---- */
    {
        double t_end = s[n-1].t;
        double t_cut = t_end - FINAL_WINDOW;
        double sum_absvrad = 0.0; int cnt = 0;
        double lat_first = 0.0, lat_last = 0.0; int have_first = 0;
        for (i = 0; i < n; i++) {
            if (s[i].t >= t_cut) {
                sum_absvrad += fabs(s[i].vrad); cnt++;
                if (!have_first) { lat_first = s[i].lat; have_first = 1; }
                lat_last = s[i].lat;
            }
        }

        /* ================= OUTPUT ================= */
        if (json) {
            int k;
            printf("{\n");
            printf("  \"file\": "); jstr(path); printf(",\n");
            if (have_hdr) { printf("  \"header\": "); jstr(hdr); printf(",\n"); }
            printf("  \"samples\": %d, \"skipped\": %ld, \"t_end\": %.2f,\n",
                   n, skipped, t_end);

            printf("  \"ignition\": ");
            if (ign_i >= 0)
                printf("{\"t\": %.2f, \"h\": %.2f, \"vz\": %.2f, \"lat\": %.2f, "
                       "\"vrad\": %.2f, \"thr\": %.3f, \"ph\": %d},\n",
                       s[ign_i].t, s[ign_i].h, s[ign_i].vz, s[ign_i].lat,
                       s[ign_i].vrad, s[ign_i].thr, s[ign_i].ph);
            else printf("null,\n");

            printf("  \"arrest\": ");
            if (arrest_i >= 0)
                printf("{\"t\": %.2f, \"h\": %.2f, \"vz\": %.2f},\n",
                       s[arrest_i].t, s[arrest_i].h, s[arrest_i].vz);
            else printf("null,\n");

            printf("  \"max_climb_after_arrest\": ");
            if (arrest_i >= 0)
                printf("{\"peak_h\": %.2f, \"climb\": %.2f, \"peak_t\": %.2f, "
                       "\"peak_vz\": %.2f},\n",
                       peak_h_after, peak_h_after - s[arrest_i].h,
                       s[peak_h_i].t, peak_vz_after);
            else printf("null,\n");

            printf("  \"fuel\": {\"m_final\": ");
            if (have_m_final) printf("%.1f", m_final); else printf("null");
            printf(", \"frozen\": %s", (fuel_frozen_i >= 0) ? "true" : "false");
            if (fuel_frozen_i >= 0)
                printf(", \"freeze_t\": %.2f, \"freeze_h\": %.2f, "
                       "\"frozen_samples\": %ld, \"fuel_out\": %s",
                       s[fuel_frozen_i].t, s[fuel_frozen_i].h, frozen_samples,
                       fuel_out ? "true" : "false");
            printf("},\n");

            printf("  \"final_approach\": {\"window_s\": %.1f, "
                   "\"mean_abs_vrad\": %.3f, \"lat_first\": %.2f, "
                   "\"lat_last\": %.2f, \"lat_trend\": %.2f, \"n\": %d},\n",
                   FINAL_WINDOW, (cnt ? sum_absvrad / cnt : 0.0),
                   lat_first, lat_last, lat_last - lat_first, cnt);

            printf("  \"phases\": [");
            for (k = 0; k < nph; k++)
                printf("%s{\"ph\": %d, \"t\": %.2f, \"h\": %.2f, \"vz\": %.2f}%s",
                       (k ? " " : ""), phases[k].ph, phases[k].t,
                       phases[k].h, phases[k].vz, (k < nph-1) ? "," : "");
            printf("]\n");
            printf("}\n");
        } else {
            int k;
            printf("=== tracestat: %s ===\n", path);
            if (have_hdr) printf("%s\n", hdr);
            printf("samples: %d  (t_end=%.2fs, %ld line(s) skipped)\n\n",
                   n, t_end, skipped);

            printf("-- phase timeline --\n");
            for (k = 0; k < nph; k++)
                printf("   ph=%d  @ t=%7.2f   h=%9.1f  vz=%8.1f\n",
                       phases[k].ph, phases[k].t, phases[k].h, phases[k].vz);
            printf("\n");

            printf("-- landing ignition --\n");
            if (ign_i >= 0)
                printf("   t=%.2f  h=%.1f  vz=%.1f  lat=%.1f  vrad=%.1f  "
                       "(thr %.2f, ph=%d)\n",
                       s[ign_i].t, s[ign_i].h, s[ign_i].vz, s[ign_i].lat,
                       s[ign_i].vrad, s[ign_i].thr, s[ign_i].ph);
            else printf("   (no landing-burn ignition detected)\n");
            printf("\n");

            printf("-- arrest (first vz>=0 after ignition) --\n");
            if (arrest_i >= 0)
                printf("   t=%.2f  h=%.1f  vz=%.1f\n",
                       s[arrest_i].t, s[arrest_i].h, s[arrest_i].vz);
            else printf("   (vz never became non-negative after ignition)\n");
            printf("\n");

            printf("-- max climb after arrest --\n");
            if (arrest_i >= 0) {
                printf("   peak h=%.1f @ t=%.2f   climb=%.1f m above arrest   "
                       "peak vz(+)=%.1f\n",
                       peak_h_after, s[peak_h_i].t,
                       peak_h_after - s[arrest_i].h, peak_vz_after);
                if ((peak_h_after - s[arrest_i].h) > CLIMB_TRAP_M)
                    printf("   ** large re-ascent after arrest -- "
                           "min-throttle climb-trap signature **\n");
            } else printf("   (n/a)\n");
            printf("\n");

            printf("-- fuel margin --\n");
            if (have_m_final) printf("   final m=%.1f", m_final);
            else printf("   final m=(none)");
            if (fuel_frozen_i >= 0) {
                if (fuel_out)
                    printf("   FUEL-OUT: mass frozen from t=%.2f at h=%.1f "
                           "(%ld samples airborne on dry tanks)\n",
                           s[fuel_frozen_i].t, s[fuel_frozen_i].h, frozen_samples);
                else
                    printf("   frozen from t=%.2f at h=%.1f (engine cut near "
                           "ground -- normal post-landing, not fuel-out)\n",
                           s[fuel_frozen_i].t, s[fuel_frozen_i].h);
            } else {
                printf("   (mass still changing at end -- burning at cutoff)\n");
            }
            printf("\n");

            printf("-- final approach (last %.0fs) --\n", FINAL_WINDOW);
            printf("   mean|vrad|=%.3f m/s   lat %.1f -> %.1f (trend %+.1f, %s)\n",
                   (cnt ? sum_absvrad / cnt : 0.0), lat_first, lat_last,
                   lat_last - lat_first,
                   (lat_last - lat_first < 0.0) ? "converging" : "diverging");
        }
    }

    free(s);
    return 0;
}
