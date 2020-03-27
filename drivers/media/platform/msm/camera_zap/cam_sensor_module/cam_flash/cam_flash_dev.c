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
 *
 */

#include <linux/module.h>
#include "cam_flash_dev.h"
#include "cam_flash_soc.h"
#include "cam_flash_core.h"

static int32_t cam_flash_driver_cmd(struct cam_flash_ctrl *fctrl,
		void *arg, struct cam_flash_private_soc *soc_private)
{
	int rc = 0;
	int i = 0;
	struct cam_control *cmd = (struct cam_control *)arg;

	if (!fctrl || !arg) {
		CAM_ERR(CAM_FLASH, "fctrl/arg is NULL with arg:%pK fctrl%pK",
			fctrl, arg);
		return -EINVAL;
	}

	if (cmd->handle_type != CAM_HANDLE_USER_POINTER) {
		CAM_ERR(CAM_FLASH, "Invalid handle type: %d",
			cmd->handle_type);
		return -EINVAL;
	}

	mutex_lock(&(fctrl->flash_mutex));
	switch (cmd->op_code) {
	case CAM_ACQUIRE_DEV: {
		struct cam_sensor_acquire_dev flash_acq_dev;
		struct cam_create_dev_hdl bridge_params;

		CAM_DBG(CAM_FLASH, "CAM_ACQUIRE_DEV");

		if (fctrl->flash_state != CAM_FLASH_STATE_INIT) {
			CAM_ERR(CAM_FLASH,
				"Cannot apply Acquire dev: Prev state: %d",
				fctrl->flash_state);
			rc = -EINVAL;
			goto release_mutex;
		}

		if (fctrl->bridge_intf.device_hdl != -1) {
			CAM_ERR(CAM_FLASH, "Device is already acquired");
			rc = -EINVAL;
			goto release_mutex;
		}

		rc = copy_from_user(&flash_acq_dev, (void __user *)cmd->handle,
			sizeof(flash_acq_dev));
		if (rc) {
			CAM_ERR(CAM_FLASH, "Failed Copying from User");
			goto release_mutex;
		}

		bridge_params.session_hdl = flash_acq_dev.session_handle;
		bridge_params.ops = &fctrl->bridge_intf.ops;
		bridge_params.v4l2_sub_dev_flag = 0;
		bridge_params.media_entity_flag = 0;
		bridge_params.priv = fctrl;

		flash_acq_dev.device_handle =
			cam_create_device_hdl(&bridge_params);
		fctrl->bridge_intf.device_hdl =
			flash_acq_dev.device_handle;
		fctrl->bridge_intf.session_hdl =
			flash_acq_dev.session_handle;

		rc = copy_to_user((void __user *) cmd->handle, &flash_acq_dev,
			sizeof(struct cam_sensor_acquire_dev));
		if (rc) {
			CAM_ERR(CAM_FLASH, "Failed Copy to User with rc = %d",
				rc);
			rc = -EFAULT;
			goto release_mutex;
		}
		fctrl->flash_state = CAM_FLASH_STATE_ACQUIRE;
		break;
	}
	case CAM_RELEASE_DEV: {
		CAM_DBG(CAM_FLASH, "CAM_RELEASE_DEV");
		if ((fctrl->flash_state == CAM_FLASH_STATE_INIT) ||
			(fctrl->flash_state == CAM_FLASH_STATE_START)) {
			CAM_WARN(CAM_FLASH,
				"Cannot apply Release dev: Prev state:%d",
				fctrl->flash_state);
		}

		rc = fctrl->func_tbl.power_ops(fctrl, false);
		if (rc) {
			CAM_ERR(CAM_FLASH, "Stop Dev Failed rc = %d",
				rc);
			goto release_mutex;
		}

		if (fctrl->bridge_intf.device_hdl == -1 &&
			fctrl->flash_state == CAM_FLASH_STATE_ACQUIRE) {
			CAM_ERR(CAM_FLASH,
				"Invalid Handle: Link Hdl: %d device hdl: %d",
				fctrl->bridge_intf.device_hdl,
				fctrl->bridge_intf.link_hdl);
			rc = -EINVAL;
			goto release_mutex;
		}
		rc = cam_flash_release_dev(fctrl);
		if (rc)
			CAM_ERR(CAM_FLASH,
				"Failed in destroying the device Handle rc= %d",
				rc);
		fctrl->flash_state = CAM_FLASH_STATE_INIT;
		break;
	}
	case CAM_QUERY_CAP: {
		struct cam_flash_query_cap_info flash_cap = {0};

		CAM_DBG(CAM_FLASH, "CAM_QUERY_CAP");
		flash_cap.slot_info = fctrl->soc_info.index;
		for (i = 0; i < fctrl->flash_num_sources; i++) {
			flash_cap.max_current_flash[i] =
				soc_private->flash_max_current[i];
			flash_cap.max_duration_flash[i] =
				soc_private->flash_max_duration[i];
		}

		for (i = 0; i < fctrl->torch_num_sources; i++)
			flash_cap.max_current_torch[i] =
				soc_private->torch_max_current[i];

		if (copy_to_user((void __user *) cmd->handle, &flash_cap,
			sizeof(struct cam_flash_query_cap_info))) {
			CAM_ERR(CAM_FLASH, "Failed Copy to User");
			rc = -EFAULT;
			goto release_mutex;
		}
		break;
	}
	case CAM_START_DEV: {
		CAM_DBG(CAM_FLASH, "CAM_START_DEV");
		if ((fctrl->flash_state == CAM_FLASH_STATE_INIT) ||
			(fctrl->flash_state == CAM_FLASH_STATE_START)) {
			CAM_WARN(CAM_FLASH,
				"Cannot apply Start Dev: Prev state: %d",
				fctrl->flash_state);
			rc = -EINVAL;
			goto release_mutex;
		}

		fctrl->flash_state = CAM_FLASH_STATE_START;
		break;
	}
	case CAM_STOP_DEV: {
		CAM_DBG(CAM_FLASH, "CAM_STOP_DEV ENTER");
		if (fctrl->flash_state != CAM_FLASH_STATE_START) {
			CAM_WARN(CAM_FLASH,
				"Cannot apply Stop dev: Prev state is: %d",
				fctrl->flash_state);
			rc = -EINVAL;
			goto release_mutex;
		}

		fctrl->func_tbl.flush_req(fctrl, DELETE_ALL, 0);
		fctrl->flash_state = CAM_FLASH_STATE_ACQUIRE;
		break;
	}
	case CAM_CONFIG_DEV: {
		CAM_DBG(CAM_FLASH, "CAM_CONFIG_DEV");
		rc = fctrl->func_tbl.parser(fctrl, arg);
		if (rc) {
			CAM_ERR(CAM_FLASH, "Failed Flash Config: rc=%d\n", rc);
			goto release_mutex;
		}
		break;
	}
	default:
		CAM_ERR(CAM_FLASH, "Invalid Opcode: %d", cmd->op_code);
		rc = -EINVAL;
	}

release_mutex:
	mutex_unlock(&(fctrl->flash_mutex));
	return rc;
}
static int32_t cam_flash_init_default_params(struct cam_flash_ctrl *fctrl)
{
	/* Validate input parameters */
	if (!fctrl) {
		CAM_ERR(CAM_FLASH, "failed: invalid params fctrl %pK",
			fctrl);
		return -EINVAL;
	}

	CAM_DBG(CAM_FLASH,
		"master_type: %d", fctrl->io_master_info.master_type);
	/* Initialize cci_client */
	if (fctrl->io_master_info.master_type == CCI_MASTER) {
		fctrl->io_master_info.cci_client = kzalloc(sizeof(
			struct cam_sensor_cci_client), GFP_KERNEL);
		if (!(fctrl->io_master_info.cci_client))
			return -ENOMEM;
	} else if (fctrl->io_master_info.master_type == I2C_MASTER) {
		if (!(fctrl->io_master_info.client))
			return -EINVAL;
	} else {
		CAM_ERR(CAM_FLASH,
			"Invalid master / Master type Not supported");
		return -EINVAL;
	}

	return 0;
}

static const struct of_device_id cam_flash_dt_match[] = {
	{.compatible = "qcom,camera-flash", .data = NULL},
	{}
};

static long cam_flash_subdev_ioctl(struct v4l2_subdev *sd,
	unsigned int cmd, void *arg)
{
	int rc = 0;
	struct cam_flash_ctrl *fctrl = NULL;
	struct cam_flash_private_soc *soc_private = NULL;

	CAM_DBG(CAM_FLASH, "Enter");

	fctrl = v4l2_get_subdevdata(sd);
	soc_private = fctrl->soc_info.soc_private;

	switch (cmd) {
	case VIDIOC_CAM_CONTROL: {
		rc = cam_flash_driver_cmd(fctrl, arg,
			soc_private);
		break;
	}
	default:
		CAM_ERR(CAM_FLASH, "Invalid ioctl cmd type");
		rc = -EINVAL;
		break;
	}

	CAM_DBG(CAM_FLASH, "Exit");
	return rc;
}

#ifdef CONFIG_COMPAT
static long cam_flash_subdev_do_ioctl(struct v4l2_subdev *sd,
	unsigned int cmd, unsigned long arg)
{
	struct cam_control cmd_data;
	int32_t rc = 0;

	if (copy_from_user(&cmd_data, (void __user *)arg,
		sizeof(cmd_data))) {
		CAM_ERR(CAM_FLASH,
			"Failed to copy from user_ptr=%pK size=%zu",
			(void __user *)arg, sizeof(cmd_data));
		return -EFAULT;
	}

	switch (cmd) {
	case VIDIOC_CAM_CONTROL: {
		rc = cam_flash_subdev_ioctl(sd, cmd, &cmd_data);
		if (rc)
			CAM_ERR(CAM_FLASH, "cam_flash_ioctl failed");
		break;
	}
	default:
		CAM_ERR(CAM_FLASH, "Invalid compat ioctl cmd_type:%d",
			cmd);
		rc = -EINVAL;
	}

	if (!rc) {
		if (copy_to_user((void __user *)arg, &cmd_data,
			sizeof(cmd_data))) {
			CAM_ERR(CAM_FLASH,
				"Failed to copy to user_ptr=%pK size=%zu",
				(void __user *)arg, sizeof(cmd_data));
			rc = -EFAULT;
		}
	}

	return rc;
}
#endif

static int cam_flash_platform_remove(struct platform_device *pdev)
{
	struct cam_flash_ctrl *fctrl;

	fctrl = platform_get_drvdata(pdev);
	if (!fctrl) {
		CAM_ERR(CAM_FLASH, "Flash device is NULL");
		return 0;
	}

	devm_kfree(&pdev->dev, fctrl);

	return 0;
}

static int32_t cam_flash_i2c_driver_remove(struct i2c_client *client)
{
	int32_t rc = 0;
	struct cam_flash_ctrl *fctrl = i2c_get_clientdata(client);
	/* Handle I2C Devices */
	if (!fctrl) {
		CAM_ERR(CAM_FLASH, "Flash device is NULL");
		return -EINVAL;
	}
	/*Free Allocated Mem */
	kfree(fctrl->i2c_data.per_frame);
	fctrl->i2c_data.per_frame = NULL;
	kfree(fctrl);
	return rc;
}

static int cam_flash_subdev_close(struct v4l2_subdev *sd,
	struct v4l2_subdev_fh *fh)
{
	struct cam_flash_ctrl *fctrl =
		v4l2_get_subdevdata(sd);

	if (!fctrl) {
		CAM_ERR(CAM_FLASH, "Flash ctrl ptr is NULL");
		return -EINVAL;
	}

	mutex_lock(&fctrl->flash_mutex);
	cam_flash_shutdown(fctrl);
	mutex_unlock(&fctrl->flash_mutex);

	return 0;
}

static struct v4l2_subdev_core_ops cam_flash_subdev_core_ops = {
	.ioctl = cam_flash_subdev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = cam_flash_subdev_do_ioctl
#endif
};

static struct v4l2_subdev_ops cam_flash_subdev_ops = {
	.core = &cam_flash_subdev_core_ops,
};

static const struct v4l2_subdev_internal_ops cam_flash_internal_ops = {
	.close = cam_flash_subdev_close,
};

static int cam_flash_init_subdev(struct cam_flash_ctrl *fctrl)
{
	int rc = 0;

	fctrl->v4l2_dev_str.internal_ops =
		&cam_flash_internal_ops;
	fctrl->v4l2_dev_str.ops = &cam_flash_subdev_ops;
	fctrl->v4l2_dev_str.name = CAMX_FLASH_DEV_NAME;
	fctrl->v4l2_dev_str.sd_flags =
		V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	fctrl->v4l2_dev_str.ent_function = CAM_FLASH_DEVICE_TYPE;
	fctrl->v4l2_dev_str.token = fctrl;

	rc = cam_register_subdev(&(fctrl->v4l2_dev_str));
	if (rc)
		CAM_ERR(CAM_FLASH, "Fail to create subdev with %d", rc);

	return rc;
}

static int32_t cam_flash_platform_probe(struct platform_device *pdev)
{
	int32_t rc = 0, i = 0;
	struct cam_flash_ctrl *fctrl = NULL;

	CAM_DBG(CAM_FLASH, "Enter");
	if (!pdev->dev.of_node) {
		CAM_ERR(CAM_FLASH, "of_node NULL");
		return -EINVAL;
	}

	fctrl = kzalloc(sizeof(struct cam_flash_ctrl), GFP_KERNEL);
	if (!fctrl)
		return -ENOMEM;

	fctrl->pdev = pdev;
	fctrl->soc_info.pdev = pdev;
	fctrl->soc_info.dev = &pdev->dev;
	fctrl->soc_info.dev_name = pdev->name;

	rc = cam_flash_get_dt_data(fctrl, &fctrl->soc_info);
	if (rc) {
		CAM_ERR(CAM_FLASH, "cam_flash_get_dt_data failed with %d", rc);
		kfree(fctrl);
		return -EINVAL;
	}

	if (of_find_property(pdev->dev.of_node, "cci-master", NULL)) {
		/* Get CCI master */
		rc = of_property_read_u32(pdev->dev.of_node, "cci-master",
			&fctrl->cci_i2c_master);
		CAM_DBG(CAM_FLASH, "cci-master %d, rc %d",
			fctrl->cci_i2c_master, rc);
		if (rc < 0) {
			/* Set default master 0 */
			fctrl->cci_i2c_master = MASTER_0;
			rc = 0;
		}

		fctrl->io_master_info.master_type = CCI_MASTER;
		rc = cam_flash_init_default_params(fctrl);
		if (rc) {
			CAM_ERR(CAM_FLASH,
				"failed: cam_flash_init_default_params rc %d",
				rc);
			goto free_resource;
		}

		fctrl->i2c_data.per_frame = (struct i2c_settings_array *)
			kzalloc(sizeof(struct i2c_settings_array) *
			MAX_PER_FRAME_ARRAY, GFP_KERNEL);
		if (fctrl->i2c_data.per_frame == NULL) {
			CAM_ERR(CAM_FLASH, "No Memory");
			rc = -ENOMEM;
			goto free_resource;
		}

		INIT_LIST_HEAD(&(fctrl->i2c_data.init_settings.list_head));
		INIT_LIST_HEAD(&(fctrl->i2c_data.config_settings.list_head));
		for (i = 0; i < MAX_PER_FRAME_ARRAY; i++)
			INIT_LIST_HEAD(
				&(fctrl->i2c_data.per_frame[i].list_head));

		fctrl->func_tbl.parser = cam_flash_i2c_pkt_parser;
		fctrl->func_tbl.apply_setting = cam_flash_i2c_apply_setting;
		fctrl->func_tbl.power_ops = cam_flash_i2c_power_ops;
		fctrl->func_tbl.flush_req = cam_flash_i2c_flush_request;
	} else {
		fctrl->func_tbl.parser = cam_flash_pmic_pkt_parser;
		fctrl->func_tbl.apply_setting = cam_flash_pmic_apply_setting;
		fctrl->func_tbl.power_ops = cam_flash_pmic_power_ops;
		fctrl->func_tbl.flush_req = cam_flash_pmic_flush_request;
	}

	rc = cam_flash_init_subdev(fctrl);
	if (rc)
		goto free_resource;

	fctrl->bridge_intf.device_hdl = -1;
	fctrl->bridge_intf.ops.get_dev_info = cam_flash_publish_dev_info;
	fctrl->bridge_intf.ops.link_setup = cam_flash_establish_link;
	fctrl->bridge_intf.ops.apply_req = cam_flash_apply_request;
	fctrl->bridge_intf.ops.flush_req = cam_flash_flush_request;

	platform_set_drvdata(pdev, fctrl);
	v4l2_set_subdevdata(&fctrl->v4l2_dev_str.sd, fctrl);

	mutex_init(&(fctrl->flash_mutex));
	mutex_init(&(fctrl->flash_wq_mutex));

	fctrl->flash_state = CAM_FLASH_STATE_INIT;
	CAM_DBG(CAM_FLASH, "Probe success");
	return rc;
free_resource:
	kfree(fctrl->soc_info.soc_private);
	cam_soc_util_release_platform_resource(&fctrl->soc_info);
	kfree(fctrl);
	return rc;
}

static int32_t cam_flash_i2c_driver_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int32_t rc = 0, i = 0;
	struct cam_flash_ctrl *fctrl;
	struct cam_sensor_power_ctrl_t *power_info = NULL;

	if (client == NULL || id == NULL) {
		CAM_ERR(CAM_FLASH, "Invalid Args client: %pK id: %pK",
			client, id);
		return -EINVAL;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		CAM_ERR(CAM_FLASH, "%s :: i2c_check_functionality failed",
			 client->name);
		rc = -EFAULT;
		return rc;
	}

	/* Create sensor control structure */
	fctrl = kzalloc(sizeof(*fctrl), GFP_KERNEL);
	if (!fctrl)
		return -ENOMEM;

	i2c_set_clientdata(client, fctrl);

	fctrl->io_master_info.client = client;
	fctrl->soc_info.dev = &client->dev;
	fctrl->soc_info.dev_name = client->name;
	fctrl->io_master_info.master_type = I2C_MASTER;

	rc = cam_flash_get_dt_data(fctrl, &fctrl->soc_info);
	if (rc) {
		CAM_ERR(CAM_FLASH, "failed: cam_sensor_parse_dt rc %d", rc);
		goto free_ctrl;
	}

	rc = cam_flash_init_subdev(fctrl);
	if (rc)
		goto free_ctrl;

	fctrl->i2c_data.per_frame =
		(struct i2c_settings_array *)
		kzalloc(sizeof(struct i2c_settings_array) *
		MAX_PER_FRAME_ARRAY, GFP_KERNEL);
	if (fctrl->i2c_data.per_frame == NULL) {
		rc = -ENOMEM;
		goto unreg_subdev;
	}

	INIT_LIST_HEAD(&(fctrl->i2c_data.init_settings.list_head));
	INIT_LIST_HEAD(&(fctrl->i2c_data.config_settings.list_head));
	for (i = 0; i < MAX_PER_FRAME_ARRAY; i++)
		INIT_LIST_HEAD(&(fctrl->i2c_data.per_frame[i].list_head));

	fctrl->func_tbl.parser = cam_flash_i2c_pkt_parser;
	fctrl->func_tbl.apply_setting = cam_flash_i2c_apply_setting;
	fctrl->func_tbl.power_ops = cam_flash_i2c_power_ops;

	fctrl->bridge_intf.device_hdl = -1;
	fctrl->bridge_intf.ops.get_dev_info = cam_flash_publish_dev_info;
	fctrl->bridge_intf.ops.link_setup = cam_flash_establish_link;
	fctrl->bridge_intf.ops.apply_req = cam_flash_apply_request;
	fctrl->bridge_intf.ops.flush_req = cam_flash_flush_request;

	v4l2_set_subdevdata(&fctrl->v4l2_dev_str.sd, fctrl);

	power_info = &fctrl->power_info;
	power_info->power_setting_size = 1;
	power_info->power_setting =
		(struct cam_sensor_power_setting *)
		kzalloc(sizeof(struct cam_sensor_power_setting),
			GFP_KERNEL);
	if (!power_info->power_setting) {
		rc = -ENOMEM;
		goto free_mem;
	}

	power_info->power_down_setting_size = 1;
	power_info->power_down_setting =
		(struct cam_sensor_power_setting *)
		kzalloc(sizeof(struct cam_sensor_power_setting),
			GFP_KERNEL);
	if (!power_info->power_down_setting) {
		rc = -ENOMEM;
		goto free_power_settings;
	}

	mutex_init(&(fctrl->flash_mutex));
	mutex_init(&(fctrl->flash_wq_mutex));
	fctrl->flash_state = CAM_FLASH_STATE_INIT;

	return rc;

free_power_settings:
	kfree(fctrl->power_info.power_setting);
free_mem:
	kfree(fctrl->i2c_data.per_frame);
unreg_subdev:
	cam_unregister_subdev(&(fctrl->v4l2_dev_str));
free_ctrl:
	kfree(fctrl);
	return rc;
}

MODULE_DEVICE_TABLE(of, cam_flash_dt_match);

static struct platform_driver cam_flash_platform_driver = {
	.probe = cam_flash_platform_probe,
	.remove = cam_flash_platform_remove,
	.driver = {
		.name = "CAM-FLASH-DRIVER",
		.owner = THIS_MODULE,
		.of_match_table = cam_flash_dt_match,
		.suppress_bind_attrs = true,
	},
};

static const struct i2c_device_id i2c_id[] = {
	{FLASH_DRIVER_I2C, (kernel_ulong_t)NULL},
	{ }
};

static struct i2c_driver cam_flash_i2c_driver = {
	.id_table = i2c_id,
	.probe  = cam_flash_i2c_driver_probe,
	.remove = cam_flash_i2c_driver_remove,
	.driver = {
		.name = FLASH_DRIVER_I2C,
	},
};

static int32_t __init cam_flash_init_module(void)
{
	int32_t rc = 0;

	rc = platform_driver_register(&cam_flash_platform_driver);
	if (rc < 0) {
		CAM_ERR(CAM_FLASH,
		"platform probe for flash failed rc = %d", rc);
		return rc;
	}

	rc = i2c_add_driver(&cam_flash_i2c_driver);
	if (rc)
		CAM_ERR(CAM_FLASH, "i2c_add_driver failed rc: %d", rc);
	return rc;
}

static void __exit cam_flash_exit_module(void)
{
	platform_driver_unregister(&cam_flash_platform_driver);
	i2c_del_driver(&cam_flash_i2c_driver);
}

module_init(cam_flash_init_module);
module_exit(cam_flash_exit_module);
MODULE_DESCRIPTION("CAM FLASH");
MODULE_LICENSE("GPL v2");
