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
#include "x86_64_accton_as7515_24x_int.h"
#include "x86_64_accton_as7515_24x_log.h"
#include "platform_lib.h"
#include <syslog.h>
#include "log_ctrl.h"

#define VALIDATE(_port) \
    do { \
        if (_port < 0 || _port > 23) { \
            return ONLP_STATUS_E_INVALID; \
        } \
    } while(0)

#define VALIDATE_SFP(_port) \
    do { \
        if (_port < 4 || _port > 23) \
            return ONLP_STATUS_E_UNSUPPORTED; \
    } while(0)

#define VALIDATE_QSFP(_port) \
    do { \
        if (_port < 0 || _port > 3 ) \
            return ONLP_STATUS_E_UNSUPPORTED; \
    } while(0)

#define MODULE_EEPROM_FORMAT       "/sys/bus/i2c/devices/%d-0050/eeprom"
#define MODULE_PRESENT_FORMAT      "/sys/devices/platform/as7515_24x_sfp/module_present_%d"
#define MODULE_RXLOS_FORMAT        "/sys/devices/platform/as7515_24x_sfp/module_rx_los_%d"
#define MODULE_TXFAULT_FORMAT      "/sys/devices/platform/as7515_24x_sfp/module_tx_fault_%d"
#define MODULE_TXDISABLE_FORMAT    "/sys/devices/platform/as7515_24x_sfp/module_tx_disable_%d"
#define MODULE_RESET_FORMAT        "/sys/devices/platform/as7515_24x_sfp/module_reset_%d"
#define MODULE_LPMODE_FORMAT       "/sys/devices/platform/as7515_24x_sfp/module_lpmode_%d"
#define MODULE_PRESENT_ALL_ATTR    "/sys/devices/platform/as7515_24x_sfp/module_present_all"
#define MODULE_RXLOS_ALL_ATTR      "/sys/devices/platform/as7515_24x_sfp/module_rx_los_all"
/* QSFP device address of eeprom */
#define PORT_EEPROM_DEVADDR             0x50
/* QSFP tx disable offset */
#define QSFP_EEPROM_OFFSET_TXDIS        0x56

#define NUM_OF_SFP_PORT 24
static const int port_bus_index[NUM_OF_SFP_PORT] = {
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
    27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38
};

#define PORT_BUS_INDEX(port) (port_bus_index[port])

#define MAX_PORT (NUM_OF_SFP_PORT - 1)

static struct sfp_log_mgmt log_mgmt[MAX_PORT+1] = {
    [0 ... MAX_PORT] = {
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
     * Ports {0, 23}
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

    if (onlp_file_read_int(&present, MODULE_PRESENT_FORMAT, port+1) < 0) {
        if (log_mgmt[port].present_rec != ONLP_STATUS_E_INTERNAL) {
            reset_log_ctrl(log_mgmt[port].log_ctrl, SFP_LOG_REASON_COUNT);
        }
        log_mgmt[port].present_rec = ONLP_STATUS_E_INTERNAL;

        syslog_ctrl(log_mgmt[port].log_ctrl, SFP_SYSFS_READ_FAIL,
                    "Unable to read present status from port(%d)", port+1);
        return ONLP_STATUS_E_INTERNAL;
    }

    if (present == 1 && present != log_mgmt[port].present_rec) {
        reset_log_ctrl(log_mgmt[port].log_ctrl, SFP_LOG_REASON_COUNT);
    }
    log_mgmt[port].present_rec = present;

    return present;
}

int
onlp_sfpi_presence_bitmap_get(onlp_sfp_bitmap_t* dst)
{
    int i = 0;
    uint32_t bytes[3] = {0};
    FILE* fp = NULL;
    int count = 0;

    /* Read present status of each port */
    fp = fopen(MODULE_PRESENT_ALL_ATTR, "r");
    if(fp == NULL) {
        syslog(LOG_ERR, "Unable to open the module_present_all device file of CPLD.");
        return ONLP_STATUS_E_INTERNAL;
    }

    count = fscanf(fp, "%x %x %x", bytes+0, bytes+1, bytes+2);
    fclose(fp);

    if(count != 3) {
        /* Likely a CPLD read timeout. */
        syslog(LOG_ERR, "Unable to read all fields the module_present_all device file of CPLD.");
        return ONLP_STATUS_E_INTERNAL;
    }

    /* Convert to 64 bit integer in port order */
    AIM_BITMAP_CLR_ALL(dst);
    uint32_t presence_all = 0 ;
    for (i = AIM_ARRAYSIZE(bytes)-1; i >= 0; i--) {
        presence_all <<= 8;
        presence_all |= bytes[i];
    }

    /* Populate bitmap */
    for (i = 0; presence_all; i++) {
        AIM_BITMAP_MOD(dst, i, (presence_all & 1));
        presence_all >>= 1;
    }

    return ONLP_STATUS_OK;
}

int
onlp_sfpi_rx_los_bitmap_get(onlp_sfp_bitmap_t* dst)
{
    int i = 0;
    uint32_t bytes[3] = {0};
    FILE* fp = NULL;
    int count = 0;

    /* Read present status of each port */
    fp = fopen(MODULE_PRESENT_ALL_ATTR, "r");
    if(fp == NULL) {
        syslog(LOG_ERR, "Unable to open the module_present_all device file of CPLD.");
        return ONLP_STATUS_E_INTERNAL;
    }

    count = fscanf(fp, "%x %x %x", bytes+0, bytes+1, bytes+2);
    fclose(fp);

    if(count != 3) {
        /* Likely a CPLD read timeout. */
        syslog(LOG_ERR, "Unable to read all fields the module_present_all device file of CPLD.");
        return ONLP_STATUS_E_INTERNAL;
    }

    /* Convert to 64 bit integer in port order */
    AIM_BITMAP_CLR_ALL(dst);
    uint32_t presence_all = 0 ;
    for (i = AIM_ARRAYSIZE(bytes)-1; i >= 0; i--) {
        presence_all <<= 8;
        presence_all |= bytes[i];
    }

    /* Read rx los status of each port */
    fp = fopen(MODULE_RXLOS_ALL_ATTR, "r");
    if(fp == NULL) {
        syslog(LOG_ERR, "Unable to open the module_rx_los_all device file of CPLD.");
        return ONLP_STATUS_E_INTERNAL;
    }

    count = fscanf(fp, "%x %x %x", bytes+0, bytes+1, bytes+2);
    fclose(fp);

    if(count != 3) {
        /* Likely a CPLD read timeout. */
        syslog(LOG_ERR, "Unable to read all fields the module_rx_los_all device file of CPLD.");
        return ONLP_STATUS_E_INTERNAL;
    }

    /* Convert to 64 bit integer in port order */
    AIM_BITMAP_CLR_ALL(dst);
    uint32_t rx_los_all = 0 ;
    for (i = AIM_ARRAYSIZE(bytes)-1; i >= 0; i--) {
        rx_los_all <<= 8;
        rx_los_all |= bytes[i];
    }

    rx_los_all = (rx_los_all & presence_all);

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

    if(onlp_file_read(data, 256, &size, MODULE_EEPROM_FORMAT, PORT_BUS_INDEX(port)) != ONLP_STATUS_OK) {
        syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_READ_FAIL,
                    "Unable to read eeprom from port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    if (size != 256) {
        syslog_ctrl(log_mgmt[port].log_ctrl, SFP_INVALID_DATA_SIZE,
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
        syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_READ_FAIL,
                    "Unable to open the eeprom device file of port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    if (fseek(fp, 256, SEEK_CUR) != 0) {
        fclose(fp);
        syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_READ_FAIL,
                    "Unable to set the file position indicator of port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    int ret = fread(data, 1, 256, fp);
    fclose(fp);
    if (ret != 256) {
        syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_READ_FAIL,
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

    switch(control) {
    case ONLP_SFP_CONTROL_TX_DISABLE:
    case ONLP_SFP_CONTROL_TX_DISABLE_CHANNEL: {
        VALIDATE(port);
        present = onlp_sfpi_is_present(port);

        if (present == 1) {
            if (port >= 0 && port < 4) {
                    /* txdis valid bit(bit0-bit3), xxxx 1111 */
                    if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_TXDIS, (value & 0xf)) < 0) {
                        syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_WRITE_TARGET_BYTE_FAIL,
                                    "Unable to write tx_disable status to port(%d)", port);
                        return ONLP_STATUS_E_INTERNAL;
                    }
                    return ONLP_STATUS_OK;
            } else {
                if (onlp_file_write_int(value, MODULE_TXDISABLE_FORMAT, port+1) < 0) {
                    syslog_ctrl(log_mgmt[port].log_ctrl, SFP_SYSFS_WRITE_FAIL,
                                "Unable to write tx_disable status to port(%d)", port);
                    return ONLP_STATUS_E_INTERNAL;
                }
                return ONLP_STATUS_OK;
            }
        } else {
            return ONLP_STATUS_E_INTERNAL;
        }
    }
    case ONLP_SFP_CONTROL_RESET_STATE: {
        VALIDATE_QSFP(port);

        if (onlp_file_write_int(value, MODULE_RESET_FORMAT, port+1) < 0) {
            syslog_ctrl(log_mgmt[port].log_ctrl, SFP_SYSFS_WRITE_FAIL,
                        "Unable to write reset status to port(%d)", port);
            return ONLP_STATUS_E_INTERNAL;
        }

        return ONLP_STATUS_OK;
    }
    case ONLP_SFP_CONTROL_LP_MODE: {
        VALIDATE_QSFP(port);

        if (onlp_file_write_int(value, MODULE_LPMODE_FORMAT, port+1) < 0) {
            syslog_ctrl(log_mgmt[port].log_ctrl, SFP_SYSFS_WRITE_FAIL,
                        "Unable to write lpmode status to port(%d)", port);
            return ONLP_STATUS_E_INTERNAL;
        }

        return ONLP_STATUS_OK;
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
    int tx_dis = 0;

    *value = 0;

    switch(control) {
    case ONLP_SFP_CONTROL_RX_LOS: {
        VALIDATE_SFP(port);

        if (onlp_file_read_int(value, MODULE_RXLOS_FORMAT, port+1) < 0) {
            syslog_ctrl(log_mgmt[port].log_ctrl, SFP_SYSFS_READ_FAIL,
                        "Unable to read rx_loss status from port(%d)", port);
            return ONLP_STATUS_E_INTERNAL;
        }

        return ONLP_STATUS_OK;
    }

    case ONLP_SFP_CONTROL_TX_FAULT: {
        VALIDATE_SFP(port);

        if (onlp_file_read_int(value, MODULE_TXFAULT_FORMAT, port+1) < 0) {
            syslog_ctrl(log_mgmt[port].log_ctrl, SFP_SYSFS_READ_FAIL,
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
            if (port >= 0 && port < 4) {
                if ((tx_dis = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_TXDIS)) < 0) {
                    syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_READ_TARGET_BYTE_FAIL,
                                "Unable to read tx_disable status from port(%d)", port);
                    return ONLP_STATUS_E_INTERNAL;
                }
                *value = (tx_dis & 0xf);
                return ONLP_STATUS_OK;
            } else {
                if (onlp_file_read_int(value, MODULE_TXDISABLE_FORMAT, port+1) < 0) {
                    syslog_ctrl(log_mgmt[port].log_ctrl, SFP_SYSFS_READ_FAIL,
                                "Unable to read tx_disabled status from port(%d)", port);
                    return ONLP_STATUS_E_INTERNAL;
                }
                return ONLP_STATUS_OK;
            }
        } else {
            return ONLP_STATUS_E_INTERNAL;
        }
    }
    case ONLP_SFP_CONTROL_RESET_STATE: {
        VALIDATE_QSFP(port);

        if (onlp_file_read_int(value, MODULE_RESET_FORMAT, port+1) < 0) {
            syslog_ctrl(log_mgmt[port].log_ctrl, SFP_SYSFS_READ_FAIL,
                        "Unable to read reset status from port(%d)", port);
            return ONLP_STATUS_E_INTERNAL;
        }

        return ONLP_STATUS_OK;
    }
    case ONLP_SFP_CONTROL_LP_MODE: {
        VALIDATE_QSFP(port);

        if (onlp_file_read_int(value, MODULE_LPMODE_FORMAT, port+1) < 0) {
            syslog_ctrl(log_mgmt[port].log_ctrl, SFP_SYSFS_READ_FAIL,
                        "Unable to read lpmode status from port(%d)", port);
            return ONLP_STATUS_E_INTERNAL;
        }

        return ONLP_STATUS_OK;
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
