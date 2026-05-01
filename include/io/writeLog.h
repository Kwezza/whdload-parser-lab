/*------------------------------------------------------------------------*/
/*                                                                        *
 *  $Id: writeLog.h,v 1.2 2025/07/09 12:00:00 user Exp $
 *                                                                        */
/*------------------------------------------------------------------------*/

#ifndef WRITELOG_H
#define WRITELOG_H

#include <stdarg.h>
#include <platform.h>

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------*/
/**
 * @brief Enable or disable logfile output at runtime
 *
 * @param enabled true to allow logfile writes, false to suppress them
 */
/*------------------------------------------------------------------------*/
void set_logging_enabled(bool enabled);

/*------------------------------------------------------------------------*/
/**
 * @brief Check whether logfile output is currently enabled
 *
 * @return true if logfile writes are enabled, false otherwise
 */
/*------------------------------------------------------------------------*/
bool is_logging_enabled(void);

/*------------------------------------------------------------------------*/
/**
 * @brief Initializes the log file by creating a new empty file
 */
/*------------------------------------------------------------------------*/
void initialize_logfile(void);

/*------------------------------------------------------------------------*/
/**
 * @brief Appends a message to the log file with timestamp
 *
 * @param format Format string
 * @param ...    Variable arguments
 */
/*------------------------------------------------------------------------*/
void append_to_log(const char *format, ...);

/* Convenience macro used throughout codebase for uniform logging */
#ifndef LOG_PRINTF
#define LOG_PRINTF(...) append_to_log(__VA_ARGS__)
#endif

#ifdef __cplusplus
}
#endif

#endif /* WRITELOG_H */

/* End of Text */
