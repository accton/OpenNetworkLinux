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
#include <syslog.h>
#include <onlp/platformi/sfpi.h>
#include <onlplib/i2c.h>
#include <onlplib/file.h>
#include "x86_64_accton_as9957_32db_int.h"
#include "x86_64_accton_as9957_32db_log.h"
#include "log_ctrl.h"

#define SFP_PORT_MIN 33
#define SFP_PORT_MAX 36
#define QSFP_PORT_MIN 1
#define QSFP_PORT_MAX 32
#define MIN_PORT QSFP_PORT_MIN
#define MAX_PORT SFP_PORT_MAX

#define VALIDATE(_port) \
    do { \
        if (_port < MIN_PORT || _port > MAX_PORT) { \
            return ONLP_STATUS_E_INVALID; \
        } \
    } while(0)

#define VALIDATE_SFP(_port) \
    do { \
        if (_port < SFP_PORT_MIN || _port > SFP_PORT_MAX) \
            return ONLP_STATUS_E_UNSUPPORTED; \
    } while(0)

#define VALIDATE_QSFP(_port) \
    do { \
        if (_port < QSFP_PORT_MIN || _port > QSFP_PORT_MAX ) \
            return ONLP_STATUS_E_UNSUPPORTED; \
    } while(0)

#define MODULE_EEPROM_FORMAT       "/sys/bus/i2c/devices/%d-0050/eeprom"
#define MODULE_PRESENT_FORMAT      "/sys/devices/platform/as9957_32db_fpga/module_present_%d"
#define MODULE_RXLOS_FORMAT        "/sys/devices/platform/as9957_32db_fpga/module_rx_los_%d"
#define MODULE_TXFAULT_FORMAT      "/sys/devices/platform/as9957_32db_fpga/module_tx_fault_%d"
#define MODULE_TXDISABLE_FORMAT    "/sys/devices/platform/as9957_32db_fpga/module_tx_disable_%d"
#define MODULE_RESET_FORMAT        "/sys/devices/platform/as9957_32db_fpga/module_reset_%d"
#define MODULE_LPMODE_FORMAT       "/sys/devices/platform/as9957_32db_fpga/module_lp_mode_%d"

#define NUM_OF_SFP_PORT 36
static const int port_bus_index[NUM_OF_SFP_PORT] = {
    1,   2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16,
    17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
    33, 34, 35, 36
};

#define PORT_BUS_INDEX(port) (port_bus_index[port-1])

/* QSFP device address of eeprom */
#define PORT_EEPROM_DEVADDR 0x50

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

static struct sfp_log_mgmt log_mgmt[MAX_PORT] = {
    [0 ... MAX_PORT-1] = {
        .present_rec = ONLP_STATUS_E_INTERNAL,
        .log_ctrl = {
            [0 ... SFP_LOG_REASON_COUNT - 1] = { .should_log = 1 }
        }
    }
};

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
     * Ports {1, 36}
     */
    int p;

    for(p = 1; p <= NUM_OF_SFP_PORT; p++) {
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

    if (onlp_file_read_int(&present, MODULE_PRESENT_FORMAT, port) < 0) {
        log_mgmt[port-1].present_rec = ONLP_STATUS_E_INTERNAL;
        
        syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_SYSFS_READ_FAIL, 
                    "Unable to read present status from port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    if (present == 1 && present != log_mgmt[port-1].present_rec) {
        reset_log_ctrl(log_mgmt[port-1].log_ctrl, SFP_LOG_REASON_COUNT);
    }
    log_mgmt[port-1].present_rec = present;

    return present;
}

int
onlp_sfpi_rx_los_bitmap_get(onlp_sfp_bitmap_t* dst)
{
    int i=0, val=0;
    /* Populate bitmap */
    for (i = 1; i <= NUM_OF_SFP_PORT; i++) {
        val = 0;

        if ((i >= 33) && (i <= 36)) {
            if (onlp_file_read_int(&val, MODULE_RXLOS_FORMAT, i) < 0) {
                syslog_ctrl(log_mgmt[i-1].log_ctrl, SFP_SYSFS_READ_FAIL, 
                            "Unable to read rx_loss status from port(%d)", i);
            }

            if (val)
                AIM_BITMAP_MOD(dst, i, 1);
            else
                AIM_BITMAP_MOD(dst, i, 0);
        }
        else {
            AIM_BITMAP_MOD(dst, i, 0);
        }
    }

    return ONLP_STATUS_OK;
}

int
onlp_sfpi_eeprom_read(int port, uint8_t data[256])
{
    int size = 0;
    memset(data, 0, 256);

    VALIDATE(port);

    if(onlp_file_read(data, 256, &size, MODULE_EEPROM_FORMAT, PORT_BUS_INDEX(port)) != ONLP_STATUS_OK) {
        syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_EEPROM_READ_FAIL, "Unable to read eeprom from port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    if (size != 256) {
        syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_INVALID_DATA_SIZE, 
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
    sprintf(file, MODULE_EEPROM_FORMAT, PORT_BUS_INDEX(port));
    fp = fopen(file, "r");
    if(fp == NULL) {
        syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_EEPROM_READ_FAIL, 
                   "Unable to open the eeprom device file of port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    if (fseek(fp, 256, SEEK_CUR) != 0) {
        fclose(fp);
        syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_EEPROM_READ_FAIL, 
                    "Unable to set the file position indicator of port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    int ret = fread(data, 1, 256, fp);
    fclose(fp);
    if (ret != 256) {
        syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_EEPROM_READ_FAIL, 
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
    int rv;
    int present = 0;
    int identifier = 0;
    int status_byte = 0;
    int support_ctrls = 0;

    VALIDATE(port);

    switch(control) {
    case ONLP_SFP_CONTROL_TX_DISABLE:
    case ONLP_SFP_CONTROL_TX_DISABLE_CHANNEL: {
        present = onlp_sfpi_is_present(port);

        if (present == 1) {
            if(port >= 1 && port <= 32) {
                if ((identifier = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_IDENTIFIER)) < 0) {
                    syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_EEPROM_PREP_FAIL, 
                                "Unable to write tx_disable status to port(%d): read identifier from eeprom fail", 
                                port);
                    return ONLP_STATUS_E_INTERNAL;
                }

                if (identifier == QSFP_DD_IDENTIFIER) {
                    /* Flat-memory CMIS modules do not implement page 01h/10h */
                    if ((status_byte = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_LOWER_OFFSET_STATUS)) < 0) {
                        syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_EEPROM_PREP_FAIL, 
                                    "Failed to read Status byte, unable to write tx_disable status to port(%d)", port);
                        return ONLP_STATUS_E_INTERNAL;
                    }
                    if (status_byte & QSFP_DD_FLAT_MEM) {
                        return ONLP_STATUS_E_UNSUPPORTED;
                    }
                    if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_BANK_SELECT, 0)) < 0) {
                        syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_EEPROM_PREP_FAIL, 
                                    "Unable to write tx_disable status to port(%d): write bank to eeprom fail", port);
                        return ONLP_STATUS_E_INTERNAL;
                    }
                    if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADVERTISING)) < 0) {
                        syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_EEPROM_PREP_FAIL, 
                                    "Unable to write tx_disable status to port(%d): write page to eeprom fail", port);
                        goto restore;
                    }
                    if ((support_ctrls = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_P01H_OFFSET_CONTROL_1)) < 0) {
                        syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_EEPROM_PREP_FAIL, 
                                    "Unable to write tx_disable status to port(%d): read control from eeprom fail", port);
                        rv = support_ctrls;
                        goto restore;
                    }
                    if (support_ctrls & QSFP_DD_P01H_TX_DISABLE_SUPPORT) {
                        if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_LANE_CTRL)) < 0) {
                            syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_EEPROM_PREP_FAIL, 
                                        "Unable to write tx_disable status to port(%d): write page to eeprom fail", port);
                            goto restore;
                        }
                        if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_P10H_OFFSET_OUTPUT_DISABLE_TX, (value & 0xff))) < 0) {
                            syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_EEPROM_WRITE_TARGET_BYTE_FAIL, 
                                        "Unable to write tx_disable status to port(%d): write TX disable to eeprom fail", port);
                            goto restore;
                        }
                    } else {
                        rv = ONLP_STATUS_E_UNSUPPORTED;
                        goto restore;
                    }

                restore:
                    if ((onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_PAGE_SELECT,
                                                            QSFP_DD_PAGE_ADMIN_INFO)) < 0) {
                        syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_EEPROM_PREP_FAIL, 
                                    "Failed to restore Page Select to Admin Info on port(%d)!", port);
                    }

                    if (rv < 0) {
                        rv = (rv == ONLP_STATUS_E_UNSUPPORTED) ? rv : ONLP_STATUS_E_INTERNAL;
                    } else {
                        rv = ONLP_STATUS_OK;
                    }
                } else { /* QSFP */
                    /* txdis valid bit(bit0-bit3), xxxx 1111 */
                    if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_TXDIS, (value & 0xf)) < 0) {
                        syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_EEPROM_WRITE_TARGET_BYTE_FAIL, 
                                    "Unable to write tx_disable status to port(%d): write TX disable to eeprom fail", port);
                        rv = ONLP_STATUS_E_INTERNAL;
                    } else {
                        rv = ONLP_STATUS_OK;
                    }
                }
            } else {
                if (onlp_file_write_int(value, MODULE_TXDISABLE_FORMAT, port) < 0) {
                    syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_SYSFS_WRITE_FAIL, 
                                "Unable to write tx_disable status to port(%d)", port);
                    rv = ONLP_STATUS_E_INTERNAL;
                } else {
                    rv = ONLP_STATUS_OK;
                }
            }
        } else {
            rv = ONLP_STATUS_E_INTERNAL;
        }
        break;
    }
    case ONLP_SFP_CONTROL_RESET_STATE: {
        VALIDATE_QSFP(port);

        if (onlp_file_write_int(value, MODULE_RESET_FORMAT, port) < 0) {
            syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_SYSFS_WRITE_FAIL, 
                        "Unable to write reset status to port(%d)", port);
            rv = ONLP_STATUS_E_INTERNAL;
        } else {
            rv = ONLP_STATUS_OK;
        }
        break;
    }
    case ONLP_SFP_CONTROL_LP_MODE: {
        VALIDATE_QSFP(port);

        if (onlp_file_write_int(value, MODULE_LPMODE_FORMAT, port) < 0) {
            syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_SYSFS_WRITE_FAIL,
                        "Unable to write LP mode to port(%d)", port);
            rv = ONLP_STATUS_E_INTERNAL;
        } else {
            rv = ONLP_STATUS_OK;
        }
        break;
    }
    default:
        rv = ONLP_STATUS_E_UNSUPPORTED;
        break;
    }

    return rv;
}

int
onlp_sfpi_control_get(int port, onlp_sfp_control_t control, int* value)
{
    int rv;
    int present = 0;
    int identifier = 0;
    int status_byte = 0;
    int support_ctrls = 0;
    int tx_dis = 0;

    VALIDATE(port);

    switch(control) {
    case ONLP_SFP_CONTROL_RX_LOS: {
        VALIDATE_SFP(port);

        if (onlp_file_read_int(value, MODULE_RXLOS_FORMAT, port) < 0) {
                syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_SYSFS_READ_FAIL, 
                            "Unable to read rx_loss status from port(%d)", port);
            rv = ONLP_STATUS_E_INTERNAL;
        } else {
            rv = ONLP_STATUS_OK;
        }
        break;
    }

    case ONLP_SFP_CONTROL_TX_FAULT: {
        VALIDATE_SFP(port);

        if (onlp_file_read_int(value, MODULE_TXFAULT_FORMAT, port) < 0) {
            syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_SYSFS_READ_FAIL, 
                        "Unable to read tx_fault status from port(%d)", port);
            rv = ONLP_STATUS_E_INTERNAL;
        } else {
            rv = ONLP_STATUS_OK;
        }
        break;
    }

    case ONLP_SFP_CONTROL_TX_DISABLE:
    case ONLP_SFP_CONTROL_TX_DISABLE_CHANNEL: {
        present = onlp_sfpi_is_present(port);

        if (present == 1) {
            if (port >= 1 && port <= 32) {
                if ((identifier = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_IDENTIFIER)) < 0) {
                    syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_EEPROM_PREP_FAIL, 
                                "Unable to read tx_disabled status from port(%d): read identifier from eeprom fail", 
                                port);
                    return ONLP_STATUS_E_INTERNAL;
                }

                if (identifier == QSFP_DD_IDENTIFIER) {
                    /* Flat-memory CMIS modules do not implement page 01h/10h */
                    if ((status_byte = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_LOWER_OFFSET_STATUS)) < 0) {
                        syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_EEPROM_PREP_FAIL, 
                                    "Failed to read Status byte, unable to read tx_disable status from port(%d)", port);
                        return ONLP_STATUS_E_INTERNAL;
                    }
                    if (status_byte & QSFP_DD_FLAT_MEM) {
                        return ONLP_STATUS_E_UNSUPPORTED;
                    }
                    if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_BANK_SELECT, 0)) < 0) {
                        syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_EEPROM_PREP_FAIL, 
                                    "Unable to read tx_disable status from port(%d): write bank to eeprom fail", port);
                        return ONLP_STATUS_E_INTERNAL;
                    }
                    if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADVERTISING)) < 0) {
                        syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_EEPROM_PREP_FAIL, 
                                    "Failed to switch to Advertising Page on port(%d)", port);
                        goto restore;
                    }
                    if ((support_ctrls = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_P01H_OFFSET_CONTROL_1)) < 0) {
                        syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_EEPROM_PREP_FAIL, 
                                    "Failed to read Support Control on port(%d)", port);
                        rv = support_ctrls;
                        goto restore;
                    }
                    if (support_ctrls & QSFP_DD_P01H_TX_DISABLE_SUPPORT) {
                        if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_LANE_CTRL)) < 0) {
                            syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_EEPROM_PREP_FAIL, 
                                        "Failed to switch to Lane Control Page (Page 0x%02x) on port(%d)",
                                        QSFP_DD_PAGE_LANE_CTRL, port);
                            goto restore;
                        }
                        if ((tx_dis = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_P10H_OFFSET_OUTPUT_DISABLE_TX)) < 0) {
                            syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_EEPROM_READ_TARGET_BYTE_FAIL, 
                                        "Unable to read tx_disable status from port(%d): read TX disable from eeprom fail",
                                        port);
                            rv = tx_dis;
                            goto restore;
                        }
                    } else {
                        rv = ONLP_STATUS_E_UNSUPPORTED;
                        goto restore;
                    }

                restore:
                    if ((onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_PAGE_SELECT,
                                                            QSFP_DD_PAGE_ADMIN_INFO)) < 0) {
                        syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_EEPROM_PREP_FAIL, 
                                    "Failed to restore Page Select to Admin Info on port(%d)!", port);
                    }

                    if (rv < 0) {
                        rv = (rv == ONLP_STATUS_E_UNSUPPORTED) ? rv : ONLP_STATUS_E_INTERNAL;
                    } else {
                        *value = (tx_dis & 0xff);
                        rv = ONLP_STATUS_OK;
                    }
                } else { /* QSFP */
                    if ((tx_dis = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_TXDIS)) < 0) {
                        syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_EEPROM_READ_TARGET_BYTE_FAIL, 
                                    "Unable to read tx_disable status from port(%d): read TX disable from eeprom fail", 
                                    port);
                        rv = ONLP_STATUS_E_INTERNAL;
                    } else {
                        *value = (tx_dis & 0xf);
                        rv = ONLP_STATUS_OK;
                    }
                }
            } else { /* SFP */
                if (onlp_file_read_int(value, MODULE_TXDISABLE_FORMAT,
                            port) < 0) {
                    syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_SYSFS_READ_FAIL, 
                                "Unable to read tx_disabled status from port(%d)", port);
                    rv = ONLP_STATUS_E_INTERNAL;
                } else {
                    rv = ONLP_STATUS_OK;
                }
            }
        } else {
            rv = ONLP_STATUS_E_INTERNAL;
        }
        break;
    }

    case ONLP_SFP_CONTROL_RESET_STATE: {
        VALIDATE_QSFP(port);

        if (onlp_file_read_int(value, MODULE_RESET_FORMAT, port) < 0) {
            syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_SYSFS_READ_FAIL, 
                        "Unable to get reset status from port(%d)", port);
            rv = ONLP_STATUS_E_INTERNAL;
        } else {
            rv = ONLP_STATUS_OK;
        }
        break;
    }
    case ONLP_SFP_CONTROL_LP_MODE: {
        VALIDATE_QSFP(port);

        if (onlp_file_read_int(value, MODULE_LPMODE_FORMAT, port) < 0) {
            syslog_ctrl(log_mgmt[port-1].log_ctrl, SFP_SYSFS_READ_FAIL,
                        "Unable to read LP mode status from port(%d)", port);
            rv = ONLP_STATUS_E_INTERNAL;
        } else {
            rv = ONLP_STATUS_OK;
        }
        break;
    }
    default:
        rv = ONLP_STATUS_E_UNSUPPORTED;
        break;
    }
    return rv;
}

int
onlp_sfpi_denit(void)
{
    return ONLP_STATUS_OK;
}
