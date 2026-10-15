/*
 * Copyright (C)  Roger Ho <roger530_ho@edge-core.com>
 *
 * Based on:
 *    pca954x.c from Kumar Gala <galak@kernel.crashing.org>
 * Copyright (C) 2006
 *
 * Based on:
 *    pca954x.c from Ken Harrenstien
 * Copyright (C) 2004 Google, Inc. (Ken Harrenstien)
 *
 * Based on:
 *    i2c-virtual_cb.c from Brian Kuschak <bkuschak@yahoo.com>
 * and
 *    pca9540.c from Jean Delvare <khali@linux-fr.org>.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/version.h>
#include <linux/stat.h>
#include <linux/sysfs.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/ipmi.h>
#include <linux/ipmi_smi.h>
#include <linux/platform_device.h>
#include <linux/string_helpers.h>
#include "accton_ipmi_intf.h"

#define DRVNAME "as9957_32db_thermal"

#define IPMI_THERMAL_READ_CMD 0x12
#define THERMAL_COUNT    24
#define THERMAL_DATA_LEN 3
#define THERMAL_DATA_COUNT (THERMAL_COUNT * THERMAL_DATA_LEN)

static ssize_t show_temp(struct device *dev, struct device_attribute *attr,
    char *buf);
static int as9957_32db_thermal_probe(struct platform_device *pdev);
static int as9957_32db_thermal_remove(struct platform_device *pdev);

enum temp_data_index {
    TEMP_ADDR,
    TEMP_FAULT,
    TEMP_INPUT,
    TEMP_DATA_COUNT
};

struct as9957_32db_thermal_data {
    struct platform_device *pdev;
    struct device   *hwmon_dev;
    struct mutex update_lock;
    char valid;           /* != 0 if registers are valid */
    unsigned long last_updated;    /* In jiffies */
    char   ipmi_resp[THERMAL_DATA_COUNT]; /* 3 bytes for each thermal */
    struct ipmi_data ipmi;
    unsigned char ipmi_tx_data[2];  /* 0: thermal id, 1: temp */
};

struct as9957_32db_thermal_data *data = NULL;

static struct platform_driver as9957_32db_thermal_driver = {
    .probe = as9957_32db_thermal_probe,
    .remove = as9957_32db_thermal_remove,
    .driver = {
        .name = DRVNAME,
        .owner = THIS_MODULE,
    },
};

enum as9957_32db_thermal_sysfs_attrs {
    TEMP1_INPUT, // LM75_0x48
    TEMP2_INPUT, // LM75_0x49
    TEMP3_INPUT, // LM75_0x4B
    TEMP4_INPUT, // RJ45_LM75_0x4A
    TEMP5_INPUT, // CARRYBRD_0x48
    TEMP6_INPUT, // CARRYBRD_0x49
    TEMP7_INPUT, // FANBRD_0x4D
    TEMP8_INPUT, // FANBRD_0x4E
    TEMP9_INPUT, // TMP_LOC_0x48
    TEMP10_INPUT, // TMP_D1_HBM_PHY1_0x48
    TEMP11_INPUT, // TMP_D1_HBM_PHY2_0x48
    TEMP12_INPUT, // TMP_D1_NIF1_0x48
    TEMP13_INPUT, // TMP_D1_CORE_0x48
    TEMP14_INPUT, // TMP_LOC_0x49
    TEMP15_INPUT, // TMP_D0_NIF0_0x49
    TEMP16_INPUT, // TMP_D0_PADS_0x49
    TEMP17_INPUT, // TMP_LOC_0x4A
    TEMP18_INPUT, // TMP_D1_NIF0_0x4A
    TEMP19_INPUT, // TMP_D1_PADS_0x4A
    TEMP20_INPUT, // TMP_LOC_0x4B
    TEMP21_INPUT, // TMP_D0_HBM_PHY1_0x4B
    TEMP22_INPUT, // TMP_D0_HBM_PHY2_0x4B
    TEMP23_INPUT, // TMP_D0_NIF1_0x4B
    TEMP24_INPUT, // TMP_D0_CORE_0x4B
    TEMP1_MAX_ALARM,
    TEMP2_MAX_ALARM,
    TEMP3_MAX_ALARM,
    TEMP4_MAX_ALARM,
    TEMP5_MAX_ALARM,
    TEMP6_MAX_ALARM,
    TEMP7_MAX_ALARM,
    TEMP8_MAX_ALARM,
    TEMP9_MAX_ALARM,
    TEMP10_MAX_ALARM,
    TEMP11_MAX_ALARM,
    TEMP12_MAX_ALARM,
    TEMP13_MAX_ALARM,
    TEMP14_MAX_ALARM,
    TEMP15_MAX_ALARM,
    TEMP16_MAX_ALARM,
    TEMP17_MAX_ALARM,
    TEMP18_MAX_ALARM,
    TEMP19_MAX_ALARM,
    TEMP20_MAX_ALARM,
    TEMP21_MAX_ALARM,
    TEMP22_MAX_ALARM,
    TEMP23_MAX_ALARM,
    TEMP24_MAX_ALARM,
    TEMP1_MAX,
    TEMP2_MAX,
    TEMP3_MAX,
    TEMP4_MAX,
    TEMP5_MAX,
    TEMP6_MAX,
    TEMP7_MAX,
    TEMP8_MAX,
    TEMP9_MAX,
    TEMP10_MAX,
    TEMP11_MAX,
    TEMP12_MAX,
    TEMP13_MAX,
    TEMP14_MAX,
    TEMP15_MAX,
    TEMP16_MAX,
    TEMP17_MAX,
    TEMP18_MAX,
    TEMP19_MAX,
    TEMP20_MAX,
    TEMP21_MAX,
    TEMP22_MAX,
    TEMP23_MAX,
    TEMP24_MAX,
    TEMP1_MIN,
    TEMP2_MIN,
    TEMP3_MIN,
    TEMP4_MIN,
    TEMP5_MIN,
    TEMP6_MIN,
    TEMP7_MIN,
    TEMP8_MIN,
    TEMP9_MIN,
    TEMP10_MIN,
    TEMP11_MIN,
    TEMP12_MIN,
    TEMP13_MIN,
    TEMP14_MIN,
    TEMP15_MIN,
    TEMP16_MIN,
    TEMP17_MIN,
    TEMP18_MIN,
    TEMP19_MIN,
    TEMP20_MIN,
    TEMP21_MIN,
    TEMP22_MIN,
    TEMP23_MIN,
    TEMP24_MIN,
    TEMP1_MIN_ALARM,
    TEMP2_MIN_ALARM,
    TEMP3_MIN_ALARM,
    TEMP4_MIN_ALARM,
    TEMP5_MIN_ALARM,
    TEMP6_MIN_ALARM,
    TEMP7_MIN_ALARM,
    TEMP8_MIN_ALARM,
    TEMP9_MIN_ALARM,
    TEMP10_MIN_ALARM,
    TEMP11_MIN_ALARM,
    TEMP12_MIN_ALARM,
    TEMP13_MIN_ALARM,
    TEMP14_MIN_ALARM,
    TEMP15_MIN_ALARM,
    TEMP16_MIN_ALARM,
    TEMP17_MIN_ALARM,
    TEMP18_MIN_ALARM,
    TEMP19_MIN_ALARM,
    TEMP20_MIN_ALARM,
    TEMP21_MIN_ALARM,
    TEMP22_MIN_ALARM,
    TEMP23_MIN_ALARM,
    TEMP24_MIN_ALARM,
};

#define DECLARE_THERMAL_SENSOR_DEVICE_ATTR(index) \
    static SENSOR_DEVICE_ATTR(temp##index##_input, S_IRUGO, show_temp, \
                    NULL, TEMP##index##_INPUT); 

#define DECLARE_THERMAL_ATTR(index) \
    &sensor_dev_attr_temp##index##_input.dev_attr.attr

DECLARE_THERMAL_SENSOR_DEVICE_ATTR(1);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR(2);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR(3);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR(4);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR(5);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR(6);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR(7);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR(8);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR(9);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR(10);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR(11);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR(12);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR(13);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR(14);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR(15);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR(16);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR(17);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR(18);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR(19);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR(20);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR(21);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR(22);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR(23);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR(24);

static struct attribute *as9957_32db_thermal_attrs[] = {
    DECLARE_THERMAL_ATTR(1),
    DECLARE_THERMAL_ATTR(2),
    DECLARE_THERMAL_ATTR(3),
    DECLARE_THERMAL_ATTR(4),
    DECLARE_THERMAL_ATTR(5),
    DECLARE_THERMAL_ATTR(6),
    DECLARE_THERMAL_ATTR(7),
    DECLARE_THERMAL_ATTR(8),
    DECLARE_THERMAL_ATTR(9),
    DECLARE_THERMAL_ATTR(10),
    DECLARE_THERMAL_ATTR(11),
    DECLARE_THERMAL_ATTR(12),
    DECLARE_THERMAL_ATTR(13),
    DECLARE_THERMAL_ATTR(14),
    DECLARE_THERMAL_ATTR(15),
    DECLARE_THERMAL_ATTR(16),
    DECLARE_THERMAL_ATTR(17),
    DECLARE_THERMAL_ATTR(18),
    DECLARE_THERMAL_ATTR(19),
    DECLARE_THERMAL_ATTR(20),
    DECLARE_THERMAL_ATTR(21),
    DECLARE_THERMAL_ATTR(22),
    DECLARE_THERMAL_ATTR(23),
    DECLARE_THERMAL_ATTR(24),
    NULL
};
ATTRIBUTE_GROUPS(as9957_32db_thermal);

static ssize_t show_temp(struct device *dev, struct device_attribute *da,
                            char *buf)
{
    int status = 0;
    int index  = 0;
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

    mutex_lock(&data->update_lock);

    if (time_after(jiffies, data->last_updated + HZ * 5) || !data->valid) {
        data->valid = 0;

        status = ipmi_send_message(&data->ipmi, IPMI_THERMAL_READ_CMD, NULL, 0,
                                    data->ipmi_resp, sizeof(data->ipmi_resp));
        if (unlikely(status != 0))
            goto exit;

        if (unlikely(data->ipmi.rx_result != 0)) {
            status = -EIO;
            goto exit;
        }

        data->last_updated = jiffies;
        data->valid = 1;
    }

    /* Get temp fault status */
    index = attr->index * TEMP_DATA_COUNT + TEMP_FAULT;
    if (unlikely(data->ipmi_resp[index] == 0)) {
        status = -EIO;
        goto exit;
    }

    /* Get temperature in degree celsius */
    index = attr->index * TEMP_DATA_COUNT + TEMP_INPUT;
    status = ((s8)data->ipmi_resp[index]) * 1000;

    mutex_unlock(&data->update_lock);
    return sprintf(buf, "%d\n", status);

exit:
    mutex_unlock(&data->update_lock);
    return status;
}

static int as9957_32db_thermal_probe(struct platform_device *pdev)
{
    int status = 0;
    struct device *hwmon_dev;

    hwmon_dev = hwmon_device_register_with_info(&pdev->dev, DRVNAME,
                                NULL, NULL, 
                                as9957_32db_thermal_groups);

    if (IS_ERR(data->hwmon_dev)) {
        status = PTR_ERR(data->hwmon_dev);
        return status;
    }

    mutex_lock(&data->update_lock);
    data->hwmon_dev = hwmon_dev;
    mutex_unlock(&data->update_lock);

    dev_info(&pdev->dev, "Device Created\n");

    return status;
}

static int as9957_32db_thermal_remove(struct platform_device *pdev)
{
    mutex_lock(&data->update_lock);
    if (data->hwmon_dev) {
        hwmon_device_unregister(data->hwmon_dev);
        data->hwmon_dev = NULL;
    }
    mutex_unlock(&data->update_lock);

    return 0;
}

static int __init as9957_32db_thermal_init(void)
{
    int ret;

    data = kzalloc(sizeof(struct as9957_32db_thermal_data), GFP_KERNEL);
    if (!data) {
        ret = -ENOMEM;
        goto alloc_err;
    }

    mutex_init(&data->update_lock);

    ret = platform_driver_register(&as9957_32db_thermal_driver);
    if (ret < 0)
        goto dri_reg_err;

    data->pdev = platform_device_register_simple(DRVNAME, -1, NULL, 0);
    if (IS_ERR(data->pdev)) {
        ret = PTR_ERR(data->pdev);
        goto dev_reg_err;
    }

    /* Set up IPMI interface */
    ret = init_ipmi_data(&data->ipmi, 0, &data->pdev->dev);
    if (ret) {
        goto ipmi_err;
    }

    return 0;

ipmi_err:
    platform_device_unregister(data->pdev);
dev_reg_err:
    platform_driver_unregister(&as9957_32db_thermal_driver);
dri_reg_err:
    kfree(data);
alloc_err:
    return ret;
}

static void __exit as9957_32db_thermal_exit(void)
{
    if (data) {
        ipmi_destroy_user(data->ipmi.user);
        platform_device_unregister(data->pdev);
        platform_driver_unregister(&as9957_32db_thermal_driver);
        kfree(data);
    }
}

MODULE_AUTHOR("Roger Ho <roger530_ho@edge-core.com>");
MODULE_DESCRIPTION("as9957_32db_thermal driver");
MODULE_LICENSE("GPL");

module_init(as9957_32db_thermal_init);
module_exit(as9957_32db_thermal_exit);
