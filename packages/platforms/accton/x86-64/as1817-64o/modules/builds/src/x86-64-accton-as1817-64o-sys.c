/*
 * A sys driver for accton as1817_64o System EEPROM
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
#include <linux/hwmon-sysfs.h>
#include <linux/ipmi.h>
#include <linux/ipmi_smi.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>

#define DRVNAME "as1817_64o_sys"
#define ACCTON_IPMI_NETFN 0x34

#define IPMI_TIMEOUT (5 * HZ)
#define IPMI_ERR_RETRY_TIMES 1
#define IPMI_READ_MAX_LEN 128

#define EEPROM_NAME "eeprom"
#define EEPROM_SIZE 256

#define IPMI_SYSEEPROM_READ_CMD 0x18
#define IPMI_CPLD_VERSION_CMD   0x20

enum cpld_version_id {
    CPU_CPLD_VERSION,
    FPGA_VERSION,
    FAN_CPLD_VERSION,
    DCSCM_CPLD_VERSION,
    SYS_CPLD_VERSION,
    CPLD1_VERSION,
    CPLD2_VERSION,
    NUM_CPLD_VERSIONS,
};

static const unsigned char cpld_addr[] = {
    [CPU_CPLD_VERSION]   = 0x21,
    [FPGA_VERSION]       = 0x60,
    [FAN_CPLD_VERSION]   = 0x33,
    [DCSCM_CPLD_VERSION] = 0x06,
    [SYS_CPLD_VERSION]   = 0x61,
    [CPLD1_VERSION]      = 0x64,
    [CPLD2_VERSION]      = 0x65,
};

static void ipmi_msg_handler(struct ipmi_recv_msg *msg, void *user_msg_data);
static int as1817_64o_sys_probe(struct platform_device *pdev);
static void as1817_64o_sys_remove(struct platform_device *pdev);

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

struct as1817_64o_sys_data {
	struct platform_device *pdev;
	struct mutex update_lock;
	struct ipmi_data ipmi;
	unsigned char ipmi_resp_eeprom[EEPROM_SIZE];
	unsigned char ipmi_tx_data[2];
	struct bin_attribute eeprom;
};

static struct as1817_64o_sys_data *data = NULL;

static struct platform_driver as1817_64o_sys_driver = {
	.probe = as1817_64o_sys_probe,
	.remove = as1817_64o_sys_remove,
	.driver = {
		.name = DRVNAME,
		.owner = THIS_MODULE,
	},
};

/* IPMI interface functions */
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

/* EEPROM read via IPMI OEM command 0x18 */
static ssize_t show_version(struct device *dev, struct device_attribute *da,
			    char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	unsigned char resp[2] = {0};
	unsigned char tx_data;
	int status;

	mutex_lock(&data->update_lock);
	tx_data = cpld_addr[attr->index];
	status = ipmi_send_message(&data->ipmi, IPMI_CPLD_VERSION_CMD,
				   &tx_data, 1, resp, sizeof(resp));
	mutex_unlock(&data->update_lock);

	if (status != 0 || data->ipmi.rx_result != 0)
		return -EIO;

	return sprintf(buf, "%x.%x\n", resp[0], resp[1]);
}

static SENSOR_DEVICE_ATTR(cpu_cpld_version, S_IRUGO, show_version, NULL, CPU_CPLD_VERSION);
static SENSOR_DEVICE_ATTR(fpga_version, S_IRUGO, show_version, NULL, FPGA_VERSION);
static SENSOR_DEVICE_ATTR(fan_cpld_version, S_IRUGO, show_version, NULL, FAN_CPLD_VERSION);
static SENSOR_DEVICE_ATTR(dcscm_cpld_version, S_IRUGO, show_version, NULL, DCSCM_CPLD_VERSION);
static SENSOR_DEVICE_ATTR(sys_cpld_version, S_IRUGO, show_version, NULL, SYS_CPLD_VERSION);
static SENSOR_DEVICE_ATTR(cpld1_version, S_IRUGO, show_version, NULL, CPLD1_VERSION);
static SENSOR_DEVICE_ATTR(cpld2_version, S_IRUGO, show_version, NULL, CPLD2_VERSION);

static struct attribute *as1817_64o_sys_attrs[] = {
	&sensor_dev_attr_cpu_cpld_version.dev_attr.attr,
	&sensor_dev_attr_fpga_version.dev_attr.attr,
	&sensor_dev_attr_fan_cpld_version.dev_attr.attr,
	&sensor_dev_attr_dcscm_cpld_version.dev_attr.attr,
	&sensor_dev_attr_sys_cpld_version.dev_attr.attr,
	&sensor_dev_attr_cpld1_version.dev_attr.attr,
	&sensor_dev_attr_cpld2_version.dev_attr.attr,
	NULL,
};

static const struct attribute_group as1817_64o_sys_group = {
	.attrs = as1817_64o_sys_attrs,
};

/* EEPROM read via IPMI OEM command 0x18 */
static ssize_t sys_eeprom_read(loff_t off, char *buf, size_t count)
{
	int status = 0;
	unsigned char length;

	if ((off + count) > EEPROM_SIZE)
		return -EINVAL;

	length = (count >= IPMI_READ_MAX_LEN) ? IPMI_READ_MAX_LEN : count;
	data->ipmi_tx_data[0] = (off & 0xff);
	data->ipmi_tx_data[1] = length;
	status = ipmi_send_message(&data->ipmi, IPMI_SYSEEPROM_READ_CMD,
				   data->ipmi_tx_data, sizeof(data->ipmi_tx_data),
				   data->ipmi_resp_eeprom + off, length);
	if (unlikely(status != 0))
		return status;

	if (unlikely(data->ipmi.rx_result != 0))
		return -EIO;

	memcpy(buf, data->ipmi_resp_eeprom + off, length);
	return length;
}

static ssize_t sysfs_bin_read(struct file *filp, struct kobject *kobj,
			      struct bin_attribute *attr,
			      char *buf, loff_t off, size_t count)
{
	ssize_t retval = 0;

	if (unlikely(!count))
		return count;

	mutex_lock(&data->update_lock);

	while (count) {
		ssize_t status;

		status = sys_eeprom_read(off, buf, count);
		if (status <= 0) {
			if (retval == 0)
				retval = status;
			break;
		}

		buf += status;
		off += status;
		count -= status;
		retval += status;
	}

	mutex_unlock(&data->update_lock);
	return retval;
}

static int sysfs_eeprom_init(struct kobject *kobj, struct bin_attribute *eeprom)
{
	sysfs_bin_attr_init(eeprom);
	eeprom->attr.name = EEPROM_NAME;
	eeprom->attr.mode = S_IRUGO;
	eeprom->read = sysfs_bin_read;
	eeprom->size = EEPROM_SIZE;
	eeprom->write = NULL;

	return sysfs_create_bin_file(kobj, eeprom);
}

static void sysfs_eeprom_cleanup(struct kobject *kobj,
				 struct bin_attribute *eeprom)
{
	sysfs_remove_bin_file(kobj, eeprom);
}

static int as1817_64o_sys_probe(struct platform_device *pdev)
{
	int status;

	status = sysfs_create_group(&pdev->dev.kobj, &as1817_64o_sys_group);
	if (status)
		return status;

	status = sysfs_eeprom_init(&pdev->dev.kobj, &data->eeprom);
	if (status) {
		sysfs_remove_group(&pdev->dev.kobj, &as1817_64o_sys_group);
		return status;
	}

	dev_info(&pdev->dev, "device created\n");
	return 0;
}

static void as1817_64o_sys_remove(struct platform_device *pdev)
{
	sysfs_eeprom_cleanup(&pdev->dev.kobj, &data->eeprom);
	sysfs_remove_group(&pdev->dev.kobj, &as1817_64o_sys_group);
}

static int __init as1817_64o_sys_init(void)
{
	int ret;

	data = kzalloc(sizeof(struct as1817_64o_sys_data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto alloc_err;
	}

	mutex_init(&data->update_lock);

	ret = platform_driver_register(&as1817_64o_sys_driver);
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
	platform_driver_unregister(&as1817_64o_sys_driver);
dri_reg_err:
	kfree(data);
alloc_err:
	return ret;
}

static void __exit as1817_64o_sys_exit(void)
{
	platform_device_unregister(data->pdev);
	platform_driver_unregister(&as1817_64o_sys_driver);
	ipmi_destroy_user(data->ipmi.user);
	kfree(data);
}

MODULE_AUTHOR("rayx_huang <rayx_huang@edge-core.com>");
MODULE_DESCRIPTION("as1817_64o_sys driver");
MODULE_LICENSE("GPL");

module_init(as1817_64o_sys_init);
module_exit(as1817_64o_sys_exit);
