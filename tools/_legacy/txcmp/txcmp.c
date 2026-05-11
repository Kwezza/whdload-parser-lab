/* tools/txcmp/txcmp.c - Text comparison utility for Amiga/host test suites
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * Two modes:
 *
 *   txcmp <file1> <file2>
 *       Compare two text files line-by-line.  Trailing \r is stripped from
 *       both sides before comparison so that CRLF expected files (generated
 *       on Windows) compare cleanly against LF output from the Amiga binary.
 *       Exits 0 if all lines match.  Exits 5 on first mismatch, printing
 *       the line number and both differing lines to stderr.
 *
 *   txcmp --contains <file> <string>
 *       Scan <file> for any line containing <string> as a substring.
 *       Exits 0 if found.  Exits 5 if not found (silent on not-found).
 *
 * Return codes:
 *   0  - match / found
 *   5  - mismatch / not found
 *   1  - usage error or file open failure
 *
 * C89-compatible.  No dynamic allocation.  Safe for vbcc / Amiga 68000.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE    512
#define RC_MATCH      0
#define RC_MISMATCH   5
#define RC_ERROR      1

/*------------------------------------------------------------------------*/
/* strip_crlf                                                             */
/*
 * Remove trailing \r and \n in-place.  Handles CRLF (Windows) and LF
 * (Unix/Amiga) line endings so that host-generated expected files can be
 * compared against Amiga output without conversion.
 */
static void strip_crlf(char *s)
{
    int len;
    len = (int)strlen(s);
    while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = '\0';
    }
}

/*------------------------------------------------------------------------*/
/* compare_files                                                          */
/*
 * Line-by-line comparison of two text files.
 * Returns RC_MATCH (0) if they are identical after stripping line endings.
 * Returns RC_MISMATCH (5) on first differing line (diagnostic to stderr).
 * Returns RC_ERROR (1) if either file cannot be opened.
 */
static int compare_files(const char *path1, const char *path2)
{
    FILE *f1;
    FILE *f2;
    char  line1[MAX_LINE];
    char  line2[MAX_LINE];
    long  lineno;
    int   got1;
    int   got2;

    f1 = fopen(path1, "r");
    if (f1 == NULL) {
        fprintf(stderr, "txcmp: cannot open: %s\n", path1);
        return RC_ERROR;
    }

    f2 = fopen(path2, "r");
    if (f2 == NULL) {
        fprintf(stderr, "txcmp: cannot open: %s\n", path2);
        fclose(f1);
        return RC_ERROR;
    }

    lineno = 0;

    for (;;) {
        got1 = (fgets(line1, (int)sizeof(line1), f1) != NULL);
        got2 = (fgets(line2, (int)sizeof(line2), f2) != NULL);
        lineno++;

        if (!got1 && !got2) {
            break;
        }

        if (!got1 || !got2) {
            fprintf(stderr, "txcmp: files differ in length at line %ld\n",
                    lineno);
            fclose(f1);
            fclose(f2);
            return RC_MISMATCH;
        }

        strip_crlf(line1);
        strip_crlf(line2);

        if (strcmp(line1, line2) != 0) {
            fprintf(stderr, "txcmp: mismatch at line %ld\n", lineno);
            fprintf(stderr, "  file1: %s\n", line1);
            fprintf(stderr, "  file2: %s\n", line2);
            fclose(f1);
            fclose(f2);
            return RC_MISMATCH;
        }
    }

    fclose(f1);
    fclose(f2);
    return RC_MATCH;
}

/*------------------------------------------------------------------------*/
/* contains_string                                                        */
/*
 * Scan <path> line by line for any line containing <needle> as a substring.
 * Returns RC_MATCH (0) if found, RC_MISMATCH (5) if not found (silent).
 * Returns RC_ERROR (1) if the file cannot be opened.
 */
static int contains_string(const char *path, const char *needle)
{
    FILE *f;
    char  line[MAX_LINE];

    f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "txcmp: cannot open: %s\n", path);
        return RC_ERROR;
    }

    while (fgets(line, (int)sizeof(line), f) != NULL) {
        if (strstr(line, needle) != NULL) {
            fclose(f);
            return RC_MATCH;
        }
    }

    fclose(f);
    return RC_MISMATCH;
}

/*------------------------------------------------------------------------*/
/* main                                                                   */

int main(int argc, char **argv)
{
    if (argc == 4 && strcmp(argv[1], "--contains") == 0) {
        return contains_string(argv[2], argv[3]);
    }

    if (argc == 3 && argv[1][0] != '-') {
        return compare_files(argv[1], argv[2]);
    }

    fprintf(stderr, "txcmp - text comparison utility\n\n");
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  txcmp <file1> <file2>\n");
    fprintf(stderr, "  txcmp --contains <file> <string>\n\n");
    fprintf(stderr, "Return codes: 0=match/found  5=mismatch/not-found"
            "  1=error\n");
    return RC_ERROR;
}
