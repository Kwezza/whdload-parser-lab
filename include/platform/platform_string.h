#ifndef PLATFORM_STRING_H
#define PLATFORM_STRING_H

#include <platform.h>

/*------------------------------------------------------------------------*/
/* Platform String Abstraction Layer */
/*------------------------------------------------------------------------*/

#if PLATFORM_AMIGA
    /* Amiga-specific string functions */
    int whd_strcasecmp(const char *s1, const char *s2);
    char *whd_strtok_r(char *str, const char *delim, char **saveptr);
#else
    /* Host platform - use standard functions */
    #ifdef _WIN32
        /* Windows */
        #include <string.h>
        #define whd_strcasecmp(s1, s2) _stricmp(s1, s2)
        #define whd_strtok_r(str, delim, saveptr) strtok_s(str, delim, saveptr)
    #else
        /* Unix/Linux */
        #include <strings.h>  /* For strcasecmp */
        #include <string.h>   /* For strtok_r */
        #define whd_strcasecmp(s1, s2) strcasecmp(s1, s2)
        #define whd_strtok_r(str, delim, saveptr) strtok_r(str, delim, saveptr)
    #endif
#endif

#endif /* PLATFORM_STRING_H */

/* End of Text */
