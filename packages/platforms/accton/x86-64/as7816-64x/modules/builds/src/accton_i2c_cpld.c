/*
 * A driver for the accton_i2c_cpld
 *
 * Copyright (C) 2013 Accton Technology Corporation.
 * Brandon Chuang <brandon_chuang@accton.com.tw>
 *
 * Based on ad7414.c
 * Copyright 2006 Stefan Roese <sr at denx.de>, DENX Software Engineering
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Local (per-platform) copy for AS7816-64X. The shared driver under
 * packages/platforms/accton/x86-64/modules/ is still pinned to 4.14.
 * Only the bits 7816 actually exercises (binding + sysfs version
 * attribute) are kept here; the cross-module read/write helpers and
 * other-platform DMI tables in the shared driver are not needed.
 */

#include <linux/module.h>
#include <linux/i2c.h>

static const unsigned short normal_i2c[] = { 0x62, 0x64, 0x66, I2C_CLIENT_END };

static ssize_t show_cpld_version(struct device *dev, struct device_attribute *attr, char *buf)
{
	int val = 0;
	struct i2c_client *client = to_i2c_client(dev);

	val = i2c_smbus_read_byte_data(client, 0x1);

	if (val < 0) {
		dev_dbg(&client->dev, "cpld(0x%x) reg(0x1) err %d\n", client->addr, val);
	}

	return sprintf(buf, "%d", val);
}

static struct device_attribute ver = __ATTR(version, 0600, show_cpld_version, NULL);

static int accton_i2c_cpld_probe(struct i2c_client *client)
{
	int status;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_dbg(&client->dev, "i2c_check_functionality failed (0x%x)\n", client->addr);
		return -EIO;
	}

	status = sysfs_create_file(&client->dev.kobj, &ver.attr);
	if (status)
		return status;

	dev_info(&client->dev, "chip found\n");
	return 0;
}

static void accton_i2c_cpld_remove(struct i2c_client *client)
{
	sysfs_remove_file(&client->dev.kobj, &ver.attr);
}

static const struct i2c_device_id accton_i2c_cpld_id[] = {
	{ "accton_i2c_cpld", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, accton_i2c_cpld_id);

static struct i2c_driver accton_i2c_cpld_driver = {
	.driver = {
		.name = "accton_i2c_cpld",
	},
	.probe		= accton_i2c_cpld_probe,
	.remove		= accton_i2c_cpld_remove,
	.id_table	= accton_i2c_cpld_id,
	.address_list = normal_i2c,
};

MODULE_AUTHOR("Brandon Chuang <brandon_chuang@accton.com.tw>");
MODULE_DESCRIPTION("accton_i2c_cpld driver");
MODULE_LICENSE("GPL");

module_i2c_driver(accton_i2c_cpld_driver);
