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
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>

#include <onlp/platformi/sysi.h>
#include <onlp/platformi/ledi.h>
#include <onlp/platformi/thermali.h>
#include <onlp/platformi/fani.h>
#include <onlp/platformi/psui.h>
#include "platform_lib.h"
#include "x86_64_accton_as9736_64d_int.h"
#include "x86_64_accton_as9736_64d_log.h"

#define BIOS_VER_PATH "/sys/devices/virtual/dmi/id/bios_version"
#define UDB_CPLD_VER_PATH "/sys/devices/platform/as9736_64d_fpga/udb_cpld1_version"
#define UDB_FPGA_VER_PATH "/sys/devices/platform/as9736_64d_fpga/udb_version"
#define NUM_OF_FAN_ON_MAIN_BROAD    6
#define PREFIX_PATH_ON_CPLD_DEV    "/sys/bus/i2c/devices/"

#define NUM_OF_I2C_CPLD            4
/* (SMB_LDB_UDB FPGA) = same version
   (UDB_LDB_CPLD1_2)  = same version ==> integrate into 2 CPLD*/
#define NUM_OF_PCIE_CPLD           2
#define NUM_OF_CPLD                NUM_OF_I2C_CPLD + NUM_OF_PCIE_CPLD

#define FAN_DUTY_CYCLE_MAX         (100)
#define FAN_DUTY_CYCLE_DEFAULT     (38)


static char arr_cplddev_name[NUM_OF_I2C_CPLD][10] =
{
	"6-0060",    /*SMB CPLD*/
	"25-0033",   /*FAN CPLD*/
	"36-0060",   /*PDB CPLD*/
	"51-0035"    /*SCM CPLD*/
};

const char* onlp_sysi_platform_get(void)
{
	return "x86-64-accton-as9736-64d-r0";
}

int onlp_sysi_onie_data_get(uint8_t** data, int* size)
{
	const int len = 256;
	uint8_t* rdata = aim_zmalloc(len + 1);

	if (onlp_file_read(rdata, len, size, IDPROM_PATH) == ONLP_STATUS_OK) {
		if (*size == len) {
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

	/* 7 Thermal sensors on the chassis */
	for (i = 1; i <= CHASSIS_THERMAL_COUNT; i++)
		*e++ = ONLP_THERMAL_ID_CREATE(i);

	/* 5 LEDs on the chassis */
	for (i = 1; i <= CHASSIS_LED_COUNT; i++)
		*e++ = ONLP_LED_ID_CREATE(i);

	/* 2 PSUs on the chassis */
	for (i = 1; i <= CHASSIS_PSU_COUNT; i++)
		*e++ = ONLP_PSU_ID_CREATE(i);

	/* 6 Fans on the chassis */
	for (i = 1; i <= CHASSIS_FAN_COUNT; i++)
		*e++ = ONLP_FAN_ID_CREATE(i);

	return 0;
}

int onlp_sysi_platform_info_get(onlp_platform_info_t* pi)
{
    int i = 0;
    char result[40] = {0};
    char    *v[NUM_OF_CPLD] = { NULL };
    char *path[NUM_OF_CPLD] = { NULL, NULL, NULL, NULL, UDB_CPLD_VER_PATH, UDB_FPGA_VER_PATH };
    char *bios_ver = NULL;

    char onie_version[15] = {0};
    uint8_t* eeprom_data = NULL;
    int size;

    for (i = 0; i < NUM_OF_CPLD ; i++) {
        if( i < NUM_OF_I2C_CPLD ) {
            memset(result, 0, sizeof(result));
            sprintf(result, "%s%s%s", PREFIX_PATH_ON_CPLD_DEV, arr_cplddev_name[i], "/version");
            path[i] = result;
        }
        onlp_file_read_str(&v[i], path[i]);
    }

    onlp_file_read_str(&bios_ver, BIOS_VER_PATH);

    onlp_sysi_onie_data_get(&eeprom_data, &size);

    if (eeprom_data != NULL) {
        for (i = 0x7f; i < 0x8c; i++) {
            onie_version[i-0x7f] =  eeprom_data[i];
        }
        onie_version[13] = '\0';
    }

    pi->cpld_versions = aim_fstrdup("\r\n\t   SMB CPLD: %s"
                                    "\r\n\t   FCM CPLD: %s"
                                    "\r\n\t   PDB CPLD: %s"
                                    "\r\n\t   SCM CPLD: %s"
                                    "\r\n\t   UDB CPLD_1_2: %s"
                                    "\r\n\t   LDB CPLD_1_2: %s\r\n", v[0], v[1], v[2], v[3], v[4], v[4]);

    pi->other_versions = aim_fstrdup("\r\n\t   UDB FPGA: %s"
                                     "\r\n\t   LDB FPGA: %s"
                                     "\r\n\t   SMB FPGA: %s"
                                     "\r\n\t   BIOS: %s"
                                     "\r\n\t   ONIE: %s", v[5], v[5], v[5], bios_ver, onie_version);

    for (i = 0; i <  AIM_ARRAYSIZE(v) ; i++) {
        AIM_FREE_IF_PTR(v[i]);
    }
    AIM_FREE_IF_PTR(bios_ver);
    AIM_FREE_IF_PTR(eeprom_data);

    return 0;
}

void onlp_sysi_platform_info_free(onlp_platform_info_t* pi)
{
	aim_free(pi->cpld_versions);
	aim_free(pi->other_versions);
}

/*
 * Air Flow Front to Back :
 * (Thermal sensor_LM75_4A + Thermal sensor_LM75_CPU) /2 <=38C : Keep 37.5%(0x04) Fan speed
 * (Thermal sensor_LM75_4A + Thermal sensor_LM75_CPU) /2 > 38C : Change Fan speed from 37.5%(0x04) to 62.5%(0x08)
 * (Thermal sensor_LM75_4A + Thermal sensor_LM75_CPU) /2 > 46C : Change Fan speed from 62.5%(0x08) to 100%(0x0E)
 * (Thermal sensor_LM75_4A + Thermal sensor_LM75_CPU) /2 > 58C : Send alarm message
 * (Thermal sensor_LM75_4A + Thermal sensor_LM75_CPU) /2 > 66C : Shut down system
 * One Fan fail : Change Fan speed to 100%(0x0E)

 * Air Flow Back to Front :
 * (Thermal sensor_LM75_4A + Thermal sensor_LM75_CPU) /2 <=34C : Keep 37.5%(0x04) Fan speed
 * (Thermal sensor_LM75_4A + Thermal sensor_LM75_CPU) /2 > 34C : Change Fan speed from 37.5%(0x04) to 62.5%(0x08)
 * (Thermal sensor_LM75_4A + Thermal sensor_LM75_CPU) /2 > 44C : Change Fan speed from 62.5%(0x08) to 100%(0x0E)
 * (Thermal sensor_LM75_4A + Thermal sensor_LM75_CPU) /2 > 59C : Send alarm message
 * (Thermal sensor_LM75_4A + Thermal sensor_LM75_CPU) /2 > 67C : Shut down system
 * One Fan fail:  Change Fan speed to 100%(0x0E)
 */


typedef struct fan_ctrl_policy {
	int duty_cycle;
	int pwm;
	int temp_down; /* The boundary temperature to down adjust fan speed */
	int temp_up;   /* The boundary temperature to up adjust fan speed */
	int state;
} fan_ctrl_policy_t;

enum
{
	LEVEL_FAN_DEF=0,
	LEVEL_FAN_MID,
	LEVEL_FAN_MAX,
	LEVEL_TEMP_HIGH,
	LEVEL_TEMP_CRITICAL
};

fan_ctrl_policy_t  fan_thermal_policy_f2b[] = {
	{50,  0x7, 0,     39000,   LEVEL_FAN_DEF},
	{75,  0xB, 39000, 46000,   LEVEL_FAN_MID},
	{100, 0xF, 46000, 50000,   LEVEL_FAN_MAX},
	{100, 0xF, 50000, 55000,   LEVEL_TEMP_HIGH},
	{100, 0xF, 55000, 200000,  LEVEL_TEMP_CRITICAL}
};

fan_ctrl_policy_t  fan_thermal_policy_b2f[] = {
	{75,  0xB, 0,     32000,   LEVEL_FAN_DEF},
	{100, 0xF, 32000, 35000,   LEVEL_FAN_MID},
	{100, 0xF, 35000, 40000,   LEVEL_FAN_MAX},
	{100, 0xF, 40000, 45000,   LEVEL_TEMP_HIGH},
	{100, 0xF, 45000, 200000,  LEVEL_TEMP_CRITICAL}
};

#define FAN_SPEED_CTRL_PATH \
	"/sys/bus/i2c/devices/14-0066/fan_duty_cycle_percentage"
#define FAN_DIRECTION_PATH "/sys/bus/i2c/devices/14-0066/fan1_direction"

static int fan_state=LEVEL_FAN_DEF;
static int alarm_state = 0; /* 0->default or clear, 1-->alarm detect */
static int fan_fail = 0;

int onlp_sysi_platform_manage_fans(void)
{
	int i=0, ori_state=LEVEL_FAN_DEF, current_state=LEVEL_FAN_DEF;
	int  fd, len, value = 1;
	int cur_duty_cycle, new_duty_cycle, temp = 0;
	onlp_thermal_info_t thermal_4, thermal_5;
	char  buf[10] = {0};
	fan_ctrl_policy_t *fan_thermal_policy;

 	/* Get fan direction
 	 */
	if (onlp_file_read_int(&value, FAN_DIRECTION_PATH) < 0)
		AIM_LOG_ERROR("Unable to read status from file (%s)\r\n",
			      FAN_DIRECTION_PATH);

	if (value == 0)
		fan_thermal_policy=fan_thermal_policy_f2b;
	else
		fan_thermal_policy=fan_thermal_policy_b2f;

	/* Get current temperature
	 */
	if (onlp_thermali_info_get(ONLP_THERMAL_ID_CREATE(4), &thermal_4) != 
	    ONLP_STATUS_OK) {
		AIM_LOG_ERROR("Unable to read thermal status, set fans to 75 %% speed");
		onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1), 
					 fan_thermal_policy[LEVEL_FAN_MID].duty_cycle);
		return ONLP_STATUS_E_INTERNAL;
	}

	if (onlp_thermali_info_get(ONLP_THERMAL_ID_CREATE(5), &thermal_5) != 
	   ONLP_STATUS_OK) {
		AIM_LOG_ERROR("Unable to read thermal status, set fans to 75 %% speed");
		onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1), 
				fan_thermal_policy[LEVEL_FAN_MID].duty_cycle);
		return ONLP_STATUS_E_INTERNAL;
	}

	temp = (thermal_4.mcelsius + thermal_5.mcelsius) / 2;
	/* Get current fan pwm percent
	 */
	fd = open(FAN_SPEED_CTRL_PATH, O_RDONLY);
	if (fd == -1){
		AIM_LOG_ERROR("Unable to open fan speed control node (%s)", 
			      FAN_SPEED_CTRL_PATH);
		return ONLP_STATUS_E_INTERNAL;
	}
	len = read(fd, buf, sizeof(buf));
	close(fd);
	if (len <= 0){
		AIM_LOG_ERROR("Unable to read fan speed from (%s)", 
			      FAN_SPEED_CTRL_PATH);
		return ONLP_STATUS_E_INTERNAL;
	}
	cur_duty_cycle = atoi(buf);
	ori_state=fan_state;
	/* Inpunt temp to get theraml_polyc state and new pwm percent. */
	for (i=0; i < sizeof(fan_thermal_policy_f2b)/sizeof(fan_ctrl_policy_t);
	    i++) {
		if (temp > fan_thermal_policy[i].temp_down) {
			if (temp <= fan_thermal_policy[i].temp_up)
				current_state = i;
		}
	}

	if (current_state > LEVEL_TEMP_CRITICAL ||
	    current_state < LEVEL_FAN_DEF) {
		AIM_LOG_ERROR("onlp_sysi_platform_manage_fans get error  current_state\n");
		return 0;
	}

	/* Decision 3: Decide new fan pwm percent.
	 */
	if (fan_fail == 0 && cur_duty_cycle != 
	    fan_thermal_policy[current_state].duty_cycle) {
		new_duty_cycle = fan_thermal_policy[current_state].duty_cycle;
		onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1), 
					 new_duty_cycle);
	}

	/* Get each fan status
	 */

	for (i = 1; i <= NUM_OF_FAN_ON_MAIN_BROAD; i++) {
		onlp_fan_info_t fan_info;

		if (onlp_fani_info_get(ONLP_FAN_ID_CREATE(i), &fan_info) != 
		    ONLP_STATUS_OK) {
			AIM_LOG_ERROR("Unable to get fan(%d) status, try to set the other fans as full speed\r\n",
				      i);
			onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1),
						 FAN_DUTY_CYCLE_MAX);
			fan_fail = 1;
			break;
		} else {
			fan_fail = 0;
		}

		/* Decision 1: Set fan as full speed if any fan is failed.
		 */
		if (fan_info.status & ONLP_FAN_STATUS_FAILED || 
		    !(fan_info.status & ONLP_FAN_STATUS_PRESENT)) {
			AIM_LOG_ERROR("Fan(%d) is not working, set the other fans as full speed\r\n", 
				      i);
			onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1),
						 FAN_DUTY_CYCLE_MAX);
			fan_fail = 1;
			break;
		} else {
			fan_fail = 0;
		}
	}

	if (current_state != ori_state) {
		fan_state = current_state;

		switch (ori_state) {
		case LEVEL_FAN_DEF:
			if (current_state == LEVEL_TEMP_HIGH) {
				if (alarm_state == 0) {
					AIM_SYSLOG_WARN("Temperature high",
							"Temperature high",
							"Alarm for temperature high is detected");
					alarm_state = 1;
				}
			}
			if (current_state == LEVEL_TEMP_CRITICAL) {
				AIM_SYSLOG_CRIT("Temperature critical",
						"Temperature critical",
						"Alarm for temperature critical is detected, reboot DUT");
				system("sync;sync;sync");
				system("reboot");
			}
			break;
		case LEVEL_FAN_MID:
			if (current_state == LEVEL_TEMP_HIGH) {
				if (alarm_state == 0) {
					AIM_SYSLOG_WARN("Temperature high",
							"Temperature high",
							"Alarm for temperature high is detected");
					alarm_state = 1;
				}
			}

			if (current_state == LEVEL_TEMP_CRITICAL) {
				AIM_SYSLOG_CRIT("Temperature critical",
						"Temperature critical", 
						"Alarm for temperature critical is detected, reboot DUT");
				system("sync;sync;sync");
				system("reboot");
			}
			break;
		case LEVEL_FAN_MAX:
			if (current_state == LEVEL_TEMP_HIGH) {
				if (alarm_state == 0) {
					AIM_SYSLOG_WARN("Temperature high",
							"Temperature high",
							"Alarm for temperature high is detected");
					alarm_state = 1;
				}
			}
			if (current_state == LEVEL_TEMP_CRITICAL) {
				AIM_SYSLOG_CRIT("Temperature critical",
						"Temperature critical ",
						"Alarm for temperature critical is detected, reboot DUT");
				system("sync;sync;sync");
				system("reboot");
			}
			break;
		case LEVEL_TEMP_HIGH:
			if (current_state == LEVEL_TEMP_CRITICAL) {
				AIM_SYSLOG_CRIT("Temperature critical",
						"Temperature critical ",
						"Alarm for temperature critical is detected, reboot DUT");
				system("sync;sync;sync");
				system("reboot");
			}
			break;
		case LEVEL_TEMP_CRITICAL:
			break;
		default:
				AIM_SYSLOG_WARN("onlp_sysi_platform_manage_fans abnormal state",
						"onlp_sysi_platform_manage_fans abnormal state",
						"onlp_sysi_platform_manage_fans at abnormal state\n");
			break;
		}
	}

	if(alarm_state == 1 && current_state < LEVEL_TEMP_HIGH) {
		if (temp < 
		    (fan_thermal_policy[LEVEL_TEMP_HIGH].temp_down - 5000)) {  /*below 58 C, clear alarm*/
			AIM_SYSLOG_INFO("Temperature high is clean",
					"Temperature high is clear",
					"Alarm for temperature high is cleared");
			alarm_state = 0;
		}
	}

	return 0;
}

int onlp_sysi_platform_manage_leds(void)
{
	return ONLP_STATUS_E_UNSUPPORTED;
}
