/************************************************************
 * <bsn.cl fy=2014 v=onl>
 *
 *           Copyright 2014 Big Switch Networks, Inc.
 *           Copyright 2014 Accton Technology Corporation.
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

#include <unistd.h>
#include <pthread.h>
#include "x86_64_accton_as9957_32db_log.h"

#define CHASSIS_FAN_COUNT      8
#define CHASSIS_THERMAL_COUNT  25
#define CHASSIS_LED_COUNT      6
#define CHASSIS_PSU_COUNT      2
#define NUM_OF_THERMAL_PER_PSU 3
#define PSU_NODE_MAX_PATH_LEN 64

#define PSU1_ID 1
#define PSU2_ID 2
#define PSU_STATUS_PRESENT 1
#define PSU_STATUS_POWER_GOOD 1

#define PSU_SYSFS_FORMAT   "/sys/devices/platform/as9957_32db_psu*psu%d_%s"
#define PSU_SYSFS_FORMAT_1 "/sys/devices/platform/as9957_32db_psu/hwmon/hwmon%d/%s"
#define FAN_SYSFS_FORMAT   "/sys/devices/platform/as9957_32db_fan*"
#define FAN_SYSFS_FORMAT_1 "/sys/devices/platform/as9957_32db_fan/hwmon/hwmon%d/%s"
#define SYS_LED_PATH   "/sys/devices/platform/as9957_32db_led/"
#define IDPROM_PATH "/sys/devices/platform/as9957_32db_sys/eeprom"
#define CPLD_REG_PATH "/sys/devices/platform/as9957_32db_fpga/cpld%d_reg"
#define BMC_THERMAL_DATA_PATH   "/sys/devices/platform/as9957_32db_sys/bmc_thermal_data"
#define FPGA_VER_PATH  "/sys/devices/platform/as9957_32db_sys/fpga_version"
#define BMC_VER1_PATH  "/sys/bus/platform/drivers/ipmi_si/IPI0001:00/bmc/firmware_revision"
#define BMC_VER2_PATH  "/sys/bus/platform/drivers/ipmi_si/IPI0001:00/bmc/aux_firmware_revision"
#define BIOS_VER_PATH  "/sys/devices/virtual/dmi/id/bios_version"

enum onlp_thermal_id {
    THERMAL_RESERVED = 0,
    THERMAL_CPU_CORE,
    THERMAL_1_ON_MAIN_BOARD,
    THERMAL_2_ON_MAIN_BOARD,
    THERMAL_3_ON_MAIN_BOARD,
    THERMAL_4_ON_MAIN_BOARD,
    THERMAL_5_ON_MAIN_BOARD,
    THERMAL_6_ON_MAIN_BOARD,
    THERMAL_7_ON_MAIN_BOARD,
    THERMAL_8_ON_MAIN_BOARD,
    THERMAL_9_ON_MAIN_BOARD,
    THERMAL_10_ON_MAIN_BOARD,
    THERMAL_11_ON_MAIN_BOARD,
    THERMAL_12_ON_MAIN_BOARD,
    THERMAL_13_ON_MAIN_BOARD,
    THERMAL_14_ON_MAIN_BOARD,
    THERMAL_15_ON_MAIN_BOARD,
    THERMAL_16_ON_MAIN_BOARD,
    THERMAL_17_ON_MAIN_BOARD,
    THERMAL_18_ON_MAIN_BOARD,
    THERMAL_19_ON_MAIN_BOARD,
    THERMAL_20_ON_MAIN_BOARD,
    THERMAL_21_ON_MAIN_BOARD,
    THERMAL_22_ON_MAIN_BOARD,
    THERMAL_23_ON_MAIN_BOARD,
    THERMAL_24_ON_MAIN_BOARD,
    THERMAL_1_ON_PSU1,
    THERMAL_2_ON_PSU1,
    THERMAL_3_ON_PSU1,
    THERMAL_1_ON_PSU2,
    THERMAL_2_ON_PSU2,
    THERMAL_3_ON_PSU2,
    THERMAL_COUNT
};

typedef enum psu_type {
    PSU_TYPE_DC12,
    PSU_TYPE_AC,
    NUM_OF_PSU_TYPE,
} psu_type_t;

enum onlp_led_id {
    LED_LOC = 1,
    LED_DIAG,
    LED_PSU1,
    LED_PSU2,
    LED_FAN,
    LED_ALARM
};

enum onlp_fan_dir {
    FAN_DIR_F2B,
    FAN_DIR_B2F,
    FAN_DIR_COUNT
};

enum onlp_fan_dir onlp_get_fan_dir(int fid);
int onlp_get_psu_hwmon_idx(int pid);
int onlp_get_fan_hwmon_idx(void);
psu_type_t get_psu_type(int id, char* modelname, int modelname_len);
int psu_status_info_get(int id, char *node, int *value);

#define AIM_FREE_IF_PTR(p) \
    do \
    { \
        if (p) { \
            aim_free(p); \
            p = NULL; \
        } \
    } while (0)

#endif  /* __PLATFORM_LIB_H__ */
