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
#include <onlp/platformi/sysi.h>
#include <onlp/platformi/ledi.h>
#include <onlp/platformi/thermali.h>
#include <onlp/platformi/fani.h>
#include <onlp/platformi/psui.h>
#include "platform_lib.h"

#define CPLD1_VER_PATH "/sys/devices/platform/as1817_64o_sys/cpld1_version"
#define CPLD2_VER_PATH "/sys/devices/platform/as1817_64o_sys/cpld2_version"
#define FPGA_VER_PATH  "/sys/devices/platform/as1817_64o_sys/fpga_version"
#define FAN_CPLD_VER_PATH "/sys/devices/platform/as1817_64o_sys/fan_cpld_version"
#define SYS_CPLD_VER_PATH "/sys/devices/platform/as1817_64o_sys/sys_cpld_version"
#define BIOS_VER_PATH  "/sys/devices/virtual/dmi/id/bios_version"

const char *onlp_sysi_platform_get(void)
{
    return "x86-64-accton-as1817-64o-r0";
}

int onlp_sysi_onie_data_get(uint8_t **data, int *size)
{
    uint8_t *rdata = aim_zmalloc(256);

    if (onlp_file_read(rdata, 256, size, IDPROM_PATH) == ONLP_STATUS_OK) {
        if (*size == 256) {
            *data = rdata;
            return ONLP_STATUS_OK;
        }
    }

    aim_free(rdata);
    *size = 0;
    return ONLP_STATUS_E_INTERNAL;
}

int onlp_sysi_oids_get(onlp_oid_t *table, int max)
{
    int i;
    onlp_oid_t *e = table;
    memset(table, 0, max * sizeof(onlp_oid_t));

    for (i = 1; i <= CHASSIS_THERMAL_COUNT; i++)
        *e++ = ONLP_THERMAL_ID_CREATE(i);

    for (i = 1; i <= CHASSIS_LED_COUNT; i++)
        *e++ = ONLP_LED_ID_CREATE(i);

    for (i = 1; i <= CHASSIS_PSU_COUNT; i++)
        *e++ = ONLP_PSU_ID_CREATE(i);

    for (i = 1; i <= CHASSIS_FAN_COUNT; i++)
        *e++ = ONLP_FAN_ID_CREATE(i);

    return 0;
}

int onlp_sysi_platform_info_get(onlp_platform_info_t *pi)
{
    char *cpld1_ver = NULL;
    char *cpld2_ver = NULL;
    char *fpga_ver = NULL;
    char *fan_cpld_ver = NULL;
    char *sys_cpld_ver = NULL;
    char *bios_ver = NULL;

    onlp_file_read_str(&cpld1_ver, CPLD1_VER_PATH);
    onlp_file_read_str(&cpld2_ver, CPLD2_VER_PATH);
    onlp_file_read_str(&fpga_ver, FPGA_VER_PATH);
    onlp_file_read_str(&fan_cpld_ver, FAN_CPLD_VER_PATH);
    onlp_file_read_str(&sys_cpld_ver, SYS_CPLD_VER_PATH);
    onlp_file_read_str(&bios_ver, BIOS_VER_PATH);

    pi->cpld_versions = aim_fstrdup(
        "\r\n\t   Sys CPLD: %s"
        "\r\n\t   Port CPLD1: %s"
        "\r\n\t   Port CPLD2: %s"
        "\r\n\t   Fan CPLD: %s",
        sys_cpld_ver ? sys_cpld_ver : "N/A",
        cpld1_ver ? cpld1_ver : "N/A",
        cpld2_ver ? cpld2_ver : "N/A",
        fan_cpld_ver ? fan_cpld_ver : "N/A");

    pi->other_versions = aim_fstrdup(
        "\r\n\t   FPGA: %s"
        "\r\n\t   BIOS: %s",
        fpga_ver ? fpga_ver : "N/A",
        bios_ver ? bios_ver : "N/A");

    AIM_FREE_IF_PTR(cpld1_ver);
    AIM_FREE_IF_PTR(cpld2_ver);
    AIM_FREE_IF_PTR(fpga_ver);
    AIM_FREE_IF_PTR(fan_cpld_ver);
    AIM_FREE_IF_PTR(sys_cpld_ver);
    AIM_FREE_IF_PTR(bios_ver);

    return ONLP_STATUS_OK;
}

void onlp_sysi_platform_info_free(onlp_platform_info_t *pi)
{
    aim_free(pi->cpld_versions);
    aim_free(pi->other_versions);
}

/* Fan and LED management handled by BMC */
int onlp_sysi_platform_manage_fans(void)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}

int onlp_sysi_platform_manage_leds(void)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}
