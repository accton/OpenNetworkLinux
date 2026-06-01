#!/usr/bin/env python
# USB LED monitor for AS7327-56X (BMC enabled mode)
import sys
import os
import time
import syslog

USB_LED_PATH = '/sys/class/leds/accton_as7327_56x_led::usb/brightness'
USB_DATA_FLOW_PATH = '/sys/bus/usb/devices/1-1.1/urbnum'

LED_DARK = 0
LED_GREEN_ON = 1
LED_GREEN_BLINK = 5

URB_THRESHOLD = 4

old_usb_data_num = 0
current_led_state = None

def get_usb_data_num():
    try:
        with open(USB_DATA_FLOW_PATH, 'r') as f:
            return int(f.read().strip())
    except (IOError, ValueError):
        return -1

def set_usb_led(led):
    global current_led_state
    if led == current_led_state:
        return
    try:
        with open(USB_LED_PATH, 'w') as f:
            f.write(str(led))
        current_led_state = led
    except IOError:
        pass

def usb_led_ctrl():
    global old_usb_data_num

    if not os.path.exists(USB_DATA_FLOW_PATH):
        set_usb_led(LED_DARK)
        old_usb_data_num = 0
        return

    cur_usb_data_num = get_usb_data_num()
    if cur_usb_data_num < 0:
        set_usb_led(LED_DARK)
        old_usb_data_num = 0
        return

    if old_usb_data_num == 0:
        old_usb_data_num = cur_usb_data_num
        set_usb_led(LED_GREEN_ON)
        return

    diff = cur_usb_data_num - old_usb_data_num
    old_usb_data_num = cur_usb_data_num

    if diff > URB_THRESHOLD:
        set_usb_led(LED_GREEN_BLINK)
    else:
        set_usb_led(LED_GREEN_ON)

if __name__ == '__main__':
    syslog.syslog(syslog.LOG_INFO, 'USB LED monitor starting')
    time.sleep(60)
    while True:
        usb_led_ctrl()
        time.sleep(1)
