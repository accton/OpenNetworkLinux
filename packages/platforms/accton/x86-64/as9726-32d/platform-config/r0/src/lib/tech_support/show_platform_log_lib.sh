#!/bin/bash

# CPU eeprom
cpu_eeprom_bus_id="13"
cpu_eeprom_i2c_addr="56"

cpu_eeprom_sysfs="/sys/bus/i2c/devices/${cpu_eeprom_bus_id}-00${cpu_eeprom_i2c_addr}/eeprom"

# BIOS flash
support_bios_flash=1

cpu_cpld_bus_id="1"
cpu_cpld_i2c_addr="65"
bios_boot_flash_sysfs="/sys/bus/i2c/devices/${cpu_cpld_bus_id}-00${cpu_cpld_i2c_addr}/bios_flash_id"

# PSU sysfs
psu1_present_sysfs="/sys/bus/i2c/devices/9-0050/psu_present"
psu2_present_sysfs="/sys/bus/i2c/devices/9-0051/psu_present"
psu1_power_good_sysfs="/sys/bus/i2c/devices/9-0050/psu_power_good"
psu2_power_good_sysfs="/sys/bus/i2c/devices/9-0051/psu_power_good"

# QSFP/SFP
support_sfp=1
support_qsfpdd=1
sfp_eeprom_bus_array=(49 50)
qsfp_eeprom_bus_array=(17 18 19 20 21 22 23 24 25 26 \
                       27 28 29 30 31 32 33 34 35 36 \
                       37 38 39 40 41 42 43 44 45 46 \
                       47 48)

port_status_cpld_i2c_bus_addr_array=("10-0061" "10-0062")
sfp_port_array=(33 34)
qsfp_port_array=(1  2  3  4  5  6  7  8  9  10 \
                 11 12 13 14 15 16 17 18 19 20 \
                 21 22 23 24 25 26 27 28 29 30 \
                 31 32)

# CPU temp
cpu_temp_hwmon=$(eval "ls /sys/devices/platform/coretemp.0/hwmon | grep hwmon")
cpu_temp_bus_id_array=("1" "2" "3" "4" "5")

# System led
sys_led_path_prefix="accton_as9726_32d_led"
sys_led_array=("diag" "loc" "fan" "psu1" "psu2")

sys_led_array=("diag" "loc" "fan" "psu1" "psu2")
sys_led_sysfs=("/sys/class/leds/accton_as9726_32d_led::diag/brightness" \
               "/sys/class/leds/accton_as9726_32d_led::loc/brightness" \
               "/sys/class/leds/accton_as9726_32d_led::fan/brightness" \
               "/sys/class/leds/accton_as9726_32d_led::psu1/brightness" \
               "/sys/class/leds/accton_as9726_32d_led::psu2/brightness")

sys_beacon_led_sysfs="/sys/class/leds/accton_as9726_32d_led::loc/brightness"

# USB
usb_auth_file_array=("/sys/bus/usb/devices/usb1/authorized" \
                     "/sys/bus/usb/devices/usb1/authorized_default" \
                     "/sys/bus/usb/devices/1-0:1.0/authorized" \
                     "/sys/bus/usb/devices/1-1/authorized" \
                     "/sys/bus/usb/devices/1-1:1.0/authorized" \
                     "/sys/bus/usb/devices/usb2/authorized" \
                     "/sys/bus/usb/devices/usb2/authorized_default" \
                     "/sys/bus/usb/devices/2-1/authorized" \
                     "/sys/bus/usb/devices/2-0:1.0/authorized" \
                     "/sys/bus/usb/devices/2-1:1.0/authorized" \
                     "/sys/bus/usb/devices/usb3/authorized" \
                     "/sys/bus/usb/devices/usb3/authorized_default" \
                     "/sys/bus/usb/devices/3-0:1.0/authorized")

# Function SFP
cpld_bus_idx=1
port_status_cpld_i2c_bus_addr=${port_status_cpld_i2c_bus_addr_array[$cpld_bus_idx]}

function _sfp_get_rx_los_func {
    idx=$1
    echo "/sys/bus/i2c/devices/${port_status_cpld_i2c_bus_addr}/module_rx_los_${sfp_port_array[$idx]}"
}

function _sfp_get_present_func {
    idx=$1
    echo "/sys/bus/i2c/devices/${port_status_cpld_i2c_bus_addr}/module_present_${sfp_port_array[$idx]}"
}

function _sfp_get_tx_fault_func {
    idx=$1
    echo "/sys/bus/i2c/devices/${port_status_cpld_i2c_bus_addr}/module_tx_fault_${sfp_port_array[$idx]}"
}

function _sfp_get_tx_disable_func {
    idx=$1
    echo "/sys/bus/i2c/devices/${port_status_cpld_i2c_bus_addr}/module_tx_disable_${sfp_port_array[$idx]}"
}

function _sfp_get_eeprom_func {
    idx=$1
    echo "/sys/bus/i2c/devices/${sfp_eeprom_bus_array[$idx]}-0050/eeprom"
}

# Function QSDP-DD
function _qsfpdd_select_cpld_i2c_bus_addr_idx {
    port_idx=$(($1 + 1))
    echo $(( port_idx <= 16 ? 0 : 1 ))
}

function _qsfpdd_get_present_func {
    idx=$1
    cpld_bus_idx=$(_qsfpdd_select_cpld_i2c_bus_addr_idx "$idx")
    port_status_cpld_i2c_bus_addr=${port_status_cpld_i2c_bus_addr_array[$cpld_bus_idx]}

    echo "/sys/bus/i2c/devices/${port_status_cpld_i2c_bus_addr}/module_present_${qsfp_port_array[$idx]}"
}

function _qsfpdd_get_lp_mode_func {
    idx=$1
    cpld_bus_idx=$(_qsfpdd_select_cpld_i2c_bus_addr_idx "$idx")
    port_status_cpld_i2c_bus_addr=${port_status_cpld_i2c_bus_addr_array[$cpld_bus_idx]}

    echo "/sys/bus/i2c/devices/${port_status_cpld_i2c_bus_addr}/module_lpmode_${qsfp_port_array[$idx]}"
}

function _qsfpdd_get_reset_func {
    idx=$1
    cpld_bus_idx=$(_qsfpdd_select_cpld_i2c_bus_addr_idx "$idx")
    port_status_cpld_i2c_bus_addr=${port_status_cpld_i2c_bus_addr_array[$cpld_bus_idx]}

    echo "/sys/bus/i2c/devices/${port_status_cpld_i2c_bus_addr}/module_reset_${qsfp_port_array[$idx]}"
}

function _qsfpdd_get_eeprom_func {
    idx=$1
    echo "/sys/bus/i2c/devices/${qsfp_eeprom_bus_array[$idx]}-0050/eeprom"
}
