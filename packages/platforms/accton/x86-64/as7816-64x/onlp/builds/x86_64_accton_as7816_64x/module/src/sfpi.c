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
#include "log_ctrl.h"

#define QSFP_PORT_MIN 0
#define QSFP_PORT_MAX 63
#define MIN_PORT QSFP_PORT_MIN
#define MAX_PORT QSFP_PORT_MAX
#define VALIDATE_QSFP(_port) \
    do { \
        if (_port < QSFP_PORT_MIN || _port > QSFP_PORT_MAX ) \
            return ONLP_STATUS_E_UNSUPPORTED; \
    } while(0)

#define NUM_OF_SFP_PORT 		64
static const int port_bus_index[NUM_OF_SFP_PORT] = {
37, 38, 39, 40, 42, 41, 44, 43,
33, 34, 35, 36, 45, 46, 47, 48,
49, 50, 51, 52, 61, 62, 63, 64,
53, 54, 55, 56, 57, 58, 59, 60,
69, 70, 71, 72, 77, 78, 79, 80,
65, 66, 67, 68, 73, 74, 75, 76,
85, 86, 87, 88, 31, 32, 29, 30,
81, 82, 83, 84, 25, 26, 27, 28
};

#define PORT_BUS_INDEX(port) (port_bus_index[port])
#define PORT_FORMAT	 "/sys/bus/i2c/devices/%d-0050/%s"

#define MODULE_PRESENT_FORMAT		"/sys/bus/i2c/devices/19-0060/module_present_%d"
#define MODULE_PRESENT_ALL_ATTR		"/sys/bus/i2c/devices/19-0060/module_present_all"
#define MODULE_RESET_FORMAT         "/sys/bus/i2c/devices/19-0060/module_reset_%d"

/* QSFP device address of eeprom */
#define PORT_EEPROM_DEVADDR             0x50

/*QSFP eeprom offset*/
#define QSFP_EEPROM_OFFSET_TXDIS        0x56
#define QSFP_EEPROM_OFFSET_LPMODE       0x5D

/*QSFP Specific*/
#define QSFP_LPMODE 0x3

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
     * Ports {0, 64}
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

	if (onlp_file_read_int(&present, MODULE_PRESENT_FORMAT, (port+1)) < 0) {
        if (log_mgmt[port].present_rec != ONLP_STATUS_E_INTERNAL) {
            reset_log_ctrl(log_mgmt[port].log_ctrl, SFP_LOG_REASON_COUNT);
        }
        log_mgmt[port].present_rec = ONLP_STATUS_E_INTERNAL;

        syslog_ctrl(log_mgmt[port].log_ctrl, SFP_SYSFS_READ_FAIL,
                    "Unable to read present status from port(%d)", port);
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
    uint32_t bytes[8];
    FILE* fp;

    fp = fopen(MODULE_PRESENT_ALL_ATTR, "r");

    if(fp == NULL) {
        syslog(LOG_ERR, "Unable to open the sfp_is_present_all device file.");
        return ONLP_STATUS_E_INTERNAL;
    }
    int count = fscanf(fp, "%x %x %x %x %x %x %x %x", 
                           bytes+0, bytes+1, bytes+2, bytes+3,
                           bytes+4, bytes+5, bytes+6, bytes+7);
    fclose(fp);
    if(count != AIM_ARRAYSIZE(bytes)) {
        /* Likely a CPLD read timeout. */
        syslog(LOG_ERR, "Unable to read all fields from the sfp_is_present_all device file.");
        return ONLP_STATUS_E_INTERNAL;
    }

    /* Convert to 64 bit integer in port order */
    int i = 0;
    uint64_t presence_all = 0 ;
    for(i = AIM_ARRAYSIZE(bytes)-1; i >= 0; i--) {
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
onlp_sfpi_eeprom_read(int port, uint8_t data[256])
{
    /*
     * Read the SFP eeprom into data[]
     *
     * Return MISSING if SFP is missing.
     * Return OK if eeprom is read
     */
	int size = 0;

    if(onlp_file_read(data, 256, &size, PORT_FORMAT, PORT_BUS_INDEX(port), "eeprom") != ONLP_STATUS_OK) {
        syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_READ_FAIL, "Unable to read eeprom from port(%d)", port);
        return ONLP_STATUS_E_INTERNAL;
    }

    if(size != 256) {
        return ONLP_STATUS_E_INTERNAL;
    }

	return ONLP_STATUS_OK;
}

int
onlp_sfpi_dev_readb(int port, uint8_t devaddr, uint8_t addr)
{
    int bus = PORT_BUS_INDEX(port);
    return onlp_i2c_readb(bus, devaddr, addr, ONLP_I2C_F_FORCE);
}

int
onlp_sfpi_dev_writeb(int port, uint8_t devaddr, uint8_t addr, uint8_t value)
{
    int bus = PORT_BUS_INDEX(port);
    return onlp_i2c_writeb(bus, devaddr, addr, value, ONLP_I2C_F_FORCE);
}

int
onlp_sfpi_dev_readw(int port, uint8_t devaddr, uint8_t addr)
{
    int bus = PORT_BUS_INDEX(port);
    return onlp_i2c_readw(bus, devaddr, addr, ONLP_I2C_F_FORCE);
}

int
onlp_sfpi_dev_writew(int port, uint8_t devaddr, uint8_t addr, uint16_t value)
{
    int bus = PORT_BUS_INDEX(port);
    return onlp_i2c_writew(bus, devaddr, addr, value, ONLP_I2C_F_FORCE);
}

int
onlp_sfpi_control_set(int port, onlp_sfp_control_t control, int value)
{
    int rv = ONLP_STATUS_E_INTERNAL;
    int present = 0;
    int lpmode_value = 0;

    VALIDATE_QSFP(port); //only QSFP

    switch(control)
        {
        case ONLP_SFP_CONTROL_TX_DISABLE:
        case ONLP_SFP_CONTROL_TX_DISABLE_CHANNEL:
            {
                present = onlp_sfpi_is_present(port);
                /* write qsfp eeprom data of tx disable if qsfp present */
                if(present == 1)
                {
                    /* txdis valid bit(bit0-bit3), xxxx 1111 */
                    value = value&0xf;

                    if(onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_TXDIS, value) < 0){
                        syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_WRITE_TARGET_BYTE_FAIL,
                                    "Unable to write tx_disabled status to port(%d): write eeprom fail", port);
                        rv = ONLP_STATUS_E_INTERNAL;
                    }
                    else {
                        rv = ONLP_STATUS_OK;
                    }
                }
                else
                {
                    rv = ONLP_STATUS_E_INTERNAL;
                }
                break;
            }

        case ONLP_SFP_CONTROL_RESET:
            {
                if (onlp_file_write_int(value, MODULE_RESET_FORMAT, (port+1)) < 0) {
                    syslog_ctrl(log_mgmt[port].log_ctrl, SFP_SYSFS_WRITE_FAIL,
                                "Unable to write reset status to port(%d)", port);
                    rv = ONLP_STATUS_E_INTERNAL;
                }
                else {
                    rv = ONLP_STATUS_OK;
                }
                break;
            }

        case ONLP_SFP_CONTROL_LP_MODE:
            {
                present = onlp_sfpi_is_present(port);
                if (present == 1) {
                    /* lpmode valid bit(bit0):set LP/txdis mode bit(bit1):set low/high power mode */
                    lpmode_value = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_LPMODE);
                    if(lpmode_value < 0){
                        syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_READ_TARGET_BYTE_FAIL,
                                    "Unable to write LP mode status to port(%d):read LP mode value fail", port);
                        rv = ONLP_STATUS_E_INTERNAL;
                    }
                    else{
                        if(value){
                            lpmode_value |= QSFP_LPMODE;
                        } else{
                            lpmode_value &= ~QSFP_LPMODE;
                        }

                        if(onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_LPMODE, lpmode_value) < 0){
                            syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_WRITE_TARGET_BYTE_FAIL,
                                        "Unable to write LP mode status to port(%d):write eeprom fail", port);
                            rv = ONLP_STATUS_E_INTERNAL;
                        }
                        else {
                            rv = ONLP_STATUS_OK;
                        }
                    }
                }
                else
                {
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
    int rv = ONLP_STATUS_E_INTERNAL;
    int present = 0;
    int lpmode_value = 0;
    int tx_disable = 0;
    VALIDATE_QSFP(port); //only QSFP

    switch(control)
        {
        case ONLP_SFP_CONTROL_TX_DISABLE:
        case ONLP_SFP_CONTROL_TX_DISABLE_CHANNEL:
            {
                present = onlp_sfpi_is_present(port);
                /* read qsfp eeprom offset of tx disable if qsfp on the port */
                if(present == 1)
                {
                    tx_disable = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_TXDIS);
                    if(tx_disable < 0){
                        syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_READ_TARGET_BYTE_FAIL,
                                    "Unable to read tx_disabled status from port(%d): read eeprom fail", port);
                        rv = ONLP_STATUS_E_INTERNAL;
                    }
                    else{
                        *value = tx_disable;
                        rv = ONLP_STATUS_OK;
                    }

                }
                else
                {
                    rv = ONLP_STATUS_E_INTERNAL;
                }
                break;
            }

        case ONLP_SFP_CONTROL_RESET: 
            {
                if (onlp_file_read_int(value, MODULE_RESET_FORMAT, (port+1)) < 0) {
                    syslog_ctrl(log_mgmt[port].log_ctrl, SFP_SYSFS_READ_FAIL,
                                "Unable to read reset status from port(%d)", port);
                    rv = ONLP_STATUS_E_INTERNAL;
                }
                else {
                    rv = ONLP_STATUS_OK;
                }
                break;
            }

        case ONLP_SFP_CONTROL_LP_MODE:
            {
                present = onlp_sfpi_is_present(port);
                if (present == 1) {
                    /* lpmode valid bit(bit0):set LP/txdis mode bit(bit1):set low/high power mode */
                    lpmode_value = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_LPMODE);
                    if(lpmode_value < 0){
                        syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_READ_TARGET_BYTE_FAIL,
                                    "Unable to read LP mode status from port(%d): read eeprom fail", port);
                        rv = ONLP_STATUS_E_INTERNAL;
                    }
                    else{
                        *value = ((lpmode_value & QSFP_LPMODE) == QSFP_LPMODE);
                        rv = ONLP_STATUS_OK;
                    }

                }
                else
                {
                    rv = ONLP_STATUS_E_INTERNAL;
                }
                break;
            }

        default:
            rv = ONLP_STATUS_E_UNSUPPORTED;
        }

    return rv;
}

int
onlp_sfpi_denit(void)
{
    return ONLP_STATUS_OK;
}

