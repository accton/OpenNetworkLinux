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
#include "x86_64_accton_as9926_24db_int.h"
#include "x86_64_accton_as9926_24db_log.h"

#define PORT_BUS_INDEX(port) (port+9)

#define SFP_PORT_MIN 24
#define SFP_PORT_MAX 25
#define QSFP_PORT_MIN 0
#define QSFP_PORT_MAX 23
#define MIN_PORT QSFP_PORT_MIN
#define MAX_PORT SFP_PORT_MAX

#define VALIDATE_SFP(_port) \
    do { \
        if (_port < SFP_PORT_MIN || _port > SFP_PORT_MAX) \
            return ONLP_STATUS_E_UNSUPPORTED; \
    } while(0)

#define VALIDATE_QSFP(_port) \
    do { \
        if (_port < QSFP_PORT_MIN || _port > QSFP_PORT_MAX) \
            return ONLP_STATUS_E_UNSUPPORTED; \
    } while(0)

#define VALIDATE_PORT(_port) \
    do { \
        if (_port < MIN_PORT || _port > MAX_PORT) \
            return ONLP_STATUS_E_UNSUPPORTED; \
    } while(0)

/* QSFP device address of eeprom */
#define PORT_EEPROM_DEVADDR 0x50

/*QSFP identify offsets*/
#define QSFP_EEPROM_OFFSET_IDENTIFIER 0x0

/* QSFP eeprom offsets*/
#define QSFP_EEPROM_OFFSET_TXDIS 0x56

/* QSFP DD eeprom offsets*/
#define QSFP_DD_EEPROM_OFFSET_BANK_SELECT 0x7E
#define QSFP_DD_EEPROM_OFFSET_PAGE_SELECT 0x7F
#define QSFP_DD_EEPROM_P01H_OFFSET_CONTROL_1 0x9B
#define QSFP_DD_EEPROM_P10H_OFFSET_OUTPUT_DISABLE_TX 0x82
#define QSFP_DD_EEPROM_OFFSET_LOWPWR_REQUEST_SW 0x1A

/*QSFP Specific*/
#define QSFP_EEPROM_OFFSET_LPMODE 0x5D
#define QSFP_LPMODE 0x3

/* QSFP DD Specific*/
#define QSFP_DD_PAGE_ADMIN_INFO 0x0
#define QSFP_DD_PAGE_ADVERTISING 0x1
#define QSFP_DD_PAGE_LANE_CTRL 0x10
#define QSFP_DD_LOWER_OFFSET_STATUS 0x02
#define QSFP_DD_FLAT_MEM 0x80                          /* byte 0x02 bit 7 */
#define QSFP_DD_P01H_TX_DISABLE_SUPPORT 0x2
#define QSFP_DD_LPMODE 0x10

/* OSFP IDENTIFIER Specific*/
#define QSFP_DD_IDENTIFIER 0x18

#define PORT_EEPROM_FORMAT \
	"/sys/bus/i2c/devices/%d-0050/eeprom"
#define MODULE_PRESENT_FORMAT \
	"/sys/devices/platform/as9926_24db_sfp/module_present_%d"
#define MODULE_RESET_FORMAT \
	"/sys/devices/platform/as9926_24db_sfp/module_reset_%d"
#define MODULE_RXLOS_FORMAT \
	"/sys/devices/platform/as9926_24db_sfp/module_rx_los_%d"
#define MODULE_TXFAULT_FORMAT \
	"/sys/devices/platform/as9926_24db_sfp/module_tx_fault_%d"
#define MODULE_TXDISABLE_FORMAT \
	"/sys/devices/platform/as9926_24db_sfp/module_tx_disable_%d"
#define MODULE_PRESENT_ALL_ATTR \
	"/sys/devices/platform/as9926_24db_sfp/module_present_all"
#define MODULE_RXLOS_ALL_ATTR \
	"/sys/devices/platform/as9926_24db_sfp/module_rxlos_all"

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

int onlp_sfpi_bitmap_get(onlp_sfp_bitmap_t* bmap)
{
	/*
	 * Ports {0, 26}
	 */
	int p;

	for(p = 0; p < 26; p++) {
		AIM_BITMAP_SET(bmap, p);
	}

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

	if (onlp_file_read_int(&present, MODULE_PRESENT_FORMAT, 
			       (port+1)) < 0) {
		AIM_LOG_ERROR("Unable to read present status from port(%d)\r\n",
			      port);
		return ONLP_STATUS_E_INTERNAL;
	}

	return present;
}

int onlp_sfpi_presence_bitmap_get(onlp_sfp_bitmap_t* dst)
{
	uint32_t bytes[4];
	FILE* fp;

	/* Read present status of port 0~25 */
	int count = 0;

	fp = fopen(MODULE_PRESENT_ALL_ATTR, "r");
	if(fp == NULL) {
		AIM_LOG_ERROR("Unable to open the module_present_all \
			      device file from (%s).", 
			      MODULE_PRESENT_ALL_ATTR);
		return ONLP_STATUS_E_INTERNAL;
	}

	count = fscanf(fp, "%x %x %x %x", bytes+0, bytes+1, bytes+2, 
		       bytes+3);
	
	fclose(fp);

	if(count != 4) {
		/* Likely a CPLD read timeout. */
		AIM_LOG_ERROR("Unable to read all fields the \
			       module_present_all device file from(%s).", 
			       MODULE_PRESENT_ALL_ATTR);
		return ONLP_STATUS_E_INTERNAL;
	}

	/* Mask out non-existant SFP ports */
	bytes[3] &= 0x3;

	/* Convert to 64 bit integer in port order */
	int i = 0;
	uint64_t presence_all = 0 ;
	AIM_BITMAP_CLR_ALL(dst);
	
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

int onlp_sfpi_rx_los_bitmap_get(onlp_sfp_bitmap_t* dst)
{
	uint32_t bytes[4];
	FILE* fp;

	/* Read present status of port 0~25 */
	int count = 0;


	bytes[0] = bytes[1] = bytes[2] = bytes[3] = 0;
	fp = fopen(MODULE_RXLOS_ALL_ATTR, "r");
    
	if(fp == NULL) {
		AIM_LOG_ERROR("Unable to open the module_rxlos_all \
			      device file from (%s).", 
			      MODULE_RXLOS_ALL_ATTR);

		return ONLP_STATUS_E_INTERNAL;
	}

	count = fscanf(fp, "%x", bytes+3);
    
	fclose(fp);

	if(count != 1) {
		/* Likely a CPLD read timeout. */
		AIM_LOG_ERROR("Unable to read all fields the module_rxlos_all \
			      device file from(%s).", MODULE_RXLOS_ALL_ATTR);
	
		return ONLP_STATUS_E_INTERNAL;
	}

	/* Mask out non-existant SFP ports */
	bytes[3] &= 0x3;

	/* Convert to 64 bit integer in port order */
	int i = 0;
	uint64_t rx_los_all = 0 ;
	AIM_BITMAP_CLR_ALL(dst);

	for(i = AIM_ARRAYSIZE(bytes)-1; i >= 0; i--) {
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

int onlp_sfpi_eeprom_read(int port, uint8_t data[256])
{
	/*
	 * Read the SFP eeprom into data[]
	 *
	 * Return MISSING if SFP is missing.
	 * Return OK if eeprom is read
	 */
    
	int size = 0;
	memset(data, 0, 256);

	if(onlp_file_read(data, 256, &size, PORT_EEPROM_FORMAT, 
			  PORT_BUS_INDEX(port)) != ONLP_STATUS_OK) {
		AIM_LOG_ERROR("Unable to read eeprom from port(%d)\r\n", port);
		return ONLP_STATUS_E_INTERNAL;
	}

	if (size != 256) {
		AIM_LOG_ERROR("Unable to read eeprom from port(%d), size is \
			      different!\r\n", port);
		return ONLP_STATUS_E_INTERNAL;
	}

	return ONLP_STATUS_OK;
}

int onlp_sfpi_dom_read(int port, uint8_t data[256])
{
	FILE* fp;
	char file[64] = {0};
    
	sprintf(file, PORT_EEPROM_FORMAT, PORT_BUS_INDEX(port));
	fp = fopen(file, "r");
	if(fp == NULL) {
		AIM_LOG_ERROR("Unable to open the eeprom device file of \
			      port(%d)", port);
		return ONLP_STATUS_E_INTERNAL;
	}

	if (fseek(fp, 256, SEEK_CUR) != 0) {
		fclose(fp);
		AIM_LOG_ERROR("Unable to set the file position indicator of \
			      port(%d)", port);
		return ONLP_STATUS_E_INTERNAL;
	}

	int ret = fread(data, 1, 256, fp);
	fclose(fp);
	if (ret != 256) {
		AIM_LOG_ERROR("Unable to read the module_eeprom device file of \
			      port(%d)", port);
		return ONLP_STATUS_E_INTERNAL;
	}

	return ONLP_STATUS_OK;
}

int onlp_sfpi_control_set(int port, onlp_sfp_control_t control, int value)
{
	int rv = ONLP_STATUS_E_INTERNAL;
	int present = 0;
	int lpmode_value = 0;
	int identifier = 0;
	int status_byte = 0;
	int eeprom_control;

	VALIDATE_PORT(port);

	switch(control)
	{
	case ONLP_SFP_CONTROL_TX_DISABLE:
	case ONLP_SFP_CONTROL_TX_DISABLE_CHANNEL:
	{
		present = onlp_sfpi_is_present(port);
		if (present == 1) {
			if (port >= SFP_PORT_MIN && port <= SFP_PORT_MAX) { /* SFP */
				if (onlp_file_write_int(value, MODULE_TXDISABLE_FORMAT,
							(port+1)) < 0) {
					AIM_LOG_ERROR("Unable to write tx_disable status to port(%d)\r\n", port);
					rv = ONLP_STATUS_E_INTERNAL;
				}
				else {
					rv = ONLP_STATUS_OK;
				}
			}
			else { /* QSFP */
				identifier = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_IDENTIFIER);
				if (identifier < 0) {
					AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): read identifier from eeprom fail\r\n", port);
					rv = ONLP_STATUS_E_INTERNAL;
				}
				else if (identifier == QSFP_DD_IDENTIFIER) { /* QSFP DD */
					/* Flat-memory CMIS modules do not implement page 01h/10h */
					status_byte = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_LOWER_OFFSET_STATUS);
					if (status_byte < 0) {
						AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): read status byte from eeprom fail\r\n", port);
						rv = ONLP_STATUS_E_INTERNAL;
					}
					else if (status_byte & QSFP_DD_FLAT_MEM) {
						AIM_LOG_ERROR("Setting tx_disable to port(%d) is not supported (flat-memory module)\r\n", port);
						rv = ONLP_STATUS_E_UNSUPPORTED;
					}
					else if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_BANK_SELECT, 0) < 0) {
						AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): write bank to eeprom fail\r\n", port);
						rv = ONLP_STATUS_E_INTERNAL;
					}
					else if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADVERTISING) < 0) {
						AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): write page to eeprom fail\r\n", port);
						rv = ONLP_STATUS_E_INTERNAL;
					}
					else {
						eeprom_control = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_P01H_OFFSET_CONTROL_1);
						if (eeprom_control < 0) {
							AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): read control from eeprom fail\r\n", port);
							rv = ONLP_STATUS_E_INTERNAL;
						}
						else if (eeprom_control & QSFP_DD_P01H_TX_DISABLE_SUPPORT) {
							if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_LANE_CTRL) < 0) {
								AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): write page to eeprom fail\r\n", port);
								rv = ONLP_STATUS_E_INTERNAL;
							}
							else if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_P10H_OFFSET_OUTPUT_DISABLE_TX, value) < 0) {
								AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): write TX disable to eeprom fail\r\n", port);
								rv = ONLP_STATUS_E_INTERNAL;
							}
							else {
								rv = ONLP_STATUS_OK;
							}
						} else {
							AIM_LOG_ERROR("Setting tx disable to port(%d) is not supported\r\n", port);
							rv = ONLP_STATUS_E_UNSUPPORTED;
						}
						if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADMIN_INFO) < 0) {
							AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): write page to eeprom fail\r\n", port);
							rv = ONLP_STATUS_E_INTERNAL;
						}
					}
				} else { /* QSFP 28 or QSFP+ */
					value = value & 0xf;
					if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_TXDIS, value) < 0) {
						AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): write TX disable to eeprom fail\r\n", port);
						rv = ONLP_STATUS_E_INTERNAL;
					}
					else {
						rv = ONLP_STATUS_OK;
					}
				}
			}
		}
		else {
			rv = ONLP_STATUS_E_INTERNAL;
		}
		break;
	}

	case ONLP_SFP_CONTROL_RESET:
	{
		VALIDATE_QSFP(port);
		if (onlp_file_write_int(value, MODULE_RESET_FORMAT,
					(port+1)) < 0) {
			AIM_LOG_ERROR("Unable to set reset to port(%d)\r\n", port);
			rv = ONLP_STATUS_E_INTERNAL;
		}
		else {
			rv = ONLP_STATUS_OK;
		}
		break;
	}

	case ONLP_SFP_CONTROL_LP_MODE:
	{
		VALIDATE_QSFP(port);
		present = onlp_sfpi_is_present(port);
		if (present == 1) {
			identifier = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_IDENTIFIER);
			if (identifier < 0) {
				AIM_LOG_ERROR("Unable to write LP mode status to port(%d): read identifier from eeprom fail\r\n", port);
				rv = ONLP_STATUS_E_INTERNAL;
			}
			else if (identifier == QSFP_DD_IDENTIFIER) { /* QSFP DD */
				lpmode_value = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_LOWPWR_REQUEST_SW);
				if (lpmode_value < 0) {
					AIM_LOG_ERROR("Unable to write LP mode status to port(%d): read LP mode value from eeprom fail\r\n", port);
					rv = ONLP_STATUS_E_INTERNAL;
				}
				else {
					if (value)
						lpmode_value |= QSFP_DD_LPMODE;
					else
						lpmode_value &= ~QSFP_DD_LPMODE;
					if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_LOWPWR_REQUEST_SW, lpmode_value) < 0) {
						AIM_LOG_ERROR("Unable to write LP mode status to port(%d): write LP mode value to eeprom fail\r\n", port);
						rv = ONLP_STATUS_E_INTERNAL;
					}
					else {
						rv = ONLP_STATUS_OK;
					}
				}
			} else { /* QSFP 28 or QSFP+ */
				lpmode_value = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_LPMODE);
				if (lpmode_value < 0) {
					AIM_LOG_ERROR("Unable to write LP mode status to port(%d): read LP mode value from eeprom fail\r\n", port);
					rv = ONLP_STATUS_E_INTERNAL;
				}
				else {
					if (value)
						lpmode_value |= QSFP_LPMODE;
					else
						lpmode_value &= ~QSFP_LPMODE;
					if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_LPMODE, lpmode_value) < 0) {
						AIM_LOG_ERROR("Unable to write LP mode status to port(%d): write LP mode value to eeprom fail\r\n", port);
						rv = ONLP_STATUS_E_INTERNAL;
					}
					else {
						rv = ONLP_STATUS_OK;
					}
				}
			}
		}
		else {
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

int onlp_sfpi_dev_readb(int port, uint8_t devaddr, uint8_t addr)
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

int onlp_sfpi_dev_readw(int port, uint8_t devaddr, uint8_t addr)
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

int onlp_sfpi_control_get(int port, onlp_sfp_control_t control, int* value)
{
	int rv = ONLP_STATUS_E_INTERNAL;
	int present = 0;
	int lpmode_value = 0;
	int identifier = 0;
	int status_byte = 0;
	int support_ctrls = 0;
	int tx_dis = 0;

	VALIDATE_PORT(port);

	switch(control)
	{
	case ONLP_SFP_CONTROL_RESET:
	{
		VALIDATE_QSFP(port);
		if (onlp_file_read_int(value, MODULE_RESET_FORMAT,
				       (port+1)) < 0) {
			AIM_LOG_ERROR("Unable to read reset status from port(%d)\r\n", port);
			rv = ONLP_STATUS_E_INTERNAL;
		}
		else {
			rv = ONLP_STATUS_OK;
		}
		break;
	}
	case ONLP_SFP_CONTROL_RX_LOS:
	{
		VALIDATE_SFP(port);
		if (onlp_file_read_int(value, MODULE_RXLOS_FORMAT,
				       (port+1)) < 0) {
			AIM_LOG_ERROR("Unable to read rx_loss status from port(%d)\r\n", port);
			rv = ONLP_STATUS_E_INTERNAL;
		}
		else {
			rv = ONLP_STATUS_OK;
		}
		break;
	}

	case ONLP_SFP_CONTROL_TX_FAULT:
	{
		VALIDATE_SFP(port);
		if (onlp_file_read_int(value, MODULE_TXFAULT_FORMAT,
				       (port+1)) < 0) {
			AIM_LOG_ERROR("Unable to read tx_fault status from port(%d)\r\n", port);
			rv = ONLP_STATUS_E_INTERNAL;
		}
		else {
			rv = ONLP_STATUS_OK;
		}
		break;
	}

	case ONLP_SFP_CONTROL_TX_DISABLE:
	case ONLP_SFP_CONTROL_TX_DISABLE_CHANNEL:
	{
		present = onlp_sfpi_is_present(port);
		if (present == 1) {
			if (port >= SFP_PORT_MIN && port <= SFP_PORT_MAX) { /* SFP */
				if (onlp_file_read_int(value, MODULE_TXDISABLE_FORMAT,
							(port+1)) < 0) {
					AIM_LOG_ERROR("Unable to read tx_disabled status from port(%d)\r\n", port);
					rv = ONLP_STATUS_E_INTERNAL;
				}
				else {
					rv = ONLP_STATUS_OK;
				}
			}
			else { /* QSFP */
				identifier = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_IDENTIFIER);
				if (identifier < 0) {
					AIM_LOG_ERROR("Unable to read tx_disabled status from port(%d): read identifier from eeprom fail\r\n", port);
					rv = ONLP_STATUS_E_INTERNAL;
				}
				else if (identifier == QSFP_DD_IDENTIFIER) { /* QSFP DD */
					/* Flat-memory CMIS modules do not implement page 01h/10h */
					status_byte = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_LOWER_OFFSET_STATUS);
					if (status_byte < 0) {
						AIM_LOG_ERROR("Unable to read tx_disable status from port(%d): read status byte from eeprom fail\r\n", port);
						rv = ONLP_STATUS_E_INTERNAL;
					}
					else if (status_byte & QSFP_DD_FLAT_MEM) {
						AIM_LOG_ERROR("Getting tx_disable from port(%d) is not supported (flat-memory module)\r\n", port);
						rv = ONLP_STATUS_E_UNSUPPORTED;
					}
					else if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_BANK_SELECT, 0) < 0) {
						AIM_LOG_ERROR("Unable to read tx_disable status from port(%d): write bank to eeprom fail\r\n", port);
						rv = ONLP_STATUS_E_INTERNAL;
					}
					else if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADVERTISING) < 0) {
						AIM_LOG_ERROR("Unable to read tx_disable status from port(%d): write page to eeprom fail\r\n", port);
						rv = ONLP_STATUS_E_INTERNAL;
					}
					else {
						support_ctrls = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_P01H_OFFSET_CONTROL_1);
						if (support_ctrls < 0) {
							AIM_LOG_ERROR("Unable to read tx_disable status from port(%d): read support control from eeprom fail\r\n", port);
							rv = ONLP_STATUS_E_INTERNAL;
						}
						else if (support_ctrls & QSFP_DD_P01H_TX_DISABLE_SUPPORT) {
							if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_LANE_CTRL) < 0) {
								AIM_LOG_ERROR("Unable to read tx_disable status from port(%d): write page to eeprom fail\r\n", port);
								rv = ONLP_STATUS_E_INTERNAL;
							}
							else {
								tx_dis = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_P10H_OFFSET_OUTPUT_DISABLE_TX);
								if (tx_dis < 0) {
									AIM_LOG_ERROR("Unable to read tx_disable status from port(%d): read TX disable from eeprom fail\r\n", port);
									rv = ONLP_STATUS_E_INTERNAL;
								}
								else {
									*value = tx_dis;
									rv = ONLP_STATUS_OK;
								}
							}
						} else {
							AIM_LOG_ERROR("Getting tx_disable from port(%d) is not supported\r\n", port);
							rv = ONLP_STATUS_E_UNSUPPORTED;
						}
						if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADMIN_INFO) < 0) {
							AIM_LOG_ERROR("Unable to read tx_disable status from port(%d): write page to eeprom fail\r\n", port);
							rv = ONLP_STATUS_E_INTERNAL;
						}
					}
				}
				else { /* QSFP 28 or QSFP+ */
					tx_dis = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_TXDIS);
					if (tx_dis < 0) {
						AIM_LOG_ERROR("Unable to read tx_disable status from port(%d): read TX disable from eeprom fail\r\n", port);
						rv = ONLP_STATUS_E_INTERNAL;
					}
					else {
						*value = tx_dis;
						rv = ONLP_STATUS_OK;
					}
				}
			}
		}
		else {
			rv = ONLP_STATUS_E_INTERNAL;
		}
		break;
	}

	case ONLP_SFP_CONTROL_LP_MODE:
	{
		VALIDATE_QSFP(port);
		present = onlp_sfpi_is_present(port);
		if (present == 1) {
			identifier = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_IDENTIFIER);
			if (identifier < 0) {
				AIM_LOG_ERROR("Unable to read LP mode status from port(%d): read identifier from eeprom fail\r\n", port);
				rv = ONLP_STATUS_E_INTERNAL;
			}
			else if (identifier == QSFP_DD_IDENTIFIER) { /* QSFP DD */
				/* lpmode valid bit(bit4): Low power request sw */
				lpmode_value = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_LOWPWR_REQUEST_SW);
				if (lpmode_value < 0) {
					AIM_LOG_ERROR("Unable to read LP mode status from port(%d): read LP mode value from eeprom fail\r\n", port);
					rv = ONLP_STATUS_E_INTERNAL;
				}
				else {
					*value = !!(lpmode_value & QSFP_DD_LPMODE);
					rv = ONLP_STATUS_OK;
				}
			} else { /* QSFP 28 or QSFP+ */
				/* lpmode valid bit(bit0):set LP/txdis mode bit(bit1):set low/high power mode */
				lpmode_value = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_LPMODE);
				if (lpmode_value < 0) {
					AIM_LOG_ERROR("Unable to read LP mode status from port(%d): read LP mode value from eeprom fail\r\n", port);
					rv = ONLP_STATUS_E_INTERNAL;
				}
				else {
					*value = ((lpmode_value & QSFP_LPMODE) == QSFP_LPMODE);
					rv = ONLP_STATUS_OK;
				}
			}
		}
		else {
			rv = ONLP_STATUS_E_INTERNAL;
		}
		break;
	}

	default:
		rv = ONLP_STATUS_E_UNSUPPORTED;
	}	

	return rv;
}

int onlp_sfpi_denit(void)
{
	return ONLP_STATUS_OK;
}
