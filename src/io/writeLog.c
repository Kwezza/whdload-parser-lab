/*------------------------------------------------------------------------*/
/*                                                                        *
 *  $Id: writeLog.c,v 1.2 2025/07/09 12:00:00 user Exp $
 *                                                                        */
/*------------------------------------------------------------------------*/

#include <platform.h>
#include <stdio.h>     /* For FILE operations */
#include <string.h>    /* For string operations */
#include <stdarg.h>    /* For va_list */
#include <time.h>      /* For time functions */
#include <stdint.h>    /* For fixed-width integers */
#include <io/writeLog.h>

#define MAX_LOG_LINE 256  /* Maximum log line length */
#define TIMESTAMP_SIZE 11 /* Buffer size for [HH:MM:SS] plus null terminator */

#define LOG_FILENAME "PROGDIR:logfile.txt"

#ifdef PLATFORM_HOST
/* Override log path for testing in host environment */
#undef LOG_FILENAME
#define LOG_FILENAME "logfile.txt"
#endif

static bool logging_enabled = true;

/*------------------------------------------------------------------------*/
/**
 * @brief Gets a timestamp string in [HH:MM:SS] format
 *
 * Uses portable C library time functions to format the current time
 * in a consistent way across all platforms.
 *
 * @param buf    Buffer to store the timestamp
 * @param buf_sz Size of the buffer
 */
/*------------------------------------------------------------------------*/
static void get_timestamp(char *buf, size_t buf_sz)
{
	time_t now;      /* Current time */
	struct tm tm_now; /* Time structure */

	/* Validate parameters */
	if (buf == NULL || buf_sz < TIMESTAMP_SIZE) {
		return;
	} /* if */

	/* Get current time */
	now = time(NULL);

#ifdef PLATFORM_AMIGA
	/* Amiga doesn't have localtime_r, use regular localtime */
	{
		struct tm *tm_ptr = localtime(&now);
		if (tm_ptr != NULL) {
			tm_now = *tm_ptr;
		} else {
			/* Fallback if localtime fails */
			memset(&tm_now, 0, sizeof(tm_now));
		} /* if */
	}
#elif defined(_WIN32)
	/* Windows uses localtime_s instead of localtime_r */
	{
		struct tm *tm_ptr = localtime(&now);
		if (tm_ptr != NULL) {
			tm_now = *tm_ptr;
		} else {
			/* Fallback if localtime fails */
			memset(&tm_now, 0, sizeof(tm_now));
		} /* if */
	}
#else
	/* Use thread-safe localtime_r on other platforms */
	localtime_r(&now, &tm_now);
#endif

	/* Format the timestamp */
	strftime(buf, buf_sz, "[%H:%M:%S]", &tm_now);
} /* get_timestamp */

/*------------------------------------------------------------------------*/
/**
 * @brief Enable or disable logfile output at runtime
 *
 * @param enabled true to allow logfile writes, false to suppress them
 */
/*------------------------------------------------------------------------*/
void set_logging_enabled(bool enabled)
{
	logging_enabled = enabled;
} /* set_logging_enabled */

/*------------------------------------------------------------------------*/
/**
 * @brief Check whether logfile output is currently enabled
 *
 * @return true if logfile writes are enabled, false otherwise
 */
/*------------------------------------------------------------------------*/
bool is_logging_enabled(void)
{
	return logging_enabled;
} /* is_logging_enabled */

/*------------------------------------------------------------------------*/
/**
 * @brief Appends a message to the log file with timestamp
 *
 * @param format Format string
 * @param ...    Variable arguments
 */
/*------------------------------------------------------------------------*/
void append_to_log(const char *format, ...)
{
	FILE *fp;                       /* File handle */
	char buffer[MAX_LOG_LINE];      /* Buffer for complete log message */
	char timestamp[TIMESTAMP_SIZE]; /* Buffer for timestamp */
	va_list args;                   /* Variable argument list */
	size_t timestamp_len;           /* Length of timestamp */
	size_t msg_len;                 /* Length of message part */
	size_t remaining;               /* Remaining buffer space */
	int result;                     /* Result of vsnprintf */

	/* Check for NULL format string */
	if (format == NULL) {
		return;
	} /* if */

	if (!logging_enabled) {
		return;
	} /* if */

	/* Get timestamp and its length */
	get_timestamp(timestamp, TIMESTAMP_SIZE);
	timestamp_len = strlen(timestamp);

	/* Copy timestamp to buffer */
	if (timestamp_len < MAX_LOG_LINE) {
		memcpy(buffer, timestamp, timestamp_len);
		buffer[timestamp_len] = ' ';  /* Add space after timestamp */
		timestamp_len++; /* Account for the added space */
	} else {
		/* Shouldn't happen with proper TIMESTAMP_SIZE, but just in case */
		return;
	} /* if */

	/* Calculate remaining space for the message */
	remaining = MAX_LOG_LINE - timestamp_len - 1; /* -1 for null terminator */

	/* Format the message with bounds checking */
	va_start(args, format);
	result = vsnprintf(buffer + timestamp_len, remaining, format, args);
	va_end(args);

	/* Check formatting result */
	if (result < 0) {
		/* Formatting error, just use timestamp */
		buffer[timestamp_len] = '\0';
	} else {
		/* Get actual message length (may be truncated) */
		msg_len = (result < (int)remaining) ? (size_t)result : remaining - 1;

		/* Ensure there's a newline at the end */
		if (msg_len > 0 && buffer[timestamp_len + msg_len - 1] != '\n') {
			/* Need to add newline if there's room */
			if (timestamp_len + msg_len < MAX_LOG_LINE - 1) {
				buffer[timestamp_len + msg_len] = '\n';
				buffer[timestamp_len + msg_len + 1] = '\0';
			} else {
				/* Replace last character with newline */
				buffer[MAX_LOG_LINE - 2] = '\n';
				buffer[MAX_LOG_LINE - 1] = '\0';
			} /* if */
		} /* if */
	} /* if */

	/* Open log file in append mode */
	fp = whd_fopen(LOG_FILENAME, "a");
	if (!fp) {
		/* Try to create the file if it doesn't exist */
		fp = whd_fopen(LOG_FILENAME, "w");
	} /* if */

	/* Write to file if successfully opened */
	if (fp) {
		whd_fwrite(buffer, 1, strlen(buffer), fp);
		whd_fclose(fp);
	} /* if */
} /* append_to_log */

/*------------------------------------------------------------------------*/
/**
 * @brief Initializes the log file by creating a new empty file
 */
/*------------------------------------------------------------------------*/
void initialize_logfile(void)
{
	FILE *fp;  /* File handle */
	char timestamp[TIMESTAMP_SIZE]; /* Buffer for timestamp */
	char buffer[MAX_LOG_LINE];      /* Buffer for log message */

	if (!logging_enabled) {
		return;
	} /* if */

	/* Create new log file (truncates if exists) */
	fp = whd_fopen(LOG_FILENAME, "w");

	if (fp) {
		/* Get current timestamp */
		get_timestamp(timestamp, TIMESTAMP_SIZE);

		/* Create initialization message */
		snprintf(buffer, MAX_LOG_LINE, "%s Log initialized\n", timestamp);

		/* Write initialization message */
		whd_fwrite(buffer, 1, strlen(buffer), fp);
		whd_fclose(fp);
	} /* if */
} /* initialize_logfile */

/* End of Text */
