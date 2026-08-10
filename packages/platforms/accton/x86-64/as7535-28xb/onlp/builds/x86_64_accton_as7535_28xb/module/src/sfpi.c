/************************************************************
 * <bsn.cl fy=2014 v=onl>
 *
 *           Copyright 2014 Big Switch Networks, Inc.
 *           Copyright 2013 Accton Technology Corporation.
 *
 * Licensed under the Eclipse Public License, Version 1.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *        http://www.eclipse.org/legal/epl-v10.html
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the
 * License.
 *
 * </bsn.cl>
 ************************************************************
 *
 *
 *
 ***********************************************************/
#include <onlp/platformi/sfpi.h>
#include <onlplib/i2c.h>
#include <onlplib/file.h>
#include "x86_64_accton_as7535_28xb_int.h"
#include "x86_64_accton_as7535_28xb_log.h"
#include <syslog.h>

#define VALIDATE(_port) \
    do { \
        if (_port < 0 || _port > 27) { \
            return ONLP_STATUS_E_INVALID; \
        } \
    } while(0)

#define VALIDATE_SFP(_port) \
    do { \
        if (_port < 4 || _port > 27) \
            return ONLP_STATUS_E_UNSUPPORTED; \
    } while(0)

#define VALIDATE_QSFP(_port) \
    do { \
        if (_port < 0 || _port > 3) \
            return ONLP_STATUS_E_UNSUPPORTED; \
    } while(0)

#define PORT_EEPROM_FORMAT         "/sys/bus/i2c/devices/%d-0050/eeprom"
#define MODULE_PRESENT_FORMAT      "/sys/bus/i2c/devices/12-0061/module_present_%d"
#define MODULE_RXLOS_FORMAT        "/sys/bus/i2c/devices/12-0061/module_rx_los_%d"
#define MODULE_TXFAULT_FORMAT      "/sys/bus/i2c/devices/12-0061/module_tx_fault_%d"
#define MODULE_TXDISABLE_FORMAT    "/sys/bus/i2c/devices/12-0061/module_tx_disable_%d"
#define MODULE_RESET_FORMAT        "/sys/bus/i2c/devices/12-0061/module_reset_%d"
#define MODULE_LPMODE_FORMAT       "/sys/bus/i2c/devices/11-0060/module_lpmode_%d"
#define MODULE_PRESENT_ALL_ATTR    "/sys/bus/i2c/devices/12-0061/module_present_all"
#define MODULE_RXLOS_ALL_ATTR      "/sys/bus/i2c/devices/12-0061/module_rx_los_all"

/* Definitions for Soft_Rate_Select*/
#define PORT_EEPROM_DEVADDR 0X51
#define MULTIRATE_OFFSET 0x6E
#define MBIT_IDENTIFIER_ADDR 0x50
#define MBIT_OFFSET 0x0d
#define RATE_SELECT 0x02

/* Macro for Multirate Bit*/
#define SET_RATE_SELECT_BIT(x,RS_BIT_pos)  (x | (1<<RS_BIT_pos))
#define RESET_RATE_SELECT_BIT(x,RS_BIT_pos)  (x & ~(1<<RS_BIT_pos))

/* QSFP device address of eeprom */
#define XCVR_PORT_EEPROM_DEVADDR 0x50

/* QSFP eeprom offsets*/
#define QSFP_EEPROM_OFFSET_IDENTIFIER 0x0
#define QSFP_EEPROM_OFFSET_TXDIS 0x56
#define QSFP_EEPROM_OFFSET_BANK_SELECT 0x7E
#define QSFP_EEPROM_OFFSET_PAGE_SELECT 0x7F

/* QSFP DD Specific*/
#define QSFP_DD_IDENTIFIER 0x18
#define QSFP_DD_PAGE_ADMIN_INFO 0x0
#define QSFP_DD_PAGE_ADVERTISING 0x1
#define QSFP_DD_PAGE_LANE_CTRL 0x10
#define QSFP_DD_LOWER_OFFSET_STATUS 0x02
#define QSFP_DD_FLAT_MEM 0x80                          /* byte 0x02 bit 7 */
#define QSFP_DD_P01H_OFFSET_CONTROL_1 0x9B
#define QSFP_DD_P01H_TX_DISABLE_SUPPORT 0x2
#define QSFP_DD_P10H_OFFSET_OUTPUT_DISABLE_TX 0x82

#define NUM_OF_SFP_PORT 28
#define MAX_PORT (NUM_OF_SFP_PORT - 1)
static const int port_bus_index[NUM_OF_SFP_PORT] = {
    23, 21, 24, 22, 25, 26, 27, 28, 29, 30,
    31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48
};

#define PORT_BUS_INDEX(port) (port_bus_index[port])

enum log_reason{
    PRESENT_UNABLE_TO_GET_STATUS,
    EEPROM_UNABLE_TO_GET_DATA,
    EEPROM_UNABLE_TO_GET_DATA_SIZE_DIFF,
    DOM_UNABLE_TO_OPEN_EEPROM_FILE,
    DOM_UNABLE_TO_SET_FILE_POS_INDICATOR,
    DOM_UNABLE_TO_GET_EEPROM_DATA,
    TX_DIS_UNABLE_TO_SET_STATUS,
    TX_DIS_UNABLE_TO_GET_IDENTIFIER,
    TX_DIS_UNABLE_TO_GET_MEM_MODEL,
    TX_DIS_UNABLE_TO_SET_EEPROM_PAGE,
    TX_DIS_UNABLE_TO_GET_CONTROL,
    TX_DIS_UNABLE_TO_SET_BANK,
    TX_DIS_UNABLE_TO_GET_STATUS,
    LP_MODE_UNABLE_TO_GET_IDENTIFIER,
    LP_MODE_UNABLE_TO_SET_STATUS,
    LP_MODE_UNABLE_TO_GET_STATUS,
    RESET_UNABLE_TO_SET_STATUS,
    RESET_UNABLE_TO_GET_STATUS,
    RX_LOS_UNABLE_TO_GET_STATUS,
    TX_FAULT_UNABLE_TO_GET_STATUS,
    MULTIRATE_UNABLE_TO_SET_STATUS,
    LOG_REASON_COUNT,
};

struct sfp_log_ctrl {
    int should_log;
};

struct sfp_log_mgmt {
    int sfp_present_rec;
    struct sfp_log_ctrl log_ctrl[LOG_REASON_COUNT];
};

static struct sfp_log_mgmt log_mgmt[MAX_PORT+1] = {
    [0 ... MAX_PORT] = {
        .sfp_present_rec = ONLP_STATUS_E_INTERNAL,
        .log_ctrl = {
            [0 ... LOG_REASON_COUNT - 1] = { .should_log = 1 }
        }
    }
};

void syslog_ctrl(struct sfp_log_mgmt *log_mgmt_ptr, int log_reason, const char *fmt, ...) {
    char buf[256];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (log_mgmt_ptr->log_ctrl[log_reason].should_log) {
        syslog(LOG_ERR, "%s", buf);

        log_mgmt_ptr->log_ctrl[log_reason].should_log = 0;
    }
}

void reset_log_ctrl(struct sfp_log_mgmt *log_mgmt_ptr)
{
    int i;

    for (i = 0; i < LOG_REASON_COUNT; i++) {
        log_mgmt_ptr->log_ctrl[i].should_log = 1;
    }
}

/************************************************************
 *
 * SFPI Entry Points
 *
 ***********************************************************/
int
onlp_sfpi_init(void)
{
    /* Called at initialization time */
    return ONLP_STATUS_OK;
}

int
onlp_sfpi_bitmap_get(onlp_sfp_bitmap_t* bmap)
{
    /*
     * Ports {0, 27}
     */
    int p;

    for(p = 0; p < NUM_OF_SFP_PORT; p++) {
        AIM_BITMAP_SET(bmap, p);
    }

    return ONLP_STATUS_OK;
}

int
onlp_sfpi_is_present(int port)
{
    /*
     * Return 1 if present.
     * Return 0 if not present.
     * Return < 0 if error.
     */
    int present;
    VALIDATE(port);

    if (onlp_file_read_int(&present, MODULE_PRESENT_FORMAT, (port+1)) < 0) {
        if (log_mgmt[port].sfp_present_rec != ONLP_STATUS_E_INTERNAL) {
            reset_log_ctrl(&log_mgmt[port]);
        }
        log_mgmt[port].sfp_present_rec = ONLP_STATUS_E_INTERNAL;

        syslog_ctrl(&log_mgmt[port], PRESENT_UNABLE_TO_GET_STATUS,
                    "Unable to read present status from port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    if (present == 1 && present != log_mgmt[port].sfp_present_rec) {
        reset_log_ctrl(&log_mgmt[port]);
    }
    log_mgmt[port].sfp_present_rec = present;

    return present;
}

int
onlp_sfpi_presence_bitmap_get(onlp_sfp_bitmap_t* dst)
{
    uint32_t bytes[4];
    FILE* fp;

    /* Read present status of port 0 ~ 27 */
    int count  = 0;

    fp = fopen(MODULE_PRESENT_ALL_ATTR, "r");
    if(fp == NULL) {
        syslog(LOG_ERR, "Unable to open the module_present_all device file of CPLD");
        return ONLP_STATUS_E_INTERNAL;
    }

    count = fscanf(fp, "%x %x %x %x", bytes, bytes+1, bytes+2, bytes+3);
    fclose(fp);

    if(count != AIM_ARRAYSIZE(bytes)) {
        /* Likely a CPLD read timeout. */
        syslog(LOG_ERR, "Unable to read all fields the module_present_all device file of CPLD");
        return ONLP_STATUS_E_INTERNAL;
    }

    /* Mask out non-existant QSFP ports */
    bytes[3] &= 0xF;


    /* Convert to 64 bit integer in port order */
    int i = 0;
    uint64_t presence_all = 0 ;
    for(i = 3; i >= 0; i--) {
        presence_all <<= 8;
        presence_all |= bytes[i];
    }

    /* Populate bitmap */
    for(i = 0; presence_all; i++) {
        AIM_BITMAP_MOD(dst, i, (presence_all & 1));
        presence_all >>= 1;
    }

    return ONLP_STATUS_OK;
}

int
onlp_sfpi_rx_los_bitmap_get(onlp_sfp_bitmap_t* dst)
{
    uint32_t bytes[4];
    FILE* fp;

    /* Read rxlos status of port 0 ~ 27 */
    int count  = 0;

    fp = fopen(MODULE_RXLOS_ALL_ATTR, "r");
    if(fp == NULL) {
        syslog(LOG_ERR, "Unable to open the module_rx_los_all device file of CPLD");
        return ONLP_STATUS_E_INTERNAL;
    }

    count = fscanf(fp, "%x %x %x %x", bytes, bytes+1, bytes+2, bytes+3);
    fclose(fp);
    if(count != AIM_ARRAYSIZE(bytes)) {
        /* Likely a CPLD read timeout. */
        syslog(LOG_ERR, "Unable to read all fields from the module_rx_los_all device file of CPLD");
        return ONLP_STATUS_E_INTERNAL;
    }

    /* Convert to 32 bit integer in port order */
    AIM_BITMAP_CLR_ALL(dst);
    int i = 0;
    uint64_t rx_los_all = 0 ;
    for(i = 3; i >= 0; i--) {
        rx_los_all <<= 8;
        rx_los_all |= bytes[i];
    }

    /* Populate bitmap */
    for(i = 0; rx_los_all; i++) {
        AIM_BITMAP_MOD(dst, i, (rx_los_all & 1));
        rx_los_all >>= 1;
    }

    return ONLP_STATUS_OK;
}

int
onlp_sfpi_eeprom_read(int port, uint8_t data[256])
{
    /*
     * Read the SFP eeprom into data[]
     *
     * Return MISSING if SFP is missing.
     * Return OK if eeprom is read
     */
    int size = 0;

    VALIDATE(port);
    memset(data, 0, 256);

    if(onlp_file_read(data, 256, &size, PORT_EEPROM_FORMAT, PORT_BUS_INDEX(port)) != ONLP_STATUS_OK) {
        syslog_ctrl(&log_mgmt[port], EEPROM_UNABLE_TO_GET_DATA,
                    "Unable to read eeprom from port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    if (size != 256) {
        syslog_ctrl(&log_mgmt[port], EEPROM_UNABLE_TO_GET_DATA_SIZE_DIFF,
                    "Unable to read eeprom from port(%d), size is different!", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    return ONLP_STATUS_OK;
}

int
onlp_sfpi_dom_read(int port, uint8_t data[256])
{
    FILE* fp;
    char file[64] = {0};

    VALIDATE(port);
    sprintf(file, PORT_EEPROM_FORMAT, PORT_BUS_INDEX(port));
    fp = fopen(file, "r");
    if(fp == NULL) {
        syslog_ctrl(&log_mgmt[port], DOM_UNABLE_TO_OPEN_EEPROM_FILE,
                    "Unable to open the eeprom device file of port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    if (fseek(fp, 256, SEEK_CUR) != 0) {
        fclose(fp);
        syslog_ctrl(&log_mgmt[port], DOM_UNABLE_TO_SET_FILE_POS_INDICATOR,
                    "Unable to set the file position indicator of port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    int ret = fread(data, 1, 256, fp);
    fclose(fp);
    if (ret != 256) {
        syslog_ctrl(&log_mgmt[port], DOM_UNABLE_TO_GET_EEPROM_DATA,
                    "Unable to read the module_eeprom device file of port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    return ONLP_STATUS_OK;
}

int
onlp_sfpi_dev_readb(int port, uint8_t devaddr, uint8_t addr)
{
    VALIDATE(port);
    return onlp_i2c_readb(PORT_BUS_INDEX(port), devaddr, addr, ONLP_I2C_F_FORCE);
}

int
onlp_sfpi_dev_writeb(int port, uint8_t devaddr, uint8_t addr, uint8_t value)
{
    VALIDATE(port);
    return onlp_i2c_writeb(PORT_BUS_INDEX(port), devaddr, addr, value, ONLP_I2C_F_FORCE);
}

int
onlp_sfpi_dev_readw(int port, uint8_t devaddr, uint8_t addr)
{
    VALIDATE(port);
    return onlp_i2c_readw(PORT_BUS_INDEX(port), devaddr, addr, ONLP_I2C_F_FORCE);
}

int
onlp_sfpi_dev_writew(int port, uint8_t devaddr, uint8_t addr, uint16_t value)
{
    VALIDATE(port);
    return onlp_i2c_writew(PORT_BUS_INDEX(port), devaddr, addr, value, ONLP_I2C_F_FORCE);
}

int
onlp_sfpi_control_set(int port, onlp_sfp_control_t control, int value)
{
    int present = 0;
    int identifier = 0;
    int status_byte = 0;
    int support_ctrls = 0;
    int mbit_identifier;
    int mbit_value;
    int rv = ONLP_STATUS_OK;

    switch(control) {
    case ONLP_SFP_CONTROL_TX_DISABLE:
    case ONLP_SFP_CONTROL_TX_DISABLE_CHANNEL: {
        VALIDATE(port);
        present = onlp_sfpi_is_present(port);

        if (present == 1) {
            if (port >= 0 && port <= 3) {
                if ((identifier = onlp_sfpi_dev_readb(port, XCVR_PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_IDENTIFIER)) < 0 ) {
                    syslog_ctrl(&log_mgmt[port], TX_DIS_UNABLE_TO_GET_IDENTIFIER,
                                "Failed to read Identifier, unable to write tx_disable status to port(%d)", port);
                    return ONLP_STATUS_E_INTERNAL;
                }

                if (identifier == QSFP_DD_IDENTIFIER) {
                    /* Flat-memory CMIS modules do not implement page 01h/10h */
                    if ((status_byte = onlp_sfpi_dev_readb(port, XCVR_PORT_EEPROM_DEVADDR, QSFP_DD_LOWER_OFFSET_STATUS)) < 0) {
                        syslog_ctrl(&log_mgmt[port], TX_DIS_UNABLE_TO_GET_MEM_MODEL,
                                    "Failed to read Status byte, unable to write tx_disable status to port(%d)", port);
                        return ONLP_STATUS_E_INTERNAL;
                    }
                    if (status_byte & QSFP_DD_FLAT_MEM) {
                        return ONLP_STATUS_E_UNSUPPORTED;
                    }
                    if ((rv = onlp_sfpi_dev_writeb(port, XCVR_PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADVERTISING)) < 0) {
                        syslog_ctrl(&log_mgmt[port], TX_DIS_UNABLE_TO_SET_EEPROM_PAGE,
                                    "Failed to switch to Advertising Page on port(%d)", port);
                        goto restore;
                    }
                    if ((support_ctrls = onlp_sfpi_dev_readb(port, XCVR_PORT_EEPROM_DEVADDR, QSFP_DD_P01H_OFFSET_CONTROL_1)) < 0 ) {
                        syslog_ctrl(&log_mgmt[port], TX_DIS_UNABLE_TO_GET_CONTROL,
                                    "Failed to read Support Control on port(%d)", port);
                        rv = support_ctrls;
                        goto restore;
                    }
                    if (support_ctrls & QSFP_DD_P01H_TX_DISABLE_SUPPORT) {
                        if ((rv = onlp_sfpi_dev_writeb(port, XCVR_PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_BANK_SELECT, 0)) < 0) {
                            syslog_ctrl(&log_mgmt[port], TX_DIS_UNABLE_TO_SET_BANK,
                                        "Failed to set Bank 0, unable to write tx_disable status to port(%d)", port);
                            goto restore;
                        }
                        if ((rv = onlp_sfpi_dev_writeb(port, XCVR_PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_LANE_CTRL)) < 0 ) {
                            syslog_ctrl(&log_mgmt[port], TX_DIS_UNABLE_TO_SET_EEPROM_PAGE,
                                        "Failed to switch to Lane Control Page (Page 0x%02x) on port(%d)",
                                        QSFP_DD_PAGE_LANE_CTRL, port);
                            goto restore;
                        }
                        if ((rv = onlp_sfpi_dev_writeb(port, XCVR_PORT_EEPROM_DEVADDR, QSFP_DD_P10H_OFFSET_OUTPUT_DISABLE_TX, (value & 0xff))) < 0) {
                            syslog_ctrl(&log_mgmt[port], TX_DIS_UNABLE_TO_SET_STATUS,
                                        "Failed to write tx_disable value(0x%02x) to Lane Control register on port(%d)", value, port);
                            goto restore;
                        }
                    } else {
                        rv = ONLP_STATUS_E_UNSUPPORTED;
                        goto restore;
                    }

                restore:
                    if ((onlp_sfpi_dev_writeb(port, XCVR_PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_PAGE_SELECT,
                                                            QSFP_DD_PAGE_ADMIN_INFO)) < 0) {
                        syslog_ctrl(&log_mgmt[port], TX_DIS_UNABLE_TO_SET_EEPROM_PAGE,
                                    "Failed to restore Page Select to Admin Info on port(%d)!", port);
                    }
                    if (rv < 0) {
                        return (rv == ONLP_STATUS_E_UNSUPPORTED) ? rv : ONLP_STATUS_E_INTERNAL;
                    }

                    return ONLP_STATUS_OK;

                } else { /* QSFP */
                    /* txdis valid bit(bit0-bit3), xxxx 1111 */
                    if (onlp_sfpi_dev_writeb(port, XCVR_PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_TXDIS, (value & 0xf)) < 0) {
                        syslog_ctrl(&log_mgmt[port], TX_DIS_UNABLE_TO_SET_STATUS,
                                    "Unable to write tx_disable status to port(%d)", port);
                        return ONLP_STATUS_E_INTERNAL;
                    }
                    return ONLP_STATUS_OK;
                }
            } else {
                if (onlp_file_write_int(value, MODULE_TXDISABLE_FORMAT, (port+1)) < 0) {
                    syslog_ctrl(&log_mgmt[port], TX_DIS_UNABLE_TO_SET_STATUS,
                                "Unable to write tx_disable status to port(%d)", port);
                    return ONLP_STATUS_E_INTERNAL;
                }
                return ONLP_STATUS_OK;
            }
        } else {
            return ONLP_STATUS_E_INTERNAL;
        }
    }
    case ONLP_SFP_CONTROL_RESET: {
        VALIDATE_QSFP(port);

        if (onlp_file_write_int(value, MODULE_RESET_FORMAT, (port+1)) < 0) {
            syslog_ctrl(&log_mgmt[port], RESET_UNABLE_TO_SET_STATUS,
                        "Unable to write reset status to port(%d)", port);
            return ONLP_STATUS_E_INTERNAL;
        }
        return ONLP_STATUS_OK;
    }
    case ONLP_SFP_CONTROL_LP_MODE: {
        VALIDATE_QSFP(port);

        if (onlp_file_write_int(value, MODULE_LPMODE_FORMAT, (port+1)) < 0) {
            syslog_ctrl(&log_mgmt[port], LP_MODE_UNABLE_TO_SET_STATUS,
                        "Unable to write LP mode status to port(%d)", port);
            return ONLP_STATUS_E_INTERNAL;
        }
        else {
            return ONLP_STATUS_OK;
        }
    }
    case ONLP_SFP_CONTROL_SOFT_RATE_SELECT: {
        VALIDATE_SFP(port);

        present = onlp_sfpi_is_present(port);
        if (present == 1) {
            mbit_identifier = onlp_sfpi_dev_readb(port, MBIT_IDENTIFIER_ADDR, MBIT_OFFSET);
            if (mbit_identifier == RATE_SELECT) {
                mbit_value = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, MULTIRATE_OFFSET);
                if (value == 0x0) {
                    mbit_value = RESET_RATE_SELECT_BIT(mbit_value,3);
                    if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, MULTIRATE_OFFSET, mbit_value) < 0){
                        syslog_ctrl(&log_mgmt[port], MULTIRATE_UNABLE_TO_SET_STATUS,
                                    "Unable to write multirate status to port(%d)", port);
                        return ONLP_STATUS_E_INTERNAL;
                    }
                    return ONLP_STATUS_OK;
                }
                else if(value == 0x1) {
                    mbit_value = SET_RATE_SELECT_BIT(mbit_value,3);
                    if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, MULTIRATE_OFFSET, mbit_value) < 0){
                        syslog_ctrl(&log_mgmt[port], MULTIRATE_UNABLE_TO_SET_STATUS,
                                    "Unable to write multirate status to port(%d)", port);
                        return ONLP_STATUS_E_INTERNAL;
                    }
                    return ONLP_STATUS_OK;
                }
                return ONLP_STATUS_E_INTERNAL;
            }
            else {
                return ONLP_STATUS_E_UNSUPPORTED;
            }
        }
        else {
            syslog_ctrl(&log_mgmt[port], MULTIRATE_UNABLE_TO_SET_STATUS,
                        "Unable to write multirate status to port(%d)", port);
            return ONLP_STATUS_E_INTERNAL;
        }
    }
    default:
        break;
    }

    return ONLP_STATUS_E_UNSUPPORTED;
}

int
onlp_sfpi_control_get(int port, onlp_sfp_control_t control, int* value)
{
    int present = 0;
    int identifier = 0;
    int status_byte = 0;
    int support_ctrls = 0;
    int tx_dis = 0;
    int multirate = 0;
    int mbit_identifier;
    int rv = ONLP_STATUS_OK;

    switch(control) {
    case ONLP_SFP_CONTROL_RX_LOS: {
        VALIDATE_SFP(port);

        if (onlp_file_read_int(value, MODULE_RXLOS_FORMAT, (port+1)) < 0) {
            syslog_ctrl(&log_mgmt[port], RX_LOS_UNABLE_TO_GET_STATUS,
                        "Unable to read rx_loss status from port(%d)", port);
            return ONLP_STATUS_E_INTERNAL;
        }

        return ONLP_STATUS_OK;
    }

    case ONLP_SFP_CONTROL_TX_FAULT: {
        VALIDATE_SFP(port);

        if (onlp_file_read_int(value, MODULE_TXFAULT_FORMAT, (port+1)) < 0) {
            syslog_ctrl(&log_mgmt[port], TX_FAULT_UNABLE_TO_GET_STATUS,
                        "Unable to read tx_fault status from port(%d)", port);
            return ONLP_STATUS_E_INTERNAL;
        }

        return ONLP_STATUS_OK;
    }

    case ONLP_SFP_CONTROL_TX_DISABLE:
    case ONLP_SFP_CONTROL_TX_DISABLE_CHANNEL: {
        VALIDATE(port);
        present = onlp_sfpi_is_present(port);

        if (present == 1) {
            if (port >= 0 && port <= 3) {
                if ((identifier = onlp_sfpi_dev_readb(port, XCVR_PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_IDENTIFIER)) < 0) {
                    syslog_ctrl(&log_mgmt[port], TX_DIS_UNABLE_TO_GET_IDENTIFIER,
                                "Failed to read Identifier, unable to read tx_disable status from port(%d)", port);
                    return ONLP_STATUS_E_INTERNAL;
                }

                if (identifier == QSFP_DD_IDENTIFIER) {
                    /* Flat-memory CMIS modules do not implement page 01h/10h */
                    if ((status_byte = onlp_sfpi_dev_readb(port, XCVR_PORT_EEPROM_DEVADDR, QSFP_DD_LOWER_OFFSET_STATUS)) < 0) {
                        syslog_ctrl(&log_mgmt[port], TX_DIS_UNABLE_TO_GET_MEM_MODEL,
                                    "Failed to read Status byte, unable to read tx_disable status from port(%d)", port);
                        return ONLP_STATUS_E_INTERNAL;
                    }
                    if (status_byte & QSFP_DD_FLAT_MEM) {
                        return ONLP_STATUS_E_UNSUPPORTED;
                    }
                    if ((rv = onlp_sfpi_dev_writeb(port, XCVR_PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADVERTISING)) < 0) {
                        syslog_ctrl(&log_mgmt[port], TX_DIS_UNABLE_TO_SET_EEPROM_PAGE,
                                    "Failed to switch to Advertising Page on port(%d)", port);
                        goto restore;
                    }
                    if ((support_ctrls = onlp_sfpi_dev_readb(port, XCVR_PORT_EEPROM_DEVADDR, QSFP_DD_P01H_OFFSET_CONTROL_1)) < 0) {
                        syslog_ctrl(&log_mgmt[port], TX_DIS_UNABLE_TO_GET_CONTROL,
                                    "Failed to read Support Control on port(%d)", port);
                        rv = support_ctrls;
                        goto restore;
                    }
                    if (support_ctrls & QSFP_DD_P01H_TX_DISABLE_SUPPORT) {
                        if ((rv = onlp_sfpi_dev_writeb(port, XCVR_PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_BANK_SELECT, 0)) < 0) {
                            syslog_ctrl(&log_mgmt[port], TX_DIS_UNABLE_TO_SET_BANK,
                                        "Failed to set Bank 0, unable to read tx_disable status from port(%d)", port);
                            goto restore;
                        }
                        if ((rv = onlp_sfpi_dev_writeb(port, XCVR_PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_LANE_CTRL)) < 0) {
                            syslog_ctrl(&log_mgmt[port], TX_DIS_UNABLE_TO_SET_EEPROM_PAGE,
                                        "Failed to switch to Lane Control Page (Page 0x%02x) on port(%d)",
                                        QSFP_DD_PAGE_LANE_CTRL, port);
                            goto restore;
                        }
                        if ((tx_dis = onlp_sfpi_dev_readb(port, XCVR_PORT_EEPROM_DEVADDR, QSFP_DD_P10H_OFFSET_OUTPUT_DISABLE_TX)) < 0) {
                            syslog_ctrl(&log_mgmt[port], TX_DIS_UNABLE_TO_GET_STATUS,
                                        "Failed to read TX_DISABLE value from Lane Control register on port(%d)", port);
                            rv = tx_dis;
                            goto restore;
                        }
                    } else {
                        rv = ONLP_STATUS_E_UNSUPPORTED;
                        goto restore;
                    }

                restore:
                    if ((onlp_sfpi_dev_writeb(port, XCVR_PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_PAGE_SELECT,
                                                            QSFP_DD_PAGE_ADMIN_INFO)) < 0) {
                        syslog_ctrl(&log_mgmt[port], TX_DIS_UNABLE_TO_SET_EEPROM_PAGE,
                                    "Failed to restore Page Select to Admin Info on port(%d)!", port);
                    }

                    if (rv < 0) {
                        return (rv == ONLP_STATUS_E_UNSUPPORTED) ? rv : ONLP_STATUS_E_INTERNAL;
                    }
                    *value = (tx_dis & 0xff);
                    return ONLP_STATUS_OK;
                } else { /* QSFP */
                    if ((tx_dis = onlp_sfpi_dev_readb(port, XCVR_PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_TXDIS)) < 0) {
                        syslog_ctrl(&log_mgmt[port], TX_DIS_UNABLE_TO_GET_STATUS,
                                    "Unable to read tx_disable status from port(%d)", port);
                        return ONLP_STATUS_E_INTERNAL;
                    }
                    *value = (tx_dis & 0xf);
                    return ONLP_STATUS_OK;
                }
            } else { /* SFP */
                if (onlp_file_read_int(value, MODULE_TXDISABLE_FORMAT, (port+1)) < 0) {
                        syslog_ctrl(&log_mgmt[port], TX_DIS_UNABLE_TO_GET_STATUS,
                                    "Unable to read tx_disable status from port(%d)", port);
                        return ONLP_STATUS_E_INTERNAL;
                }
                return ONLP_STATUS_OK;
            }
        } else {
            return ONLP_STATUS_E_INTERNAL;
        }
    }
    case ONLP_SFP_CONTROL_RESET: {
        VALIDATE_QSFP(port);

        if (onlp_file_read_int(value, MODULE_RESET_FORMAT, (port+1)) < 0) {
            syslog_ctrl(&log_mgmt[port], RESET_UNABLE_TO_GET_STATUS,
                        "Unable to read reset status from port(%d)", port);
            return ONLP_STATUS_E_INTERNAL;
        }

        return ONLP_STATUS_OK;
    }
    case ONLP_SFP_CONTROL_LP_MODE: {
        VALIDATE_QSFP(port);

        if (onlp_file_read_int(value, MODULE_LPMODE_FORMAT, (port+1)) < 0) {
            syslog_ctrl(&log_mgmt[port], LP_MODE_UNABLE_TO_GET_STATUS,
                        "Unable to read LP mode status from port(%d)", port);
            return ONLP_STATUS_E_INTERNAL;
        }

        return ONLP_STATUS_OK;
    }
    case ONLP_SFP_CONTROL_SOFT_RATE_SELECT: {
        VALIDATE_SFP(port);

        present = onlp_sfpi_is_present(port);
        if(present == 1) {
            mbit_identifier = onlp_sfpi_dev_readb(port, MBIT_IDENTIFIER_ADDR, MBIT_OFFSET);
            if(mbit_identifier == RATE_SELECT) {
                multirate = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, MULTIRATE_OFFSET);
                if (multirate < 0) {
                    return ONLP_STATUS_E_INTERNAL;
                }
                else{
                    *value = multirate;
                    return ONLP_STATUS_OK;
                }
            }
            else {
                return ONLP_STATUS_E_UNSUPPORTED;
            }
        }
        else {
            return ONLP_STATUS_E_INTERNAL;
        }
    }
    default:
        break;
    }

    return ONLP_STATUS_E_UNSUPPORTED;
}


int
onlp_sfpi_denit(void)
{
    return ONLP_STATUS_OK;
}
