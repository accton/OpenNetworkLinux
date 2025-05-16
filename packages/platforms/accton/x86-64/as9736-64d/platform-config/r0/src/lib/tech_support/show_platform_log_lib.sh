#!/bin/bash

# CPU eeprom
cpu_eeprom_bus_id="20"      
cpu_eeprom_i2c_addr="51"

cpu_eeprom_sysfs="/sys/bus/i2c/devices/${cpu_eeprom_bus_id}-00${cpu_eeprom_i2c_addr}/eeprom"

# BIOS flash
support_bios_flash=1
bios_boot_flash_sysfs="/sys/bus/i2c/devices/6-0060/bios_flash_id"

# PSU sysfs
psu1_present_sysfs="/sys/bus/i2c/devices/33-0050/psu_present"
psu2_present_sysfs="/sys/bus/i2c/devices/41-0051/psu_present"
psu1_power_good_sysfs="/sys/bus/i2c/devices/33-0050/psu_power_good"
psu2_power_good_sysfs="/sys/bus/i2c/devices/41-0051/psu_power_good"

# QSFP/SFP
support_sfp=1
support_qsfpdd=1
sfp_port_array=(65 66)
qsfp_port_array=(1  2  3  4  5  6  7  8  9  10 \
                 11 12 13 14 15 16 17 18 19 20 \
                 21 22 23 24 25 26 27 28 29 30 \
                 31 32 33 34 35 36 37 38 39 40 \
                 41 42 43 44 45 46 47 48 49 50 \
                 51 52 53 54 55 56 57 58 59 60 \
                 61 62 63 64)

# CPU temp
cpu_temp_hwmon=$(eval "ls /sys/devices/platform/coretemp.0/hwmon | grep hwmon")
cpu_temp_bus_id_array=("1" "2" "3" "4" "5")

# System led
sys_led_path_prefix="accton_as9736_64d_led"
sys_led_array=("diag" "loc" "fan" "psu1" "psu2")

sys_led_array=("diag" "loc" "fan" "psu1" "psu2")
sys_led_sysfs=("/sys/class/leds/accton_as9736_64d_led::diag/brightness" \
               "/sys/class/leds/accton_as9736_64d_led::loc/brightness" \
               "/sys/class/leds/accton_as9736_64d_led::fan/brightness" \
               "/sys/class/leds/accton_as9736_64d_led::psu1/brightness" \
               "/sys/class/leds/accton_as9736_64d_led::psu2/brightness")

sys_beacon_led_sysfs="/sys/class/leds/accton_as9736_64d_led::loc/brightness"

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
function _sfp_get_rx_los_func {
    idx=$1
    echo "/sys/devices/platform/as9736_64d_fpga/module_rx_los_${sfp_port_array[$idx]}"
}

function _sfp_get_present_func {
    idx=$1
    echo "/sys/devices/platform/as9736_64d_fpga/module_present_${sfp_port_array[$idx]}"
}

function _sfp_get_tx_fault_func {
    idx=$1
    echo "/sys/devices/platform/as9736_64d_fpga/module_tx_fault_${sfp_port_array[$idx]}"
}

function _sfp_get_tx_disable_func {
    idx=$1
    echo "/sys/devices/platform/as9736_64d_fpga/module_tx_disable_${sfp_port_array[$idx]}"
}

function _sfp_get_eeprom_func {
    port_idx=$(( ${sfp_port_array[$1]} - 33 ))
    echo "/sys/devices/platform/pcie_ldb_fpga_device.${port_idx}/eeprom"
}

# Function QSDP-DD
function _qsfpdd_get_present_func {
    idx=$1
    echo "/sys/devices/platform/as9736_64d_fpga/module_present_${qsfp_port_array[$idx]}"
}

function _qsfpdd_get_lp_mode_func {
    idx=$1
    echo "/sys/devices/platform/as9736_64d_fpga/module_lp_mode_${qsfp_port_array[$idx]}"
}

function _qsfpdd_get_reset_func {
    idx=$1
    echo "/sys/devices/platform/as9736_64d_fpga/module_reset_${qsfp_port_array[$idx]}"
}

function _qsfpdd_get_eeprom_func {
    idx=$1
    if [ ${qsfp_port_array[$idx]} -le 32 ]; then
        port_idx=$((qsfp_port_array[i]-1))
        sysfs="/sys/devices/platform/pcie_udb_fpga_device.${port_idx}/eeprom"
    else
        port_idx=$((qsfp_port_array[$idx]-33))
        sysfs="/sys/devices/platform/pcie_ldb_fpga_device.${port_idx}/eeprom"
    fi

    echo "${sysfs}"
}
