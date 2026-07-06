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
 * Thermal Sensor Platform Implementation.
 *
 ***********************************************************/
#include <onlplib/file.h>
#include <onlp/platformi/thermali.h>
#include "platform_lib.h"

#define VALIDATE(_id)                           \
    do {                                        \
        if (!ONLP_OID_IS_THERMAL(_id)) {        \
            return ONLP_STATUS_E_INVALID;       \
        }                                       \
    } while (0)

enum onlp_thermal_id {
    THERMAL_RESERVED = 0,
    THERMAL_CPU_CORE,
    THERMAL_1_ON_CARRIER_BOARD,
    THERMAL_2_ON_CARRIER_BOARD,
    THERMAL_1_ON_MAIN_BOARD,
    THERMAL_2_ON_MAIN_BOARD,
    THERMAL_3_ON_MAIN_BOARD,
    THERMAL_4_ON_MAIN_BOARD,
    THERMAL_5_ON_MAIN_BOARD,
    THERMAL_6_ON_MAIN_BOARD,
    THERMAL_1_ON_FAN_BOARD,
    THERMAL_2_ON_FAN_BOARD,
    THERMAL_TMP464_LO,
    THERMAL_TMP464_MAC1,
    THERMAL_TMP464_MAC2,
    THERMAL_TMP464_MAC3,
    THERMAL_1_ON_PSU1,
    THERMAL_2_ON_PSU1,
    THERMAL_3_ON_PSU1,
    THERMAL_1_ON_PSU2,
    THERMAL_2_ON_PSU2,
    THERMAL_3_ON_PSU2,
    THERMAL_1_ON_PSU3,
    THERMAL_2_ON_PSU3,
    THERMAL_3_ON_PSU3,
    THERMAL_1_ON_PSU4,
    THERMAL_2_ON_PSU4,
    THERMAL_3_ON_PSU4,
    THERMAL_COUNT,
};

static char *cpu_coretemp_files[] = {
    "/sys/devices/platform/coretemp.0*temp1_input",
    "/sys/devices/platform/coretemp.0*temp2_input",
    "/sys/devices/platform/coretemp.0*temp3_input",
    "/sys/devices/platform/coretemp.0*temp4_input",
    "/sys/devices/platform/coretemp.0*temp5_input",
    NULL,
};

static char *devfiles[] = {
    [THERMAL_RESERVED]          = NULL,
    [THERMAL_CPU_CORE]          = NULL,
    [THERMAL_1_ON_CARRIER_BOARD] = "/sys/devices/platform/as1817_64o_thermal*temp1_input",
    [THERMAL_2_ON_CARRIER_BOARD] = "/sys/devices/platform/as1817_64o_thermal*temp2_input",
    [THERMAL_1_ON_MAIN_BOARD]   = "/sys/devices/platform/as1817_64o_thermal*temp3_input",
    [THERMAL_2_ON_MAIN_BOARD]   = "/sys/devices/platform/as1817_64o_thermal*temp4_input",
    [THERMAL_3_ON_MAIN_BOARD]   = "/sys/devices/platform/as1817_64o_thermal*temp5_input",
    [THERMAL_4_ON_MAIN_BOARD]   = "/sys/devices/platform/as1817_64o_thermal*temp6_input",
    [THERMAL_5_ON_MAIN_BOARD]   = "/sys/devices/platform/as1817_64o_thermal*temp7_input",
    [THERMAL_6_ON_MAIN_BOARD]   = "/sys/devices/platform/as1817_64o_thermal*temp8_input",
    [THERMAL_1_ON_FAN_BOARD]    = "/sys/devices/platform/as1817_64o_thermal*temp9_input",
    [THERMAL_2_ON_FAN_BOARD]    = "/sys/devices/platform/as1817_64o_thermal*temp10_input",
    [THERMAL_TMP464_LO]         = "/sys/devices/platform/as1817_64o_thermal*temp11_input",
    [THERMAL_TMP464_MAC1]       = "/sys/devices/platform/as1817_64o_thermal*temp12_input",
    [THERMAL_TMP464_MAC2]       = "/sys/devices/platform/as1817_64o_thermal*temp13_input",
    [THERMAL_TMP464_MAC3]       = "/sys/devices/platform/as1817_64o_thermal*temp14_input",
    [THERMAL_1_ON_PSU1]         = "/sys/devices/platform/as1817_64o_psu.0*psu_temp1_input",
    [THERMAL_2_ON_PSU1]         = "/sys/devices/platform/as1817_64o_psu.0*psu_temp2_input",
    [THERMAL_3_ON_PSU1]         = "/sys/devices/platform/as1817_64o_psu.0*psu_temp3_input",
    [THERMAL_1_ON_PSU2]         = "/sys/devices/platform/as1817_64o_psu.1*psu_temp1_input",
    [THERMAL_2_ON_PSU2]         = "/sys/devices/platform/as1817_64o_psu.1*psu_temp2_input",
    [THERMAL_3_ON_PSU2]         = "/sys/devices/platform/as1817_64o_psu.1*psu_temp3_input",
    [THERMAL_1_ON_PSU3]         = "/sys/devices/platform/as1817_64o_psu.2*psu_temp1_input",
    [THERMAL_2_ON_PSU3]         = "/sys/devices/platform/as1817_64o_psu.2*psu_temp2_input",
    [THERMAL_3_ON_PSU3]         = "/sys/devices/platform/as1817_64o_psu.2*psu_temp3_input",
    [THERMAL_1_ON_PSU4]         = "/sys/devices/platform/as1817_64o_psu.3*psu_temp1_input",
    [THERMAL_2_ON_PSU4]         = "/sys/devices/platform/as1817_64o_psu.3*psu_temp2_input",
    [THERMAL_3_ON_PSU4]         = "/sys/devices/platform/as1817_64o_psu.3*psu_temp3_input",
};

#define THERMAL_CAPS (ONLP_THERMAL_CAPS_GET_TEMPERATURE | \
                      ONLP_THERMAL_CAPS_GET_WARNING_THRESHOLD | \
                      ONLP_THERMAL_CAPS_GET_ERROR_THRESHOLD | \
                      ONLP_THERMAL_CAPS_GET_SHUTDOWN_THRESHOLD)

static onlp_thermal_info_t tinfo[] = {
    { }, /* Not used */
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_CPU_CORE), "CPU Core", 0 },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 90000, 100000, 105000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_1_ON_CARRIER_BOARD), "CB RearCenter(0x48)", 0 },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 67000, 72000, 77000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_2_ON_CARRIER_BOARD), "CB FrontLeft(0x49)", 0 },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 67000, 72000, 77000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_1_ON_MAIN_BOARD), "MB FrontRight(0x48)", 0 },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 67000, 72000, 77000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_2_ON_MAIN_BOARD), "MB RearRight(0x49)", 0 },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 65000, 70000, 75000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_3_ON_MAIN_BOARD), "MB RearCenter(0x4a)", 0 },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 64000, 69000, 74000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_4_ON_MAIN_BOARD), "MB RearLeft(0x4b)", 0 },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 67000, 72000, 77000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_5_ON_MAIN_BOARD), "MB RearCenter(0x4c)", 0 },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 62000, 67000, 72000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_6_ON_MAIN_BOARD), "RJ45 FrontLeft(0x4d)", 0 },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 64000, 69000, 74000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_1_ON_FAN_BOARD), "FB Top RearCenter(0x4d)", 0 },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 55000, 60000, 65000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_2_ON_FAN_BOARD), "FB Bottom RearCenter(0x4d)", 0 },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 55000, 60000, 65000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_TMP464_LO), "TMP464 Lo(0x48)", 0 },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 75000, 80000, 85000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_TMP464_MAC1), "TMP464 MAC1(0x48)", 0 },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 95000, 100000, 105000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_TMP464_MAC2), "TMP464 MAC2(0x48)", 0 },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 95000, 100000, 105000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_TMP464_MAC3), "TMP464 MAC3(0x48)", 0 },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 95000, 100000, 105000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_1_ON_PSU1), "PSU-1 Thermal 1", ONLP_PSU_ID_CREATE(PSU1_ID) },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 78000, 83000, 88000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_2_ON_PSU1), "PSU-1 Thermal 2", ONLP_PSU_ID_CREATE(PSU1_ID) },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 78000, 83000, 88000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_3_ON_PSU1), "PSU-1 Thermal 3", ONLP_PSU_ID_CREATE(PSU1_ID) },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 78000, 83000, 88000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_1_ON_PSU2), "PSU-2 Thermal 1", ONLP_PSU_ID_CREATE(PSU2_ID) },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 78000, 83000, 88000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_2_ON_PSU2), "PSU-2 Thermal 2", ONLP_PSU_ID_CREATE(PSU2_ID) },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 78000, 83000, 88000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_3_ON_PSU2), "PSU-2 Thermal 3", ONLP_PSU_ID_CREATE(PSU2_ID) },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 78000, 83000, 88000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_1_ON_PSU3), "PSU-3 Thermal 1", ONLP_PSU_ID_CREATE(PSU3_ID) },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 78000, 83000, 88000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_2_ON_PSU3), "PSU-3 Thermal 2", ONLP_PSU_ID_CREATE(PSU3_ID) },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 78000, 83000, 88000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_3_ON_PSU3), "PSU-3 Thermal 3", ONLP_PSU_ID_CREATE(PSU3_ID) },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 78000, 83000, 88000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_1_ON_PSU4), "PSU-4 Thermal 1", ONLP_PSU_ID_CREATE(PSU4_ID) },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 78000, 83000, 88000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_2_ON_PSU4), "PSU-4 Thermal 2", ONLP_PSU_ID_CREATE(PSU4_ID) },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 78000, 83000, 88000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_3_ON_PSU4), "PSU-4 Thermal 3", ONLP_PSU_ID_CREATE(PSU4_ID) },
        ONLP_THERMAL_STATUS_PRESENT, THERMAL_CAPS, 0, { 78000, 83000, 88000 }
    },
};

int onlp_thermali_init(void)
{
    return ONLP_STATUS_OK;
}

int onlp_thermali_info_get(onlp_oid_t id, onlp_thermal_info_t *info)
{
    int tid;

    VALIDATE(id);
    tid = ONLP_OID_ID_GET(id);

    if (tid <= THERMAL_RESERVED || tid >= THERMAL_COUNT)
        return ONLP_STATUS_E_INVALID;

    *info = tinfo[tid];

    if (tid == THERMAL_CPU_CORE) {
        return onlp_file_read_int_max(&info->mcelsius, cpu_coretemp_files);
    }

    return onlp_file_read_int(&info->mcelsius, devfiles[tid]);
}
