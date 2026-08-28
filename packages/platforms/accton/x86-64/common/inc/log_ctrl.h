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

/*
 * Reason categories are split by *hardware target* (CPLD/FPGA vs. optical
 * module EEPROM) crossed with *access mechanism* (sysfs file vs. direct I2C
 * ioctl):
 *
 *   *_SYSFS_*             : Failure reading/writing a CPLD/FPGA attribute
 *                           file exposed by the platform driver
 *                           (module_present, module_lp_mode, module_tx_disable,
 *                           module_reset, module_rx_los, module_tx_fault, ...).
 *   *_EEPROM_SYSFS_*      : Failure reading/writing an EEPROM data file
 *                           exposed by a driver (module_eeprom_%d, or the
 *                           at24-style /sys/bus/i2c/devices/<bus>-0050/eeprom).
 *                           Content is optical module EEPROM data, but the
 *                           access path is sysfs (fopen()+fread()/fwrite() or
 *                           onlp_file_*).
 *   *_I2C_*_TARGET_*      : Failure performing a direct-I2C access
 *                           (onlp_sfpi_dev_readb/writeb -> onlp_i2c_readb/
 *                           writeb) to the final byte(s) that carry the
 *                           requested data (e.g. the tx_disable byte in a
 *                           QSFP module).
 *   *_I2C_PREP_*          : Failure performing a direct-I2C access to an
 *                           auxiliary byte needed to reach a target byte
 *                           (identifier read, status byte read, page/bank
 *                           select writes, control-byte read).
 *   *_INVALID_*           : Value-level anomalies where the I/O succeeded but
 *                           the returned data violates an invariant.
 */
enum sfp_log_reason {
    SFP_SYSFS_READ_FAIL,
    SFP_SYSFS_WRITE_FAIL,
    SFP_EEPROM_SYSFS_READ_FAIL,
    SFP_EEPROM_SYSFS_WRITE_FAIL,
    SFP_I2C_READ_TARGET_FAIL,
    SFP_I2C_WRITE_TARGET_FAIL,
    SFP_I2C_PREP_FAIL,
    SFP_INVALID_DATA_SIZE,

    SFP_LOG_REASON_COUNT,
};

struct sfp_log_mgmt {
    int             present_rec;
    struct log_ctrl log_ctrl[SFP_LOG_REASON_COUNT];
};

#endif /* __ACCTON_COMMON_LOG_CTRL_H__ */
