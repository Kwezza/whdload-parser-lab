/* whdtlv/utils/logging.h - Internal diagnostic logging shim
 *
 * Copyright (C) 2026 Kerry Thompson
 * SPDX-License-Identifier: MIT
 *
 * LOG_PRINTF is an internal diagnostic macro used by the filtering pipeline.
 * In v1, it is silenced so the reusable subsystem does not write to
 * stdout or stderr.  Future milestones may route it through writeLog.c.
 *
 * C89-compatible; vbcc-safe.
 */

#ifndef WHDTLV_UTILS_LOGGING_H
#define WHDTLV_UTILS_LOGGING_H

/*
 * Suppress all internal diagnostic output.
 * The comma operator with (void)0 keeps the compiler happy on all
 * conforming C89 implementations even when the argument list is empty.
 */
#ifdef _MSC_VER
#  define LOG_PRINTF(...) ((void)0)
#else
#  define LOG_PRINTF(fmt, ...) ((void)0)
#endif

#endif /* WHDTLV_UTILS_LOGGING_H */
/* End of Text */
