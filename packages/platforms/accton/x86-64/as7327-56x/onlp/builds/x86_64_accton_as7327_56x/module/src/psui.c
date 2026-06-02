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
#include <onlp/platformi/psui.h>
#include <onlplib/mmap.h>
#include <ctype.h>
#include <string.h>
#include "platform_lib.h"

#define VALIDATE(_id)                           \
    do {                                        \
        if(!ONLP_OID_IS_PSU(_id)) {             \
            return ONLP_STATUS_E_INVALID;       \
        }                                       \
    } while(0)

int
onlp_psui_init(void)
{
    return ONLP_STATUS_OK;
}

static int
psu_bmc_detail_info_get(onlp_psu_info_t* info, int index)
{
    int val;
    char *basepath = NULL;

    if (index == PSU1_ID)
        basepath = PSU1_BMC_BASE_PATH;
    else if (index == PSU2_ID)
        basepath = PSU2_BMC_BASE_PATH;

    /* Read voltage, current and power */
    if (psu_bmc_info_get(basepath, "psu_vout", &val) == ONLP_STATUS_OK) {
        info->mvout = val;
        info->caps |= ONLP_PSU_CAPS_VOUT;
    }

    if (psu_bmc_info_get(basepath, "psu_vin", &val) == ONLP_STATUS_OK) {
        info->mvin = val;
        info->caps |= ONLP_PSU_CAPS_VIN;
    }

    if (psu_bmc_info_get(basepath, "psu_iout", &val) == ONLP_STATUS_OK) {
        info->miout = val;
        info->caps |= ONLP_PSU_CAPS_IOUT;
    }

    if (psu_bmc_info_get(basepath, "psu_iin", &val) == ONLP_STATUS_OK) {
        info->miin = val;
        info->caps |= ONLP_PSU_CAPS_IIN;
    }

    if (psu_bmc_info_get(basepath, "psu_pout", &val) == ONLP_STATUS_OK) {
        info->mpout = val;
        info->caps |= ONLP_PSU_CAPS_POUT;
    }

    if (psu_bmc_info_get(basepath, "psu_pin", &val) == ONLP_STATUS_OK) {
        info->mpin = val;
        info->caps |= ONLP_PSU_CAPS_PIN;
    }

    return ONLP_STATUS_OK;
}

static int
psu_pmbus_detail_info_get(onlp_psu_info_t* info, int index)
{
    int val;
    /* Read voltage, current and power */
    if (psu_pmbus_info_get(index, "psu_v_out", &val) == 0) {
        info->mvout = val;
        info->caps |= ONLP_PSU_CAPS_VOUT;
    }

    if (psu_pmbus_info_get(index, "psu_v_in", &val) == 0) {
        info->mvin = val;
        info->caps |= ONLP_PSU_CAPS_VIN;
    }

    if (psu_pmbus_info_get(index, "psu_i_out", &val) == 0) {
        info->miout = val;
        info->caps |= ONLP_PSU_CAPS_IOUT;
    }

    if (psu_pmbus_info_get(index, "psu_i_in", &val) == 0) {
        info->miin = val;
        info->caps |= ONLP_PSU_CAPS_IIN;
    }

    if (psu_pmbus_info_get(index, "psu_p_out", &val) == 0) {
        info->mpout = val;
        info->caps |= ONLP_PSU_CAPS_POUT;
    }

    if (psu_pmbus_info_get(index, "psu_p_in", &val) == 0) {
        info->mpin = val;
        info->caps |= ONLP_PSU_CAPS_PIN;
    }

    psu_pmbus_model_name_get(index, info->model, sizeof(info->model));

    psu_pmbus_serial_number_get(index, info->serial, sizeof(info->serial));

    return ONLP_STATUS_OK;
}

static int
psu_detail_info_get(onlp_psu_info_t* info)
{
    int index = ONLP_OID_ID_GET(info->hdr.id);
    int temp_index = 0;

    /* Set the associated oid_table */
    info->hdr.coids[0] = ONLP_FAN_ID_CREATE(index + CHASSIS_FAN_COUNT);
    for(temp_index = 1; temp_index <= CHASSIS_PSU_THERMAL_COUNT; temp_index++)
    {
        info->hdr.coids[temp_index] = ONLP_THERMAL_ID_CREATE((index-1)*CHASSIS_PSU_THERMAL_COUNT + CHASSIS_THERMAL_COUNT + temp_index);
    }

    if (info->status & (ONLP_PSU_STATUS_FAILED | ONLP_PSU_STATUS_UNPLUGGED)) {
        return ONLP_STATUS_OK;
    }

    if(BMC_IS_ENABLED()) {
        psu_bmc_detail_info_get(info, index);
    } else {
        psu_pmbus_detail_info_get(info, index);
    }

    return ONLP_STATUS_OK;
}

/*
 * Get all information about the given PSU oid.
 */
static onlp_psu_info_t pinfo[] =
{
    { }, /* Not used */
    {
        { ONLP_PSU_ID_CREATE(PSU1_ID), "PSU-1", 0 },
    },
    {
        { ONLP_PSU_ID_CREATE(PSU2_ID), "PSU-2", 0 },
    }
};

static void
onlp_psu_str_check(char *str, int buffer_size)
{
    str[buffer_size - 1] = '\0';

    if(strlen(str) && isspace(str[strlen(str) - 1])) {
        str[strlen(str) - 1] = 0;
    }
}

int
onlp_psui_info_get(onlp_oid_t id, onlp_psu_info_t* info)
{
    int val   = 0;
    int ret   = ONLP_STATUS_OK;
    int index = ONLP_OID_ID_GET(id);
    int psu_type;
    char *basepath;

    VALIDATE(id);

    memset(info, 0, sizeof(onlp_psu_info_t));
    *info = pinfo[index]; /* Set the onlp_oid_hdr_t */

    initialize_bmc_status();

    if(BMC_IS_ENABLED()) {
        if (index == PSU1_ID) 
            basepath = PSU1_BMC_BASE_PATH;
        else if (index == PSU2_ID)
            basepath = PSU2_BMC_BASE_PATH;
        else
            return ONLP_STATUS_E_INVALID;

        if (psu_bmc_info_get(basepath, "psu_present", &val) != ONLP_STATUS_OK) {
            AIM_LOG_ERROR("Unable to read PSU(%d) node(psu_present)\r\n", index);
            return ONLP_STATUS_E_INTERNAL;
        }

        /* Get the present state */
        if (val != PSU_STATUS_PRESENT) {
            info->status &= ~ONLP_PSU_STATUS_PRESENT;
            return ONLP_STATUS_OK;
        }
        info->status |= ONLP_PSU_STATUS_PRESENT;

        /* Get model name */
        psu_bmc_str_get(basepath, "psu_model_name", info->model, sizeof(info->model));

        onlp_psu_str_check(info->model, ONLP_CONFIG_INFO_STR_MAX);

        /* Get serial number */
        psu_bmc_str_get(basepath, "psu_serial_number", info->serial, sizeof(info->serial));

        onlp_psu_str_check(info->serial, ONLP_CONFIG_INFO_STR_MAX);

        /* get psu type
         */
        if (psu_bmc_info_get(basepath, "psu_vin_type", &psu_type) != ONLP_STATUS_OK) {
            AIM_LOG_ERROR("unable to read PSU(%d) node(psu_mfr_vin_type)\r\n", index);
        }
        switch (psu_type) {
            case PSU_TYPE_DC:
                info->caps = ONLP_PSU_CAPS_DC48;
                break;
            case PSU_TYPE_AC:
                info->caps = ONLP_PSU_CAPS_AC;
                break;
            case PSU_TYPE_UNKNOWN:  /* user insert a unknown psu or unplugged.*/
                info->status |= ONLP_PSU_STATUS_UNPLUGGED;
                info->status &= ~ONLP_PSU_STATUS_FAILED;
                ret = ONLP_STATUS_OK;
                break;
            default:
                ret = ONLP_STATUS_E_UNSUPPORTED;
                break;
        }

        /* Get power good status */
        if (psu_bmc_info_get(basepath, "psu_power_good", &val) != ONLP_STATUS_OK) {
            AIM_LOG_ERROR("Unable to read PSU(%d) node(psu_power_good)\r\n", index);
        }

        if (val != PSU_STATUS_POWER_GOOD) {
            info->status |= ONLP_PSU_STATUS_UNPLUGGED;
            info->caps = 0;
        }

    } else {
        /* Get the present state */
        if (psu_pmbus_info_get(index, "psu_present", &val) != 0) {
            AIM_LOG_ERROR("Unable to read PSU(%d) node(psu_present)\r\n", index);
        }

        if (val != PSU_STATUS_PRESENT) {
            info->status &= ~ONLP_PSU_STATUS_PRESENT;
            return ONLP_STATUS_OK;
        }
        info->status |= ONLP_PSU_STATUS_PRESENT;

        /* Get model name */
        psu_pmbus_model_name_get(index, info->model, sizeof(info->model));

        onlp_psu_str_check(info->model, ONLP_CONFIG_INFO_STR_MAX);

        /* Get serial number */
        psu_pmbus_serial_number_get(index, info->serial, sizeof(info->serial));

        onlp_psu_str_check(info->serial, ONLP_CONFIG_INFO_STR_MAX);

        /* get psu type
         */
        if (psu_pmbus_info_get(index, "psu_mfr_vin_type", &psu_type) != 0) {
            AIM_LOG_ERROR("unable to read psu(%d) node(psu_mfr_vin_type)\r\n", index);
        }
        switch (psu_type) {
            case PSU_TYPE_DC:
                info->caps = ONLP_PSU_CAPS_DC48;
                break;
            case PSU_TYPE_AC:
                info->caps = ONLP_PSU_CAPS_AC;
                break;
            case PSU_TYPE_UNKNOWN:  /* user insert a unknown psu or unplugged.*/
                info->status |= ONLP_PSU_STATUS_UNPLUGGED;
                info->status &= ~ONLP_PSU_STATUS_FAILED;
                ret = ONLP_STATUS_OK;
                break;
            default:
                ret = ONLP_STATUS_E_UNSUPPORTED;
                break;
        }

        /* Get power good status */
        if (psu_pmbus_info_get(index, "psu_power_good", &val) != 0) {
            AIM_LOG_ERROR("Unable to read PSU(%d) node(psu_power_good)\r\n", index);
        }

        if (val != PSU_STATUS_POWER_GOOD) {
            info->status |= ONLP_PSU_STATUS_UNPLUGGED;
            info->caps = 0;
        }

    }

    ret = psu_detail_info_get(info);

    return ret;
}

int
onlp_psui_ioctl(onlp_oid_t pid, va_list vargs)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}

