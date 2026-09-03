/************************************************************
 * <bsn.cl fy=2014 v=onl>
 *
 *           Copyright 2014 Big Switch Networks, Inc.
 *           Copyright 2017 Accton Technology Corporation.
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
#include <unistd.h>
#include <fcntl.h>

#include <onlplib/file.h>
#include <onlp/platformi/sysi.h>
#include <onlp/platformi/ledi.h>
#include <onlp/platformi/thermali.h>
#include <onlp/platformi/fani.h>
#include <onlp/platformi/psui.h>
#include "platform_lib.h"

#include "x86_64_accton_as9926_24db_int.h"
#include "x86_64_accton_as9926_24db_log.h"

#define BIOS_VER_PATH "/sys/devices/virtual/dmi/id/bios_version"
#define BMC_VER_PREFIX "/sys/bus/platform/drivers/ipmi_si/IPI0001:00/bmc/"

const char* onlp_sysi_platform_get(void)
{
	return "x86-64-accton-as9926-24db-r0";
}

int onlp_sysi_onie_data_get(uint8_t** data, int* size)
{
	const int len = 256;
	uint8_t* rdata = aim_zmalloc(len + 1);
	if(onlp_file_read(rdata, len, size, IDPROM_PATH) == ONLP_STATUS_OK) {
		if(*size == len) {
			*data = rdata;
			return ONLP_STATUS_OK;
		}
	}

	aim_free(rdata);
	*size = 0;
	return ONLP_STATUS_E_INTERNAL;
}

int onlp_sysi_oids_get(onlp_oid_t* table, int max)
{
	int i;
	onlp_oid_t* e = table;
	memset(table, 0, max*sizeof(onlp_oid_t));
    
	/* 5 Thermal sensors on the chassis */
	for (i = 1; i <= CHASSIS_THERMAL_COUNT; i++) {
		*e++ = ONLP_THERMAL_ID_CREATE(i);
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

#define CPLD_VERSION_FORMAT "/sys/devices/platform/as9926_24db_sys/%s"

typedef struct cpld_version {
	char *attr_name;
	int   version;
	char *description;
} cpld_version_t;

int onlp_sysi_platform_info_get(onlp_platform_info_t* pi)
{
    int i, major_ver, minor_ver, aux_ver[4];
    onlp_onie_info_t onie;
    char *bios_ver = NULL;
    char *bmc_fw_ver = NULL;
    char *aux_fw_ver = NULL;
    cpld_version_t cplds[] = { { "mb_cpld2_ver", 0, "Main CPLD2(0x61)"},
                   { "mb_cpld3_ver", 0, "Main CPLD3(0x62)"},
                   { "cpu_cpld_ver", 0, "CPU CPLD(0x65)"},
                   { "fan_cpld_ver", 0, "FAN CPLD(0x66)"},
                   { "fpga_cpld_ver", 0, "FPGA(0x68)"} };
    /* BMC version
       Major: decimal, Minor: hex, Aux[0]: hex, Aux[1]: hex, Aux[2]: hex,
       Aux[3]: hex. 
       Only print Major, Minor and Aux[3].
       Transfer the three version to decimal and then hex print  */
    onlp_file_read_str(&bmc_fw_ver, BMC_VER_PREFIX"firmware_revision");
    sscanf(bmc_fw_ver, "%d.%x", &major_ver, &minor_ver);

    onlp_file_read_str(&aux_fw_ver, BMC_VER_PREFIX"aux_firmware_revision");
    sscanf(aux_fw_ver, "0x%x 0x%x 0x%x 0x%x", &aux_ver[1], &aux_ver[2], &aux_ver[3], &aux_ver[4]);

    /* BIOS version */
    onlp_file_read_str(&bios_ver, BIOS_VER_PATH);
    /* ONIE version */
    onlp_onie_decode_file(&onie, IDPROM_PATH);
    /* Read CPLD version
     */
    for (i = 0; i < AIM_ARRAYSIZE(cplds); i++) {
        onlp_file_read_int(&cplds[i].version, 
                            CPLD_VERSION_FORMAT, 
                            cplds[i].attr_name);
    }

    pi->cpld_versions = aim_fstrdup("\r\n\t   %s: %02X"
                                    "\r\n\t   %s: %02X"
                                    "\r\n\t   %s: %02X"
                                    "\r\n\t   %s: %02X"
                                    "\r\n\t   %s: %02X"
                                    , cplds[0].description, cplds[0].version
                                    , cplds[1].description, cplds[1].version
                                    , cplds[2].description, cplds[2].version
                                    , cplds[3].description, cplds[3].version
                                    , cplds[4].description, cplds[4].version);

    pi->other_versions = aim_fstrdup("\r\n\t   BIOS: %s\r\n\t   ONIE: %s"
                                     "\r\n\t   BMC: %02X.%02X.%02X"
                                    ,bios_ver, onie.onie_version
                                    ,major_ver, minor_ver, aux_ver[4]);

    onlp_onie_info_free(&onie);
    AIM_FREE_IF_PTR(bios_ver);
    AIM_FREE_IF_PTR(bmc_fw_ver);
    AIM_FREE_IF_PTR(aux_fw_ver);

    return ONLP_STATUS_OK;
}

void onlp_sysi_platform_info_free(onlp_platform_info_t* pi)
{
    aim_free(pi->cpld_versions);
    aim_free(pi->other_versions);
}
