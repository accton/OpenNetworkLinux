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
#include "x86_64_accton_as9737_32db_int.h"
#include "x86_64_accton_as9737_32db_log.h"

#define SFP_PORT_MIN 33
#define SFP_PORT_MAX 34
#define QSFP_PORT_MIN 1
#define QSFP_PORT_MAX 32
#define MIN_PORT QSFP_PORT_MIN
#define MAX_PORT SFP_PORT_MAX

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

#define VALIDATE_PORT(_port) \
    do { \
        if (_port < MIN_PORT || _port > MAX_PORT ) \
            return ONLP_STATUS_E_UNSUPPORTED; \
    } while(0)

#define MODULE_EEPROM_FORMAT       "/sys/bus/i2c/devices/%d-0050/eeprom"
#define MODULE_PRESENT_CPLD2_FORMAT "/sys/bus/i2c/devices/36-0061/module_present_%d"
#define MODULE_PRESENT_CPLD3_FORMAT "/sys/bus/i2c/devices/37-0062/module_present_%d"
#define MODULE_RXLOS_FORMAT         "/sys/bus/i2c/devices/37-0062/module_rx_los_%d"
#define MODULE_TXFAULT_FORMAT       "/sys/bus/i2c/devices/37-0062/module_tx_fault_%d"
#define MODULE_TXDISABLE_FORMAT     "/sys/bus/i2c/devices/37-0062/module_tx_disable_%d"
#define MODULE_RESET_CPLD2_FORMAT   "/sys/bus/i2c/devices/36-0061/module_reset_%d"
#define MODULE_RESET_CPLD3_FORMAT   "/sys/bus/i2c/devices/37-0062/module_reset_%d"
#define MODULE_LPMODE_CPLD2_FORMAT  "/sys/bus/i2c/devices/36-0061/module_lpmode_%d"
#define MODULE_LPMODE_CPLD3_FORMAT  "/sys/bus/i2c/devices/37-0062/module_lpmode_%d"

#define NUM_OF_SFP_PORT 34
static const int port_bus_index[NUM_OF_SFP_PORT] = {
	 2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17,
	18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
	34, 35
};

#define PORT_BUS_INDEX(port) (port_bus_index[port-1])

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

/* QSFP DD Specific*/
#define QSFP_DD_PAGE_ADMIN_INFO 0x0
#define QSFP_DD_PAGE_ADVERTISING 0x1
#define QSFP_DD_PAGE_LANE_CTRL 0x10
#define QSFP_DD_LOWER_OFFSET_STATUS 0x02
#define QSFP_DD_FLAT_MEM 0x80                          /* byte 0x02 bit 7 */
#define QSFP_DD_P01H_TX_DISABLE_SUPPORT 0x2

/*QSFP Specific*/
#define QSFP_DD_IDENTIFIER 0x18


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
	 * Ports {1, 34}
	 */
	int p;

	for(p = 1; p <= NUM_OF_SFP_PORT; p++)
		AIM_BITMAP_SET(bmap, p);

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

	switch (port) {
	case 1 ... 16:
		path = MODULE_PRESENT_CPLD2_FORMAT;
		break;
	case 17 ... 34:
		path = MODULE_PRESENT_CPLD3_FORMAT;
		break;
	default:
		return ONLP_STATUS_E_INVALID;
	}

	if (onlp_file_read_int(&present, path, port) < 0) {
		AIM_LOG_ERROR("Unable to read present status from port(%d)\r\n", 
				port);

		return ONLP_STATUS_E_INTERNAL;
	}

	return present;
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
	memset(data, 0, 256);

	if(onlp_file_read(data, 256, &size, MODULE_EEPROM_FORMAT, PORT_BUS_INDEX(port)) 
		!= ONLP_STATUS_OK) {
		AIM_LOG_ERROR("Unable to read eeprom from port(%d)\r\n", port);
		return ONLP_STATUS_E_INTERNAL;
	}

	if (size != 256) {
		AIM_LOG_ERROR("Unable to read eeprom from port(%d), size is different!\r\n", 
				port);
		return ONLP_STATUS_E_INTERNAL;
	}

	return ONLP_STATUS_OK;
}

int
onlp_sfpi_dom_read(int port, uint8_t data[256])
{
	FILE* fp;
	char file[64] = {0};

	sprintf(file, MODULE_EEPROM_FORMAT, PORT_BUS_INDEX(port));
	fp = fopen(file, "r");
	if(fp == NULL) {
		AIM_LOG_ERROR("Unable to open the eeprom device file of port(%d)",
				port);
		return ONLP_STATUS_E_INTERNAL;
	}

	if (fseek(fp, 256, SEEK_CUR) != 0) {
		fclose(fp);
		AIM_LOG_ERROR("Unable to set the file position indicator of port(%d)", 
				port);
		return ONLP_STATUS_E_INTERNAL;
	}

	int ret = fread(data, 1, 256, fp);
	fclose(fp);
	if (ret != 256) {
		AIM_LOG_ERROR("Unable to read the module_eeprom device file of port(%d)", 
				port);
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
	char *path = NULL;
    int present = 0;
    int identifier = 0;
	int eeprom_control;
	int status_byte = 0;

    VALIDATE_PORT(port);

	switch(control) {
	case ONLP_SFP_CONTROL_TX_DISABLE:
	case ONLP_SFP_CONTROL_TX_DISABLE_CHANNEL: {
		present = onlp_sfpi_is_present(port);
		/* read qsfp eeprom offset of tx disable if qsfp on the port */
		if(present == 1) {
			if (port >= SFP_PORT_MIN && port <= SFP_PORT_MAX) {
				if (onlp_file_write_int(value, MODULE_TXDISABLE_FORMAT, port) 
					< 0) {
					AIM_LOG_ERROR("Unable to write tx_disable status to port(%d)\r\n", 
							port);
					return ONLP_STATUS_E_INTERNAL;
				}
			}
			else { //QSFP

				identifier = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_IDENTIFIER);
				if(identifier < 0){
					AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): read identifier from eeprom fail\r\n", 
							port);
					return ONLP_STATUS_E_INTERNAL;
				}
				else if (identifier == QSFP_DD_IDENTIFIER) { /* QSFP DD */
					/* Flat-memory CMIS modules do not implement page 01h/10h */
					if ((status_byte = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_LOWER_OFFSET_STATUS)) < 0) {
						AIM_LOG_ERROR("Failed to read Status byte, unable to write tx_disable status to port(%d)\r\n", port);
						return ONLP_STATUS_E_INTERNAL;
					}
					else if (status_byte & QSFP_DD_FLAT_MEM) {
						AIM_LOG_ERROR("Setting tx_disable to port(%d) is not supported (flat-memory module)\r\n", port);
						return ONLP_STATUS_E_UNSUPPORTED;
					}
					else if(onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADVERTISING) <0 ){
						AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): write page to eeprom fail\r\n", 
								port);
						return ONLP_STATUS_E_INTERNAL;
					}

					eeprom_control = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_P01H_OFFSET_CONTROL_1);
					if(eeprom_control < 0){
						AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): read control from eeprom fail\r\n", port);

						if(onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADMIN_INFO) <0){
							AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): write page to eeprom fail\r\n", 
									port);
						}

						return ONLP_STATUS_E_INTERNAL;
					}
					if (eeprom_control & QSFP_DD_P01H_TX_DISABLE_SUPPORT){

						if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_BANK_SELECT, 0) < 0) {
							AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): write bank to eeprom fail\r\n", port);

							if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADMIN_INFO) < 0) {
								AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): write page to eeprom fail\r\n", port);
							}

							return ONLP_STATUS_E_INTERNAL;
						}

						if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_LANE_CTRL) < 0) {
							AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): write page to eeprom fail\r\n", port);

							if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADMIN_INFO) < 0) {
								AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): write page to eeprom fail\r\n", port);
							}

							return ONLP_STATUS_E_INTERNAL;
						}

						if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_P10H_OFFSET_OUTPUT_DISABLE_TX, value) < 0) {
							AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): write TX disable to eeprom fail\r\n", port);

							if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADMIN_INFO) < 0) {
								AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): write page to eeprom fail\r\n", port);
							}

							return ONLP_STATUS_E_INTERNAL;
						}
					} else {
						if(onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADMIN_INFO) <0){
							AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): write page to eeprom fail\r\n", port);
						}

						AIM_LOG_ERROR("Setting tx disable to port(%d) is not supported\r\n", port);
						return ONLP_STATUS_E_UNSUPPORTED;
					}

					if(onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADMIN_INFO) <0){
						AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): write page to eeprom fail\r\n", port);
						return ONLP_STATUS_E_INTERNAL;
					}
				} 
				else { /* QSFP 28 or QSFP+ */
					/* txdis valid bit(bit0-bit3), xxxx 1111 */
					value = value&0xf;
					if(onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_TXDIS, value) <0 ){
						AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): write TX disable to eeprom fail\r\n", 
								port);
						return ONLP_STATUS_E_INTERNAL;
					}

				}
			}
		}
		else {
			return ONLP_STATUS_E_INTERNAL;
		}

		return ONLP_STATUS_OK;
	}
	case ONLP_SFP_CONTROL_RESET_STATE: {
		VALIDATE_QSFP(port);

		path = (port < 17) ? MODULE_RESET_CPLD2_FORMAT
				   : MODULE_RESET_CPLD3_FORMAT;

		if (onlp_file_write_int(value, path, port) < 0) {
			AIM_LOG_ERROR("Unable to write reset status to port(%d)\r\n", 
					port);
			return ONLP_STATUS_E_INTERNAL;
		}

		return ONLP_STATUS_OK;
	}
	case ONLP_SFP_CONTROL_LP_MODE: {
		VALIDATE_QSFP(port);

		path = (port < 17) ? MODULE_LPMODE_CPLD2_FORMAT 
				   : MODULE_LPMODE_CPLD3_FORMAT;

		if (onlp_file_write_int(value, path, port) < 0) {
			AIM_LOG_ERROR("Unable to write LP mode to port(%d)\r\n", 
					port);
			return ONLP_STATUS_E_INTERNAL;
		}

		return ONLP_STATUS_OK;
	}
	default:
		return ONLP_STATUS_E_UNSUPPORTED;
	}

	return ONLP_STATUS_E_INTERNAL;
}

int
onlp_sfpi_control_get(int port, onlp_sfp_control_t control, int* value)
{
	char *path = NULL;
    int present = 0;
    int identifier = 0;
	int tx_disable;
	int status_byte = 0;
	int support_ctrls = 0;

	VALIDATE_PORT(port);

	switch(control) {
	case ONLP_SFP_CONTROL_RX_LOS: {
		*value = 0;
		if (port >= 33 && port <= 34) {
			if (onlp_file_read_int(value, MODULE_RXLOS_FORMAT, port) < 0) {
				AIM_LOG_ERROR("Unable to read rx_loss status from port(%d)\r\n", 
					port);
				return ONLP_STATUS_E_INTERNAL;
			}
		}

		return ONLP_STATUS_OK;
	}

	case ONLP_SFP_CONTROL_TX_FAULT: {
		VALIDATE_SFP(port);

		if (onlp_file_read_int(value, MODULE_TXFAULT_FORMAT, port) < 0) {
			AIM_LOG_ERROR("Unable to read tx_fault status from port(%d)\r\n", 
					port);
			return ONLP_STATUS_E_INTERNAL;
		}

		return ONLP_STATUS_OK;
	}

	case ONLP_SFP_CONTROL_TX_DISABLE: 
	case ONLP_SFP_CONTROL_TX_DISABLE_CHANNEL: {

        present = onlp_sfpi_is_present(port);
        /* read qsfp eeprom offset of tx disable if qsfp on the port */
        if(present == 1) {
            if (port >= SFP_PORT_MIN && port <= SFP_PORT_MAX) {
				if (onlp_file_read_int(value, MODULE_TXDISABLE_FORMAT, port) < 0) {
					AIM_LOG_ERROR("Unable to read tx_disabled status from port(%d)\r\n", 
							port);
					return ONLP_STATUS_E_INTERNAL;
				}
			}
			else {
				identifier = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_IDENTIFIER);
				if(identifier < 0){
					AIM_LOG_ERROR("Unable to read tx_disabled status from port(%d) : read identifier from eeprom fail\r\n", 
							port);
					return ONLP_STATUS_E_INTERNAL;
				}
				else if (identifier == QSFP_DD_IDENTIFIER) { /* QSFP DD */
					/* Flat-memory CMIS modules do not implement page 01h/10h */
					if ((status_byte = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_LOWER_OFFSET_STATUS)) < 0) {
						AIM_LOG_ERROR("Failed to read Status byte, unable to read tx_disabled status from port(%d)\r\n", port);
						return ONLP_STATUS_E_INTERNAL;
					}
					else if (status_byte & QSFP_DD_FLAT_MEM) {
						AIM_LOG_ERROR("Reading tx_disable from port(%d) is not supported (flat-memory module)\r\n", port);
						return ONLP_STATUS_E_UNSUPPORTED;
					}

					/* Switch to Advertising page to check TX_DISABLE support */
					if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADVERTISING) < 0) {
						AIM_LOG_ERROR("Unable to read tx_disabled status from port(%d) : write page to eeprom fail\r\n",
									port);
						return ONLP_STATUS_E_INTERNAL;
					}

					support_ctrls = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_P01H_OFFSET_CONTROL_1);
					if (support_ctrls < 0) {
						AIM_LOG_ERROR("Unable to read tx_disabled status from port(%d) : read support control from eeprom fail\r\n",
								port);
						if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADMIN_INFO) < 0) {
							AIM_LOG_ERROR("Unable to read tx_disabled status from port(%d) : write page to eeprom fail\r\n",
									port);
						}
						return ONLP_STATUS_E_INTERNAL;
					}

					if (!(support_ctrls & QSFP_DD_P01H_TX_DISABLE_SUPPORT)) {
						AIM_LOG_ERROR("Reading tx_disable from port(%d) is not supported\r\n", port);
						if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADMIN_INFO) < 0) {
							AIM_LOG_ERROR("Unable to read tx_disabled status from port(%d) : write page to eeprom fail\r\n",
									port);
						}
						return ONLP_STATUS_E_UNSUPPORTED;
					}

					if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_BANK_SELECT, 0) < 0) {
						AIM_LOG_ERROR("Unable to read tx_disabled status from port(%d) : write bank to eeprom fail\r\n",
									port);
						if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADMIN_INFO) < 0) {
							AIM_LOG_ERROR("Unable to read tx_disabled status from port(%d) : write page to eeprom fail\r\n",
									port);
						}
						return ONLP_STATUS_E_INTERNAL;
					}

					/* Switch to Lane Control page to read TX disable */
					if (onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_LANE_CTRL) < 0) {
						AIM_LOG_ERROR("Unable to read tx_disabled status from port(%d) : write page to eeprom fail\r\n",
									port);
						return ONLP_STATUS_E_INTERNAL;
					}

					tx_disable = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_P10H_OFFSET_OUTPUT_DISABLE_TX);
					if(tx_disable < 0){
						if(onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADMIN_INFO) <0){
							AIM_LOG_ERROR("Unable to read tx_disabled status from port(%d) : write page to eeprom fail\r\n", 
									port);
						}
						AIM_LOG_ERROR("Unable to read tx_disabled status from port(%d) : read TX disable from eeprom fail\r\n", 
								port);
						return ONLP_STATUS_E_INTERNAL;
					}
					*value = tx_disable;

					/* Restore page */
					if(onlp_sfpi_dev_writeb(port, PORT_EEPROM_DEVADDR, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADMIN_INFO) <0){
						AIM_LOG_ERROR("Unable to read tx_disabled status from port(%d) : write page to eeprom fail\r\n", 
								port);
						return ONLP_STATUS_E_INTERNAL;
					}
				}
				else { /* QSFP 28 or QSFP+ */
					tx_disable = onlp_sfpi_dev_readb(port, PORT_EEPROM_DEVADDR, QSFP_EEPROM_OFFSET_TXDIS);
					if(tx_disable < 0){
						AIM_LOG_ERROR("Unable to read tx_disabled status from port(%d) : read TX disable from eeprom fail\r\n", 
								port);
						return ONLP_STATUS_E_INTERNAL;
					}
					*value = tx_disable;
				}
			}
		}
		else {
			return ONLP_STATUS_E_INTERNAL;
		}

		return ONLP_STATUS_OK;
	}
	case ONLP_SFP_CONTROL_RESET_STATE: {
		VALIDATE_QSFP(port);

		path = (port < 17) ? MODULE_RESET_CPLD2_FORMAT 
				   : MODULE_RESET_CPLD3_FORMAT;

		if (onlp_file_read_int(value, path, port) < 0) {
			AIM_LOG_ERROR("Unable to read reset status from port(%d)\r\n", 
					port);
			return ONLP_STATUS_E_INTERNAL;
		}

		return ONLP_STATUS_OK;
	}
	case ONLP_SFP_CONTROL_LP_MODE: {
		VALIDATE_QSFP(port);

		path = (port < 17) ? MODULE_LPMODE_CPLD2_FORMAT 
				   : MODULE_LPMODE_CPLD3_FORMAT;

		if (onlp_file_read_int(value, path, port) < 0) {
			AIM_LOG_ERROR("Unable to read LP mode status from port(%d)\r\n", 
					port);
			return ONLP_STATUS_E_INTERNAL;
		}

		return ONLP_STATUS_OK;
	}
	default:
		return ONLP_STATUS_E_UNSUPPORTED;
	}

	return ONLP_STATUS_E_INTERNAL;
}

int
onlp_sfpi_denit(void)
{
	return ONLP_STATUS_OK;
}
