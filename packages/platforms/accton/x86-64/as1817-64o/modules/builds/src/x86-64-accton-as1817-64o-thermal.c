/*
 * A thermal driver for accton as1817_64o
 *
 * Copyright (C) 2026 Accton Technology Corporation.
 * rayx_huang <rayx_huang@edge-core.com>
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
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/stat.h>
#include <linux/sysfs.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/ipmi.h>
#include <linux/ipmi_smi.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>

#define DRVNAME "as1817_64o_thermal"
#define ACCTON_IPMI_NETFN 0x34
#define IPMI_THERMAL_READ_CMD 0x12

#define IPMI_TIMEOUT (5 * HZ)
#define IPMI_ERR_RETRY_TIMES 1

#define THERMAL_COUNT 14
#define THERMAL_DATA_LEN 3
#define THERMAL_DATA_SIZE (THERMAL_COUNT * THERMAL_DATA_LEN)

enum temp_data_index {
	TEMP_ADDR,
	TEMP_FAULT,
	TEMP_INPUT
};

static void ipmi_msg_handler(struct ipmi_recv_msg *msg, void *user_msg_data);
static ssize_t show_temp(struct device *dev, struct device_attribute *da, char *buf);
static int as1817_64o_thermal_probe(struct platform_device *pdev);
static void as1817_64o_thermal_remove(struct platform_device *pdev);

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

struct as1817_64o_thermal_data {
	struct platform_device *pdev;
	struct device *hwmon_dev;
	struct mutex update_lock;
	char valid;
	unsigned long last_updated;
	struct ipmi_data ipmi;
	unsigned char ipmi_resp[THERMAL_DATA_SIZE];
};

static struct as1817_64o_thermal_data *data = NULL;

static struct platform_driver as1817_64o_thermal_driver = {
	.probe = as1817_64o_thermal_probe,
	.remove = as1817_64o_thermal_remove,
	.driver = {
		.name = DRVNAME,
		.owner = THIS_MODULE,
	},
};

enum as1817_64o_thermal_sysfs_attrs {
	TEMP1_INPUT,
	TEMP2_INPUT,
	TEMP3_INPUT,
	TEMP4_INPUT,
	TEMP5_INPUT,
	TEMP6_INPUT,
	TEMP7_INPUT,
	TEMP8_INPUT,
	TEMP9_INPUT,
	TEMP10_INPUT,
	TEMP11_INPUT,
	TEMP12_INPUT,
	TEMP13_INPUT,
	TEMP14_INPUT,
};

#define DECLARE_THERMAL_SENSOR_DEVICE_ATTR(index) \
	static SENSOR_DEVICE_ATTR(temp##index##_input, S_IRUGO, show_temp, \
				  NULL, TEMP##index##_INPUT)

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

static struct attribute *as1817_64o_thermal_attrs[] = {
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
	NULL
};
ATTRIBUTE_GROUPS(as1817_64o_thermal);

/* IPMI functions */
static int init_ipmi_data(struct ipmi_data *ipmi, int iface, struct device *dev)
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
		dev_err(dev, "Unable to register user with IPMI interface %d\n",
			ipmi->interface);
		return -EACCES;
	}

	return 0;
}

static int _ipmi_send_message(struct ipmi_data *ipmi, unsigned char cmd,
			      unsigned char *tx_data, unsigned short tx_len,
			      unsigned char *rx_data, unsigned short rx_len)
{
	int err;

	ipmi->tx_message.cmd = cmd;
	ipmi->tx_message.data = tx_data;
	ipmi->tx_message.data_len = tx_len;
	ipmi->rx_msg_data = rx_data;
	ipmi->rx_msg_len = rx_len;

	err = ipmi_validate_addr(&ipmi->address, sizeof(ipmi->address));
	if (err)
		return err;

	ipmi->tx_msgid++;
	err = ipmi_request_settime(ipmi->user, &ipmi->address, ipmi->tx_msgid,
				   &ipmi->tx_message, ipmi, 0, 0, 0);
	if (err)
		return err;

	err = wait_for_completion_timeout(&ipmi->read_complete, IPMI_TIMEOUT);
	if (!err)
		return -ETIMEDOUT;

	return 0;
}

static int ipmi_send_message(struct ipmi_data *ipmi, unsigned char cmd,
			     unsigned char *tx_data, unsigned short tx_len,
			     unsigned char *rx_data, unsigned short rx_len)
{
	int status = 0, retry = 0;

	for (retry = 0; retry <= IPMI_ERR_RETRY_TIMES; retry++) {
		status = _ipmi_send_message(ipmi, cmd, tx_data, tx_len,
					    rx_data, rx_len);
		if (unlikely(status != 0))
			continue;
		if (unlikely(ipmi->rx_result != 0))
			continue;
		break;
	}

	return status;
}

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

/* Thermal show function using IPMI 0x12 */
static ssize_t show_temp(struct device *dev, struct device_attribute *da,
			 char *buf)
{
	int status = 0;
	int index;
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ * 5) || !data->valid) {
		data->valid = 0;

		status = ipmi_send_message(&data->ipmi, IPMI_THERMAL_READ_CMD,
					   NULL, 0, data->ipmi_resp,
					   sizeof(data->ipmi_resp));
		if (unlikely(status != 0))
			goto exit;

		if (unlikely(data->ipmi.rx_result != 0)) {
			status = -EIO;
			goto exit;
		}

		data->last_updated = jiffies;
		data->valid = 1;
	}

	/* Check fault status */
	index = attr->index * THERMAL_DATA_LEN + TEMP_FAULT;
	if (unlikely(data->ipmi_resp[index] == 0)) {
		status = -EIO;
		goto exit;
	}

	/* Return temperature in millidegrees */
	index = attr->index * THERMAL_DATA_LEN + TEMP_INPUT;
	mutex_unlock(&data->update_lock);
	return sprintf(buf, "%d\n", ((s8)data->ipmi_resp[index]) * 1000);

exit:
	mutex_unlock(&data->update_lock);
	return status;
}

static int as1817_64o_thermal_probe(struct platform_device *pdev)
{
	struct device *hwmon_dev;

	hwmon_dev = hwmon_device_register_with_groups(&pdev->dev, DRVNAME,
						     NULL, as1817_64o_thermal_groups);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	data->hwmon_dev = hwmon_dev;
	dev_info(&pdev->dev, "device created\n");
	return 0;
}

static void as1817_64o_thermal_remove(struct platform_device *pdev)
{
	if (data->hwmon_dev) {
		hwmon_device_unregister(data->hwmon_dev);
		data->hwmon_dev = NULL;
	}
}

static int __init as1817_64o_thermal_init(void)
{
	int ret;

	data = kzalloc(sizeof(struct as1817_64o_thermal_data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto alloc_err;
	}

	mutex_init(&data->update_lock);

	ret = platform_driver_register(&as1817_64o_thermal_driver);
	if (ret < 0)
		goto dri_reg_err;

	data->pdev = platform_device_register_simple(DRVNAME, -1, NULL, 0);
	if (IS_ERR(data->pdev)) {
		ret = PTR_ERR(data->pdev);
		goto dev_reg_err;
	}

	ret = init_ipmi_data(&data->ipmi, 0, &data->pdev->dev);
	if (ret)
		goto ipmi_err;

	return 0;

ipmi_err:
	platform_device_unregister(data->pdev);
dev_reg_err:
	platform_driver_unregister(&as1817_64o_thermal_driver);
dri_reg_err:
	kfree(data);
alloc_err:
	return ret;
}

static void __exit as1817_64o_thermal_exit(void)
{
	platform_device_unregister(data->pdev);
	platform_driver_unregister(&as1817_64o_thermal_driver);
	ipmi_destroy_user(data->ipmi.user);
	kfree(data);
}

MODULE_AUTHOR("rayx_huang <rayx_huang@edge-core.com>");
MODULE_DESCRIPTION("as1817_64o_thermal driver");
MODULE_LICENSE("GPL");

module_init(as1817_64o_thermal_init);
module_exit(as1817_64o_thermal_exit);
