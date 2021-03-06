/* SPDX-License-Identifier: LGPL-2.1-or-later */

#ifndef __NM_LOGGING_BASE_H__
#define __NM_LOGGING_BASE_H__

#include "nm-logging-fwd.h"

typedef struct {
    const char *name;
    const char *level_str;

    /* nm-logging uses syslog internally. Note that the three most-verbose syslog levels
     * are LOG_DEBUG, LOG_INFO and LOG_NOTICE. Journal already highlights LOG_NOTICE
     * as special.
     *
     * On the other hand, we have three levels LOGL_TRACE, LOGL_DEBUG and LOGL_INFO,
     * which are regular messages not to be highlighted. For that reason, we must map
     * LOGL_TRACE and LOGL_DEBUG both to syslog level LOG_DEBUG. */
    int syslog_level;

    GLogLevelFlags g_log_level;
} LogLevelDesc;

extern const LogLevelDesc nm_log_level_desc[_LOGL_N];

gboolean _nm_log_parse_level(const char *level, NMLogLevel *out_level);

/*****************************************************************************/

extern volatile NMLogLevel _nm_logging_enabled_value;

static inline gboolean
_nm_logging_enabled(NMLogLevel level)
{
    return level >= _nm_logging_enabled_value;
}

void _nm_logging_enabled_init(const char *level_str);

/*****************************************************************************/

void _nm_log_simple_printf(NMLogLevel level, const char *fmt, ...) _nm_printf(2, 3);

#endif /* __NM_LOGGING_BASE_H__ */
