/* Copyright (c) 2016-2017 The Linux Foundation. All rights reserved.
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

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/qpnp/qpnp-revid.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include "smb-reg.h"
#include "smb-lib.h"
#include "storm-watch.h"
#include <linux/pmic-voter.h>

//ASUS BSP add include files +++
#include <linux/proc_fs.h>
#include <linux/of_gpio.h>
#include <linux/switch.h>
#include <linux/wakelock.h>
//ASUS BSP add include files ---

//ASUS BSP : Add debug log +++
#define CHARGER_TAG "[BAT][CHG]"
#define ERROR_TAG "[ERR]"
#define CHG_DBG(...)  printk(KERN_INFO CHARGER_TAG __VA_ARGS__)
#define CHG_DBG_E(...)  printk(KERN_ERR CHARGER_TAG ERROR_TAG __VA_ARGS__)
//ex:CHG_DBG("%s: %d\n", __func__, l_result);
//ASUS BSP : Add debug log ---

#define SMB2_DEFAULT_WPWR_UW	8000000

static struct smb_params v1_params = {
	.fcc			= {
		.name	= "fast charge current",
		.reg	= FAST_CHARGE_CURRENT_CFG_REG,
		.min_u	= 0,
		.max_u	= 4500000,
		.step_u	= 25000,
	},
	.fv			= {
		.name	= "float voltage",
		.reg	= FLOAT_VOLTAGE_CFG_REG,
		.min_u	= 3487500,
		.max_u	= 4920000,
		.step_u	= 7500,
	},
	.usb_icl		= {
		.name	= "usb input current limit",
		.reg	= USBIN_CURRENT_LIMIT_CFG_REG,
		.min_u	= 0,
		.max_u	= 4800000,
		.step_u	= 25000,
	},
	.icl_stat		= {
		.name	= "input current limit status",
		.reg	= ICL_STATUS_REG,
		.min_u	= 0,
		.max_u	= 4800000,
		.step_u	= 25000,
	},
	.otg_cl			= {
		.name	= "usb otg current limit",
		.reg	= OTG_CURRENT_LIMIT_CFG_REG,
		.min_u	= 250000,
		.max_u	= 2000000,
		.step_u	= 250000,
	},
	.dc_icl			= {
		.name	= "dc input current limit",
		.reg	= DCIN_CURRENT_LIMIT_CFG_REG,
		.min_u	= 0,
		.max_u	= 6000000,
		.step_u	= 25000,
	},
	.dc_icl_pt_lv		= {
		.name	= "dc icl PT <8V",
		.reg	= ZIN_ICL_PT_REG,
		.min_u	= 0,
		.max_u	= 3000000,
		.step_u	= 25000,
	},
	.dc_icl_pt_hv		= {
		.name	= "dc icl PT >8V",
		.reg	= ZIN_ICL_PT_HV_REG,
		.min_u	= 0,
		.max_u	= 3000000,
		.step_u	= 25000,
	},
	.dc_icl_div2_lv		= {
		.name	= "dc icl div2 <5.5V",
		.reg	= ZIN_ICL_LV_REG,
		.min_u	= 0,
		.max_u	= 3000000,
		.step_u	= 25000,
	},
	.dc_icl_div2_mid_lv	= {
		.name	= "dc icl div2 5.5-6.5V",
		.reg	= ZIN_ICL_MID_LV_REG,
		.min_u	= 0,
		.max_u	= 3000000,
		.step_u	= 25000,
	},
	.dc_icl_div2_mid_hv	= {
		.name	= "dc icl div2 6.5-8.0V",
		.reg	= ZIN_ICL_MID_HV_REG,
		.min_u	= 0,
		.max_u	= 3000000,
		.step_u	= 25000,
	},
	.dc_icl_div2_hv		= {
		.name	= "dc icl div2 >8.0V",
		.reg	= ZIN_ICL_HV_REG,
		.min_u	= 0,
		.max_u	= 3000000,
		.step_u	= 25000,
	},
	.jeita_cc_comp		= {
		.name	= "jeita fcc reduction",
		.reg	= JEITA_CCCOMP_CFG_REG,
		.min_u	= 0,
		.max_u	= 1575000,
		.step_u	= 25000,
	},
	.step_soc_threshold[0]		= {
		.name	= "step charge soc threshold 1",
		.reg	= STEP_CHG_SOC_OR_BATT_V_TH1_REG,
		.min_u	= 0,
		.max_u	= 100,
		.step_u	= 1,
	},
	.step_soc_threshold[1]		= {
		.name	= "step charge soc threshold 2",
		.reg	= STEP_CHG_SOC_OR_BATT_V_TH2_REG,
		.min_u	= 0,
		.max_u	= 100,
		.step_u	= 1,
	},
	.step_soc_threshold[2]         = {
		.name	= "step charge soc threshold 3",
		.reg	= STEP_CHG_SOC_OR_BATT_V_TH3_REG,
		.min_u	= 0,
		.max_u	= 100,
		.step_u	= 1,
	},
	.step_soc_threshold[3]         = {
		.name	= "step charge soc threshold 4",
		.reg	= STEP_CHG_SOC_OR_BATT_V_TH4_REG,
		.min_u	= 0,
		.max_u	= 100,
		.step_u	= 1,
	},
	.step_soc			= {
		.name	= "step charge soc",
		.reg	= STEP_CHG_SOC_VBATT_V_REG,
		.min_u	= 0,
		.max_u	= 100,
		.step_u	= 1,
		.set_proc	= smblib_mapping_soc_from_field_value,
	},
	.step_cc_delta[0]	= {
		.name	= "step charge current delta 1",
		.reg	= STEP_CHG_CURRENT_DELTA1_REG,
		.min_u	= 100000,
		.max_u	= 3200000,
		.step_u	= 100000,
		.get_proc	= smblib_mapping_cc_delta_to_field_value,
		.set_proc	= smblib_mapping_cc_delta_from_field_value,
	},
	.step_cc_delta[1]	= {
		.name	= "step charge current delta 2",
		.reg	= STEP_CHG_CURRENT_DELTA2_REG,
		.min_u	= 100000,
		.max_u	= 3200000,
		.step_u	= 100000,
		.get_proc	= smblib_mapping_cc_delta_to_field_value,
		.set_proc	= smblib_mapping_cc_delta_from_field_value,
	},
	.step_cc_delta[2]	= {
		.name	= "step charge current delta 3",
		.reg	= STEP_CHG_CURRENT_DELTA3_REG,
		.min_u	= 100000,
		.max_u	= 3200000,
		.step_u	= 100000,
		.get_proc	= smblib_mapping_cc_delta_to_field_value,
		.set_proc	= smblib_mapping_cc_delta_from_field_value,
	},
	.step_cc_delta[3]	= {
		.name	= "step charge current delta 4",
		.reg	= STEP_CHG_CURRENT_DELTA4_REG,
		.min_u	= 100000,
		.max_u	= 3200000,
		.step_u	= 100000,
		.get_proc	= smblib_mapping_cc_delta_to_field_value,
		.set_proc	= smblib_mapping_cc_delta_from_field_value,
	},
	.step_cc_delta[4]	= {
		.name	= "step charge current delta 5",
		.reg	= STEP_CHG_CURRENT_DELTA5_REG,
		.min_u	= 100000,
		.max_u	= 3200000,
		.step_u	= 100000,
		.get_proc	= smblib_mapping_cc_delta_to_field_value,
		.set_proc	= smblib_mapping_cc_delta_from_field_value,
	},
	.freq_buck		= {
		.name	= "buck switching frequency",
		.reg	= CFG_BUCKBOOST_FREQ_SELECT_BUCK_REG,
		.min_u	= 600,
		.max_u	= 2000,
		.step_u	= 200,
	},
	.freq_boost		= {
		.name	= "boost switching frequency",
		.reg	= CFG_BUCKBOOST_FREQ_SELECT_BOOST_REG,
		.min_u	= 600,
		.max_u	= 2000,
		.step_u	= 200,
	},
};

static struct smb_params pm660_params = {
	.freq_buck		= {
		.name	= "buck switching frequency",
		.reg	= FREQ_CLK_DIV_REG,
		.min_u	= 600,
		.max_u	= 1600,
		.set_proc = smblib_set_chg_freq,
	},
	.freq_boost		= {
		.name	= "boost switching frequency",
		.reg	= FREQ_CLK_DIV_REG,
		.min_u	= 600,
		.max_u	= 1600,
		.set_proc = smblib_set_chg_freq,
	},
};

#define STEP_CHARGING_MAX_STEPS	5
struct smb_dt_props {
	int	usb_icl_ua;
	int	dc_icl_ua;
	int	boost_threshold_ua;
	int	wipower_max_uw;
	int	min_freq_khz;
	int	max_freq_khz;
	u32	step_soc_threshold[STEP_CHARGING_MAX_STEPS - 1];
	s32	step_cc_delta[STEP_CHARGING_MAX_STEPS];
	struct	device_node *revid_dev_node;
	int	float_option;
	int	chg_inhibit_thr_mv;
	bool	no_battery;
	bool	hvdcp_disable;
	bool	auto_recharge_soc;
};

struct smb2 {
	struct smb_charger	chg;
	struct dentry		*dfs_root;
	struct smb_dt_props	dt;
	bool			bad_part;
};

//ASUS BSP add struct functions +++
struct smb_charger *smbchg_dev;
struct gpio_control *global_gpio;	//global gpio_control
struct timespec last_jeita_time;
struct wake_lock asus_chg_lock;
extern bool g_usb_alert_mode;		//ASUS_BSP Austin_T : add usb alert mode trigger
extern bool g_low_impedance_mode;	//ASUS_BSP Austin_T : add low impedance mode trigger
extern bool g_water_proof_mode;		//ASUS_BSP Austin_T : add water proof mode trigger
volatile bool usb_alert_flag = 0;
bool low_impedance_flag = 0;
bool water_once_flag = 0;
bool boot_completed_flag = 0;
extern int asus_CHG_TYPE;
bool no_input_suspend_flag = 0;
extern bool asus_flow_done_flag;
bool demo_app_property_flag = 0;
bool smartchg_stop_flag = 0;

extern void smblib_asus_monitor_start(struct smb_charger *chg, int time);
extern bool asus_get_prop_usb_present(struct smb_charger *chg);
extern void asus_smblib_stay_awake(struct smb_charger *chg);
extern void asus_smblib_relax(struct smb_charger *chg);
//ASUS BSP add struct functions ---

static int __debug_mask;
module_param_named(
	debug_mask, __debug_mask, int, S_IRUSR | S_IWUSR
);

#define MICRO_1P5A		1500000
#define MICRO_P1A		100000
#define OTG_DEFAULT_DEGLITCH_TIME_MS	50
static int smb2_parse_dt(struct smb2 *chip)
{
	struct smb_charger *chg = &chip->chg;
	struct device_node *node = chg->dev->of_node;
	int rc, byte_len;

	if (!node) {
		pr_err("device tree node missing\n");
		return -EINVAL;
	}

	chg->step_chg_enabled = true;

	if (of_property_count_u32_elems(node, "qcom,step-soc-thresholds")
			!= STEP_CHARGING_MAX_STEPS - 1)
		chg->step_chg_enabled = false;

	rc = of_property_read_u32_array(node, "qcom,step-soc-thresholds",
			chip->dt.step_soc_threshold,
			STEP_CHARGING_MAX_STEPS - 1);
	if (rc < 0)
		chg->step_chg_enabled = false;

	if (of_property_count_u32_elems(node, "qcom,step-current-deltas")
			!= STEP_CHARGING_MAX_STEPS)
		chg->step_chg_enabled = false;

	rc = of_property_read_u32_array(node, "qcom,step-current-deltas",
			chip->dt.step_cc_delta,
			STEP_CHARGING_MAX_STEPS);
	if (rc < 0)
		chg->step_chg_enabled = false;

	chip->dt.no_battery = of_property_read_bool(node,
						"qcom,batteryless-platform");

	chg->external_vconn = of_property_read_bool(node,
						"qcom,external-vconn");

	rc = of_property_read_u32(node,
				"qcom,fcc-max-ua", &chg->batt_profile_fcc_ua);
	if (rc < 0)
		chg->batt_profile_fcc_ua = -EINVAL;

	rc = of_property_read_u32(node,
				"qcom,fv-max-uv", &chg->batt_profile_fv_uv);
	if (rc < 0)
		chg->batt_profile_fv_uv = -EINVAL;

	rc = of_property_read_u32(node,
				"qcom,usb-icl-ua", &chip->dt.usb_icl_ua);
	if (rc < 0)
		chip->dt.usb_icl_ua = -EINVAL;

	rc = of_property_read_u32(node,
				"qcom,otg-cl-ua", &chg->otg_cl_ua);
	if (rc < 0) {
		//chg->otg_cl_ua = MICRO_1P5A;
		chg->otg_cl_ua = 500000;//Set default to 500mA
	}

	rc = of_property_read_u32(node,
				"qcom,dc-icl-ua", &chip->dt.dc_icl_ua);
	if (rc < 0)
		chip->dt.dc_icl_ua = -EINVAL;

	rc = of_property_read_u32(node,
				"qcom,boost-threshold-ua",
				&chip->dt.boost_threshold_ua);
	if (rc < 0)
		chip->dt.boost_threshold_ua = MICRO_P1A;

	rc = of_property_read_u32(node,
				"qcom,min-freq-khz",
				&chip->dt.min_freq_khz);
	if (rc < 0)
		chip->dt.min_freq_khz = -EINVAL;

	rc = of_property_read_u32(node,
				"qcom,max-freq-khz",
				&chip->dt.max_freq_khz);
	if (rc < 0)
		chip->dt.max_freq_khz = -EINVAL;

	rc = of_property_read_u32(node, "qcom,wipower-max-uw",
				&chip->dt.wipower_max_uw);
	if (rc < 0)
		chip->dt.wipower_max_uw = -EINVAL;

	if (of_find_property(node, "qcom,thermal-mitigation", &byte_len)) {
		chg->thermal_mitigation = devm_kzalloc(chg->dev, byte_len,
			GFP_KERNEL);

		if (chg->thermal_mitigation == NULL)
			return -ENOMEM;

		chg->thermal_levels = byte_len / sizeof(u32);
		rc = of_property_read_u32_array(node,
				"qcom,thermal-mitigation",
				chg->thermal_mitigation,
				chg->thermal_levels);
		if (rc < 0) {
			dev_err(chg->dev,
				"Couldn't read threm limits rc = %d\n", rc);
			return rc;
		}
	}

	of_property_read_u32(node, "qcom,float-option", &chip->dt.float_option);
	if (chip->dt.float_option < 0 || chip->dt.float_option > 4) {
		pr_err("qcom,float-option is out of range [0, 4]\n");
		return -EINVAL;
	}

	chip->dt.hvdcp_disable = of_property_read_bool(node,
						"qcom,hvdcp-disable");

	of_property_read_u32(node, "qcom,chg-inhibit-threshold-mv",
				&chip->dt.chg_inhibit_thr_mv);
	if ((chip->dt.chg_inhibit_thr_mv < 0 ||
		chip->dt.chg_inhibit_thr_mv > 300)) {
		pr_err("qcom,chg-inhibit-threshold-mv is incorrect\n");
		return -EINVAL;
	}

	chip->dt.auto_recharge_soc = of_property_read_bool(node,
						"qcom,auto-recharge-soc");

	chg->micro_usb_mode = of_property_read_bool(node, "qcom,micro-usb");

	chg->dcp_icl_ua = chip->dt.usb_icl_ua;

	chg->suspend_input_on_debug_batt = of_property_read_bool(node,
					"qcom,suspend-input-on-debug-batt");

	rc = of_property_read_u32(node, "qcom,otg-deglitch-time-ms",
					&chg->otg_delay_ms);
	if (rc < 0)
		chg->otg_delay_ms = OTG_DEFAULT_DEGLITCH_TIME_MS;

	return 0;
}

/************************
 * USB PSY REGISTRATION *
 ************************/

static enum power_supply_property smb2_usb_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_PD_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_TYPEC_MODE,
	POWER_SUPPLY_PROP_TYPEC_POWER_ROLE,
	POWER_SUPPLY_PROP_TYPEC_CC_ORIENTATION,
	POWER_SUPPLY_PROP_PD_ALLOWED,
	POWER_SUPPLY_PROP_PD_ACTIVE,
	POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED,
	POWER_SUPPLY_PROP_INPUT_CURRENT_NOW,
	POWER_SUPPLY_PROP_BOOST_CURRENT,
	POWER_SUPPLY_PROP_PE_START,
	POWER_SUPPLY_PROP_CTM_CURRENT_MAX,
	POWER_SUPPLY_PROP_HW_CURRENT_MAX,
};

static int smb2_usb_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smb2 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		if (chip->bad_part)
			val->intval = 1;
		else
			rc = smblib_get_prop_usb_present(chg, val);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		rc = smblib_get_prop_usb_online(chg, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		val->intval = chg->voltage_min_uv;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = chg->voltage_max_uv;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		rc = smblib_get_prop_usb_voltage_now(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_CURRENT_MAX:
		rc = smblib_get_prop_pd_current_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_get_prop_usb_current_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_TYPE:
		if (chip->bad_part)
			val->intval = POWER_SUPPLY_TYPE_USB;
		else
			val->intval = chg->usb_psy_desc.type;
		break;
	case POWER_SUPPLY_PROP_TYPEC_MODE:
		if (chg->micro_usb_mode)
			val->intval = POWER_SUPPLY_TYPEC_NONE;
		else if (chip->bad_part)
			val->intval = POWER_SUPPLY_TYPEC_SOURCE_DEFAULT;
		else
			rc = smblib_get_prop_typec_mode(chg, val);
		break;
	case POWER_SUPPLY_PROP_TYPEC_POWER_ROLE:
		if (chg->micro_usb_mode)
			val->intval = POWER_SUPPLY_TYPEC_PR_NONE;
		else
			rc = smblib_get_prop_typec_power_role(chg, val);
		break;
	case POWER_SUPPLY_PROP_TYPEC_CC_ORIENTATION:
		if (chg->micro_usb_mode)
			val->intval = 0;
		else
			rc = smblib_get_prop_typec_cc_orientation(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_ALLOWED:
		rc = smblib_get_prop_pd_allowed(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_ACTIVE:
		val->intval = chg->pd_active;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED:
		rc = smblib_get_prop_input_current_settled(chg, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_NOW:
		rc = smblib_get_prop_usb_current_now(chg, val);
		break;
	case POWER_SUPPLY_PROP_BOOST_CURRENT:
		val->intval = chg->boost_current_ua;
		break;
	case POWER_SUPPLY_PROP_PD_IN_HARD_RESET:
		rc = smblib_get_prop_pd_in_hard_reset(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_USB_SUSPEND_SUPPORTED:
		val->intval = chg->system_suspend_supported;
		break;
	case POWER_SUPPLY_PROP_PE_START:
		rc = smblib_get_pe_start(chg, val);
		break;
	case POWER_SUPPLY_PROP_CTM_CURRENT_MAX:
		val->intval = get_client_vote(chg->usb_icl_votable, CTM_VOTER);
		break;
	case POWER_SUPPLY_PROP_HW_CURRENT_MAX:
		rc = smblib_get_charge_current(chg, &val->intval);
		break;
	default:
		pr_err("get prop %d is not supported in usb\n", psp);
		rc = -EINVAL;
		break;
	}
	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}
	return 0;
}

static int smb2_usb_set_prop(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct smb2 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	mutex_lock(&chg->lock);
	if (!chg->typec_present) {
		rc = -EINVAL;
		goto unlock;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		rc = smblib_set_prop_usb_voltage_min(chg, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		rc = smblib_set_prop_usb_voltage_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_CURRENT_MAX:
		rc = smblib_set_prop_pd_current_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_set_prop_usb_current_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_TYPEC_POWER_ROLE:
		rc = smblib_set_prop_typec_power_role(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_ACTIVE:
		rc = smblib_set_prop_pd_active(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_IN_HARD_RESET:
		rc = smblib_set_prop_pd_in_hard_reset(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_USB_SUSPEND_SUPPORTED:
		chg->system_suspend_supported = val->intval;
		break;
	case POWER_SUPPLY_PROP_BOOST_CURRENT:
		rc = smblib_set_prop_boost_current(chg, val);
		break;
	case POWER_SUPPLY_PROP_CTM_CURRENT_MAX:
		rc = vote(chg->usb_icl_votable, CTM_VOTER,
						val->intval >= 0, val->intval);
		break;
	default:
		pr_err("set prop %d is not supported\n", psp);
		rc = -EINVAL;
		break;
	}

unlock:
	mutex_unlock(&chg->lock);
	return rc;
}

static int smb2_usb_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_TYPEC_POWER_ROLE:
	case POWER_SUPPLY_PROP_CTM_CURRENT_MAX:
		return 1;
	default:
		break;
	}

	return 0;
}

static int smb2_init_usb_psy(struct smb2 *chip)
{
	struct power_supply_config usb_cfg = {};
	struct smb_charger *chg = &chip->chg;

	chg->usb_psy_desc.name			= "usb";
	chg->usb_psy_desc.type			= POWER_SUPPLY_TYPE_UNKNOWN;
	chg->usb_psy_desc.properties		= smb2_usb_props;
	chg->usb_psy_desc.num_properties	= ARRAY_SIZE(smb2_usb_props);
	chg->usb_psy_desc.get_property		= smb2_usb_get_prop;
	chg->usb_psy_desc.set_property		= smb2_usb_set_prop;
	chg->usb_psy_desc.property_is_writeable	= smb2_usb_prop_is_writeable;

	usb_cfg.drv_data = chip;
	usb_cfg.of_node = chg->dev->of_node;
	chg->usb_psy = power_supply_register(chg->dev,
						  &chg->usb_psy_desc,
						  &usb_cfg);
	if (IS_ERR(chg->usb_psy)) {
		pr_err("Couldn't register USB power supply\n");
		return PTR_ERR(chg->usb_psy);
	}

	return 0;
}

/*****************************
 * USB MAIN PSY REGISTRATION *
 *****************************/

static enum power_supply_property smb2_usb_main_props[] = {
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_SETTLED,
	POWER_SUPPLY_PROP_FCC_DELTA,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	/*
	 * TODO move the TEMP and TEMP_MAX properties here,
	 * and update the thermal balancer to look here
	 */
};

static int smb2_usb_main_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smb2 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		rc = smblib_get_charge_param(chg, &chg->param.fv, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		rc = smblib_get_charge_param(chg, &chg->param.fcc,
							&val->intval);
		break;
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = POWER_SUPPLY_TYPE_MAIN;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED:
		rc = smblib_get_prop_input_current_settled(chg, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_SETTLED:
		rc = smblib_get_prop_input_voltage_settled(chg, val);
		break;
	case POWER_SUPPLY_PROP_FCC_DELTA:
		rc = smblib_get_prop_fcc_delta(chg, val);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_get_icl_current(chg, &val->intval);
		break;
	default:
		pr_debug("get prop %d is not supported in usb-main\n", psp);
		rc = -EINVAL;
		break;
	}
	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}
	return 0;
}

static int smb2_usb_main_set_prop(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct smb2 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		rc = smblib_set_charge_param(chg, &chg->param.fv, val->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		rc = smblib_set_charge_param(chg, &chg->param.fcc, val->intval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_set_icl_current(chg, val->intval);
		break;
	default:
		pr_err("set prop %d is not supported\n", psp);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static const struct power_supply_desc usb_main_psy_desc = {
	.name		= "main",
	.type		= POWER_SUPPLY_TYPE_MAIN,
	.properties	= smb2_usb_main_props,
	.num_properties	= ARRAY_SIZE(smb2_usb_main_props),
	.get_property	= smb2_usb_main_get_prop,
	.set_property	= smb2_usb_main_set_prop,
};

static int smb2_init_usb_main_psy(struct smb2 *chip)
{
	struct power_supply_config usb_main_cfg = {};
	struct smb_charger *chg = &chip->chg;

	usb_main_cfg.drv_data = chip;
	usb_main_cfg.of_node = chg->dev->of_node;
	chg->usb_main_psy = power_supply_register(chg->dev,
						  &usb_main_psy_desc,
						  &usb_main_cfg);
	if (IS_ERR(chg->usb_main_psy)) {
		pr_err("Couldn't register USB main power supply\n");
		return PTR_ERR(chg->usb_main_psy);
	}

	return 0;
}

/*************************
 * DC PSY REGISTRATION   *
 *************************/

static enum power_supply_property smb2_dc_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};

static int smb2_dc_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smb2 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		rc = smblib_get_prop_dc_present(chg, val);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		rc = smblib_get_prop_dc_online(chg, val);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_get_prop_dc_current_max(chg, val);
		break;
	default:
		return -EINVAL;
	}
	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}
	return 0;
}

static int smb2_dc_set_prop(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct smb2 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_set_prop_dc_current_max(chg, val);
		break;
	default:
		return -EINVAL;
	}

	return rc;
}

static int smb2_dc_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	int rc;

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = 1;
		break;
	default:
		rc = 0;
		break;
	}

	return rc;
}

static const struct power_supply_desc dc_psy_desc = {
	.name = "dc",
	.type = POWER_SUPPLY_TYPE_WIPOWER,
	.properties = smb2_dc_props,
	.num_properties = ARRAY_SIZE(smb2_dc_props),
	.get_property = smb2_dc_get_prop,
	.set_property = smb2_dc_set_prop,
	.property_is_writeable = smb2_dc_prop_is_writeable,
};

static int smb2_init_dc_psy(struct smb2 *chip)
{
	struct power_supply_config dc_cfg = {};
	struct smb_charger *chg = &chip->chg;

	dc_cfg.drv_data = chip;
	dc_cfg.of_node = chg->dev->of_node;
	chg->dc_psy = power_supply_register(chg->dev,
						  &dc_psy_desc,
						  &dc_cfg);
	if (IS_ERR(chg->dc_psy)) {
		pr_err("Couldn't register USB power supply\n");
		return PTR_ERR(chg->dc_psy);
	}

	return 0;
}

/*************************
 * BATT PSY REGISTRATION *
 *************************/

static enum power_supply_property smb2_batt_props[] = {
	POWER_SUPPLY_PROP_INPUT_SUSPEND,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL,
	POWER_SUPPLY_PROP_CHARGER_TEMP,
	POWER_SUPPLY_PROP_CHARGER_TEMP_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMITED,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_QNOVO,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_QNOVO,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_STEP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_STEP_CHARGING_STEP,
	POWER_SUPPLY_PROP_CHARGE_DONE,
	POWER_SUPPLY_PROP_PARALLEL_DISABLE,
	POWER_SUPPLY_PROP_SET_SHIP_MODE,
	POWER_SUPPLY_PROP_DIE_HEALTH,
	POWER_SUPPLY_PROP_RERUN_AICL,
	POWER_SUPPLY_PROP_DP_DM,
};

static int smb2_batt_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smb_charger *chg = power_supply_get_drvdata(psy);
	int rc = 0;
	union power_supply_propval pval = {0, };

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		rc = smblib_get_prop_batt_status(chg, val);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		rc = smblib_get_prop_batt_health(chg, val);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		rc = smblib_get_prop_batt_present(chg, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
		rc = smblib_get_prop_input_suspend(chg, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		rc = smblib_get_prop_batt_charge_type(chg, val);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		rc = smblib_get_prop_batt_capacity(chg, val);
		break;
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
		rc = smblib_get_prop_system_temp_level(chg, val);
		break;
	case POWER_SUPPLY_PROP_CHARGER_TEMP:
		/* do not query RRADC if charger is not present */
		rc = smblib_get_prop_usb_present(chg, &pval);
		if (rc < 0)
			pr_err("Couldn't get usb present rc=%d\n", rc);

		rc = -ENODATA;
		if (pval.intval)
			rc = smblib_get_prop_charger_temp(chg, val);
		break;
	case POWER_SUPPLY_PROP_CHARGER_TEMP_MAX:
		rc = smblib_get_prop_charger_temp_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMITED:
		rc = smblib_get_prop_input_current_limited(chg, val);
		break;
	case POWER_SUPPLY_PROP_STEP_CHARGING_ENABLED:
		val->intval = chg->step_chg_enabled;
		break;
	case POWER_SUPPLY_PROP_STEP_CHARGING_STEP:
		rc = smblib_get_prop_step_chg_step(chg, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		rc = smblib_get_prop_batt_voltage_now(chg, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = get_client_vote(chg->fv_votable, DEFAULT_VOTER);
		break;
	case POWER_SUPPLY_PROP_CHARGE_QNOVO_ENABLE:
		rc = smblib_get_prop_charge_qnovo_enable(chg, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_QNOVO:
		val->intval = get_client_vote_locked(chg->fv_votable,
				QNOVO_VOTER);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		rc = smblib_get_prop_batt_current_now(chg, val);
		break;
	case POWER_SUPPLY_PROP_CURRENT_QNOVO:
		val->intval = get_client_vote_locked(chg->fcc_votable,
				QNOVO_VOTER);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = get_client_vote(chg->fcc_votable,
					      DEFAULT_VOTER);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		rc = smblib_get_prop_batt_temp(chg, val);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CHARGE_DONE:
		rc = smblib_get_prop_batt_charge_done(chg, val);
		break;
	case POWER_SUPPLY_PROP_PARALLEL_DISABLE:
		val->intval = get_client_vote(chg->pl_disable_votable,
					      USER_VOTER);
		break;
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		/* Not in ship mode as long as device is active */
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_DIE_HEALTH:
		rc = smblib_get_prop_die_health(chg, val);
		break;
	case POWER_SUPPLY_PROP_DP_DM:
		val->intval = chg->pulse_cnt;
		break;
	case POWER_SUPPLY_PROP_RERUN_AICL:
		val->intval = 0;
		break;
	default:
		pr_err("batt power supply prop %d not supported\n", psp);
		return -EINVAL;
	}

	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}

	return 0;
}

static int smb2_batt_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *val)
{
	int rc = 0;
	struct smb_charger *chg = power_supply_get_drvdata(psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
		rc = smblib_set_prop_input_suspend(chg, val);
		break;
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
		rc = smblib_set_prop_system_temp_level(chg, val);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		rc = smblib_set_prop_batt_capacity(chg, val);
		break;
	case POWER_SUPPLY_PROP_PARALLEL_DISABLE:
		vote(chg->pl_disable_votable, USER_VOTER, (bool)val->intval, 0);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		chg->batt_profile_fv_uv = val->intval;
		vote(chg->fv_votable, BATT_PROFILE_VOTER, true, val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_QNOVO_ENABLE:
		rc = smblib_set_prop_charge_qnovo_enable(chg, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_QNOVO:
		if (val->intval == -EINVAL) {
			vote(chg->fv_votable, BATT_PROFILE_VOTER,
					true, chg->batt_profile_fv_uv);
			vote(chg->fv_votable, QNOVO_VOTER, false, 0);
		} else {
			vote(chg->fv_votable, QNOVO_VOTER, true, val->intval);
			vote(chg->fv_votable, BATT_PROFILE_VOTER, false, 0);
		}
		break;
	case POWER_SUPPLY_PROP_CURRENT_QNOVO:
		vote(chg->pl_disable_votable, PL_QNOVO_VOTER,
			val->intval != -EINVAL && val->intval < 2000000, 0);
		if (val->intval == -EINVAL) {
			vote(chg->fcc_votable, BATT_PROFILE_VOTER,
					true, chg->batt_profile_fcc_ua);
			vote(chg->fcc_votable, QNOVO_VOTER, false, 0);
		} else {
			vote(chg->fcc_votable, QNOVO_VOTER, true, val->intval);
			vote(chg->fcc_votable, BATT_PROFILE_VOTER, false, 0);
		}
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		chg->batt_profile_fcc_ua = val->intval;
		vote(chg->fcc_votable, BATT_PROFILE_VOTER, true, val->intval);
		break;
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		/* Not in ship mode as long as the device is active */
		if (!val->intval)
			break;
		if (chg->pl.psy)
			power_supply_set_property(chg->pl.psy,
				POWER_SUPPLY_PROP_SET_SHIP_MODE, val);
		rc = smblib_set_prop_ship_mode(chg, val);
		break;
	case POWER_SUPPLY_PROP_RERUN_AICL:
		rc = smblib_rerun_aicl(chg);
		break;
	case POWER_SUPPLY_PROP_DP_DM:
		rc = smblib_dp_dm(chg, val->intval);
		break;
	default:
		rc = -EINVAL;
	}

	return rc;
}

static int smb2_batt_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
	case POWER_SUPPLY_PROP_CAPACITY:
	case POWER_SUPPLY_PROP_PARALLEL_DISABLE:
	case POWER_SUPPLY_PROP_DP_DM:
	case POWER_SUPPLY_PROP_RERUN_AICL:
		return 1;
	default:
		break;
	}

	return 0;
}

static const struct power_supply_desc batt_psy_desc = {
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = smb2_batt_props,
	.num_properties = ARRAY_SIZE(smb2_batt_props),
	.get_property = smb2_batt_get_prop,
	.set_property = smb2_batt_set_prop,
	.property_is_writeable = smb2_batt_prop_is_writeable,
};

static int smb2_init_batt_psy(struct smb2 *chip)
{
	struct power_supply_config batt_cfg = {};
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	batt_cfg.drv_data = chg;
	batt_cfg.of_node = chg->dev->of_node;
	chg->batt_psy = power_supply_register(chg->dev,
						   &batt_psy_desc,
						   &batt_cfg);
	if (IS_ERR(chg->batt_psy)) {
		pr_err("Couldn't register battery power supply\n");
		return PTR_ERR(chg->batt_psy);
	}

	return rc;
}

/******************************
 * VBUS REGULATOR REGISTRATION *
 ******************************/

struct regulator_ops smb2_vbus_reg_ops = {
	.enable = smblib_vbus_regulator_enable,
	.disable = smblib_vbus_regulator_disable,
	.is_enabled = smblib_vbus_regulator_is_enabled,
};

static int smb2_init_vbus_regulator(struct smb2 *chip)
{
	struct smb_charger *chg = &chip->chg;
	struct regulator_config cfg = {};
	int rc = 0;

	chg->vbus_vreg = devm_kzalloc(chg->dev, sizeof(*chg->vbus_vreg),
				      GFP_KERNEL);
	if (!chg->vbus_vreg)
		return -ENOMEM;

	cfg.dev = chg->dev;
	cfg.driver_data = chip;

	chg->vbus_vreg->rdesc.owner = THIS_MODULE;
	chg->vbus_vreg->rdesc.type = REGULATOR_VOLTAGE;
	chg->vbus_vreg->rdesc.ops = &smb2_vbus_reg_ops;
	chg->vbus_vreg->rdesc.of_match = "qcom,smb2-vbus";
	chg->vbus_vreg->rdesc.name = "qcom,smb2-vbus";

	chg->vbus_vreg->rdev = devm_regulator_register(chg->dev,
						&chg->vbus_vreg->rdesc, &cfg);
	if (IS_ERR(chg->vbus_vreg->rdev)) {
		rc = PTR_ERR(chg->vbus_vreg->rdev);
		chg->vbus_vreg->rdev = NULL;
		if (rc != -EPROBE_DEFER)
			pr_err("Couldn't register VBUS regualtor rc=%d\n", rc);
	}

	return rc;
}

/******************************
 * VCONN REGULATOR REGISTRATION *
 ******************************/

struct regulator_ops smb2_vconn_reg_ops = {
	.enable = smblib_vconn_regulator_enable,
	.disable = smblib_vconn_regulator_disable,
	.is_enabled = smblib_vconn_regulator_is_enabled,
};

static int smb2_init_vconn_regulator(struct smb2 *chip)
{
	struct smb_charger *chg = &chip->chg;
	struct regulator_config cfg = {};
	int rc = 0;

	if (chg->micro_usb_mode)
		return 0;

	chg->vconn_vreg = devm_kzalloc(chg->dev, sizeof(*chg->vconn_vreg),
				      GFP_KERNEL);
	if (!chg->vconn_vreg)
		return -ENOMEM;

	cfg.dev = chg->dev;
	cfg.driver_data = chip;

	chg->vconn_vreg->rdesc.owner = THIS_MODULE;
	chg->vconn_vreg->rdesc.type = REGULATOR_VOLTAGE;
	chg->vconn_vreg->rdesc.ops = &smb2_vconn_reg_ops;
	chg->vconn_vreg->rdesc.of_match = "qcom,smb2-vconn";
	chg->vconn_vreg->rdesc.name = "qcom,smb2-vconn";

	chg->vconn_vreg->rdev = devm_regulator_register(chg->dev,
						&chg->vconn_vreg->rdesc, &cfg);
	if (IS_ERR(chg->vconn_vreg->rdev)) {
		rc = PTR_ERR(chg->vconn_vreg->rdev);
		chg->vconn_vreg->rdev = NULL;
		if (rc != -EPROBE_DEFER)
			pr_err("Couldn't register VCONN regualtor rc=%d\n", rc);
	}

	return rc;
}

/***************************
 * HARDWARE INITIALIZATION *
 ***************************/
static int smb2_config_step_charging(struct smb2 *chip)
{
	struct smb_charger *chg = &chip->chg;
	int rc = 0;
	int i;

	if (!chg->step_chg_enabled)
		return rc;

	for (i = 0; i < STEP_CHARGING_MAX_STEPS - 1; i++) {
		rc = smblib_set_charge_param(chg,
					     &chg->param.step_soc_threshold[i],
					     chip->dt.step_soc_threshold[i]);
		if (rc < 0) {
			pr_err("Couldn't configure soc thresholds rc = %d\n",
				rc);
			goto err_out;
		}
	}

	for (i = 0; i < STEP_CHARGING_MAX_STEPS; i++) {
		rc = smblib_set_charge_param(chg, &chg->param.step_cc_delta[i],
					     chip->dt.step_cc_delta[i]);
		if (rc < 0) {
			pr_err("Couldn't configure cc delta rc = %d\n",
				rc);
			goto err_out;
		}
	}

	rc = smblib_write(chg, STEP_CHG_UPDATE_REQUEST_TIMEOUT_CFG_REG,
			  STEP_CHG_UPDATE_REQUEST_TIMEOUT_40S);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure soc request timeout reg rc=%d\n",
			 rc);
		goto err_out;
	}

	rc = smblib_write(chg, STEP_CHG_UPDATE_FAIL_TIMEOUT_CFG_REG,
			  STEP_CHG_UPDATE_FAIL_TIMEOUT_120S);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure soc fail timeout reg rc=%d\n",
			rc);
		goto err_out;
	}

	/*
	 *  enable step charging, source soc, standard mode, go to final
	 *  state in case of failure.
	 */
	rc = smblib_write(chg, CHGR_STEP_CHG_MODE_CFG_REG,
			       STEP_CHARGING_ENABLE_BIT |
			       STEP_CHARGING_SOURCE_SELECT_BIT |
			       STEP_CHARGING_SOC_FAIL_OPTION_BIT);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure charger rc=%d\n", rc);
		goto err_out;
	}

	return 0;
err_out:
	chg->step_chg_enabled = false;
	return rc;
}

static int smb2_config_wipower_input_power(struct smb2 *chip, int uw)
{
	int rc;
	int ua;
	struct smb_charger *chg = &chip->chg;
	s64 nw = (s64)uw * 1000;

	if (uw < 0)
		return 0;

	ua = div_s64(nw, ZIN_ICL_PT_MAX_MV);
	rc = smblib_set_charge_param(chg, &chg->param.dc_icl_pt_lv, ua);
	if (rc < 0) {
		pr_err("Couldn't configure dc_icl_pt_lv rc = %d\n", rc);
		return rc;
	}

	ua = div_s64(nw, ZIN_ICL_PT_HV_MAX_MV);
	rc = smblib_set_charge_param(chg, &chg->param.dc_icl_pt_hv, ua);
	if (rc < 0) {
		pr_err("Couldn't configure dc_icl_pt_hv rc = %d\n", rc);
		return rc;
	}

	ua = div_s64(nw, ZIN_ICL_LV_MAX_MV);
	rc = smblib_set_charge_param(chg, &chg->param.dc_icl_div2_lv, ua);
	if (rc < 0) {
		pr_err("Couldn't configure dc_icl_div2_lv rc = %d\n", rc);
		return rc;
	}

	ua = div_s64(nw, ZIN_ICL_MID_LV_MAX_MV);
	rc = smblib_set_charge_param(chg, &chg->param.dc_icl_div2_mid_lv, ua);
	if (rc < 0) {
		pr_err("Couldn't configure dc_icl_div2_mid_lv rc = %d\n", rc);
		return rc;
	}

	ua = div_s64(nw, ZIN_ICL_MID_HV_MAX_MV);
	rc = smblib_set_charge_param(chg, &chg->param.dc_icl_div2_mid_hv, ua);
	if (rc < 0) {
		pr_err("Couldn't configure dc_icl_div2_mid_hv rc = %d\n", rc);
		return rc;
	}

	ua = div_s64(nw, ZIN_ICL_HV_MAX_MV);
	rc = smblib_set_charge_param(chg, &chg->param.dc_icl_div2_hv, ua);
	if (rc < 0) {
		pr_err("Couldn't configure dc_icl_div2_hv rc = %d\n", rc);
		return rc;
	}

	return 0;
}

static int smb2_configure_typec(struct smb_charger *chg)
{
	int rc;

	/*
	 * trigger the usb-typec-change interrupt only when the CC state
	 * changes
	 */
	rc = smblib_write(chg, TYPE_C_INTRPT_ENB_REG,
			  TYPEC_CCSTATE_CHANGE_INT_EN_BIT);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure Type-C interrupts rc=%d\n", rc);
		return rc;
	}

	/*
	 * disable Type-C factory mode and stay in Attached.SRC state when VCONN
	 * over-current happens
	 */
	rc = smblib_masked_write(chg, TYPE_C_CFG_REG,
			FACTORY_MODE_DETECTION_EN_BIT | VCONN_OC_CFG_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure Type-C rc=%d\n", rc);
		return rc;
	}

	/* increase VCONN softstart */
	rc = smblib_masked_write(chg, TYPE_C_CFG_2_REG,
			VCONN_SOFTSTART_CFG_MASK, VCONN_SOFTSTART_CFG_MASK);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't increase VCONN softstart rc=%d\n",
			rc);
		return rc;
	}

	/* disable try.SINK mode and legacy cable IRQs */
	rc = smblib_masked_write(chg, TYPE_C_CFG_3_REG, EN_TRYSINK_MODE_BIT |
				TYPEC_NONCOMPLIANT_LEGACY_CABLE_INT_EN_BIT |
				TYPEC_LEGACY_CABLE_INT_EN_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set Type-C config rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static int smb2_disable_typec(struct smb_charger *chg)
{
	int rc;

	/* Move to typeC mode */
	/* configure FSM in idle state and disable UFP_ENABLE bit */
	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
			TYPEC_DISABLE_CMD_BIT | UFP_EN_CMD_BIT,
			TYPEC_DISABLE_CMD_BIT);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't put FSM in idle rc=%d\n", rc);
		return rc;
	}

	/* wait for FSM to enter idle state */
	msleep(200);
	/* configure TypeC mode */
	rc = smblib_masked_write(chg, TYPE_C_CFG_REG,
			TYPE_C_OR_U_USB_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't enable micro USB mode rc=%d\n", rc);
		return rc;
	}

	/* wait for mode change before enabling FSM */
	usleep_range(10000, 11000);
	/* release FSM from idle state */
	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
			TYPEC_DISABLE_CMD_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't release FSM rc=%d\n", rc);
		return rc;
	}

	/* wait for FSM to start */
	msleep(100);
	/* move to uUSB mode */
	/* configure FSM in idle state */
	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
			TYPEC_DISABLE_CMD_BIT, TYPEC_DISABLE_CMD_BIT);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't put FSM in idle rc=%d\n", rc);
		return rc;
	}

	/* wait for FSM to enter idle state */
	msleep(200);
	/* configure micro USB mode */
	rc = smblib_masked_write(chg, TYPE_C_CFG_REG,
			TYPE_C_OR_U_USB_BIT, TYPE_C_OR_U_USB_BIT);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't enable micro USB mode rc=%d\n", rc);
		return rc;
	}

	/* wait for mode change before enabling FSM */
	usleep_range(10000, 11000);
	/* release FSM from idle state */
	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
			TYPEC_DISABLE_CMD_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't release FSM rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static int smb2_init_hw(struct smb2 *chip)
{
	struct smb_charger *chg = &chip->chg;
	int rc;
	u8 stat;

	if (chip->dt.no_battery)
		chg->fake_capacity = 50;

	if (chg->batt_profile_fcc_ua < 0)
		smblib_get_charge_param(chg, &chg->param.fcc,
				&chg->batt_profile_fcc_ua);

	if (chg->batt_profile_fv_uv < 0)
		smblib_get_charge_param(chg, &chg->param.fv,
				&chg->batt_profile_fv_uv);

	smblib_get_charge_param(chg, &chg->param.usb_icl,
				&chg->default_icl_ua);
	if (chip->dt.usb_icl_ua < 0)
		chip->dt.usb_icl_ua = chg->default_icl_ua;

	if (chip->dt.dc_icl_ua < 0)
		smblib_get_charge_param(chg, &chg->param.dc_icl,
					&chip->dt.dc_icl_ua);

	if (chip->dt.min_freq_khz > 0) {
		chg->param.freq_buck.min_u = chip->dt.min_freq_khz;
		chg->param.freq_boost.min_u = chip->dt.min_freq_khz;
	}

	if (chip->dt.max_freq_khz > 0) {
		chg->param.freq_buck.max_u = chip->dt.max_freq_khz;
		chg->param.freq_boost.max_u = chip->dt.max_freq_khz;
	}

	/* set a slower soft start setting for OTG */
	rc = smblib_masked_write(chg, DC_ENG_SSUPPLY_CFG2_REG,
				ENG_SSUPPLY_IVREF_OTG_SS_MASK, OTG_SS_SLOW);
	if (rc < 0) {
		pr_err("Couldn't set otg soft start rc=%d\n", rc);
		return rc;
	}

	/* set OTG current limit */
	rc = smblib_set_charge_param(chg, &chg->param.otg_cl,
				(chg->wa_flags & OTG_WA) ?
				chg->param.otg_cl.min_u : chg->otg_cl_ua);
	if (rc < 0) {
		pr_err("Couldn't set otg current limit rc=%d\n", rc);
		return rc;
	}

	chg->boost_threshold_ua = chip->dt.boost_threshold_ua;

	rc = smblib_read(chg, APSD_RESULT_STATUS_REG, &stat);
	if (rc < 0) {
		pr_err("Couldn't read APSD_RESULT_STATUS rc=%d\n", rc);
		return rc;
	}

	smblib_rerun_apsd_if_required(chg);

	/* clear the ICL override if it is set */
	if (smblib_icl_override(chg, false) < 0) {
		pr_err("Couldn't disable ICL override rc=%d\n", rc);
		return rc;
	}

	/* votes must be cast before configuring software control */
	/* vote 0mA on usb_icl for non battery platforms */
	vote(chg->usb_icl_votable,
		DEFAULT_VOTER, chip->dt.no_battery, 0);
	vote(chg->dc_suspend_votable,
		DEFAULT_VOTER, chip->dt.no_battery, 0);
	vote(chg->fcc_votable,
		BATT_PROFILE_VOTER, true, chg->batt_profile_fcc_ua);
	vote(chg->fv_votable,
		BATT_PROFILE_VOTER, true, chg->batt_profile_fv_uv);
	vote(chg->dc_icl_votable,
		DEFAULT_VOTER, true, chip->dt.dc_icl_ua);
	vote(chg->hvdcp_disable_votable_indirect, PD_INACTIVE_VOTER,
			true, 0);
	vote(chg->hvdcp_disable_votable_indirect, VBUS_CC_SHORT_VOTER,
			true, 0);
	vote(chg->hvdcp_disable_votable_indirect, DEFAULT_VOTER,
		chip->dt.hvdcp_disable, 0);
	vote(chg->pd_disallowed_votable_indirect, CC_DETACHED_VOTER,
			true, 0);
	vote(chg->pd_disallowed_votable_indirect, HVDCP_TIMEOUT_VOTER,
			true, 0);
	vote(chg->pd_disallowed_votable_indirect, MICRO_USB_VOTER,
			chg->micro_usb_mode, 0);
	vote(chg->hvdcp_enable_votable, MICRO_USB_VOTER,
			chg->micro_usb_mode, 0);

	/*
	 * AICL configuration:
	 * start from min and AICL ADC disable
	 */
	rc = smblib_masked_write(chg, USBIN_AICL_OPTIONS_CFG_REG,
			USBIN_AICL_START_AT_MAX_BIT
				| USBIN_AICL_ADC_EN_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure AICL rc=%d\n", rc);
		return rc;
	}

	/* Configure charge enable for software control; active high */
	rc = smblib_masked_write(chg, CHGR_CFG2_REG,
				 CHG_EN_POLARITY_BIT |
				 CHG_EN_SRC_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure charger rc=%d\n", rc);
		return rc;
	}

	/* enable the charging path */
	rc = vote(chg->chg_disable_votable, DEFAULT_VOTER, false, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't enable charging rc=%d\n", rc);
		return rc;
	}

	if (chg->micro_usb_mode)
		rc = smb2_disable_typec(chg);
	else
		rc = smb2_configure_typec(chg);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure Type-C interrupts rc=%d\n", rc);
		return rc;
	}

	/* configure VCONN for software control */
	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
				 VCONN_EN_SRC_BIT | VCONN_EN_VALUE_BIT,
				 VCONN_EN_SRC_BIT);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure VCONN for SW control rc=%d\n", rc);
		return rc;
	}

	/* configure VBUS for software control */
	rc = smblib_masked_write(chg, OTG_CFG_REG, OTG_EN_SRC_CFG_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure VBUS for SW control rc=%d\n", rc);
		return rc;
	}

	/* configure step charging */
	rc = smb2_config_step_charging(chip);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure step charging rc=%d\n",
			rc);
		return rc;
	}

	/* configure wipower watts */
	rc = smb2_config_wipower_input_power(chip, chip->dt.wipower_max_uw);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure wipower rc=%d\n", rc);
		return rc;
	}

	/* disable SW STAT override */
	rc = smblib_masked_write(chg, STAT_CFG_REG,
				 STAT_SW_OVERRIDE_CFG_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't disable SW STAT override rc=%d\n",
			rc);
		return rc;
	}

	/* disable h/w autonomous parallel charging control */
	rc = smblib_masked_write(chg, MISC_CFG_REG,
				 STAT_PARALLEL_1400MA_EN_CFG_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't disable h/w autonomous parallel control rc=%d\n",
			rc);
		return rc;
	}

	/* configure float charger options */
	switch (chip->dt.float_option) {
	case 1:
		rc = smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG,
				FLOAT_OPTIONS_MASK, 0);
		break;
	case 2:
		rc = smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG,
				FLOAT_OPTIONS_MASK, FORCE_FLOAT_SDP_CFG_BIT);
		break;
	case 3:
		rc = smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG,
				FLOAT_OPTIONS_MASK, FLOAT_DIS_CHGING_CFG_BIT);
		break;
	case 4:
		rc = smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG,
				FLOAT_OPTIONS_MASK, SUSPEND_FLOAT_CFG_BIT);
		break;
	default:
		rc = 0;
		break;
	}

	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure float charger options rc=%d\n",
			rc);
		return rc;
	}

	switch (chip->dt.chg_inhibit_thr_mv) {
	case 50:
		rc = smblib_masked_write(chg, CHARGE_INHIBIT_THRESHOLD_CFG_REG,
				CHARGE_INHIBIT_THRESHOLD_MASK,
				CHARGE_INHIBIT_THRESHOLD_50MV);
		break;
	case 100:
		rc = smblib_masked_write(chg, CHARGE_INHIBIT_THRESHOLD_CFG_REG,
				CHARGE_INHIBIT_THRESHOLD_MASK,
				CHARGE_INHIBIT_THRESHOLD_100MV);
		break;
	case 200:
		rc = smblib_masked_write(chg, CHARGE_INHIBIT_THRESHOLD_CFG_REG,
				CHARGE_INHIBIT_THRESHOLD_MASK,
				CHARGE_INHIBIT_THRESHOLD_200MV);
		break;
	case 300:
		rc = smblib_masked_write(chg, CHARGE_INHIBIT_THRESHOLD_CFG_REG,
				CHARGE_INHIBIT_THRESHOLD_MASK,
				CHARGE_INHIBIT_THRESHOLD_300MV);
		break;
	case 0:
		rc = smblib_masked_write(chg, CHGR_CFG2_REG,
				CHARGER_INHIBIT_BIT, 0);
	default:
		break;
	}

	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure charge inhibit threshold rc=%d\n",
			rc);
		return rc;
	}

	if (chip->dt.auto_recharge_soc) {
		rc = smblib_masked_write(chg, FG_UPDATE_CFG_2_SEL_REG,
				SOC_LT_CHG_RECHARGE_THRESH_SEL_BIT |
				VBT_LT_CHG_RECHARGE_THRESH_SEL_BIT,
				VBT_LT_CHG_RECHARGE_THRESH_SEL_BIT);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't configure FG_UPDATE_CFG2_SEL_REG rc=%d\n",
				rc);
			return rc;
		}
	} else {
		rc = smblib_masked_write(chg, FG_UPDATE_CFG_2_SEL_REG,
				SOC_LT_CHG_RECHARGE_THRESH_SEL_BIT |
				VBT_LT_CHG_RECHARGE_THRESH_SEL_BIT,
				SOC_LT_CHG_RECHARGE_THRESH_SEL_BIT);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't configure FG_UPDATE_CFG2_SEL_REG rc=%d\n",
				rc);
			return rc;
		}
	}

	return rc;
}

static int smb2_post_init(struct smb2 *chip)
{
	struct smb_charger *chg = &chip->chg;
	int rc; 
 
 	/* configure power role for dual-role */ 
 	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG, 
 	TYPEC_POWER_ROLE_CMD_MASK, 0); 
 	if (rc < 0) { 
 		dev_err(chg->dev, 
 		"Couldn't configure power role for DRP rc=%d\n", rc); 
 		return rc; 
 	} 

	rerun_election(chg->usb_irq_enable_votable);

	return 0;
}

static int smb2_chg_config_init(struct smb2 *chip)
{
	struct smb_charger *chg = &chip->chg;
	struct pmic_revid_data *pmic_rev_id;
	struct device_node *revid_dev_node;

	revid_dev_node = of_parse_phandle(chip->chg.dev->of_node,
					  "qcom,pmic-revid", 0);
	if (!revid_dev_node) {
		pr_err("Missing qcom,pmic-revid property\n");
		return -EINVAL;
	}

	pmic_rev_id = get_revid_data(revid_dev_node);
	if (IS_ERR_OR_NULL(pmic_rev_id)) {
		/*
		 * the revid peripheral must be registered, any failure
		 * here only indicates that the rev-id module has not
		 * probed yet.
		 */
		return -EPROBE_DEFER;
	}

	switch (pmic_rev_id->pmic_subtype) {
	case PMI8998_SUBTYPE:
		chip->chg.smb_version = PMI8998_SUBTYPE;
		chip->chg.wa_flags |= BOOST_BACK_WA | QC_AUTH_INTERRUPT_WA_BIT;
		if (pmic_rev_id->rev4 == PMI8998_V1P1_REV4) /* PMI rev 1.1 */
			chg->wa_flags |= QC_CHARGER_DETECTION_WA_BIT;
		if (pmic_rev_id->rev4 == PMI8998_V2P0_REV4) /* PMI rev 2.0 */
			chg->wa_flags |= TYPEC_CC2_REMOVAL_WA_BIT;
		chg->chg_freq.freq_5V		= 600;
		chg->chg_freq.freq_6V_8V	= 800;
		chg->chg_freq.freq_9V		= 1000;
		chg->chg_freq.freq_12V		= 1200;
		chg->chg_freq.freq_removal	= 1000;
		chg->chg_freq.freq_below_otg_threshold = 2000;
		chg->chg_freq.freq_above_otg_threshold = 800;
		break;
	case PM660_SUBTYPE:
		chip->chg.smb_version = PM660_SUBTYPE;
		chip->chg.wa_flags |= BOOST_BACK_WA | OTG_WA;
		chg->param.freq_buck = pm660_params.freq_buck;
		chg->param.freq_boost = pm660_params.freq_boost;
		chg->chg_freq.freq_5V		= 600;
		chg->chg_freq.freq_6V_8V	= 800;
		chg->chg_freq.freq_9V		= 1050;
		chg->chg_freq.freq_12V		= 1200;
		chg->chg_freq.freq_removal	= 1050;
		chg->chg_freq.freq_below_otg_threshold = 1600;
		chg->chg_freq.freq_above_otg_threshold = 800;
		break;
	default:
		pr_err("PMIC subtype %d not supported\n",
				pmic_rev_id->pmic_subtype);
		return -EINVAL;
	}

	return 0;
}

/****************************
 * DETERMINE INITIAL STATUS *
 ****************************/

static int smb2_determine_initial_status(struct smb2 *chip)
{
	struct smb_irq_data irq_data = {chip, "determine-initial-status"};
	struct smb_charger *chg = &chip->chg;

	if (chg->bms_psy)
		smblib_suspend_on_debug_battery(chg);
	smblib_handle_usb_plugin(0, &irq_data);
	smblib_handle_usb_typec_change(0, &irq_data);
	smblib_handle_usb_source_change(0, &irq_data);
	smblib_handle_chg_state_change(0, &irq_data);
	smblib_handle_icl_change(0, &irq_data);
	smblib_handle_step_chg_state_change(0, &irq_data);
	smblib_handle_step_chg_soc_update_request(0, &irq_data);

	return 0;
}

/**************************
 * INTERRUPT REGISTRATION *
 **************************/

static struct smb_irq_info smb2_irqs[] = {
/* CHARGER IRQs */
	[CHG_ERROR_IRQ] = {
		.name		= "chg-error",
		.handler	= smblib_handle_debug,
	},
	[CHG_STATE_CHANGE_IRQ] = {
		.name		= "chg-state-change",
		.handler	= smblib_handle_chg_state_change,
		.wake		= true,
	},
	[STEP_CHG_STATE_CHANGE_IRQ] = {
		.name		= "step-chg-state-change",
		.handler	= smblib_handle_step_chg_state_change,
		.wake		= true,
	},
	[STEP_CHG_SOC_UPDATE_FAIL_IRQ] = {
		.name		= "step-chg-soc-update-fail",
		.handler	= smblib_handle_step_chg_soc_update_fail,
		.wake		= true,
	},
	[STEP_CHG_SOC_UPDATE_REQ_IRQ] = {
		.name		= "step-chg-soc-update-request",
		.handler	= smblib_handle_step_chg_soc_update_request,
		.wake		= true,
	},
/* OTG IRQs */
	[OTG_FAIL_IRQ] = {
		.name		= "otg-fail",
		.handler	= smblib_handle_debug,
	},
	[OTG_OVERCURRENT_IRQ] = {
		.name		= "otg-overcurrent",
		.handler	= smblib_handle_otg_overcurrent,
	},
	[OTG_OC_DIS_SW_STS_IRQ] = {
		.name		= "otg-oc-dis-sw-sts",
		.handler	= smblib_handle_debug,
	},
	[TESTMODE_CHANGE_DET_IRQ] = {
		.name		= "testmode-change-detect",
		.handler	= smblib_handle_debug,
	},
/* BATTERY IRQs */
	[BATT_TEMP_IRQ] = {
		.name		= "bat-temp",
		.handler	= smblib_handle_batt_temp_changed,
	},
	[BATT_OCP_IRQ] = {
		.name		= "bat-ocp",
		.handler	= smblib_handle_batt_psy_changed,
	},
	[BATT_OV_IRQ] = {
		.name		= "bat-ov",
		.handler	= smblib_handle_batt_psy_changed,
	},
	[BATT_LOW_IRQ] = {
		.name		= "bat-low",
		.handler	= smblib_handle_batt_psy_changed,
	},
	[BATT_THERM_ID_MISS_IRQ] = {
		.name		= "bat-therm-or-id-missing",
		.handler	= smblib_handle_batt_psy_changed,
	},
	[BATT_TERM_MISS_IRQ] = {
		.name		= "bat-terminal-missing",
		.handler	= smblib_handle_batt_psy_changed,
	},
/* USB INPUT IRQs */
	[USBIN_COLLAPSE_IRQ] = {
		.name		= "usbin-collapse",
		.handler	= smblib_handle_debug,
	},
	[USBIN_LT_3P6V_IRQ] = {
		.name		= "usbin-lt-3p6v",
		.handler	= smblib_handle_debug,
	},
	[USBIN_UV_IRQ] = {
		.name		= "usbin-uv",
		.handler	= smblib_handle_usbin_uv,
	},
	[USBIN_OV_IRQ] = {
		.name		= "usbin-ov",
		.handler	= smblib_handle_debug,
	},
	[USBIN_PLUGIN_IRQ] = {
		.name		= "usbin-plugin",
		.handler	= smblib_handle_usb_plugin,
		.wake		= true,
	},
	[USBIN_SRC_CHANGE_IRQ] = {
		.name		= "usbin-src-change",
		.handler	= smblib_handle_usb_source_change,
		.wake		= true,
	},
	[USBIN_ICL_CHANGE_IRQ] = {
		.name		= "usbin-icl-change",
		.handler	= smblib_handle_icl_change,
		.wake		= true,
	},
	[TYPE_C_CHANGE_IRQ] = {
		.name		= "type-c-change",
		.handler	= smblib_handle_usb_typec_change,
		.wake		= true,
	},
/* DC INPUT IRQs */
	[DCIN_COLLAPSE_IRQ] = {
		.name		= "dcin-collapse",
		.handler	= smblib_handle_debug,
	},
	[DCIN_LT_3P6V_IRQ] = {
		.name		= "dcin-lt-3p6v",
		.handler	= smblib_handle_debug,
	},
	[DCIN_UV_IRQ] = {
		.name		= "dcin-uv",
		.handler	= smblib_handle_debug,
	},
	[DCIN_OV_IRQ] = {
		.name		= "dcin-ov",
		.handler	= smblib_handle_debug,
	},
	[DCIN_PLUGIN_IRQ] = {
		.name		= "dcin-plugin",
		.handler	= smblib_handle_dc_plugin,
		.wake		= true,
	},
	[DIV2_EN_DG_IRQ] = {
		.name		= "div2-en-dg",
		.handler	= smblib_handle_debug,
	},
	[DCIN_ICL_CHANGE_IRQ] = {
		.name		= "dcin-icl-change",
		.handler	= smblib_handle_debug,
	},
/* MISCELLANEOUS IRQs */
	[WDOG_SNARL_IRQ] = {
		.name		= "wdog-snarl",
		.handler	= NULL,
	},
	[WDOG_BARK_IRQ] = {
		.name		= "wdog-bark",
		.handler	= NULL,
	},
	[AICL_FAIL_IRQ] = {
		.name		= "aicl-fail",
		.handler	= smblib_handle_debug,
	},
	[AICL_DONE_IRQ] = {
		.name		= "aicl-done",
		.handler	= smblib_handle_debug,
	},
	[HIGH_DUTY_CYCLE_IRQ] = {
		.name		= "high-duty-cycle",
		.handler	= smblib_handle_high_duty_cycle,
		.wake		= true,
	},
	[INPUT_CURRENT_LIMIT_IRQ] = {
		.name		= "input-current-limiting",
		.handler	= smblib_handle_debug,
	},
	[TEMPERATURE_CHANGE_IRQ] = {
		.name		= "temperature-change",
		.handler	= smblib_handle_debug,
	},
	[SWITCH_POWER_OK_IRQ] = {
		.name		= "switcher-power-ok",
		.handler	= smblib_handle_switcher_power_ok,
		.storm_data	= {true, 1000, 3},
	},
};

static int smb2_get_irq_index_byname(const char *irq_name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(smb2_irqs); i++) {
		if (strcmp(smb2_irqs[i].name, irq_name) == 0)
			return i;
	}

	return -ENOENT;
}

static int smb2_request_interrupt(struct smb2 *chip,
				struct device_node *node, const char *irq_name)
{
	struct smb_charger *chg = &chip->chg;
	int rc, irq, irq_index;
	struct smb_irq_data *irq_data;

	irq = of_irq_get_byname(node, irq_name);
	if (irq < 0) {
		pr_err("Couldn't get irq %s byname\n", irq_name);
		return irq;
	}

	irq_index = smb2_get_irq_index_byname(irq_name);
	if (irq_index < 0) {
		pr_err("%s is not a defined irq\n", irq_name);
		return irq_index;
	}

	if (!smb2_irqs[irq_index].handler)
		return 0;

	irq_data = devm_kzalloc(chg->dev, sizeof(*irq_data), GFP_KERNEL);
	if (!irq_data)
		return -ENOMEM;

	irq_data->parent_data = chip;
	irq_data->name = irq_name;
	irq_data->storm_data = smb2_irqs[irq_index].storm_data;
	mutex_init(&irq_data->storm_data.storm_lock);

	rc = devm_request_threaded_irq(chg->dev, irq, NULL,
					smb2_irqs[irq_index].handler,
					IRQF_ONESHOT, irq_name, irq_data);
	if (rc < 0) {
		pr_err("Couldn't request irq %d\n", irq);
		return rc;
	}

	smb2_irqs[irq_index].irq = irq;
	smb2_irqs[irq_index].irq_data = irq_data;
	if (smb2_irqs[irq_index].wake)
		enable_irq_wake(irq);

	return rc;
}

static int smb2_request_interrupts(struct smb2 *chip)
{
	struct smb_charger *chg = &chip->chg;
	struct device_node *node = chg->dev->of_node;
	struct device_node *child;
	int rc = 0;
	const char *name;
	struct property *prop;

	for_each_available_child_of_node(node, child) {
		of_property_for_each_string(child, "interrupt-names",
					    prop, name) {
			rc = smb2_request_interrupt(chip, child, name);
			if (rc < 0)
				return rc;
		}
	}

	return rc;
}

static void smb2_free_interrupts(struct smb_charger *chg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(smb2_irqs); i++) {
		if (smb2_irqs[i].irq > 0) {
			if (smb2_irqs[i].wake)
				disable_irq_wake(smb2_irqs[i].irq);

			devm_free_irq(chg->dev, smb2_irqs[i].irq,
					smb2_irqs[i].irq_data);
		}
	}
}

static void smb2_disable_interrupts(struct smb_charger *chg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(smb2_irqs); i++) {
		if (smb2_irqs[i].irq > 0)
			disable_irq(smb2_irqs[i].irq);
	}
}

#if defined(CONFIG_DEBUG_FS)

static int force_batt_psy_update_write(void *data, u64 val)
{
	struct smb_charger *chg = data;

	power_supply_changed(chg->batt_psy);
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(force_batt_psy_update_ops, NULL,
			force_batt_psy_update_write, "0x%02llx\n");

static int force_usb_psy_update_write(void *data, u64 val)
{
	struct smb_charger *chg = data;

	power_supply_changed(chg->usb_psy);
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(force_usb_psy_update_ops, NULL,
			force_usb_psy_update_write, "0x%02llx\n");

static int force_dc_psy_update_write(void *data, u64 val)
{
	struct smb_charger *chg = data;

	power_supply_changed(chg->dc_psy);
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(force_dc_psy_update_ops, NULL,
			force_dc_psy_update_write, "0x%02llx\n");

static void smb2_create_debugfs(struct smb2 *chip)
{
	struct dentry *file;

	chip->dfs_root = debugfs_create_dir("charger", NULL);
	if (IS_ERR_OR_NULL(chip->dfs_root)) {
		pr_err("Couldn't create charger debugfs rc=%ld\n",
			(long)chip->dfs_root);
		return;
	}

	file = debugfs_create_file("force_batt_psy_update", S_IRUSR | S_IWUSR,
			    chip->dfs_root, chip, &force_batt_psy_update_ops);
	if (IS_ERR_OR_NULL(file))
		pr_err("Couldn't create force_batt_psy_update file rc=%ld\n",
			(long)file);

	file = debugfs_create_file("force_usb_psy_update", S_IRUSR | S_IWUSR,
			    chip->dfs_root, chip, &force_usb_psy_update_ops);
	if (IS_ERR_OR_NULL(file))
		pr_err("Couldn't create force_usb_psy_update file rc=%ld\n",
			(long)file);

	file = debugfs_create_file("force_dc_psy_update", S_IRUSR | S_IWUSR,
			    chip->dfs_root, chip, &force_dc_psy_update_ops);
	if (IS_ERR_OR_NULL(file))
		pr_err("Couldn't create force_dc_psy_update file rc=%ld\n",
			(long)file);
}

#else

static void smb2_create_debugfs(struct smb2 *chip)
{}

#endif

// ASUS BSP Austin_T : Add attributes +++

static ssize_t pmic_reg_dump_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int rc;
	u8 stat;

	rc = smblib_read(smbchg_dev, USBIN_LOAD_CFG_REG, &stat);
	if (rc < 0) {
		pr_err("Couldn't read USBIN_LOAD_CFG_REG rc=%d\n", rc);
		return rc;
	}
	CHG_DBG("%s: register 0x1365 = 0x%x\n", __func__, stat);

	rc = smblib_read(smbchg_dev, APSD_RESULT_STATUS_REG, &stat);
	if (rc < 0) {
		pr_err("Couldn't read APSD_RESULT_STATUS_REG rc=%d\n", rc);
		return rc;
	}
	CHG_DBG("%s: register 0x1308 = 0x%x\n", __func__, stat);

	rc = smblib_read(smbchg_dev, TYPE_C_STATUS_1_REG, &stat);
	if (rc < 0) {
		pr_err("Couldn't read TYPE_C_STATUS_1_REG rc=%d\n", rc);
		return rc;
	}
	CHG_DBG("%s: register 0x130B = 0x%x\n", __func__, stat);

	rc = smblib_read(smbchg_dev, TYPE_C_STATUS_5_REG, &stat);
	if (rc < 0) {
		pr_err("Couldn't read TYPE_C_STATUS_5_REG rc=%d\n", rc);
		return rc;
	}
	CHG_DBG("%s: register 0x130F = 0x%x\n", __func__, stat);

	rc = smblib_read(smbchg_dev, USBIN_CURRENT_LIMIT_CFG_REG, &stat);
	if (rc < 0) {
		pr_err("Couldn't read USBIN_CURRENT_LIMIT_CFG_REG rc=%d\n", rc);
		return rc;
	}
	CHG_DBG("%s: register 0x1370 = 0x%x\n", __func__, stat);

	rc = smblib_read(smbchg_dev, FAST_CHARGE_CURRENT_CFG_REG, &stat);
	if (rc < 0) {
		pr_err("Couldn't read FAST_CHARGE_CURRENT_CFG_REG rc=%d\n", rc);
		return rc;
	}
	CHG_DBG("%s: register 0x1061 = 0x%x\n", __func__, stat);

	rc = smblib_read(smbchg_dev, CMD_HVDCP_2_REG, &stat);
	if (rc < 0) {
		pr_err("Couldn't read CMD_HVDCP_2_REG rc=%d\n", rc);
		return rc;
	}
	CHG_DBG("%s: register 0x1343 = 0x%x\n", __func__, stat);

	rc = smblib_read(smbchg_dev, HVDCP_PULSE_COUNT_MAX, &stat);
	if (rc < 0) {
		pr_err("Couldn't read HVDCP_PULSE_COUNT_MAX rc=%d\n", rc);
		return rc;
	}
	CHG_DBG("%s: register 0x135B = 0x%x\n", __func__, stat);

	return 0;
}

static ssize_t boot_completed_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int tmp = 0;

	tmp = buf[0] - 48;

	if (tmp == 0) {
		boot_completed_flag = false;
		CHG_DBG("%s: boot_completed_flag = 0\n", __func__);
	} else if (tmp == 1) {
		boot_completed_flag = true;
		CHG_DBG("%s: boot_completed_flag = 1, check USB functions\n", __func__);
		if (g_usb_alert_mode)
			schedule_delayed_work(&smbchg_dev->asus_usb_alert_work, 0);
		/*if (g_low_impedance_mode)
			schedule_delayed_work(&smbchg_dev->asus_low_impedance_work, 0);
		if (g_water_proof_mode && g_ASUS_hwID >= ZE554KL_PR2)
			schedule_delayed_work(&smbchg_dev->asus_water_proof_work, 0);*/
	}

	return len;
}

static ssize_t boot_completed_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", boot_completed_flag);
}

static ssize_t usb_thermal_alert_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int status;

	status = gpio_get_value(global_gpio->USB_THERMAL_ALERT);
	return sprintf(buf, "%d\n", status);
}

static ssize_t usb_low_impedance_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int status;

	status = gpio_get_value(global_gpio->USB_LOW_IMPEDANCE);
	return sprintf(buf, "%d\n", status);
}

static ssize_t usb_water_proof_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int status;

	status = gpio_get_value(global_gpio->USB_WATER_PROOF);
	return sprintf(buf, "%d\n", status);
}

static ssize_t asus_usb_suspend_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int tmp = 0;
	int rc;

	tmp = buf[0] - 48;

	if (tmp == 0) {
		CHG_DBG("%s: Set EnableCharging\n", __func__);
		rc = smblib_set_usb_suspend(smbchg_dev, 0);
	} else if (tmp == 1) {
		CHG_DBG("%s: Set DisableCharging\n", __func__);
		rc = smblib_set_usb_suspend(smbchg_dev, 1);
	}

	return len;
}

static ssize_t asus_usb_suspend_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 reg;
	int ret;
	int suspend;

	ret = smblib_read(smbchg_dev, USBIN_CMD_IL_REG, &reg);
	suspend = reg & USBIN_SUSPEND_BIT;

	return sprintf(buf, "%d\n", suspend);
}

static ssize_t TypeC_Side_Detect_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int typec_side_detect, open, cc_pin;
	u8 reg;
	int ret = -1;

	ret = smblib_read(smbchg_dev, TYPE_C_STATUS_4_REG, &reg);
	open = reg & CC_ATTACHED_BIT;

	ret = smblib_read(smbchg_dev, TYPE_C_STATUS_4_REG, &reg);
	cc_pin = reg & CC_ORIENTATION_BIT;

	if (open == 0)
		typec_side_detect = 0;
	else if (cc_pin == 0)
		typec_side_detect = 1;
	else
		typec_side_detect = 2;

	return sprintf(buf, "%d\n", typec_side_detect);
}

static ssize_t CHG_TYPE_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (asus_CHG_TYPE == 750)
		return sprintf(buf, "DCP_ASUS_750K_2A\n");
	else if (asus_CHG_TYPE == 200)
		return sprintf(buf, "HVDCP_ASUS_200K_2A\n");
	else
		return sprintf(buf, "OTHERS\n");
}

static ssize_t disable_input_suspend_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int tmp = 0;
	int rc;

	tmp = buf[0] - 48;

	if (tmp == 0) {
		CHG_DBG("%s: Thermal Test, can not suspend input\n", __func__);
		no_input_suspend_flag = 0;
	} else if (tmp == 1) {
		CHG_DBG("%s: Thermal Test, can not suspend input\n", __func__);
		no_input_suspend_flag = 1;
	}

	rc = smblib_set_usb_suspend(smbchg_dev, 0);

	return len;
}

static ssize_t disable_input_suspend_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", no_input_suspend_flag);
}

static ssize_t demo_app_property_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int tmp = 0;

	tmp = buf[0] - 48;

	if (tmp == 0) {
		demo_app_property_flag = false;
		CHG_DBG("%s: demo_app_property_flag = 0\n", __func__);
	} else if (tmp == 1) {
		demo_app_property_flag = true;
		CHG_DBG("%s: demo_app_property_flag = 1\n", __func__);
    }

	return len;
}

static ssize_t demo_app_property_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", demo_app_property_flag);
}

static ssize_t smartchg_stop_charging_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int tmp = 0;
	int rc;

	tmp = buf[0] - 48;

	if (tmp == 0) {
		CHG_DBG("%s: Smart charge enable charging\n", __func__);
		smartchg_stop_flag = 0;
		rc = smblib_masked_write(smbchg_dev, CHARGING_ENABLE_CMD_REG, CHARGING_ENABLE_CMD_BIT, 0);
		if (rc < 0) {
			printk("[BAT][CHG] Couldn't write charging_enable rc = %d\n", rc);
			return rc;
		}
	} else if (tmp == 1) {
		CHG_DBG("%s: Smart charge stop charging\n", __func__);
		smartchg_stop_flag = 1;
		rc = smblib_masked_write(smbchg_dev, CHARGING_ENABLE_CMD_REG, CHARGING_ENABLE_CMD_BIT, CHARGING_ENABLE_CMD_BIT);
		if (rc < 0) {
			printk("[BAT][CHG] Couldn't write charging_enable rc = %d\n", rc);
			return rc;
		}
	}

	return len;
}

static ssize_t smartchg_stop_charging_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", smartchg_stop_flag);
}

static DEVICE_ATTR(pmic_reg_dump, 0664, pmic_reg_dump_show, NULL);
static DEVICE_ATTR(boot_completed, 0664, boot_completed_show, boot_completed_store);
static DEVICE_ATTR(usb_thermal_alert, 0664, usb_thermal_alert_show, NULL);
static DEVICE_ATTR(usb_low_impedance, 0664, usb_low_impedance_show, NULL);
static DEVICE_ATTR(usb_water_proof, 0664, usb_water_proof_show, NULL);
static DEVICE_ATTR(TypeC_Side_Detect, 0664, TypeC_Side_Detect_show, NULL);
static DEVICE_ATTR(asus_usb_suspend, 0664, asus_usb_suspend_show, asus_usb_suspend_store);
static DEVICE_ATTR(CHG_TYPE, 0664, CHG_TYPE_show, NULL);
static DEVICE_ATTR(disable_input_suspend, 0664, disable_input_suspend_show, disable_input_suspend_store);
static DEVICE_ATTR(demo_app_property, 0664, demo_app_property_show, demo_app_property_store);
static DEVICE_ATTR(smartchg_stop_charging, 0664, smartchg_stop_charging_show, smartchg_stop_charging_store);

static struct attribute *asus_smblib_attrs[] = {
	&dev_attr_pmic_reg_dump.attr,
	&dev_attr_boot_completed.attr,
	&dev_attr_usb_thermal_alert.attr,
	&dev_attr_usb_low_impedance.attr,
	&dev_attr_usb_water_proof.attr,
	&dev_attr_TypeC_Side_Detect.attr,
	&dev_attr_asus_usb_suspend.attr,
	&dev_attr_CHG_TYPE.attr,
	&dev_attr_disable_input_suspend.attr,
	&dev_attr_demo_app_property.attr,
	&dev_attr_smartchg_stop_charging.attr,
	NULL
};

static const struct attribute_group asus_smblib_attr_group = {
	.attrs = asus_smblib_attrs,
};
// ASUS BSP Austin_T : Add attributes ---

/*+++BSP Austin_T BMMI Adb Interface+++*/
#define chargerIC_status_PROC_FILE	"driver/chargerIC_status"
static struct proc_dir_entry *chargerIC_status_proc_file;
static int chargerIC_status_proc_read(struct seq_file *buf, void *v)
{
	int ret = -1;
    u8 reg;
    ret = smblib_read(smbchg_dev, SHDN_CMD_REG, &reg);
    if (ret) {
		ret = 0;
    } else {
		ret = 1;
    }
	seq_printf(buf, "%d\n", ret);
	return 0;
}
static int chargerIC_status_proc_open(struct inode *inode, struct  file *file)
{
	return single_open(file, chargerIC_status_proc_read, NULL);
}

static const struct file_operations chargerIC_status_fops = {
	.owner = THIS_MODULE,
    .open = chargerIC_status_proc_open,
    .read = seq_read,
    .release = single_release,
};
void static create_chargerIC_status_proc_file(void)
{
	chargerIC_status_proc_file = proc_create(chargerIC_status_PROC_FILE, 0644, NULL, &chargerIC_status_fops);

    if (chargerIC_status_proc_file) {
		CHG_DBG("%s: sucessed!\n", __func__);
    } else {
	    CHG_DBG("%s: failed!\n", __func__);
    }
}
/*---BSP Austin_T BMMI Adb Interface---*/

/*+++BSP Austin_T Add USB ALERT function+++*/
struct switch_dev usb_alert_dev;
void asus_usb_alert_work(struct work_struct *work)
{
	int status;
	int rc;

	status = gpio_get_value(global_gpio->USB_THERMAL_ALERT);
	CHG_DBG("%s: USB_alert boot completed, gpio79 status = %d\n", __func__, status);

	if(status==1){
		status = 0;
		CHG_DBG("Thermal alert triggered but not report it\n");
	}
	
	switch_set_state(&usb_alert_dev, status);
	usb_alert_flag = status;

	if (status == 1) {
		rc = smblib_set_usb_suspend(smbchg_dev, 1);
		rc = smblib_masked_write(smbchg_dev, CMD_OTG_REG, OTG_EN_BIT, 0);
		if (rc < 0)
			dev_err(smbchg_dev->dev, "Couldn't set CMD_OTG_REG rc=%d\n", rc);
		CHG_DBG("%s: usb_temp_alert_interrupt, suspend charger and otg\n", __func__);
	}
}

//[+++]Add the interrupt handler for usb temperature detection
static irqreturn_t usb_temp_alert_interrupt(int irq, void *dev_id)
{
	int status = gpio_get_value_cansleep(global_gpio->USB_THERMAL_ALERT);
	int rc;

	CHG_DBG("%s: Get USB_Thermal_Status : %d\n", __func__, status);

	if(status==1){
		status = 0;
		CHG_DBG("Thermal alert triggered but not report it\n");
	}	

	switch_set_state(&usb_alert_dev, status);
	usb_alert_flag = status;

	if (status == 1) {
		rc = smblib_set_usb_suspend(smbchg_dev, 1);
		rc = smblib_masked_write(smbchg_dev, CMD_OTG_REG, OTG_EN_BIT, 0);
		if (rc < 0)
			dev_err(smbchg_dev->dev, "Couldn't set CMD_OTG_REG rc=%d\n", rc);
		CHG_DBG("%s: usb_temp_alert_interrupt, suspend charger and otg\n", __func__);
	} else {
		CHG_DBG("%s: usb_temp_alert_cancel, enable charger and otg\n", __func__);
	}

	return IRQ_HANDLED;
}
//[---]Add the interrupt handler for usb temperature detection

void register_usb_alert(void)
{
	int ret;
	/* register switch device for usb alert report */
	usb_alert_dev.name = "usb_connector";
	usb_alert_dev.index = 0;

	ret = switch_dev_register(&usb_alert_dev);
	if (ret < 0)
        CHG_DBG("%s: Fail to register switch usb_alert uevent\n", __func__);
    else
        CHG_DBG("%s: Success to register switch usb_alert uevent\n", __func__);
}
/*---BSP Austin_T Add USB ALERT function---*/

/*+++BSP Austin_T Add LOW IMPEDANCE function+++*/
struct switch_dev low_impedance_dev;
void asus_low_impedance_work(struct work_struct *work)
{
	int status;
	int usb_present;
	int rc;

	msleep(3000);

	status = gpio_get_value(global_gpio->USB_LOW_IMPEDANCE);
	CHG_DBG("%s: LOW_impedance boot completed, gpio77 status = %d\n", __func__, status);
	low_impedance_flag = status;

	usb_present = asus_get_prop_usb_present(smbchg_dev);

	switch_set_state(&low_impedance_dev, status);
	if (status == 1 && usb_present) {
		//schedule_work(&smbchg_dev->usb_set_online_work);
		rc = smblib_set_usb_suspend(smbchg_dev, 1);
		CHG_DBG("%s: usb_low_impedance_interrupts, set usb_online = 0 and suspend charger\n", __func__);
	}
}

//[+++]Add the interrupt handler for usb low impedance
static irqreturn_t usb_low_impedance_interrupt(int irq, void *dev_id)
{
	int status = gpio_get_value_cansleep(global_gpio->USB_LOW_IMPEDANCE);
	int usb_present;
	int rc;

	msleep(3000);

	CHG_DBG("%s: Get LOW_Impedance_Status : %d\n", __func__, status);
	switch_set_state(&low_impedance_dev, status);
	low_impedance_flag = status;

	usb_present = asus_get_prop_usb_present(smbchg_dev);

	if (status == 1 && usb_present) {
		rc = smblib_set_usb_suspend(smbchg_dev, 1);
		CHG_DBG("%s: usb_low_impedance_interrupt, suspend charger\n", __func__);
	}
	//schedule_work(&smbchg_dev->usb_set_online_work);

	return IRQ_HANDLED;
}
//[---]Add the interrupt handler for usb low impedance

void register_low_impedance(void)
{
	int ret;
	// register switch device for low impedance report
	low_impedance_dev.name = "vbus_short";
	low_impedance_dev.index = 0;

	ret = switch_dev_register(&low_impedance_dev);
	if (ret < 0)
        CHG_DBG("%s: Fail to register switch low_impedance uevent\n", __func__);
    else
        CHG_DBG("%s: Success to register switch low_impedance uevent\n", __func__);
}
/*---BSP Austin_T Add LOW IMPEDANCE function---*/

/*+++BSP Austin_T Add WATER PROOF function+++*/
struct switch_dev water_proof_dev;
void asus_water_proof_work(struct work_struct *work)
{
	int status;
	int usb_otg_present;
	int ret;
	u8 reg;

	msleep(500);

	ret = smblib_read(smbchg_dev, TYPE_C_STATUS_4_REG, &reg);
	usb_otg_present = reg & CC_ATTACHED_BIT;
	status = gpio_get_value(global_gpio->USB_WATER_PROOF);

	CHG_DBG("%s: status = %d, usb_otg_present = %d\n", __func__, status, usb_otg_present);

	if (status == 1) {
		water_once_flag = 1;
		switch_set_state(&water_proof_dev, 1);
		CHG_DBG("%s: usb_water_proof_interrupt, show warning\n", __func__);
	} else if (usb_otg_present && water_once_flag) {
		switch_set_state(&water_proof_dev, 1);
		CHG_DBG("%s: usb_water_proof_once, show warning\n", __func__);
	} else {
		water_once_flag = 0;
		switch_set_state(&water_proof_dev, 0);
		CHG_DBG("%s: no usb water, cancel warning\n", __func__);
	}
}

//[+++]Add the interrupt handler for usb water proof
static irqreturn_t usb_water_proof_interrupt(int irq, void *dev_id)
{
	int usb_otg_present;
	int status;
	int ret;
	u8 reg;

	msleep(500);

	ret = smblib_read(smbchg_dev, TYPE_C_STATUS_4_REG, &reg);
	usb_otg_present = reg & CC_ATTACHED_BIT;
	status = gpio_get_value(global_gpio->USB_WATER_PROOF);

	CHG_DBG("%s: status = %d, usb_otg_present = %d\n", __func__, status, usb_otg_present);

	if (status == 1) {
		water_once_flag = 1;
		switch_set_state(&water_proof_dev, 1);
		CHG_DBG("%s: usb_water_proof_interrupt, show warning\n", __func__);
	} else if (usb_otg_present && water_once_flag) {
		switch_set_state(&water_proof_dev, 1);
		CHG_DBG("%s: usb_water_proof_once, show warning\n", __func__);
	} else {
		water_once_flag = 0;
		switch_set_state(&water_proof_dev, 0);
		CHG_DBG("%s: no usb water, cancel warning\n", __func__);
	}
	//schedule_work(&smbchg_dev->usb_set_online_work);

	return IRQ_HANDLED;
}
//[---]Add the interrupt handler for usb temperature detection

void register_water_proof(void)
{
	int ret;
	// register switch device for water proof report
	water_proof_dev.name = "vbus_liquid";
	water_proof_dev.index = 0;

	ret = switch_dev_register(&water_proof_dev);
	if (ret < 0)
        CHG_DBG("%s: Fail to register switch water_proof uevent\n", __func__);
    else
        CHG_DBG("%s: Success to register switch water_proof uevent\n", __func__);
}
/*---BSP Austin_T Add WATER PROOF function---*/

void asus_probe_pmic_settings(struct smb_charger *chg)
{
	int rc;

//A-1:0x1365
	rc = smblib_masked_write(chg, USBIN_LOAD_CFG_REG,
			ICL_OVERRIDE_AFTER_APSD_BIT, ICL_OVERRIDE_AFTER_APSD_BIT);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default USBIN_LOAD_CFG_REG rc=%d\n", rc);
	}
//A-2:0x1370
	rc = smblib_write(chg, USBIN_CURRENT_LIMIT_CFG_REG, 0x28);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default USBIN_CURRENT_LIMIT_CFG_REG rc=%d\n", rc);
	}
//A-3:0x1061
	rc = smblib_write(chg, FAST_CHARGE_CURRENT_CFG_REG, 0x3B);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default FAST_CHARGE_CURRENT_CFG_REG rc=%d\n", rc);
	}
//A-4:0x1070
	rc = smblib_write(chg, FLOAT_VOLTAGE_CFG_REG, 0x74);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default FLOAT_VOLTAGE_CFG_REG rc=%d\n", rc);
	}
//A-5:0x1063
	rc = smblib_masked_write(chg, TCCC_CHARGE_CURRENT_TERMINATION_CFG_REG,
			TCCC_CHARGE_CURRENT_TERMINATION_SETTING_MASK, 0x02);
	if (rc < 0) {
			dev_err(chg->dev, "Couldn't set default TCCC_CHARGE_CURRENT rc=%d\n", rc);
		}
//A-6:0x1060
	rc = smblib_masked_write(chg, PRE_CHARGE_CURRENT_CFG_REG,
			PRE_CHARGE_CURRENT_SETTING_MASK, 0x03);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default PRE_CHARGE_CURRENT rc=%d\n", rc);
	}
//A-7:0x1359
	rc = smblib_masked_write(chg, TYPE_C_CFG_2_REG,
			EN_80UA_180UA_CUR_SOURCE_BIT, EN_80UA_180UA_CUR_SOURCE_BIT);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default TYPE_C_CFG_2_REG rc=%d\n", rc);
	}
//A-8:0x1152
	rc = smblib_masked_write(chg, OTG_CURRENT_LIMIT_CFG_REG,
			OTG_CURRENT_LIMIT_MASK, 0x01);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default OTG_CURRENT_LIMIT_CFG_REG rc=%d\n", rc);
	}
//K-1:0x1090
	rc = smblib_write(chg, JEITA_EN_CFG_REG, 0x1F);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default JEITA_EN_CFG_REG rc=%d\n", rc);
	}
//K-2:0x1091
	rc = smblib_write(chg, JEITA_FVCOMP_CFG_REG, 0x22);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default JEITA_FVCOMP_CFG_REG rc=%d\n", rc);
	}
//K-3:0x1092
	rc = smblib_write(chg, JEITA_CCCOMP_CFG_REG, 0x18);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default JEITA_CCCOMP_CFG_REG rc=%d\n", rc);
	}
}

static int smb2_probe(struct platform_device *pdev)
{
	struct smb2 *chip;
	struct smb_charger *chg;
	struct gpio_control *gpio_ctrl;
	int rc = 0;
	union power_supply_propval val;
	int usb_present, batt_present, batt_health, batt_charge_type;
	int usb_alert_irq = 0;
	int low_impedance_irq = 0;
	int water_proof_irq = 0;

	CHG_DBG("%s: start\n", __func__);

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

//ASUS BSP allocate GPIO control +++
	gpio_ctrl = devm_kzalloc(&pdev->dev, sizeof(*gpio_ctrl), GFP_KERNEL);
	if (!gpio_ctrl)
		return -ENOMEM;
//ASUS BSP allocate GPIO control ---

	chg = &chip->chg;
	chg->dev = &pdev->dev;
	chg->param = v1_params;
	chg->debug_mask = &__debug_mask;
	chg->mode = PARALLEL_MASTER;
	chg->irq_info = smb2_irqs;
	chg->name = "PMI";

	wake_lock_init(&asus_chg_lock, WAKE_LOCK_SUSPEND, "asus_chg_lock");
	smbchg_dev = chg;			//ASUS BSP add globe device struct +++
	global_gpio = gpio_ctrl;	//ASUS BSP add gpio control struct +++

	chg->regmap = dev_get_regmap(chg->dev->parent, NULL);
	if (!chg->regmap) {
		pr_err("parent regmap is missing\n");
		return -EINVAL;
	}

	rc = smb2_chg_config_init(chip);
	if (rc < 0) {
		if (rc != -EPROBE_DEFER)
			pr_err("Couldn't setup chg_config rc=%d\n", rc);
		return rc;
	}

	rc = smblib_init(chg);
	if (rc < 0) {
		pr_err("Smblib_init failed rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb2_parse_dt(chip);
	if (rc < 0) {
		pr_err("Couldn't parse device tree rc=%d\n", rc);
		goto cleanup;
	}

	/* set driver data before resources request it */
	platform_set_drvdata(pdev, chip);

	rc = smb2_init_vbus_regulator(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize vbus regulator rc=%d\n",
			rc);
		goto cleanup;
	}

	rc = smb2_init_vconn_regulator(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize vconn regulator rc=%d\n",
				rc);
		goto cleanup;
	}

	/* extcon registration */
	chg->extcon = devm_extcon_dev_allocate(chg->dev, smblib_extcon_cable);
	if (IS_ERR(chg->extcon)) {
		rc = PTR_ERR(chg->extcon);
		dev_err(chg->dev, "failed to allocate extcon device rc=%d\n",
				rc);
		goto cleanup;
	}

	rc = devm_extcon_dev_register(chg->dev, chg->extcon);
	if (rc < 0) {
		dev_err(chg->dev, "failed to register extcon device rc=%d\n",
				rc);
		goto cleanup;
	}

	rc = smb2_init_hw(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize hardware rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb2_init_dc_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize dc psy rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb2_init_usb_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize usb psy rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb2_init_usb_main_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize usb psy rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb2_init_batt_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize batt psy rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb2_determine_initial_status(chip);
	if (rc < 0) {
		pr_err("Couldn't determine initial status rc=%d\n",
			rc);
		goto cleanup;
	}

	rc = smb2_request_interrupts(chip);
	if (rc < 0) {
		pr_err("Couldn't request interrupts rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb2_post_init(chip); 
 	if (rc < 0) { 
 		pr_err("Failed in post init rc=%d\n", rc); 
 		goto cleanup; 
 	}
//ASUS BSP : Add asus_workque +++
	INIT_DELAYED_WORK(&chg->asus_usb_alert_work, asus_usb_alert_work);
	INIT_DELAYED_WORK(&chg->asus_low_impedance_work, asus_low_impedance_work);
	INIT_DELAYED_WORK(&chg->asus_water_proof_work, asus_water_proof_work);

//ASUS BSP : Request ADC_SW_EN-gpios59, ADCPWREN_PMI_GP1-gpios34 +++
	gpio_ctrl->ADC_SW_EN = of_get_named_gpio(pdev->dev.of_node, "ADC_SW_EN-gpios59", 0);
	rc = gpio_request(gpio_ctrl->ADC_SW_EN, "ADC_SW_EN-gpios59");
	if (rc)
		CHG_DBG_E("%s: failed to request ADC_SW_EN-gpios59\n", __func__);
	else
		CHG_DBG("%s: Success to request ADC_SW_EN-gpios59\n", __func__);

	gpio_ctrl->ADCPWREN_PMI_GP1 = of_get_named_gpio(pdev->dev.of_node, "ADCPWREN_PMI_GP1-gpios34", 0);
	rc = gpio_request(gpio_ctrl->ADCPWREN_PMI_GP1, "ADCPWREN_PMI_GP1-gpios34");
	if (rc)
		CHG_DBG_E("%s: failed to request ADCPWREN_PMI_GP1-gpios34\n", __func__);
	else
		CHG_DBG("%s: Success to request ADCPWREN_PMI_GP1-gpios34\n", __func__);
//ASUS BSP : Request Request ADC_SW_EN-gpios59, ADCPWREN_PMI_GP1-gpios34 ---

//ASUS BSP : Charger default PMIC settings +++
	asus_probe_pmic_settings(chg);

//[+++]Add the gpio for USB high temperature alert
	if (g_usb_alert_mode) {
		gpio_ctrl->USB_THERMAL_ALERT = of_get_named_gpio(pdev->dev.of_node, "USB_THERMAL_ALERT-gpios52", 0);
		if (gpio_ctrl->USB_THERMAL_ALERT > 0) {
			CHG_DBG("%s: USB_THERMAL_ALERT-gpios52 init successfully\n", __func__);
			rc = gpio_request(gpio_ctrl->USB_THERMAL_ALERT, "USB_THERMAL_ALERT-gpios52");
			if (rc < 0) {
				CHG_DBG_E("%s: USB_THERMAL_ALERT-gpios52 request failed\n", __func__);
			}
		} else {
			CHG_DBG_E("%s: USB_THERMAL_ALERT-gpios52 init failed\n", __func__);
		}

		register_usb_alert();
		usb_alert_irq = gpio_to_irq(gpio_ctrl->USB_THERMAL_ALERT);
		if (usb_alert_irq < 0) {
			CHG_DBG_E("%s: gpio52_to_irq ERROR(%d).\n", __func__, usb_alert_irq);
		}
		rc = request_threaded_irq(usb_alert_irq, NULL, usb_temp_alert_interrupt,
			IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_ONESHOT | IRQF_NO_SUSPEND, "usb_temp_alert", NULL);	//IRQF_ONESHOT
		if (rc < 0)
			CHG_DBG_E("%s: Failed to request usb_temp_alert_interrupt\n", __func__);
	}
//[---]Add the gpio for USB high temperature alert

//[+++]Add the gpio for USB low impedance alert
	if (0) {
		/*gpio_ctrl->USB_LID_EN = of_get_named_gpio(pdev->dev.of_node, "USB_LID_EN-gpios71", 0);
		if (gpio_ctrl->USB_LID_EN > 0) {
			CHG_DBG("%s: USB_LID_EN-gpios71 init successfully\n", __func__);
			rc = gpio_request(gpio_ctrl->USB_LID_EN, "USB_LID_EN-gpios71");
			if (rc < 0) {
				CHG_DBG_E("%s: USB_LID_EN-gpios71 request failed\n", __func__);
			}
		} else {
			CHG_DBG_E("%s: USB_LID_EN-gpios71 init failed\n", __func__);
		}*/

		gpio_ctrl->USB_LOW_IMPEDANCE = of_get_named_gpio(pdev->dev.of_node, "USB_LOW_IMPEDANCE-gpios77", 0);
		if (gpio_ctrl->USB_LOW_IMPEDANCE > 0) {
			CHG_DBG("%s: USB_LOW_IMPEDANCE-gpios77 init successfully\n", __func__);
			rc = gpio_request(gpio_ctrl->USB_LOW_IMPEDANCE, "USB_LOW_IMPEDANCE-gpios77");
			if (rc < 0) {
				CHG_DBG_E("%s: USB_LOW_IMPEDANCE-gpios77 request failed\n", __func__);
			}
		} else {
			CHG_DBG_E("%s: USB_LOW_IMPEDANCE-gpios77 init failed\n", __func__);
		}

		register_low_impedance();
		low_impedance_irq = gpio_to_irq(gpio_ctrl->USB_LOW_IMPEDANCE);
		if (low_impedance_irq < 0) {
			CHG_DBG_E("%s: gpio77_to_irq ERROR(%d).\n", __func__, low_impedance_irq);
		}
		rc = request_threaded_irq(low_impedance_irq, NULL, usb_low_impedance_interrupt,
			IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_ONESHOT | IRQF_NO_SUSPEND, "usb_low_impedance", NULL);	//IRQF_ONESHOT
		if (rc < 0)
			CHG_DBG_E("%s: Failed to request usb_low_impedance_interrupt\n", __func__);
	}
//[---]Add the gpio for USB low impedance alert

//[+++]Add the gpio for USB water proof alert
	//if (g_water_proof_mode && g_ASUS_hwID >= ZE554KL_PR2) {
	if (0) {
		gpio_ctrl->USB_WATER_PROOF = of_get_named_gpio(pdev->dev.of_node, "USB_WATER_PROOF-gpios73", 0);
		if (gpio_ctrl->USB_WATER_PROOF > 0) {
			CHG_DBG("%s: USB_WATER_PROOF-gpios73 init successfully\n", __func__);
			rc = gpio_request(gpio_ctrl->USB_WATER_PROOF, "USB_WATER_PROOF-gpios73");
			if (rc < 0) {
				CHG_DBG_E("%s: USB_WATER_PROOF-gpios73 request failed\n", __func__);
			}
		} else {
			CHG_DBG_E("%s: USB_WATER_PROOF-gpios73 init failed\n", __func__);
		}

		register_water_proof();
		water_proof_irq = gpio_to_irq(gpio_ctrl->USB_WATER_PROOF);
		if (water_proof_irq < 0) {
			CHG_DBG_E("%s: gpio73_to_irq ERROR(%d).\n", __func__, water_proof_irq);
		}
		rc = request_threaded_irq(water_proof_irq, NULL, usb_water_proof_interrupt,
			IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_ONESHOT | IRQF_NO_SUSPEND, "usb_water_proof", NULL);	//IRQF_ONESHOT
		if (rc < 0)
			CHG_DBG_E("%s: Failed to request usb_water_proof_interrupt\n", __func__);
	}
//[---]Add the gpio for USB water proof alert

// ASUS BSP add a file for SMMI adb interface +++
	create_chargerIC_status_proc_file();
// ASUS BSP add a file for SMMI adb interface ---

//ASUS BSP Austin_T : CHG_ATTRs +++
	rc = sysfs_create_group(&chg->dev->kobj, &asus_smblib_attr_group);
	if (rc)
		goto cleanup;
//ASUS BSP Austin_T : CHG_ATTRs ---

	smb2_create_debugfs(chip);

	rc = smblib_get_prop_usb_present(chg, &val);
	if (rc < 0) {
		pr_err("Couldn't get usb present rc=%d\n", rc);
		goto cleanup;
	}
	usb_present = val.intval;

	rc = smblib_get_prop_batt_present(chg, &val);
	if (rc < 0) {
		pr_err("Couldn't get batt present rc=%d\n", rc);
		goto cleanup;
	}
	batt_present = val.intval;

	rc = smblib_get_prop_batt_health(chg, &val);
	if (rc < 0) {
		pr_err("Couldn't get batt health rc=%d\n", rc);
		val.intval = POWER_SUPPLY_HEALTH_UNKNOWN;
	}
	batt_health = val.intval;

	rc = smblib_get_prop_batt_charge_type(chg, &val);
	if (rc < 0) {
		pr_err("Couldn't get batt charge type rc=%d\n", rc);
		goto cleanup;
	}
	batt_charge_type = val.intval;

	device_init_wakeup(chg->dev, true);

	pr_info("QPNP SMB2 probed successfully usb:present=%d type=%d batt:present = %d health = %d charge = %d\n",
		usb_present, chg->usb_psy_desc.type,
		batt_present, batt_health, batt_charge_type);
	return rc;

cleanup:
	smb2_free_interrupts(chg);
	if (chg->batt_psy)
		power_supply_unregister(chg->batt_psy);
	if (chg->usb_main_psy)
		power_supply_unregister(chg->usb_main_psy);
	if (chg->usb_psy)
		power_supply_unregister(chg->usb_psy);
	if (chg->dc_psy)
		power_supply_unregister(chg->dc_psy);
	if (chg->vconn_vreg && chg->vconn_vreg->rdev)
		devm_regulator_unregister(chg->dev, chg->vconn_vreg->rdev);
	if (chg->vbus_vreg && chg->vbus_vreg->rdev)
		devm_regulator_unregister(chg->dev, chg->vbus_vreg->rdev);

	smblib_deinit(chg);

	platform_set_drvdata(pdev, NULL);

//ASUS BSP Austin_T : CHG_ATTRs +++
	sysfs_remove_group(&chg->dev->kobj, &asus_smblib_attr_group);
//ASUS BSP Austin_T : CHG_ATTRs ---
	return rc;
}

//ASUS BSP Austin_T : Add suspend/resume function +++
#define JEITA_MINIMUM_INTERVAL (30)
static int smb2_resume(struct device *dev)
{
	struct timespec mtNow;
	int nextJEITAinterval;

	if (!asus_get_prop_usb_present(smbchg_dev)) {
		return 0;
	}

	if(!asus_flow_done_flag)
		return 0;

	asus_smblib_stay_awake(smbchg_dev);
	mtNow = current_kernel_time();

	/*BSP Austin_Tseng: if next JEITA time less than 30s, do JEITA
			(next JEITA time = last JEITA time + 60s)*/
	nextJEITAinterval = 60 - (mtNow.tv_sec - last_jeita_time.tv_sec);
	CHG_DBG("%s: nextJEITAinterval = %d\n", __func__, nextJEITAinterval);
	if (nextJEITAinterval <= JEITA_MINIMUM_INTERVAL) {
		smblib_asus_monitor_start(smbchg_dev, 0);
		cancel_delayed_work(&smbchg_dev->asus_batt_RTC_work);
	} else {
		smblib_asus_monitor_start(smbchg_dev, nextJEITAinterval * 1000);
		asus_smblib_relax(smbchg_dev);
	}
	return 0;
}
//ASUS BSP Austin_T : Add suspend/resume function +++

static int smb2_remove(struct platform_device *pdev)
{
	struct smb2 *chip = platform_get_drvdata(pdev);
	struct smb_charger *chg = &chip->chg;

	power_supply_unregister(chg->batt_psy);
	power_supply_unregister(chg->usb_psy);
	regulator_unregister(chg->vconn_vreg->rdev);
	regulator_unregister(chg->vbus_vreg->rdev);

	platform_set_drvdata(pdev, NULL);
	return 0;
}

static void smb2_shutdown(struct platform_device *pdev)
{
	struct smb2 *chip = platform_get_drvdata(pdev);
	struct smb_charger *chg = &chip->chg;

	/* disable all interrupts */
	smb2_disable_interrupts(chg);

	/* configure power role for UFP */
	smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
				TYPEC_POWER_ROLE_CMD_MASK, UFP_EN_CMD_BIT);

	/* force HVDCP to 5V */
	smblib_masked_write(chg, USBIN_OPTIONS_1_CFG_REG,
				HVDCP_AUTONOMOUS_MODE_EN_CFG_BIT, 0);
	smblib_write(chg, CMD_HVDCP_2_REG, FORCE_5V_BIT);

	/* force enable APSD */
	smblib_masked_write(chg, USBIN_OPTIONS_1_CFG_REG,
				 AUTO_SRC_DETECT_BIT, AUTO_SRC_DETECT_BIT);
}

static const struct dev_pm_ops smb2_pm_ops = {
	.resume		= smb2_resume,
};

static const struct of_device_id match_table[] = {
	{ .compatible = "qcom,qpnp-smb2", },
	{ },
};

static struct platform_driver smb2_driver = {
	.driver		= {
		.name		= "qcom,qpnp-smb2",
		.owner		= THIS_MODULE,
		.of_match_table	= match_table,
		.pm			= &smb2_pm_ops,
	},
	.probe		= smb2_probe,
	.remove		= smb2_remove,
	.shutdown	= smb2_shutdown,
};
module_platform_driver(smb2_driver);

MODULE_DESCRIPTION("QPNP SMB2 Charger Driver");
MODULE_LICENSE("GPL v2");
