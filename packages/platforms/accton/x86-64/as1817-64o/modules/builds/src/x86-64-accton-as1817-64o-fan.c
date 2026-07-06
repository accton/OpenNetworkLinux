/*
 * A fan driver for accton as1817_64o via IPMI
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

#define DRVNAME "as1817_64o_fan"
#define ACCTON_IPMI_NETFN 0x34
#define IPMI_FAN_READ_CMD 0x14

#define IPMI_TIMEOUT (5 * HZ)
#define IPMI_ERR_RETRY_TIMES 1

#define NUM_OF_FAN_MODULE 8
#define MAX_FAN_SPEED_FRONT 20500
#define MAX_FAN_SPEED_REAR  21800

enum fan_data_index {
	FAN_PRESENT,
	FAN_PWM,
	FAN_SPEED0,
	FAN_SPEED1,
	FAN_DATA_COUNT
};

/* IPMI response: 8 front rotors * 4 bytes + 8 rear rotors * 4 bytes + 2 bytes fan dir */
#define FAN_RESP_SIZE (NUM_OF_FAN_MODULE * 2 * FAN_DATA_COUNT + 2)

enum fan_sysfs_attrs {
	FAN_ATTR_PRESENT,
	FAN_ATTR_PWM,
	FAN_ATTR_DIR,
	FAN_ATTR_FRONT_INPUT,
	FAN_ATTR_REAR_INPUT,
	FAN_ATTR_FRONT_FAULT,
	FAN_ATTR_REAR_FAULT,
	FAN_ATTR_FRONT_MAX_SPEED,
	FAN_ATTR_REAR_MAX_SPEED,
	FAN_ATTR_FRONT_PERCENTAGE,
	FAN_ATTR_REAR_PERCENTAGE,
};

static void ipmi_msg_handler(struct ipmi_recv_msg *msg, void *user_msg_data);
static ssize_t show_fan(struct device *dev, struct device_attribute *da, char *buf);
static int as1817_64o_fan_probe(struct platform_device *pdev);
static void as1817_64o_fan_remove(struct platform_device *pdev);

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

struct as1817_64o_fan_data {
	struct platform_device *pdev;
	struct device *hwmon_dev;
	struct mutex update_lock;
	char valid;
	unsigned long last_updated;
	unsigned char ipmi_resp[FAN_RESP_SIZE];
	struct ipmi_data ipmi;
};

static struct as1817_64o_fan_data *data = NULL;

static struct platform_driver as1817_64o_fan_driver = {
	.probe = as1817_64o_fan_probe,
	.remove = as1817_64o_fan_remove,
	.driver = {
		.name = DRVNAME,
		.owner = THIS_MODULE,
	},
};

/* Each fan module index encodes: bits[7:4]=module(0-3), bits[3:0]=attr */
#define FAN_MOD_ATTR(mod, attr) (((mod) << 4) | (attr))
#define FAN_MOD_ID(index) ((index) >> 4)
#define FAN_MOD_ATTR_ID(index) ((index) & 0x0f)

#define DECLARE_FAN_MODULE_ATTR(index) \
	static SENSOR_DEVICE_ATTR(fan##index##_present, S_IRUGO, show_fan, \
		NULL, FAN_MOD_ATTR((index)-1, FAN_ATTR_PRESENT)); \
	static SENSOR_DEVICE_ATTR(fan##index##_pwm, S_IRUGO, \
		show_fan, NULL, FAN_MOD_ATTR((index)-1, FAN_ATTR_PWM)); \
	static SENSOR_DEVICE_ATTR(fan##index##_dir, S_IRUGO, show_fan, \
		NULL, FAN_MOD_ATTR((index)-1, FAN_ATTR_DIR)); \
	static SENSOR_DEVICE_ATTR(fan##index##_front_input, S_IRUGO, show_fan, \
		NULL, FAN_MOD_ATTR((index)-1, FAN_ATTR_FRONT_INPUT)); \
	static SENSOR_DEVICE_ATTR(fan##index##_rear_input, S_IRUGO, show_fan, \
		NULL, FAN_MOD_ATTR((index)-1, FAN_ATTR_REAR_INPUT)); \
	static SENSOR_DEVICE_ATTR(fan##index##_front_fault, S_IRUGO, show_fan, \
		NULL, FAN_MOD_ATTR((index)-1, FAN_ATTR_FRONT_FAULT)); \
	static SENSOR_DEVICE_ATTR(fan##index##_rear_fault, S_IRUGO, show_fan, \
		NULL, FAN_MOD_ATTR((index)-1, FAN_ATTR_REAR_FAULT)); \
	static SENSOR_DEVICE_ATTR(fan##index##_front_max_speed, S_IRUGO, show_fan, \
		NULL, FAN_MOD_ATTR((index)-1, FAN_ATTR_FRONT_MAX_SPEED)); \
	static SENSOR_DEVICE_ATTR(fan##index##_rear_max_speed, S_IRUGO, show_fan, \
		NULL, FAN_MOD_ATTR((index)-1, FAN_ATTR_REAR_MAX_SPEED)); \
	static SENSOR_DEVICE_ATTR(fan##index##_front_percentage, S_IRUGO, show_fan, \
		NULL, FAN_MOD_ATTR((index)-1, FAN_ATTR_FRONT_PERCENTAGE)); \
	static SENSOR_DEVICE_ATTR(fan##index##_rear_percentage, S_IRUGO, show_fan, \
		NULL, FAN_MOD_ATTR((index)-1, FAN_ATTR_REAR_PERCENTAGE))

#define DECLARE_FAN_MODULE_SYSFS(index) \
	&sensor_dev_attr_fan##index##_present.dev_attr.attr, \
	&sensor_dev_attr_fan##index##_pwm.dev_attr.attr, \
	&sensor_dev_attr_fan##index##_dir.dev_attr.attr, \
	&sensor_dev_attr_fan##index##_front_input.dev_attr.attr, \
	&sensor_dev_attr_fan##index##_rear_input.dev_attr.attr, \
	&sensor_dev_attr_fan##index##_front_fault.dev_attr.attr, \
	&sensor_dev_attr_fan##index##_rear_fault.dev_attr.attr, \
	&sensor_dev_attr_fan##index##_front_max_speed.dev_attr.attr, \
	&sensor_dev_attr_fan##index##_rear_max_speed.dev_attr.attr, \
	&sensor_dev_attr_fan##index##_front_percentage.dev_attr.attr, \
	&sensor_dev_attr_fan##index##_rear_percentage.dev_attr.attr

DECLARE_FAN_MODULE_ATTR(1);
DECLARE_FAN_MODULE_ATTR(2);
DECLARE_FAN_MODULE_ATTR(3);
DECLARE_FAN_MODULE_ATTR(4);
DECLARE_FAN_MODULE_ATTR(5);
DECLARE_FAN_MODULE_ATTR(6);
DECLARE_FAN_MODULE_ATTR(7);
DECLARE_FAN_MODULE_ATTR(8);

static struct attribute *as1817_64o_fan_attrs[] = {
	DECLARE_FAN_MODULE_SYSFS(1),
	DECLARE_FAN_MODULE_SYSFS(2),
	DECLARE_FAN_MODULE_SYSFS(3),
	DECLARE_FAN_MODULE_SYSFS(4),
	DECLARE_FAN_MODULE_SYSFS(5),
	DECLARE_FAN_MODULE_SYSFS(6),
	DECLARE_FAN_MODULE_SYSFS(7),
	DECLARE_FAN_MODULE_SYSFS(8),
	NULL
};
ATTRIBUTE_GROUPS(as1817_64o_fan);

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
		dev_err(dev, "Unable to register user with IPMI interface %d\n", iface);
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

static void as1817_64o_fan_update(void)
{
	int status;

	if (time_before(jiffies, data->last_updated + HZ * 5) && data->valid)
		return;

	data->valid = 0;
	status = ipmi_send_message(&data->ipmi, IPMI_FAN_READ_CMD, NULL, 0,
				   data->ipmi_resp, sizeof(data->ipmi_resp));
	if (unlikely(status != 0 || data->ipmi.rx_result != 0))
		return;

	data->last_updated = jiffies;
	data->valid = 1;
}

static ssize_t show_fan(struct device *dev, struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	int mid = FAN_MOD_ID(attr->index);
	int aid = FAN_MOD_ATTR_ID(attr->index);
	int front_idx, rear_idx, value = 0, dir;
	int error = 0;

	mutex_lock(&data->update_lock);
	as1817_64o_fan_update();
	if (!data->valid) {
		error = -EIO;
		goto exit;
	}

	/* Front fan data at offset mid*4, rear at (mid+4)*4 */
	front_idx = mid * FAN_DATA_COUNT;
	rear_idx = (mid + NUM_OF_FAN_MODULE) * FAN_DATA_COUNT;

	switch (aid) {
	case FAN_ATTR_PRESENT:
		value = !!data->ipmi_resp[front_idx + FAN_PRESENT];
		break;
	case FAN_ATTR_PWM:
		value = data->ipmi_resp[front_idx + FAN_PWM];
		break;
	case FAN_ATTR_DIR:
		dir = data->ipmi_resp[NUM_OF_FAN_MODULE * 2 * FAN_DATA_COUNT] |
		      (data->ipmi_resp[NUM_OF_FAN_MODULE * 2 * FAN_DATA_COUNT + 1] << 8);
		mutex_unlock(&data->update_lock);
		return sprintf(buf, "%s\n", (dir & BIT(mid)) ? "B2F" : "F2B");
	case FAN_ATTR_FRONT_INPUT:
		value = (int)data->ipmi_resp[front_idx + FAN_SPEED0] |
			(int)data->ipmi_resp[front_idx + FAN_SPEED1] << 8;
		break;
	case FAN_ATTR_REAR_INPUT:
		value = (int)data->ipmi_resp[rear_idx + FAN_SPEED0] |
			(int)data->ipmi_resp[rear_idx + FAN_SPEED1] << 8;
		break;
	case FAN_ATTR_FRONT_FAULT:
		if (!data->ipmi_resp[front_idx + FAN_PRESENT]) {
			value = 1;
		} else {
			int rpm = (int)data->ipmi_resp[front_idx + FAN_SPEED0] |
				  (int)data->ipmi_resp[front_idx + FAN_SPEED1] << 8;
			value = (rpm == 0) ? 1 : 0;
		}
		break;
	case FAN_ATTR_REAR_FAULT:
		if (!data->ipmi_resp[front_idx + FAN_PRESENT]) {
			value = 1;
		} else {
			int rpm = (int)data->ipmi_resp[rear_idx + FAN_SPEED0] |
				  (int)data->ipmi_resp[rear_idx + FAN_SPEED1] << 8;
			value = (rpm == 0) ? 1 : 0;
		}
		break;
	case FAN_ATTR_FRONT_MAX_SPEED:
		value = MAX_FAN_SPEED_FRONT;
		break;
	case FAN_ATTR_REAR_MAX_SPEED:
		value = MAX_FAN_SPEED_REAR;
		break;
	case FAN_ATTR_FRONT_PERCENTAGE:
		value = (int)data->ipmi_resp[front_idx + FAN_SPEED0] |
			(int)data->ipmi_resp[front_idx + FAN_SPEED1] << 8;
		value = DIV_ROUND_CLOSEST(value * 100, MAX_FAN_SPEED_FRONT);
		if (value > 100)
			value = 100;
		break;
	case FAN_ATTR_REAR_PERCENTAGE:
		value = (int)data->ipmi_resp[rear_idx + FAN_SPEED0] |
			(int)data->ipmi_resp[rear_idx + FAN_SPEED1] << 8;
		value = DIV_ROUND_CLOSEST(value * 100, MAX_FAN_SPEED_REAR);
		if (value > 100)
			value = 100;
		break;
	default:
		error = -EINVAL;
		goto exit;
	}

	mutex_unlock(&data->update_lock);
	return sprintf(buf, "%d\n", value);

exit:
	mutex_unlock(&data->update_lock);
	return error;
}

static int as1817_64o_fan_probe(struct platform_device *pdev)
{
	struct device *hwmon_dev;

	hwmon_dev = hwmon_device_register_with_groups(&pdev->dev, DRVNAME,
						     NULL, as1817_64o_fan_groups);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	data->hwmon_dev = hwmon_dev;
	dev_info(&pdev->dev, "device created\n");
	return 0;
}

static void as1817_64o_fan_remove(struct platform_device *pdev)
{
	if (data->hwmon_dev) {
		hwmon_device_unregister(data->hwmon_dev);
		data->hwmon_dev = NULL;
	}
}

static int __init as1817_64o_fan_init(void)
{
	int ret;

	data = kzalloc(sizeof(struct as1817_64o_fan_data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto alloc_err;
	}

	mutex_init(&data->update_lock);

	ret = platform_driver_register(&as1817_64o_fan_driver);
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
	platform_driver_unregister(&as1817_64o_fan_driver);
dri_reg_err:
	kfree(data);
alloc_err:
	return ret;
}

static void __exit as1817_64o_fan_exit(void)
{
	platform_device_unregister(data->pdev);
	platform_driver_unregister(&as1817_64o_fan_driver);
	ipmi_destroy_user(data->ipmi.user);
	kfree(data);
}

MODULE_AUTHOR("rayx_huang <rayx_huang@edge-core.com>");
MODULE_DESCRIPTION("as1817_64o_fan driver");
MODULE_LICENSE("GPL");

module_init(as1817_64o_fan_init);
module_exit(as1817_64o_fan_exit);
