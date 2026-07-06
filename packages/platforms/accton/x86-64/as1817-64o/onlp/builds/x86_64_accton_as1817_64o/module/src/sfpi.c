/************************************************************
 * <bsn.cl fy=2014 v=onl>
 *
 *           Copyright 2014 Big Switch Networks, Inc.
 *           Copyright 2026 Accton Technology Corporation.
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
#include <unistd.h>
#include <syslog.h>
#include "x86_64_accton_as1817_64o_int.h"
#include "x86_64_accton_as1817_64o_log.h"
#include "platform_lib.h"

#define NUM_OF_PORT (CHASSIS_OSFP_COUNT + CHASSIS_SFP_COUNT)

#define MODULE_PRESENT_FORMAT \
    "/sys/devices/platform/as1817_64o_fpga/module_present_%d"
#define MODULE_RESET_FORMAT \
    "/sys/devices/platform/as1817_64o_fpga/module_reset_%d"
#define MODULE_LPMODE_FORMAT \
    "/sys/devices/platform/as1817_64o_fpga/module_lp_mode_%d"
#define MODULE_TXDISABLE_FORMAT \
    "/sys/devices/platform/as1817_64o_fpga/module_tx_disable_%d"
#define MODULE_TXFAULT_FORMAT \
    "/sys/devices/platform/as1817_64o_fpga/module_tx_fault_%d"
#define MODULE_RXLOS_FORMAT \
    "/sys/devices/platform/as1817_64o_fpga/module_rx_los_%d"

/*
 * EEPROM access via optoe sysfs.
 * i2c bus layout: bus0=I801, bus1=iSMT, bus2~67=ocores (front-panel port 1~66)
 * optoe3 for OSFP ports, optoe2 for SFP28 ports.
 *
 * Port numbering: ONLP uses 0-based SFP port indices (0..65).
 *   - OSFP:  ONLP index 0..63  (front-panel port 1..64)
 *   - SFP28: ONLP index 64..65 (front-panel port 65..66)
 * Kernel sysfs and i2c bus use 1-based front-panel numbers, so:
 *   - sysfs module_*_%d attribute number = PORT_NUM(port)
 *   - i2c bus number                     = PORT_NUM(port) + 1
 */
#define PORT_NUM(port) ((port) + 1)
#define PORT_BUS(port) (PORT_NUM(port) + 1)
#define PORT_EEPROM_FORMAT "/sys/bus/i2c/devices/%d-0050/eeprom"

#define OSFP_PORT_MIN 0
#define OSFP_PORT_MAX 63
#define SFP_PORT_MIN  64
#define SFP_PORT_MAX  65

/* SFP EEPROM Address */
#define PORT_EEPROM_DEVADDR 0x50

/* CMIS Register Addresses (lower page) */
#define CMIS_REG_IDENTIFIER          0x00  /* Module type identifier */
#define CMIS_REG_STATUS              0x02  /* Module status (bit7=flat_mem) */
#define CMIS_REG_BANK_SELECT         0x7E  /* Bank select for upper pages */
#define CMIS_REG_PAGE_SELECT         0x7F  /* Page select for upper memory */

/* CMIS Register Addresses (upper page) */
#define CMIS_P01H_REG_CONTROL_1      0x9B  /* Page 01h: supported controls */
#define CMIS_P10H_REG_TX_DISABLE     0x82  /* Page 10h: per-lane TX disable */

/* CMIS Pages */
#define CMIS_PAGE_ADMIN_INFO         0x00  /* Basic module info */
#define CMIS_PAGE_ADVERTISING        0x01  /* Module capabilities */
#define CMIS_PAGE_LANE_CTRL          0x10  /* Per-lane control */

/* CMIS Bit Masks */
#define CMIS_STATUS_FLAT_MEM         0x80  /* Status reg bit7: 1=flat memory */
#define CMIS_P01H_TX_DISABLE_SUPPORT 0x02  /* Control_1 bit1: TX disable supported */


/* SFP IDENTIFIER */
#define QSFP_28_IDENTIFIER   0x11
#define QSFP_PLUS_IDENTIFIER 0x0d
#define QSFP_DD_IDENTIFIER   0x18
#define OSFP_IDENTIFIER      0x19

#define VALIDATE_PORT(port) \
    do { if ((port) < 0 || (port) >= NUM_OF_PORT) return ONLP_STATUS_E_INVALID; } while(0)

/************************************************************
 *
 * SFPI Entry Points
 *
 ***********************************************************/

int onlp_sfpi_init(void)
{
    return ONLP_STATUS_OK;
}

int onlp_sfpi_bitmap_get(onlp_sfp_bitmap_t *bmap)
{
    int p;
    for (p = 0; p < NUM_OF_PORT; p++)
        AIM_BITMAP_SET(bmap, p);
    return ONLP_STATUS_OK;
}

int onlp_sfpi_is_present(int port)
{
    int present;

    VALIDATE_PORT(port);

    if (onlp_file_read_int(&present, MODULE_PRESENT_FORMAT, PORT_NUM(port)) < 0) {
        syslog(LOG_ERR, "Unable to read present status from port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    return present;
}

int onlp_sfpi_presence_bitmap_get(onlp_sfp_bitmap_t *dst)
{
    int i;

    for (i = 0; i < NUM_OF_PORT; i++) {
        int present = onlp_sfpi_is_present(i);
        AIM_BITMAP_MOD(dst, i, (present == 1) ? 1 : 0);
    }

    return ONLP_STATUS_OK;
}

int onlp_sfpi_rx_los_bitmap_get(onlp_sfp_bitmap_t *dst)
{
    int i;
    int rx_los;

    AIM_BITMAP_CLR_ALL(dst);

    /* RX_LOS only applies to SFP28 ports */
    for (i = SFP_PORT_MIN; i <= SFP_PORT_MAX; i++) {
        if (onlp_file_read_int(&rx_los, MODULE_RXLOS_FORMAT, PORT_NUM(i)) < 0) {
            syslog(LOG_ERR, "Unable to read rx_los status from port(%d)", i);
            return ONLP_STATUS_E_INTERNAL;
        }
        AIM_BITMAP_MOD(dst, i, (rx_los == 1) ? 1 : 0);
    }

    return ONLP_STATUS_OK;
}

int onlp_sfpi_eeprom_read(int port, uint8_t data[256])
{
    int size = 0;

    VALIDATE_PORT(port);

    memset(data, 0, 256);

    if (onlp_file_read(data, 256, &size, PORT_EEPROM_FORMAT, PORT_BUS(port)) != ONLP_STATUS_OK) {
        syslog(LOG_ERR, "Unable to read eeprom from port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    return ONLP_STATUS_OK;
}

int onlp_sfpi_dom_read(int port, uint8_t data[256])
{
    FILE* fp;
    char file[64] = {0};

    VALIDATE_PORT(port);
    sprintf(file, PORT_EEPROM_FORMAT, PORT_BUS(port));
    fp = fopen(file, "r");
    if (fp == NULL) {
        syslog(LOG_ERR, "Unable to open the eeprom device file of port(%d)",
                  port);
        return ONLP_STATUS_E_INTERNAL;
    }

    if (fseek(fp, 256, SEEK_CUR) != 0) {
        fclose(fp);
        syslog(LOG_ERR, "Unable to set the file position indicator of port(%d)",
                  port);
        return ONLP_STATUS_E_INTERNAL;
    }

    int ret = fread(data, 1, 256, fp);
    fclose(fp);
    if (ret != 256) {
        syslog(LOG_ERR, "Unable to read the module_eeprom device file of port(%d)",
                  port);
        return ONLP_STATUS_E_INTERNAL;
    }

    return ONLP_STATUS_OK;
}

int onlp_sfpi_dev_readb(int port, uint8_t devaddr, uint8_t addr)
{
    VALIDATE_PORT(port);
    return onlp_i2c_readb(PORT_BUS(port), devaddr, addr, ONLP_I2C_F_FORCE);
}

int onlp_sfpi_dev_writeb(int port, uint8_t devaddr, uint8_t addr,
             uint8_t value)
{
    VALIDATE_PORT(port);
    return onlp_i2c_writeb(PORT_BUS(port), devaddr, addr, value, ONLP_I2C_F_FORCE);
}

int onlp_sfpi_dev_readw(int port, uint8_t devaddr, uint8_t addr)
{
    VALIDATE_PORT(port);
    return onlp_i2c_readw(PORT_BUS(port), devaddr, addr, ONLP_I2C_F_FORCE);
}

int onlp_sfpi_dev_writew(int port, uint8_t devaddr, uint8_t addr,
             uint16_t value)
{
    VALIDATE_PORT(port);
    return onlp_i2c_writew(PORT_BUS(port), devaddr, addr, value, ONLP_I2C_F_FORCE);
}

int onlp_sfpi_control_set(int port, onlp_sfp_control_t control, int value)
{
    int rv;
    int present = 0;
    int identifier = 0;
    int status_byte = 0;
    int support_ctrls = 0;

    VALIDATE_PORT(port);

    switch (control) {
    case ONLP_SFP_CONTROL_TX_DISABLE:
    case ONLP_SFP_CONTROL_TX_DISABLE_CHANNEL:
        present = onlp_sfpi_is_present(port);

        if (present == 1) {
            if (port >= OSFP_PORT_MIN && port < SFP_PORT_MIN) { /* OSFP */
                if ((identifier = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, CMIS_REG_IDENTIFIER)) < 0) {
                    syslog(LOG_ERR, "Failed to read xcvr(%d) Identifier", port);
                    rv = ONLP_STATUS_E_INTERNAL;
                    goto exit;
                }

                /* OSFP and QSFP-DD both use the CMIS protocol */
                if (identifier == OSFP_IDENTIFIER || identifier == QSFP_DD_IDENTIFIER) {
                    /* Flat-memory CMIS modules do not implement page 01h/10h */
                    if ((status_byte = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, CMIS_REG_STATUS)) < 0) {
                        syslog(LOG_ERR, "Failed to read xcvr(%d) Status", port);
                        rv = ONLP_STATUS_E_INTERNAL;
                        goto exit;
                    }
                    if (status_byte & CMIS_STATUS_FLAT_MEM) {
                        syslog(LOG_ERR, "Xcvr(%d) is flat mem not support get/set tx disable", port);
                        return ONLP_STATUS_E_UNSUPPORTED;
                    }
                    if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, CMIS_REG_BANK_SELECT, 0)) < 0) {
                        syslog(LOG_ERR, "Failed to set xcvr(%d) Bank 0", port);
                        rv = ONLP_STATUS_E_INTERNAL;
                        goto exit;
                    }
                    if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, CMIS_REG_PAGE_SELECT, CMIS_PAGE_ADVERTISING)) < 0) {
                        syslog(LOG_ERR, "Failed to switch to xcvr(%d) Advertising Page", port);
                        goto restore;
                    }
                    if ((support_ctrls = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, CMIS_P01H_REG_CONTROL_1)) < 0) {
                        syslog(LOG_ERR, "Failed to read xcvr(%d) Support Control", port);
                        rv = support_ctrls;
                        goto restore;
                    }
                    if (support_ctrls & CMIS_P01H_TX_DISABLE_SUPPORT) {
                        if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, CMIS_REG_BANK_SELECT, 0)) < 0) {
                            syslog(LOG_ERR, "Failed to set xcvr(%d) Bank 0", port);
                            goto restore;
                        }
                        if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, CMIS_REG_PAGE_SELECT, CMIS_PAGE_LANE_CTRL)) < 0) {
                            syslog(LOG_ERR, "Failed to switch xcvr(%d) Lane Control Page (Page 0x%02x)",
                                        port, CMIS_PAGE_LANE_CTRL);
                            goto restore;
                        }
                        if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, CMIS_P10H_REG_TX_DISABLE, (value & 0xff))) < 0) {
                            syslog(LOG_ERR, "Failed to write xcvr(%d) tx disable(0x%02x)", port, value);
                            goto restore;
                        }
                    } else {
                        syslog(LOG_ERR, "Xcvr(%d) not support get/set tx disable", port);
                        rv = ONLP_STATUS_E_UNSUPPORTED;
                        goto restore;
                    }

                restore:
                    if ((onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, CMIS_REG_PAGE_SELECT,
                                                            CMIS_PAGE_ADMIN_INFO)) < 0) {
                        syslog(LOG_ERR, "Failed to restore xcvr(%d) Page Select to Admin Info!", port);
                    }
                exit:
                    if (rv < 0) {
                        syslog(LOG_ERR, "Unable to set port(%d) tx disable", port);
                        rv = (rv == ONLP_STATUS_E_UNSUPPORTED) ? rv : ONLP_STATUS_E_INTERNAL;
                    } 
                } else {
                    syslog(LOG_ERR, "Unable to recognize xcvr(%d) identifier", port);
                    rv = ONLP_STATUS_E_UNSUPPORTED;
                }
            } else { /* SFP */
                if (onlp_file_write_int(value, MODULE_TXDISABLE_FORMAT, PORT_NUM(port)) < 0) {
                    syslog(LOG_ERR, "Unable to set port(%d) tx disable", port);
                    rv = ONLP_STATUS_E_INTERNAL;
                } else {
                    rv = ONLP_STATUS_OK;
                }
            }
        } else {
            rv = ONLP_STATUS_E_MISSING;
        } 
        break;

    case ONLP_SFP_CONTROL_RESET:
    case ONLP_SFP_CONTROL_RESET_STATE:
        if (port >= SFP_PORT_MIN) {
            rv = ONLP_STATUS_E_UNSUPPORTED;
            break;
        }
        /* Latching reset: value=1 asserts reset, value=0 deasserts it. */
        if (onlp_file_write_int(value, MODULE_RESET_FORMAT, PORT_NUM(port)) < 0) {
            syslog(LOG_ERR, "Unable to set reset to port(%d)", port);
            rv = ONLP_STATUS_E_INTERNAL;
        } else {
            rv = ONLP_STATUS_OK;
        }
        break;

    case ONLP_SFP_CONTROL_LP_MODE:
        if (port >= SFP_PORT_MIN) {
            rv = ONLP_STATUS_E_UNSUPPORTED;
            break;
        }
        if (onlp_file_write_int(value, MODULE_LPMODE_FORMAT, PORT_NUM(port)) < 0) {
            syslog(LOG_ERR, "Unable to set lp_mode to port(%d)", port);
            rv = ONLP_STATUS_E_INTERNAL;
        } else {
            rv = ONLP_STATUS_OK;
        }
        break;

    default:
        rv = ONLP_STATUS_E_UNSUPPORTED;
        break;
    }

    return rv;
}

int onlp_sfpi_control_get(int port, onlp_sfp_control_t control, int *value)
{
    int rv;
    int present = 0;
    int identifier = 0;
    int status_byte = 0;
    int support_ctrls = 0;

    VALIDATE_PORT(port);

    switch (control) {
    case ONLP_SFP_CONTROL_RX_LOS:
        if (port < SFP_PORT_MIN) {
            rv = ONLP_STATUS_E_UNSUPPORTED;
            break;
        }
        if (onlp_file_read_int(value, MODULE_RXLOS_FORMAT, PORT_NUM(port)) < 0) {
            syslog(LOG_ERR, "Unable to read rx_los from port(%d)", port);
            rv = ONLP_STATUS_E_INTERNAL;
        } else {
            rv = ONLP_STATUS_OK;
        }
        break;

    case ONLP_SFP_CONTROL_TX_FAULT:
        if (port < SFP_PORT_MIN) {
            rv = ONLP_STATUS_E_UNSUPPORTED;
            break;
        }
        if (onlp_file_read_int(value, MODULE_TXFAULT_FORMAT, PORT_NUM(port)) < 0) {
            syslog(LOG_ERR, "Unable to read tx_fault from port(%d)", port);
            rv = ONLP_STATUS_E_INTERNAL;
        } else {
            rv = ONLP_STATUS_OK;
        }
        break;

    case ONLP_SFP_CONTROL_TX_DISABLE:
    case ONLP_SFP_CONTROL_TX_DISABLE_CHANNEL:
        present = onlp_sfpi_is_present(port);

        if (present == 1) {
            if (port >= OSFP_PORT_MIN && port < SFP_PORT_MIN) { /* OSFP */
                if ((identifier = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, CMIS_REG_IDENTIFIER)) < 0) {
                    syslog(LOG_ERR, "Failed to read xcvr(%d) Identifier", port);
                    rv = ONLP_STATUS_E_INTERNAL;
                    goto exit;
                }

                /* OSFP and QSFP-DD both use the CMIS protocol */
                if (identifier == OSFP_IDENTIFIER || identifier == QSFP_DD_IDENTIFIER) {
                    /* Flat-memory CMIS modules do not implement page 01h/10h */
                    if ((status_byte = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, CMIS_REG_STATUS)) < 0) {
                        syslog(LOG_ERR, "Failed to read xcvr(%d) Status", port);
                        rv = ONLP_STATUS_E_INTERNAL;
                        goto exit;
                    }
                    if (status_byte & CMIS_STATUS_FLAT_MEM) {
                        syslog(LOG_ERR, "Xcvr(%d) is flat mem not support get/set tx disable", port);
                        return ONLP_STATUS_E_UNSUPPORTED;
                    }
                    if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, CMIS_REG_BANK_SELECT, 0)) < 0) {
                        syslog(LOG_ERR, "Failed to set xcvr(%d) Bank 0", port);
                        rv = ONLP_STATUS_E_INTERNAL;
                        goto exit;
                    }
                    if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, CMIS_REG_PAGE_SELECT, CMIS_PAGE_ADVERTISING)) < 0) {
                        syslog(LOG_ERR, "Failed to switch to xcvr(%d) Advertising Page", port);
                        goto restore;
                    }
                    if ((support_ctrls = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, CMIS_P01H_REG_CONTROL_1)) < 0) {
                        syslog(LOG_ERR, "Failed to read xcvr(%d) Support Control", port);
                        rv = support_ctrls;
                        goto restore;
                    }
                    if (support_ctrls & CMIS_P01H_TX_DISABLE_SUPPORT) {
                        if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, CMIS_REG_BANK_SELECT, 0)) < 0) {
                            syslog(LOG_ERR, "Failed to set xcvr(%d) Bank 0", port);
                            goto restore;
                        }
                        if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, CMIS_REG_PAGE_SELECT, CMIS_PAGE_LANE_CTRL)) < 0) {
                            syslog(LOG_ERR, "Failed to switch xcvr(%d) Lane Control Page (Page 0x%02x)",
                                        port, CMIS_PAGE_LANE_CTRL);
                            goto restore;
                        }
                        if ((rv = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, CMIS_P10H_REG_TX_DISABLE)) < 0) {
                            syslog(LOG_ERR, "Failed to read xcvr(%d) TX_DISABLE from Lane Control register", port);
                            goto restore;
                        } 
                        *value = rv & 0xff;
                        rv = ONLP_STATUS_OK;
                    } else {
                        syslog(LOG_ERR, "Xcvr(%d) not support get/set tx disable", port);
                        rv = ONLP_STATUS_E_UNSUPPORTED;
                        goto restore;
                    }

                restore:
                    if ((onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, CMIS_REG_PAGE_SELECT,
                                                            CMIS_PAGE_ADMIN_INFO)) < 0) {
                        syslog(LOG_ERR, "Failed to restore xcvr(%d) Page Select to Admin Info!", port);
                    }
                exit:
                    if (rv < 0) {
                        syslog(LOG_ERR, "Unable to read port(%d) tx disable status", port);
                        rv = (rv == ONLP_STATUS_E_UNSUPPORTED) ? rv : ONLP_STATUS_E_INTERNAL;
                    } 
                } else {
                    syslog(LOG_ERR, "Unable to recognize xcvr(%d) identifier", port);
                    rv = ONLP_STATUS_E_UNSUPPORTED;
                }
            } else { /* SFP */
                if (onlp_file_read_int(value, MODULE_TXDISABLE_FORMAT, PORT_NUM(port)) < 0) {
                    syslog(LOG_ERR, "Unable to read port(%d) tx disable status", port);
                    rv = ONLP_STATUS_E_INTERNAL;
                } else {
                    rv = ONLP_STATUS_OK;
                }
            }
        } else {
            rv = ONLP_STATUS_E_MISSING;
        } 
        break;

    case ONLP_SFP_CONTROL_RESET_STATE:
        if (port >= SFP_PORT_MIN) {
            rv = ONLP_STATUS_E_UNSUPPORTED;
            break;
        }
        if (onlp_file_read_int(value, MODULE_RESET_FORMAT, PORT_NUM(port)) < 0) {
            syslog(LOG_ERR, "Unable to read reset from port(%d)", port);
            rv = ONLP_STATUS_E_INTERNAL;
        } else {
            rv = ONLP_STATUS_OK;
        }
        break;

    case ONLP_SFP_CONTROL_LP_MODE:
        if (port >= SFP_PORT_MIN) {
            rv = ONLP_STATUS_E_UNSUPPORTED;
            break;
        }
        if (onlp_file_read_int(value, MODULE_LPMODE_FORMAT, PORT_NUM(port)) < 0) {
            syslog(LOG_ERR, "Unable to read lp_mode from port(%d)", port);
            rv = ONLP_STATUS_E_INTERNAL;
        } else {
            rv = ONLP_STATUS_OK;
        }
        break;

    default:
        rv = ONLP_STATUS_E_UNSUPPORTED;
    }

    return rv;
}

int onlp_sfpi_denit(void)
{
    return ONLP_STATUS_OK;
}
