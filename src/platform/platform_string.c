/**
 * @file platform_string.c
 * @brief Platform-specific string function implementations
 *
 * Provides Amiga-compatible implementations of POSIX string functions
 * that are not available in the standard Amiga C library.
 */

#include <platform.h>
#include <platform/platform_string.h>

#if PLATFORM_AMIGA

#include <ctype.h>
#include <string.h>

/*------------------------------------------------------------------------*/

/**
 * @brief Case-insensitive string comparison for Amiga
 *
 * Amiga-compatible implementation of strcasecmp() which is not available
 * in the standard Amiga C library. Uses tolower() for character comparison.
 *
 * @param s1 First string to compare
 * @param s2 Second string to compare
 * @return 0 if strings match (case-insensitive), negative if s1 < s2, positive if s1 > s2
 */
int whd_strcasecmp(const char *s1, const char *s2) {
    /* Validate parameters */
    if (!s1 || !s2) {
        if (s1 == s2) return 0;
        return s1 ? 1 : -1;
    }

    /* Compare characters case-insensitively */
    while (*s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2) {
            return c1 - c2;
        }
        s1++;
        s2++;
    } /* while */

    /* Handle end of strings */
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
} /* whd_strcasecmp */

/*------------------------------------------------------------------------*/

/**
 * @brief Thread-safe string tokenization for Amiga
 *
 * Amiga-compatible implementation of strtok_r() which is not available
 * in the standard Amiga C library. Provides reentrant string tokenization.
 *
 * @param str String to tokenize (NULL for subsequent calls)
 * @param delim Delimiter characters
 * @param saveptr Pointer to maintain state between calls
 * @return Pointer to next token, or NULL if no more tokens
 */
char *whd_strtok_r(char *str, const char *delim, char **saveptr) {
    char *start, *end;

    /* Validate parameters */
    if (!delim || !saveptr) {
        return NULL;
    }

    /* Initialize or continue from saved position */
    if (str != NULL) {
        *saveptr = str;
    }

    start = *saveptr;
    if (start == NULL) {
        return NULL;
    }

    /* Skip leading delimiters */
    while (*start && strchr(delim, *start)) {
        start++;
    } /* while */

    /* Check for end of string */
    if (*start == '\0') {
        *saveptr = NULL;
        return NULL;
    }

    /* Find end of current token */
    end = start;
    while (*end && !strchr(delim, *end)) {
        end++;
    } /* while */

    /* Null-terminate token and update saveptr */
    if (*end) {
        *end = '\0';
        *saveptr = end + 1;
    } else {
        *saveptr = NULL;
    }

    return start;
} /* whd_strtok_r */

/*------------------------------------------------------------------------*/

#endif /* PLATFORM_AMIGA */

/* End of Text */
