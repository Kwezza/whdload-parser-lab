/* src/whdtlv/reporting/whdtlv_report_csv.c — TLV-to-CSV export
 *
 * Host-only module.  Reads an existing .tlv file, decodes its records, resolves
 * numeric IDs back to human-readable tokens/descriptions via the asset CSVs, and
 * writes a CSV file suitable for inspection in Excel or any spreadsheet tool.
 *
 * Two output modes:
 *   WHDTLV_REPORT_CSV_WIDE — one row per variant, multi-values joined with ';'
 *   WHDTLV_REPORT_CSV_LONG — one row per stored field value
 *
 * See whdtlv_report_csv.h for the full public API.
 */

#include "whdtlv/reporting/whdtlv_report_csv.h"
#include "whdtlv/filtering/tlv_runtime.h"
#include "whdtlv/filtering/tlv_reader.h"
#include "whdtlv/filtering/tlv_variant.h"
#include "whdtlv/filtering/tlv_group.h"
#include "whdtlv/filtering/tlv_filter.h"
#include "whdtlv/core/csv_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================================================================
 * Constants
 * ====================================================================== */

/* Resolution status string literals (never heap-allocated) */
#define ST_OK          "ok"
#define ST_STRING      "string_field"
#define ST_SPECIAL     "special_field"
#define ST_MISSING_CSV "missing_csv"
#define ST_NOT_IN_CSV  "not_in_csv"
#define ST_MALFORMED   "malformed_length"
#define ST_UNKNOWN     "unknown_field_type"
#define ST_EMPTY       "empty_value"

/* Effective-value source strings (--include-effective) */
#define ST_SRC_EXPLICIT         "explicit"
#define ST_SRC_DEFAULT          "default"
#define ST_SRC_MISSING          "missing"
#define ST_SRC_INVALID_DEFAULT  "invalid_default"

/* Buffer for rendering one decoded value as text (numeric or string field) */
#define REPT_VAL_BUF_SIZE 256

/* Buffer for accumulating multi-value cells in wide mode (tokens joined by ';') */
#define REPT_CELL_BUF_SIZE 4096

/* ======================================================================
 * Internal types
 * ====================================================================== */

/* How a TLV field is classified for export purposes */
typedef enum {
    REPT_FIELD_DISPLAY,       /* display_name: variant boundary — stored in variant.filename */
    REPT_FIELD_GROUP_ID,      /* group_id: extracted to variant.group_id, not in fields[] */
    REPT_FIELD_ARCHIVE_INFO,  /* archive_info: 8-byte BE payload in variant.fields[] */
    REPT_FIELD_TOKEN,         /* CSV-backed 4-byte LE uint32 in variant.fields[] */
    REPT_FIELD_STRING,        /* free-form bytes in variant.fields[] */
    REPT_FIELD_UNKNOWN        /* cannot classify (no CSV and not a known special field) */
} ReptFieldKind;

/* Default value metadata for a CSV-backed TOKEN field.
 * Populated once per field in build_field_table(); used by --include-effective. */
typedef struct {
    int      has_default;                    /* 1 = exactly one valid default row */
    int      invalid_default;               /* 1 = >1 default rows (ambiguous)   */
    uint32_t default_id;                    /* CSV ID of the default row          */
    char     default_token[REPT_VAL_BUF_SIZE]; /* short token, e.g. "En"         */
    char     default_desc[REPT_VAL_BUF_SIZE];  /* long description, e.g. "English" */
} ReptFieldDefault;

/* Per-field metadata derived from TlvFieldMap + CRC map during export setup */
typedef struct {
    uint8_t        field_id;
    char           field_name[TLV_RUNTIME_FIELD_NAME_MAX]; /* 32 bytes */
    char           csv_name[TLV_RUNTIME_CSV_NAME_MAX];     /* 64 bytes; empty if none */
    ReptFieldKind  kind;
    int            csv_available; /* 1 = CSV file was found and pre-loaded */
    ReptFieldDefault def_meta;   /* default value for --include-effective  */
} ReptFieldInfo;

/* Export context — owns all loaded data for one whdtlv_report_csv_file() call */
typedef struct {
    TlvRuntime       rt;
    WhdVariantArray  arr;
    WhdGroupSet      gs;
    GlobalCSVManager mgr;
    ReptFieldInfo    fields[TLV_RUNTIME_MAX_FIELDS]; /* one slot per field in TLV map */
    uint8_t          field_count;
    uint8_t          display_fid;
    uint8_t          group_id_fid;
    uint8_t          archive_info_fid;
    int              arr_built;
    int              gs_built;
    int              mgr_init;
} ReptCtx;

/* ======================================================================
 * Low-level helpers
 * ====================================================================== */

/* Portable case-insensitive strcmp (ASCII only) */
static int rept_strcasecmp(const char *a, const char *b)
{
    for (;;) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb || ca == '\0') {
            return (unsigned char)ca - (unsigned char)cb;
        }
        ++a; ++b;
    }
}

/* Decode a 4-byte little-endian uint32 */
static uint32_t decode_le32(const uint8_t *v)
{
    return (uint32_t)v[0]
         | ((uint32_t)v[1] <<  8)
         | ((uint32_t)v[2] << 16)
         | ((uint32_t)v[3] << 24);
}

/* Write one CSV cell, quoting the value when it contains , " CR LF or ;
 * An empty or NULL value writes nothing (empty unquoted cell). */
static void write_csv_cell(FILE *f, const char *s)
{
    const char *p;
    int needs_quote = 0;

    if (!s || !s[0]) { return; }

    for (p = s; *p; ++p) {
        if (*p == '"' || *p == ',' || *p == '\r' || *p == '\n' || *p == ';') {
            needs_quote = 1;
            break;
        }
    }
    if (needs_quote) {
        fputc('"', f);
        for (p = s; *p; ++p) {
            if (*p == '"') { fputc('"', f); } /* RFC 4180: double the quote */
            fputc(*p, f);
        }
        fputc('"', f);
    } else {
        fputs(s, f);
    }
}

/* Write a comma then a CSV-escaped cell */
static void write_sep_cell(FILE *f, const char *s)
{
    fputc(',', f);
    write_csv_cell(f, s);
}

/* 1 if status represents a data quality problem; 0 if it is expected/normal */
static int is_problem_status(const char *s)
{
    return strcmp(s, ST_OK)      != 0
        && strcmp(s, ST_STRING)  != 0
        && strcmp(s, ST_SPECIAL) != 0;
}

/* ======================================================================
 * Context lifecycle
 * ====================================================================== */

static void rept_ctx_free(ReptCtx *ctx)
{
    if (!ctx) { return; }
    if (ctx->gs_built)  { tlv_group_free(&ctx->gs);   ctx->gs_built  = 0; }
    if (ctx->arr_built) { tlv_variant_free(&ctx->arr); ctx->arr_built = 0; }
    if (ctx->mgr_init)  { csv_cache_manager_cleanup(&ctx->mgr); ctx->mgr_init = 0; }
    tlv_runtime_free(&ctx->rt);
}

/* ======================================================================
 * Field info table construction
 * ====================================================================== */

/* Search the TLV CRC map for a csv_name that matches field_name
 * (case-insensitive).  Returns the stored csv_name with its original casing
 * (e.g. "Chipset") so that csv_cache_get_or_load() finds the right file. */
static const char *find_csv_name_in_crc_map(const TlvRuntime *rt, const char *fname)
{
    unsigned long i;
    for (i = 0; i < rt->crc_map.count; ++i) {
        if (rept_strcasecmp(rt->crc_map.entries[i].csv_name, fname) == 0) {
            return rt->crc_map.entries[i].csv_name;
        }
    }
    return NULL;
}

/* Count lines in <base_path>/<csv_name>.csv where the 4th column (flags),
 * after trimming leading/trailing spaces and tabs, equals "default"
 * (case-insensitive).  CR/LF are consumed by fgets.  Returns 0 on failure. */
static int count_csv_default_rows(const char *base_path, const char *csv_name)
{
    char  path[512];
    char  line[1024];
    FILE *fp;
    int   count = 0;

    if (!base_path || !csv_name || !base_path[0] || !csv_name[0]) { return 0; }
    snprintf(path, sizeof(path), "%s/%s.csv", base_path, csv_name);
    fp = fopen(path, "r");
    if (!fp) { return 0; }

    while (fgets(line, (int)sizeof(line), fp)) {
        const char *p   = line;
        int         col = 0;
        const char *col4;
        char        field[64];
        size_t      flen;
        size_t      s;
        size_t      k;

        /* Walk to the start of the 4th column (after 3 commas) */
        col4 = NULL;
        while (*p) {
            if (*p == ',') {
                ++col;
                if (col == 3) { col4 = p + 1; break; }
            }
            ++p;
        }
        if (!col4) { continue; } /* fewer than 4 columns */

        /* Copy field content until comma, CR, LF, or NUL */
        flen = 0u;
        while (*col4 && *col4 != ',' && *col4 != '\r' && *col4 != '\n') {
            if (flen < sizeof(field) - 1u) { field[flen++] = *col4; }
            ++col4;
        }
        field[flen] = '\0';

        /* Trim trailing spaces/tabs */
        while (flen > 0u && (field[flen - 1u] == ' ' || field[flen - 1u] == '\t')) {
            field[--flen] = '\0';
        }
        /* Trim leading spaces/tabs */
        s = 0u;
        while (s < flen && (field[s] == ' ' || field[s] == '\t')) { ++s; }
        if (s > 0u) { memmove(field, field + s, flen - s + 1u); flen -= s; }

        /* Case-insensitive match against "default" (7 chars) */
        if (flen == 7u) {
            static const char def_str[] = "default";
            int match = 1;
            for (k = 0u; k < 7u; ++k) {
                char c = field[k];
                if (c >= 'A' && c <= 'Z') { c = (char)(c + 32); }
                if (c != def_str[k]) { match = 0; break; }
            }
            if (match) { ++count; }
        }
    }

    fclose(fp);
    return count;
}

/* Build the per-field classification table.
 * Called once after TlvRuntime and GlobalCSVManager are both initialised. */
static void build_field_table(ReptCtx *ctx)
{
    uint8_t       i;
    TlvFieldMap  *fm = &ctx->rt.field_map;
    const char   *cname;
    ReptFieldInfo *fi;

    ctx->field_count = fm->count;

    for (i = 0; i < fm->count; ++i) {
        fi = &ctx->fields[i];
        fi->field_id = fm->entries[i].id;
        strncpy(fi->field_name, fm->entries[i].name, sizeof(fi->field_name) - 1);
        fi->field_name[sizeof(fi->field_name) - 1] = '\0';
        fi->csv_name[0]  = '\0';
        fi->csv_available = 0;

        /* Classify special fields first */
        if (fi->field_id == ctx->display_fid) {
            fi->kind = REPT_FIELD_DISPLAY;
            continue;
        }
        if (ctx->group_id_fid != 0u && fi->field_id == ctx->group_id_fid) {
            fi->kind = REPT_FIELD_GROUP_ID;
            continue;
        }
        if (ctx->archive_info_fid != 0u && fi->field_id == ctx->archive_info_fid) {
            fi->kind = REPT_FIELD_ARCHIVE_INFO;
            continue;
        }

        /* Try to find a matching CSV via the embedded CRC map (gives correct
         * file casing e.g. "Chipset"), then fall back to the field name itself. */
        cname = find_csv_name_in_crc_map(&ctx->rt, fi->field_name);
        if (!cname) {
            /* Direct-name fallback: try field_name as the CSV basename */
            if (csv_cache_get_or_load(&ctx->mgr, fi->field_name) != NULL) {
                cname = fi->field_name;
            }
        }

        if (cname) {
            strncpy(fi->csv_name, cname, sizeof(fi->csv_name) - 1);
            fi->csv_name[sizeof(fi->csv_name) - 1] = '\0';
            /* Pre-load the CSV; csv_cache_get_or_load is graceful if the file
             * is missing — it simply returns NULL without aborting. */
            fi->csv_available =
                (csv_cache_get_or_load(&ctx->mgr, fi->csv_name) != NULL) ? 1 : 0;
            fi->kind = REPT_FIELD_TOKEN;

            /* Populate default metadata for --include-effective */
            if (fi->csv_available) {
                bool        has_def;
                uint32_t    def_id;
                int         def_count;
                const char *def_tok;
                const char *def_desc;

                has_def   = 0;
                def_id    = csv_cache_get_default_token(&ctx->mgr, fi->csv_name, &has_def);
                def_count = 0;
                def_tok   = NULL;
                def_desc  = NULL;

                if (has_def) {
                    def_tok   = csv_cache_reverse_lookup(&ctx->mgr, fi->csv_name, def_id, 0);
                    def_desc  = csv_cache_reverse_lookup(&ctx->mgr, fi->csv_name, def_id, 1);
                    def_count = count_csv_default_rows(ctx->mgr.csv_base_path, fi->csv_name);
                    fi->def_meta.default_id = def_id;
                    if (def_tok) {
                        strncpy(fi->def_meta.default_token, def_tok, REPT_VAL_BUF_SIZE - 1u);
                        fi->def_meta.default_token[REPT_VAL_BUF_SIZE - 1u] = '\0';
                    }
                    if (def_desc) {
                        strncpy(fi->def_meta.default_desc, def_desc, REPT_VAL_BUF_SIZE - 1u);
                        fi->def_meta.default_desc[REPT_VAL_BUF_SIZE - 1u] = '\0';
                    }
                    if (def_count > 1) {
                        fi->def_meta.invalid_default = 1; /* ambiguous: multiple defaults */
                    } else {
                        fi->def_meta.has_default = 1;     /* exactly one clean default    */
                    }
                }
            }
        } else {
            fi->kind = REPT_FIELD_STRING;
        }
    }
}

/* Find ReptFieldInfo by field_id.  Linear scan — field count is at most 252. */
static const ReptFieldInfo *find_field_info(const ReptCtx *ctx, uint8_t fid)
{
    uint8_t i;
    for (i = 0; i < ctx->field_count; ++i) {
        if (ctx->fields[i].field_id == fid) { return &ctx->fields[i]; }
    }
    return NULL;
}

/* ======================================================================
 * Value resolution
 * ====================================================================== */

/* Resolve one raw TLV field value to a human-readable form.
 *
 * val_buf   — caller-provided buffer of REPT_VAL_BUF_SIZE bytes.  Used for:
 *               - numeric IDs rendered as decimal strings (token fields)
 *               - raw byte content (string fields)
 * token_out — set to the short resolved token (lowercase from CSV) or the
 *             raw string value for string fields.  NULL if unresolved.
 * desc_out  — set to the long-form description from the CSV if a separate
 *             long_name column exists; otherwise equals token_out or NULL.
 * sum       — optional summary counters to update (pass NULL to skip).
 *
 * Returns a status literal from the ST_* set.
 */
static const char *resolve_value(
    ReptCtx                *ctx,
    const ReptFieldInfo    *fi,
    const WhdTlvFieldValue *fv,
    char                   *val_buf,
    const char            **token_out,
    const char            **desc_out,
    WhdTlvReportSummary    *sum)
{
    *token_out = NULL;
    *desc_out  = NULL;
    val_buf[0] = '\0';

    if (!fv || fv->length == 0u) {
        if (sum) { sum->values_unresolved++; }
        return ST_EMPTY;
    }

    switch (fi->kind) {
    case REPT_FIELD_DISPLAY:
    case REPT_FIELD_GROUP_ID:
    case REPT_FIELD_ARCHIVE_INFO:
        /* Handled at a higher level; should not reach here in normal flow */
        return ST_SPECIAL;

    case REPT_FIELD_TOKEN: {
        uint32_t    id;
        const char *tok;
        const char *desc;

        if (fv->length != 4u && fv->length != 2u) {
            snprintf(val_buf, REPT_VAL_BUF_SIZE, "[len=%u]", (unsigned)fv->length);
            if (sum) { sum->values_unresolved++; }
            return ST_MALFORMED;
        }

        if (!fi->csv_available) {
            /* Still render the raw number */
            if (fv->length == 2u) {
                snprintf(val_buf, REPT_VAL_BUF_SIZE, "0x%04X",
                         (unsigned)((uint32_t)fv->value[0] | ((uint32_t)fv->value[1] << 8)));
            } else {
                snprintf(val_buf, REPT_VAL_BUF_SIZE, "%lu", (unsigned long)decode_le32(fv->value));
            }
            if (sum) { sum->values_unresolved++; }
            return ST_MISSING_CSV;
        }

        /* ---- 2-byte bitmask (e.g. language) ---- */
        if (fv->length == 2u) {
            uint16_t bits = (uint16_t)fv->value[0] | ((uint16_t)fv->value[1] << 8);
            size_t   tok_len = 0, desc_len = 0;
            int      bit, resolved_any = 0, first = 1;
            /* static buffers: bitmask fields only appear in resolve_value which is
             * always single-threaded; the host reporter has no threading */
            static char bm_tok[REPT_VAL_BUF_SIZE];
            static char bm_desc[REPT_VAL_BUF_SIZE];
            bm_tok[0] = '\0'; bm_desc[0] = '\0';

            for (bit = 0; bit < 16; ++bit) {
                const char *bt, *bd;
                size_t slen;
                if (!(bits & (uint16_t)(1u << bit))) { continue; }
                id = (uint32_t)(bit + 1);
                bt = csv_cache_reverse_lookup(&ctx->mgr, fi->csv_name, id, 0);
                bd = csv_cache_reverse_lookup(&ctx->mgr, fi->csv_name, id, 1);
                if (!bt) { continue; }
                resolved_any = 1;
                if (!first) {
                    if (tok_len  < REPT_VAL_BUF_SIZE - 2u) { bm_tok[tok_len++]  = ';'; bm_tok[tok_len]  = '\0'; }
                    if (desc_len < REPT_VAL_BUF_SIZE - 2u) { bm_desc[desc_len++] = ';'; bm_desc[desc_len] = '\0'; }
                }
                first = 0;
                slen = strlen(bt);
                if (tok_len + slen < REPT_VAL_BUF_SIZE - 1u) {
                    memcpy(bm_tok + tok_len, bt, slen); tok_len += slen; bm_tok[tok_len] = '\0';
                }
                slen = strlen(bd ? bd : bt);
                if (desc_len + slen < REPT_VAL_BUF_SIZE - 1u) {
                    memcpy(bm_desc + desc_len, bd ? bd : bt, slen); desc_len += slen; bm_desc[desc_len] = '\0';
                }
            }

            snprintf(val_buf, REPT_VAL_BUF_SIZE, "0x%04X", (unsigned)bits);
            if (!resolved_any) {
                if (sum) { sum->values_unresolved++; }
                return ST_NOT_IN_CSV;
            }
            *token_out = bm_tok;
            *desc_out  = bm_desc;
            if (sum) { sum->values_resolved++; }
            return ST_OK;
        }

        /* ---- 4-byte single token ID ---- */
        id = decode_le32(fv->value);
        snprintf(val_buf, REPT_VAL_BUF_SIZE, "%lu", (unsigned long)id);

        tok  = csv_cache_reverse_lookup(&ctx->mgr, fi->csv_name, id, 0 /* short */);
        desc = csv_cache_reverse_lookup(&ctx->mgr, fi->csv_name, id, 1 /* long  */);
        if (!tok) {
            if (sum) { sum->values_unresolved++; }
            return ST_NOT_IN_CSV;
        }
        *token_out = tok;
        *desc_out  = desc; /* equals tok if no separate long_name */
        if (sum) { sum->values_resolved++; }
        return ST_OK;
    }

    case REPT_FIELD_STRING: {
        size_t n = (fv->length < (unsigned)(REPT_VAL_BUF_SIZE - 1))
                 ? fv->length : (unsigned)(REPT_VAL_BUF_SIZE - 1);

        /* A 2- or 4-byte field whose bytes are not all printable ASCII is a
         * stored integer (e.g. SPS catalog number), not a text value.
         * Render it as an unsigned decimal so the reporter outputs "1653"
         * instead of binary garbage. */
        if (fv->length == 2u || fv->length == 4u) {
            size_t k;
            int printable = 1;
            for (k = 0u; k < fv->length; ++k) {
                unsigned char c = fv->value[k];
                if (c < 0x20u || c > 0x7Eu) { printable = 0; break; }
            }
            if (!printable) {
                uint32_t v;
                if (fv->length == 2u) {
                    v = (uint32_t)fv->value[0] | ((uint32_t)fv->value[1] << 8);
                } else {
                    v = decode_le32(fv->value);
                }
                snprintf(val_buf, REPT_VAL_BUF_SIZE, "%lu", (unsigned long)v);
                *token_out = val_buf;
                if (sum) { sum->values_resolved++; }
                return ST_STRING;
            }
        }

        memcpy(val_buf, fv->value, n);
        val_buf[n] = '\0';
        *token_out = val_buf;
        if (sum) { sum->values_resolved++; }
        return ST_STRING;
    }

    case REPT_FIELD_UNKNOWN:
    default:
        if (fv->length == 4u) {
            snprintf(val_buf, REPT_VAL_BUF_SIZE, "%lu",
                     (unsigned long)decode_le32(fv->value));
        } else {
            snprintf(val_buf, REPT_VAL_BUF_SIZE, "[len=%u]", (unsigned)fv->length);
        }
        if (sum) { sum->values_unresolved++; }
        return ST_UNKNOWN;
    }
}

/* ======================================================================
 * Archive-info helper
 * ====================================================================== */

/* Scan variant.fields[] for the archive_info entry and decode its 8-byte BE
 * payload.  Returns 1 on success, 0 if the field is absent or malformed. */
static int decode_archive_info(const ReptCtx        *ctx,
                               const WhdVariantView *v,
                               uint32_t             *size_kib_out,
                               uint32_t             *crc32_out)
{
    unsigned short i;

    *size_kib_out = 0u;
    *crc32_out    = 0u;

    if (ctx->archive_info_fid == 0u) { return 0; }

    for (i = 0u; i < v->field_count; ++i) {
        if (v->fields[i].field_id == ctx->archive_info_fid) {
            if (v->fields[i].length == 8u) {
                *size_kib_out = tlv_read_u32_be(v->fields[i].value);
                *crc32_out    = tlv_read_u32_be(v->fields[i].value + 4u);
                return 1;
            }
            return 0; /* malformed payload length */
        }
    }
    return 0; /* field absent */
}

/* ======================================================================
 * Problem pre-scan (used by only_problem_rows filter in wide mode)
 * ====================================================================== */

/* Returns 1 if any interior field in the variant resolves to a problem status.
 * When include_effective is active also flags variants where a TOKEN CSV has
 * more than one 'default' row (invalid_default), since that is a data-quality
 * issue that the caller should surface. */
static int variant_has_problem(ReptCtx *ctx, const WhdVariantView *v,
                               const WhdTlvReportOptions *opts)
{
    unsigned short      i;
    char                val_buf[REPT_VAL_BUF_SIZE];
    const char         *tok, *desc;
    const char         *status;
    const ReptFieldInfo *fi;

    for (i = 0u; i < v->field_count; ++i) {
        fi = find_field_info(ctx, v->fields[i].field_id);
        if (!fi) { continue; }
        if (fi->kind == REPT_FIELD_DISPLAY
         || fi->kind == REPT_FIELD_GROUP_ID
         || fi->kind == REPT_FIELD_ARCHIVE_INFO) { continue; }

        status = resolve_value(ctx, fi, &v->fields[i], val_buf, &tok, &desc, NULL);
        if (is_problem_status(status)) { return 1; }
    }

    /* With --include-effective, an ambiguous CSV default (>1 default rows) is
     * a data-quality problem regardless of the variant's explicit values. */
    if (opts->include_effective) {
        for (i = 0u; i < ctx->field_count; ++i) {
            fi = &ctx->fields[i];
            if (fi->kind == REPT_FIELD_TOKEN && fi->def_meta.invalid_default) { return 1; }
        }
    }

    return 0;
}

/* ======================================================================
 * Wide-mode export
 * ====================================================================== */

static void write_wide_header(FILE *f, const ReptCtx *ctx,
                              const WhdTlvReportOptions *opts)
{
    uint8_t i;
    /* +32 to accommodate the longest suffix: "_effective_descriptions" (23 chars) */
    char    col[TLV_RUNTIME_FIELD_NAME_MAX + 32];

    fputs("group_id,group_name,display_name,archive_size_kib,archive_crc32", f);

    for (i = 0u; i < ctx->field_count; ++i) {
        const ReptFieldInfo *fi = &ctx->fields[i];
        if (fi->kind == REPT_FIELD_DISPLAY
         || fi->kind == REPT_FIELD_GROUP_ID
         || fi->kind == REPT_FIELD_ARCHIVE_INFO) { continue; }

        fputc(',', f);
        fputs(fi->field_name, f);

        if (opts->include_ids) {
            snprintf(col, sizeof(col), "%s_ids", fi->field_name);
            write_sep_cell(f, col);
        }
        if (opts->include_descriptions) {
            snprintf(col, sizeof(col), "%s_descriptions", fi->field_name);
            write_sep_cell(f, col);
        }
        if (opts->include_status) {
            snprintf(col, sizeof(col), "%s_status", fi->field_name);
            write_sep_cell(f, col);
        }

        /* Effective companion columns — TOKEN fields only */
        if (opts->include_effective && fi->kind == REPT_FIELD_TOKEN) {
            snprintf(col, sizeof(col), "%s_effective", fi->field_name);
            write_sep_cell(f, col);
            if (opts->include_descriptions) {
                snprintf(col, sizeof(col), "%s_effective_descriptions", fi->field_name);
                write_sep_cell(f, col);
            }
            if (opts->include_ids) {
                snprintf(col, sizeof(col), "%s_effective_ids", fi->field_name);
                write_sep_cell(f, col);
            }
            if (opts->include_status) {
                snprintf(col, sizeof(col), "%s_effective_status", fi->field_name);
                write_sep_cell(f, col);
            }
        }
    }
    fputc('\n', f);
}

static void write_wide_row(FILE *f,
                           ReptCtx                   *ctx,
                           const WhdVariantView      *v,
                           const char                *group_name,
                           const WhdTlvReportOptions *opts,
                           WhdTlvReportSummary       *sum)
{
    uint8_t  i;
    uint32_t size_kib, crc32;
    int      has_archive;

    /* group_id */
    if (v->has_group_id) { fprintf(f, "%u", (unsigned)v->group_id); }

    /* group_name */
    fputc(',', f);
    if (group_name) { write_csv_cell(f, group_name); }

    /* display_name */
    fputc(',', f);
    write_csv_cell(f, v->filename ? v->filename : "");

    /* archive_size_kib, archive_crc32 */
    has_archive = decode_archive_info(ctx, v, &size_kib, &crc32);
    fputc(',', f);
    if (has_archive) { fprintf(f, "%lu", (unsigned long)size_kib); }
    fputc(',', f);
    if (has_archive) { fprintf(f, "%08lX", (unsigned long)crc32); }

    /* One data column (or column-group) per non-special field */
    for (i = 0u; i < ctx->field_count; ++i) {
        const ReptFieldInfo *fi = &ctx->fields[i];
        unsigned short       j;
        int                  value_count, multi_seen;

        /* Cell accumulation buffers (stack; reset each column iteration) */
        char   cell_tok[REPT_CELL_BUF_SIZE];
        char   cell_ids[REPT_CELL_BUF_SIZE];
        char   cell_desc[REPT_CELL_BUF_SIZE];
        char   cell_stat[REPT_CELL_BUF_SIZE];
        size_t tok_len, ids_len, desc_len, stat_len;

        if (fi->kind == REPT_FIELD_DISPLAY
         || fi->kind == REPT_FIELD_GROUP_ID
         || fi->kind == REPT_FIELD_ARCHIVE_INFO) { continue; }

        cell_tok[0]  = '\0'; tok_len  = 0u;
        cell_ids[0]  = '\0'; ids_len  = 0u;
        cell_desc[0] = '\0'; desc_len = 0u;
        cell_stat[0] = '\0'; stat_len = 0u;
        value_count  = 0;
        multi_seen   = 0;

        /* Collect all variant.fields[] entries for this field_id */
        for (j = 0u; j < v->field_count; ++j) {
            const WhdTlvFieldValue *fv = &v->fields[j];
            char                    val_buf[REPT_VAL_BUF_SIZE];
            const char             *tok = NULL, *desc = NULL;
            const char             *status;
            size_t                  slen;

            if (fv->field_id != fi->field_id) { continue; }

            /* Separator for subsequent values */
            if (value_count > 0) {
                multi_seen = 1;
#define APPEND_SEP(buf, len) \
    if ((len) < REPT_CELL_BUF_SIZE - 2u) { (buf)[(len)++] = ';'; (buf)[(len)] = '\0'; }
                APPEND_SEP(cell_tok,  tok_len)
                APPEND_SEP(cell_ids,  ids_len)
                APPEND_SEP(cell_desc, desc_len)
                APPEND_SEP(cell_stat, stat_len)
#undef APPEND_SEP
            }

            status = resolve_value(ctx, fi, fv, val_buf, &tok, &desc, sum);

            /* Append to token accumulator */
            {
                const char *t = tok ? tok : "";
                slen = strlen(t);
                if (tok_len + slen < REPT_CELL_BUF_SIZE - 1u) {
                    memcpy(cell_tok + tok_len, t, slen);
                    tok_len += slen;
                    cell_tok[tok_len] = '\0';
                }
            }

            /* Append to raw-ID accumulator */
            if (opts->include_ids) {
                slen = strlen(val_buf);
                if (ids_len + slen < REPT_CELL_BUF_SIZE - 1u) {
                    memcpy(cell_ids + ids_len, val_buf, slen);
                    ids_len += slen;
                    cell_ids[ids_len] = '\0';
                }
            }

            /* Append to description accumulator */
            if (opts->include_descriptions) {
                /* Prefer long description; fall back to token when identical */
                const char *d = (desc && desc != tok) ? desc : (tok ? tok : "");
                slen = strlen(d);
                if (desc_len + slen < REPT_CELL_BUF_SIZE - 1u) {
                    memcpy(cell_desc + desc_len, d, slen);
                    desc_len += slen;
                    cell_desc[desc_len] = '\0';
                }
            }

            /* Append to status accumulator */
            if (opts->include_status) {
                slen = strlen(status);
                if (stat_len + slen < REPT_CELL_BUF_SIZE - 1u) {
                    memcpy(cell_stat + stat_len, status, slen);
                    stat_len += slen;
                    cell_stat[stat_len] = '\0';
                }
            }

            ++value_count;
            if (is_problem_status(status) && sum) { sum->problem_rows++; }
        }

        if (multi_seen && sum) { sum->multi_value_fields_seen++; }

        /* Write token cell (always present, even if empty) */
        fputc(',', f);
        write_csv_cell(f, cell_tok);

        if (opts->include_ids) {
            fputc(',', f);
            write_csv_cell(f, cell_ids);
        }
        if (opts->include_descriptions) {
            fputc(',', f);
            write_csv_cell(f, cell_desc);
        }
        if (opts->include_status) {
            fputc(',', f);
            write_csv_cell(f, cell_stat);
        }

        if (value_count > 0 && sum) { sum->fields_written++; }

        /* ------ Effective value columns (TOKEN fields only) ------ */
        if (opts->include_effective && fi->kind == REPT_FIELD_TOKEN) {
            const char *eff_src;
            const char *eff_tok;
            const char *eff_desc;
            char        eff_id_buf[REPT_VAL_BUF_SIZE];

            eff_id_buf[0] = '\0';

            if (value_count > 0) {
                /* Explicit: mirror the accumulated explicit value */
                eff_src  = ST_SRC_EXPLICIT;
                eff_tok  = cell_tok;
                eff_desc = cell_desc;
                if (opts->include_ids) {
                    strncpy(eff_id_buf, cell_ids, sizeof(eff_id_buf) - 1u);
                    eff_id_buf[sizeof(eff_id_buf) - 1u] = '\0';
                }
                if (sum) { sum->effective_explicit++; }
            } else if (fi->def_meta.has_default) {
                /* Default: use the CSV default row */
                eff_src  = ST_SRC_DEFAULT;
                eff_tok  = fi->def_meta.default_token[0] ? fi->def_meta.default_token : NULL;
                eff_desc = fi->def_meta.default_desc[0]  ? fi->def_meta.default_desc  : NULL;
                if (opts->include_ids) {
                    snprintf(eff_id_buf, sizeof(eff_id_buf), "%lu",
                             (unsigned long)fi->def_meta.default_id);
                }
                if (sum) { sum->effective_default++; }
            } else if (fi->def_meta.invalid_default) {
                eff_src  = ST_SRC_INVALID_DEFAULT;
                eff_tok  = fi->def_meta.default_token[0] ? fi->def_meta.default_token : NULL;
                eff_desc = fi->def_meta.default_desc[0]  ? fi->def_meta.default_desc  : NULL;
                if (opts->include_ids && fi->def_meta.default_id) {
                    snprintf(eff_id_buf, sizeof(eff_id_buf), "%lu",
                             (unsigned long)fi->def_meta.default_id);
                }
                if (sum) { sum->effective_invalid_default++; }
            } else {
                eff_src  = ST_SRC_MISSING;
                eff_tok  = NULL;
                eff_desc = NULL;
            }

            /* Write effective_token */
            fputc(',', f);
            if (eff_tok) { write_csv_cell(f, eff_tok); }

            /* Write effective_descriptions */
            if (opts->include_descriptions) {
                fputc(',', f);
                if (eff_desc && eff_desc[0]) { write_csv_cell(f, eff_desc); }
            }

            /* Write effective_ids */
            if (opts->include_ids) {
                fputc(',', f);
                if (eff_id_buf[0]) { write_csv_cell(f, eff_id_buf); }
            }

            /* Write effective_status */
            if (opts->include_status) {
                fputc(',', f);
                write_csv_cell(f, eff_src);
            }
        }
    }

    fputc('\n', f);
    if (sum) { sum->rows_written++; }
}

/* ======================================================================
 * Long-mode export
 * ====================================================================== */

/* ------------------------------------------------------------------ */
/* Synthetic default row: emitted when a TOKEN field with a CSV default
 * is absent from the variant.  The explicit columns are left blank;
 * the effective columns carry the default value.                       */
static void write_long_synthetic_default_row(
        FILE                      *f,
        const WhdVariantView      *v,
        const char                *group_name,
        const ReptFieldInfo       *fi,
        const WhdTlvReportOptions *opts,
        WhdTlvReportSummary       *sum)
{
    /* group_id, group_name, display_name */
    if (v->has_group_id) { fprintf(f, "%u", (unsigned)v->group_id); }
    fputc(',', f);
    if (group_name) { write_csv_cell(f, group_name); }
    fputc(',', f);
    write_csv_cell(f, v->filename ? v->filename : "");

    /* field_id, field_name */
    fprintf(f, ",%u,", (unsigned)fi->field_id);
    write_csv_cell(f, fi->field_name);

    /* value_index=0, raw_value, resolved_token, resolved_description, status — all blank */
    fputs(",0,,,,", f);

    /* effective_token */
    fputc(',', f);
    if (fi->def_meta.default_token[0]) { write_csv_cell(f, fi->def_meta.default_token); }

    if (opts->include_descriptions) {
        fputc(',', f);
        if (fi->def_meta.default_desc[0]) { write_csv_cell(f, fi->def_meta.default_desc); }
    }
    if (opts->include_ids) {
        fprintf(f, ",%lu", (unsigned long)fi->def_meta.default_id);
    }
    if (opts->include_status) {
        fputc(',', f);
        write_csv_cell(f, ST_SRC_DEFAULT);
    }

    fputc('\n', f);
    if (sum) {
        sum->rows_written++;
        sum->fields_written++;
        sum->effective_default++;
    }
}
/* ------------------------------------------------------------------ */

static void write_long_header(FILE *f, const WhdTlvReportOptions *opts)
{
    fputs("group_id,group_name,display_name,"
          "field_id,field_name,value_index,"
          "raw_value,resolved_token,resolved_description,status", f);
    if (opts->include_effective) {
        fputs(",effective_token", f);
        if (opts->include_descriptions) { fputs(",effective_description", f); }
        if (opts->include_ids)          { fputs(",effective_id", f); }
        if (opts->include_status)       { fputs(",effective_status", f); }
    }
    fputc('\n', f);
}

static void write_long_row(FILE *f,
                           ReptCtx                   *ctx,
                           const WhdVariantView      *v,
                           const char                *group_name,
                           const ReptFieldInfo        *fi,
                           const WhdTlvFieldValue     *fv,
                           unsigned int               value_index,
                           WhdTlvReportSummary        *sum,
                           const WhdTlvReportOptions *opts)
{
    char        val_buf[REPT_VAL_BUF_SIZE];
    const char *tok = NULL, *desc = NULL;
    const char *status;

    status = resolve_value(ctx, fi, fv, val_buf, &tok, &desc, sum);

    /* group_id */
    if (v->has_group_id) { fprintf(f, "%u", (unsigned)v->group_id); }
    fputc(',', f);
    if (group_name) { write_csv_cell(f, group_name); }
    fputc(',', f);
    write_csv_cell(f, v->filename ? v->filename : "");

    /* field_id, field_name, value_index */
    fprintf(f, ",%u,", (unsigned)fi->field_id);
    write_csv_cell(f, fi->field_name);
    fprintf(f, ",%u", value_index);

    /* raw_value — formatted according to field kind */
    fputc(',', f);
    if (fi->kind == REPT_FIELD_TOKEN && fv->length == 4u) {
        /* Emit the numeric token ID */
        fprintf(f, "%lu", (unsigned long)decode_le32(fv->value));
    } else if (fi->kind == REPT_FIELD_ARCHIVE_INFO && fv->length == 8u) {
        /* Emit size_kib:CRC32 for clarity */
        fprintf(f, "%lu:%08lX",
                (unsigned long)tlv_read_u32_be(fv->value),
                (unsigned long)tlv_read_u32_be(fv->value + 4u));
    } else {
        write_csv_cell(f, val_buf);
    }

    /* resolved_token */
    write_sep_cell(f, tok ? tok : "");

    /* resolved_description — omit if identical to token */
    fputc(',', f);
    if (desc && desc != tok) { write_csv_cell(f, desc); }

    /* status */
    write_sep_cell(f, status);

    /* Effective value columns (always emitted when include_effective;
     * populated only for TOKEN fields — other field types leave them blank). */
    if (opts->include_effective) {
        if (fi->kind == REPT_FIELD_TOKEN) {
            /* This is an explicit row: effective mirrors the resolved value. */
            fputc(',', f);
            if (tok) { write_csv_cell(f, tok); }
            if (opts->include_descriptions) {
                fputc(',', f);
                if (desc && desc != tok) { write_csv_cell(f, desc); }
            }
            if (opts->include_ids) {
                /* val_buf holds the numeric id (4-byte) or hex bitmask (2-byte). */
                fputc(',', f);
                write_csv_cell(f, val_buf);
            }
            if (opts->include_status) {
                fputc(',', f);
                write_csv_cell(f, ST_SRC_EXPLICIT);
            }
            if (sum) { sum->effective_explicit++; }
        } else {
            /* Non-TOKEN field: emit blank placeholders to keep column count. */
            fputc(',', f);
            if (opts->include_descriptions) { fputc(',', f); }
            if (opts->include_ids)          { fputc(',', f); }
            if (opts->include_status)       { fputc(',', f); }
        }
    }

    fputc('\n', f);
    if (sum) {
        sum->rows_written++;
        sum->fields_written++;
        if (is_problem_status(status)) { sum->problem_rows++; }
    }
}

/* ======================================================================
 * Export driver (groups → variants → fields)
 * ====================================================================== */

static int export_csv(FILE                      *f,
                      ReptCtx                   *ctx,
                      const WhdTlvReportOptions *opts,
                      WhdTlvReportSummary       *sum)
{
    unsigned long gi;

    if (opts->mode == WHDTLV_REPORT_CSV_WIDE) {
        write_wide_header(f, ctx, opts);
    } else {
        write_long_header(f, opts);
    }

    for (gi = 0u; gi < ctx->gs.group_count; ++gi) {
        const WhdVariantGroup *grp = &ctx->gs.groups[gi];
        unsigned long          vi;
        const char            *group_name;

        if (sum) { sum->groups_total++; }
        if (grp->variant_count > 1u && sum) { sum->multi_variant_groups_seen++; }

        /* Filter single-variant groups when requested */
        if (opts->only_multi_variant_groups && grp->variant_count <= 1u) { continue; }

        /* Resolve group name: prefer group_map; fall back to base_name */
        group_name = NULL;
        if (ctx->rt.has_group_map && grp->group_id != 0u) {
            group_name = tlv_runtime_group_name(&ctx->rt, grp->group_id);
        }
        if (!group_name) { group_name = grp->group_name; }

        for (vi = 0u; vi < grp->variant_count; ++vi) {
            unsigned long         sidx = ctx->gs.sorted_indices[grp->first_variant + vi];
            const WhdVariantView *v    = &ctx->arr.items[sidx];

            if (sum) { sum->variants_total++; }

            if (opts->mode == WHDTLV_REPORT_CSV_WIDE) {
                if (opts->only_problem_rows && !variant_has_problem(ctx, v, opts)) {
                    continue;
                }
                write_wide_row(f, ctx, v, group_name, opts, sum);

            } else {
                /* Long mode: one row per field value in this variant.
                 * Track value_index independently for each field_id. */
                uint8_t       seen_fids[WHD_VARIANT_MAX_FIELDS];
                unsigned int  seen_cnt[WHD_VARIANT_MAX_FIELDS];
                unsigned char nfids = 0u;
                unsigned short i;

                memset(seen_fids, 0, sizeof(seen_fids));
                memset(seen_cnt,  0, sizeof(seen_cnt));

                for (i = 0u; i < v->field_count; ++i) {
                    const WhdTlvFieldValue *fv = &v->fields[i];
                    const ReptFieldInfo    *fi;
                    unsigned int            vidx;
                    unsigned char           k;
                    int                     found;

                    fi = find_field_info(ctx, fv->field_id);
                    if (!fi) { continue; }
                    if (fi->kind == REPT_FIELD_DISPLAY
                     || fi->kind == REPT_FIELD_GROUP_ID) { continue; }

                    /* Determine value_index for this field_id within the variant */
                    found = 0;
                    vidx  = 0u;
                    for (k = 0u; k < nfids; ++k) {
                        if (seen_fids[k] == fv->field_id) {
                            vidx = seen_cnt[k]++;
                            found = 1;
                            if (vidx > 0u && sum) { sum->multi_value_fields_seen++; }
                            break;
                        }
                    }
                    if (!found && nfids < WHD_VARIANT_MAX_FIELDS) {
                        seen_fids[nfids] = fv->field_id;
                        seen_cnt[nfids]  = 1u;
                        vidx             = 0u;
                        ++nfids;
                    }

                    /* Apply problem-row filter when requested */
                    if (opts->only_problem_rows) {
                        char        pb[REPT_VAL_BUF_SIZE];
                        const char *pt = NULL, *pd = NULL;
                        const char *ps = resolve_value(ctx, fi, fv, pb, &pt, &pd, NULL);
                        if (!is_problem_status(ps)) { continue; }
                    }

                    write_long_row(f, ctx, v, group_name, fi, fv, vidx, sum, opts);
                }

                /* Second pass: for each TOKEN field with a CSV default that was
                 * absent from this variant, emit a synthetic default row so the
                 * consumer can see the effective value that would be used. */
                if (opts->include_effective) {
                    uint8_t fi_idx;
                    for (fi_idx = 0u; fi_idx < ctx->field_count; ++fi_idx) {
                        const ReptFieldInfo *fi2 = &ctx->fields[fi_idx];
                        unsigned char        k;
                        int                  fnd = 0;

                        if (fi2->kind == REPT_FIELD_DISPLAY
                         || fi2->kind == REPT_FIELD_GROUP_ID
                         || fi2->kind == REPT_FIELD_ARCHIVE_INFO) { continue; }
                        if (fi2->kind != REPT_FIELD_TOKEN) { continue; }
                        if (!fi2->def_meta.has_default) { continue; }

                        for (k = 0u; k < nfids; ++k) {
                            if (seen_fids[k] == fi2->field_id) { fnd = 1; break; }
                        }
                        /* Synthetic default rows are never 'problems'; skip when
                         * only_problem_rows is active. */
                        if (!fnd && !opts->only_problem_rows) {
                            write_long_synthetic_default_row(
                                f, v, group_name, fi2, opts, sum);
                        }
                    }
                }
            }
        }
    }

    return WHDTLV_REPORT_OK;
}

/* ======================================================================
 * Public API implementation
 * ====================================================================== */

void whdtlv_report_options_defaults(WhdTlvReportOptions *opts)
{
    if (!opts) { return; }
    memset(opts, 0, sizeof(*opts));
    opts->mode = WHDTLV_REPORT_CSV_WIDE;
}

int whdtlv_report_csv_file(const char                *tlv_path,
                           const char                *defs_dir,
                           const char                *output_csv_path,
                           const WhdTlvReportOptions *options,
                           WhdTlvReportSummary       *summary)
{
    WhdTlvReportOptions        default_opts;
    const WhdTlvReportOptions *opts;
    ReptCtx                    ctx;
    WhdTlvReportSummary        sum;
    FILE                      *f;
    int                        rc;

    if (!tlv_path || !defs_dir || !output_csv_path) {
        return WHDTLV_REPORT_ERR_BAD_ARG;
    }

    opts = options;
    if (!opts) {
        whdtlv_report_options_defaults(&default_opts);
        opts = &default_opts;
    }

    memset(&ctx, 0, sizeof(ctx));
    memset(&sum, 0, sizeof(sum));

    /* --- Load TLV metadata and data block --- */
    tlv_runtime_init(&ctx.rt);
    rc = tlv_runtime_load(&ctx.rt, tlv_path);
    if (rc != WHD_FILTER_OK) {
        return (rc == WHD_FILTER_ERR_TLV_OPEN)
             ? WHDTLV_REPORT_ERR_TLV_OPEN
             : WHDTLV_REPORT_ERR_TLV_PARSE;
    }

    /* Resolve the field IDs for the three special fields */
    ctx.display_fid      = tlv_runtime_field_id(&ctx.rt, "display_name");
    ctx.group_id_fid     = ctx.rt.group_id_field_id; /* pre-resolved by tlv_runtime_load */
    ctx.archive_info_fid = tlv_runtime_field_id(&ctx.rt, "archive_info");

    if (ctx.display_fid == 0u) {
        /* A TLV without a display_name field is unreadable */
        tlv_runtime_free(&ctx.rt);
        return WHDTLV_REPORT_ERR_TLV_PARSE;
    }

    /* --- Build variant array (points into rt.reader.buffer) --- */
    rc = tlv_variant_build(&ctx.arr,
                            ctx.rt.reader.buffer + ctx.rt.data_offset,
                            ctx.rt.reader.size   - ctx.rt.data_offset,
                            ctx.display_fid,
                            ctx.group_id_fid);
    if (rc != WHD_FILTER_OK) {
        rept_ctx_free(&ctx);
        return WHDTLV_REPORT_ERR_OOM;
    }
    ctx.arr_built = 1;

    /* --- Initialise CSV manager (graceful: missing CSVs are non-fatal) --- */
    if (!csv_cache_manager_init(&ctx.mgr, NULL, defs_dir)) {
        rept_ctx_free(&ctx);
        return WHDTLV_REPORT_ERR_OOM;
    }
    ctx.mgr_init = 1;

    /* --- Build field info table and pre-load asset CSVs --- */
    build_field_table(&ctx);

    /* --- Group the variants --- */
    rc = tlv_group_build(&ctx.gs, &ctx.arr, (ctx.group_id_fid != 0u) ? 1 : 0);
    if (rc != WHD_FILTER_OK) {
        rept_ctx_free(&ctx);
        return WHDTLV_REPORT_ERR_OOM;
    }
    ctx.gs_built = 1;

    /* --- Open output file --- */
    f = fopen(output_csv_path, "w");
    if (!f) {
        rept_ctx_free(&ctx);
        return WHDTLV_REPORT_ERR_CSV_OPEN;
    }

    /* --- Export --- */
    rc = export_csv(f, &ctx, opts, &sum);

    fclose(f);
    rept_ctx_free(&ctx);

    if (summary) { *summary = sum; }
    return rc;
}

/* End of Text */
