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
#include "log_ctrl.h"

#include <onlp/platformi/sfpi.h>
#include <onlplib/i2c.h>
#include <onlplib/file.h>
#include "x86_64_accton_as9726_32d_int.h"
#include "x86_64_accton_as9726_32d_log.h"

#define MAX_PORT 33

#define VALIDATE(_port) \
	do { \
		if (_port < 0 || _port > 33) { \
			return ONLP_STATUS_E_INVALID; \
		} \
	} while(0)

#define VALIDATE_SFP(_port) \
	do { \
		if (_port < 32 || _port > 33) \
			return ONLP_STATUS_E_UNSUPPORTED; \
	} while(0)

#define VALIDATE_QSFP(_port) \
	do { \
		if (_port < 0 || _port > 31 ) \
			return ONLP_STATUS_E_UNSUPPORTED; \
	} while(0)

#define PORT_BUS_INDEX(port) (port+18)

#define PORT_EEPROM_FORMAT        "/sys/bus/i2c/devices/%d-0050/eeprom"
#define MODULE_PRESENT_FORMAT \
	"/sys/bus/i2c/devices/%d-00%d/module_present_%d"
#define MODULE_RXLOS_FORMAT \
	"/sys/bus/i2c/devices/%d-00%d/module_rx_los_%d"
#define MODULE_TXFAULT_FORMAT \
	"/sys/bus/i2c/devices/%d-00%d/module_tx_fault_%d"
#define MODULE_TXDISABLE_FORMAT \
	"/sys/bus/i2c/devices/%d-00%d/module_tx_disable_%d"
#define MODULE_RESET_FORMAT \
	"/sys/bus/i2c/devices/%d-00%d/module_reset_%d"
#define MODULE_LPMODE_FORMAT \
	"/sys/bus/i2c/devices/%d-00%d/module_lpmode_%d"
#define MODULE_PRESENT_ALL_ATTR \
	"/sys/bus/i2c/devices/%d-00%d/module_present_all"
#define MODULE_RXLOS_ALL_ATTR_CPLD \
	"/sys/bus/i2c/devices/10-0062/module_rx_los_all"

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

int sfp_map_bus[] = {17, 18, 19, 20, 21, 22, 23, 24,
		    25, 26, 27, 28, 29, 30, 31, 32,
		    33, 34, 35, 36, 37, 38, 39, 40,
		    41, 42, 43, 44, 45, 46, 47, 48,
		    49, 50};

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

int onlp_sfpi_init(void)
{
	/* Called at initialization time */
	return ONLP_STATUS_OK;
}

int onlp_sfpi_map_bus_index(int port)
{
	VALIDATE(port);
	return sfp_map_bus[port];
}

int onlp_sfpi_bitmap_get(onlp_sfp_bitmap_t* bmap)
{
	/*
	 * Ports {0, 34}
	 */
	int p;

	for (p = 0; p < 34; p++)
		AIM_BITMAP_SET(bmap, p);

	return ONLP_STATUS_OK;
}

int onlp_sfpi_is_present(int port)
{
	/*
	 * Return 1 if present.
	 * Return 0 if not present.
	 * Return < 0 if error.
	 */
	int present;
	int bus, addr;

	VALIDATE(port);

	if (port < 16) {
		addr = 61;
		bus  = 10;

		if (onlp_file_read_int(&present, MODULE_PRESENT_FORMAT, bus, 
					addr, (port+1)) < 0) {
			if (log_mgmt[port].present_rec != ONLP_STATUS_E_INTERNAL) {
				reset_log_ctrl(log_mgmt[port].log_ctrl, SFP_LOG_REASON_COUNT);
			}
			log_mgmt[port].present_rec = ONLP_STATUS_E_INTERNAL;

			syslog_ctrl(log_mgmt[port].log_ctrl, SFP_SYSFS_READ_FAIL,
				    "Unable to read present status from port(%d)", port);
			return ONLP_STATUS_E_INTERNAL;
		}
	} else {
		addr = 62;
		bus = 10;

		if (onlp_file_read_int(&present, MODULE_PRESENT_FORMAT, bus, 
					addr, (port+1)) < 0) {
			if (log_mgmt[port].present_rec != ONLP_STATUS_E_INTERNAL) {
				reset_log_ctrl(log_mgmt[port].log_ctrl, SFP_LOG_REASON_COUNT);
			}
			log_mgmt[port].present_rec = ONLP_STATUS_E_INTERNAL;

			syslog_ctrl(log_mgmt[port].log_ctrl, SFP_SYSFS_READ_FAIL,
				    "Unable to read present status from port(%d)", port);
			return ONLP_STATUS_E_INTERNAL;
		}
	}

	if (present == 1 && present != log_mgmt[port].present_rec) {
		reset_log_ctrl(log_mgmt[port].log_ctrl, SFP_LOG_REASON_COUNT);
	}
	log_mgmt[port].present_rec = present;

	return present;
}

int onlp_sfpi_presence_bitmap_get(onlp_sfp_bitmap_t* dst)
{
	uint32_t bytes[5], *ptr = NULL;
	FILE* fp;
	int addr = 61;
	int bus = 10;
	char file[64] = {0};
	int count;

	ptr = bytes;
	sprintf(file, MODULE_PRESENT_ALL_ATTR, bus, addr);
	fp = fopen(file, "r");
	if (fp == NULL) {
		syslog(LOG_ERR, "Unable to open the module_present_all device file of CPLD2.");
		return ONLP_STATUS_E_INTERNAL;
	}

	count = fscanf(fp, "%x %x", ptr+0, ptr+1);
	fclose(fp);
	if (count != 2) {
		/* Likely a CPLD read timeout. */
		syslog(LOG_ERR, "Unable to read all fields the module_present_all device file of CPLD2.");
		return ONLP_STATUS_E_INTERNAL;
	}

	addr = 62;

	sprintf(file, MODULE_PRESENT_ALL_ATTR, bus, addr);
	fp = fopen(file, "r");
	if (fp == NULL) {
		syslog(LOG_ERR, "Unable to open the module_present_all device file of CPLD3.");
		return ONLP_STATUS_E_INTERNAL;
	}

	count = fscanf(fp, "%x %x %x", ptr+2, ptr+3, ptr+4);
	fclose(fp);
	if (count != 3) {
		/* Likely a CPLD read timeout. */
		syslog(LOG_ERR, "Unable to read all fields the module_present_all device file of CPLD3.");
		return ONLP_STATUS_E_INTERNAL;
	}

	/* Convert to 64 bit integer in port order */
	uint64_t presence_all = 0 ;
	int i = 0;
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

int onlp_sfpi_rx_los_bitmap_get(onlp_sfp_bitmap_t* dst)
{
	uint32_t bytes[5];
	uint32_t *ptr = bytes;
	FILE* fp;

	int addr = 60, i = 0;

	fp = fopen(MODULE_RXLOS_ALL_ATTR_CPLD, "r");
	if (fp == NULL) {
		syslog(LOG_ERR, "Unable to open the module_rx_los_all device file of CPLD(0x%d)"
			      , addr);
		return ONLP_STATUS_E_INTERNAL;
	}

	int count = fscanf(fp, "%x %x %x %x %x", ptr+0, ptr+1, ptr+2, ptr+3, 
			   ptr+4);
	fclose(fp);
	if (count != 5) {
		/* Likely a CPLD read timeout. */
		syslog(LOG_ERR, "Unable to read all fields from the module_rx_los_all device file of CPLD(0x%d)"
			      , addr);
		return ONLP_STATUS_E_INTERNAL;
	}

	uint64_t rx_los_all = 0;
	
	for (i = AIM_ARRAYSIZE(bytes)-1; i >= 0; i--) {
		rx_los_all <<= 8;
		rx_los_all |= bytes[i];
	}

	/* Populate bitmap */
	for (i = 0; rx_los_all; i++) {
		AIM_BITMAP_MOD(dst, i, (rx_los_all & 1));
		rx_los_all >>= 1;
	}

	return ONLP_STATUS_OK;
}

int onlp_sfpi_eeprom_read(int port, uint8_t data[256])
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

	if (onlp_file_read(data, 256, &size, PORT_EEPROM_FORMAT,
			  onlp_sfpi_map_bus_index(port)) != ONLP_STATUS_OK) {
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

int onlp_sfpi_dom_read(int port, uint8_t data[256])
{
	FILE* fp;
	char file[64] = {0};

	VALIDATE(port);
	sprintf(file, PORT_EEPROM_FORMAT, onlp_sfpi_map_bus_index(port));
	fp = fopen(file, "r");
	if (fp == NULL) {
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

int onlp_sfpi_dev_readb(int port, uint8_t devaddr, uint8_t addr)
{
	VALIDATE(port);
	return onlp_i2c_readb(onlp_sfpi_map_bus_index(port), devaddr, addr, ONLP_I2C_F_FORCE);
}

int onlp_sfpi_dev_writeb(int port, uint8_t devaddr, uint8_t addr, 
			 uint8_t value)
{
	VALIDATE(port);
	return onlp_i2c_writeb(onlp_sfpi_map_bus_index(port), devaddr, addr, value, ONLP_I2C_F_FORCE);
}

int onlp_sfpi_dev_readw(int port, uint8_t devaddr, uint8_t addr)
{
	VALIDATE(port);
	return onlp_i2c_readw(onlp_sfpi_map_bus_index(port), devaddr, addr, ONLP_I2C_F_FORCE);
}

int onlp_sfpi_dev_writew(int port, uint8_t devaddr, uint8_t addr, 
			 uint16_t value)
{
	VALIDATE(port);
	return onlp_i2c_writew(onlp_sfpi_map_bus_index(port), devaddr, addr, value, ONLP_I2C_F_FORCE);
}

int onlp_sfpi_control_set(int port, onlp_sfp_control_t control, int value)
{
	int rv;
	int addr = 0;
	int bus  = 10;
	int present = 0;
	int identifier = 0;
	int status_byte = 0;
	int support_ctrls = 0;

	VALIDATE(port);

	switch(control) {
	case ONLP_SFP_CONTROL_TX_DISABLE:
	case ONLP_SFP_CONTROL_TX_DISABLE_CHANNEL:
		present = onlp_sfpi_is_present(port);

		if (present == 1) {
			if(port >= 0 && port <= 31) {
				if ((identifier = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_IDENTIFIER)) < 0) {
					syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_PREP_FAIL,
						    "Failed to read Identifier, unable to write tx_disable status to port(%d)", port);
					return ONLP_STATUS_E_INTERNAL;
				}

				if (identifier == QSFP_DD_IDENTIFIER) {
					/* Flat-memory CMIS modules do not implement page 01h/10h */
					if ((status_byte = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_LOWER_OFFSET_STATUS)) < 0) {
						syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_PREP_FAIL,
							    "Failed to read Status byte, unable to write tx_disable status to port(%d)", port);
						return ONLP_STATUS_E_INTERNAL;
					}
					if (status_byte & QSFP_DD_FLAT_MEM) {
						return ONLP_STATUS_E_UNSUPPORTED;
					}
					if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADVERTISING)) < 0) {
						syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_PREP_FAIL,
							    "Failed to switch to Advertising Page on port(%d)", port);
						goto restore;
					}
					if ((support_ctrls = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_P01H_OFFSET_CONTROL_1)) < 0) {
						syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_PREP_FAIL,
							    "Failed to read Support Control on port(%d)", port);
						rv = support_ctrls;
						goto restore;
					}
					if (support_ctrls & QSFP_DD_P01H_TX_DISABLE_SUPPORT) {
						if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_BANK_SELECT, 0)) < 0) {
							syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_PREP_FAIL,
								    "Failed to set Bank 0, unable to write tx_disable status to port(%d)", port);
							goto restore;
						}
						if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_LANE_CTRL)) < 0) {
							syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_PREP_FAIL,
								    "Failed to switch to Lane Control Page (Page 0x%02x) on port(%d)",
								    QSFP_DD_PAGE_LANE_CTRL, port);
							goto restore;
						}
						if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_P10H_OFFSET_OUTPUT_DISABLE_TX, (value & 0xff))) < 0) {
							syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_WRITE_TARGET_BYTE_FAIL,
								    "Failed to write tx_disable value(0x%02x) to Lane Control register on port(%d)", value, port);
							goto restore;
						}
					} else {
						rv = ONLP_STATUS_E_UNSUPPORTED;
						goto restore;
					}

				restore:
					if ((onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_PAGE_SELECT,
															QSFP_DD_PAGE_ADMIN_INFO)) < 0) {
						syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_PREP_FAIL,
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
						syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_WRITE_TARGET_BYTE_FAIL,
							    "Unable to write tx_disable status to port(%d)", port);
						rv = ONLP_STATUS_E_INTERNAL;
					} else {
						rv = ONLP_STATUS_OK;
					}
				}
			} else {
				addr = 62;
				if (onlp_file_write_int(value, MODULE_TXDISABLE_FORMAT,
							bus, addr, (port + 1)) < 0) {
					syslog_ctrl(log_mgmt[port].log_ctrl, SFP_SYSFS_WRITE_FAIL,
						    "Unable to set tx_disable status to port(%d)", port);
					rv = ONLP_STATUS_E_INTERNAL;
				} else {
					rv = ONLP_STATUS_OK;
				}
			}
		} else {
			rv = ONLP_STATUS_E_INTERNAL;
		}
		break;

	case ONLP_SFP_CONTROL_RESET:

		VALIDATE_QSFP(port);
		addr = (port < 16) ? 61 : 62;

		if (onlp_file_write_int(value, MODULE_RESET_FORMAT,
					bus, addr, (port + 1)) < 0) {
			syslog_ctrl(log_mgmt[port].log_ctrl, SFP_SYSFS_WRITE_FAIL,
				    "Unable to set reset status to port(%d)", port);
			rv = ONLP_STATUS_E_INTERNAL;
		} else {
			rv = ONLP_STATUS_OK;
		}
		break;

	case ONLP_SFP_CONTROL_LP_MODE:

		VALIDATE_QSFP(port);
		addr = (port < 16) ? 61 : 62;

		if (onlp_file_write_int(value, MODULE_LPMODE_FORMAT,
					bus, addr, (port + 1)) < 0) {
			syslog_ctrl(log_mgmt[port].log_ctrl, SFP_SYSFS_WRITE_FAIL,
				    "Unable to set LP mode to port(%d)", port);
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

int onlp_sfpi_control_get(int port, onlp_sfp_control_t control, int* value)
{
	int rv;
	int addr = 0;
	int bus  = 10;
	int present = 0;
	int identifier = 0;
	int status_byte = 0;
	int support_ctrls = 0;
	int tx_dis = 0;

	VALIDATE(port);

	switch (control) {
	case ONLP_SFP_CONTROL_RX_LOS:

		VALIDATE_SFP(port);
		addr = 62;

		if (onlp_file_read_int(value, MODULE_RXLOS_FORMAT, 
				       bus, addr, (port+1)) < 0) {
			syslog_ctrl(log_mgmt[port].log_ctrl, SFP_SYSFS_READ_FAIL,
				    "Unable to read rx_loss status from port(%d)", port);
			rv = ONLP_STATUS_E_INTERNAL;
		} else {
			rv = ONLP_STATUS_OK;
		}
		break;

	case ONLP_SFP_CONTROL_TX_FAULT:

		VALIDATE_SFP(port);
		addr = 62;

		if (onlp_file_read_int(value, MODULE_TXFAULT_FORMAT,
						bus, addr, (port+1)) < 0) {
			syslog_ctrl(log_mgmt[port].log_ctrl, SFP_SYSFS_READ_FAIL,
				    "Unable to read tx_fault status from port(%d)", port);
			rv = ONLP_STATUS_E_INTERNAL;
		} else {
			rv = ONLP_STATUS_OK;
		}
		break;

	case ONLP_SFP_CONTROL_TX_DISABLE:
	case ONLP_SFP_CONTROL_TX_DISABLE_CHANNEL:
		present = onlp_sfpi_is_present(port);

		if (present == 1) {
			if (port >= 0 && port <= 31) {
				if ((identifier = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_IDENTIFIER)) < 0) {
					syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_PREP_FAIL,
						    "Failed to read Identifier, unable to read tx_disable status from port(%d)", port);
					return ONLP_STATUS_E_INTERNAL;
				}

				if (identifier == QSFP_DD_IDENTIFIER) {
					/* Flat-memory CMIS modules do not implement page 01h/10h */
					if ((status_byte = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_LOWER_OFFSET_STATUS)) < 0) {
						syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_PREP_FAIL,
							    "Failed to read Status byte, unable to read tx_disable status from port(%d)", port);
						return ONLP_STATUS_E_INTERNAL;
					}
					if (status_byte & QSFP_DD_FLAT_MEM) {
						return ONLP_STATUS_E_UNSUPPORTED;
					}
					if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADVERTISING)) < 0) {
						syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_PREP_FAIL,
							    "Failed to switch to Advertising Page on port(%d)", port);
						goto restore;
					}
					if ((support_ctrls = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_P01H_OFFSET_CONTROL_1)) < 0) {
						syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_PREP_FAIL,
							    "Failed to read Support Control on port(%d)", port);
						rv = support_ctrls;
						goto restore;
					}
					if (support_ctrls & QSFP_DD_P01H_TX_DISABLE_SUPPORT) {
						if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_BANK_SELECT, 0)) < 0) {
							syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_PREP_FAIL,
								    "Failed to set Bank 0, unable to read tx_disable status from port(%d)", port);
							goto restore;
						}
						if ((rv = onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_LANE_CTRL)) < 0) {
							syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_PREP_FAIL,
								    "Failed to switch to Lane Control Page (Page 0x%02x) on port(%d)",
								    QSFP_DD_PAGE_LANE_CTRL, port);
							goto restore;
						}
						if ((tx_dis = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_P10H_OFFSET_OUTPUT_DISABLE_TX)) < 0) {
							syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_READ_TARGET_BYTE_FAIL,
								    "Failed to read TX_DISABLE value from Lane Control register on port(%d)", port);
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
						syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_PREP_FAIL,
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
						syslog_ctrl(log_mgmt[port].log_ctrl, SFP_EEPROM_READ_TARGET_BYTE_FAIL,
							    "Unable to read tx_disable status from port(%d)", port);
						rv = ONLP_STATUS_E_INTERNAL;
					} else {
						*value = (tx_dis & 0xf);
						rv = ONLP_STATUS_OK;
					}
				}
			} else { /* SFP */
				addr = 62;
				if (onlp_file_read_int(value, MODULE_TXDISABLE_FORMAT,
							bus, addr, (port+1)) < 0) {
					syslog_ctrl(log_mgmt[port].log_ctrl, SFP_SYSFS_READ_FAIL,
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

	case ONLP_SFP_CONTROL_RESET:

		VALIDATE_QSFP(port);
		addr = (port < 16) ? 61 : 62;

		if (onlp_file_read_int(value, MODULE_RESET_FORMAT,
					bus, addr, (port + 1)) < 0) {
			syslog_ctrl(log_mgmt[port].log_ctrl, SFP_SYSFS_READ_FAIL,
				    "Unable to get reset status from port(%d)", port);
			rv = ONLP_STATUS_E_INTERNAL;
		} else {
			rv = ONLP_STATUS_OK;
		}
		break;	

	case ONLP_SFP_CONTROL_LP_MODE:

		VALIDATE_QSFP(port);
		addr = (port < 16) ? 61 : 62;

		if (onlp_file_read_int(value, MODULE_LPMODE_FORMAT,
					bus, addr, (port + 1)) < 0) {
			syslog_ctrl(log_mgmt[port].log_ctrl, SFP_SYSFS_READ_FAIL,
				    "Unable to get LP mode from port(%d)", port);
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
