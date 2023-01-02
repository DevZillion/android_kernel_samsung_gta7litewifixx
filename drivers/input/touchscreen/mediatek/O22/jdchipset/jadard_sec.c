#include "jadard_common.h"
extern struct jadard_ic_data *pjadard_ic_data;

extern int jadard_enable_gesture_wakeup(void);
extern int jadard_disable_gesture_wakeup(void);

static void get_fw_ver_bin(void *device_data);
static void get_fw_ver_ic(void *device_data);
static void aot_enable(void *device_data);
static void high_sensitivity_mode(void *device_data);
static void not_support_cmd(void *device_data);

struct sec_cmd jadard_commands[] = {
    {SEC_CMD("get_fw_ver_bin", get_fw_ver_bin),},
    {SEC_CMD("get_fw_ver_ic", get_fw_ver_ic),},
    {SEC_CMD("aot_enable", aot_enable),},
    {SEC_CMD("glove_mode", high_sensitivity_mode),},
    {SEC_CMD("not_support_cmd", not_support_cmd),},
};

int jadard_get_array_size(void)
{
    return ARRAY_SIZE(jadard_commands);
}

static void get_fw_ver_bin(void *device_data)
{
    char buff[20] = { 0 };
    struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
    printk("%s:  \n", __func__);
    sec_cmd_set_default_result(sec);
    snprintf(buff, sizeof(buff), "JDTP_BIN=%02x",pjadard_ic_data->fw_ver & 0xFF);
    sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
    sec->cmd_state = SEC_CMD_STATUS_OK;
    printk("%s: %s\n", __func__, buff);
}

static void get_fw_ver_ic(void *device_data)
{
    struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
    char buff[20] = { 0 };
    printk("%s:  \n", __func__);
    sec_cmd_set_default_result(sec);
    snprintf(buff, sizeof(buff), "JD9522T_XX_BOE");
    sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
    sec->cmd_state = SEC_CMD_STATUS_OK;
    printk("%s: %s\n", __func__, buff);
}

static void aot_enable(void *device_data)
{
    struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
    char buff[SEC_CMD_STR_LEN] = { 0 };

    sec_cmd_set_default_result(sec);

    if (sec->cmd_param[0] < 0 || sec->cmd_param[0] > 1) {
        snprintf(buff, sizeof(buff), "%s", "NG");
        sec->cmd_state = SEC_CMD_STATUS_FAIL;
    } else {
        if (sec->cmd_param[0] == 0) {
            jadard_disable_gesture_wakeup();
            printk("%s: smart_wakeup_open_flag=%d\n", __func__, smart_wakeup_open_flag);
        } else {;
            jadard_enable_gesture_wakeup();
            printk("%s: smart_wakeup_open_flag=%d\n", __func__, smart_wakeup_open_flag);
        }
        snprintf(buff, sizeof(buff), "%s", "OK");
        sec->cmd_state = SEC_CMD_STATUS_OK;
    }
    sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
    sec->cmd_state = SEC_CMD_STATUS_WAITING;
    sec_cmd_set_cmd_exit(sec);

    printk("%s: %s\n", __func__, buff);
}

static void high_sensitivity_mode(void *device_data)
{
    struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;

    char buff[SEC_CMD_STR_LEN] = { 0 };
    printk("%s:  \n", __func__);
    sec_cmd_set_default_result(sec);

    if (sec->cmd_param[0] < 0 || sec->cmd_param[0] > 1) {
        snprintf(buff, sizeof(buff), "%s", "NG");
        sec->cmd_state = SEC_CMD_STATUS_FAIL;
    } else {
        if (sec->cmd_param[0] == 0) {
            printk("%s: disable high sensitivity \n", __func__);
        } else {
            printk("%s: enable high sensitivity \n", __func__);
        }
        snprintf(buff, sizeof(buff), "%s", "OK");
        sec->cmd_state = SEC_CMD_STATUS_OK;
    }
    sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
    sec->cmd_state = SEC_CMD_STATUS_WAITING;
    sec_cmd_set_cmd_exit(sec);

    printk("%s: %s\n", __func__, buff);
}

static void not_support_cmd(void *device_data)
{
    struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
    char buff[16] = { 0 };
    printk("%s:  \n", __func__);
    sec_cmd_set_default_result(sec);
    snprintf(buff, sizeof(buff), "%s", "NA");
    sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
    sec->cmd_state = SEC_CMD_STATUS_NOT_APPLICABLE;
    sec_cmd_set_cmd_exit(sec);

    printk("%s: %s\n", __func__, buff);
}
