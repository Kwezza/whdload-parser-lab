/* tools_src/whdtlv_report/main.c — CLI wrapper for the TLV CSV export tool
 *
 * Host-only.  Reads a .tlv file, resolves numeric field values back to human-
 * readable tokens/descriptions via the asset CSV definitions, and writes a
 * CSV file suitable for inspection in Excel.
 *
 * Usage:
 *   whdtlv_report --tlv <file.tlv> [--defs <dir>] [--out <file.csv>]
 *                 [--mode wide|long]
 *                 [--include-ids] [--include-desc] [--include-status]
 *                 [--include-effective]
 *                 [--multi-only] [--problems-only]
 *                 [--help]
 *
 * Defaults:
 *   --defs     assets_raw/defs
 *   --out      <tlv_basename>.csv   (written alongside the .tlv if no --out)
 *   --mode     wide
 */

#include "whdtlv/reporting/whdtlv_report_csv.h"
#include "whdtlv/reporting/whdtlv_report_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================================================================
 * Helpers
 * ====================================================================== */

static void print_usage(const char *prog)
{
    printf("Usage: %s --tlv <file.tlv> [options]\n\n", prog ? prog : "whdtlv_report");
    printf("Required:\n");
    printf("  --tlv <path>        Input .tlv file to decode\n\n");
    printf("Options:\n");
    printf("  --defs <dir>        Asset CSV definitions directory  [default: assets_raw/defs]\n");
    printf("  --out <path>        Output CSV file path             [default: <tlv>.csv]\n");
    printf("  --mode wide|long|profile  Export layout             [default: wide]\n");
    printf("                        wide    = one row per variant; multi-values joined with ;\n");
    printf("                        long    = one row per stored field value\n");
    printf("                        profile = selection trace report (requires --profile)\n");
    printf("  --profile <path>    Profile file (.profile) for profile mode\n");
    printf("  --search <pattern>  Group search pattern for profile mode\n");
    printf("  --include-ids       Add extra _ids columns with raw numeric token IDs\n");
    printf("  --include-desc      Add extra _descriptions columns with long CSV descriptions\n");
    printf("  --include-status    Add extra _status columns with resolution status codes\n");
    printf("  --include-effective Add companion _effective columns for TOKEN fields\n");
    printf("                        Shows the value that would be used after applying the\n");
    printf("                        CSV default when no explicit value is stored in the TLV.\n");
    printf("                        source is one of: explicit / default / missing / invalid_default\n");
    printf("  --multi-only        Only export groups that contain more than one variant\n");
    printf("  --problems-only     Only export rows where at least one field failed to resolve\n");
    printf("  --help              Print this help and exit\n\n");
    printf("Exit codes:\n");
    printf("  0  Success\n");
    printf("  1  Bad arguments\n");
    printf("  2  TLV file could not be opened\n");
    printf("  3  TLV structure is invalid\n");
    printf("  4  Output CSV could not be opened\n");
    printf("  5  Out of memory\n");
    printf("  9  Unknown error\n");
}

/* Map WHDTLV_REPORT_* codes to shell exit codes */
static int to_exit_code(int rc)
{
    switch (rc) {
    case WHDTLV_REPORT_OK:             return 0;
    case WHDTLV_REPORT_ERR_BAD_ARG:   return 1;
    case WHDTLV_REPORT_ERR_TLV_OPEN:  return 2;
    case WHDTLV_REPORT_ERR_TLV_PARSE: return 3;
    case WHDTLV_REPORT_ERR_CSV_OPEN:  return 4;
    case WHDTLV_REPORT_ERR_OOM:       return 5;
    default:                           return 9;
    }
}

/* Derive a default output path: replace .tlv extension with .csv,
 * or append .csv if no .tlv suffix.  Writes into buf (size bytes). */
static void derive_output_path(const char *tlv_path, char *buf, size_t size)
{
    size_t      n;
    const char *dot;

    if (!tlv_path || !buf || size == 0u) { return; }
    n   = strlen(tlv_path);
    dot = NULL;

    /* Walk backwards to find the last dot before any path separator */
    {
        const char *p = tlv_path + n;
        while (p > tlv_path) {
            --p;
            if (*p == '.' && dot == NULL) { dot = p; }
            if (*p == '/' || *p == '\\') {
                /* stop: only look at the filename component */
                break;
            }
        }
    }

    if (dot && dot > tlv_path) {
        size_t prefix = (size_t)(dot - tlv_path);
        if (prefix + 4u < size) {
            memcpy(buf, tlv_path, prefix);
            memcpy(buf + prefix, ".csv", 5u); /* includes NUL */
            return;
        }
    }

    /* Fallback: just append ".csv" */
    if (n + 4u < size) {
        memcpy(buf, tlv_path, n);
        memcpy(buf + n, ".csv", 5u);
        return;
    }

    /* If even the fallback overflows, truncate safely */
    strncpy(buf, tlv_path, size - 1u);
    buf[size - 1u] = '\0';
}

/* Print a summary table to stdout */
static void print_summary(const WhdTlvReportSummary *s, const char *out_path)
{
    printf("\n--- Export summary ---\n");
    printf("  Output file          : %s\n", out_path);
    printf("  Groups scanned       : %lu\n",   s->groups_total);
    printf("  Variants scanned     : %lu\n",   s->variants_total);
    printf("  Rows written         : %lu\n",   s->rows_written);
    printf("  Fields written       : %lu\n",   s->fields_written);
    printf("  Values resolved      : %lu\n",   s->values_resolved);
    printf("  Values unresolved    : %lu\n",   s->values_unresolved);
    printf("  Problem rows         : %lu\n",   s->problem_rows);
    printf("  Multi-value fields   : %lu\n",   s->multi_value_fields_seen);
    printf("  Multi-variant groups : %lu\n",   s->multi_variant_groups_seen);
    if (s->effective_explicit + s->effective_default + s->effective_invalid_default > 0u) {
        printf("  Effective explicit   : %lu\n",   s->effective_explicit);
        printf("  Effective default    : %lu\n",   s->effective_default);
        if (s->effective_invalid_default > 0u) {
            printf("  Effective inv_def    : %lu  (WARNING: ambiguous CSV default)\n",
                   s->effective_invalid_default);
        }
    }
    printf("----------------------\n");
}

/* ======================================================================
 * Entry point
 * ====================================================================== */

int main(int argc, char **argv)
{
    const char          *tlv_path       = NULL;
    const char          *defs_dir       = "assets_raw/defs";
    const char          *out_path       = NULL;
    const char          *profile_path   = NULL;
    const char          *search_pattern = NULL;
    int                  mode_is_profile = 0;
    char                 out_buf[1024];
    WhdTlvReportOptions  opts;
    WhdTlvReportSummary  sum;
    int                  i, rc;

    whdtlv_report_options_defaults(&opts);

    /* ---- argument parsing ---- */
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        else if (strcmp(argv[i], "--tlv") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --tlv requires an argument\n");
                return 1;
            }
            tlv_path = argv[++i];
        }
        else if (strcmp(argv[i], "--defs") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --defs requires an argument\n");
                return 1;
            }
            defs_dir = argv[++i];
        }
        else if (strcmp(argv[i], "--out") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --out requires an argument\n");
                return 1;
            }
            out_path = argv[++i];
        }
        else if (strcmp(argv[i], "--mode") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --mode requires wide or long\n");
                return 1;
            }
            ++i;
            if (strcmp(argv[i], "wide") == 0) {
                opts.mode = WHDTLV_REPORT_CSV_WIDE;
            } else if (strcmp(argv[i], "long") == 0) {
                opts.mode = WHDTLV_REPORT_CSV_LONG;
            } else if (strcmp(argv[i], "profile") == 0) {
                mode_is_profile = 1;
            } else {
                fprintf(stderr, "error: --mode must be 'wide', 'long', or 'profile', got '%s'\n", argv[i]);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--include-ids") == 0) {
            opts.include_ids = 1;
        }
        else if (strcmp(argv[i], "--include-desc") == 0) {
            opts.include_descriptions = 1;
        }
        else if (strcmp(argv[i], "--include-status") == 0) {
            opts.include_status = 1;
        }
        else if (strcmp(argv[i], "--include-effective") == 0) {
            opts.include_effective = 1;
        }
        else if (strcmp(argv[i], "--multi-only") == 0) {
            opts.only_multi_variant_groups = 1;
        }
        else if (strcmp(argv[i], "--problems-only") == 0) {
            opts.only_problem_rows = 1;
        }
        else if (strcmp(argv[i], "--profile") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --profile requires an argument\n");
                return 1;
            }
            profile_path = argv[++i];
        }
        else if (strcmp(argv[i], "--search") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --search requires an argument\n");
                return 1;
            }
            search_pattern = argv[++i];
        }
        else {
            fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
            fprintf(stderr, "       run with --help for usage\n");
            return 1;
        }
    }

    if (!tlv_path) {
        fprintf(stderr, "error: --tlv <file> is required\n");
        fprintf(stderr, "       run with --help for usage\n");
        return 1;
    }

    /* Derive output path if not explicitly provided */
    if (!out_path) {
        out_buf[0] = '\0';
        derive_output_path(tlv_path, out_buf, sizeof(out_buf));
        out_path = out_buf;
    }

    /* ---- Profile mode dispatch ---- */
    if (mode_is_profile) {
        WhdTlvProfileReportOptions prof_opts;
        WhdTlvProfileReportSummary prof_sum;
        const char *reason;

        if (!profile_path || profile_path[0] == '\0') {
            fprintf(stderr, "error: --mode profile requires --profile <path>\n");
            fprintf(stderr, "       run with --help for usage\n");
            return 1;
        }

        memset(&prof_opts, 0, sizeof(prof_opts));
        prof_opts.tlv_path        = tlv_path;
        prof_opts.defs_dir        = defs_dir;
        prof_opts.profile_path    = profile_path;
        prof_opts.search_pattern  = search_pattern;
        prof_opts.output_csv_path = out_path;

        printf("whdtlv_report: reading '%s'\n", tlv_path);
        printf("  defs    : %s\n", defs_dir);
        printf("  profile : %s\n", profile_path);
        printf("  out     : %s\n", out_path);
        printf("  mode    : profile\n");
        if (search_pattern && search_pattern[0] != '\0')
            printf("  search  : %s\n", search_pattern);

        memset(&prof_sum, 0, sizeof(prof_sum));
        rc = whdtlv_report_profile_file(&prof_opts, &prof_sum);

        if (rc != WHDTLV_PROFILE_REPORT_OK) {
            switch (rc) {
            case WHDTLV_PROFILE_REPORT_ERR_BAD_ARG:   reason = "bad argument";                break;
            case WHDTLV_PROFILE_REPORT_ERR_TLV_OPEN:  reason = "could not open TLV file";    break;
            case WHDTLV_PROFILE_REPORT_ERR_TLV_PARSE: reason = "TLV structure is invalid";   break;
            case WHDTLV_PROFILE_REPORT_ERR_CSV_OPEN:  reason = "could not open output file"; break;
            case WHDTLV_PROFILE_REPORT_ERR_OOM:       reason = "out of memory";               break;
            case WHDTLV_PROFILE_REPORT_ERR_PROFILE:   reason = "profile load failed";         break;
            default:                                   reason = "unknown error";               break;
            }
            fprintf(stderr, "error: %s (rc=%d)\n", reason, rc);
            return (rc <= WHDTLV_PROFILE_REPORT_ERR_OOM) ? 6 : to_exit_code(rc);
        }

        printf("\n--- Profile export summary ---\n");
        printf("  Output file       : %s\n", out_path);
        printf("  Groups scanned    : %lu\n", prof_sum.groups_total);
        printf("  Variants scanned  : %lu\n", prof_sum.variants_total);
        printf("  Rows written      : %lu\n", prof_sum.rows_written);
        printf("  Winners           : %lu\n", prof_sum.winners);
        printf("  Losers            : %lu\n", prof_sum.losers);
        printf("  Rejected          : %lu\n", prof_sum.rejected);
        printf("  Not eligible      : %lu\n", prof_sum.not_eligible);
        printf("  Dup-suppressed    : %lu\n", prof_sum.dup_suppressed);
        printf("------------------------------\n");
        return 0;
    }

    printf("whdtlv_report: reading '%s'\n", tlv_path);
    printf("  defs : %s\n", defs_dir);
    printf("  out  : %s\n", out_path);
    printf("  mode : %s\n", (opts.mode == WHDTLV_REPORT_CSV_WIDE) ? "wide" : "long");

    memset(&sum, 0, sizeof(sum));
    rc = whdtlv_report_csv_file(tlv_path, defs_dir, out_path, &opts, &sum);

    if (rc != WHDTLV_REPORT_OK) {
        const char *reason;
        switch (rc) {
        case WHDTLV_REPORT_ERR_BAD_ARG:   reason = "bad argument";                break;
        case WHDTLV_REPORT_ERR_TLV_OPEN:  reason = "could not open TLV file";    break;
        case WHDTLV_REPORT_ERR_TLV_PARSE: reason = "TLV structure is invalid";   break;
        case WHDTLV_REPORT_ERR_CSV_OPEN:  reason = "could not open output file"; break;
        case WHDTLV_REPORT_ERR_OOM:       reason = "out of memory";               break;
        default:                           reason = "unknown error";               break;
        }
        fprintf(stderr, "error: %s (rc=%d)\n", reason, rc);
        return to_exit_code(rc);
    }

    print_summary(&sum, out_path);
    return 0;
}

/* End of Text */
