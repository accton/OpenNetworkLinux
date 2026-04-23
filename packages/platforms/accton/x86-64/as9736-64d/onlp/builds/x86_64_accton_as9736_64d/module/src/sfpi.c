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
#include "x86_64_accton_as9736_64d_int.h"
#include "x86_64_accton_as9736_64d_log.h"
#include "platform_lib.h"

#define SFP_PORT_MIN 64
#define SFP_PORT_MAX 65
#define QSFP_PORT_MIN 0
#define QSFP_PORT_MAX 63
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

#define UDB_PORT_EEPROM_FORMAT \
	"/sys/bus/platform/devices/pcie_udb_fpga_device.%d/eeprom"

#define LDB_PORT_EEPROM_FORMAT \
	"/sys/bus/platform/devices/pcie_ldb_fpga_device.%d/eeprom"

#define MODULE_PRESENT_FORMAT \
	"/sys/bus/platform/devices/as9736_64d_fpga/module_present_%d"
#define MODULE_RXLOS_FORMAT \
	"/sys/bus/platform/devices/as9736_64d_fpga/module_rx_los_%d"
#define MODULE_TXFAULT_FORMAT \
	"/sys/bus/platform/devices/as9736_64d_fpga/module_tx_fault_%d"
#define MODULE_TXDISABLE_FORMAT \
	"/sys/bus/platform/devices/as9736_64d_fpga/module_tx_disable_%d"
#define MODULE_RESET_FORMAT \
	"/sys/bus/platform/devices/as9736_64d_fpga/module_reset_%d"
#define MODULE_LPMODE_FORMAT \
	"/sys/bus/platform/devices/as9736_64d_fpga/module_lp_mode_%d"
#define MODULE_PRESENT_ALL_ATTR \
	"/sys/bus/platform/devices/as9736_64d_fpga/module_present_all"
#define MODULE_RXLOS_ALL_ATTR \
	"/sys/bus/platform/devices/as9736_64d_fpga/module_rx_los_all"

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
#define QSFP_DD_P01H_TX_DISABLE_SUPPORT 0x2


/* OSFP IDENTIFIER Specific*/
#define QSFP_DD_IDENTIFIER 0x18

#if 0
int sfp_map_bus[] = {17, 18, 19, 20, 21, 22, 23, 24,
		    25, 26, 27, 28, 29, 30, 31, 32,
		    33, 34, 35, 36, 37, 38, 39, 40,
		    41, 42, 43, 44, 45, 46, 47, 48,
		    49, 50};
#endif

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

#if 0
int onlp_sfpi_map_bus_index(int port)
{
	if (port < 0 || port >= 34)
		return ONLP_STATUS_E_INTERNAL;
	return sfp_map_bus[port];
}
#endif

int onlp_sfpi_bitmap_get(onlp_sfp_bitmap_t* bmap)
{
	/*
	 * Ports {0, 65}
	 */
	int p;

	for (p = 0; p < 66; p++)
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

	VALIDATE_PORT(port);

	if (onlp_file_read_int(&present, MODULE_PRESENT_FORMAT, (port+1)) < 0) {
		AIM_LOG_ERROR("Unable to read present status from port(%d)\r\n"
				, port);
		return ONLP_STATUS_E_INTERNAL;
	}

	return present;
}

int onlp_sfpi_presence_bitmap_get(onlp_sfp_bitmap_t* dst)
{
	int i = 1;
	int present = 0;

	for (i = 0; i <= CHASSIS_QSFP_COUNT + CHASSIS_SFP_COUNT-1; i++) {        
		present = onlp_sfpi_is_present(i);
		AIM_BITMAP_MOD(dst, i, (1 == present) ? 1 : 0);
	}

	return ONLP_STATUS_OK;
}

int onlp_sfpi_rx_los_bitmap_get(onlp_sfp_bitmap_t* dst)
{
#if 0
	uint32_t bytes[9];
	uint32_t *ptr = bytes;
	FILE* fp;

	int i = 0;

	AIM_BITMAP_CLR_ALL(dst);

	fp = fopen(MODULE_RXLOS_ALL_ATTR, "r");
	if (fp == NULL) {
		AIM_LOG_ERROR("Unable to open the module_rx_los_all device file.");
		return ONLP_STATUS_E_INTERNAL;
	}

	int count = fscanf(fp, "%x %x %x %x %x %x %x %x %x", ptr+3, ptr+2,
				ptr+1, ptr+0, ptr+7, ptr+6, ptr+5, ptr+4,
				ptr+8);
	fclose(fp);
	if (count != 9) {
		/* Likely a read timeout. */
		AIM_LOG_ERROR("Unable to read all fields from the module_rx_los_all device file.");
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
#endif
	//=========================================

	int i = 0;
	int rx_loss = 0;

	AIM_BITMAP_CLR_ALL(dst);

	for(i = 0; i < CHASSIS_QSFP_COUNT+CHASSIS_SFP_COUNT; i++) {

		if (i < CHASSIS_QSFP_COUNT) {
			AIM_BITMAP_MOD(dst, i, 0);
		} else {
			if (onlp_file_read_int(&rx_loss, MODULE_RXLOS_FORMAT, 
			(i+1)) < 0) {
				AIM_LOG_ERROR("Unable to read rxloss status from port(%d)\r\n"
					, i+1);
				return ONLP_STATUS_E_INTERNAL;
			}

			AIM_BITMAP_MOD(dst, i, (1 == rx_loss) ? 1 : 0);
		}		
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
	if (port < 0 || port > 65)
		return ONLP_STATUS_E_INTERNAL;
	memset(data, 0, 256);

	if (port <= 31) {
		if (onlp_file_read(data, 256, &size, UDB_PORT_EEPROM_FORMAT,
					port) != ONLP_STATUS_OK) {
			AIM_LOG_ERROR("Unable to read eeprom from port(%d)\r\n", port);
			return ONLP_STATUS_E_INTERNAL;
		}
	} else {
		if (onlp_file_read(data, 256, &size, LDB_PORT_EEPROM_FORMAT,
					port-32) != ONLP_STATUS_OK) {
			AIM_LOG_ERROR("Unable to read eeprom from port(%d)\r\n", port);
			return ONLP_STATUS_E_INTERNAL;
		}
	}
	

	if (size != 256) {
		AIM_LOG_ERROR("Unable to read eeprom from port(%d), size is different!\r\n", 
			      port);
		return ONLP_STATUS_E_INTERNAL;
	}

	return ONLP_STATUS_OK;
}

int onlp_sfpi_dom_read(int port, uint8_t data[256])
{
#if 0
	FILE* fp;
	char file[64] = {0};

	sprintf(file, PORT_EEPROM_FORMAT, onlp_sfpi_map_bus_index(port));
	fp = fopen(file, "r");
	if (fp == NULL) {
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
#endif
	return ONLP_STATUS_OK;
}

#define UDB_PORT_MAX    32

int read_byte(int offset, const char *fmt, ...)
{
    FILE *f;
    uint8_t value = 0;
	char path[100];
    va_list args;
    va_start(args, fmt);
    vsnprintf(path, sizeof(path), fmt, args);
    va_end(args);

    f = fopen(path, "rb");
    if (!f)
        return -1;

    if (fseek(f, offset, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }

    if (fread(&value, 1, 1, f) != 1) {
        fclose(f);
        return -1;
    }

    fclose(f);
    return value;
}

int write_byte(int offset, uint8_t value, const char *fmt, ...)
{
    FILE *f;
	char path[100];
    va_list args;
    va_start(args, fmt);
    vsnprintf(path, sizeof(path), fmt, args);
    va_end(args);

    f = fopen(path, "r+b");
    if (!f)
        return -1;

    if (fseek(f, offset, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }

    if (fwrite(&value, 1, 1, f) != 1) {
        fclose(f);
        return -1;
    }

    fclose(f);
    return 0;
}

int
onlp_sfpi_eeprom_readb(int port, uint8_t addr)
{
    const char* fmt;
    int real_port;

    /* Validate port */
    if (port < 0) {
        return ONLP_STATUS_E_PARAM;
    }

    /* Select EEPROM region */
    if (port < UDB_PORT_MAX) {
        fmt = UDB_PORT_EEPROM_FORMAT;
        real_port = port;
    } else {
        fmt = LDB_PORT_EEPROM_FORMAT;
        real_port = port - UDB_PORT_MAX;
    }

    return read_byte(addr, fmt, real_port);
}

int
onlp_sfpi_eeprom_writeb(int port, uint8_t addr, uint8_t value)
{
    const char* fmt;
    int real_port;

    /* Validate port */
    if (port < 0) {
        return ONLP_STATUS_E_PARAM;
    }

    if (port < UDB_PORT_MAX) {
        fmt = UDB_PORT_EEPROM_FORMAT;
        real_port = port;
    } else {
        fmt = LDB_PORT_EEPROM_FORMAT;
        real_port = port - UDB_PORT_MAX;
    }

    return write_byte(addr, value, fmt, real_port);
}

int onlp_sfpi_control_set(int port, onlp_sfp_control_t control, int value)
{
	int rv = ONLP_STATUS_E_INTERNAL;
	int present = 0;
	int identifier = 0;
	int eeprom_control;

	VALIDATE_PORT(port);

	switch(control) {
	case ONLP_SFP_CONTROL_TX_DISABLE:
	case ONLP_SFP_CONTROL_TX_DISABLE_CHANNEL:
		present = onlp_sfpi_is_present(port);
		if (present == 1) {
			if (port >= SFP_PORT_MIN && port <= SFP_PORT_MAX) { //SFP
				if (onlp_file_write_int(value, MODULE_TXDISABLE_FORMAT,
							(port + 1)) < 0) {
					AIM_LOG_ERROR("Unable to write tx_disable status to port(%d)\r\n",
							port);
					rv = ONLP_STATUS_E_INTERNAL;
				}
				else {
					rv = ONLP_STATUS_OK;
				}
			}
			else { //QSFP
				identifier = onlp_sfpi_eeprom_readb(port, QSFP_EEPROM_OFFSET_IDENTIFIER);
				if (identifier < 0) {
					AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): read identifier from eeprom fail\r\n",
							port);
					rv = ONLP_STATUS_E_INTERNAL;
				}
				else if (identifier == QSFP_DD_IDENTIFIER) { /* QSFP DD */
					if (onlp_sfpi_eeprom_writeb(port, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADVERTISING) < 0) {
						AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): write page to eeprom fail\r\n",
								port);
						rv = ONLP_STATUS_E_INTERNAL;
					}
					else {
						eeprom_control = onlp_sfpi_eeprom_readb(port, QSFP_DD_EEPROM_P01H_OFFSET_CONTROL_1);
						if (eeprom_control < 0) {
							AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): read control from eeprom fail\r\n",
									port);
							rv = ONLP_STATUS_E_INTERNAL;
						}
						else if (eeprom_control & QSFP_DD_P01H_TX_DISABLE_SUPPORT) {
							if (onlp_sfpi_eeprom_writeb(port, QSFP_DD_EEPROM_OFFSET_BANK_SELECT, 0) < 0) {
								AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): write bank to eeprom fail\r\n",
										port);
								rv = ONLP_STATUS_E_INTERNAL;
							}
							else if (onlp_sfpi_eeprom_writeb(port, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_LANE_CTRL) < 0) {
								AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): write page to eeprom fail\r\n",
										port);
								rv = ONLP_STATUS_E_INTERNAL;
							}
							else if (onlp_sfpi_eeprom_writeb(port, QSFP_DD_EEPROM_P10H_OFFSET_OUTPUT_DISABLE_TX, value) < 0) {
								AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): write TX disable to eeprom fail\r\n",
										port);
								rv = ONLP_STATUS_E_INTERNAL;
							}
							else {
								rv = ONLP_STATUS_OK;
							}
						} else {
							AIM_LOG_ERROR("Setting tx disable to port(%d) is not supported\r\n", port);
							rv = ONLP_STATUS_E_UNSUPPORTED;
						}
						if (onlp_sfpi_eeprom_writeb(port, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADMIN_INFO) < 0) {
							AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): write page to eeprom fail\r\n",
									port);
							rv = ONLP_STATUS_E_INTERNAL;
						}
					}
				} else { /* QSFP 28 or QSFP+ */
					/* txdis valid bit(bit0-bit3), xxxx 1111 */
					value = value & 0xf;

					if (onlp_sfpi_eeprom_writeb(port, QSFP_EEPROM_OFFSET_TXDIS, value) < 0) {
						AIM_LOG_ERROR("Unable to write tx_disable status to port(%d): write TX disable to eeprom fail\r\n",
								port);
						rv = ONLP_STATUS_E_INTERNAL;
					}
					else {
						rv = ONLP_STATUS_OK;
					}
				}
			}
		}
		else {
			AIM_LOG_ERROR("Unable to write tx_disabled status to port(%d): module is not present\r\n", port);
			rv = ONLP_STATUS_E_INTERNAL;
		}
		break;

	case ONLP_SFP_CONTROL_RESET:
		VALIDATE_QSFP(port);

		if (onlp_file_write_int(value, MODULE_RESET_FORMAT,
					(port + 1)) < 0) {
			AIM_LOG_ERROR("Unable to write reset status to port(%d)\r\n",
					port);
			rv = ONLP_STATUS_E_INTERNAL;
		}
		else {
			rv = ONLP_STATUS_OK;
		}
		break;

	case ONLP_SFP_CONTROL_LP_MODE:
		VALIDATE_QSFP(port);

		if (onlp_file_write_int(value, MODULE_LPMODE_FORMAT,
					(port + 1)) < 0) {
			AIM_LOG_ERROR("Unable to write LP mode status to port(%d)\r\n",
					port);
			rv = ONLP_STATUS_E_INTERNAL;
		}
		else {
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
	int rv = ONLP_STATUS_E_INTERNAL;
	int present = 0;
	int identifier = 0;
	int tx_dis = 0;

	VALIDATE_PORT(port);

	switch (control) {
	case ONLP_SFP_CONTROL_RX_LOS:
		VALIDATE_SFP(port);
		if (onlp_file_read_int(value, MODULE_RXLOS_FORMAT,
						(port+1)) < 0) {
			AIM_LOG_ERROR("Unable to read rx_loss status from port(%d)\r\n",
						port);
			rv = ONLP_STATUS_E_INTERNAL;
		}
		else {
			rv = ONLP_STATUS_OK;
		}
		break;

	case ONLP_SFP_CONTROL_TX_FAULT:
		VALIDATE_SFP(port);
		if (onlp_file_read_int(value, MODULE_TXFAULT_FORMAT,
						(port+1)) < 0) {
			AIM_LOG_ERROR("Unable to read tx_fault status from port(%d)\r\n",
						port);
			rv = ONLP_STATUS_E_INTERNAL;
		}
		else {
			rv = ONLP_STATUS_OK;
		}
		break;

	case ONLP_SFP_CONTROL_TX_DISABLE:
	case ONLP_SFP_CONTROL_TX_DISABLE_CHANNEL:
		present = onlp_sfpi_is_present(port);
		if (present == 1) {
			if (port >= SFP_PORT_MIN && port <= SFP_PORT_MAX) {
				if (onlp_file_read_int(value, MODULE_TXDISABLE_FORMAT,
							(port+1)) < 0) {
					AIM_LOG_ERROR("Unable to read tx_disabled status from port(%d)\r\n",
							port);
					rv = ONLP_STATUS_E_INTERNAL;
				}
				else {
					rv = ONLP_STATUS_OK;
				}
			}
			else {
				identifier = onlp_sfpi_eeprom_readb(port, QSFP_EEPROM_OFFSET_IDENTIFIER);
				if (identifier < 0) {
					AIM_LOG_ERROR("Unable to read tx_disabled status from port(%d): read identifier from eeprom fail\r\n",
							port);
					rv = ONLP_STATUS_E_INTERNAL;
				}
				else if (identifier == QSFP_DD_IDENTIFIER) { /* QSFP DD */
					if (onlp_sfpi_eeprom_writeb(port, QSFP_DD_EEPROM_OFFSET_BANK_SELECT, 0) < 0) {
						AIM_LOG_ERROR("Unable to read tx_disable status from port(%d): write bank to eeprom fail\r\n",
								port);
						rv = ONLP_STATUS_E_INTERNAL;
					}
					else if (onlp_sfpi_eeprom_writeb(port, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_LANE_CTRL) < 0) {
						AIM_LOG_ERROR("Unable to read tx_disable status from port(%d): write page to eeprom fail\r\n",
								port);
						rv = ONLP_STATUS_E_INTERNAL;
					}
					else {
						tx_dis = onlp_sfpi_eeprom_readb(port, QSFP_DD_EEPROM_P10H_OFFSET_OUTPUT_DISABLE_TX);
						if (tx_dis < 0) {
							AIM_LOG_ERROR("Unable to read tx_disable status from port(%d): read TX disable from eeprom fail\r\n",
									port);
							rv = ONLP_STATUS_E_INTERNAL;
						}
						else {
							*value = tx_dis;
							rv = ONLP_STATUS_OK;
						}
					}
					if (onlp_sfpi_eeprom_writeb(port, QSFP_DD_EEPROM_OFFSET_PAGE_SELECT, QSFP_DD_PAGE_ADMIN_INFO) < 0) {
						AIM_LOG_ERROR("Unable to read tx_disable status from port(%d): write page to eeprom fail\r\n",
								port);
						rv = ONLP_STATUS_E_INTERNAL;
					}
				}
				else { /* QSFP 28 or QSFP+ */
					tx_dis = onlp_sfpi_eeprom_readb(port, QSFP_EEPROM_OFFSET_TXDIS);
					if (tx_dis < 0) {
						AIM_LOG_ERROR("Unable to read tx_disable status from port(%d): read TX disable from eeprom fail\r\n",
								port);
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
			AIM_LOG_ERROR("Unable to read tx_disabled status from port(%d): module is not present\r\n", port);
			rv = ONLP_STATUS_E_INTERNAL;
		}
		break;

	case ONLP_SFP_CONTROL_RESET:
		VALIDATE_QSFP(port);

		if (onlp_file_read_int(value, MODULE_RESET_FORMAT,
					(port + 1)) < 0) {
			AIM_LOG_ERROR("Unable to read reset status from port(%d)\r\n",
					port);
			rv = ONLP_STATUS_E_INTERNAL;
		}
		else {
			rv = ONLP_STATUS_OK;
		}
		break;

	case ONLP_SFP_CONTROL_LP_MODE:
		VALIDATE_QSFP(port);

		if (onlp_file_read_int(value, MODULE_LPMODE_FORMAT,
					(port + 1)) < 0) {
			AIM_LOG_ERROR("Unable to read LP mode status from port(%d)\r\n",
					port);
			rv = ONLP_STATUS_E_INTERNAL;
		}
		else {
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
