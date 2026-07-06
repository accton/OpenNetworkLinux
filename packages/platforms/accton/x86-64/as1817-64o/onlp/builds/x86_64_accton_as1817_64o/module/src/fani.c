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
 * Fan Platform Implementation.
 *
 ***********************************************************/
#include <onlp/platformi/fani.h>
#include <onlplib/file.h>
#include <string.h>
#include "platform_lib.h"

#define FAN_SYSFS_FMT "/sys/devices/platform/as1817_64o_fan*fan%d_%s"
#define PSU_FAN_SYSFS_FMT "/sys/devices/platform/as1817_64o_psu.%d*psu_fan_input"
#define PSU_FAN_PCT_FMT "/sys/devices/platform/as1817_64o_psu.%d*psu_fan_percentage"
#define PSU_POWER_GOOD_FMT "/sys/devices/platform/as1817_64o_psu.%d*psu_power_good"
#define PSU_FAN_DIR_FMT "/sys/devices/platform/as1817_64o_psu.%d*psu_fan_dir"

/*
 * Reference maps 16 chassis fans as individual rotors:
 *   fan@1-4:   Top Front Fan 1-4   (sysfs fan1-4_front_input)
 *   fan@5-8:   Top Rear Fan 1-4    (sysfs fan1-4_rear_input)
 *   fan@9-12:  Bottom Front Fan 1-4 (sysfs fan5-8_front_input)
 *   fan@13-16: Bottom Rear Fan 1-4  (sysfs fan5-8_rear_input)
 *   fan@17-20: PSU 1-4 fans
 */
enum fan_id {
    FAN_1 = 1, FAN_2, FAN_3, FAN_4,
    FAN_5, FAN_6, FAN_7, FAN_8,
    FAN_9, FAN_10, FAN_11, FAN_12,
    FAN_13, FAN_14, FAN_15, FAN_16,
    FAN_PSU_1, FAN_PSU_2, FAN_PSU_3, FAN_PSU_4,
};

/* Map ONLP fan ID to kernel sysfs fan module (1-8) and side (front/rear) */
struct fan_rotor_map {
    int sysfs_fid;       /* kernel fan module 1-8 */
    const char *side;    /* "front" or "rear" */
};

static const struct fan_rotor_map rotor_map[CHASSIS_FAN_COUNT] = {
    { 1, "front" },  /* Fan 1 Front */
    { 1, "rear"  },  /* Fan 1 Rear  */
    { 2, "front" },  /* Fan 2 Front */
    { 2, "rear"  },  /* Fan 2 Rear  */
    { 3, "front" },  /* Fan 3 Front */
    { 3, "rear"  },  /* Fan 3 Rear  */
    { 4, "front" },  /* Fan 4 Front */
    { 4, "rear"  },  /* Fan 4 Rear  */
    { 5, "front" },  /* Fan 5 Front */
    { 5, "rear"  },  /* Fan 5 Rear  */
    { 6, "front" },  /* Fan 6 Front */
    { 6, "rear"  },  /* Fan 6 Rear  */
    { 7, "front" },  /* Fan 7 Front */
    { 7, "rear"  },  /* Fan 7 Rear  */
    { 8, "front" },  /* Fan 8 Front */
    { 8, "rear"  },  /* Fan 8 Rear  */
};

#define CHASSIS_FAN_CAPS (ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE)

static onlp_fan_info_t chassis_finfo[] = {
    { }, /* Not used (index 0) */
    { { ONLP_FAN_ID_CREATE(1),  "Chassis Fan - 1 Front Fan", 0 }, 0, CHASSIS_FAN_CAPS, 0, 0, ONLP_FAN_MODE_INVALID },
    { { ONLP_FAN_ID_CREATE(2),  "Chassis Fan - 1 Rear Fan",  0 }, 0, CHASSIS_FAN_CAPS, 0, 0, ONLP_FAN_MODE_INVALID },
    { { ONLP_FAN_ID_CREATE(3),  "Chassis Fan - 2 Front Fan", 0 }, 0, CHASSIS_FAN_CAPS, 0, 0, ONLP_FAN_MODE_INVALID },
    { { ONLP_FAN_ID_CREATE(4),  "Chassis Fan - 2 Rear Fan",  0 }, 0, CHASSIS_FAN_CAPS, 0, 0, ONLP_FAN_MODE_INVALID },
    { { ONLP_FAN_ID_CREATE(5),  "Chassis Fan - 3 Front Fan", 0 }, 0, CHASSIS_FAN_CAPS, 0, 0, ONLP_FAN_MODE_INVALID },
    { { ONLP_FAN_ID_CREATE(6),  "Chassis Fan - 3 Rear Fan",  0 }, 0, CHASSIS_FAN_CAPS, 0, 0, ONLP_FAN_MODE_INVALID },
    { { ONLP_FAN_ID_CREATE(7),  "Chassis Fan - 4 Front Fan", 0 }, 0, CHASSIS_FAN_CAPS, 0, 0, ONLP_FAN_MODE_INVALID },
    { { ONLP_FAN_ID_CREATE(8),  "Chassis Fan - 4 Rear Fan",  0 }, 0, CHASSIS_FAN_CAPS, 0, 0, ONLP_FAN_MODE_INVALID },
    { { ONLP_FAN_ID_CREATE(9),  "Chassis Fan - 5 Front Fan", 0 }, 0, CHASSIS_FAN_CAPS, 0, 0, ONLP_FAN_MODE_INVALID },
    { { ONLP_FAN_ID_CREATE(10), "Chassis Fan - 5 Rear Fan",  0 }, 0, CHASSIS_FAN_CAPS, 0, 0, ONLP_FAN_MODE_INVALID },
    { { ONLP_FAN_ID_CREATE(11), "Chassis Fan - 6 Front Fan", 0 }, 0, CHASSIS_FAN_CAPS, 0, 0, ONLP_FAN_MODE_INVALID },
    { { ONLP_FAN_ID_CREATE(12), "Chassis Fan - 6 Rear Fan",  0 }, 0, CHASSIS_FAN_CAPS, 0, 0, ONLP_FAN_MODE_INVALID },
    { { ONLP_FAN_ID_CREATE(13), "Chassis Fan - 7 Front Fan", 0 }, 0, CHASSIS_FAN_CAPS, 0, 0, ONLP_FAN_MODE_INVALID },
    { { ONLP_FAN_ID_CREATE(14), "Chassis Fan - 7 Rear Fan",  0 }, 0, CHASSIS_FAN_CAPS, 0, 0, ONLP_FAN_MODE_INVALID },
    { { ONLP_FAN_ID_CREATE(15), "Chassis Fan - 8 Front Fan", 0 }, 0, CHASSIS_FAN_CAPS, 0, 0, ONLP_FAN_MODE_INVALID },
    { { ONLP_FAN_ID_CREATE(16), "Chassis Fan - 8 Rear Fan",  0 }, 0, CHASSIS_FAN_CAPS, 0, 0, ONLP_FAN_MODE_INVALID },
};

static onlp_fan_info_t psu_finfo[] = {
    { { ONLP_FAN_ID_CREATE(FAN_PSU_1), "PSU-1 Fan", ONLP_PSU_ID_CREATE(PSU1_ID) },
      0x0, ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE, 0, 0, ONLP_FAN_MODE_INVALID },
    { { ONLP_FAN_ID_CREATE(FAN_PSU_2), "PSU-2 Fan", ONLP_PSU_ID_CREATE(PSU2_ID) },
      0x0, ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE, 0, 0, ONLP_FAN_MODE_INVALID },
    { { ONLP_FAN_ID_CREATE(FAN_PSU_3), "PSU-3 Fan", ONLP_PSU_ID_CREATE(PSU3_ID) },
      0x0, ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE, 0, 0, ONLP_FAN_MODE_INVALID },
    { { ONLP_FAN_ID_CREATE(FAN_PSU_4), "PSU-4 Fan", ONLP_PSU_ID_CREATE(PSU4_ID) },
      0x0, ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE, 0, 0, ONLP_FAN_MODE_INVALID },
};

#define VALIDATE(_id)                           \
do {                                            \
    if(!ONLP_OID_IS_FAN(_id)) {                 \
        return ONLP_STATUS_E_INVALID;           \
    }                                           \
} while(0)

static int _onlp_fani_info_get_chassis(int fid, onlp_fan_info_t* info)
{
    int idx = fid - 1;
    const struct fan_rotor_map *m = &rotor_map[idx];
    int value = 0;
    char attr[32];

    /* present — use the sysfs module's present */
    if (onlp_file_read_int(&value, FAN_SYSFS_FMT, m->sysfs_fid, "present") < 0)
        return ONLP_STATUS_E_INTERNAL;

    if (value == 0)
        return ONLP_STATUS_OK;

    info->status |= ONLP_FAN_STATUS_PRESENT;

    /* direction */
    {
        char dir_str[8] = {0};
        int len = 0;
        if (onlp_file_read((uint8_t *)dir_str, sizeof(dir_str) - 1, &len,
                           FAN_SYSFS_FMT, m->sysfs_fid, "dir") == 0) {
            if (strncmp(dir_str, "F2B", 3) == 0)
                info->status |= ONLP_FAN_STATUS_F2B;
            else if (strncmp(dir_str, "B2F", 3) == 0)
                info->status |= ONLP_FAN_STATUS_B2F;
        }
    }

    /* fault */
    snprintf(attr, sizeof(attr), "%s_fault", m->side);
    if (onlp_file_read_int(&value, FAN_SYSFS_FMT, m->sysfs_fid, attr) == 0 && value)
        info->status |= ONLP_FAN_STATUS_FAILED;

    /* RPM */
    snprintf(attr, sizeof(attr), "%s_input", m->side);
    if (onlp_file_read_int(&value, FAN_SYSFS_FMT, m->sysfs_fid, attr) == 0)
        info->rpm = value;

    /* percentage */
    snprintf(attr, sizeof(attr), "%s_percentage", m->side);
    if (onlp_file_read_int(&value, FAN_SYSFS_FMT, m->sysfs_fid, attr) == 0)
        info->percentage = value;

    return ONLP_STATUS_OK;
}

static int _onlp_fani_info_get_psu(int pid, onlp_fan_info_t* info)
{
    int val = 0;
    int len = 0;
    char dir[8] = {0};

    info->status |= ONLP_FAN_STATUS_PRESENT;

    if (onlp_file_read_int(&val, PSU_POWER_GOOD_FMT, pid - 1) == 0) {
        if (val != PSU_STATUS_POWER_GOOD)
            info->status |= ONLP_FAN_STATUS_FAILED;
    }

    if (onlp_file_read_int(&val, PSU_FAN_SYSFS_FMT, pid - 1) == 0) {
        info->rpm = val;
    }

    if (onlp_file_read_int(&val, PSU_FAN_PCT_FMT, pid - 1) == 0) {
        info->percentage = val;
    }

    /* Fan direction */
    if (onlp_file_read((uint8_t *)dir, sizeof(dir) - 1, &len,
                       PSU_FAN_DIR_FMT, pid - 1) == 0) {
        if (strncmp(dir, "F2B", 3) == 0)
            info->status |= ONLP_FAN_STATUS_F2B;
        else if (strncmp(dir, "B2F", 3) == 0)
            info->status |= ONLP_FAN_STATUS_B2F;
    }

    return ONLP_STATUS_OK;
}

int onlp_fani_init(void)
{
    return ONLP_STATUS_OK;
}

int onlp_fani_info_get(onlp_oid_t id, onlp_fan_info_t* info)
{
    int fid;
    VALIDATE(id);

    fid = ONLP_OID_ID_GET(id);
    memset(info, 0, sizeof(*info));

    if (fid >= FAN_1 && fid <= FAN_16) {
        *info = chassis_finfo[fid];
        return _onlp_fani_info_get_chassis(fid, info);
    }

    if (fid >= FAN_PSU_1 && fid <= FAN_PSU_4) {
        *info = psu_finfo[fid - FAN_PSU_1];
        return _onlp_fani_info_get_psu(fid - FAN_PSU_1 + 1, info);
    }

    return ONLP_STATUS_E_INVALID;
}

int onlp_fani_rpm_set(onlp_oid_t id, int rpm)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}

int onlp_fani_percentage_set(onlp_oid_t id, int p)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}

int onlp_fani_mode_set(onlp_oid_t id, onlp_fan_mode_t mode)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}

int onlp_fani_dir_set(onlp_oid_t id, onlp_fan_dir_t dir)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}

int onlp_fani_ioctl(onlp_oid_t id, va_list vargs)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}
