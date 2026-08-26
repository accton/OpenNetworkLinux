/************************************************************
 * <bsn.cl fy=2026 v=onl>
 *
 *           Copyright 2026 Accton Technology Corporation.
 *
 * Licensed under the Eclipse Public License, Version 1.0.
 *
 * </bsn.cl>
 ***********************************************************/
#include <stdio.h>
#include <stdarg.h>
#include <syslog.h>

#include "log_ctrl.h"

void
syslog_ctrl(struct log_ctrl *arr, int reason, const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (arr[reason].should_log) {
        syslog(LOG_ERR, "%s", buf);
        arr[reason].should_log = 0;
    }
}

void
reset_log_ctrl(struct log_ctrl *arr, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        arr[i].should_log = 1;
    }
}
