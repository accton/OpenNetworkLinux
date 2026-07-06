/*
 * A LED driver for the accton as1817_64o via IPMI
 *
 * Copyright (C) 2026 Accton Technology Corporation.
 * rayx_huang <rayx_huang@edge-core.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/stat.h>
#include <linux/sysfs.h>
#include <linux/hwmon-sysfs.h>
#include <linux/ipmi.h>
#include <linux/ipmi_smi.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>

#define DRVNAME "as1817_64o_led"
#define ACCTON_IPMI_NETFN 0x34
#define IPMI_LED_READ_CMD 0x1A
#define IPMI_LED_WRITE_CMD 0x1B

#define IPMI_TIMEOUT (5 * HZ)
#define IPMI_ERR_RETRY_TIMES 1

#define NUM_OF_LED 5

/* IPMI response color codes from BMC (0x1A get) */
enum ipmi_led_color {
    IPMI_LED_OFF            = 0x00,
    IPMI_LED_RED            = 0x02,
    IPMI_LED_GREEN          = 0x04,
    IPMI_LED_GREEN_BLINK    = 0x05,
    IPMI_LED_YELLOW         = 0x06,
    IPMI_LED_BLUE           = 0x08,
    IPMI_LED_BLUE_BLINK     = 0x09,
};

/* Userspace LED mode values (sysfs) */
enum led_light_mode {
    LED_MODE_OFF            = 0,
    LED_MODE_RED            = 10,
    LED_MODE_RED_BLINKING   = 11,
    LED_MODE_YELLOW         = 14,
    LED_MODE_GREEN          = 16,
    LED_MODE_GREEN_BLINKING = 17,
    LED_MODE_BLUE           = 18,
    LED_MODE_BLUE_BLINKING  = 19,
    LED_MODE_UNKNOWN        = 99,
};

/* IPMI set (0x1B) color codes */
enum ipmi_led_set_color {
    IPMI_SET_LED_OFF          = 0x00,
    IPMI_SET_LED_RED          = 0x02,
    IPMI_SET_LED_GREEN        = 0x04,
    IPMI_SET_LED_GREEN_BLINK  = 0x05,
    IPMI_SET_LED_AMBER        = 0x06,
    IPMI_SET_LED_BLUE         = 0x08,
    IPMI_SET_LED_BLUE_BLINK   = 0x09,
};

enum led_sysfs_attrs {
    LED_LOC,
    LED_DIAG,
    LED_ALARM,
    LED_FAN,
    LED_PSU,
};

struct ipmi_data {
    struct completion read_complete;
    struct ipmi_addr address;
    struct ipmi_user *user;
    int interface;
    struct kernel_ipmi_msg tx_message;
    long tx_msgid;
    void *rx_msg_data;
    unsigned short rx_msg_len;
    unsigned char rx_result;
    int rx_recv_type;
    struct ipmi_user_hndl ipmi_hndlrs;
};

struct as1817_64o_led_data {
    struct platform_device *pdev;
    struct mutex update_lock;
    char valid;
    unsigned long last_updated;
    unsigned char ipmi_resp[NUM_OF_LED];
    struct ipmi_data ipmi;
};

static struct as1817_64o_led_data *data = NULL;

static ssize_t show_led(struct device *dev, struct device_attribute *da, char *buf);
static ssize_t set_led(struct device *dev, struct device_attribute *da,
                       const char *buf, size_t count);
static int as1817_64o_led_probe(struct platform_device *pdev);
static void as1817_64o_led_remove(struct platform_device *pdev);

static struct platform_driver as1817_64o_led_driver = {
    .probe = as1817_64o_led_probe,
    .remove = as1817_64o_led_remove,
    .driver = {
        .name = DRVNAME,
        .owner = THIS_MODULE,
    },
};

static SENSOR_DEVICE_ATTR(led_loc, S_IWUSR | S_IRUGO, show_led, set_led, LED_LOC);
static SENSOR_DEVICE_ATTR(led_diag, S_IRUGO, show_led, NULL, LED_DIAG);
static SENSOR_DEVICE_ATTR(led_alarm, S_IRUGO, show_led, NULL, LED_ALARM);
static SENSOR_DEVICE_ATTR(led_fan, S_IRUGO, show_led, NULL, LED_FAN);
static SENSOR_DEVICE_ATTR(led_psu, S_IRUGO, show_led, NULL, LED_PSU);

static struct attribute *as1817_64o_led_attrs[] = {
    &sensor_dev_attr_led_loc.dev_attr.attr,
    &sensor_dev_attr_led_diag.dev_attr.attr,
    &sensor_dev_attr_led_alarm.dev_attr.attr,
    &sensor_dev_attr_led_fan.dev_attr.attr,
    &sensor_dev_attr_led_psu.dev_attr.attr,
    NULL
};

static const struct attribute_group as1817_64o_led_group = {
    .attrs = as1817_64o_led_attrs,
};

/* IPMI functions */
static void ipmi_msg_handler(struct ipmi_recv_msg *msg, void *user_msg_data)
{
    unsigned short rx_len;
    struct ipmi_data *ipmi = user_msg_data;

    if (msg->msgid != ipmi->tx_msgid) {
        ipmi_free_recv_msg(msg);
        return;
    }

    ipmi->rx_recv_type = msg->recv_type;
    if (msg->msg.data_len > 0)
        ipmi->rx_result = msg->msg.data[0];
    else
        ipmi->rx_result = IPMI_UNKNOWN_ERR_COMPLETION_CODE;

    if (msg->msg.data_len > 1) {
        rx_len = msg->msg.data_len - 1;
        if (ipmi->rx_msg_len < rx_len)
            rx_len = ipmi->rx_msg_len;
        ipmi->rx_msg_len = rx_len;
        memcpy(ipmi->rx_msg_data, msg->msg.data + 1, ipmi->rx_msg_len);
    } else {
        ipmi->rx_msg_len = 0;
    }

    ipmi_free_recv_msg(msg);
    complete(&ipmi->read_complete);
}

static int init_ipmi_data(struct ipmi_data *ipmi, int iface)
{
    int err;

    init_completion(&ipmi->read_complete);
    ipmi->address.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
    ipmi->address.channel = IPMI_BMC_CHANNEL;
    ipmi->address.data[0] = 0;
    ipmi->interface = iface;
    ipmi->tx_msgid = 0;
    ipmi->tx_message.netfn = ACCTON_IPMI_NETFN;
    ipmi->ipmi_hndlrs.ipmi_recv_hndl = ipmi_msg_handler;

    err = ipmi_create_user(ipmi->interface, &ipmi->ipmi_hndlrs,
                           ipmi, &ipmi->user);
    if (err < 0) {
        pr_err("Unable to register user with IPMI interface %d\n", iface);
        return -EACCES;
    }
    return 0;
}

static int ipmi_send_message(struct ipmi_data *ipmi, unsigned char cmd,
                             unsigned char *tx_data, unsigned short tx_len,
                             unsigned char *rx_data, unsigned short rx_len)
{
    int err, retry;

    for (retry = 0; retry <= IPMI_ERR_RETRY_TIMES; retry++) {
        ipmi->tx_message.cmd = cmd;
        ipmi->tx_message.data = tx_data;
        ipmi->tx_message.data_len = tx_len;
        ipmi->rx_msg_data = rx_data;
        ipmi->rx_msg_len = rx_len;

        err = ipmi_validate_addr(&ipmi->address, sizeof(ipmi->address));
        if (err)
            return err;

        ipmi->tx_msgid++;
        err = ipmi_request_settime(ipmi->user, &ipmi->address,
                                   ipmi->tx_msgid, &ipmi->tx_message,
                                   ipmi, 0, 0, 0);
        if (err)
            continue;

        err = wait_for_completion_timeout(&ipmi->read_complete,
                                          IPMI_TIMEOUT);
        if (!err) {
            err = -ETIMEDOUT;
            continue;
        }

        if (ipmi->rx_result != 0)
            continue;

        return 0;
    }
    return err ? err : -EIO;
}

static void as1817_64o_led_update(void)
{
    int status;

    if (time_before(jiffies, data->last_updated + HZ * 5) && data->valid)
        return;

    data->valid = 0;
    status = ipmi_send_message(&data->ipmi, IPMI_LED_READ_CMD,
                               NULL, 0,
                               data->ipmi_resp, NUM_OF_LED);
    if (unlikely(status != 0))
        return;

    data->last_updated = jiffies;
    data->valid = 1;
}

static int ipmi_color_to_led_mode(unsigned char ipmi_color)
{
    switch (ipmi_color) {
    case IPMI_LED_OFF:           return LED_MODE_OFF;
    case IPMI_LED_RED:           return LED_MODE_RED;
    case IPMI_LED_GREEN:         return LED_MODE_GREEN;
    case IPMI_LED_BLUE_BLINK:    return LED_MODE_BLUE_BLINKING;
    default:                     return LED_MODE_UNKNOWN;
    }
}

static int led_mode_to_ipmi_color(int mode)
{
    switch (mode) {
    case LED_MODE_OFF:            return IPMI_SET_LED_OFF;
    case LED_MODE_RED:            return IPMI_SET_LED_RED;
    case LED_MODE_GREEN:          return IPMI_SET_LED_GREEN;
    case LED_MODE_BLUE_BLINKING:  return IPMI_SET_LED_BLUE_BLINK;
    default:                      return -EINVAL;
    }
}

static ssize_t show_led(struct device *dev, struct device_attribute *da, char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    int value;

    mutex_lock(&data->update_lock);
    as1817_64o_led_update();

    if (!data->valid) {
        mutex_unlock(&data->update_lock);
        return -EIO;
    }

    value = ipmi_color_to_led_mode(data->ipmi_resp[attr->index]);
    mutex_unlock(&data->update_lock);

    return sprintf(buf, "%d\n", value);
}

static ssize_t set_led(struct device *dev, struct device_attribute *da,
                       const char *buf, size_t count)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    long mode;
    int status, ipmi_color;
    unsigned char tx_data[2];

    status = kstrtol(buf, 10, &mode);
    if (status)
        return status;

    ipmi_color = led_mode_to_ipmi_color(mode);
    if (ipmi_color < 0)
        return -EINVAL;

    /* LED type is 1-based in IPMI: 1=LOC, 2=DIAG, 3=ALARM, 4=FAN, 5=PSU1, 6=PSU2 */
    tx_data[0] = attr->index + 1;
    tx_data[1] = (unsigned char)ipmi_color;

    mutex_lock(&data->update_lock);
    status = ipmi_send_message(&data->ipmi, IPMI_LED_WRITE_CMD,
                               tx_data, 2, NULL, 0);
    if (status) {
        mutex_unlock(&data->update_lock);
        return -EIO;
    }

    data->valid = 0; /* force re-read on next show */
    mutex_unlock(&data->update_lock);

    return count;
}

static int as1817_64o_led_probe(struct platform_device *pdev)
{
    return sysfs_create_group(&pdev->dev.kobj, &as1817_64o_led_group);
}

static void as1817_64o_led_remove(struct platform_device *pdev)
{
    sysfs_remove_group(&pdev->dev.kobj, &as1817_64o_led_group);
}

static int __init as1817_64o_led_init(void)
{
    int ret;

    data = kzalloc(sizeof(struct as1817_64o_led_data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    mutex_init(&data->update_lock);

    ret = platform_driver_register(&as1817_64o_led_driver);
    if (ret)
        goto err_free;

    data->pdev = platform_device_register_simple(DRVNAME, -1, NULL, 0);
    if (IS_ERR(data->pdev)) {
        ret = PTR_ERR(data->pdev);
        goto err_driver;
    }

    ret = init_ipmi_data(&data->ipmi, 0);
    if (ret)
        goto err_device;

    return 0;

err_device:
    platform_device_unregister(data->pdev);
err_driver:
    platform_driver_unregister(&as1817_64o_led_driver);
err_free:
    kfree(data);
    return ret;
}

static void __exit as1817_64o_led_exit(void)
{
    platform_device_unregister(data->pdev);
    platform_driver_unregister(&as1817_64o_led_driver);
    ipmi_destroy_user(data->ipmi.user);
    kfree(data);
}

module_init(as1817_64o_led_init);
module_exit(as1817_64o_led_exit);

MODULE_AUTHOR("rayx_huang <rayx_huang@edge-core.com>");
MODULE_DESCRIPTION("as1817_64o_led driver");
MODULE_LICENSE("GPL");
