/*
 * Copyright (C) Alex Lai <alex_lai@edge-core.com>
 *
 * This module supports the accton cpld that hold the channel select
 * mechanism for other i2c slave devices, such as SFP.
 * This includes the:
 *	 Accton as9736_64d CPLD1/CPLD2/CPLD3
 *
 * Based on:
 *	pca954x.c from Kumar Gala <galak@kernel.crashing.org>
 * Copyright (C) 2006
 *
 * Based on:
 *	pca954x.c from Ken Harrenstien
 * Copyright (C) 2004 Google, Inc. (Ken Harrenstien)
 *
 * Based on:
 * i2c-virtual_cb.c from Brian Kuschak <bkuschak@yahoo.com>
 * and
 * pca9540.c from Jean Delvare <khali@linux-fr.org>.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/version.h>
#include <linux/stat.h>
#include <linux/hwmon-sysfs.h>
#include <linux/delay.h>

#define I2C_RW_RETRY_COUNT			10
#define I2C_RW_RETRY_INTERVAL			60 /* ms */

static LIST_HEAD(cpld_client_list);
static struct mutex     list_lock;

struct cpld_client_node {
	struct i2c_client *client;
	struct list_head   list;
};

enum cpld_type {
    as9736_64d_fpga,
    as9736_64d_sys_cpld,
    as9736_64d_pdb_cpld,
    as9736_64d_scm_cpld
};

struct as9736_64d_cpld_data {
	enum cpld_type   type;
	struct device   *hwmon_dev;
	struct mutex     update_lock;
};

static const struct i2c_device_id as9736_64d_cpld_id[] = {
    { "as9736_64d_fpga", as9736_64d_fpga },
    { "as9736_64d_sys_cpld", as9736_64d_sys_cpld },
    { "as9736_64d_pdb_cpld", as9736_64d_pdb_cpld },
    { "as9736_64d_scm_cpld", as9736_64d_scm_cpld },
	{ }
};
MODULE_DEVICE_TABLE(i2c, as9736_64d_cpld_id);

enum as9736_64d_cpld_sysfs_attributes {
	CPLD_VERSION,
	BIOS_FLASH_ID,
	ACCESS,
};

/* sysfs attributes for hwmon 
 */
static ssize_t access(struct device *dev, struct device_attribute *da,
			const char *buf, size_t count);
static ssize_t show_version(struct device *dev, struct device_attribute *da,
			char *buf);
static ssize_t show_bios_flash_id(struct device *dev, struct device_attribute *da,
            char *buf);
static int as9736_64d_cpld_read_internal(struct i2c_client *client, u8 reg);
static int as9736_64d_cpld_write_internal(struct i2c_client *client, u8 reg,
					u8 value);

static SENSOR_DEVICE_ATTR(version, S_IRUGO, show_version, NULL, CPLD_VERSION);
static SENSOR_DEVICE_ATTR(bios_flash_id, S_IRUGO, show_bios_flash_id, NULL, BIOS_FLASH_ID);
static SENSOR_DEVICE_ATTR(access, S_IWUSR, NULL, access, ACCESS);

static struct attribute *as9736_64d_fpga_attributes[] = {
	&sensor_dev_attr_version.dev_attr.attr,
	&sensor_dev_attr_access.dev_attr.attr,
	NULL
};

static const struct attribute_group as9736_64d_fpga_group = {
	.attrs = as9736_64d_fpga_attributes,
};

static struct attribute *as9736_64d_sys_cpld_attributes[] = {
    &sensor_dev_attr_version.dev_attr.attr,
	&sensor_dev_attr_bios_flash_id.dev_attr.attr,
    NULL
};

static struct attribute *as9736_64d_pdb_cpld_attributes[] = {
    &sensor_dev_attr_version.dev_attr.attr,
    NULL
};

static struct attribute *as9736_64d_scm_cpld_attributes[] = {
    &sensor_dev_attr_version.dev_attr.attr,
    NULL
};

static const struct attribute_group as9736_64d_sys_cpld_group = {
    .attrs = as9736_64d_sys_cpld_attributes,
};

static const struct attribute_group as9736_64d_pdb_cpld_group = {
    .attrs = as9736_64d_pdb_cpld_attributes,
};

static const struct attribute_group as9736_64d_scm_cpld_group = {
    .attrs = as9736_64d_scm_cpld_attributes,
};

static ssize_t access(struct device *dev, struct device_attribute *da,
			const char *buf, size_t count)
{
	int status;
	u32 addr, val;
	struct i2c_client *client = to_i2c_client(dev);
	struct as9736_64d_cpld_data *data = i2c_get_clientdata(client);

	if (sscanf(buf, "0x%x 0x%x", &addr, &val) != 2)
		return -EINVAL;

	if (addr > 0xFF || val > 0xFF)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	status = as9736_64d_cpld_write_internal(client, addr, val);
	if (unlikely(status < 0))
		goto exit;

	mutex_unlock(&data->update_lock);
	return count;

exit:
	mutex_unlock(&data->update_lock);
	return status;
}

static void as9736_64d_cpld_add_client(struct i2c_client *client)
{
	struct cpld_client_node *node = kzalloc(sizeof(struct cpld_client_node)
					, GFP_KERNEL);

	if (!node) {
		dev_dbg(&client->dev, "Can't allocate cpld_client_node (0x%x)\n",
			client->addr);
		return;
	}

	node->client = client;

	mutex_lock(&list_lock);
	list_add(&node->list, &cpld_client_list);
	mutex_unlock(&list_lock);
}

static void as9736_64d_cpld_remove_client(struct i2c_client *client)
{
	struct list_head    *list_node = NULL;
	struct cpld_client_node *cpld_node = NULL;
	int found = 0;

	mutex_lock(&list_lock);

	list_for_each(list_node, &cpld_client_list)
	{
		cpld_node = list_entry(list_node, struct cpld_client_node,
					list);

		if (cpld_node->client == client) {
			found = 1;
			break;
		}
	}

	if (found) {
		list_del(list_node);
		kfree(cpld_node);
	}

	mutex_unlock(&list_lock);
}

static ssize_t show_version(struct device *dev, struct device_attribute *attr,
				char *buf)
{
    int val_major = 0, val_minor = 0;
	struct i2c_client *client = to_i2c_client(dev);
	
    val_major = i2c_smbus_read_byte_data(client, 0x1);
    val_minor = i2c_smbus_read_byte_data(client, 0x0);

    if (val_major < 0) {
        dev_dbg(&client->dev, "cpld(0x%x) reg(0x1) err %d\n", client->addr, val_major);
    }
    if (val_minor < 0) {
        dev_dbg(&client->dev, "cpld(0x%x) reg(0x0) err %d\n", client->addr, val_minor);
    }

    return sprintf(buf, "%x.%x\n", val_major, val_minor);
}

static ssize_t show_bios_flash_id(struct device *dev, struct device_attribute *attr, char *buf)
{
    int val = 0;
    struct i2c_client *client = to_i2c_client(dev);

    val = i2c_smbus_read_byte_data(client, 0x6);

    return sprintf(buf, "%d\n", ( (val & 0x1) == 0 ) ? 1 : 2); /*1: master, 2: slave*/
}

/*
 * I2C init/probing/exit functions
 */
static int as9736_64d_cpld_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct i2c_adapter *adap = to_i2c_adapter(client->dev.parent);
	struct as9736_64d_cpld_data *data;
	int ret = -ENODEV;
	int status;	
	const struct attribute_group *group = NULL;

	if (!i2c_check_functionality(adap, I2C_FUNC_SMBUS_BYTE))
		goto exit;

	data = kzalloc(sizeof(struct as9736_64d_cpld_data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto exit;
	}

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);
	data->type = id->driver_data;

	/* Register sysfs hooks */
    switch (data->type) {
    case as9736_64d_fpga:
        group = &as9736_64d_fpga_group;
        break;
    case as9736_64d_sys_cpld:
        group = &as9736_64d_sys_cpld_group;
        break;
    case as9736_64d_pdb_cpld:
        group = &as9736_64d_pdb_cpld_group;
        break;
    case as9736_64d_scm_cpld:
        group = &as9736_64d_scm_cpld_group;
        break;
	default:
		break;
	}

	if (group) {
		ret = sysfs_create_group(&client->dev.kobj, group);
		if (ret)
			goto exit_free;
	}

    if( data->type != as9736_64d_pdb_cpld ) {
        as9736_64d_cpld_add_client(client);
    }

	return 0;

exit_free:
	kfree(data);
exit:
	return ret;
}

static void as9736_64d_cpld_remove(struct i2c_client *client)
{
	struct as9736_64d_cpld_data *data = i2c_get_clientdata(client);
	const struct attribute_group *group = NULL;

	as9736_64d_cpld_remove_client(client);

	/* Remove sysfs hooks */
	switch (data->type) {
    case as9736_64d_fpga:
        group = &as9736_64d_fpga_group;
        break;
    case as9736_64d_sys_cpld:
        group = &as9736_64d_sys_cpld_group;
        break;
    case as9736_64d_pdb_cpld:
        group = &as9736_64d_pdb_cpld_group;
        break;
    case as9736_64d_scm_cpld:
        group = &as9736_64d_scm_cpld_group;
        break;
	default:
		break;
	}

	if (group)
		sysfs_remove_group(&client->dev.kobj, group);

	kfree(data);
}

static int as9736_64d_cpld_read_internal(struct i2c_client *client, u8 reg)
{
	int status = 0, retry = I2C_RW_RETRY_COUNT;

	while (retry) {
		status = i2c_smbus_read_byte_data(client, reg);
		if (unlikely(status < 0)) {
			msleep(I2C_RW_RETRY_INTERVAL);
			retry--;
			continue;
		}
		break;
	}

	return status;
}

static int as9736_64d_cpld_write_internal(struct i2c_client *client, u8 reg,
						u8 value)
{
	int status = 0, retry = I2C_RW_RETRY_COUNT;

	while (retry) {
		status = i2c_smbus_write_byte_data(client, reg, value);
		if (unlikely(status < 0)) {
			msleep(I2C_RW_RETRY_INTERVAL);
			retry--;
			continue;
		}
		break;
	}

	return status;
}

int as9736_64d_cpld_read(unsigned short cpld_addr, u8 reg)
{
	struct list_head   *list_node = NULL;
	struct cpld_client_node *cpld_node = NULL;
	int ret = -EPERM;

	mutex_lock(&list_lock);

	list_for_each(list_node, &cpld_client_list)
	{
		cpld_node = list_entry(list_node, struct cpld_client_node,
					list);

		if (cpld_node->client->addr == cpld_addr) {
			ret = as9736_64d_cpld_read_internal(cpld_node->client,
								reg);
			break;
		}
	}

	mutex_unlock(&list_lock);

	return ret;
}
EXPORT_SYMBOL(as9736_64d_cpld_read);

int as9736_64d_cpld_write(unsigned short cpld_addr, u8 reg, u8 value)
{
	struct list_head   *list_node = NULL;
	struct cpld_client_node *cpld_node = NULL;
	int ret = -EIO;

	mutex_lock(&list_lock);

	list_for_each(list_node, &cpld_client_list)
	{
		cpld_node = list_entry(list_node, struct cpld_client_node,
					list);
		if (cpld_node->client->addr == cpld_addr) {
			ret = as9736_64d_cpld_write_internal(cpld_node->client,
								reg, value);
			break;
		}
	}

	mutex_unlock(&list_lock);

	return ret;
}
EXPORT_SYMBOL(as9736_64d_cpld_write);

static struct i2c_driver as9736_64d_cpld_driver = {
	.driver		= {
		.name	= "as9736_64d_cpld",
		.owner	= THIS_MODULE,
	},
	.probe		= as9736_64d_cpld_probe,
	.remove		= as9736_64d_cpld_remove,
	.id_table	= as9736_64d_cpld_id,
};

static int __init as9736_64d_cpld_init(void)
{
	mutex_init(&list_lock);
	return i2c_add_driver(&as9736_64d_cpld_driver);
}

static void __exit as9736_64d_cpld_exit(void)
{
	i2c_del_driver(&as9736_64d_cpld_driver);
}

MODULE_AUTHOR("Alex Lai <alex_lai@edge-core.com>");
MODULE_DESCRIPTION("Accton I2C CPLD driver");
MODULE_LICENSE("GPL");

module_init(as9736_64d_cpld_init);
module_exit(as9736_64d_cpld_exit);
