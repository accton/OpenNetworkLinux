/************************************************************
 * <bsn.cl fy=2026 v=onl>
 *
 *           Copyright 2026 Accton Technology Corporation.
 *
 * Licensed under the Eclipse Public License, Version 1.0.
 *
 * </bsn.cl>
 ************************************************************
 *
 * Per-key syslog rate limiting shared by Accton ONLP driver
 * modules (sfpi.c, fani.c, sysi.c, ...).
 *
 * Generic primitive:
 *   struct log_ctrl { int should_log; };
 *   void syslog_ctrl(struct log_ctrl *arr, int reason,
 *                    const char *fmt, ...);
 *   void reset_log_ctrl(struct log_ctrl *arr, int count);
 *
 * Each domain (SFP, fan, ...) defines its own reason enum plus a
 * bookkeeping struct bundling a last-known state value with a
 * per-reason log_ctrl[] array.  The SFP domain lives in this header
 * to avoid an extra include; new domains (fan, PSU, ...) should be
 * appended below in the same style.
 *
 * Typical driver-side declaration (SFP example):
 *
 *   static struct sfp_log_mgmt log_mgmt[MAX_PORT + 1] = {
 *       [0 ... MAX_PORT] = {
 *           .present_rec = ONLP_STATUS_E_INTERNAL,
 *           .log_ctrl    = {
 *               [0 ... SFP_LOG_REASON_COUNT - 1] = { .should_log = 1 }
 *           }
 *       }
 *   };
 *
 *   syslog_ctrl(log_mgmt[port].log_ctrl, SFP_PRESENT_UNABLE_TO_GET_STATUS,
 *               "fmt %d", arg);
 *   reset_log_ctrl(log_mgmt[port].log_ctrl, SFP_LOG_REASON_COUNT);
 *
 ***********************************************************/
#ifndef __ACCTON_COMMON_LOG_CTRL_H__
#define __ACCTON_COMMON_LOG_CTRL_H__

/* ---------------------------------------------------------------
 * Generic throttling primitive
 * --------------------------------------------------------------- */

struct log_ctrl {
    int should_log;
};

/*
 * Emit one syslog(LOG_ERR, ...) line for arr[reason] if its
 * should_log flag is set, then clear the flag.  Subsequent calls
 * with the same (arr, reason) key are silent until reset_log_ctrl()
 * re-arms them.
 *
 * The caller owns the array and must guarantee 0 <= reason < count,
 * where count is the size passed to reset_log_ctrl() below.
 */
void syslog_ctrl(struct log_ctrl *arr, int reason, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/*
 * Re-arm every entry in arr[0..count-1] so the next syslog_ctrl()
 * for each reason is emitted again.
 */
void reset_log_ctrl(struct log_ctrl *arr, int count);

/* ---------------------------------------------------------------
 * SFP domain
 * --------------------------------------------------------------- */

enum sfp_log_reason {
    SFP_PRESENT_UNABLE_TO_GET_STATUS,
    SFP_EEPROM_UNABLE_TO_GET_DATA,
    SFP_EEPROM_UNABLE_TO_GET_DATA_SIZE_DIFF,
    SFP_DOM_UNABLE_TO_OPEN_EEPROM_FILE,
    SFP_DOM_UNABLE_TO_SET_FILE_POS_INDICATOR,
    SFP_DOM_UNABLE_TO_GET_EEPROM_DATA,
    SFP_TX_DIS_UNABLE_TO_SET_STATUS,
    SFP_TX_DIS_UNABLE_TO_GET_IDENTIFIER,
    SFP_TX_DIS_UNABLE_TO_GET_MEM_MODEL,
    SFP_TX_DIS_UNABLE_TO_SET_EEPROM_PAGE,
    SFP_TX_DIS_UNABLE_TO_GET_CONTROL,
    SFP_TX_DIS_UNABLE_TO_SET_BANK,
    SFP_TX_DIS_UNABLE_TO_GET_STATUS,
    SFP_LP_MODE_UNABLE_TO_GET_IDENTIFIER,
    SFP_LP_MODE_UNABLE_TO_SET_STATUS,
    SFP_LP_MODE_UNABLE_TO_GET_STATUS,
    SFP_RESET_UNABLE_TO_SET_STATUS,
    SFP_RESET_UNABLE_TO_GET_STATUS,
    SFP_RX_LOS_UNABLE_TO_GET_STATUS,
    SFP_TX_FAULT_UNABLE_TO_GET_STATUS,
    SFP_MULTIRATE_UNABLE_TO_SET_STATUS,

    SFP_LOG_REASON_COUNT,
};

struct sfp_log_mgmt {
    int             present_rec;
    struct log_ctrl log_ctrl[SFP_LOG_REASON_COUNT];
};

#endif /* __ACCTON_COMMON_LOG_CTRL_H__ */
