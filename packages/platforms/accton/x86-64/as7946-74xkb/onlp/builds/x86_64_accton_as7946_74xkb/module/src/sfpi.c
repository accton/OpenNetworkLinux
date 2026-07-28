/************************************************************
 * <bsn.cl fy=2014 v=onl>
 *
 *           Copyright 2014 Big Switch Networks, Inc.
 *           Copyright 2017 Accton Technology Corporation.
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
#include "platform_lib.h"
#include <syslog.h>

#define NUM_OF_SFP_PORT     74

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

#define VALIDATE(_port) \
    do { \
        if (_port < 0 || _port > (NUM_OF_SFP_PORT-1)) \
            return ONLP_STATUS_E_INVALID; \
    } while(0)

#define VALIDATE_SFP(_port) \
    do { \
        if (_port < 10 || _port > (NUM_OF_SFP_PORT-1)) \
            return ONLP_STATUS_E_UNSUPPORTED; \
    } while(0)

#define VALIDATE_QSFP(_port) \
    do { \
        if (_port < 0 || _port > 9) \
            return ONLP_STATUS_E_UNSUPPORTED; \
    } while(0)

static const int port_bus_index[NUM_OF_SFP_PORT] = {
    33,  34,  35,  36,  37,  38,  39,  40,  41,  42,
    43,  44,  45,  46,  47,  48,  49,  50,  51,  52,
    53,  54,  55,  56,  57,  58,  59,  60,  61,  62,
    63,  64,  65,  66,  67,  68,  69,  70,  71,  72,
    73,  74,  75,  76,  77,  78,  79,  80,  81,  82,
    83,  84,  85,  86,  87,  88,  89,  90,  91,  92,
    93,  94,  95,  96,  97,  98,  99, 100, 101, 102,
    103, 104, 105, 106
};

#define PORT_BUS_INDEX(port) (port_bus_index[port])
#define PORT_FORMAT "/sys/bus/i2c/devices/%d-0050/%s"
#define PORT_EEPROM_FORMAT "/sys/bus/i2c/devices/%d-0050/eeprom"
#define MODULE_RESET_MAIN_BOARD_CPLD1_FORMAT "/sys/bus/i2c/devices/12-0061/module_reset_%d"
#define MODULE_RESET_MAIN_BOARD_CPLD2_FORMAT "/sys/bus/i2c/devices/13-0062/module_reset_%d"
#define MODULE_RESET_MAIN_BOARD_CPLD3_FORMAT "/sys/bus/i2c/devices/16-0063/module_reset_%d"
#define MODULE_LPMODE_MAIN_BOARD_CPLD1_FORMAT "/sys/bus/i2c/devices/12-0061/module_lpmode_%d"
#define MODULE_PRESENT_MAIN_BOARD_CPLD1_FORMAT "/sys/bus/i2c/devices/12-0061/module_present_%d"
#define MODULE_PRESENT_MAIN_BOARD_CPLD2_FORMAT "/sys/bus/i2c/devices/13-0062/module_present_%d"
#define MODULE_PRESENT_MAIN_BOARD_CPLD3_FORMAT "/sys/bus/i2c/devices/16-0063/module_present_%d"
#define MODULE_RXLOS_MAIN_BOARD_CPLD1_FORMAT "/sys/bus/i2c/devices/12-0061/module_rx_los_%d"
#define MODULE_RXLOS_MAIN_BOARD_CPLD2_FORMAT "/sys/bus/i2c/devices/13-0062/module_rx_los_%d"
#define MODULE_RXLOS_MAIN_BOARD_CPLD3_FORMAT "/sys/bus/i2c/devices/16-0063/module_rx_los_%d"
#define MODULE_TXDISABLE_MAIN_BOARD_CPLD1_FORMAT "/sys/bus/i2c/devices/12-0061/module_tx_disable_%d"
#define MODULE_TXDISABLE_MAIN_BOARD_CPLD2_FORMAT "/sys/bus/i2c/devices/13-0062/module_tx_disable_%d"
#define MODULE_TXDISABLE_MAIN_BOARD_CPLD3_FORMAT "/sys/bus/i2c/devices/16-0063/module_tx_disable_%d"

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
     * Ports {0, 73}
     */
    int p;
    AIM_BITMAP_CLR_ALL(bmap);

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
    char *path = NULL;

    VALIDATE(port);

    switch (port) {
    case 0 ... 24:
        path = MODULE_PRESENT_MAIN_BOARD_CPLD1_FORMAT;
        break;
    case 25 ... 49:
        path = MODULE_PRESENT_MAIN_BOARD_CPLD2_FORMAT;
        break;
    case 50 ... 73:
        path = MODULE_PRESENT_MAIN_BOARD_CPLD3_FORMAT;
        break;
    default:
        return ONLP_STATUS_E_INVALID;
    }

    if (onlp_file_read_int(&present, path, (port+1)) < 0) {
        syslog(LOG_ERR, "Unable to read present status from port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    return present;
}

int
onlp_sfpi_rx_los_bitmap_get(onlp_sfp_bitmap_t* dst)
{
    int i=0, val=0;
    char *path = NULL;
    /* Populate bitmap */
    for (i = 0; i < NUM_OF_SFP_PORT; i++) {
        val = 0;
        switch (i) {
        case 0 ... 24:
            path = MODULE_RXLOS_MAIN_BOARD_CPLD1_FORMAT;
            break;
        case 25 ... 49:
            path = MODULE_RXLOS_MAIN_BOARD_CPLD2_FORMAT;
            break;
        case 50 ... 73:
            path = MODULE_RXLOS_MAIN_BOARD_CPLD3_FORMAT;
            break;
        default:
            break;
        }
        if ((i >= 10) && (i <= 73)) {
            if (onlp_file_read_int(&val, path, i+1) < 0) {
                syslog(LOG_ERR, "Unable to read rx_loss status from port(%d)", i);
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
    /*
     * Read the SFP eeprom into data[]
     *
     * Return MISSING if SFP is missing.
     * Return OK if eeprom is read
     */
    int size = 0;

    VALIDATE(port);

    if (onlp_file_read(data, 256, &size, PORT_FORMAT, PORT_BUS_INDEX(port), "eeprom") != ONLP_STATUS_OK) {
        syslog(LOG_ERR, "Unable to read eeprom from port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    if (size != 256) {
        syslog(LOG_ERR, "Invalid file size(%d)", size);
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
        syslog(LOG_ERR, "Unable to open the eeprom device file of port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    if (fseek(fp, 256, SEEK_CUR) != 0) {
        fclose(fp);
        syslog(LOG_ERR, "Unable to set the file position indicator of port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    int ret = fread(data, 1, 256, fp);
    fclose(fp);
    if (ret != 256) {
        syslog(LOG_ERR, "Unable to read the module_eeprom device file of port(%d)", port);
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
    int rv = ONLP_STATUS_OK;
    char *path = NULL;
    int present = 0;
    int identifier = 0;
    int status_byte = 0;
    int support_ctrls = 0;

    VALIDATE(port);

    switch(control) {
    case ONLP_SFP_CONTROL_TX_DISABLE:
    case ONLP_SFP_CONTROL_TX_DISABLE_CHANNEL:
    {
        present = onlp_sfpi_is_present(port);
        if (present == 1) {
            if(port >= 0 && port <= 9) {
                if ((identifier = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_IDENTIFIER)) < 0) {
                    syslog(LOG_ERR, "Failed to read Identifier, unable to write tx_disable status to port(%d)", port);
                    return ONLP_STATUS_E_INTERNAL;
                }

                if (identifier == QSFP_DD_IDENTIFIER) {
                    /* Flat-memory CMIS modules do not implement page 01h/10h */
                    if ((status_byte = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_LOWER_OFFSET_STATUS)) < 0) {
                        syslog(LOG_ERR, "Failed to read Status byte, unable to write tx_disable status to port(%d)", port);
                        return ONLP_STATUS_E_INTERNAL;
                    }
                    if (status_byte & QSFP_DD_FLAT_MEM) {
                        return ONLP_STATUS_E_UNSUPPORTED;
                    }
                    if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADVERTISING)) < 0) {
                        syslog(LOG_ERR, "Failed to switch to Advertising Page on port(%d)", port);
                        goto restore;
                    }
                    if ((support_ctrls = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_P01H_OFFSET_CONTROL_1)) < 0) {
                        syslog(LOG_ERR, "Failed to read Support Control on port(%d)", port);
                        rv = support_ctrls;
                        goto restore;
                    }
                    if (support_ctrls & QSFP_DD_P01H_TX_DISABLE_SUPPORT) {
                        if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_BANK_SELECT, 0)) < 0) {
                            syslog(LOG_ERR, "Failed to set Bank 0, unable to write tx_disable status to port(%d)", port);
                            goto restore;
                        }
                        if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_LANE_CTRL)) < 0) {
                            syslog(LOG_ERR, "Failed to switch to Lane Control Page (Page 0x%02x) on port(%d)", 
                                        QSFP_DD_PAGE_LANE_CTRL, port);
                            goto restore;
                        }
                        if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_P10H_OFFSET_OUTPUT_DISABLE_TX, (value & 0xff))) < 0) {
                            syslog(LOG_ERR, "Failed to write tx_disable value(0x%02x) to Lane Control register on port(%d)", value, port);
                            goto restore;
                        }
                    } else {
                        rv = ONLP_STATUS_E_UNSUPPORTED;
                        goto restore;
                    }

                restore:
                    if ((onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_PAGE_SELECT,
                                                            QSFP_DD_PAGE_ADMIN_INFO)) < 0) {
                        syslog(LOG_ERR, "Failed to restore Page Select to Admin Info on port(%d)!", port);
                    }
                    if (rv < 0) {
                        rv = (rv == ONLP_STATUS_E_UNSUPPORTED) ? rv : ONLP_STATUS_E_INTERNAL;
                    } else {
                        rv = ONLP_STATUS_OK;
                    }
                } else { /* QSFP */
                    /* txdis valid bit(bit0-bit3), xxxx 1111 */
                    if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_TXDIS, (value & 0xf)) < 0) {
                        syslog(LOG_ERR, "Unable to write tx_disable status to port(%d)", port);
                        rv = ONLP_STATUS_E_INTERNAL;
                    } else {
                        rv = ONLP_STATUS_OK;
                    }
                }
            } else {
                switch (port) {
                case 10 ... 24:
                    path = MODULE_TXDISABLE_MAIN_BOARD_CPLD1_FORMAT;
                    break;
                case 25 ... 49:
                    path = MODULE_TXDISABLE_MAIN_BOARD_CPLD2_FORMAT;
                    break;
                case 50 ... 73:
                    path = MODULE_TXDISABLE_MAIN_BOARD_CPLD3_FORMAT;
                    break;
                default:
                    break;
                }
                if (onlp_file_write_int(value, path, (port+1)) < 0) {
                    syslog(LOG_ERR, "Unable to set tx_disable status to port(%d)", port);
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
    case ONLP_SFP_CONTROL_RESET: {
        VALIDATE_QSFP(port);

        switch (port) {
        case 0 ... 24:
            path = MODULE_RESET_MAIN_BOARD_CPLD1_FORMAT;
            break;
        case 25 ... 49:
            path = MODULE_RESET_MAIN_BOARD_CPLD1_FORMAT;
            break;
        case 50 ... 73:
            path = MODULE_RESET_MAIN_BOARD_CPLD1_FORMAT;
            break;
        default:
            break;
        }
        if (onlp_file_write_int(value, path, (port+1)) < 0) {
            syslog(LOG_ERR, "Unable to write reset status to port(%d)", port);
            rv = ONLP_STATUS_E_INTERNAL;
        }
        break;
    }
    case ONLP_SFP_CONTROL_LP_MODE:
    {
        VALIDATE_QSFP(port);

        switch (port) {
        case 0 ... 9:
            path = MODULE_LPMODE_MAIN_BOARD_CPLD1_FORMAT;
            break;
        default:
            break;
        }
        if (onlp_file_write_int(value, path, (port+1)) < 0) {
            syslog(LOG_ERR, "Unable to write LP mode status to port(%d)", port);
            rv = ONLP_STATUS_E_INTERNAL;
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
    int rv = ONLP_STATUS_OK;
    char *path = NULL;
    int present = 0;
    int identifier = 0;
    int status_byte = 0;
    int support_ctrls = 0;
    int tx_dis = 0;

    VALIDATE(port);

    switch(control) {
    case ONLP_SFP_CONTROL_RX_LOS:
    {
        VALIDATE_SFP(port);

        switch (port) {
        case 0 ... 24:
            path = MODULE_RXLOS_MAIN_BOARD_CPLD1_FORMAT;
            break;
        case 25 ... 49:
            path = MODULE_RXLOS_MAIN_BOARD_CPLD2_FORMAT;
            break;
        case 50 ... 73:
            path = MODULE_RXLOS_MAIN_BOARD_CPLD3_FORMAT;
            break;
        default:
            break;
        }
        if (onlp_file_read_int(value, path, (port+1)) < 0) {
            syslog(LOG_ERR, "Unable to read rx_loss status from port(%d)", port);
            rv = ONLP_STATUS_E_INTERNAL;
        }
        break;
    }
    case ONLP_SFP_CONTROL_TX_FAULT:
        rv = ONLP_STATUS_E_UNSUPPORTED;
        break;
    case ONLP_SFP_CONTROL_TX_DISABLE:
    case ONLP_SFP_CONTROL_TX_DISABLE_CHANNEL:
    {
        present = onlp_sfpi_is_present(port);
        if (present == 1) {
            if (port >= 0 && port <= 9) {
                if ((identifier = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_IDENTIFIER)) < 0) {
                    syslog(LOG_ERR, "Failed to read Identifier, unable to read tx_disable status from port(%d)", port);
                    return ONLP_STATUS_E_INTERNAL;
                }

                if (identifier == QSFP_DD_IDENTIFIER) {
                    /* Flat-memory CMIS modules do not implement page 01h/10h */
                    if ((status_byte = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_LOWER_OFFSET_STATUS)) < 0) {
                        syslog(LOG_ERR, "Failed to read Status byte, unable to read tx_disable status from port(%d)", port);
                        return ONLP_STATUS_E_INTERNAL;
                    }
                    if (status_byte & QSFP_DD_FLAT_MEM) {
                        return ONLP_STATUS_E_UNSUPPORTED;
                    }
                    if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADVERTISING)) < 0) {
                        syslog(LOG_ERR, "Failed to switch to Advertising Page on port(%d)", port);
                        goto restore;
                    }
                    if ((support_ctrls = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_P01H_OFFSET_CONTROL_1)) < 0) {
                        syslog(LOG_ERR, "Failed to read Support Control on port(%d)", port);
                        rv = support_ctrls;
                        goto restore;
                    }
                    if (support_ctrls & QSFP_DD_P01H_TX_DISABLE_SUPPORT) {
                        if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_BANK_SELECT, 0)) < 0) {
                            syslog(LOG_ERR, "Failed to set Bank 0, unable to read tx_disable status from port(%d)", port);
                            goto restore;
                        }
                        if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_LANE_CTRL)) < 0) {
                            syslog(LOG_ERR, "Failed to switch to Lane Control Page (Page 0x%02x) on port(%d)", 
                                        QSFP_DD_PAGE_LANE_CTRL, port);
                            goto restore;
                        }
                        if ((tx_dis = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_P10H_OFFSET_OUTPUT_DISABLE_TX)) < 0) {
                            syslog(LOG_ERR, "Failed to read TX_DISABLE value from Lane Control register on port(%d)", port);
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
                        syslog(LOG_ERR, "Failed to restore Page Select to Admin Info on port(%d)!", port);
                    }

                    if (rv < 0) {
                        rv = (rv == ONLP_STATUS_E_UNSUPPORTED) ? rv : ONLP_STATUS_E_INTERNAL;
                    } else {
                        *value = (tx_dis & 0xff);
                        rv = ONLP_STATUS_OK;
                    }
                } else { /* QSFP */
                    if ((tx_dis = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_TXDIS)) < 0) {
                        syslog(LOG_ERR, "Unable to read tx_disable status from port(%d)", port);
                        rv = ONLP_STATUS_E_INTERNAL;
                    } else {
                        *value = (tx_dis & 0xf);
                        rv = ONLP_STATUS_OK;
                    }
                }
            } else { /* SFP */
                switch (port) {
                case 10 ... 24:
                    path = MODULE_TXDISABLE_MAIN_BOARD_CPLD1_FORMAT;
                    break;
                case 25 ... 49:
                    path = MODULE_TXDISABLE_MAIN_BOARD_CPLD2_FORMAT;
                    break;
                case 50 ... 73:
                    path = MODULE_TXDISABLE_MAIN_BOARD_CPLD3_FORMAT;
                    break;
                default:
                    break;
                }
                if (onlp_file_read_int(value, path, (port+1)) < 0) {
                    syslog(LOG_ERR, "Unable to read tx_disabled status from port(%d)", port);
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
    case ONLP_SFP_CONTROL_RESET: {
        VALIDATE_QSFP(port);

        switch (port) {
        case 0 ... 24:
            path = MODULE_RESET_MAIN_BOARD_CPLD1_FORMAT;
            break;
        case 25 ... 49:
            path = MODULE_RESET_MAIN_BOARD_CPLD2_FORMAT;
            break;
        case 50 ... 73:
            path = MODULE_RESET_MAIN_BOARD_CPLD3_FORMAT;
            break;
        default:
            break;
        }
        if (onlp_file_read_int(value, path, (port+1)) < 0) {
            syslog(LOG_ERR, "Unable to read reset status from port(%d)", port);
            rv = ONLP_STATUS_E_INTERNAL;
        }
        break;
    }
    case ONLP_SFP_CONTROL_LP_MODE:
    {
        VALIDATE_QSFP(port);

        switch (port) {
        case 0 ... 9:
            path = MODULE_LPMODE_MAIN_BOARD_CPLD1_FORMAT;
            break;
        default:
            break;
        }

        if (onlp_file_read_int(value, path, (port+1)) < 0) {
            syslog(LOG_ERR, "Unable to read LP mode status from port(%d)", port);
            rv = ONLP_STATUS_E_INTERNAL;
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
