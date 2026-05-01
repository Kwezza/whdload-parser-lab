/*------------------------------------------------------------------------*/
/*                                                                        *
 * platform_mem.h — Cross-platform memory allocation wrappers           *
 * WHDLoad Manager - Support both Amiga and host platforms               *
 *                                                                        *
 * Copyright © 2025 Kerry Thompson                                        *
 * SPDX-License-Identifier: MIT                                           *
 *                                                                        */
/*------------------------------------------------------------------------*/

#ifndef PLATFORM_MEM_H
#define PLATFORM_MEM_H

#include <stddef.h>  /* for size_t */

#ifdef PLATFORM_AMIGA
  /* For Amiga builds, we'll use simple malloc/free from stdlib.h */
  /* This avoids the complex header dependencies with vbcc */
  #include <stdlib.h>
  #define whd_malloc  malloc
  #define whd_free    free
#else
  #include <stdlib.h>
  #define whd_malloc  malloc
  #define whd_free    free
#endif

#endif /* PLATFORM_MEM_H */
