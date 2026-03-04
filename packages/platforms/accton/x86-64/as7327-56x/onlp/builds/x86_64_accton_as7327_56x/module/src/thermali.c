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
 * Thermal Sensor Platform Implementation.
 *
 ***********************************************************/
//#include <unistd.h>
#include <onlplib/file.h>
#include <onlp/platformi/thermali.h>
#include "platform_lib.h"

#define THERMAL_PATH_FORMAT         "/sys/bus/i2c/devices/%s/*temp1_input"
#define THERMAL_BMC_BASE_PATH       "/sys/bus/platform/devices/as7327_56x_thermal_bmc/hwmon/*temp%s_input"

#define PSU_THERMAL_PATH_FORMAT     "/sys/bus/i2c/devices/%s/*psu_temp%d_input"
#define PSU1_THERMAL_BMC_BASE_PATH  "/sys/bus/platform/devices/as7327_56x_psu_bmc.0/hwmon/*psu_temp%d_input"
#define PSU2_THERMAL_BMC_BASE_PATH  "/sys/bus/platform/devices/as7327_56x_psu_bmc.1/hwmon/*psu_temp%d_input"

#define VALIDATE(_id)                           \
    do {                                        \
        if(!ONLP_OID_IS_THERMAL(_id)) {         \
            return ONLP_STATUS_E_INVALID;       \
        }                                       \
    } while(0)

enum onlp_thermal_id
{
    THERMAL_RESERVED = 0,
    THERMAL_CPU_CORE,
    THERMAL_1_ON_MAIN_BOARD,
    THERMAL_2_ON_MAIN_BOARD,
    THERMAL_3_ON_MAIN_BOARD,
    THERMAL_1_ON_PSU1,
    THERMAL_2_ON_PSU1,
    THERMAL_3_ON_PSU1,
    THERMAL_1_ON_PSU2,
    THERMAL_2_ON_PSU2,
    THERMAL_3_ON_PSU2,
};

static char* directory[] =  /* must map with onlp_thermal_id */
{
    NULL,
    NULL,                  /* CPU_CORE files          */
    "7-004c",              /* Thermal 1 on main board */
    "7-004b",              /* Thermal 2 on main board */
    "7-004a",              /* Thermal 3 on main board */
    "1-005a",              /* Thermal 1 on PSU1       */
    "1-005a",              /* Thermal 2 on PSU1       */
    "1-005a",              /* Thermal 3 on PSU1       */
    "2-0059",              /* Thermal 1 on PSU2       */
    "2-0059",              /* Thermal 2 on PSU2       */
    "2-0059",              /* Thermal 3 on PSU2       */
};

static char* directory_bmc[] =  /* must map with onlp_thermal_id */
{
    NULL,
    NULL,                  /* CPU_CORE files          */
    "1",                   /* Thermal 1 on main board */
    "2",                   /* Thermal 2 on main board */
    "3",                   /* Thermal 3 on main board */
    NULL,                  /* Thermal 1 on PSU1       */
    NULL,                  /* Thermal 2 on PSU1       */
    NULL,                  /* Thermal 3 on PSU1       */
    NULL,                  /* Thermal 1 on PSU2       */
    NULL,                  /* Thermal 2 on PSU2       */
    NULL,                  /* Thermal 3 on PSU2       */
};

static char* cpu_coretemp_files[] =
    {
        "/sys/devices/platform/coretemp.0*temp2_input",
        "/sys/devices/platform/coretemp.0*temp3_input",
        "/sys/devices/platform/coretemp.0*temp4_input",
        "/sys/devices/platform/coretemp.0*temp5_input",
        NULL,
    };

/* Static values */
static onlp_thermal_info_t linfo[] = {
    { }, /* Not used */
    { { ONLP_THERMAL_ID_CREATE(THERMAL_CPU_CORE), "CPU Core", 0},
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, { 82000, 104000, 104000 }
        },  
    { { ONLP_THERMAL_ID_CREATE(THERMAL_1_ON_MAIN_BOARD), "MAC Around(0x4C)", 0},
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, { 68000, 70000, 70000 }
        },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_2_ON_MAIN_BOARD), "COMe bottom(0x4B)", 0},
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, { 68000, 70000, 70000 }
        },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_3_ON_MAIN_BOARD), "Air Outlet(0x4A)", 0},
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, { 68000, 70000, 70000 }
        },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_1_ON_PSU1), "PSU-1 Thermal Sensor 1", ONLP_PSU_ID_CREATE(PSU1_ID)},
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, { 60000, 70000, 70000 }
        },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_2_ON_PSU1), "PSU-1 Thermal Sensor 2", ONLP_PSU_ID_CREATE(PSU1_ID)},
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, { 100000, 120000, 120000 }
        },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_3_ON_PSU1), "PSU-1 Thermal Sensor 3", ONLP_PSU_ID_CREATE(PSU1_ID)},
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, { 100000, 120000, 120000 }
        },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_1_ON_PSU2), "PSU-2 Thermal Sensor 1", ONLP_PSU_ID_CREATE(PSU2_ID)},
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, { 60000, 70000, 70000 }
        },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_2_ON_PSU2), "PSU-2 Thermal Sensor 2", ONLP_PSU_ID_CREATE(PSU2_ID)},
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, { 100000, 120000, 120000 }
        },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_3_ON_PSU2), "PSU-2 Thermal Sensor 3", ONLP_PSU_ID_CREATE(PSU2_ID)},
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, { 100000, 120000, 120000 }
        }
};

/*
 * This will be called to intiialize the thermali subsystem.
 */
int
onlp_thermali_init(void)
{
    return ONLP_STATUS_OK;
}

/*
 * Retrieve the information structure for the given thermal OID.
 *
 * If the OID is invalid, return ONLP_E_STATUS_INVALID.
 * If an unexpected error occurs, return ONLP_E_STATUS_INTERNAL.
 * Otherwise, return ONLP_STATUS_OK with the OID's information.
 *
 * Note -- it is expected that you fill out the information
 * structure even if the sensor described by the OID is not present.
 */
int
onlp_thermali_info_get(onlp_oid_t id, onlp_thermal_info_t* info)
{
    int   tid;
    int   pid;
    int   index = 0;  /* thermal index in psu */
    char  path[64] = {0};

    VALIDATE(id);

    tid = ONLP_OID_ID_GET(id);

    /* Set the onlp_oid_hdr_t and capabilities */
    *info = linfo[tid];

    initialize_bmc_status();

    if(tid == THERMAL_CPU_CORE) {
        return onlp_file_read_int_max(&info->mcelsius, cpu_coretemp_files);
    }


    switch (tid) {
        case THERMAL_1_ON_MAIN_BOARD:
        case THERMAL_2_ON_MAIN_BOARD:
        case THERMAL_3_ON_MAIN_BOARD:
            /* get path for each thermal sensor on the main board */
            if (BMC_IS_ENABLED()) {
                /* 1 -> 0x4c 2 -> 0x4b 3 -> 0x4a */
                sprintf(path, THERMAL_BMC_BASE_PATH, directory_bmc[tid]);
            } else {
                sprintf(path, THERMAL_PATH_FORMAT, directory[tid]);
            }
            break;
        case THERMAL_1_ON_PSU1:
        case THERMAL_2_ON_PSU1:
        case THERMAL_3_ON_PSU1:
        case THERMAL_1_ON_PSU2:
        case THERMAL_2_ON_PSU2:
        case THERMAL_3_ON_PSU2:
            pid = ONLP_OID_ID_GET(info->hdr.poid);

            /* each psu has 3 thermal sensors, which are indexed 1-3 */
            index = (tid - THERMAL_1_ON_PSU1) % 3 + 1;

            /* get path for each sensor of the psu */
            if (BMC_IS_ENABLED()) {
                if (pid == PSU1_ID)
                    sprintf(path, PSU1_THERMAL_BMC_BASE_PATH, index);
                else if (pid == PSU2_ID)
                    sprintf(path, PSU2_THERMAL_BMC_BASE_PATH, index);
                else
                    return ONLP_STATUS_E_INVALID;
            } else {
                sprintf(path, PSU_THERMAL_PATH_FORMAT, directory[tid], index);
            }
            break;
        default:
            return ONLP_STATUS_E_INVALID;
    };

    if (onlp_file_read_int(&info->mcelsius, path) < 0) {
        AIM_LOG_ERROR("Unable to read status from file (%s)\r\n", path);
        return ONLP_STATUS_E_INTERNAL;
    }

    return ONLP_STATUS_OK;
}


