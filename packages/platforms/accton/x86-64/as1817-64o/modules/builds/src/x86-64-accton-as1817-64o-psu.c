/*
 * A PSU driver for accton as1817_64o via IPMI
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

#define DRVNAME "as1817_64o_psu"
#define ACCTON_IPMI_NETFN 0x34
#define IPMI_PSU_READ_CMD 0x16
#define IPMI_PSU_MODEL_NAME_CMD 0x10
#define IPMI_PSU_SERIAL_NUM_CMD 0x11
#define IPMI_PSU_FAN_DIR_CMD 0x13
#define IPMI_PSU_INFO_CMD 0x20

#define PSU_INFO_SIZE 46

enum psu_info_offset {
	PSU_INFO_FANDIR       = 0,
	PSU_INFO_TYPE         = 1,
	PSU_INFO_TEMP1_HCRIT  = 2,
	PSU_INFO_TEMP1_LCRIT  = 4,
	PSU_INFO_TEMP2_HCRIT  = 6,
	PSU_INFO_TEMP2_LCRIT  = 8,
	PSU_INFO_TEMP3_HCRIT  = 10,
	PSU_INFO_TEMP3_LCRIT  = 12,
	PSU_INFO_VIN_HCRIT    = 14,
	PSU_INFO_VIN_HWARN    = 17,
	PSU_INFO_VIN_LWARN    = 20,
	PSU_INFO_VIN_LCRIT    = 23,
	PSU_INFO_VOUT_HCRIT   = 26,
	PSU_INFO_VOUT_LCRIT   = 29,
	PSU_INFO_IIN_HCRIT    = 32,
	PSU_INFO_IOUT_HCRIT   = 35,
	PSU_INFO_PIN_HCRIT    = 38,
	PSU_INFO_POUT_HCRIT   = 42,
};

#define IPMI_TIMEOUT (5 * HZ)
#define IPMI_ERR_RETRY_TIMES 1
#define IPMI_MODEL_SERIAL_LEN 32
#define IPMI_FAN_DIR_LEN 3

#define NUM_OF_PSU 4

enum psu_data_index {
	PSU_PRESENT = 0,
	PSU_TEMP_FAULT,
	PSU_POWER_GOOD_CPLD,
	PSU_POWER_GOOD_PMBUS,
	PSU_OVER_VOLTAGE,
	PSU_OVER_CURRENT,
	PSU_POWER_ON,
	PSU_VIN0, PSU_VIN1, PSU_VIN2,
	PSU_VOUT0, PSU_VOUT1, PSU_VOUT2,
	PSU_IIN0, PSU_IIN1, PSU_IIN2,
	PSU_IOUT0, PSU_IOUT1, PSU_IOUT2,
	PSU_PIN0, PSU_PIN1, PSU_PIN2, PSU_PIN3,
	PSU_POUT0, PSU_POUT1, PSU_POUT2, PSU_POUT3,
	PSU_TEMP1_0, PSU_TEMP1_1,
	PSU_TEMP2_0, PSU_TEMP2_1,
	PSU_TEMP3_0, PSU_TEMP3_1,
	PSU_FAN0, PSU_FAN1,
	PSU_VOUT_MODE,
	PSU_STATUS_COUNT
};

enum psu_sysfs_attrs {
	PSU_ATTR_PRESENT,
	PSU_ATTR_POWER_GOOD,
	PSU_ATTR_VIN,
	PSU_ATTR_VOUT,
	PSU_ATTR_IIN,
	PSU_ATTR_IOUT,
	PSU_ATTR_PIN,
	PSU_ATTR_POUT,
	PSU_ATTR_MODEL,
	PSU_ATTR_SERIAL,
	PSU_ATTR_TEMP1_INPUT,
	PSU_ATTR_TEMP2_INPUT,
	PSU_ATTR_TEMP3_INPUT,
	PSU_ATTR_FAN_INPUT,
	PSU_ATTR_FAN_MAX_SPEED,
	PSU_ATTR_FAN_PERCENTAGE,
	PSU_ATTR_FAN_DIR,
	PSU_ATTR_TYPE,
	PSU_ATTR_TEMP1_HIGH_CRIT,
	PSU_ATTR_TEMP1_LOW_CRIT,
	PSU_ATTR_TEMP2_HIGH_CRIT,
	PSU_ATTR_TEMP2_LOW_CRIT,
	PSU_ATTR_TEMP3_HIGH_CRIT,
	PSU_ATTR_TEMP3_LOW_CRIT,
	PSU_ATTR_VIN_HIGH_CRIT,
	PSU_ATTR_VIN_LOW_CRIT,
	PSU_ATTR_VIN_HIGH_WARN,
	PSU_ATTR_VIN_LOW_WARN,
	PSU_ATTR_VOUT_HIGH_CRIT,
	PSU_ATTR_VOUT_LOW_CRIT,
	PSU_ATTR_IIN_HIGH_CRIT,
	PSU_ATTR_IOUT_HIGH_CRIT,
};

static void ipmi_msg_handler(struct ipmi_recv_msg *msg, void *user_msg_data);
static ssize_t show_psu(struct device *dev, struct device_attribute *da, char *buf);
static ssize_t show_string(struct device *dev, struct device_attribute *da, char *buf);
static int as1817_64o_psu_probe(struct platform_device *pdev);
static void as1817_64o_psu_remove(struct platform_device *pdev);

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

struct ipmi_psu_resp_data {
	unsigned char status[PSU_STATUS_COUNT];
	unsigned char info[PSU_INFO_SIZE];
	char model[IPMI_MODEL_SERIAL_LEN + 1];
	char serial[IPMI_MODEL_SERIAL_LEN + 1];
	char fandir[IPMI_FAN_DIR_LEN + 1];
};

struct as1817_64o_psu_data {
	struct platform_device *pdev[NUM_OF_PSU];
	struct device *hwmon_dev[NUM_OF_PSU];
	struct mutex update_lock;
	char valid[NUM_OF_PSU];
	unsigned long last_updated[NUM_OF_PSU];
	struct ipmi_data ipmi;
	struct ipmi_psu_resp_data ipmi_resp[NUM_OF_PSU];
	unsigned char ipmi_tx_data[2];
};

static struct as1817_64o_psu_data *data = NULL;

static struct platform_driver as1817_64o_psu_driver = {
	.probe = as1817_64o_psu_probe,
	.remove = as1817_64o_psu_remove,
	.driver = {
		.name = DRVNAME,
		.owner = THIS_MODULE,
	},
};

#define DECLARE_PSU_SENSOR_DEVICE_ATTR() \
	static SENSOR_DEVICE_ATTR(psu_present, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_PRESENT); \
	static SENSOR_DEVICE_ATTR(psu_power_good, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_POWER_GOOD); \
	static SENSOR_DEVICE_ATTR(psu_vin, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_VIN); \
	static SENSOR_DEVICE_ATTR(psu_vout, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_VOUT); \
	static SENSOR_DEVICE_ATTR(psu_iin, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_IIN); \
	static SENSOR_DEVICE_ATTR(psu_iout, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_IOUT); \
	static SENSOR_DEVICE_ATTR(psu_pin, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_PIN); \
	static SENSOR_DEVICE_ATTR(psu_pout, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_POUT); \
	static SENSOR_DEVICE_ATTR(psu_model, S_IRUGO, show_string, \
				  NULL, PSU_ATTR_MODEL); \
	static SENSOR_DEVICE_ATTR(psu_serial, S_IRUGO, show_string, \
				  NULL, PSU_ATTR_SERIAL); \
	static SENSOR_DEVICE_ATTR(psu_temp1_input, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_TEMP1_INPUT); \
	static SENSOR_DEVICE_ATTR(psu_temp2_input, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_TEMP2_INPUT); \
	static SENSOR_DEVICE_ATTR(psu_temp3_input, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_TEMP3_INPUT); \
	static SENSOR_DEVICE_ATTR(psu_fan_input, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_FAN_INPUT); \
	static SENSOR_DEVICE_ATTR(psu_fan_max_speed, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_FAN_MAX_SPEED); \
	static SENSOR_DEVICE_ATTR(psu_fan_percentage, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_FAN_PERCENTAGE); \
	static SENSOR_DEVICE_ATTR(psu_fan_dir, S_IRUGO, show_string, \
				  NULL, PSU_ATTR_FAN_DIR); \
	static SENSOR_DEVICE_ATTR(psu_type, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_TYPE); \
	static SENSOR_DEVICE_ATTR(psu_temp1_high_crit, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_TEMP1_HIGH_CRIT); \
	static SENSOR_DEVICE_ATTR(psu_temp1_low_crit, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_TEMP1_LOW_CRIT); \
	static SENSOR_DEVICE_ATTR(psu_temp2_high_crit, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_TEMP2_HIGH_CRIT); \
	static SENSOR_DEVICE_ATTR(psu_temp2_low_crit, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_TEMP2_LOW_CRIT); \
	static SENSOR_DEVICE_ATTR(psu_temp3_high_crit, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_TEMP3_HIGH_CRIT); \
	static SENSOR_DEVICE_ATTR(psu_temp3_low_crit, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_TEMP3_LOW_CRIT); \
	static SENSOR_DEVICE_ATTR(psu_vin_high_crit, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_VIN_HIGH_CRIT); \
	static SENSOR_DEVICE_ATTR(psu_vin_low_crit, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_VIN_LOW_CRIT); \
	static SENSOR_DEVICE_ATTR(psu_vin_high_warn, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_VIN_HIGH_WARN); \
	static SENSOR_DEVICE_ATTR(psu_vin_low_warn, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_VIN_LOW_WARN); \
	static SENSOR_DEVICE_ATTR(psu_vout_high_crit, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_VOUT_HIGH_CRIT); \
	static SENSOR_DEVICE_ATTR(psu_vout_low_crit, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_VOUT_LOW_CRIT); \
	static SENSOR_DEVICE_ATTR(psu_iin_high_crit, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_IIN_HIGH_CRIT); \
	static SENSOR_DEVICE_ATTR(psu_iout_high_crit, S_IRUGO, show_psu, \
				  NULL, PSU_ATTR_IOUT_HIGH_CRIT)

DECLARE_PSU_SENSOR_DEVICE_ATTR();

static struct attribute *as1817_64o_psu_attrs[] = {
	&sensor_dev_attr_psu_present.dev_attr.attr,
	&sensor_dev_attr_psu_power_good.dev_attr.attr,
	&sensor_dev_attr_psu_vin.dev_attr.attr,
	&sensor_dev_attr_psu_vout.dev_attr.attr,
	&sensor_dev_attr_psu_iin.dev_attr.attr,
	&sensor_dev_attr_psu_iout.dev_attr.attr,
	&sensor_dev_attr_psu_pin.dev_attr.attr,
	&sensor_dev_attr_psu_pout.dev_attr.attr,
	&sensor_dev_attr_psu_model.dev_attr.attr,
	&sensor_dev_attr_psu_serial.dev_attr.attr,
	&sensor_dev_attr_psu_temp1_input.dev_attr.attr,
	&sensor_dev_attr_psu_temp2_input.dev_attr.attr,
	&sensor_dev_attr_psu_temp3_input.dev_attr.attr,
	&sensor_dev_attr_psu_fan_input.dev_attr.attr,
	&sensor_dev_attr_psu_fan_max_speed.dev_attr.attr,
	&sensor_dev_attr_psu_fan_percentage.dev_attr.attr,
	&sensor_dev_attr_psu_fan_dir.dev_attr.attr,
	&sensor_dev_attr_psu_type.dev_attr.attr,
	&sensor_dev_attr_psu_temp1_high_crit.dev_attr.attr,
	&sensor_dev_attr_psu_temp1_low_crit.dev_attr.attr,
	&sensor_dev_attr_psu_temp2_high_crit.dev_attr.attr,
	&sensor_dev_attr_psu_temp2_low_crit.dev_attr.attr,
	&sensor_dev_attr_psu_temp3_high_crit.dev_attr.attr,
	&sensor_dev_attr_psu_temp3_low_crit.dev_attr.attr,
	&sensor_dev_attr_psu_vin_high_crit.dev_attr.attr,
	&sensor_dev_attr_psu_vin_low_crit.dev_attr.attr,
	&sensor_dev_attr_psu_vin_high_warn.dev_attr.attr,
	&sensor_dev_attr_psu_vin_low_warn.dev_attr.attr,
	&sensor_dev_attr_psu_vout_high_crit.dev_attr.attr,
	&sensor_dev_attr_psu_vout_low_crit.dev_attr.attr,
	&sensor_dev_attr_psu_iin_high_crit.dev_attr.attr,
	&sensor_dev_attr_psu_iout_high_crit.dev_attr.attr,
	NULL
};

static struct attribute_group as1817_64o_psu_group = { .attrs = as1817_64o_psu_attrs };

static const struct attribute_group *as1817_64o_psu_groups[] = {
	&as1817_64o_psu_group, NULL
};

/* IPMI functions */
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

static void as1817_64o_psu_update(unsigned char pid)
{
	int status;

	if (time_before(jiffies, data->last_updated[pid] + HZ * 5) && data->valid[pid])
		return;

	data->valid[pid] = 0;
	data->ipmi_resp[pid].status[PSU_VOUT_MODE] = 0xff;

	data->ipmi_tx_data[0] = pid + 1;
	status = ipmi_send_message(&data->ipmi, IPMI_PSU_READ_CMD,
				   data->ipmi_tx_data, 1,
				   data->ipmi_resp[pid].status,
				   sizeof(data->ipmi_resp[pid].status));
	if (unlikely(status != 0 || data->ipmi.rx_result != 0))
		return;

	data->ipmi_tx_data[1] = IPMI_PSU_MODEL_NAME_CMD;
	status = ipmi_send_message(&data->ipmi, IPMI_PSU_READ_CMD,
				   data->ipmi_tx_data, 2,
				   data->ipmi_resp[pid].model,
				   sizeof(data->ipmi_resp[pid].model) - 1);
	if (unlikely(status != 0 || data->ipmi.rx_result != 0))
		return;

	data->ipmi_tx_data[1] = IPMI_PSU_SERIAL_NUM_CMD;
	status = ipmi_send_message(&data->ipmi, IPMI_PSU_READ_CMD,
				   data->ipmi_tx_data, 2,
				   data->ipmi_resp[pid].serial,
				   sizeof(data->ipmi_resp[pid].serial) - 1);
	if (unlikely(status != 0 || data->ipmi.rx_result != 0))
		return;

	data->ipmi_tx_data[1] = IPMI_PSU_FAN_DIR_CMD;
	status = ipmi_send_message(&data->ipmi, IPMI_PSU_READ_CMD,
				   data->ipmi_tx_data, 2,
				   data->ipmi_resp[pid].fandir,
				   sizeof(data->ipmi_resp[pid].fandir) - 1);
	if (unlikely(status != 0 || data->ipmi.rx_result != 0))
		return;

	data->ipmi_tx_data[1] = IPMI_PSU_INFO_CMD;
	status = ipmi_send_message(&data->ipmi, IPMI_PSU_READ_CMD,
				   data->ipmi_tx_data, 2,
				   data->ipmi_resp[pid].info,
				   sizeof(data->ipmi_resp[pid].info));
	if (unlikely(status != 0 || data->ipmi.rx_result != 0))
		return;

	data->last_updated[pid] = jiffies;
	data->valid[pid] = 1;
}

#define VALIDATE_PRESENT_RETURN(pid) \
do { \
	if (data->ipmi_resp[pid].status[PSU_PRESENT] != 1) { \
		mutex_unlock(&data->update_lock); \
		return -ENXIO; \
	} \
} while (0)

static ssize_t show_psu(struct device *dev, struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct platform_device *pdev = dev_get_drvdata(dev);
	unsigned char pid = pdev->id;
	int value = 0;
	int error = 0;

	mutex_lock(&data->update_lock);
	as1817_64o_psu_update(pid);
	if (!data->valid[pid]) {
		error = -EIO;
		goto exit;
	}

	switch (attr->index) {
	case PSU_ATTR_PRESENT:
		value = (data->ipmi_resp[pid].status[PSU_PRESENT] == 1) ? 1 : 0;
		break;
	case PSU_ATTR_POWER_GOOD:
		VALIDATE_PRESENT_RETURN(pid);
		value = data->ipmi_resp[pid].status[PSU_POWER_GOOD_PMBUS];
		break;
	case PSU_ATTR_VIN:
		VALIDATE_PRESENT_RETURN(pid);
		value = (u32)data->ipmi_resp[pid].status[PSU_VIN0] |
			(u32)data->ipmi_resp[pid].status[PSU_VIN1] << 8 |
			(u32)data->ipmi_resp[pid].status[PSU_VIN2] << 16;
		break;
	case PSU_ATTR_VOUT:
		VALIDATE_PRESENT_RETURN(pid);
		value = (u32)data->ipmi_resp[pid].status[PSU_VOUT0] |
			(u32)data->ipmi_resp[pid].status[PSU_VOUT1] << 8 |
			(u32)data->ipmi_resp[pid].status[PSU_VOUT2] << 16;
		break;
	case PSU_ATTR_IIN:
		VALIDATE_PRESENT_RETURN(pid);
		value = (u32)data->ipmi_resp[pid].status[PSU_IIN0] |
			(u32)data->ipmi_resp[pid].status[PSU_IIN1] << 8 |
			(u32)data->ipmi_resp[pid].status[PSU_IIN2] << 16;
		break;
	case PSU_ATTR_IOUT:
		VALIDATE_PRESENT_RETURN(pid);
		value = (u32)data->ipmi_resp[pid].status[PSU_IOUT0] |
			(u32)data->ipmi_resp[pid].status[PSU_IOUT1] << 8 |
			(u32)data->ipmi_resp[pid].status[PSU_IOUT2] << 16;
		break;
	case PSU_ATTR_PIN:
		VALIDATE_PRESENT_RETURN(pid);
		value = (u32)data->ipmi_resp[pid].status[PSU_PIN0] |
			(u32)data->ipmi_resp[pid].status[PSU_PIN1] << 8 |
			(u32)data->ipmi_resp[pid].status[PSU_PIN2] << 16 |
			(u32)data->ipmi_resp[pid].status[PSU_PIN3] << 24;
		value /= 1000; /* microWatt to milliWatt */
		break;
	case PSU_ATTR_POUT:
		VALIDATE_PRESENT_RETURN(pid);
		value = (u32)data->ipmi_resp[pid].status[PSU_POUT0] |
			(u32)data->ipmi_resp[pid].status[PSU_POUT1] << 8 |
			(u32)data->ipmi_resp[pid].status[PSU_POUT2] << 16 |
			(u32)data->ipmi_resp[pid].status[PSU_POUT3] << 24;
		value /= 1000; /* microWatt to milliWatt */
		break;
	case PSU_ATTR_TEMP1_INPUT:
		VALIDATE_PRESENT_RETURN(pid);
		value = (s16)((u16)data->ipmi_resp[pid].status[PSU_TEMP1_0] |
			      (u16)data->ipmi_resp[pid].status[PSU_TEMP1_1] << 8);
		value *= 1000; /* to millidegree */
		break;
	case PSU_ATTR_TEMP2_INPUT:
		VALIDATE_PRESENT_RETURN(pid);
		value = (s16)((u16)data->ipmi_resp[pid].status[PSU_TEMP2_0] |
			      (u16)data->ipmi_resp[pid].status[PSU_TEMP2_1] << 8);
		value *= 1000;
		break;
	case PSU_ATTR_TEMP3_INPUT:
		VALIDATE_PRESENT_RETURN(pid);
		value = (s16)((u16)data->ipmi_resp[pid].status[PSU_TEMP3_0] |
			      (u16)data->ipmi_resp[pid].status[PSU_TEMP3_1] << 8);
		value *= 1000;
		break;
	case PSU_ATTR_FAN_INPUT:
		VALIDATE_PRESENT_RETURN(pid);
		value = (u32)data->ipmi_resp[pid].status[PSU_FAN0] |
			(u32)data->ipmi_resp[pid].status[PSU_FAN1] << 8;
		break;
	case PSU_ATTR_FAN_MAX_SPEED:
		value = 35000;
		break;
	case PSU_ATTR_FAN_PERCENTAGE:
		VALIDATE_PRESENT_RETURN(pid);
		value = (u32)data->ipmi_resp[pid].status[PSU_FAN0] |
			(u32)data->ipmi_resp[pid].status[PSU_FAN1] << 8;
		value = DIV_ROUND_CLOSEST(value * 100, 35000);
		if (value > 100)
			value = 100;
		break;
	case PSU_ATTR_TYPE:
		VALIDATE_PRESENT_RETURN(pid);
		mutex_unlock(&data->update_lock);
		return sprintf(buf, "%s\n",
			       data->ipmi_resp[pid].info[PSU_INFO_TYPE] ? "AC" : "DC");
	case PSU_ATTR_TEMP1_HIGH_CRIT:
		VALIDATE_PRESENT_RETURN(pid);
		value = (data->ipmi_resp[pid].info[PSU_INFO_TEMP1_HCRIT] |
			 data->ipmi_resp[pid].info[PSU_INFO_TEMP1_HCRIT + 1] << 8) * 1000;
		break;
	case PSU_ATTR_TEMP1_LOW_CRIT:
		VALIDATE_PRESENT_RETURN(pid);
		value = (data->ipmi_resp[pid].info[PSU_INFO_TEMP1_LCRIT] |
			 data->ipmi_resp[pid].info[PSU_INFO_TEMP1_LCRIT + 1] << 8) * 1000;
		break;
	case PSU_ATTR_TEMP2_HIGH_CRIT:
		VALIDATE_PRESENT_RETURN(pid);
		value = (data->ipmi_resp[pid].info[PSU_INFO_TEMP2_HCRIT] |
			 data->ipmi_resp[pid].info[PSU_INFO_TEMP2_HCRIT + 1] << 8) * 1000;
		break;
	case PSU_ATTR_TEMP2_LOW_CRIT:
		VALIDATE_PRESENT_RETURN(pid);
		value = (data->ipmi_resp[pid].info[PSU_INFO_TEMP2_LCRIT] |
			 data->ipmi_resp[pid].info[PSU_INFO_TEMP2_LCRIT + 1] << 8) * 1000;
		break;
	case PSU_ATTR_TEMP3_HIGH_CRIT:
		VALIDATE_PRESENT_RETURN(pid);
		value = (data->ipmi_resp[pid].info[PSU_INFO_TEMP3_HCRIT] |
			 data->ipmi_resp[pid].info[PSU_INFO_TEMP3_HCRIT + 1] << 8) * 1000;
		break;
	case PSU_ATTR_TEMP3_LOW_CRIT:
		VALIDATE_PRESENT_RETURN(pid);
		value = (data->ipmi_resp[pid].info[PSU_INFO_TEMP3_LCRIT] |
			 data->ipmi_resp[pid].info[PSU_INFO_TEMP3_LCRIT + 1] << 8) * 1000;
		break;
	case PSU_ATTR_VIN_HIGH_CRIT:
		VALIDATE_PRESENT_RETURN(pid);
		value = data->ipmi_resp[pid].info[PSU_INFO_VIN_HCRIT] |
			data->ipmi_resp[pid].info[PSU_INFO_VIN_HCRIT + 1] << 8 |
			data->ipmi_resp[pid].info[PSU_INFO_VIN_HCRIT + 2] << 16;
		break;
	case PSU_ATTR_VIN_LOW_CRIT:
		VALIDATE_PRESENT_RETURN(pid);
		value = data->ipmi_resp[pid].info[PSU_INFO_VIN_LCRIT] |
			data->ipmi_resp[pid].info[PSU_INFO_VIN_LCRIT + 1] << 8 |
			data->ipmi_resp[pid].info[PSU_INFO_VIN_LCRIT + 2] << 16;
		break;
	case PSU_ATTR_VIN_HIGH_WARN:
		VALIDATE_PRESENT_RETURN(pid);
		value = data->ipmi_resp[pid].info[PSU_INFO_VIN_HWARN] |
			data->ipmi_resp[pid].info[PSU_INFO_VIN_HWARN + 1] << 8 |
			data->ipmi_resp[pid].info[PSU_INFO_VIN_HWARN + 2] << 16;
		break;
	case PSU_ATTR_VIN_LOW_WARN:
		VALIDATE_PRESENT_RETURN(pid);
		value = data->ipmi_resp[pid].info[PSU_INFO_VIN_LWARN] |
			data->ipmi_resp[pid].info[PSU_INFO_VIN_LWARN + 1] << 8 |
			data->ipmi_resp[pid].info[PSU_INFO_VIN_LWARN + 2] << 16;
		break;
	case PSU_ATTR_VOUT_HIGH_CRIT:
		VALIDATE_PRESENT_RETURN(pid);
		value = data->ipmi_resp[pid].info[PSU_INFO_VOUT_HCRIT] |
			data->ipmi_resp[pid].info[PSU_INFO_VOUT_HCRIT + 1] << 8 |
			data->ipmi_resp[pid].info[PSU_INFO_VOUT_HCRIT + 2] << 16;
		break;
	case PSU_ATTR_VOUT_LOW_CRIT:
		VALIDATE_PRESENT_RETURN(pid);
		value = data->ipmi_resp[pid].info[PSU_INFO_VOUT_LCRIT] |
			data->ipmi_resp[pid].info[PSU_INFO_VOUT_LCRIT + 1] << 8 |
			data->ipmi_resp[pid].info[PSU_INFO_VOUT_LCRIT + 2] << 16;
		break;
	case PSU_ATTR_IIN_HIGH_CRIT:
		VALIDATE_PRESENT_RETURN(pid);
		value = data->ipmi_resp[pid].info[PSU_INFO_IIN_HCRIT] |
			data->ipmi_resp[pid].info[PSU_INFO_IIN_HCRIT + 1] << 8 |
			data->ipmi_resp[pid].info[PSU_INFO_IIN_HCRIT + 2] << 16;
		break;
	case PSU_ATTR_IOUT_HIGH_CRIT:
		VALIDATE_PRESENT_RETURN(pid);
		value = data->ipmi_resp[pid].info[PSU_INFO_IOUT_HCRIT] |
			data->ipmi_resp[pid].info[PSU_INFO_IOUT_HCRIT + 1] << 8 |
			data->ipmi_resp[pid].info[PSU_INFO_IOUT_HCRIT + 2] << 16;
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

static ssize_t show_string(struct device *dev, struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct platform_device *pdev = dev_get_drvdata(dev);
	unsigned char pid = pdev->id;
	char *str = NULL;
	int error = 0;

	mutex_lock(&data->update_lock);
	as1817_64o_psu_update(pid);
	if (!data->valid[pid]) {
		error = -EIO;
		goto exit;
	}

	VALIDATE_PRESENT_RETURN(pid);

	switch (attr->index) {
	case PSU_ATTR_MODEL:
		str = data->ipmi_resp[pid].model;
		break;
	case PSU_ATTR_SERIAL:
		str = data->ipmi_resp[pid].serial;
		break;
	case PSU_ATTR_FAN_DIR:
		str = data->ipmi_resp[pid].fandir;
		break;
	default:
		error = -EINVAL;
		goto exit;
	}

	mutex_unlock(&data->update_lock);
	return sprintf(buf, "%s\n", str);

exit:
	mutex_unlock(&data->update_lock);
	return error;
}

static int as1817_64o_psu_probe(struct platform_device *pdev)
{
	struct device *hwmon_dev;

	hwmon_dev = hwmon_device_register_with_groups(&pdev->dev, DRVNAME,
						     pdev, as1817_64o_psu_groups);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	data->hwmon_dev[pdev->id] = hwmon_dev;
	dev_info(&pdev->dev, "PSU%d device created\n", pdev->id + 1);
	return 0;
}

static void as1817_64o_psu_remove(struct platform_device *pdev)
{
	if (data->hwmon_dev[pdev->id]) {
		hwmon_device_unregister(data->hwmon_dev[pdev->id]);
		data->hwmon_dev[pdev->id] = NULL;
	}
}

static int __init as1817_64o_psu_init(void)
{
	int ret, i;

	data = kzalloc(sizeof(struct as1817_64o_psu_data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto alloc_err;
	}

	mutex_init(&data->update_lock);

	ret = platform_driver_register(&as1817_64o_psu_driver);
	if (ret < 0)
		goto dri_reg_err;

	ret = init_ipmi_data(&data->ipmi, 0);
	if (ret)
		goto ipmi_err;

	for (i = 0; i < NUM_OF_PSU; i++) {
		data->pdev[i] = platform_device_register_simple(DRVNAME, i, NULL, 0);
		if (IS_ERR(data->pdev[i])) {
			ret = PTR_ERR(data->pdev[i]);
			goto dev_reg_err;
		}
	}

	return 0;

dev_reg_err:
	while (--i >= 0)
		platform_device_unregister(data->pdev[i]);
	ipmi_destroy_user(data->ipmi.user);
ipmi_err:
	platform_driver_unregister(&as1817_64o_psu_driver);
dri_reg_err:
	kfree(data);
alloc_err:
	return ret;
}

static void __exit as1817_64o_psu_exit(void)
{
	int i;

	for (i = 0; i < NUM_OF_PSU; i++)
		platform_device_unregister(data->pdev[i]);
	platform_driver_unregister(&as1817_64o_psu_driver);
	ipmi_destroy_user(data->ipmi.user);
	kfree(data);
}

MODULE_AUTHOR("rayx_huang <rayx_huang@edge-core.com>");
MODULE_DESCRIPTION("as1817_64o_psu driver");
MODULE_LICENSE("GPL");

module_init(as1817_64o_psu_init);
module_exit(as1817_64o_psu_exit);
