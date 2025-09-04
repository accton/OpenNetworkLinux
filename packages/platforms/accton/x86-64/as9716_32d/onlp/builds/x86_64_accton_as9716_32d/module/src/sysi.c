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

#include <onlplib/i2c.h>
#include <onlp/platformi/sysi.h>
#include <onlp/platformi/ledi.h>
#include <onlp/platformi/thermali.h>
#include <onlp/platformi/fani.h>
#include <onlp/platformi/psui.h>
#include <onlp/platformi/sfpi.h>
#include "platform_lib.h"
#include "x86_64_accton_as9716_32d_int.h"
#include "x86_64_accton_as9716_32d_log.h"

#define NUM_OF_FAN_ON_MAIN_BROAD      6
#define PREFIX_PATH_ON_CPLD_DEV          "/sys/bus/i2c/devices/"
#define NUM_OF_CPLD                      3
#define FAN_DUTY_CYCLE_MAX         (100)
#define FAN_DUTY_CYCLE_75     (75)
#define FAN_DUTY_CYCLE_50     (50)

static char arr_cplddev_name[NUM_OF_CPLD][10] =
{
 "4-0060",
 "5-0062",
 "6-0064"
};

const char*
onlp_sysi_platform_get(void)
{
    return "x86-64-accton-as9716-32d-r0";
}

int
onlp_sysi_onie_data_get(uint8_t** data, int* size)
{
    uint8_t* rdata = aim_zmalloc(256);

    /*New board eeprom i2c-addr is 0x57. Old board's eeprom i2c-addr is 0x56*/
    if(onlp_file_read(rdata, 256, size, IDPROM_PATH_1) == ONLP_STATUS_OK) /*0x57*/
    {
        if(*size == 256)
        {
            *data = rdata;
            return ONLP_STATUS_OK;
        }
    }
    else
    {
        if(onlp_file_read(rdata, 256, size, IDPROM_PATH_2) == ONLP_STATUS_OK) /*0x56*/
        {
            if(*size == 256)
            {
                *data = rdata;
                return ONLP_STATUS_OK;
            }
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

    /* 8 Thermal sensors on the chassis */
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

int
onlp_sysi_platform_info_get(onlp_platform_info_t* pi)
{
    int   i, v[NUM_OF_CPLD]={0};

    for (i = 0; i < NUM_OF_CPLD; i++) {
        v[i] = 0;

        if(onlp_file_read_int(v+i, "%s%s/version", PREFIX_PATH_ON_CPLD_DEV, arr_cplddev_name[i]) < 0) {
            return ONLP_STATUS_E_INTERNAL;
        }
    }
    pi->cpld_versions = aim_fstrdup("%d.%d.%d", v[0], v[1], v[2]);

    return 0;
}

void
onlp_sysi_platform_info_free(onlp_platform_info_t* pi)
{
    aim_free(pi->cpld_versions);
}

/*Read fanN_direction=1: The air flow of Fan6 is "AFI-Back to Front"
 *                    0: The air flow of Fan6 is "AFO-Front to back"
 */
/*
 # Thermal policy:
   a.Defaut fan duty_cycle=100%
   b.One fan fail, set to fan duty_cycle=100%

  1.For AFI:
    Default fan duty_cycle will be 100%(fan_policy_state=LEVEL_FAN_MAX).
    If all below case meet with, set to 75%(LEVEL_FAN_MID).
    (MB board)
        LM75-1(0X48)<=45.5
        LM75-2(0X49)<=39.5
        LM75-3(0X4A)<=37.5
        LM75-4(0X4C)<=38.5
        LM75-5(0X4E)<=34.5
        LM75-6(0X4F)<=37
    (CPU board)
        Core(1~4) <=40
        LM75-1(0X4B)<=30.5
        ZR<=62

    When fan_policy_state=LEVEL_FAN_MID, meet with below case,  Fan duty_cycle will be 100%(LEVEL_FAN_MAX)
    (MB board)
        LM75-1(0X48)>=51.5
        LM75-2(0X49)>=44.5
        LM75-3(0X4A)>=43.5
        LM75-4(0X4C)>=43.5
        LM75-5(0X4E)>=40
        LM75-6(0X4F)>=42.5
    (CPU board)
        Core-1>=45, Core-2>=45, Core-3>=46, Core-4>=46
        LM75-1(0X4B)>=35.5
        ZR>=65

    # Red Alarm
    (MB board)
        LM75-1(0X48)>=65
        LM75-2(0X49)>=58
        LM75-3(0X4A)>=57
        LM75-4(0X4C)>=57
        LM75-5(0X4E)>=57
        LM75-6(0X4F)>=60
    (CPU board)
        Core>=60
        LM75-1(0X4B)>=50
        ZR>=75

    # Shutdown
    (MB board)
        LM75-1(0X48)>=71
        LM75-2(0X49)>=64
        LM75-3(0X4A)>=63
        LM75-4(0X4C)>=63
        LM75-5(0X4E)>=63
        LM75-6(0X4F)>=66
     (CPU board)
        Core>=66
        LM75-1(0X4B)>=56
        ZR>=82

  2.For AFO:
    At default, FAN duty_cycle was 100%(LEVEL_FAN_MAX). If all below case meet with, set to 75%(LEVEL_FAN_MID).
    (MB board)
        LM75-1(0X48)<=47
        LM75-2(0X49)<=47
        LM75-3(0X4A)<=47
        LM75-4(0X4C)<=47
        LM75-5(0X4E)<=47
        LM75-6(0X4F)<=47
    (CPU board)
        Core-(1~4)<=55
        LM75-1(0X4B)<=40
        ZR<=60

    When FAN duty_cycle was 75%(LEVEL_FAN_MID). If all below case meet with, set to 50%(LEVEL_FAN_DEF).
    (MB board)
        LM75-1(0X48)<=40
        LM75-2(0X49)<=40
        LM75-3(0X4A)<=40
        LM75-4(0X4C)<=40
        LM75-5(0X4E)<=40
        LM75-6(0X4F)<=40
    (CPU board)
        Core-(1~4)<=50
        LM75-1(0X4B)<=33
        ZR<=55

    When fan_speed 50%(LEVEL_FAN_DEF).
    Meet with below case, Fan duty_cycle will be 75%(LEVEL_FAN_MID)
    (MB board)
        LM75-1(0X48)>=63
        LM75-2(0X49)>=63
        LM75-3(0X4A)>=63
        LM75-4(0X4C)>=63
        LM75-5(0X4E)>=63
        LM75-6(0X4F)>=63
    (CPU board)
        Core-(1~4)>=73
        LM75-1(0X4B)>=50
        ZR>=65

    When FAN duty_cycle was 75%(LEVEL_FAN_MID). If all below case meet with, set to 100%(LEVEL_FAN_MAX).
    (MB board)
        LM75-1(0X48)>=68
        LM75-2(0X49)>=68
        LM75-3(0X4A)>=68
        LM75-4(0X4C)>=68
        LM75-5(0X4E)>=68
        LM75-6(0X4F)>=68
        (CPU board)
        Core-(1~4)>=77
        LM75-1(0X4B)>=55
        ZR>=70

    # Red Alarm
    (MB board)
        LM75-1(0X48)>=72
        LM75-2(0X49)>=72
        LM75-3(0X4A)>=72
        LM75-4(0X4C)>=72
        LM75-5(0X4E)>=72
        LM75-6(0X4F)>=72
    (CPU board)
        Core>=81
        LM75-1(0X4B)>=60
        ZR>=75

    # Shutdown
    (MB board)
        LM75-1(0X48)>=78
        LM75-2(0X49)>=78
        LM75-3(0X4A)>=78
        LM75-4(0X4C)>=78
        LM75-5(0X4E)>=78
        LM75-6(0X4F)>=78
     (CPU board)
        Core>=87
        LM75-1(0X4B)>=70
        ZR>=82
 */

 #define MONITOR_PORT_NUM 8
 static int monitor_port[MONITOR_PORT_NUM] = {5, 6, 11, 12, 19, 20, 31, 32};

typedef struct afi_temp_range{
    int mid_to_max_temp[8];
    int max_to_mid_temp[8];
    int max_to_red_alarm_temp[8];
    int red_alarm_to_shutdown_temp[8];
    int xcvr_mid_to_max_temp;
    int xcvr_max_to_mid_temp;
    int xcvr_max_to_red_alarm_temp;
    int xcvr_red_alarm_to_shutdown_temp;
}afi_temp_range_t;

afi_temp_range_t afi_thermal_spec={
    {51500, 44500, 43500, 43500, 40000, 42500, 45000, 35500},
    {45500, 39500, 37500, 38500, 34500, 37000, 44000, 35000},
    {65000, 58000, 57000, 57000, 57000, 60000, 60000, 50000},
    {71000, 64000, 63000, 63000, 63000, 66000, 66000, 56000},
    65000, 62000, 75000, 82000
};

typedef struct afo_temp_range{
    int min_to_mid_temp[8];
    int mid_to_max_temp[8];
    int max_to_mid_temp[8];
    int mid_to_min_temp[8];
    int max_to_red_alarm_temp[8];
    int red_alarm_to_shutdown_temp[8];
    int xcvr_min_to_mid_temp;
    int xcvr_mid_to_max_temp;
    int xcvr_max_to_mid_temp;
    int xcvr_mid_to_min_temp;
    int xcvr_max_to_red_alarm_temp;
    int xcvr_red_alarm_to_shutdown_temp;
} afo_temp_range_t;

afo_temp_range_t afo_thermal_spec={
    {63000, 63000, 63000, 63000, 63000, 63000, 73000, 50000},
    {68000, 68000, 68000, 68000, 68000, 68000, 77000, 55000},
    {47000, 47000, 47000, 47000, 47000, 47000, 55000, 40000},
    {40000, 40000, 40000, 40000, 40000, 40000, 50000, 33000},
    {72000, 72000, 72000, 72000, 72000, 72000, 81000, 60000},
    {78000, 78000, 78000, 78000, 78000, 78000, 87000, 70000},
    65000, 70000, 60000, 55000, 75000, 82000
};

typedef struct fan_ctrl_policy {
   int duty_cycle;
   int pwm;
   int state;
} fan_ctrl_policy_t;

/*For AFI. 2 state. LEVEL_FAN_MID(75%), LEVEL_FAN_MAX(100%)
  For AFO. 3 state. LEVEL_FAN_MIN(50%), LEVEL_FAN_MID(75%), LEVEL_FAN_MAX(100%)
 */
enum
{
   LEVEL_FAN_INIT=0,
   LEVEL_FAN_MIN=1,
   LEVEL_FAN_MID=2,
   LEVEL_FAN_MAX=3,
   LEVEL_FAN_DEF=LEVEL_FAN_MAX,
   LEVEL_FAN_RED_ALARM=4,
   LEVEL_FAN_SHUTDOWN=5,
};

fan_ctrl_policy_t  fan_thermal_policy_f2b[] = { /*AFO*/
    {50,  0x7, LEVEL_FAN_MIN},
    {75,  0xb, LEVEL_FAN_MID},
    {100, 0xf, LEVEL_FAN_MAX},
};

fan_ctrl_policy_t  fan_thermal_policy_b2f[] = { /*AFI*/
    {75,  0xb, LEVEL_FAN_MID},
    {100, 0xf, LEVEL_FAN_MAX}
};

void onlp_sysi_shutdown(void)
{
    int ret;

    /* Sync log buffer to disk */
    ret = system("sync");
    if (ret != 0) {
        AIM_LOG_ERROR("sync failed (ret=%d)\n", ret);
        return;
    }

    ret = system("/sbin/fstrim -av");
    if (ret != 0) {
        AIM_LOG_ERROR("fstrim failed (ret=%d)\n", ret);
        return;
    }

    system("sleep 3");

    ret = system("i2cset -y -f 19 0x60 0x60 0x10");
    if (ret != 0) {
        AIM_LOG_ERROR("i2cset failed (ret=%d)\n", ret);
        return;
    }
}

int onlp_sysi_get_duty_cycle_by_fan_state(int state, int direction)
{
    int i;
    if(direction)
    {
        for(i=0; i< 2; i++)
        {
            if(state==fan_thermal_policy_b2f[i].state)
            {
                return fan_thermal_policy_b2f[i].duty_cycle;
            }
        }
    }
    else
    {
        for(i=0; i< 3; i++)
        {
            if(state==fan_thermal_policy_f2b[i].state)
            {
                return fan_thermal_policy_f2b[i].duty_cycle;
            }
        }
    }
    return 0;

}
/*
 * If only one PSU insert , and watt >800w. Must let DUT fan pwm >= 75% in AFO.
 *  Because the psu temp is high.
 */

/* Return 1: full load
 * Return 0: Not full load
 */
int onlp_sysi_check_psu_loading(void)
{
    int psu_power_good[2]={1, 1};
    int psu_p_in[2]={0, 0};
    int psu_p_out[2]={0, 0};
    int id=1;
    int check_psu_watt=0;

    for (id=1; id<=2; id++)
    {
        if (psu_status_info_get(id, "psu_power_good", &psu_power_good[id-1]) != 0) {
            AIM_LOG_ERROR("Unable to read PSU(%d) node(psu_power_good)\r\n", id);
        }
        if (psu_power_good[id-1] != PSU_STATUS_POWER_GOOD)
        {
            check_psu_watt=1;
        }
    }
    if (check_psu_watt)
    {
        for (id=1; id<=2; id++)
        {
            if(psu_power_good[id-1]== PSU_STATUS_POWER_GOOD)
            {  /*check watt*/
                 if (psu_pmbus_info_get(id, "psu_p_in", &psu_p_in[id-1]) == 0)
                 {
                    if (psu_p_in[id-1]/1000 > 800)
                    {
                        return 1;
                    }
                 }
                 if (psu_pmbus_info_get(id, "psu_p_out", &psu_p_out[id-1]) == 0)
                 {
                    if (psu_p_out[id-1]/1000 > 800)
                    {
                        return 1;
                    }
                 }
            }
        }
        return 0;
    }
    else
        return 0;


    return 0;

}

#define FAN_SPEED_CTRL_PATH "/sys/bus/i2c/devices/17-0066/fan_duty_cycle_percentage"
#define FAN_DIRECTION_PATH "/sys/bus/i2c/devices/17-0066/fan1_direction"
#define CHECK_TIMES 3

static int fan_state=LEVEL_FAN_INIT;
static int fan_fail = 0;

static int fan_alarm_state=LEVEL_FAN_INIT;
static int send_red_alarm=0;
static int count_check=0;

int current_duty_cycle, new_duty_cycle;

int onlp_sysi_get_monitor_xcvr_presence(void)
{
    onlp_sfp_bitmap_t bitmap;
    onlp_sfp_bitmap_t_init(&bitmap);
    onlp_sfp_presence_bitmap_get(&bitmap);

    int i = 0, port = 0, ret = 0;
    /*
     * return 0: No monitor ports are present
     *        1: At least one monitor port is present
     */
    for (i = 0; i < MONITOR_PORT_NUM; i++) {
        port = monitor_port[i] - 1;
        ret = ret | AIM_BITMAP_GET(&bitmap, port);
    }

    return !(ret == 0);
}

int onlp_sysi_get_sff8436_temp(int port, int *temp)
{
    int value;
    int16_t port_temp;

    /* Read memory model */
    value = onlp_sfpi_dev_readb(port, 0x50, 0x2);
    if (value & 0x04) {
        *temp = ONLP_STATUS_E_MISSING;
        return ONLP_STATUS_OK;
    }

    value = onlp_sfpi_dev_readb(port, 0x50, 22);
    if (value < 0) {
        *temp = ONLP_STATUS_E_MISSING;
        return ONLP_STATUS_OK;
    }
    port_temp = (int16_t)((value & 0xFF) << 8);

    value = onlp_sfpi_dev_readb(port, 0x50, 23);
    if (value < 0) {
        *temp = ONLP_STATUS_E_MISSING;
        return ONLP_STATUS_OK;
    }
    port_temp = (port_temp | (int16_t)(value & 0xFF));
    *temp = (int)port_temp * 1000 / 256;

    return ONLP_STATUS_OK;
}

int onlp_sysi_get_cmis_temp(int port, int *temp)
{
    int value;
    int16_t port_temp;

    /* Read memory model */
    value = onlp_sfpi_dev_readb(port, 0x50, 0x2);
    if (value & 0x80) {
        *temp = ONLP_STATUS_E_MISSING;
        return ONLP_STATUS_OK;
    }

    value = onlp_sfpi_dev_readb(port, 0x50, 14);
    if (value < 0) {
        *temp = ONLP_STATUS_E_MISSING;
        return ONLP_STATUS_OK;
    }
    port_temp = (int16_t)((value & 0xFF) << 8);

    value = onlp_sfpi_dev_readb(port, 0x50, 15);
    if (value < 0) {
        *temp = ONLP_STATUS_E_MISSING;
        return ONLP_STATUS_OK;
    }
    port_temp = (port_temp | (int16_t)(value & 0xFF));

    *temp = (int)port_temp * 1000 / 256;
    return ONLP_STATUS_OK;
}

int onlp_sysi_get_xcvr_temp(int port, int *temp)
{
    int value = 0, ret = 0;
    int port_temp = 0;

    *temp = ONLP_STATUS_E_MISSING;

    if (!onlp_sfpi_is_present(port)) {
        return ONLP_STATUS_OK;
    }

    /*Check xcvr identifier*/
    value = onlp_sfpi_dev_readb(port, 0x50, 0);
    if (value < 0) {
        AIM_LOG_ERROR("Unable to get read port(%d) eeprom\r\n", port);
        return ONLP_STATUS_OK;
    }
    if (value == 0x18 || value == 0x19 || value == 0x1E) {
        ret = onlp_sysi_get_cmis_temp(port, &port_temp);
        if (ret != ONLP_STATUS_OK) {
            return ONLP_STATUS_OK;
        }
    }
    else if (value == 0x0C || value == 0x0D || value == 0x11 || value ==  0xE1) {
        ret = onlp_sysi_get_sff8436_temp(port, &port_temp);
        if (ret != ONLP_STATUS_OK) {
            return ONLP_STATUS_OK;
        }
    }
    *temp = port_temp;

    return ONLP_STATUS_OK;
}

int onlp_sysi_platform_manage_fans(void)
{
    int i=0,k=0, ori_state=LEVEL_FAN_DEF, current_state=LEVEL_FAN_DEF;
    int fd, len, direction_val=1;
    int max_to_mid=0, mid_to_min=0; /* Only this flag equal to 8, otherwise state can not to be down.*/
    int psu_full_load=0;
    int port = 0;
    int port_temp = ONLP_STATUS_E_MISSING, max_port_temp = ONLP_STATUS_E_MISSING;
    int check_xcvr_temp = 0, xcvr_shutdown_flag = 0;
    onlp_thermal_info_t thermal[8];
    char  buf[10] = {0};

    /* Get fan direction
     */
    if (onlp_file_read_int(&direction_val, FAN_DIRECTION_PATH) < 0) {
        AIM_LOG_ERROR("Unable to read status from file (%s)\r\n", FAN_DIRECTION_PATH);
    }

    if(fan_state==LEVEL_FAN_INIT)
    {
        fan_state=LEVEL_FAN_MAX; /*This is default state*/
        onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1), 100);
        return ONLP_STATUS_OK;
    }

    count_check++;
    if(count_check < CHECK_TIMES)
        return ONLP_STATUS_OK;
    else
        count_check=0;

    /* Get current temperature
     */
    for (i=2; i <5; i++)
    {
        if (onlp_thermali_info_get(ONLP_THERMAL_ID_CREATE(i), &thermal[k]) != ONLP_STATUS_OK  )
        {
            AIM_LOG_ERROR("Unable to read thermal status, set fans to full speed");
            onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1), 100);
            return ONLP_STATUS_E_INTERNAL;
       }
        k++;
    }
    for (i=6; i <=8; i++)
    {
        if (onlp_thermali_info_get(ONLP_THERMAL_ID_CREATE(i), &thermal[k]) != ONLP_STATUS_OK  )
        {
            AIM_LOG_ERROR("Unable to read thermal status, set fans to full speed");
            onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1), 100);
            return ONLP_STATUS_E_INTERNAL;
        }
        k++;
    }
    if (onlp_thermali_info_get(ONLP_THERMAL_ID_CREATE(1), &thermal[6]) != ONLP_STATUS_OK  )
    {
        AIM_LOG_ERROR("Unable to read thermal status, set fans to full speed");
        onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1), 100);
        return ONLP_STATUS_E_INTERNAL;
    }
    if (onlp_thermali_info_get(ONLP_THERMAL_ID_CREATE(5), &thermal[7]) != ONLP_STATUS_OK  )
    {
        AIM_LOG_ERROR("Unable to read thermal status, set fans to  full speed");
        onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1), 100);
        return ONLP_STATUS_E_INTERNAL;
    }

    /* Get xcvr current temperature
     */
    if ( onlp_sysi_get_monitor_xcvr_presence() != 0 )
    {
        for (i = 0; i < MONITOR_PORT_NUM; i++)
        {
            port = monitor_port[i] - 1;

            if(onlp_sysi_get_xcvr_temp(port, &port_temp) == ONLP_STATUS_OK)
            {
                if (port_temp > max_port_temp) {
                    max_port_temp = port_temp;
                }
            }
        }

        if( max_port_temp == ONLP_STATUS_E_INTERNAL ) {
            AIM_LOG_ERROR("Unable to get port temperature.\r\n");
            check_xcvr_temp = 0;
        }
        else {
            check_xcvr_temp = 1;
        }

    }
    else {
        check_xcvr_temp = 0; /*monitor xcvr port all unpresent*/
    }

    /* Get current fan pwm percent
     */
    fd = open(FAN_SPEED_CTRL_PATH, O_RDONLY);
    if (fd == -1){
        AIM_LOG_ERROR("Unable to open fan speed control node (%s)", FAN_SPEED_CTRL_PATH);
        return ONLP_STATUS_E_INTERNAL;
    }
    len = read(fd, buf, sizeof(buf));
    close(fd);
    if (len <= 0) {
        AIM_LOG_ERROR("Unable to read fan speed from (%s)", FAN_SPEED_CTRL_PATH);
        return ONLP_STATUS_E_INTERNAL;
    }
    current_duty_cycle = atoi(buf);
    ori_state=fan_state;
    current_state=fan_state;

    mid_to_min = 0;
    max_to_mid = 0;
    /* Check thermal is in which range. */
    if( direction_val == 1 ) /* AFI */
    {
        if( ori_state == LEVEL_FAN_MID )
        {
            for ( i = 0; i < CHASSIS_THERMAL_COUNT; i++ )
            {
                if( (thermal[i].mcelsius >= afi_thermal_spec.mid_to_max_temp[i]) &&
                    (check_xcvr_temp && max_port_temp >= afi_thermal_spec.xcvr_mid_to_max_temp ) )
                {
                   AIM_SYSLOG_WARN("Temperature is over the error threshold",
                                   "Temperature is over the error threshold",
                                   "Error threshold for temperature is detected");
                   current_state = LEVEL_FAN_MAX;
                   break;
                }
                else {
                    current_state = LEVEL_FAN_MID;
                }
            }
        }
        else /*LEVEL_FAN_MAX*/
        {
            for ( i = 0; i < CHASSIS_THERMAL_COUNT; i++ )
            {
                if( (thermal[i].mcelsius <= afi_thermal_spec.max_to_mid_temp[i]) &&
                    (check_xcvr_temp && max_port_temp <= afi_thermal_spec.xcvr_max_to_mid_temp) && (fan_fail==0) )
                {
                    max_to_mid++;
                }

                if( !fan_alarm_state )
                {
                    if( (thermal[i].mcelsius >= afi_thermal_spec.max_to_red_alarm_temp[i]) ||
                        (check_xcvr_temp && max_port_temp >= afi_thermal_spec.xcvr_max_to_red_alarm_temp) )
                    {
                        fan_alarm_state = LEVEL_FAN_RED_ALARM;
                        if( send_red_alarm == 0 )
                        {
                            send_red_alarm = 1;
                            AIM_SYSLOG_WARN("Temperature is over the critical threshold",
                                            "Temperature is over the critical threshold",
                                            "Critical threshold for temperature is detected");
                        }
                    }
                }
                else if( fan_alarm_state == LEVEL_FAN_RED_ALARM )
                {
                    if (thermal[i].mcelsius >= afi_thermal_spec.red_alarm_to_shutdown_temp[i])
                    {
                        fan_alarm_state = LEVEL_FAN_SHUTDOWN;
                        sleep(1);
                        AIM_SYSLOG_CRIT("Temperature is over the shutdown threshold",
                                        "Temperature is over the shutdown threshold",
                                        "Shutdown threshold for temperature is detected, Shutdown DUT");
                        onlp_sysi_shutdown();
                    }
                    /*ZR xcvr do HW protect*/
                    if (max_port_temp >= afi_thermal_spec.xcvr_red_alarm_to_shutdown_temp)
                    {
                        if(!xcvr_shutdown_flag) {
                            AIM_SYSLOG_CRIT("XCVR temperature is over the shutdown threshold",
                                            "XCVR temperature is over the shutdown threshold",
                                            "Shutdown threshold for xcvr temperature is detected");
                            xcvr_shutdown_flag = 1;
                        }
                    }
                }
            }

            if(max_to_mid==CHASSIS_THERMAL_COUNT && fan_state==LEVEL_FAN_MAX)
            {
                if (fan_fail==0)
                {
                    AIM_SYSLOG_INFO("temperature is less than the error threshold",
                                    "temperature is less than the error threshold",
                                    "Monitor all sensors, temperature is less than the error threshold of thermal policy.");
                    current_state=LEVEL_FAN_MID;
                }
                if (fan_alarm_state)
                {
                    fan_alarm_state=0;
                    send_red_alarm=0;
                    xcvr_shutdown_flag = 0;
                    AIM_SYSLOG_INFO("Temperature is over the error threshold is clean",
                                    "Temperature is over the error threshold is clear",
                                    "Alarm for temperature is over the critical threshold is cleared");
                }
            }
        }
    }
    else  /* AFO */
    {
        psu_full_load = onlp_sysi_check_psu_loading();

        if (ori_state==LEVEL_FAN_MIN)
        {
            if(psu_full_load==1)
            {
                 current_state = LEVEL_FAN_MID;
            }
            else
            {
                for (i=0; i <CHASSIS_THERMAL_COUNT; i++)
                {
                    if( (thermal[i].mcelsius >= afo_thermal_spec.min_to_mid_temp[i]) ||
                        (check_xcvr_temp && max_port_temp >= afo_thermal_spec.xcvr_min_to_mid_temp) ) {
                        AIM_SYSLOG_WARN("Temperature is over the warning threshold",
                                        "Temperature is over the warning threshold",
                                        "Warning threshold for temperature is detected");
                        current_state=LEVEL_FAN_MID;
                        break;
                    }
                }
            }

        }
        else if (ori_state == LEVEL_FAN_MID)
        {
            for (i=0; i <CHASSIS_THERMAL_COUNT; i++)
            {
                if ( (thermal[i].mcelsius >= afo_thermal_spec.mid_to_max_temp[i]) ||
                     (check_xcvr_temp && max_port_temp >= afo_thermal_spec.xcvr_mid_to_max_temp) )
                {
                    AIM_SYSLOG_WARN("Temperature is over the error threshold",
                                    "Temperature is over the error threshold",
                                    "Error threshold for temperature is detected");
                    current_state=LEVEL_FAN_MAX;
                    break;
                }
                else
                {
                    if ( (thermal[i].mcelsius <= afo_thermal_spec.mid_to_min_temp[i]) &&
                         (check_xcvr_temp && max_port_temp <= afo_thermal_spec.xcvr_mid_to_min_temp) && fan_fail==0 )
                    {
                        mid_to_min++;
                    }
                }
            }
        }
        else
        {
            for ( i = 0; i < CHASSIS_THERMAL_COUNT; i++ )
            {
                if ( (thermal[i].mcelsius <= afo_thermal_spec.max_to_mid_temp[i]) &&
                     (check_xcvr_temp && max_port_temp <= afo_thermal_spec.xcvr_max_to_mid_temp) && fan_fail==0 )
                {
                   max_to_mid++;
                }

                if( !fan_alarm_state )
                {
                    if( (thermal[i].mcelsius >= afo_thermal_spec.max_to_red_alarm_temp[i]) ||
                        (check_xcvr_temp && max_port_temp >= afo_thermal_spec.xcvr_max_to_red_alarm_temp) )
                    {
                        fan_alarm_state = LEVEL_FAN_RED_ALARM;
                        if( send_red_alarm == 0 )
                        {
                            send_red_alarm = 1;
                            AIM_SYSLOG_WARN("Temperature is over the critical threshold",
                                            "Temperature is over the critical threshold",
                                            "Critical threshold for temperature is detected");
                        }
                    }
                }
                else if( fan_alarm_state == LEVEL_FAN_RED_ALARM )
                {
                    if (thermal[i].mcelsius >= afo_thermal_spec.red_alarm_to_shutdown_temp[i])
                    {
                        fan_alarm_state = LEVEL_FAN_SHUTDOWN;
                        sleep(1);
                        AIM_SYSLOG_CRIT("Temperature is over the shutdown threshold",
                                        "Temperature is over the shutdown threshold",
                                        "Shutdown threshold for temperature is detected, Shutdown DUT");
                        onlp_sysi_shutdown();
                    }
                    /*ZR xcvr do HW protect*/
                    if (max_port_temp >= afo_thermal_spec.xcvr_red_alarm_to_shutdown_temp)
                    {
                        if(!xcvr_shutdown_flag)
                        {
                            AIM_SYSLOG_CRIT("XCVR temperature is over the shutdown threshold",
                                            "XCVR temperature is over the shutdown threshold",
                                            "Shutdown threshold for xcvr temperature is detected");
                            xcvr_shutdown_flag = 1;
                        }
                    }
                }
            }
        }

        if(max_to_mid==CHASSIS_THERMAL_COUNT && ori_state==LEVEL_FAN_MAX)
        {
            if (fan_fail==0) /*For fan fail or remove_test, don't set current_state to MID, must keep MAX*/
            {
                current_state = LEVEL_FAN_MID;
                AIM_SYSLOG_INFO("temperature is less than the error threshold",
                                "temperature is less than the error threshold",
                                "Monitor all sensors, temperature is less than the error threshold of thermal policy.");
            }

            if (fan_alarm_state)
            {
                fan_alarm_state=0;
                send_red_alarm=0;
                xcvr_shutdown_flag = 0;
                AIM_SYSLOG_INFO("Temperature is over the critical threshold is clean",
                                "Temperature is over the critical threshold is clear",
                                "Alarm for temperature is over the critical threshold is cleared");
            }
        }
        if(mid_to_min==CHASSIS_THERMAL_COUNT && ori_state==LEVEL_FAN_MID)
        {
            if (!psu_full_load && fan_fail==0)
            {
                current_state=LEVEL_FAN_MIN;
                AIM_SYSLOG_INFO("temperature is less than the warning threshold",
                                "temperature is less than the warning threshold",
                                "Monitor all sensors, temperature is less than the warning of thermal policy.");
            }
        }
    }

    /* Get each fan status
     */
    for (i = 1; i <= NUM_OF_FAN_ON_MAIN_BROAD; i++)
    {
        onlp_fan_info_t fan_info;

        if (onlp_fani_info_get(ONLP_FAN_ID_CREATE(i), &fan_info) != ONLP_STATUS_OK)
        {
            AIM_LOG_ERROR("Unable to get fan(%d) status, try to set the other fans as full speed\r\n", i);
            if(current_duty_cycle != FAN_DUTY_CYCLE_MAX)
            {
                onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1), FAN_DUTY_CYCLE_MAX);
            }
           /*
            * 1.When insert/remove fan, fan speed/log still according to thermal policy.
            * 2.If thermal policy state is bigger than LEVEL_FAN_MAX:
            *   Do not change back to LEVEL_FAN_MAX, beacuse still need to deal with LOG or shutdown case.
            * 3.If thermal policy state is smaller than LEVEL_FAN_MAX, set state=MAX.
            *   When remove and insert back fan test, policy check temp and set to correct fan_speed.
            */
            if (fan_state <LEVEL_FAN_MAX)
            {
                AIM_LOG_ERROR("Unable to get fan(%d), set fan_state=LEVEL_FAN_MAX\n", i);
                fan_state=LEVEL_FAN_MAX;
            }
            fan_fail=1;
            break;
        }
        if (fan_info.status & ONLP_FAN_STATUS_FAILED || !(fan_info.status & ONLP_FAN_STATUS_PRESENT))
        {
            AIM_SYSLOG_WARN("Fan fail", "Fan fail", "Fan(%d) is not working, set the other fans as full speed\r\n", i);
            if(current_duty_cycle != FAN_DUTY_CYCLE_MAX)
            {
                onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1), FAN_DUTY_CYCLE_MAX);
            }
            fan_fail=1;
            if (fan_state <LEVEL_FAN_MAX)
            {
                AIM_LOG_ERROR("Fan(%d) fail, set fan_state=LEVEL_FAN_MAX\n", i);
                fan_state=LEVEL_FAN_MAX;
            }
            break;
        }
        fan_fail=0;
    }
    if(current_state!=ori_state && !fan_fail)
    {
        new_duty_cycle=onlp_sysi_get_duty_cycle_by_fan_state(current_state, direction_val);

        if(new_duty_cycle!=current_duty_cycle)
        {
            if (new_duty_cycle > current_duty_cycle) {
                AIM_SYSLOG_WARN("Increase info", "Increase info", "Increase fan duty_cycle from %d%% to %d%%", current_duty_cycle, new_duty_cycle);
            }
            else{
                AIM_SYSLOG_INFO("Decrease info", "Decrease info", "Decrease fan duty_cycle from %d%% to %d%%", current_duty_cycle, new_duty_cycle);
            }

            onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1), new_duty_cycle);
            fan_state=current_state;

            return 0;
        }
        if(!new_duty_cycle)
        {
            onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1), FAN_DUTY_CYCLE_MAX);
        }
    }

    return 0;
}

int
onlp_sysi_platform_manage_leds(void)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}

