/* Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>

#include "cam_sensor_cmn_header.h"
#include "cam_flash_core.h"
#include "cam_res_mgr_api.h"

static int cam_flash_prepare(struct cam_flash_ctrl *flash_ctrl,
	bool regulator_enable)
{
	int rc = 0;

	if (!(flash_ctrl->switch_trigger)) {
		CAM_ERR(CAM_FLASH, "Invalid argument");
		return -EINVAL;
	}

	if (regulator_enable &&
		(flash_ctrl->is_regulator_enabled == false)) {
		rc = qpnp_flash_led_prepare(flash_ctrl->switch_trigger,
			ENABLE_REGULATOR, NULL);
		if (rc) {
			CAM_ERR(CAM_FLASH, "regulator enable failed rc = %d",
				rc);
			return rc;
		}
		flash_ctrl->is_regulator_enabled = true;
	} else if ((!regulator_enable) &&
		(flash_ctrl->is_regulator_enabled == true)) {
		rc = qpnp_flash_led_prepare(flash_ctrl->switch_trigger,
			DISABLE_REGULATOR, NULL);
		if (rc) {
			CAM_ERR(CAM_FLASH, "regulator disable failed rc = %d",
				rc);
			return rc;
		}
		flash_ctrl->is_regulator_enabled = false;
	} else {
		CAM_ERR(CAM_FLASH, "Wrong Flash State : %d",
			flash_ctrl->flash_state);
		rc = -EINVAL;
	}

	return rc;
}

static int cam_flash_flush_nrt(struct cam_flash_ctrl *fctrl)
{
	int j = 0;
	struct cam_flash_frame_setting *nrt_settings;

	if (!fctrl)
		return -EINVAL;

	nrt_settings = &fctrl->nrt_info;

	if (nrt_settings->cmn_attr.cmd_type ==
		CAMERA_SENSOR_FLASH_CMD_TYPE_INIT_INFO) {
		fctrl->flash_init_setting.cmn_attr.is_settings_valid = false;
	} else if ((nrt_settings->cmn_attr.cmd_type ==
		CAMERA_SENSOR_FLASH_CMD_TYPE_WIDGET) ||
		(nrt_settings->cmn_attr.cmd_type ==
		CAMERA_SENSOR_FLASH_CMD_TYPE_RER) ||
		(nrt_settings->cmn_attr.cmd_type ==
		CAMERA_SENSOR_FLASH_CMD_TYPE_INIT_FIRE)) {
		fctrl->nrt_info.cmn_attr.is_settings_valid = false;
		fctrl->nrt_info.cmn_attr.count = 0;
		fctrl->nrt_info.num_iterations = 0;
		fctrl->nrt_info.led_on_delay_ms = 0;
		fctrl->nrt_info.led_off_delay_ms = 0;
		for (j = 0; j < CAM_FLASH_MAX_LED_TRIGGERS; j++)
			fctrl->nrt_info.led_current_ma[j] = 0;
	}

	return 0;
}

static int cam_flash_construct_default_power_setting(
	struct cam_sensor_power_ctrl_t *power_info)
{
	int rc = 0;

	power_info->power_setting_size = 1;
	power_info->power_setting =
		(struct cam_sensor_power_setting *)
		kzalloc(sizeof(struct cam_sensor_power_setting),
			GFP_KERNEL);
	if (!power_info->power_setting)
		return -ENOMEM;

	power_info->power_setting[0].seq_type = SENSOR_CUSTOM_REG1;
	power_info->power_setting[0].seq_val = CAM_V_CUSTOM1;
	power_info->power_setting[0].config_val = 1;
	power_info->power_setting[0].delay = 2;

	power_info->power_down_setting_size = 1;
	power_info->power_down_setting =
		(struct cam_sensor_power_setting *)
		kzalloc(sizeof(struct cam_sensor_power_setting),
			GFP_KERNEL);
	if (!power_info->power_down_setting) {
		rc = -ENOMEM;
		goto free_power_settings;
	}

	power_info->power_down_setting[0].seq_type = SENSOR_CUSTOM_REG1;
	power_info->power_down_setting[0].seq_val = CAM_V_CUSTOM1;
	power_info->power_down_setting[0].config_val = 0;

	return rc;

free_power_settings:
	kfree(power_info->power_setting);
	return rc;
}

int cam_flash_pmic_power_ops(struct cam_flash_ctrl *fctrl,
	bool regulator_enable)
{
	int rc = 0;

	if (!(fctrl->switch_trigger)) {
		CAM_ERR(CAM_FLASH, "Invalid argument");
		return -EINVAL;
	}

	if (regulator_enable) {
		rc = cam_flash_prepare(fctrl, true);
		if (rc) {
			CAM_ERR(CAM_FLASH,
				"Enable Regulator Failed rc = %d", rc);
			return rc;
		}
	}

	if (!regulator_enable) {
		//LENOVO_CUSTOM start
		if ((fctrl->is_regulator_enabled == true) &&
			((fctrl->flash_state == CAM_FLASH_STATE_ACQUIRE) ||
			(fctrl->flash_state == CAM_FLASH_STATE_CONFIG))) {
			rc = cam_flash_off(fctrl);
				if (rc) {
					CAM_ERR(CAM_FLASH,
					"LED OFF FAILED: %d",
					rc);
			}
		}
		//LENOVO_CUSTOM start
		if ((fctrl->flash_state == CAM_FLASH_STATE_START) &&
			(fctrl->is_regulator_enabled == true)) {
			rc = cam_flash_prepare(fctrl, false);
			if (rc)
				CAM_ERR(CAM_FLASH,
					"Disable Regulator Failed rc: %d", rc);
		}
	}

	return rc;
}

//LENOVO_CUSTOM turn i2c flash off by i2c register begin
int msm_camera_i2c_flash_off(struct cam_flash_ctrl *fctrl)
{
#ifndef DISABLE_I2C_FLASH_READ_REGISTER_CHECK
	uint32_t reg_value = 0;
#endif
	int rc = 0;
	struct cam_sensor_i2c_reg_setting i2c_reg_settings;
	struct cam_sensor_i2c_reg_array    i2c_reg_array;

	CAM_INFO(CAM_FLASH, "msm_camera_i2c_flash_off enter");

	i2c_reg_settings.addr_type=CAMERA_SENSOR_I2C_TYPE_BYTE;
	i2c_reg_settings.data_type=CAMERA_SENSOR_I2C_TYPE_BYTE;
	i2c_reg_settings.size=1;
	i2c_reg_settings.delay=0;
	i2c_reg_array.reg_addr = CAM_I2C_FLASH_AW36423_ENABLE_REGISTER;
	i2c_reg_array.reg_data = 0x00;
	i2c_reg_array.delay = 1;
	i2c_reg_settings.reg_setting = &i2c_reg_array;

	rc = camera_io_dev_write(&(fctrl->io_master_info),
		&i2c_reg_settings);
	if (rc < 0) {
		CAM_ERR(CAM_FLASH,
			"Failed to random write I2C settings: %d",
			rc);
		return rc;
	}

#ifndef DISABLE_I2C_FLASH_READ_REGISTER_CHECK
	/* Register 0x00 */
	camera_io_dev_read(
		&(fctrl->io_master_info),
		CAM_I2C_FLASH_AW36423_CHIP_ID_REGISTER,
		&reg_value, CAMERA_SENSOR_I2C_TYPE_BYTE,
		CAMERA_SENSOR_I2C_TYPE_BYTE);
	CAM_INFO(CAM_FLASH, "### Reg[0x%x]=0x%x ",CAM_I2C_FLASH_AW36423_CHIP_ID_REGISTER,
			 reg_value);

	/* Register 0x01 */
	camera_io_dev_read(
		&(fctrl->io_master_info),
		CAM_I2C_FLASH_AW36423_ENABLE_REGISTER,
		&reg_value, CAMERA_SENSOR_I2C_TYPE_BYTE,
		CAMERA_SENSOR_I2C_TYPE_BYTE);
	CAM_INFO(CAM_FLASH, "### Reg[0x%x]=0x%x ",CAM_I2C_FLASH_AW36423_ENABLE_REGISTER,
			 reg_value);

	/* Register 0x02 */
	camera_io_dev_read(
		&(fctrl->io_master_info),
		CAM_I2C_FLASH_AW36423_IVFM_REGISTER,
		&reg_value, CAMERA_SENSOR_I2C_TYPE_BYTE,
		CAMERA_SENSOR_I2C_TYPE_BYTE);
	CAM_INFO(CAM_FLASH, "### Reg[0x%x]=0x%x ",CAM_I2C_FLASH_AW36423_IVFM_REGISTER,
			 reg_value);

	/* Register 0x03 */
	camera_io_dev_read(
		&(fctrl->io_master_info),
		CAM_I2C_FLASH_AW36423_LED1_FLASH_BRIGHTNESS_REGISTER,
		&reg_value, CAMERA_SENSOR_I2C_TYPE_BYTE,
		CAMERA_SENSOR_I2C_TYPE_BYTE);
	CAM_INFO(CAM_FLASH, "### Reg[0x%x]=0x%x ",CAM_I2C_FLASH_AW36423_LED1_FLASH_BRIGHTNESS_REGISTER,
			 reg_value);

	/* Register 0x04 */
	camera_io_dev_read(
		&(fctrl->io_master_info),
		CAM_I2C_FLASH_AW36423_LED1_TORCH_BRIGHTNESS_REGISTER,
		&reg_value, CAMERA_SENSOR_I2C_TYPE_BYTE,
		CAMERA_SENSOR_I2C_TYPE_BYTE);
	CAM_INFO(CAM_FLASH, "### Reg[0x%x]=0x%x ",CAM_I2C_FLASH_AW36423_LED1_TORCH_BRIGHTNESS_REGISTER,
			 reg_value);

	/* Register 0x05 */
	camera_io_dev_read(
		&(fctrl->io_master_info),
		CAM_I2C_FLASH_AW36423_LED2_FLASH_BRIGHTNESS_REGISTER,
		&reg_value, CAMERA_SENSOR_I2C_TYPE_BYTE,
		CAMERA_SENSOR_I2C_TYPE_BYTE);
	CAM_INFO(CAM_FLASH, "### Reg[0x%x]=0x%x ",CAM_I2C_FLASH_AW36423_LED2_FLASH_BRIGHTNESS_REGISTER,
			 reg_value);

	/* Register 0x06 */
	camera_io_dev_read(
		&(fctrl->io_master_info),
		CAM_I2C_FLASH_AW36423_LED2_TORCH_BRIGHTNESS_REGISTER,
		&reg_value, CAMERA_SENSOR_I2C_TYPE_BYTE,
		CAMERA_SENSOR_I2C_TYPE_BYTE);
	CAM_INFO(CAM_FLASH, "### Reg[0x%x]=0x%x ",CAM_I2C_FLASH_AW36423_LED2_TORCH_BRIGHTNESS_REGISTER,
			 reg_value);

	/* Register 0x0B */
	camera_io_dev_read(
		&(fctrl->io_master_info),
		CAM_I2C_FLASH_AW36423_FLASH_TORCH_TIMING_REGISTER,
		&reg_value, CAMERA_SENSOR_I2C_TYPE_BYTE,
		CAMERA_SENSOR_I2C_TYPE_BYTE);
	CAM_INFO(CAM_FLASH, "### Reg[0x%x]=0x%x ",CAM_I2C_FLASH_AW36423_FLASH_TORCH_TIMING_REGISTER,
			 reg_value);

	/* Register 0x0F */
	camera_io_dev_read(
		&(fctrl->io_master_info),
		CAM_I2C_FLASH_AW36423_FLAGS_REGISTER,
		&reg_value, CAMERA_SENSOR_I2C_TYPE_BYTE,
		CAMERA_SENSOR_I2C_TYPE_BYTE);
	CAM_INFO(CAM_FLASH, "### Reg[0x%x]=0x%x ",CAM_I2C_FLASH_AW36423_FLAGS_REGISTER,
			 reg_value);

	/* Register 0x10 */
	camera_io_dev_read(
		&(fctrl->io_master_info),
		CAM_I2C_FLASH_AW36423_DEVICE_ID_REGISTER,
		&reg_value, CAMERA_SENSOR_I2C_TYPE_BYTE,
		CAMERA_SENSOR_I2C_TYPE_BYTE);
	CAM_INFO(CAM_FLASH, "### Reg[0x%x]=0x%x ",CAM_I2C_FLASH_AW36423_DEVICE_ID_REGISTER,
			 reg_value);
#endif
	return rc;
}
//LENOVO_CUSTOM turn i2c flash off by i2c register end
int cam_flash_i2c_power_ops(struct cam_flash_ctrl *fctrl,
	bool regulator_enable)
{
	int rc = 0;
	struct cam_hw_soc_info *soc_info = &fctrl->soc_info;
	struct cam_sensor_power_ctrl_t *power_info =
		&fctrl->power_info;

	if (!power_info || !soc_info) {
		CAM_ERR(CAM_FLASH, "Power Info is NULL");
		return -EINVAL;
	}
	power_info->dev = soc_info->dev;

	if (regulator_enable && (fctrl->is_regulator_enabled == false)) {
		if ((power_info->power_setting == NULL) &&
			(power_info->power_down_setting == NULL)) {
			CAM_INFO(CAM_FLASH,
				"Using default power settings");
			rc = cam_flash_construct_default_power_setting(
					power_info);
			if (rc < 0) {
				CAM_ERR(CAM_FLASH,
					"Construct default actuator power setting failed.");
				return rc;
			}
		}

		rc = cam_sensor_core_power_up(power_info, soc_info);
		if (rc) {
			CAM_ERR(CAM_FLASH, "power up the core is failed:%d",
				rc);
			return rc;
		}

		rc = camera_io_init(&(fctrl->io_master_info));
		if (rc) {
			CAM_ERR(CAM_FLASH, "cci_init failed: rc: %d", rc);
			return rc;
		}
		fctrl->is_regulator_enabled = true;
	} else if ((!regulator_enable) &&
		(fctrl->is_regulator_enabled == true)) {
		//LENOVO_CUSTOM turn i2c flash off by i2c register begin
		rc = msm_camera_i2c_flash_off(fctrl);
		if (rc) {
			CAM_ERR(CAM_FLASH, "i2c flash off is failed:%d",
				rc);
		}
		//LENOVO_CUSTOM turn i2c flash off by i2c register end

		rc = msm_camera_power_down(power_info, soc_info);
		if (rc) {
			CAM_ERR(CAM_FLASH, "power down the core is failed:%d",
				rc);
			return rc;
		}

		camera_io_release(&(fctrl->io_master_info));
		fctrl->is_regulator_enabled = false;
	}
	return rc;
}

int cam_flash_pmic_flush_request(struct cam_flash_ctrl *fctrl,
	enum cam_flash_delete_type type, uint64_t req_id)
{
	int rc = 0;
	int i = 0, j = 0;
	int frame_offset = 0;

	if (!fctrl) {
		CAM_ERR(CAM_FLASH, "Device data is NULL");
		return -EINVAL;
	}

	if (type == DELETE_ALL) {
	/* flush all requests*/
		for (i = 0; i < MAX_PER_FRAME_ARRAY; i++) {
			fctrl->per_frame[i].cmn_attr.request_id = 0;
			fctrl->per_frame[i].cmn_attr.is_settings_valid = false;
			fctrl->per_frame[i].cmn_attr.count = 0;
			for (j = 0; j < CAM_FLASH_MAX_LED_TRIGGERS; j++)
				fctrl->per_frame[i].led_current_ma[j] = 0;
		}

		rc = cam_flash_flush_nrt(fctrl);
		if (rc) {
			CAM_WARN(CAM_FLASH, "NonRealTime flush error");
			rc = 0;
		}
	} else if ((type == DELETE_REQ) && (req_id != 0)) {
	/* flush request with req_id*/
		frame_offset = req_id % MAX_PER_FRAME_ARRAY;
		fctrl->per_frame[frame_offset].cmn_attr.request_id = 0;
		fctrl->per_frame[frame_offset].cmn_attr.is_settings_valid =
			false;
		fctrl->per_frame[frame_offset].cmn_attr.count = 0;
		for (i = 0; i < CAM_FLASH_MAX_LED_TRIGGERS; i++)
			fctrl->per_frame[frame_offset].led_current_ma[i] = 0;
	} else {
		CAM_ERR(CAM_FLASH, "Invalid arguments");
		return -EINVAL;
	}

	return rc;
}

int cam_flash_i2c_flush_request(struct cam_flash_ctrl *fctrl,
	enum cam_flash_delete_type type, uint64_t req_id)
{
	int rc = 0;
	int i = 0;
	uint32_t cancel_req_id_found = 0;
	struct i2c_settings_array *i2c_set = NULL;

	if (!fctrl) {
		CAM_ERR(CAM_FLASH, "Device data is NULL");
		return -EINVAL;
	}

	for (i = 0; i < MAX_PER_FRAME_ARRAY; i++) {
		i2c_set = &(fctrl->i2c_data.per_frame[i]);

		if ((type == DELETE_REQ) &&
			(i2c_set->request_id != req_id))
			continue;

		if (i2c_set->is_settings_valid == 1) {
			rc = delete_request(i2c_set);
			if (rc < 0)
				CAM_ERR(CAM_FLASH,
					"delete request: %lld rc: %d",
					i2c_set->request_id, rc);

			if (type == DELETE_REQ) {
				cancel_req_id_found = 1;
				break;
			}
		}
	}

	if (type == DELETE_REQ && !cancel_req_id_found)
		CAM_DBG(CAM_FLASH,
			"Flush request id:%lld not found in the pending list",
			req_id);
	return rc;
}

int cam_flash_flush_request(struct cam_req_mgr_flush_request *flush)
{
	int rc = 0;
	struct cam_flash_ctrl *fctrl = NULL;

	fctrl = (struct cam_flash_ctrl *) cam_get_device_priv(flush->dev_hdl);
	if (!fctrl) {
		CAM_ERR(CAM_FLASH, "Device data is NULL");
		return -EINVAL;
	}

	if (flush->type == CAM_REQ_MGR_FLUSH_TYPE_ALL) {
		rc = fctrl->func_tbl.flush_req(fctrl, DELETE_ALL, 0);
		if (rc) {
			CAM_ERR(CAM_FLASH, "FLUSH_TYPE_ALL failed rc: %d", rc);
			return rc;
		}
	} else if (flush->type == CAM_REQ_MGR_FLUSH_TYPE_CANCEL_REQ) {
		rc = fctrl->func_tbl.flush_req(fctrl,
				DELETE_REQ, flush->req_id);
		if (rc) {
			CAM_ERR(CAM_FLASH, "FLUSH_TYPE_ALL failed rc: %d", rc);
			return rc;
		}
	}

	return rc;
}

static int cam_flash_ops(struct cam_flash_ctrl *flash_ctrl,
	struct cam_flash_frame_setting *flash_data, enum camera_flash_opcode op)
{
	uint32_t curr = 0, max_current = 0;
	struct cam_flash_private_soc *soc_private = NULL;
	int i = 0;

	if (!flash_ctrl || !flash_data) {
		CAM_ERR(CAM_FLASH, "Fctrl or Data NULL");
		return -EINVAL;
	}

	soc_private = (struct cam_flash_private_soc *)
		flash_ctrl->soc_info.soc_private;

	if (op == CAMERA_SENSOR_FLASH_OP_FIRELOW) {
		for (i = 0; i < flash_ctrl->torch_num_sources; i++) {
			if (flash_ctrl->torch_trigger[i]) {
				max_current = soc_private->torch_max_current[i];

				if (flash_data->led_current_ma[i] <=
					max_current)
					curr = flash_data->led_current_ma[i];
				else
					curr = soc_private->torch_op_current[i];

				CAM_DBG(CAM_FLASH,
					"Led_Current[%d] = %d", i, curr);
				cam_res_mgr_led_trigger_event(
					flash_ctrl->torch_trigger[i],
					curr);
			}
		}
	} else if (op == CAMERA_SENSOR_FLASH_OP_FIREHIGH) {
		for (i = 0; i < flash_ctrl->flash_num_sources; i++) {
			if (flash_ctrl->flash_trigger[i]) {
				max_current = soc_private->flash_max_current[i];

				if (flash_data->led_current_ma[i] <=
					max_current)
					curr = flash_data->led_current_ma[i];
				else
					curr = soc_private->flash_op_current[i];

				CAM_DBG(CAM_FLASH, "LED flash_current[%d]: %d",
					i, curr);
				cam_res_mgr_led_trigger_event(
					flash_ctrl->flash_trigger[i],
					curr);
			}
		}
	} else {
		CAM_ERR(CAM_FLASH, "Wrong Operation: %d", op);
		return -EINVAL;
	}

	if (flash_ctrl->switch_trigger)
		cam_res_mgr_led_trigger_event(
			flash_ctrl->switch_trigger,
			LED_SWITCH_ON);

	return 0;
}

int cam_flash_off(struct cam_flash_ctrl *flash_ctrl)
{
	int i = 0;

	if (!flash_ctrl) {
		CAM_ERR(CAM_FLASH, "Flash control Null");
		return -EINVAL;
	}

	for (i = 0; i < flash_ctrl->flash_num_sources; i++)
		if (flash_ctrl->flash_trigger[i])
			cam_res_mgr_led_trigger_event(
				flash_ctrl->flash_trigger[i],
				LED_OFF);

	for (i = 0; i < flash_ctrl->torch_num_sources; i++)
		if (flash_ctrl->torch_trigger[i])
			cam_res_mgr_led_trigger_event(
				flash_ctrl->torch_trigger[i],
				LED_OFF);

	if (flash_ctrl->switch_trigger)
		cam_res_mgr_led_trigger_event(flash_ctrl->switch_trigger,
			LED_SWITCH_OFF);

	flash_ctrl->flash_state = CAM_FLASH_STATE_START;
	return 0;
}

static int cam_flash_low(
	struct cam_flash_ctrl *flash_ctrl,
	struct cam_flash_frame_setting *flash_data)
{
	int i = 0, rc = 0;

	if (!flash_data) {
		CAM_ERR(CAM_FLASH, "Flash Data Null");
		return -EINVAL;
	}

	for (i = 0; i < flash_ctrl->flash_num_sources; i++)
		if (flash_ctrl->flash_trigger[i])
			cam_res_mgr_led_trigger_event(
				flash_ctrl->flash_trigger[i],
				LED_OFF);

	rc = cam_flash_ops(flash_ctrl, flash_data,
		CAMERA_SENSOR_FLASH_OP_FIRELOW);
	if (rc)
		CAM_ERR(CAM_FLASH, "Fire Torch failed: %d", rc);

	return rc;
}

static int cam_flash_high(
	struct cam_flash_ctrl *flash_ctrl,
	struct cam_flash_frame_setting *flash_data)
{
	int i = 0, rc = 0;

	if (!flash_data) {
		CAM_ERR(CAM_FLASH, "Flash Data Null");
		return -EINVAL;
	}

	for (i = 0; i < flash_ctrl->torch_num_sources; i++)
		if (flash_ctrl->torch_trigger[i])
			cam_res_mgr_led_trigger_event(
				flash_ctrl->torch_trigger[i],
				LED_OFF);

	rc = cam_flash_ops(flash_ctrl, flash_data,
		CAMERA_SENSOR_FLASH_OP_FIREHIGH);
	if (rc)
		CAM_ERR(CAM_FLASH, "Fire Flash Failed: %d", rc);

	return rc;
}

static int delete_i2c_req(struct cam_flash_ctrl *fctrl, uint64_t req_id)
{
	int i = 0, rc = 0;
	uint64_t top = 0, del_req_id = 0;

	if (req_id == 0) {
		if (fctrl->i2c_data.init_settings.is_settings_valid == true) {
			rc = delete_request(&fctrl->i2c_data.init_settings);
			if (rc) {
				CAM_WARN(CAM_FLASH,
					"Failed to delete Init i2c_setting: %d",
					rc);
				rc = 0;
			}
		}
		if (fctrl->i2c_data.config_settings.is_settings_valid == true) {
			rc = delete_request(&fctrl->i2c_data.config_settings);
			if (rc) {
				CAM_WARN(CAM_FLASH,
					"Failed to delete NRT i2c_setting: %d",
					rc);
				rc = 0;
			}
		}
	} else {
		for (i = 0; i < MAX_PER_FRAME_ARRAY; i++) {
			if ((req_id >=
				fctrl->i2c_data.per_frame[i].request_id) &&
				(top <
				fctrl->i2c_data.per_frame[i].request_id) &&
				(fctrl->i2c_data.per_frame[i].is_settings_valid
					== 1)) {
				del_req_id = top;
				top = fctrl->i2c_data.per_frame[i].request_id;
			}
		}

		if (top < req_id) {
			if ((((top % MAX_PER_FRAME_ARRAY) - (req_id %
				MAX_PER_FRAME_ARRAY)) >= BATCH_SIZE_MAX) ||
				(((top % MAX_PER_FRAME_ARRAY) - (req_id %
				MAX_PER_FRAME_ARRAY)) <= -BATCH_SIZE_MAX))
				del_req_id = req_id;
		}

		if (!del_req_id)
			return rc;

		CAM_DBG(CAM_FLASH, "top: %llu, del_req_id:%llu",
			top, del_req_id);

		for (i = 0; i < MAX_PER_FRAME_ARRAY; i++) {
			if ((del_req_id >
				 fctrl->i2c_data.per_frame[i].request_id) && (
				 fctrl->i2c_data.per_frame[i].is_settings_valid
					== 1)) {
				fctrl->i2c_data.per_frame[i].request_id = 0;
				rc = delete_request(
					&(fctrl->i2c_data.per_frame[i]));
				if (rc < 0)
					CAM_ERR(CAM_FLASH,
					"Delete request Fail:%lld rc:%d",
					del_req_id, rc);
			}
		}
	}
	return 0;
}

static int delete_req(struct cam_flash_ctrl *fctrl, uint64_t req_id)
{
	int i = 0;
	struct cam_flash_frame_setting *flash_data = NULL;
	uint64_t top = 0, del_req_id = 0;

	if (req_id == 0) {
		flash_data = &fctrl->nrt_info;
		if ((fctrl->nrt_info.cmn_attr.cmd_type ==
			CAMERA_SENSOR_FLASH_CMD_TYPE_WIDGET) ||
			(fctrl->nrt_info.cmn_attr.cmd_type ==
			CAMERA_SENSOR_FLASH_CMD_TYPE_RER)) {
			flash_data->cmn_attr.is_settings_valid = false;
			for (i = 0; i < flash_data->cmn_attr.count; i++)
				flash_data->led_current_ma[i] = 0;
		} else {
			fctrl->flash_init_setting.cmn_attr.is_settings_valid
				= false;
		}
	} else {
		for (i = 0; i < MAX_PER_FRAME_ARRAY; i++) {
			flash_data = &fctrl->per_frame[i];
			if (req_id >= flash_data->cmn_attr.request_id &&
				flash_data->cmn_attr.is_settings_valid
				== 1) {
				if (top < flash_data->cmn_attr.request_id) {
					del_req_id = top;
					top = flash_data->cmn_attr.request_id;
				} else if (top >
					flash_data->cmn_attr.request_id &&
					del_req_id <
					flash_data->cmn_attr.request_id) {
					del_req_id =
						flash_data->cmn_attr.request_id;
				}
			}
		}

		if (top < req_id) {
			if ((((top % MAX_PER_FRAME_ARRAY) - (req_id %
				MAX_PER_FRAME_ARRAY)) >= BATCH_SIZE_MAX) ||
				(((top % MAX_PER_FRAME_ARRAY) - (req_id %
				MAX_PER_FRAME_ARRAY)) <= -BATCH_SIZE_MAX))
				del_req_id = req_id;
		}

		if (!del_req_id)
			return 0;

		CAM_DBG(CAM_FLASH, "top: %llu, del_req_id:%llu",
			top, del_req_id);

		for (i = 0; i < MAX_PER_FRAME_ARRAY; i++) {
			flash_data = &fctrl->per_frame[i];
			if ((del_req_id ==
				flash_data->cmn_attr.request_id) &&
				(flash_data->cmn_attr.
					is_settings_valid == 1)) {
				CAM_DBG(CAM_FLASH, "Deleting request[%d] %llu",
					i, flash_data->cmn_attr.request_id);
				flash_data->cmn_attr.request_id = 0;
				flash_data->cmn_attr.is_settings_valid = false;
				flash_data->opcode = 0;
				for (i = 0; i < flash_data->cmn_attr.count; i++)
					flash_data->led_current_ma[i] = 0;
			}
		}
	}

	return 0;
}

static int32_t cam_flash_slaveInfo_pkt_parser(struct cam_flash_ctrl *fctrl,
	uint32_t *cmd_buf)
{
	int32_t rc = 0;
	struct cam_cmd_i2c_info *i2c_info;

	if (!fctrl || !cmd_buf) {
		CAM_ERR(CAM_FLASH, "Invalid Args");
		return -EINVAL;
	}

	i2c_info = (struct cam_cmd_i2c_info *)cmd_buf;
	if (fctrl->io_master_info.master_type == CCI_MASTER) {
		fctrl->io_master_info.cci_client->cci_i2c_master =
			fctrl->cci_i2c_master;
		fctrl->io_master_info.cci_client->i2c_freq_mode =
			i2c_info->i2c_freq_mode;
		fctrl->io_master_info.cci_client->sid =
			i2c_info->slave_addr >> 1;
		CAM_DBG(CAM_FLASH, "Slave addr: 0x%x Freq Mode: %d",
			i2c_info->slave_addr, i2c_info->i2c_freq_mode);
	} else if (fctrl->io_master_info.master_type == I2C_MASTER) {
		fctrl->io_master_info.client->addr = i2c_info->slave_addr;
		CAM_DBG(CAM_FLASH, "Slave addr: 0x%x", i2c_info->slave_addr);
	} else {
		CAM_ERR(CAM_FLASH, "Invalid Master type: %d",
			fctrl->io_master_info.master_type);
		 rc = -EINVAL;
	}

	return rc;
}

int cam_flash_i2c_apply_setting(struct cam_flash_ctrl *fctrl,
	uint64_t req_id)
{
	struct i2c_settings_list *i2c_list;
	struct i2c_settings_array *i2c_set = NULL;
	int frame_offset = 0, rc = 0;

	if (req_id == 0) {
		/* NonRealTime Init settings*/
		if (fctrl->i2c_data.init_settings.is_settings_valid == true) {
			list_for_each_entry(i2c_list,
				&(fctrl->i2c_data.init_settings.list_head),
				list) {
				rc = cam_i2c_apply_setting
					(&(fctrl->io_master_info), i2c_list);
				if (rc) {
					CAM_ERR(CAM_FLASH,
					"Failed to apply init settings: %d",
					rc);
					return rc;
				}
			}
		}
		/* NonRealTime (Widget/RER/INIT_FIRE settings) */
		if (fctrl->i2c_data.config_settings.is_settings_valid == true) {
			list_for_each_entry(i2c_list,
				&(fctrl->i2c_data.config_settings.list_head),
				list) {
				rc = cam_i2c_apply_setting
					(&(fctrl->io_master_info), i2c_list);
				if (rc) {
					CAM_ERR(CAM_FLASH,
					"Failed to apply NRT settings: %d", rc);
					return rc;
				}
			}
		}
	} else {
		/* RealTime */
		frame_offset = req_id % MAX_PER_FRAME_ARRAY;
		i2c_set = &fctrl->i2c_data.per_frame[frame_offset];
		if (i2c_set->is_settings_valid == true &&
			i2c_set->request_id == req_id) {
			list_for_each_entry(i2c_list,
				&(i2c_set->list_head), list) {
				rc = cam_i2c_apply_setting(
					&(fctrl->io_master_info), i2c_list);
				if (rc) {
					CAM_ERR(CAM_FLASH,
					"Failed to apply settings: %d", rc);
					return rc;
				}
			}
		}
	}

	delete_i2c_req(fctrl, req_id);
	return rc;
}

int cam_flash_pmic_apply_setting(struct cam_flash_ctrl *fctrl,
	uint64_t req_id)
{
	int rc = 0, i = 0;
	int frame_offset = 0;
	uint16_t num_iterations;
	struct cam_flash_frame_setting *flash_data = NULL;

	if (req_id == 0) {
		if (fctrl->nrt_info.cmn_attr.cmd_type ==
			CAMERA_SENSOR_FLASH_CMD_TYPE_INIT_FIRE) {
			flash_data = &fctrl->nrt_info;
			if (flash_data->opcode ==
				CAMERA_SENSOR_FLASH_OP_FIREHIGH) {
				if (fctrl->flash_state !=
					CAM_FLASH_STATE_CONFIG) {
					CAM_WARN(CAM_FLASH,
					"Cannot apply Start Dev:Prev state: %d",
					fctrl->flash_state);
					return rc;
				}
				rc = cam_flash_prepare(fctrl, true);
				if (rc) {
					CAM_ERR(CAM_FLASH,
					"Enable Regulator Failed rc = %d", rc);
					return rc;
				}
				rc = cam_flash_high(fctrl, flash_data);
				if (rc)
					CAM_ERR(CAM_FLASH,
						"FLASH ON failed : %d",
						rc);
			}
			if (flash_data->opcode ==
				CAMERA_SENSOR_FLASH_OP_OFF) {
				rc = cam_flash_off(fctrl);
				if (rc) {
					CAM_ERR(CAM_FLASH,
					"LED OFF FAILED: %d",
					rc);
					return rc;
				}
				if ((fctrl->flash_state ==
					CAM_FLASH_STATE_START) &&
					(fctrl->is_regulator_enabled == true)) {
					rc = cam_flash_prepare(fctrl, false);
					if (rc)
						CAM_ERR(CAM_FLASH,
						"Disable Regulator failed: %d",
						rc);
				}
			}
		} else if (fctrl->nrt_info.cmn_attr.cmd_type ==
			CAMERA_SENSOR_FLASH_CMD_TYPE_WIDGET) {
			flash_data = &fctrl->nrt_info;
			if (flash_data->opcode ==
				CAMERA_SENSOR_FLASH_OP_FIRELOW) {
				rc = cam_flash_low(fctrl, flash_data);
				if (rc) {
					CAM_ERR(CAM_FLASH,
						"Torch ON failed : %d",
						rc);
					goto nrt_del_req;
				}
			} else if (flash_data->opcode ==
				CAMERA_SENSOR_FLASH_OP_OFF) {
				rc = cam_flash_off(fctrl);
				if (rc)
					CAM_ERR(CAM_FLASH,
					"LED off failed: %d",
					rc);
			}
		} else if (fctrl->nrt_info.cmn_attr.cmd_type ==
			CAMERA_SENSOR_FLASH_CMD_TYPE_RER) {
			flash_data = &fctrl->nrt_info;
			if (fctrl->flash_state != CAM_FLASH_STATE_START) {
				rc = cam_flash_off(fctrl);
				if (rc) {
					CAM_ERR(CAM_FLASH,
						"Flash off failed: %d",
						rc);
					goto nrt_del_req;
				}
			}
			num_iterations = flash_data->num_iterations;
			for (i = 0; i < num_iterations; i++) {
				/* Turn On Torch */
				if (fctrl->flash_state ==
					CAM_FLASH_STATE_START) {
					rc = cam_flash_low(fctrl, flash_data);
					if (rc) {
						CAM_ERR(CAM_FLASH,
							"Fire Torch Failed");
						goto nrt_del_req;
					}

					usleep_range(
					flash_data->led_on_delay_ms * 1000,
					flash_data->led_on_delay_ms * 1000 +
						100);
				}
				/* Turn Off Torch */
				rc = cam_flash_off(fctrl);
				if (rc) {
					CAM_ERR(CAM_FLASH,
						"Flash off failed: %d", rc);
					continue;
				}
				fctrl->flash_state = CAM_FLASH_STATE_START;
				usleep_range(
				flash_data->led_off_delay_ms * 1000,
				flash_data->led_off_delay_ms * 1000 + 100);
			}
		}
	} else {
		frame_offset = req_id % MAX_PER_FRAME_ARRAY;
		flash_data = &fctrl->per_frame[frame_offset];

		if ((flash_data->opcode == CAMERA_SENSOR_FLASH_OP_FIREHIGH) &&
			(flash_data->cmn_attr.is_settings_valid) &&
			(flash_data->cmn_attr.request_id == req_id)) {
			/* Turn On Flash */
			if (fctrl->flash_state == CAM_FLASH_STATE_START) {
				rc = cam_flash_high(fctrl, flash_data);
				if (rc) {
					CAM_ERR(CAM_FLASH,
						"Flash ON failed: rc= %d",
						rc);
					goto apply_setting_err;
				}
			}
		} else if ((flash_data->opcode ==
			CAMERA_SENSOR_FLASH_OP_FIRELOW) &&
			(flash_data->cmn_attr.is_settings_valid) &&
			(flash_data->cmn_attr.request_id == req_id)) {
			/* Turn On Torch */
			if (fctrl->flash_state == CAM_FLASH_STATE_START) {
				rc = cam_flash_low(fctrl, flash_data);
				if (rc) {
					CAM_ERR(CAM_FLASH,
						"Torch ON failed: rc= %d",
						rc);
					goto apply_setting_err;
				}
			}
		} else if ((flash_data->opcode == CAMERA_SENSOR_FLASH_OP_OFF) &&
			(flash_data->cmn_attr.is_settings_valid) &&
			(flash_data->cmn_attr.request_id == req_id)) {
			rc = cam_flash_off(fctrl);
			if (rc) {
				CAM_ERR(CAM_FLASH,
					"Flash off failed %d", rc);
				goto apply_setting_err;
			}
		} else {
			CAM_DBG(CAM_FLASH, "NOP opcode: req_id: %u", req_id);
		}
	}

nrt_del_req:
	delete_req(fctrl, req_id);
apply_setting_err:
	return rc;
}

int cam_flash_i2c_pkt_parser(struct cam_flash_ctrl *fctrl, void *arg)
{
	int rc = 0, i = 0;
	uint64_t generic_ptr;
	uint32_t total_cmd_buf_in_bytes = 0;
	uint32_t processed_cmd_buf_in_bytes = 0;
	uint16_t cmd_length_in_bytes = 0;
	uint32_t *cmd_buf =  NULL;
	uint32_t *offset = NULL;
	uint32_t frm_offset = 0;
	size_t len_of_buffer;
	struct cam_flash_init *flash_init = NULL;
	struct common_header  *cmn_hdr = NULL;
	struct cam_control *ioctl_ctrl = NULL;
	struct cam_packet *csl_packet = NULL;
	struct cam_cmd_buf_desc *cmd_desc = NULL;
	struct cam_config_dev_cmd config;
	struct cam_req_mgr_add_request add_req;
	struct i2c_data_settings *i2c_data = NULL;
	struct i2c_settings_array *i2c_reg_settings = NULL;
	struct cam_sensor_power_ctrl_t *power_info = NULL;

	if (!fctrl || !arg) {
		CAM_ERR(CAM_FLASH, "fctrl/arg is NULL");
		return -EINVAL;
	}
	/* getting CSL Packet */
	ioctl_ctrl = (struct cam_control *)arg;

	if (copy_from_user((&config), (void __user *) ioctl_ctrl->handle,
		sizeof(config))) {
		CAM_ERR(CAM_FLASH, "Copy cmd handle from user failed");
		rc = -EFAULT;
		return rc;
	}

	rc = cam_mem_get_cpu_buf(config.packet_handle,
		(uint64_t *)&generic_ptr, &len_of_buffer);
	if (rc) {
		CAM_ERR(CAM_FLASH, "Failed in getting the buffer : %d", rc);
		return rc;
	}

	if (config.offset > len_of_buffer) {
		CAM_ERR(CAM_FLASH,
			"offset is out of bounds: offset: %lld len: %zu",
			config.offset, len_of_buffer);
		return -EINVAL;
	}

	/* Add offset to the flash csl header */
	csl_packet = (struct cam_packet *)(generic_ptr + config.offset);
	switch (csl_packet->header.op_code & 0xFFFFFF) {
	case CAM_FLASH_PACKET_OPCODE_INIT: {
         CAM_INFO(CAM_FLASH, "CAM_FLASH_PACKET_OPCODE_INIT enter");
		/* INIT packet*/
		offset = (uint32_t *)((uint8_t *)&csl_packet->payload +
			csl_packet->cmd_buf_offset);
		cmd_desc = (struct cam_cmd_buf_desc *)(offset);

		/* Loop through multiple command buffers */
		for (i = 1; i < csl_packet->num_cmd_buf; i++) {
			total_cmd_buf_in_bytes = cmd_desc[i].length;
			processed_cmd_buf_in_bytes = 0;
			if (!total_cmd_buf_in_bytes)
				continue;
			rc = cam_mem_get_cpu_buf(cmd_desc[i].mem_handle,
				(uint64_t *)&generic_ptr, &len_of_buffer);
			if (rc < 0) {
				CAM_ERR(CAM_FLASH, "Failed to get cpu buf");
				return rc;
			}
			cmd_buf = (uint32_t *)generic_ptr;
			if (!cmd_buf) {
				CAM_ERR(CAM_FLASH, "invalid cmd buf");
				return -EINVAL;
			}
			cmd_buf += cmd_desc[i].offset / sizeof(uint32_t);
			cmn_hdr = (struct common_header *)cmd_buf;

			/* Loop through cmd formats in one cmd buffer */
			CAM_DBG(CAM_FLASH,
				"command Type: %d,Processed: %d,Total: %d",
				cmn_hdr->cmd_type, processed_cmd_buf_in_bytes,
				total_cmd_buf_in_bytes);
			switch (cmn_hdr->cmd_type) {
			case CAMERA_SENSOR_FLASH_CMD_TYPE_INIT_INFO:
				flash_init = (struct cam_flash_init *)cmd_buf;
				fctrl->flash_type = flash_init->flash_type;
				cmd_length_in_bytes =
					sizeof(struct cam_flash_init);
				processed_cmd_buf_in_bytes +=
					cmd_length_in_bytes;
				cmd_buf += cmd_length_in_bytes/
						sizeof(uint32_t);
				break;
			case CAMERA_SENSOR_CMD_TYPE_I2C_INFO:
				rc = cam_flash_slaveInfo_pkt_parser(
					fctrl, cmd_buf);
				if (rc < 0) {
					CAM_ERR(CAM_FLASH,
					"Failed parsing slave info: rc: %d",
					rc);
					return rc;
				}
				cmd_length_in_bytes =
					sizeof(struct cam_cmd_i2c_info);
				processed_cmd_buf_in_bytes +=
					cmd_length_in_bytes;
				cmd_buf += cmd_length_in_bytes/
						sizeof(uint32_t);
				break;
			case CAMERA_SENSOR_CMD_TYPE_PWR_UP:
			case CAMERA_SENSOR_CMD_TYPE_PWR_DOWN:
				CAM_DBG(CAM_FLASH,
					"Received power settings");
				cmd_length_in_bytes =
					total_cmd_buf_in_bytes;
				rc = cam_sensor_update_power_settings(
					cmd_buf,
					total_cmd_buf_in_bytes,
					&fctrl->power_info);
				processed_cmd_buf_in_bytes +=
					cmd_length_in_bytes;
				cmd_buf += cmd_length_in_bytes/
						sizeof(uint32_t);
				if (rc) {
					CAM_ERR(CAM_FLASH,
					"Failed update power settings");
					return rc;
				}
				break;
			default:
				CAM_DBG(CAM_FLASH,
					"Received initSettings");
				i2c_data = &(fctrl->i2c_data);
				i2c_reg_settings =
					&fctrl->i2c_data.init_settings;

				i2c_reg_settings->request_id = 0;
				i2c_reg_settings->is_settings_valid = 1;
				rc = cam_sensor_i2c_command_parser(
					&fctrl->io_master_info,
					i2c_reg_settings,
					&cmd_desc[i], 1);
				if (rc < 0) {
					CAM_ERR(CAM_FLASH,
					"pkt parsing failed: %d", rc);
					return rc;
				}
				cmd_length_in_bytes =
					cmd_desc[i].length;
				processed_cmd_buf_in_bytes +=
					cmd_length_in_bytes;
				cmd_buf += cmd_length_in_bytes/
						sizeof(uint32_t);

				break;
			}
		}
		power_info = &fctrl->power_info;
		if (!power_info) {
			CAM_ERR(CAM_FLASH, "Power_info is NULL");
			return -EINVAL;
		}

		/* Parse and fill vreg params for power up settings */
		rc = msm_camera_fill_vreg_params(&fctrl->soc_info,
			power_info->power_setting,
			power_info->power_setting_size);
		if (rc) {
			CAM_ERR(CAM_FLASH,
				"failed to fill vreg params for power up rc:%d",
				rc);
			return rc;
		}

		/* Parse and fill vreg params for power down settings*/
		rc = msm_camera_fill_vreg_params(
			&fctrl->soc_info,
			power_info->power_down_setting,
			power_info->power_down_setting_size);
		if (rc) {
			CAM_ERR(CAM_FLASH,
				"failed to fill vreg params power down rc:%d",
				rc);
			return rc;
		}

		rc = fctrl->func_tbl.power_ops(fctrl, true);
		if (rc) {
			CAM_ERR(CAM_FLASH,
				"Enable Regulator Failed rc = %d", rc);
			return rc;
		}

		rc = fctrl->func_tbl.apply_setting(fctrl, 0);
		if (rc) {
			CAM_ERR(CAM_FLASH, "cannot apply settings rc = %d", rc);
			return rc;
		}

		fctrl->flash_state = CAM_FLASH_STATE_CONFIG;
		break;
	}
	case CAM_FLASH_PACKET_OPCODE_SET_OPS: {
		offset = (uint32_t *)((uint8_t *)&csl_packet->payload +
			csl_packet->cmd_buf_offset);
		frm_offset = csl_packet->header.request_id %
			MAX_PER_FRAME_ARRAY;
		/* add support for handling i2c_data*/
		i2c_reg_settings =
			&fctrl->i2c_data.per_frame[frm_offset];
		if (i2c_reg_settings->is_settings_valid == true) {
			i2c_reg_settings->request_id = 0;
			i2c_reg_settings->is_settings_valid = false;
			goto update_req_mgr;
		}
		i2c_reg_settings->is_settings_valid = true;
		i2c_reg_settings->request_id =
			csl_packet->header.request_id;
		cmd_desc = (struct cam_cmd_buf_desc *)(offset);
		rc = cam_sensor_i2c_command_parser(
			&fctrl->io_master_info,
			i2c_reg_settings, cmd_desc, 1);
		if (rc) {
			CAM_ERR(CAM_FLASH,
			"Failed in parsing i2c packets");
			return rc;
		}
		break;
	}
	case CAM_FLASH_PACKET_OPCODE_NON_REALTIME_SET_OPS: {
		offset = (uint32_t *)((uint8_t *)&csl_packet->payload +
			csl_packet->cmd_buf_offset);

		/* add support for handling i2c_data*/
		i2c_reg_settings = &fctrl->i2c_data.config_settings;
		if (i2c_reg_settings->is_settings_valid == true) {
			i2c_reg_settings->request_id = 0;
			i2c_reg_settings->is_settings_valid = false;

			rc = delete_request(i2c_reg_settings);
			if (rc) {
				CAM_ERR(CAM_FLASH,
				"Failed in Deleting the err: %d", rc);
				return rc;
			}
		}
		i2c_reg_settings->is_settings_valid = true;
		i2c_reg_settings->request_id =
			csl_packet->header.request_id;
		cmd_desc = (struct cam_cmd_buf_desc *)(offset);
		rc = cam_sensor_i2c_command_parser(
			&fctrl->io_master_info,
			i2c_reg_settings, cmd_desc, 1);
		if (rc) {
			CAM_ERR(CAM_FLASH,
			"Failed in parsing i2c NRT packets");
			return rc;
		}
		rc = fctrl->func_tbl.apply_setting(fctrl, 0);
		if (rc) {
            CAM_ERR(CAM_FLASH,
            "Apply setting failed: %d", rc);
            return rc;
        }
		break;
    }
	case CAM_PKT_NOP_OPCODE: {
		if ((fctrl->flash_state == CAM_FLASH_STATE_INIT) ||
			(fctrl->flash_state == CAM_FLASH_STATE_ACQUIRE)) {
			CAM_WARN(CAM_FLASH,
				"Rxed NOP packets without linking");
			frm_offset = csl_packet->header.request_id %
				MAX_PER_FRAME_ARRAY;
			fctrl->i2c_data.per_frame[frm_offset].is_settings_valid
				= false;
			return 0;
		}

		CAM_DBG(CAM_FLASH, "NOP Packet is Received: req_id: %u",
			csl_packet->header.request_id);
		goto update_req_mgr;
	}
	default:
		CAM_ERR(CAM_FLASH, "Wrong Opcode : %d",
			(csl_packet->header.op_code & 0xFFFFFF));
		return -EINVAL;
	}
update_req_mgr:
	if (((csl_packet->header.op_code  & 0xFFFFF) ==
		CAM_PKT_NOP_OPCODE) ||
		((csl_packet->header.op_code & 0xFFFFF) ==
		CAM_FLASH_PACKET_OPCODE_SET_OPS)) {
		add_req.link_hdl = fctrl->bridge_intf.link_hdl;
		add_req.req_id = csl_packet->header.request_id;
		add_req.dev_hdl = fctrl->bridge_intf.device_hdl;

		if ((csl_packet->header.op_code & 0xFFFFF) ==
			CAM_FLASH_PACKET_OPCODE_SET_OPS)
			add_req.skip_before_applying = 1;
		else
			add_req.skip_before_applying = 0;

		if (fctrl->bridge_intf.crm_cb &&
			fctrl->bridge_intf.crm_cb->add_req)
			fctrl->bridge_intf.crm_cb->add_req(&add_req);
		CAM_DBG(CAM_FLASH, "add req to req_mgr= %lld", add_req.req_id);
	}
	return rc;
}

int cam_flash_pmic_pkt_parser(struct cam_flash_ctrl *fctrl, void *arg)
{
	int rc = 0, i = 0;
	uint64_t generic_ptr;
	uint32_t *cmd_buf =  NULL;
	uint32_t *offset = NULL;
	uint32_t frm_offset = 0;
	size_t len_of_buffer;
	struct cam_control *ioctl_ctrl = NULL;
	struct cam_packet *csl_packet = NULL;
	struct cam_cmd_buf_desc *cmd_desc = NULL;
	struct common_header *cmn_hdr;
	struct cam_config_dev_cmd config;
	struct cam_req_mgr_add_request add_req;
	struct cam_flash_init *cam_flash_info = NULL;
	struct cam_flash_set_rer *flash_rer_info = NULL;
	struct cam_flash_set_on_off *flash_operation_info = NULL;
	struct cam_flash_query_curr *flash_query_info = NULL;
	struct cam_flash_frame_setting *flash_data = NULL;

	if (!fctrl || !arg) {
		CAM_ERR(CAM_FLASH, "fctrl/arg is NULL");
		return -EINVAL;
	}
	/* getting CSL Packet */
	ioctl_ctrl = (struct cam_control *)arg;

	if (copy_from_user((&config), (void __user *) ioctl_ctrl->handle,
		sizeof(config))) {
		CAM_ERR(CAM_FLASH, "Copy cmd handle from user failed");
		rc = -EFAULT;
		return rc;
	}

	rc = cam_mem_get_cpu_buf(config.packet_handle,
		(uint64_t *)&generic_ptr, &len_of_buffer);
	if (rc) {
		CAM_ERR(CAM_FLASH, "Failed in getting the buffer : %d", rc);
		return rc;
	}

	if (config.offset > len_of_buffer) {
		CAM_ERR(CAM_FLASH,
			"offset is out of bounds: offset: %lld len: %zu",
			config.offset, len_of_buffer);
		return -EINVAL;
	}

	/* Add offset to the flash csl header */
	csl_packet = (struct cam_packet *)(generic_ptr + config.offset);

	switch (csl_packet->header.op_code & 0xFFFFFF) {
	case CAM_FLASH_PACKET_OPCODE_INIT: {
		/* INIT packet*/
		offset = (uint32_t *)((uint8_t *)&csl_packet->payload +
			csl_packet->cmd_buf_offset);
		cmd_desc = (struct cam_cmd_buf_desc *)(offset);
		rc = cam_mem_get_cpu_buf(cmd_desc->mem_handle,
			(uint64_t *)&generic_ptr, &len_of_buffer);
		cmd_buf = (uint32_t *)((uint8_t *)generic_ptr +
			cmd_desc->offset);
		cam_flash_info = (struct cam_flash_init *)cmd_buf;

		switch (cam_flash_info->cmd_type) {
		case CAMERA_SENSOR_FLASH_CMD_TYPE_INIT_INFO: {
			CAM_DBG(CAM_FLASH, "INIT_INFO CMD CALLED");
			fctrl->flash_init_setting.cmn_attr.request_id = 0;
			fctrl->flash_init_setting.cmn_attr.is_settings_valid =
				true;
			fctrl->flash_type = cam_flash_info->flash_type;
			fctrl->is_regulator_enabled = false;
			fctrl->nrt_info.cmn_attr.cmd_type =
				CAMERA_SENSOR_FLASH_CMD_TYPE_INIT_INFO;

			rc = fctrl->func_tbl.power_ops(fctrl, true);
			if (rc) {
				CAM_ERR(CAM_FLASH,
					"Enable Regulator Failed rc = %d", rc);
				return rc;
			}

			fctrl->flash_state =
				CAM_FLASH_STATE_CONFIG;
			break;
		}
		case CAMERA_SENSOR_FLASH_CMD_TYPE_INIT_FIRE: {
			CAM_DBG(CAM_FLASH, "INIT_FIRE Operation");

			flash_operation_info =
				(struct cam_flash_set_on_off *) cmd_buf;
			fctrl->nrt_info.cmn_attr.count =
				flash_operation_info->count;
			fctrl->nrt_info.cmn_attr.request_id = 0;
			fctrl->nrt_info.opcode =
				flash_operation_info->opcode;
			fctrl->nrt_info.cmn_attr.cmd_type =
				CAMERA_SENSOR_FLASH_CMD_TYPE_INIT_FIRE;
			for (i = 0;
				i < flash_operation_info->count; i++)
				fctrl->nrt_info.led_current_ma[i] =
				flash_operation_info->led_current_ma[i];

			mutex_lock(&fctrl->flash_wq_mutex);
			rc = fctrl->func_tbl.apply_setting(fctrl, 0);
			if (rc)
				CAM_ERR(CAM_FLASH,
					"Apply setting failed: %d",
					rc);
			mutex_unlock(&fctrl->flash_wq_mutex);

			fctrl->flash_state = CAM_FLASH_STATE_CONFIG;
			break;
		}
		default:
			CAM_ERR(CAM_FLASH, "Wrong cmd_type = %d",
				cam_flash_info->cmd_type);
			return -EINVAL;
		}
		break;
	}
	case CAM_FLASH_PACKET_OPCODE_SET_OPS: {
		offset = (uint32_t *)((uint8_t *)&csl_packet->payload +
			csl_packet->cmd_buf_offset);
		frm_offset = csl_packet->header.request_id %
			MAX_PER_FRAME_ARRAY;
		flash_data = &fctrl->per_frame[frm_offset];

		if (flash_data->cmn_attr.is_settings_valid == true) {
			flash_data->cmn_attr.request_id = 0;
			flash_data->cmn_attr.is_settings_valid = false;
			for (i = 0; i < flash_data->cmn_attr.count; i++)
				flash_data->led_current_ma[i] = 0;
		}

		flash_data->cmn_attr.request_id = csl_packet->header.request_id;
		flash_data->cmn_attr.is_settings_valid = true;
		cmd_desc = (struct cam_cmd_buf_desc *)(offset);
		rc = cam_mem_get_cpu_buf(cmd_desc->mem_handle,
			(uint64_t *)&generic_ptr, &len_of_buffer);
		cmd_buf = (uint32_t *)((uint8_t *)generic_ptr +
			cmd_desc->offset);

		if (!cmd_buf)
			return -EINVAL;

		cmn_hdr = (struct common_header *)cmd_buf;

		switch (cmn_hdr->cmd_type) {
		case CAMERA_SENSOR_FLASH_CMD_TYPE_FIRE: {
			CAM_DBG(CAM_FLASH,
				"CAMERA_SENSOR_FLASH_CMD_TYPE_FIRE cmd called");
			if ((fctrl->flash_state == CAM_FLASH_STATE_INIT) ||
				(fctrl->flash_state ==
					CAM_FLASH_STATE_ACQUIRE)) {
				CAM_WARN(CAM_FLASH,
					"Rxed Flash fire ops without linking");
				flash_data->cmn_attr.is_settings_valid = false;
				return 0;
			}

			flash_operation_info =
				(struct cam_flash_set_on_off *) cmd_buf;
			if (!flash_operation_info) {
				CAM_ERR(CAM_FLASH,
					"flash_operation_info Null");
				return -EINVAL;
			}

			flash_data->opcode = flash_operation_info->opcode;
			flash_data->cmn_attr.count =
				flash_operation_info->count;
			for (i = 0; i < flash_operation_info->count; i++)
				flash_data->led_current_ma[i]
				= flash_operation_info->led_current_ma[i];
		}
		break;
		default:
			CAM_ERR(CAM_FLASH, "Wrong cmd_type = %d",
				cmn_hdr->cmd_type);
			return -EINVAL;
		}
		break;
	}
	case CAM_FLASH_PACKET_OPCODE_NON_REALTIME_SET_OPS: {
		offset = (uint32_t *)((uint8_t *)&csl_packet->payload +
			csl_packet->cmd_buf_offset);
		fctrl->nrt_info.cmn_attr.is_settings_valid = true;
		cmd_desc = (struct cam_cmd_buf_desc *)(offset);
		rc = cam_mem_get_cpu_buf(cmd_desc->mem_handle,
		(uint64_t *)&generic_ptr, &len_of_buffer);
		cmd_buf = (uint32_t *)((uint8_t *)generic_ptr +
			cmd_desc->offset);
		cmn_hdr = (struct common_header *)cmd_buf;

		switch (cmn_hdr->cmd_type) {
		case CAMERA_SENSOR_FLASH_CMD_TYPE_WIDGET: {
			CAM_DBG(CAM_FLASH, "Widget Flash Operation");
			flash_operation_info =
				(struct cam_flash_set_on_off *) cmd_buf;
			fctrl->nrt_info.cmn_attr.count =
				flash_operation_info->count;
			fctrl->nrt_info.cmn_attr.request_id = 0;
			fctrl->nrt_info.opcode =
				flash_operation_info->opcode;
			fctrl->nrt_info.cmn_attr.cmd_type =
				CAMERA_SENSOR_FLASH_CMD_TYPE_WIDGET;

			for (i = 0; i < flash_operation_info->count; i++)
				fctrl->nrt_info.led_current_ma[i] =
					flash_operation_info->led_current_ma[i];

			mutex_lock(&fctrl->flash_wq_mutex);
			rc = fctrl->func_tbl.apply_setting(fctrl, 0);
			if (rc)
				CAM_ERR(CAM_FLASH, "Apply setting failed: %d",
					rc);
			mutex_unlock(&fctrl->flash_wq_mutex);
			return rc;
		}
		case CAMERA_SENSOR_FLASH_CMD_TYPE_QUERYCURR: {
			int query_curr_ma = 0;

			flash_query_info =
				(struct cam_flash_query_curr *)cmd_buf;

			rc = qpnp_flash_led_prepare(fctrl->switch_trigger,
					QUERY_MAX_CURRENT, &query_curr_ma);
			CAM_DBG(CAM_FLASH, "query_curr_ma = %d",
				query_curr_ma);
			if (rc) {
				CAM_ERR(CAM_FLASH,
				"Query current failed with rc=%d", rc);
				return rc;
			}
			flash_query_info->query_current_ma = query_curr_ma;
			break;
		}
		case CAMERA_SENSOR_FLASH_CMD_TYPE_RER: {
			rc = 0;
			flash_rer_info = (struct cam_flash_set_rer *)cmd_buf;
			fctrl->nrt_info.cmn_attr.cmd_type =
				CAMERA_SENSOR_FLASH_CMD_TYPE_RER;
			fctrl->nrt_info.opcode = flash_rer_info->opcode;
			fctrl->nrt_info.cmn_attr.count = flash_rer_info->count;
			fctrl->nrt_info.cmn_attr.request_id = 0;
			fctrl->nrt_info.num_iterations =
				flash_rer_info->num_iteration;
			fctrl->nrt_info.led_on_delay_ms =
				flash_rer_info->led_on_delay_ms;
			fctrl->nrt_info.led_off_delay_ms =
				flash_rer_info->led_off_delay_ms;

			for (i = 0; i < flash_rer_info->count; i++)
				fctrl->nrt_info.led_current_ma[i] =
					flash_rer_info->led_current_ma[i];


			mutex_lock(&fctrl->flash_wq_mutex);
			rc = fctrl->func_tbl.apply_setting(fctrl, 0);
			if (rc)
				CAM_ERR(CAM_FLASH, "apply_setting failed: %d",
					rc);
			mutex_unlock(&fctrl->flash_wq_mutex);
			return rc;
		}
		default:
			CAM_ERR(CAM_FLASH, "Wrong cmd_type : %d",
				cmn_hdr->cmd_type);
			return -EINVAL;
		}
		break;
	}
	case CAM_PKT_NOP_OPCODE: {
		if ((fctrl->flash_state == CAM_FLASH_STATE_INIT) ||
			(fctrl->flash_state == CAM_FLASH_STATE_ACQUIRE)) {
			CAM_WARN(CAM_FLASH,
				"Rxed NOP packets without linking");
			frm_offset = csl_packet->header.request_id %
				MAX_PER_FRAME_ARRAY;
			fctrl->per_frame[frm_offset].cmn_attr.is_settings_valid
				= false;
			return 0;
		}

		CAM_DBG(CAM_FLASH, "NOP Packet is Received: req_id: %lu",
			csl_packet->header.request_id);
		goto update_req_mgr;
	}
	default:
		CAM_ERR(CAM_FLASH, "Wrong Opcode : %d",
			(csl_packet->header.op_code & 0xFFFFFF));
		return -EINVAL;
	}
update_req_mgr:
	if (((csl_packet->header.op_code  & 0xFFFFF) ==
		CAM_PKT_NOP_OPCODE) ||
		((csl_packet->header.op_code & 0xFFFFF) ==
		CAM_FLASH_PACKET_OPCODE_SET_OPS)) {
		add_req.link_hdl = fctrl->bridge_intf.link_hdl;
		add_req.req_id = csl_packet->header.request_id;
		add_req.dev_hdl = fctrl->bridge_intf.device_hdl;

		if ((csl_packet->header.op_code & 0xFFFFF) ==
			CAM_FLASH_PACKET_OPCODE_SET_OPS)
			add_req.skip_before_applying = 1;
		else
			add_req.skip_before_applying = 0;

		if (fctrl->bridge_intf.crm_cb &&
			fctrl->bridge_intf.crm_cb->add_req)
			fctrl->bridge_intf.crm_cb->add_req(&add_req);
		CAM_DBG(CAM_FLASH, "add req to req_mgr= %lld", add_req.req_id);
	}

	return rc;
}

int cam_flash_publish_dev_info(struct cam_req_mgr_device_info *info)
{
	info->dev_id = CAM_REQ_MGR_DEVICE_FLASH;
	strlcpy(info->name, CAM_FLASH_NAME, sizeof(info->name));
	info->p_delay = CAM_FLASH_PIPELINE_DELAY;
	info->trigger = CAM_TRIGGER_POINT_SOF;
	return 0;
}

int cam_flash_establish_link(struct cam_req_mgr_core_dev_link_setup *link)
{
	struct cam_flash_ctrl *fctrl = NULL;

	if (!link)
		return -EINVAL;

	fctrl = (struct cam_flash_ctrl *)cam_get_device_priv(link->dev_hdl);
	if (!fctrl) {
		CAM_ERR(CAM_FLASH, " Device data is NULL");
		return -EINVAL;
	}

	if (link->link_enable) {
		fctrl->bridge_intf.link_hdl = link->link_hdl;
		fctrl->bridge_intf.crm_cb = link->crm_cb;
	} else {
		fctrl->bridge_intf.link_hdl = -1;
		fctrl->bridge_intf.crm_cb = NULL;
	}

	return 0;
}

int cam_flash_release_dev(struct cam_flash_ctrl *fctrl)
{
	int rc = 0;

	if (fctrl->bridge_intf.device_hdl != 1) {
		rc = cam_destroy_device_hdl(fctrl->bridge_intf.device_hdl);
		if (rc)
			CAM_ERR(CAM_FLASH,
				"Failed in destroying device handle rc = %d",
				rc);
		fctrl->bridge_intf.device_hdl = -1;
		fctrl->bridge_intf.link_hdl = -1;
		fctrl->bridge_intf.session_hdl = -1;
	}

	return rc;
}

void cam_flash_shutdown(struct cam_flash_ctrl *fctrl)
{
	int rc;

	if (fctrl->flash_state == CAM_FLASH_STATE_INIT)
		return;

	if ((fctrl->flash_state == CAM_FLASH_STATE_CONFIG) ||
		(fctrl->flash_state == CAM_FLASH_STATE_START)) {
		rc = fctrl->func_tbl.power_ops(fctrl, false);
		if (rc)
			CAM_ERR(CAM_FLASH, "Stop Failed rc: %d", rc);
	}

	rc = cam_flash_release_dev(fctrl);
	if (rc)
		CAM_ERR(CAM_FLASH, "Release failed rc: %d", rc);

	fctrl->flash_state = CAM_FLASH_STATE_INIT;
}

int cam_flash_apply_request(struct cam_req_mgr_apply_request *apply)
{
	int rc = 0;
	struct cam_flash_ctrl *fctrl = NULL;

	if (!apply)
		return -EINVAL;

	fctrl = (struct cam_flash_ctrl *) cam_get_device_priv(apply->dev_hdl);
	if (!fctrl) {
		CAM_ERR(CAM_FLASH, "Device data is NULL");
		rc = -EINVAL;
		goto free_resource;
	}

	if (!(apply->report_if_bubble)) {
		mutex_lock(&fctrl->flash_wq_mutex);
		rc = fctrl->func_tbl.apply_setting(fctrl, apply->request_id);
		if (rc)
			CAM_ERR(CAM_FLASH, "apply_setting failed with rc=%d",
				rc);
		mutex_unlock(&fctrl->flash_wq_mutex);
	}

free_resource:
	return rc;
}