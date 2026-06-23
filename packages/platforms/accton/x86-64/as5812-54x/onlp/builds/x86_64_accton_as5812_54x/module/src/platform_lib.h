/************************************************************
 * <bsn.cl fy=2014 v=onl>
 *
 *           Copyright 2014 Big Switch Networks, Inc.
 *           Copyright 2015 Accton Technology Corporation.
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
#ifndef __PLATFORM_LIB_H__
#define __PLATFORM_LIB_H__

#include "x86_64_accton_as5812_54x_log.h"

#define PSU1_ID 1
#define PSU2_ID 2

#define CHASSIS_FAN_COUNT     5
#define CHASSIS_THERMAL_COUNT 4 
#define CHASSIS_LED_COUNT     10 

#define PSU1_AC_PMBUS_PREFIX            "/sys/bus/i2c/devices/57-003c/"	/* Compuware psu */
#define PSU2_AC_PMBUS_PREFIX            "/sys/bus/i2c/devices/58-003f/" /* Compuware psu */
#define PSU1_AC_3YPOWER_PMBUS_PREFIX    "/sys/bus/i2c/devices/57-0058/" /* 3YPower psu */
#define PSU2_AC_3YPOWER_PMBUS_PREFIX    "/sys/bus/i2c/devices/58-005b/" /* 3YPower psu */

#define PSU1_AC_EEPROM_PREFIX "/sys/bus/i2c/devices/57-0038/"
#define PSU1_DC_EEPROM_PREFIX "/sys/bus/i2c/devices/57-0050/"
#define PSU2_AC_EEPROM_PREFIX "/sys/bus/i2c/devices/58-003b/"
#define PSU2_DC_EEPROM_PREFIX "/sys/bus/i2c/devices/58-0053/"
#define PSU1_AC_3YPOWER_EEPROM_PREFIX "/sys/bus/i2c/devices/57-0050/"
#define PSU2_AC_3YPOWER_EEPROM_PREFIX "/sys/bus/i2c/devices/58-0053/"

#define PSU1_AC_EEPROM_NODE(node) PSU1_AC_EEPROM_PREFIX#node
#define PSU1_DC_EEPROM_NODE(node) PSU1_DC_EEPROM_PREFIX#node
#define PSU2_AC_EEPROM_NODE(node) PSU2_AC_EEPROM_PREFIX#node
#define PSU2_DC_EEPROM_NODE(node) PSU2_DC_EEPROM_PREFIX#node
#define PSU1_AC_3YPOWER_EEPROM_NODE(node) PSU1_AC_3YPOWER_EEPROM_PREFIX#node
#define PSU2_AC_3YPOWER_EEPROM_NODE(node) PSU2_AC_3YPOWER_EEPROM_PREFIX#node

#define IDPROM_PATH_1 "/sys/bus/i2c/devices/0-0057/eeprom"
#define IDPROM_PATH_2 "/sys/bus/i2c/devices/1-0057/eeprom"

int deviceNodeWriteInt(char *filename, int value, int data_len);
int deviceNodeReadBinary(char *filename, char *buffer, int buf_size, int data_len);
int deviceNodeReadString(char *filename, char *buffer, int buf_size, int data_len);

typedef enum psu_type {
    PSU_TYPE_UNKNOWN,
    PSU_TYPE_AC_COMPUWARE_F2B,
    PSU_TYPE_AC_COMPUWARE_B2F,
    PSU_TYPE_AC_3YPOWER_F2B,
    PSU_TYPE_AC_3YPOWER_B2F,
    PSU_TYPE_DC_48V_F2B,
    PSU_TYPE_DC_48V_B2F
} psu_type_t;

psu_type_t get_psu_type(int id, char* modelname, int modelname_len);
int psu_serial_number_get(int id, psu_type_t psu_type, char *serial, int serial_len);
int psu_status_info_get(int id, int is_ac, char *node, int *value);
int psu_ym2401_pmbus_info_get(int id, char *node, int *value);
int psu_ym2401_pmbus_info_set(int id, char *node, int value);

/*
 * Resolve the i2c bus number that hosts the three CPLDs (0x60/0x61/0x62).
 * AS5812 wires the CPLDs to the i801 SMBus controller, whose i2c index
 * shifts between kernels: 4.14 enumerated i801 as i2c-0, 6.12 lands iSMT
 * first and pushes i801 to i2c-1. The result is cached after the first
 * successful resolution. Returns -1 if no CPLD at 0x60 is found on any
 * bus 0..15. Not thread-safe; the first caller initialises the cache.
 */
int as5812_54x_cpld_bus(void);

#define PSU_STATUS_PRESENT    1
#define PSU_STATUS_POWER_GOOD 1

#define DEBUG_MODE 0

#if (DEBUG_MODE == 1)
	#define DEBUG_PRINT(fmt, args...)                                        \
		printf("%s:%s[%d]: " fmt "\r\n", __FILE__, __FUNCTION__, __LINE__, ##args)
#else
	#define DEBUG_PRINT(fmt, args...)
#endif

#define AIM_FREE_IF_PTR(p) \
    do \
    { \
        if (p) { \
            aim_free(p); \
            p = NULL; \
        } \
    } while (0)

#endif  /* __PLATFORM_LIB_H__ */
