/*
 * Copyright (C)  Ray Huang <rayx_huang@edge-core.com>
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
#include <linux/moduleparam.h>
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

#define DRVNAME                 "as7327_56x_psu_bmc"
#define IPMI_NETFN              0x36
#define IPMI_TIMEOUT            (5 * HZ)
#define IPMI_ERR_RETRY_TIMES    1
#define IPMI_MFR_LEN            32

#define IPMI_GET_PSU_ALARM_DATA_CMD      0x18
#define IPMI_GET_PSU_ATTRIBUTE_DATA_CMD  0x1A
#define IPMI_PSU_GET_EEPROM_CMD          0xA1

#define PSU_MAX_FAN_SPEED   23000

#define IS_PRESENT(id, value)       (!(value & BIT(7 - id)))
#define VALIDATE_PRESENT_RETURN(id) \
do { \
    if (data->psu_data[id].present == 0) { \
        mutex_unlock(&data->update_lock);   \
        return -ENXIO; \
    } \
} while (0)

#define VALIDATE_IPMI_REQUEST_RV(status, addr, cmd) \
do { \
    if (unlikely(status != 0))                 \
        dev_err(&data->pdev[pid]->dev, "ipmi request (0x%x) err (0x%x)", addr, cmd); \
    if (unlikely(data->ipmi.rx_result != 0))   \
        dev_err(&data->pdev[pid]->dev, "ipmi request (0x%x) failed (0x%x)", addr, cmd); \
} while (0)


enum psu_eeprom_read_type
{
    eeprom_psu_product_number    = 0x01,
    eeprom_psu_serial_number     = 0x02,
    eeprom_psu_vendor            = 0x03,
    eeprom_psu_model_name        = 0x04,
    eeprom_psu_hardware_revision = 0x05,
    eeprom_psu_date              = 0x06,
};

enum ipmi_app_power_attribute_type
{
    POWER_GOOD           = 0x02,
    POWER_STATUS         = 0x03,
    POWER_CURRENT_LIMIT  = 0x04,
    POWER_TEMP_LIMIT     = 0x05,
    POWER_INPUT_VOLTAGE  = 0x0E,
    POWER_OUTPUT_VOLTAGE = 0x0F,
    POWER_OUTPUT_CURRENT = 0x10,
    POWER_TEMPERATURE    = 0x11,
    POWER_FAN_SPEED      = 0x12,
    POWER_OUTPUT_POWER   = 0x13,
    POWER_INPUT_POWER    = 0x14,
    POWER_HARDWARE_VER   = 0x21,
    POWER_INPUT_CURRENT  = 0x23,
    POWER_TEMPERATURE_1  = 0x24,
    POWER_TEMPERATURE_2  = 0x25,
    POWER_TEMPERATURE_3  = 0x26,
    POWER_POUT_MAX       = 0x27,
    POWER_VIN_TYPE       = 0x28,
    POWER_LED_STATUS     = 0x29,
};

enum ipmi_app_power_alarm_type
{
    POWER_ALARM_CMD            = 0x1,
    POWER_ALARM_TEMPERATURE    = 0x2,
    POWER_ALARM_VIN_UV_FAULT   = 0x3,
    POWER_ALARM_IOUT_OC_FAULT  = 0x4,
    POWER_ALARM_VOUT_OV_FAULT  = 0x5,
    POWER_ALARM_VIN_OV_DETECT  = 0x6,
    POWER_ALARM_VIN_UNIT_OFF   = 0x7,
    POWER_ALARM_OTHER          = 0x9,
    POWER_ALARM_FAN            = 0xA,
    POWER_ALARM_INPUT          = 0xD,
    POWER_ALARM_IOUT_POUT      = 0xE,
    POWER_ALARM_VOUT           = 0xF,
};

#define PSU_STATUS_NONE_ABOVE   BIT(0)
#define PSU_STATUS_CML          BIT(1)
#define PSU_STATUS_TEMPERATURE  BIT(2)
#define PSU_STATUS_VIN_UV       BIT(3)
#define PSU_STATUS_IOUT_OC      BIT(4)
#define PSU_STATUS_VOUT_OV      BIT(5)
#define PSU_STATUS_OFF          BIT(6)
#define PSU_STATUS_BUSY         BIT(7)
#define PSU_STATUS_UNKNOWN      BIT(8)
#define PSU_STATUS_OTHER        BIT(9)
#define PSU_STATUS_FANS         BIT(10)
#define PSU_STATUS_POWER_GOOD_N BIT(11)
#define PSU_STATUS_WORD_MFR     BIT(12)
#define PSU_STATUS_INPUT        BIT(13)
#define PSU_STATUS_IOUT_POUT    BIT(14)
#define PSU_STATUS_VOUT         BIT(15)

extern int as7327_56x_cpld_read(unsigned short cpld_addr, u8 reg);

static void ipmi_msg_handler(struct ipmi_recv_msg *msg, void *user_msg_data);
static ssize_t show_present(struct device *dev, struct device_attribute *da,
                            char *buf);
static ssize_t show_psu_alarm(struct device *dev, struct device_attribute *da,
                            char *buf);
static ssize_t show_psu_fan(struct device *dev, struct device_attribute *da,
                            char *buf);
static ssize_t show_psu_status(struct device *dev, struct device_attribute *da,
                            char *buf);
static ssize_t show_psu_mfr(struct device *dev, struct device_attribute *da,
                            char *buf);
static int as7327_56x_psu_probe(struct platform_device *pdev);
static void as7327_56x_psu_remove(struct platform_device *pdev);
static int as7327_56x_psu_update_device(struct device *dev, unsigned char pid);

enum psu_id {
    PSU_1,
    PSU_2,
    NUM_OF_PSU
};

struct ipmi_data {
    struct completion read_complete;
    struct ipmi_addr address;
    struct ipmi_user * user;
    int interface;

    struct kernel_ipmi_msg tx_message;
    long tx_msgid;

    void *rx_msg_data;
    unsigned short rx_msg_len;
    unsigned char rx_result;
    int rx_recv_type;

    struct ipmi_user_hndl ipmi_hndlrs;
};

struct psu_data_s {
    char mfr_pn[IPMI_MFR_LEN+1];
    char mfr_sn[IPMI_MFR_LEN+1];
    char mfr_id[IPMI_MFR_LEN+1];
    char mfr_model[IPMI_MFR_LEN+1];
    char mfr_hwrev[IPMI_MFR_LEN+1];
    char mfr_date[IPMI_MFR_LEN+1];
    unsigned char status[4];
    unsigned char vin[4];
    unsigned char vout[4];
    unsigned char iin[4];
    unsigned char iout[4];
    unsigned char pin[4];
    unsigned char pout[4];
    unsigned char fan_speed[4];
    unsigned char temp[3][4];   /* 3 temp sensors */
    unsigned char present;
};

struct as7327_56x_psu_data {
    struct platform_device *pdev[2];
    struct device   *hwmon_dev[2];
    struct mutex update_lock;
    char valid[2]; /* != 0 if registers are valid, 0: PSU1, 1: PSU2 */
    unsigned long last_updated[2];    /* In jiffies, 0: PSU1, 1: PSU2 */
    struct ipmi_data ipmi;
    char ipmi_resp[30];
    unsigned char ipmi_tx_data[3];
    struct psu_data_s psu_data[2];
};

struct as7327_56x_psu_data *data = NULL;

static struct platform_driver as7327_56x_psu_driver = {
    .probe = as7327_56x_psu_probe,
    .remove = as7327_56x_psu_remove,
    .driver = {
        .name = DRVNAME,
        .owner = THIS_MODULE,
    },
};

enum as7327_56x_psu_sysfs_attrs {
    /* psu attributes */
    PSU_PRESENT,
    PSU_POWER_GOOD,
    PSU_POWER_ON,
    PSU_TEMP_FAULT,
    PSU_FAN_FAULT,
    PSU_OVR_TEMP,
    PSU_PN,
    PSU_SN,
    PSU_ID,
    PSU_MODEL,
    PSU_REVISION,
    PSU_DATE,
    PSU_VIN,
    PSU_VOUT,
    PSU_IIN,
    PSU_IOUT,
    PSU_PIN,
    PSU_POUT,
    PSU_TEMP1_INPUT,
    PSU_TEMP2_INPUT,
    PSU_TEMP3_INPUT,
    PSU_FAN_SPEED,
    PSU_FAN_DUTY_CYCLE,
    PSU_FAN_DIRECTION,
    PSU_POUT_MAX,
    PSU_VIN_TYPE,
    NUM_OF_PSU_ATTR
};

/* psu attributes */
static SENSOR_DEVICE_ATTR(psu_present,         S_IRUGO, show_present,    NULL, PSU_PRESENT);
static SENSOR_DEVICE_ATTR(psu_power_good,      S_IRUGO, show_psu_alarm,  NULL, PSU_POWER_GOOD);
static SENSOR_DEVICE_ATTR(psu_power_on,        S_IRUGO, show_psu_alarm,  NULL, PSU_POWER_ON);
static SENSOR_DEVICE_ATTR(psu_temp_fault,      S_IRUGO, show_psu_alarm,  NULL, PSU_TEMP_FAULT);
static SENSOR_DEVICE_ATTR(psu_ovr_temp,        S_IRUGO, show_psu_alarm,  NULL, PSU_OVR_TEMP);
static SENSOR_DEVICE_ATTR(psu_fan1_fault,      S_IRUGO, show_psu_alarm,  NULL, PSU_FAN_FAULT);
static SENSOR_DEVICE_ATTR(psu_product_number,  S_IRUGO, show_psu_mfr,    NULL, PSU_PN);
static SENSOR_DEVICE_ATTR(psu_serial_number,   S_IRUGO, show_psu_mfr,    NULL, PSU_SN);
static SENSOR_DEVICE_ATTR(psu_id,              S_IRUGO, show_psu_mfr,    NULL, PSU_ID);
static SENSOR_DEVICE_ATTR(psu_model_name,      S_IRUGO, show_psu_mfr,    NULL, PSU_MODEL);
static SENSOR_DEVICE_ATTR(psu_revision,        S_IRUGO, show_psu_mfr,    NULL, PSU_REVISION);
static SENSOR_DEVICE_ATTR(psu_date,            S_IRUGO, show_psu_mfr,    NULL, PSU_DATE);
static SENSOR_DEVICE_ATTR(psu_vin_type,        S_IRUGO, show_psu_mfr,    NULL, PSU_VIN_TYPE);
static SENSOR_DEVICE_ATTR(psu_vin,             S_IRUGO, show_psu_status, NULL, PSU_VIN);
static SENSOR_DEVICE_ATTR(psu_vout,            S_IRUGO, show_psu_status, NULL, PSU_VOUT);
static SENSOR_DEVICE_ATTR(psu_iin,             S_IRUGO, show_psu_status, NULL, PSU_IIN);
static SENSOR_DEVICE_ATTR(psu_iout,            S_IRUGO, show_psu_status, NULL, PSU_IOUT);
static SENSOR_DEVICE_ATTR(psu_temp1_input,     S_IRUGO, show_psu_status, NULL, PSU_TEMP1_INPUT);
static SENSOR_DEVICE_ATTR(psu_temp2_input,     S_IRUGO, show_psu_status, NULL, PSU_TEMP2_INPUT);
static SENSOR_DEVICE_ATTR(psu_temp3_input,     S_IRUGO, show_psu_status, NULL, PSU_TEMP3_INPUT);
static SENSOR_DEVICE_ATTR(psu_pout_max,        S_IRUGO, show_psu_status, NULL, PSU_POUT_MAX);
static SENSOR_DEVICE_ATTR(psu_pin,             S_IRUGO, show_psu_status, NULL, PSU_PIN);
static SENSOR_DEVICE_ATTR(psu_pout,            S_IRUGO, show_psu_status, NULL, PSU_POUT);
static SENSOR_DEVICE_ATTR(psu_fan1_speed,      S_IRUGO, show_psu_fan,    NULL, PSU_FAN_SPEED);
static SENSOR_DEVICE_ATTR(psu_fan1_duty_cycle, S_IRUGO, show_psu_fan,    NULL, PSU_FAN_DUTY_CYCLE);
static SENSOR_DEVICE_ATTR(psu_fan1_dir,        S_IRUGO, show_psu_fan,    NULL, PSU_FAN_DIRECTION);

#define DECLARE_PSU_ATTR() \
    &sensor_dev_attr_psu_present.dev_attr.attr, \
    &sensor_dev_attr_psu_power_good.dev_attr.attr, \
    &sensor_dev_attr_psu_power_on.dev_attr.attr, \
    &sensor_dev_attr_psu_temp_fault.dev_attr.attr, \
    &sensor_dev_attr_psu_ovr_temp.dev_attr.attr, \
    &sensor_dev_attr_psu_fan1_fault.dev_attr.attr, \
    &sensor_dev_attr_psu_product_number.dev_attr.attr, \
    &sensor_dev_attr_psu_serial_number.dev_attr.attr, \
    &sensor_dev_attr_psu_id.dev_attr.attr, \
    &sensor_dev_attr_psu_model_name.dev_attr.attr, \
    &sensor_dev_attr_psu_revision.dev_attr.attr, \
    &sensor_dev_attr_psu_date.dev_attr.attr, \
    &sensor_dev_attr_psu_vin_type.dev_attr.attr, \
    &sensor_dev_attr_psu_vin.dev_attr.attr, \
    &sensor_dev_attr_psu_vout.dev_attr.attr, \
    &sensor_dev_attr_psu_iin.dev_attr.attr, \
    &sensor_dev_attr_psu_iout.dev_attr.attr, \
    &sensor_dev_attr_psu_pin.dev_attr.attr, \
    &sensor_dev_attr_psu_pout.dev_attr.attr, \
    &sensor_dev_attr_psu_temp1_input.dev_attr.attr, \
    &sensor_dev_attr_psu_temp2_input.dev_attr.attr, \
    &sensor_dev_attr_psu_temp3_input.dev_attr.attr, \
    &sensor_dev_attr_psu_pout_max.dev_attr.attr, \
    &sensor_dev_attr_psu_fan1_speed.dev_attr.attr, \
    &sensor_dev_attr_psu_fan1_duty_cycle.dev_attr.attr, \
    &sensor_dev_attr_psu_fan1_dir.dev_attr.attr

static struct attribute *as7327_56x_psu_attrs[] = {
    /* psu attributes */
    DECLARE_PSU_ATTR(),
    NULL
};
static struct attribute_group as7327_56x_psu_group = {
    .attrs = as7327_56x_psu_attrs,
};

const struct attribute_group *as7327_56x_psu_groups[][2] = {
    {&as7327_56x_psu_group, NULL},
    {&as7327_56x_psu_group, NULL}
};

/* Functions to talk to the IPMI layer */

/* Initialize IPMI address, message buffers and user data */
static int init_ipmi_data(struct ipmi_data *ipmi, int iface)
{
    int err;

    init_completion(&ipmi->read_complete);

    /* Initialize IPMI address */
    ipmi->address.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
    ipmi->address.channel = IPMI_BMC_CHANNEL;
    ipmi->address.data[0] = 0;
    ipmi->interface = iface;

    /* Initialize message buffers */
    ipmi->tx_msgid = 0;
    ipmi->tx_message.netfn = IPMI_NETFN;

    ipmi->ipmi_hndlrs.ipmi_recv_hndl = ipmi_msg_handler;

    /* Create IPMI messaging interface user */
    err = ipmi_create_user(ipmi->interface, &ipmi->ipmi_hndlrs,
                   ipmi, &ipmi->user);
    if (err < 0) {
        pr_err("Unable to register user with IPMI "
            "interface %d\n", ipmi->interface);
        return -EACCES;
    }

    return 0;
}

/* Send an IPMI command */
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
        goto addr_err;

    ipmi->tx_msgid++;
    err = ipmi_request_settime(ipmi->user, &ipmi->address, ipmi->tx_msgid,
                   &ipmi->tx_message, ipmi, 0, 0, 0);
    if (err)
        goto ipmi_req_err;

    err = wait_for_completion_timeout(&ipmi->read_complete, IPMI_TIMEOUT);
    if (!err)
        goto ipmi_timeout_err;

    return 0;

ipmi_timeout_err:
    err = -ETIMEDOUT;
    pr_err("request_timeout=%x\n", err);
    return err;
ipmi_req_err:
    pr_err("request_settime=%x\n", err);
    return err;
addr_err:
    pr_err("validate_addr=%x\n", err);
    return err;
}

/* Send an IPMI command with retry */
static int ipmi_send_message(struct ipmi_data *ipmi, unsigned char cmd,
                                unsigned char *tx_data, unsigned short tx_len,
                                unsigned char *rx_data, unsigned short rx_len)
{
    int status = 0, retry = 0;

    for (retry = 0; retry <= IPMI_ERR_RETRY_TIMES; retry++) {
        status = _ipmi_send_message(ipmi, cmd, tx_data, tx_len, rx_data, rx_len);
        if (unlikely(status != 0)) {
            pr_err("ipmi_send_message_%d err status(%d)\r\n", retry, status);
            continue;
        }

        if (unlikely(ipmi->rx_result != 0)) {
            pr_err("ipmi_send_message_%d err result(%d)\r\n", retry, ipmi->rx_result);
            continue;
        }

        break;
    }

    return status;
}

/* Dispatch IPMI messages to callers */
static void ipmi_msg_handler(struct ipmi_recv_msg *msg, void *user_msg_data)
{
    unsigned short rx_len;
    struct ipmi_data *ipmi = user_msg_data;

    if (msg->msgid != ipmi->tx_msgid) {
        pr_notice("Mismatch between received msgid "
            "(%02x) and transmitted msgid (%02x)!\n",
            (int)msg->msgid,
            (int)ipmi->tx_msgid);
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
    } else
        ipmi->rx_msg_len = 0;

    ipmi_free_recv_msg(msg);
    complete(&ipmi->read_complete);
}

static int as7327_56x_psu_update_present(struct device *dev, unsigned char pid)
{
    int status = 0;
    static unsigned long last_updated = 0;

    if (time_before(jiffies, last_updated + HZ * 5))
        return status;

    status = as7327_56x_cpld_read(0x62, 0x1d);

    last_updated = jiffies;

    if (status < 0) {
        dev_warn(&data->pdev[pid]->dev, "cpld reg 0x62 err %d\n", status);
    }
    else {
        data->psu_data[PSU_1].present = IS_PRESENT(PSU_1, status);
        data->psu_data[PSU_2].present = IS_PRESENT(PSU_2, status);
    }
    return status;
}

static int as7327_56x_psu_update_device(struct device *dev, unsigned char pid)
{
    int status = 0;

    if (time_before(jiffies, data->last_updated[pid] + HZ + HZ / 2))
        goto exit;

    status = as7327_56x_psu_update_present(dev, pid);

    if (unlikely(status < 0)) {
        status = -EIO;
        goto exit;
    }

    if (data->psu_data[pid].present == 0) {
        status = -ENXIO;
        goto exit;
    }

    data->ipmi_tx_data[0] = pid + 1; /* PSU ID base id for ipmi start from 1 */

    /* get psu alarm */
    data->ipmi_tx_data[2] = POWER_STATUS;
    status = ipmi_send_message(&data->ipmi, IPMI_GET_PSU_ATTRIBUTE_DATA_CMD,
                                data->ipmi_tx_data, 3,
                                data->psu_data[pid].status,
                                sizeof(data->psu_data[pid].status));
    VALIDATE_IPMI_REQUEST_RV(status, IPMI_GET_PSU_ATTRIBUTE_DATA_CMD, data->ipmi_tx_data[2]);

    /* get psu vin */
    data->ipmi_tx_data[2] = POWER_INPUT_VOLTAGE;
    status = ipmi_send_message(&data->ipmi, IPMI_GET_PSU_ATTRIBUTE_DATA_CMD,
                                data->ipmi_tx_data, 3,
                                data->psu_data[pid].vin,
                                sizeof(data->psu_data[pid].vin));
    VALIDATE_IPMI_REQUEST_RV(status, IPMI_GET_PSU_ATTRIBUTE_DATA_CMD, data->ipmi_tx_data[2]);

    /* get psu vout */
    data->ipmi_tx_data[2] = POWER_OUTPUT_VOLTAGE;
    status = ipmi_send_message(&data->ipmi, IPMI_GET_PSU_ATTRIBUTE_DATA_CMD,
                                data->ipmi_tx_data, 3,
                                data->psu_data[pid].vout,
                                sizeof(data->psu_data[pid].vout));
    VALIDATE_IPMI_REQUEST_RV(status, IPMI_GET_PSU_ATTRIBUTE_DATA_CMD, data->ipmi_tx_data[2]);

    /* get psu iin */
    data->ipmi_tx_data[2] = POWER_INPUT_CURRENT;
    status = ipmi_send_message(&data->ipmi, IPMI_GET_PSU_ATTRIBUTE_DATA_CMD,
                                data->ipmi_tx_data, 3,
                                data->psu_data[pid].iin,
                                sizeof(data->psu_data[pid].iin));
    VALIDATE_IPMI_REQUEST_RV(status, IPMI_GET_PSU_ATTRIBUTE_DATA_CMD, data->ipmi_tx_data[2]);

    /* get psu iout */
    data->ipmi_tx_data[2] = POWER_OUTPUT_CURRENT;
    status = ipmi_send_message(&data->ipmi, IPMI_GET_PSU_ATTRIBUTE_DATA_CMD,
                                data->ipmi_tx_data, 3,
                                data->psu_data[pid].iout,
                                sizeof(data->psu_data[pid].iout));
    VALIDATE_IPMI_REQUEST_RV(status, IPMI_GET_PSU_ATTRIBUTE_DATA_CMD, data->ipmi_tx_data[2]);

    /* get psu pin */
    data->ipmi_tx_data[2] = POWER_INPUT_POWER;
    status = ipmi_send_message(&data->ipmi, IPMI_GET_PSU_ATTRIBUTE_DATA_CMD,
                                data->ipmi_tx_data, 3,
                                data->psu_data[pid].pin,
                                sizeof(data->psu_data[pid].pin));
    VALIDATE_IPMI_REQUEST_RV(status, IPMI_GET_PSU_ATTRIBUTE_DATA_CMD, data->ipmi_tx_data[2]);

    /* get psu pout */
    data->ipmi_tx_data[2] = POWER_OUTPUT_POWER;
    status = ipmi_send_message(&data->ipmi, IPMI_GET_PSU_ATTRIBUTE_DATA_CMD,
                                data->ipmi_tx_data, 3,
                                data->psu_data[pid].pout,
                                sizeof(data->psu_data[pid].pout));
    VALIDATE_IPMI_REQUEST_RV(status, IPMI_GET_PSU_ATTRIBUTE_DATA_CMD, data->ipmi_tx_data[2]);

    /* get psu temp1 */
    data->ipmi_tx_data[2] = POWER_TEMPERATURE_1;
    status = ipmi_send_message(&data->ipmi, IPMI_GET_PSU_ATTRIBUTE_DATA_CMD,
                                data->ipmi_tx_data, 3,
                                data->psu_data[pid].temp[0],
                                sizeof(data->psu_data[pid].temp[0]));
    VALIDATE_IPMI_REQUEST_RV(status, IPMI_GET_PSU_ATTRIBUTE_DATA_CMD, data->ipmi_tx_data[2]);

    /* get psu temp2 */
    data->ipmi_tx_data[2] = POWER_TEMPERATURE_2;
    status = ipmi_send_message(&data->ipmi, IPMI_GET_PSU_ATTRIBUTE_DATA_CMD,
                                data->ipmi_tx_data, 3,
                                data->psu_data[pid].temp[1],
                                sizeof(data->psu_data[pid].temp[1]));
    VALIDATE_IPMI_REQUEST_RV(status, IPMI_GET_PSU_ATTRIBUTE_DATA_CMD, data->ipmi_tx_data[2]);

    /* get psu temp3 */
    data->ipmi_tx_data[2] = POWER_TEMPERATURE_3;
    status = ipmi_send_message(&data->ipmi, IPMI_GET_PSU_ATTRIBUTE_DATA_CMD,
                                data->ipmi_tx_data, 3,
                                data->psu_data[pid].temp[2],
                                sizeof(data->psu_data[pid].temp[2]));
    VALIDATE_IPMI_REQUEST_RV(status, IPMI_GET_PSU_ATTRIBUTE_DATA_CMD, data->ipmi_tx_data[2]);

    /* get fan speed */
    data->ipmi_tx_data[2] = POWER_FAN_SPEED;
    status = ipmi_send_message(&data->ipmi, IPMI_GET_PSU_ATTRIBUTE_DATA_CMD,
                                data->ipmi_tx_data, 3,
                                data->psu_data[pid].fan_speed,
                                sizeof(data->psu_data[pid].fan_speed));
    VALIDATE_IPMI_REQUEST_RV(status, IPMI_GET_PSU_ATTRIBUTE_DATA_CMD, data->ipmi_tx_data[2]);

    /* get psu mfr */
    data->ipmi_tx_data[2] = eeprom_psu_product_number;
    status = ipmi_send_message(&data->ipmi, IPMI_PSU_GET_EEPROM_CMD,
                                data->ipmi_tx_data, 3,
                                data->psu_data[pid].mfr_pn,
                                sizeof(data->psu_data[pid].mfr_pn));
    VALIDATE_IPMI_REQUEST_RV(status, IPMI_PSU_GET_EEPROM_CMD, data->ipmi_tx_data[2]);

    data->ipmi_tx_data[2] = eeprom_psu_serial_number;
    status = ipmi_send_message(&data->ipmi, IPMI_PSU_GET_EEPROM_CMD,
                                data->ipmi_tx_data, 3,
                                data->psu_data[pid].mfr_sn,
                                sizeof(data->psu_data[pid].mfr_sn));
    VALIDATE_IPMI_REQUEST_RV(status, IPMI_PSU_GET_EEPROM_CMD, data->ipmi_tx_data[2]);

    data->ipmi_tx_data[2] = eeprom_psu_vendor;
    status = ipmi_send_message(&data->ipmi, IPMI_PSU_GET_EEPROM_CMD,
                                data->ipmi_tx_data, 3,
                                data->psu_data[pid].mfr_id,
                                sizeof(data->psu_data[pid].mfr_id));
    VALIDATE_IPMI_REQUEST_RV(status, IPMI_PSU_GET_EEPROM_CMD, data->ipmi_tx_data[2]);

    data->ipmi_tx_data[2] = eeprom_psu_model_name;
    status = ipmi_send_message(&data->ipmi, IPMI_PSU_GET_EEPROM_CMD,
                                data->ipmi_tx_data, 3,
                                data->psu_data[pid].mfr_model,
                                sizeof(data->psu_data[pid].mfr_model));
    VALIDATE_IPMI_REQUEST_RV(status, IPMI_PSU_GET_EEPROM_CMD, data->ipmi_tx_data[2]);

    data->ipmi_tx_data[2] = eeprom_psu_hardware_revision;
    status = ipmi_send_message(&data->ipmi, IPMI_PSU_GET_EEPROM_CMD,
                                data->ipmi_tx_data, 3,
                                data->psu_data[pid].mfr_hwrev,
                                sizeof(data->psu_data[pid].mfr_hwrev));
    VALIDATE_IPMI_REQUEST_RV(status, IPMI_PSU_GET_EEPROM_CMD, data->ipmi_tx_data[2]);

    data->ipmi_tx_data[2] = eeprom_psu_date;
    status = ipmi_send_message(&data->ipmi, IPMI_PSU_GET_EEPROM_CMD,
                                data->ipmi_tx_data, 3,
                                data->psu_data[pid].mfr_date,
                                sizeof(data->psu_data[pid].mfr_date));
    VALIDATE_IPMI_REQUEST_RV(status, IPMI_PSU_GET_EEPROM_CMD, data->ipmi_tx_data[2]);

    data->last_updated[pid] = jiffies;
exit:
    return status;
}

static ssize_t show_present(struct device *dev, struct device_attribute *da,
                            char *buf)
{
    int pid = ((struct platform_device *)dev_get_drvdata(dev))->id;
    int status = 0;

    mutex_lock(&data->update_lock);

    status = as7327_56x_psu_update_present(dev, pid);

    if (status < 0) {
        status = -EIO;
        goto exit;
    }

    status = sprintf(buf, "%d\n", data->psu_data[pid].present);

exit:
    mutex_unlock(&data->update_lock);
    return status ;
}

static ssize_t show_psu_alarm(struct device *dev, struct device_attribute *da,
                            char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    int pid = ((struct platform_device *)dev_get_drvdata(dev))->id;
    unsigned char value = 0;
    unsigned int psu_status = 0;
    int status;

    mutex_lock(&data->update_lock);

    status = as7327_56x_psu_update_device(dev, pid);

    if (unlikely(status != 0)) /* IO ERR */
        goto exit;

    VALIDATE_PRESENT_RETURN(pid);

    psu_status = (u32)data->psu_data[pid].status[0] << 24 | 
                 (u32)data->psu_data[pid].status[1] << 16 |
                 (u32)data->psu_data[pid].status[2] << 8 |
                 (u32)data->psu_data[pid].status[3];

    switch(attr->index) {
        case PSU_POWER_GOOD:
            value = !(psu_status & PSU_STATUS_POWER_GOOD_N);
            break;
        case PSU_POWER_ON:
            value = !(psu_status & PSU_STATUS_OFF);
            break;
        case PSU_OVR_TEMP:
        case PSU_TEMP_FAULT:
            value = !!(psu_status & PSU_STATUS_TEMPERATURE);
            break;
        case PSU_FAN_FAULT:
            value = !!(psu_status & PSU_STATUS_FANS);
            break;
        default:
            dev_warn(&data->pdev[pid]->dev, "unknown attr id %d\n", attr->index);
            status = sprintf(buf, "NA\n");
            goto exit;
    };

    status = sprintf(buf, "%d\n", value);
exit:
    mutex_unlock(&data->update_lock);
    return status;
}

static ssize_t show_psu_fan(struct device *dev, struct device_attribute *da,
                            char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    int pid = ((struct platform_device *)dev_get_drvdata(dev))->id;
    unsigned int value = 0;
    unsigned char *ptr = NULL;
    int status = 0;

    mutex_lock(&data->update_lock);

    status = as7327_56x_psu_update_device(dev, pid);

    if (unlikely(status != 0)) /* IO ERR */
        goto exit;

    VALIDATE_PRESENT_RETURN(pid);

    ptr = data->psu_data[pid].fan_speed;
    value = (u32)ptr[0] << 24 | (u32)ptr[1] << 16 | (u32)ptr[2] << 8 | (u32)ptr[3];

    switch(attr->index) {
        case PSU_FAN_SPEED:
            status = sprintf(buf, "%d\n", value);
            break;
        case PSU_FAN_DUTY_CYCLE:
            value = value * 100 / PSU_MAX_FAN_SPEED;
            value = (value > 100)? 100: value;
            status = sprintf(buf, "%d\n", value);
            break;
        case PSU_FAN_DIRECTION: /* psu_fan_dir, 0=>F2B, 1=>B2F */
            if ((strncmp((data->psu_data[pid].mfr_model), "C1A-B0650-C", strlen("C1A-B0650-C")) == 0) ||
                (strncmp((data->psu_data[pid].mfr_model), "G1342-0800W", strlen("G1342-0800W")) == 0))
                status = sprintf(buf, "0\n");
            else
                /* Unknown direction */
                status = sprintf(buf, "2\n");
            break;
        default:
            dev_err(&data->pdev[pid]->dev, "unknown attr id %d\n", attr->index);
            status = -EINVAL;;
    };

exit:
    mutex_unlock(&data->update_lock);
    return status;
}

static ssize_t show_psu_status(struct device *dev, struct device_attribute *da,
                            char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    int pid = ((struct platform_device *)dev_get_drvdata(dev))->id;
    unsigned int value = 0;
    unsigned int multiplier = 1;
    unsigned char *ptr = NULL;
    int status;
    mutex_lock(&data->update_lock);

    status = as7327_56x_psu_update_device(dev, pid);

    if (unlikely(status != 0)) /* IO ERR */
        goto exit;

    VALIDATE_PRESENT_RETURN(pid);

    switch(attr->index) {
        case PSU_VIN:
            ptr = data->psu_data[pid].vin;
            break;
        case PSU_VOUT:
            ptr = data->psu_data[pid].vout;
            break;
        case PSU_IIN:
            ptr = data->psu_data[pid].iin;
            break;
        case PSU_IOUT:
            ptr = data->psu_data[pid].iout;
            break;
        case PSU_TEMP1_INPUT:
            ptr = data->psu_data[pid].temp[0];
            break;
        case PSU_TEMP2_INPUT:
            ptr = data->psu_data[pid].temp[1];
            break;
        case PSU_TEMP3_INPUT:
            ptr = data->psu_data[pid].temp[2];
            break;
        case PSU_PIN:
            multiplier = 1000;
            ptr = data->psu_data[pid].pin;
            break;
        case PSU_POUT:
            multiplier = 1000;
            ptr = data->psu_data[pid].pout;
            break;
        case PSU_POUT_MAX:
            status = sprintf(buf, "650000\n");
            goto exit;
        case PSU_FAN_SPEED:
            ptr = data->psu_data[pid].fan_speed;
            value = (u32)ptr[0] << 24 | (u32)ptr[1] << 16 | (u32)ptr[2] << 8 | (u32)ptr[3];
            status = sprintf(buf, "%d\n", value);
            goto exit;
        case PSU_FAN_DUTY_CYCLE:
            ptr = data->psu_data[pid].fan_speed;
            value = (u32)ptr[0] << 24 | (u32)ptr[1] << 16 | (u32)ptr[2] << 8 | (u32)ptr[3];
            value = value * 100 / PSU_MAX_FAN_SPEED;
            value = (value > 100)? 100: value;
            status = sprintf(buf, "%d\n", value);
            goto exit;
        default:
            dev_err(&data->pdev[pid]->dev, "unknown attr id %d\n", attr->index);
            status = sprintf(buf, "NA\n");
            goto exit;
    };
    value = (u32)ptr[0] << 24 | (u32)ptr[1] << 16 | (u32)ptr[2] << 8 | (u32)ptr[3];

    status = sprintf(buf, "%d\n", value/multiplier);

exit:
    mutex_unlock(&data->update_lock);
    return status;
}


static ssize_t show_psu_mfr(struct device *dev, struct device_attribute *da,
                            char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    int pid = ((struct platform_device *)dev_get_drvdata(dev))->id;
    int status;
    mutex_lock(&data->update_lock);

    status = as7327_56x_psu_update_device(dev, pid);

    if (unlikely(status != 0)) /* IO ERR */
        goto exit;

    VALIDATE_PRESENT_RETURN(pid);

    switch(attr->index) {
        case PSU_PN:
            status = sprintf(buf, "%s\n", data->psu_data[pid].mfr_pn);
            break;
        case PSU_SN:
            status = sprintf(buf, "%s\n", data->psu_data[pid].mfr_sn);
            break;
        case PSU_ID:
            status = sprintf(buf, "%s\n", data->psu_data[pid].mfr_id);
            break;
        case PSU_MODEL:
            status = sprintf(buf, "%s\n", data->psu_data[pid].mfr_model);
            break;
        case PSU_REVISION:
            status = sprintf(buf, "%s\n", data->psu_data[pid].mfr_hwrev);
            break;
        case PSU_DATE:
            status = sprintf(buf, "%s\n", data->psu_data[pid].mfr_date);
            break;
        case PSU_VIN_TYPE:
            /* 0:Unknown 1:AC 2:DC*/
            if ((strncmp((data->psu_data[pid].mfr_model), "C1A-B0650-C", strlen("C1A-B0650-C")) == 0) ||
                (strncmp((data->psu_data[pid].mfr_model), "G1342-0800W", strlen("G1342-0800W")) == 0)) {
                status = sprintf(buf, "1\n");
            } else {
                status = sprintf(buf, "0\n");
            }

            break;
        default:
            dev_err(&data->pdev[pid]->dev, "unknown attr id %d\n", attr->index);
            status = sprintf(buf, "NA\n");
            break;
    }
exit:
    mutex_unlock(&data->update_lock);
    return status;
}

static int as7327_56x_psu_probe(struct platform_device *pdev)
{
    int status = 0;
    struct device *hwmon_dev = NULL;

    hwmon_dev = hwmon_device_register_with_groups(&pdev->dev, DRVNAME, 
                    pdev, as7327_56x_psu_groups[pdev->id]);
    if (IS_ERR(hwmon_dev)) {
        status = PTR_ERR(hwmon_dev);
        return status;
    }

    mutex_lock(&data->update_lock);
    data->hwmon_dev[pdev->id] = hwmon_dev;
    mutex_unlock(&data->update_lock);

    dev_info(&pdev->dev, "PSU%d device created\n", pdev->id + 1);

    return 0;
}

static void as7327_56x_psu_remove(struct platform_device *pdev)
{
    mutex_lock(&data->update_lock);
    if (data->hwmon_dev[pdev->id]) {
        hwmon_device_unregister(data->hwmon_dev[pdev->id]);
        data->hwmon_dev[pdev->id] = NULL;
    }
    mutex_unlock(&data->update_lock);
}

static int __init as7327_56x_psu_init(void)
{
    int ret;
    int i;

    data = kzalloc(sizeof(struct as7327_56x_psu_data), GFP_KERNEL);
    if (!data) {
        ret = -ENOMEM;
        goto alloc_err;
    }

    mutex_init(&data->update_lock);

    ret = platform_driver_register(&as7327_56x_psu_driver);
    if (ret < 0)
        goto dri_reg_err;

    for (i = 0; i < NUM_OF_PSU; i++) {
        data->pdev[i] = platform_device_register_simple(DRVNAME, i, NULL, 0);
        if (IS_ERR(data->pdev[i])) {
            ret = PTR_ERR(data->pdev[i]);
            goto dev_reg_err;
        }
    }

    /* Set up IPMI interface */
    ret = init_ipmi_data(&data->ipmi, 0);
    if (ret) {
        goto ipmi_err;
    }

    return 0;

ipmi_err:
    while (i > 0) {
        i--;
        platform_device_unregister(data->pdev[i]);
    }
dev_reg_err:
    platform_driver_unregister(&as7327_56x_psu_driver);
dri_reg_err:
    kfree(data);
alloc_err:
    return ret;
}

static void __exit as7327_56x_psu_exit(void)
{
    int i;

    ipmi_destroy_user(data->ipmi.user);
    for (i = 0; i < NUM_OF_PSU; i++) {
        platform_device_unregister(data->pdev[i]);
    }
    platform_driver_unregister(&as7327_56x_psu_driver);
    kfree(data);
}

MODULE_AUTHOR("Ray Huang <rayx_huang@edge-core.com>");
MODULE_DESCRIPTION("as7327_56x_psu_bmc driver");
MODULE_LICENSE("GPL");

module_init(as7327_56x_psu_init);
module_exit(as7327_56x_psu_exit);

