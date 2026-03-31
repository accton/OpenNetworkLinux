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
#include <onlplib/file.h>
#include <onlp/platformi/sysi.h>
#include <onlp/platformi/ledi.h>
#include <onlp/platformi/thermali.h>
#include <onlp/platformi/fani.h>
#include <onlp/platformi/psui.h>
#include "platform_lib.h"

#include "x86_64_accton_as7535_28xb_int.h"
#include "x86_64_accton_as7535_28xb_log.h"


#define NUM_OF_CPLD_VER 8

static char* cpld_ver_path[NUM_OF_CPLD_VER] = {
    "/sys/devices/platform/as7535_28xb_sys/cpu_cpld_version",
    "/sys/devices/platform/as7535_28xb_sys/cpu_cpld_minor_version", 
    "/sys/bus/i2c/devices/12-0061/version",   /* Main CPLD */
    "/sys/bus/i2c/devices/12-0061/minor_version",
    "/sys/devices/platform/as7535_28xb_fan/version",
    "/sys/devices/platform/as7535_28xb_fan/minor_version",
    "/sys/devices/platform/as7535_28xb_sys/fpga_version",
    "/sys/devices/platform/as7535_28xb_sys/fpga_minor_version",
};

const char*
onlp_sysi_platform_get(void)
{
    return "x86-64-accton-as7535-28xb-r0";
}

int
onlp_sysi_onie_data_get(uint8_t** data, int* size)
{
    uint8_t* rdata = aim_zmalloc(256);
    if(onlp_file_read(rdata, 256, size, IDPROM_PATH) == ONLP_STATUS_OK) {
        if(*size == 256) {
            *data = rdata;
            return ONLP_STATUS_OK;
        }
    }

    aim_free(rdata);
    *size = 0;
    return ONLP_STATUS_E_INTERNAL;
}

int
onlp_sysi_oids_get(onlp_oid_t* table, int max)
{
    int i;
    onlp_oid_t* e = table;
    memset(table, 0, max*sizeof(onlp_oid_t));
    int pcb_id = 0;

    pcb_id = get_pcb_id();
    if (pcb_id == 1)
    {
        /* 9 Thermal sensors on the chassis */
        for (i = 1; i <= CHASSIS_THERMAL_COUNT_R02; i++) {
            *e++ = ONLP_THERMAL_ID_CREATE(i);
        }
    }
    else
    {
        /* 7 Thermal sensors on the chassis */
        for (i = 1; i <= CHASSIS_THERMAL_COUNT; i++) {
            *e++ = ONLP_THERMAL_ID_CREATE(i);
        }
    }

    /* 5 LEDs on the chassis */
    for (i = 1; i <= CHASSIS_LED_COUNT; i++) {
        *e++ = ONLP_LED_ID_CREATE(i);
    }

    /* 2 PSUs on the chassis */
    for (i = 1; i <= CHASSIS_PSU_COUNT; i++) {
        *e++ = ONLP_PSU_ID_CREATE(i);
    }

    /* 6 Fans on the chassis */
    for (i = 1; i <= CHASSIS_FAN_COUNT; i++) {
        *e++ = ONLP_FAN_ID_CREATE(i);
    }

    return 0;
}

int
onlp_sysi_platform_info_get(onlp_platform_info_t* pi)
{
    int i, v[NUM_OF_CPLD_VER] = {0};
    char *bmc_buf = NULL;
    char *aux_buf = NULL;
    int bmc_major = 0, bmc_minor = 0;
    unsigned int bmc_aux[4] = {0};
    char bmc_ver[16] = ""; 
    onlp_onie_info_t onie = {0};
    char *bios_ver = NULL;

    for (i = 0; i < AIM_ARRAYSIZE(cpld_ver_path); i++) {
        v[i] = 0;

        if(onlp_file_read_int(v+i, cpld_ver_path[i]) < 0) {
            return ONLP_STATUS_E_INTERNAL;
        }
    }

    onlp_file_read_str(&bios_ver, BIOS_VER_PATH);
    onlp_onie_decode_file(&onie, IDPROM_PATH);

    if ((onlp_file_read_str(&bmc_buf, BMC_VER1_PATH) >= 0) &&
        (onlp_file_read_str(&aux_buf, BMC_VER2_PATH) >= 0))
    {
        bmc_buf[strcspn(bmc_buf, "\n")] = '\0';
        aux_buf[strcspn(aux_buf, "\n")] = '\0';

        /*
         * NOTE: The value in /sys/devices/platform/ipmi_bmc.0/firmware_revision is formatted
         * using "%u.%x" in the kernel driver (see ipmi_msghandler.c::firmware_revision_show).
         * The second field (after the dot) is output in hexadecimal format and must be parsed
         * using "%x" from user-space.
         */
        if (sscanf(bmc_buf, "%u.%x", &bmc_major, &bmc_minor) == 2 &&
            sscanf(aux_buf, "0x%x 0x%x 0x%x 0x%x", &bmc_aux[0], &bmc_aux[1], &bmc_aux[2], &bmc_aux[3]) == 4)
        {
            snprintf(bmc_ver, sizeof(bmc_ver), "%02X.%02X.%02X",
                     bmc_major, bmc_minor, bmc_aux[3]);
        }
    }

    pi->cpld_versions = aim_fstrdup("\r\n\t   CPU CPLD(0x65): %02X.%02X"
                                    "\r\n\t   Main CPLD(0x61): %02X.%02X"
                                    "\r\n\t   Fan CPLD(0x66): %02X.%02X"
                                    "\r\n\t   FPGA(0x60): %02X.%02X\r\n",
                                    v[0], v[1], v[2], v[3], 
                                    v[4], v[5], v[6], v[7]);

    pi->other_versions = aim_fstrdup("\r\n\t   BIOS: %s\r\n\t   ONIE: %s"
                                     "\r\n\t   BMC: %s",
                                    bios_ver, onie.onie_version, bmc_ver);

    AIM_FREE_IF_PTR(bmc_buf);
    AIM_FREE_IF_PTR(aux_buf);
    AIM_FREE_IF_PTR(bios_ver);
    onlp_onie_info_free(&onie);

    return ONLP_STATUS_OK;
}

void
onlp_sysi_platform_info_free(onlp_platform_info_t* pi)
{
    aim_free(pi->cpld_versions);
    aim_free(pi->other_versions);
}
