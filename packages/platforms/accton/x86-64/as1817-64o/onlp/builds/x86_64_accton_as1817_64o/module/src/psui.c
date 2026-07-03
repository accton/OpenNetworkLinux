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
 *
 *
 ***********************************************************/
#include <onlp/platformi/psui.h>
#include <onlplib/file.h>
#include <string.h>
#include "platform_lib.h"

#define PSU_SYSFS_FMT "/sys/devices/platform/as1817_64o_psu.%d*psu_%s"

#define VALIDATE(_id)                           \
    do {                                        \
        if (!ONLP_OID_IS_PSU(_id)) {            \
            return ONLP_STATUS_E_INVALID;       \
        }                                       \
    } while (0)

static int psu_read_int(int index, const char *attr, int *value)
{
    return onlp_file_read_int(value, PSU_SYSFS_FMT, index - 1, attr);
}

static int psu_read_str(int index, const char *attr, char *buf, int len)
{
    int size = 0;
    int ret;

    ret = onlp_file_read((uint8_t *)buf, len - 1, &size,
                         PSU_SYSFS_FMT, index - 1, attr);
    if (ret == ONLP_STATUS_OK) {
        buf[size] = '\0';
        while (size > 0 && (buf[size-1] == '\n' || buf[size-1] == '\r'))
            buf[--size] = '\0';
    }
    return ret;
}

int onlp_psui_init(void)
{
    return ONLP_STATUS_OK;
}

static onlp_psu_info_t pinfo[] = {
    { }, /* Not used */
    {
        { ONLP_PSU_ID_CREATE(PSU1_ID), "PSU-1", 0 },
    },
    {
        { ONLP_PSU_ID_CREATE(PSU2_ID), "PSU-2", 0 },
    },
    {
        { ONLP_PSU_ID_CREATE(PSU3_ID), "PSU-3", 0 },
    },
    {
        { ONLP_PSU_ID_CREATE(PSU4_ID), "PSU-4", 0 },
    }
};

int onlp_psui_info_get(onlp_oid_t id, onlp_psu_info_t *info)
{
    int val = 0;
    int index = ONLP_OID_ID_GET(id);

    VALIDATE(id);

    memset(info, 0, sizeof(onlp_psu_info_t));
    *info = pinfo[index];

    /* Get present state */
    if (psu_read_int(index, "present", &val) != 0) {
        return ONLP_STATUS_E_INTERNAL;
    }

    if (val != PSU_STATUS_PRESENT) {
        info->status &= ~ONLP_PSU_STATUS_PRESENT;
        return ONLP_STATUS_OK;
    }

    info->status |= ONLP_PSU_STATUS_PRESENT;
    info->caps = ONLP_PSU_CAPS_AC;

    /* Read model and serial (FRU EEPROM is readable even when unplugged) */
    psu_read_str(index, "model", info->model, sizeof(info->model));
    psu_read_str(index, "serial", info->serial, sizeof(info->serial));

    /* Get power good */
    if (psu_read_int(index, "power_good", &val) != 0) {
        return ONLP_STATUS_E_INTERNAL;
    }

    if (val != PSU_STATUS_POWER_GOOD) {
        info->status |= ONLP_PSU_STATUS_UNPLUGGED;
        return ONLP_STATUS_OK;
    }

    /* Set associated OIDs: 1 fan + 3 thermals per PSU */
    info->hdr.coids[0] = ONLP_FAN_ID_CREATE(index + CHASSIS_FAN_COUNT);
    info->hdr.coids[1] = ONLP_THERMAL_ID_CREATE(CHASSIS_THERMAL_COUNT + (index - 1) * 3 + 1);
    info->hdr.coids[2] = ONLP_THERMAL_ID_CREATE(CHASSIS_THERMAL_COUNT + (index - 1) * 3 + 2);
    info->hdr.coids[3] = ONLP_THERMAL_ID_CREATE(CHASSIS_THERMAL_COUNT + (index - 1) * 3 + 3);

    /* Read voltage, current and power */
    if (psu_read_int(index, "vin", &val) == 0) {
        info->mvin = val;
        info->caps |= ONLP_PSU_CAPS_VIN;
    }
    if (psu_read_int(index, "vout", &val) == 0) {
        info->mvout = val;
        info->caps |= ONLP_PSU_CAPS_VOUT;
    }
    if (psu_read_int(index, "iin", &val) == 0) {
        info->miin = val;
        info->caps |= ONLP_PSU_CAPS_IIN;
    }
    if (psu_read_int(index, "iout", &val) == 0) {
        info->miout = val;
        info->caps |= ONLP_PSU_CAPS_IOUT;
    }
    if (psu_read_int(index, "pin", &val) == 0) {
        info->mpin = val;
        info->caps |= ONLP_PSU_CAPS_PIN;
    }
    if (psu_read_int(index, "pout", &val) == 0) {
        info->mpout = val;
        info->caps |= ONLP_PSU_CAPS_POUT;
    }

    return ONLP_STATUS_OK;
}

int onlp_psui_ioctl(onlp_oid_t pid, va_list vargs)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}
