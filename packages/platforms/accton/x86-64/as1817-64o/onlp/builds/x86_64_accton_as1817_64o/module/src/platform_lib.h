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
#ifndef __PLATFORM_LIB_H__
#define __PLATFORM_LIB_H__

#include <onlplib/file.h>
#include "x86_64_accton_as1817_64o_log.h"

#define CHASSIS_FAN_COUNT       16
#define CHASSIS_THERMAL_COUNT   15
#define CHASSIS_PSU_COUNT       4
#define CHASSIS_LED_COUNT       5
#define CHASSIS_OSFP_COUNT      64
#define CHASSIS_SFP_COUNT       2

#define PSU1_ID 1
#define PSU2_ID 2
#define PSU3_ID 3
#define PSU4_ID 4

#define PSU_STATUS_PRESENT    1
#define PSU_STATUS_POWER_GOOD 1

#define IDPROM_PATH "/sys/devices/platform/as1817_64o_sys/eeprom"

enum onlp_fan_dir {
    FAN_DIR_F2B,
    FAN_DIR_B2F,
    FAN_DIR_COUNT,
};

#define AIM_FREE_IF_PTR(p) \
    do { \
        if (p) { \
            aim_free(p); \
            p = NULL; \
        } \
    } while (0)

#endif  /* __PLATFORM_LIB_H__ */
