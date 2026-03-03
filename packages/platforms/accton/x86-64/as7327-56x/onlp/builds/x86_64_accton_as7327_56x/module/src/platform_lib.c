#include <unistd.h>
#include <fcntl.h>
#include "platform_lib.h"
#include <onlp/platformi/sfpi.h>
#include "x86_64_accton_as7327_56x_log.h"

int bmc_enable = -1;

int onlp_file_write_integer(char *filename, int value)
{
    char buf[8] = {0};
    sprintf(buf, "%d", value);

    return onlp_file_write((uint8_t*)buf, strlen(buf), filename);
}

int onlp_file_read_binary(char *filename, char *buffer, int buf_size, int data_len)
{
    int fd;
    int len;

    if ((buffer == NULL) || (buf_size < 0)) {
        return -1;
    }

    if ((fd = open(filename, O_RDONLY)) == -1) {
        return -1;
    }

    if ((len = read(fd, buffer, buf_size)) < 0) {
        close(fd);
        return -1;
    }

    if ((close(fd) == -1)) {
        return -1;
    }

    if ((len > buf_size) || (data_len != 0 && len != data_len)) {
        return -1;
    }

    return 0;
}

int onlp_file_read_string(char *filename, char *buffer, int buf_size, int data_len)
{
    int ret;

    if (data_len >= buf_size) {
	    return -1;
	}

	ret = onlp_file_read_binary(filename, buffer, buf_size-1, data_len);

    if (ret == 0) {
        buffer[buf_size-1] = '\0';
    }

    return ret;
}

int initialize_bmc_status(void)
{
    int   rv = 0;
    int   len;
    uint8_t  data;

    if (bmc_enable >= 0) {
        return ONLP_STATUS_OK;
    }

    rv = onlp_file_read(&data, sizeof(data), &len, BMC_ENABLE_NODE);
    if (rv == ONLP_STATUS_OK) {
        if (data == '1')
            bmc_enable = 1;
        else
            bmc_enable = 0;
    } else {
        AIM_LOG_ERROR("Unable to get bmc enable (%s)\r\n", BMC_ENABLE_NODE);
        bmc_enable = 0;
    }
    return ONLP_STATUS_OK;

}

/* Get the fullpath of the subdirectory from root */
int subdir_path_get(char *root, char *prefix, int prefix_len, char *path, int path_len)
{
    int  rv = 0;
    char tmp[100];
    char *sysfs_node[] = {tmp};

    rv = onlp_dir_read(root, prefix, prefix_len, sysfs_node, 1);

    if (rv != ONLP_STATUS_OK)
        return ONLP_STATUS_E_GENERIC;

    if (path_len > (strlen(root) + strlen(tmp) + 2))
        sprintf(path, "%s/%s", root, tmp);
    else
        return ONLP_STATUS_E_GENERIC;

    return ONLP_STATUS_OK;

}

int psu_bmc_str_get(path_t *bmc_path, char *field, char *data, int size)
{
    int  len;
    int  rv = 0;
    char path[256] = {0};

    if (!strlen(bmc_path->target_path) ||
        strncmp(bmc_path->target_path, bmc_path->base_path, strlen(bmc_path->base_path))) {

        rv = subdir_path_get(bmc_path->base_path, "hwmon", strlen("hwmon"), bmc_path->target_path, sizeof(bmc_path->target_path));
        if (rv != ONLP_STATUS_OK) {
            AIM_LOG_ERROR("Unable to get the subdirectory (%s)\r\n", bmc_path->base_path);
            return ONLP_STATUS_E_INTERNAL;
        }
    }

    sprintf(path, "%s/%s", bmc_path->target_path, field);

    rv = onlp_file_read((uint8_t*)data, size, &len, "%s", path);
    if ((rv != ONLP_STATUS_OK) && len) {
        AIM_LOG_ERROR("Unable to read string from file (%s)\r\n", path);
        return ONLP_STATUS_E_INTERNAL;
    }
    return rv;
}

/* 
 * Search the directory under hwmon and get data from the node
 */
int psu_bmc_info_get(path_t *bmc_path, char *field, int *val)
{
    int  rv = 0;
    char path[256] = {0};

    if (!strlen(bmc_path->target_path) ||
        strncmp(bmc_path->target_path, bmc_path->base_path, strlen(bmc_path->base_path))) {

        rv = subdir_path_get(bmc_path->base_path, "hwmon", strlen("hwmon"), bmc_path->target_path, sizeof(bmc_path->target_path));
        if (rv != ONLP_STATUS_OK) {
            AIM_LOG_ERROR("Unable to get the subdirectory (%s)\r\n", bmc_path->base_path);
            return ONLP_STATUS_E_INTERNAL;
        }
    }

    sprintf(path, "%s/%s", bmc_path->target_path, field);

    if (onlp_file_read_int(val, path) < 0) {
        AIM_LOG_ERROR("Unable to read status from file (%s)\r\n", path);
        return ONLP_STATUS_E_INTERNAL;
    }
    return ONLP_STATUS_OK;
}

int psu_pmbus_info_get(int id, char *node, int *value)
{
    int  ret = 0;
    char path[PSU_NODE_MAX_PATH_LEN] = {0};

    *value = 0;

    if (PSU1_ID == id) {
        sprintf(path, "%s%s", PSU1_AC_PMBUS_PREFIX, node);
    }
    else {
        sprintf(path, "%s%s", PSU2_AC_PMBUS_PREFIX, node);
    }

    if (onlp_file_read_int(value, path) < 0) {
        AIM_LOG_ERROR("Unable to read status from file(%s)\r\n", path);
        return ONLP_STATUS_E_INTERNAL;
    }

    return ret;
}

int psu_pmbus_info_set(int id, char *node, int value)
{
    char path[PSU_NODE_MAX_PATH_LEN] = {0};

	switch (id) {
	case PSU1_ID:
		sprintf(path, "%s%s", PSU1_AC_PMBUS_PREFIX, node);
		break;
	case PSU2_ID:
		sprintf(path, "%s%s", PSU2_AC_PMBUS_PREFIX, node);
		break;
	default:
		return ONLP_STATUS_E_UNSUPPORTED;
	};

    if (onlp_file_write_integer(path, value) < 0) {
        AIM_LOG_ERROR("Unable to write data to file (%s)\r\n", path);
        return ONLP_STATUS_E_INTERNAL;
    }

    return ONLP_STATUS_OK;
}


int psu_pmbus_model_name_get(int id, char *model, int model_len)
{
	int   size = 0;
	int   ret  = ONLP_STATUS_OK;
	char *prefix = NULL;

	if (model == NULL || model_len < PSU_MODEL_NAME_LEN) {
		return ONLP_STATUS_E_PARAM;
	}

	prefix = (id == PSU1_ID) ? PSU1_AC_PMBUS_PREFIX : PSU2_AC_PMBUS_PREFIX;

	ret = onlp_file_read((uint8_t*)model, PSU_MODEL_NAME_LEN, &size, "%s%s", prefix, "psu_mfr_model");
    if (ret != ONLP_STATUS_OK || size != PSU_MODEL_NAME_LEN) {
		return ONLP_STATUS_E_INTERNAL;

    }

	model[PSU_MODEL_NAME_LEN] = '\0';
	return ONLP_STATUS_OK;
}


int psu_pmbus_serial_number_get(int id, char *serial, int serial_len)
{
	int   size = 0;
	int   ret  = ONLP_STATUS_OK;
	char *prefix = NULL;

	if (serial == NULL || serial_len < PSU_SERIAL_NUMBER_LEN) {
		return ONLP_STATUS_E_PARAM;
	}

	prefix = (id == PSU1_ID) ? PSU1_AC_PMBUS_PREFIX : PSU2_AC_PMBUS_PREFIX;

	ret = onlp_file_read((uint8_t*)serial, PSU_SERIAL_NUMBER_LEN, &size, "%s%s", prefix, "psu_mfr_serial");
    if (ret != ONLP_STATUS_OK || size != PSU_SERIAL_NUMBER_LEN) {
		return ONLP_STATUS_E_INTERNAL;

    }

	serial[PSU_SERIAL_NUMBER_LEN] = '\0';
	return ONLP_STATUS_OK;
}

