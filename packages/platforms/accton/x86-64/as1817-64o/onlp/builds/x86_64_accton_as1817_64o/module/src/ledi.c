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
#include <onlp/platformi/ledi.h>
#include <onlplib/file.h>
#include "platform_lib.h"

#define LED_SYSFS_FMT "/sys/devices/platform/as1817_64o_led/led_%s"

#define VALIDATE(_id)                           \
    do {                                        \
        if (!ONLP_OID_IS_LED(_id)) {            \
            return ONLP_STATUS_E_INVALID;       \
        }                                       \
    } while (0)

enum onlp_led_id {
    LED_RESERVED = 0,
    LED_LOC,
    LED_DIAG,
    LED_ALARM,
    LED_FAN,
    LED_PSU,
};

static const char *led_sysfs_name[] = {
    [LED_RESERVED] = "",
    [LED_LOC]      = "loc",
    [LED_DIAG]     = "diag",
    [LED_ALARM]    = "alarm",
    [LED_FAN]      = "fan",
    [LED_PSU]      = "psu",
};

static onlp_led_info_t linfo[] = {
    { }, /* Not used */
    {
        { ONLP_LED_ID_CREATE(LED_LOC), "LED (LOC)", 0 },
        ONLP_LED_STATUS_PRESENT,
        ONLP_LED_CAPS_ON_OFF | ONLP_LED_CAPS_BLUE_BLINKING,
    },
    {
        { ONLP_LED_ID_CREATE(LED_DIAG), "LED (DIAG)", 0 },
        ONLP_LED_STATUS_PRESENT,
        ONLP_LED_CAPS_AUTO,
    },
    {
        { ONLP_LED_ID_CREATE(LED_ALARM), "LED (ALARM)", 0 },
        ONLP_LED_STATUS_PRESENT,
        ONLP_LED_CAPS_AUTO,
    },
    {
        { ONLP_LED_ID_CREATE(LED_FAN), "LED (FAN)", 0 },
        ONLP_LED_STATUS_PRESENT,
        ONLP_LED_CAPS_AUTO,
    },
    {
        { ONLP_LED_ID_CREATE(LED_PSU), "LED (PSU)", 0 },
        ONLP_LED_STATUS_PRESENT,
        ONLP_LED_CAPS_AUTO,
    },
};

int onlp_ledi_init(void)
{
    return ONLP_STATUS_OK;
}

int onlp_ledi_info_get(onlp_oid_t id, onlp_led_info_t *info)
{
    int lid, value;

    VALIDATE(id);
    lid = ONLP_OID_ID_GET(id);

    if (lid < LED_LOC || lid > LED_PSU)
        return ONLP_STATUS_E_INVALID;

    *info = linfo[lid];

    if (onlp_file_read_int(&value, LED_SYSFS_FMT, led_sysfs_name[lid]) < 0)
        return ONLP_STATUS_E_INTERNAL;

    info->mode = value;

    if (info->mode != ONLP_LED_MODE_OFF)
        info->status |= ONLP_LED_STATUS_ON;

    return ONLP_STATUS_OK;
}

int onlp_ledi_set(onlp_oid_t id, int on_or_off)
{
    VALIDATE(id);

    if (!on_or_off)
        return onlp_ledi_mode_set(id, ONLP_LED_MODE_OFF);

    return ONLP_STATUS_E_UNSUPPORTED;
}

int onlp_ledi_mode_set(onlp_oid_t id, onlp_led_mode_t mode)
{
    int lid;

    VALIDATE(id);
    lid = ONLP_OID_ID_GET(id);

    if (onlp_file_write_int(mode, LED_SYSFS_FMT, led_sysfs_name[lid]) != 0)
        return ONLP_STATUS_E_INTERNAL;

    return ONLP_STATUS_OK;
}

int onlp_ledi_ioctl(onlp_oid_t id, va_list vargs)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}
