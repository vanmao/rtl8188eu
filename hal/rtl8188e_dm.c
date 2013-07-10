/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
/*  */
/*  Description: */
/*  */
/*  This file is for 92CE/92CU dynamic mechanism only */
/*  */
/*  */
/*  */
#define _RTL8188E_DM_C_

/*  */
/*  include files */
/*  */
#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>

#include <rtl8188e_hal.h>

/*  */
/*  Global var */
/*  */


static void
dm_CheckProtection(
		PADAPTER	Adapter
	)
{
}

static void
dm_CheckStatistics(
		PADAPTER	Adapter
	)
{
}

static void dm_CheckPbcGPIO(_adapter *padapter)
{
	u8	tmp1byte;
	u8	bPbcPressed = false;

	if (!padapter->registrypriv.hw_wps_pbc)
		return;

#ifdef CONFIG_USB_HCI
	tmp1byte = rtw_read8(padapter, GPIO_IO_SEL);
	tmp1byte |= (HAL_8192C_HW_GPIO_WPS_BIT);
	rtw_write8(padapter, GPIO_IO_SEL, tmp1byte);	/* enable GPIO[2] as output mode */

	tmp1byte &= ~(HAL_8192C_HW_GPIO_WPS_BIT);
	rtw_write8(padapter,  GPIO_IN, tmp1byte);		/* reset the floating voltage level */

	tmp1byte = rtw_read8(padapter, GPIO_IO_SEL);
	tmp1byte &= ~(HAL_8192C_HW_GPIO_WPS_BIT);
	rtw_write8(padapter, GPIO_IO_SEL, tmp1byte);	/* enable GPIO[2] as input mode */

	tmp1byte =rtw_read8(padapter, GPIO_IN);

	if (tmp1byte == 0xff)
		return ;

	if (tmp1byte&HAL_8192C_HW_GPIO_WPS_BIT)
	{
		bPbcPressed = true;
	}
#else
	tmp1byte = rtw_read8(padapter, GPIO_IN);

	if (tmp1byte == 0xff || padapter->init_adpt_in_progress)
		return ;

	if ((tmp1byte&HAL_8192C_HW_GPIO_WPS_BIT)==0)
	{
		bPbcPressed = true;
	}
#endif

	if ( true == bPbcPressed)
	{
		/*  Here we only set bPbcPressed to true */
		/*  After trigger PBC, the variable will be set to false */
		DBG_88E("CheckPbcGPIO - PBC is pressed\n");

#ifdef RTK_DMP_PLATFORM
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,12))
		kobject_uevent(&padapter->pnetdev->dev.kobj, KOBJ_NET_PBC);
#else
		kobject_hotplug(&padapter->pnetdev->class_dev.kobj, KOBJ_NET_PBC);
#endif
#else

		if ( padapter->pid[0] == 0 )
		{	/* 	0 is the default value and it means the application monitors the HW PBC doesn't privde its pid to driver. */
			return;
		}

#ifdef PLATFORM_LINUX
		rtw_signal_process(padapter->pid[0], SIGUSR1);
#endif
#endif
	}
}

#ifdef CONFIG_PCI_HCI
/*  */
/* 	Description: */
/* 		Perform interrupt migration dynamically to reduce CPU utilization. */
/*  */
/* 	Assumption: */
/* 		1. Do not enable migration under WIFI test. */
/*  */
/* 	Created by Roger, 2010.03.05. */
/*  */
void
dm_InterruptMigration(
		PADAPTER	Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	bool			bCurrentIntMt, bCurrentACIntDisable;
	bool			IntMtToSet = false;
	bool			ACIntToSet = false;


	/*  Retrieve current interrupt migration and Tx four ACs IMR settings first. */
	bCurrentIntMt = pHalData->bInterruptMigration;
	bCurrentACIntDisable = pHalData->bDisableTxInt;

	/*  */
	/*  <Roger_Notes> Currently we use busy traffic for reference instead of RxIntOK counts to prevent non-linear Rx statistics */
	/*  when interrupt migration is set before. 2010.03.05. */
	/*  */
	if (!Adapter->registrypriv.wifi_spec &&
		(check_fwstate(pmlmepriv, _FW_LINKED)== true) &&
		pmlmepriv->LinkDetectInfo.bHigherBusyTraffic)
	{
		IntMtToSet = true;

		/*  To check whether we should disable Tx interrupt or not. */
		if (pmlmepriv->LinkDetectInfo.bHigherBusyRxTraffic )
			ACIntToSet = true;
	}

	/* Update current settings. */
	if ( bCurrentIntMt != IntMtToSet ){
		DBG_88E("%s(): Update interrrupt migration(%d)\n",__func__,IntMtToSet);
		if (IntMtToSet)
		{
			/*  */
			/*  <Roger_Notes> Set interrrupt migration timer and corresponging Tx/Rx counter. */
			/*  timer 25ns*0xfa0=100us for 0xf packets. */
			/*  2010.03.05. */
			/*  */
			rtw_write32(Adapter, REG_INT_MIG, 0xff000fa0);/*  0x306:Rx, 0x307:Tx */
			pHalData->bInterruptMigration = IntMtToSet;
		} else {
			/*  Reset all interrupt migration settings. */
			rtw_write32(Adapter, REG_INT_MIG, 0);
			pHalData->bInterruptMigration = IntMtToSet;
		}
	}
}

#endif

/*  */
/*  Initialize GPIO setting registers */
/*  */
static void
dm_InitGPIOSetting(
		PADAPTER	Adapter
	)
{
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(Adapter);

	u8	tmp1byte;

	tmp1byte = rtw_read8(Adapter, REG_GPIO_MUXCFG);
	tmp1byte &= (GPIOSEL_GPIO | ~GPIOSEL_ENBT);

#ifdef CONFIG_BT_COEXIST
	/*  UMB-B cut bug. We need to support the modification. */
	if (IS_81xxC_VENDOR_UMC_B_CUT(pHalData->VersionID) &&
		pHalData->bt_coexist.BT_Coexist)
	{
		tmp1byte |= (BIT5);
	}
#endif
	rtw_write8(Adapter, REG_GPIO_MUXCFG, tmp1byte);

}

/*  */
/*  functions */
/*  */
static void Init_ODM_ComInfo_88E(PADAPTER	Adapter)
{

	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	PDM_ODM_T		pDM_Odm = &(pHalData->odmpriv);
	u8	cut_ver,fab_ver;

	/*  */
	/*  Init Value */
	/*  */
	_rtw_memset(pDM_Odm,0,sizeof(pDM_Odm));

	pDM_Odm->Adapter = Adapter;

	ODM_CmnInfoInit(pDM_Odm,ODM_CMNINFO_PLATFORM,ODM_CE);

	if (Adapter->interface_type == RTW_GSPI )
		ODM_CmnInfoInit(pDM_Odm,ODM_CMNINFO_INTERFACE,ODM_ITRF_SDIO);
	else
		ODM_CmnInfoInit(pDM_Odm,ODM_CMNINFO_INTERFACE,Adapter->interface_type);/* RTL871X_HCI_TYPE */

	ODM_CmnInfoInit(pDM_Odm,ODM_CMNINFO_IC_TYPE,ODM_RTL8188E);

	fab_ver = ODM_TSMC;
	cut_ver = ODM_CUT_A;

	ODM_CmnInfoInit(pDM_Odm,ODM_CMNINFO_FAB_VER,fab_ver);
	ODM_CmnInfoInit(pDM_Odm,ODM_CMNINFO_CUT_VER,cut_ver);

	ODM_CmnInfoInit(pDM_Odm,	ODM_CMNINFO_MP_TEST_CHIP,IS_NORMAL_CHIP(pHalData->VersionID));

	ODM_CmnInfoInit(pDM_Odm,ODM_CMNINFO_PATCH_ID,pHalData->CustomerID);
	ODM_CmnInfoInit(pDM_Odm,ODM_CMNINFO_BWIFI_TEST,Adapter->registrypriv.wifi_spec);


	if (pHalData->rf_type == RF_1T1R){
		ODM_CmnInfoUpdate(pDM_Odm,ODM_CMNINFO_RF_TYPE,ODM_1T1R);
	}
	else if (pHalData->rf_type == RF_2T2R){
		ODM_CmnInfoUpdate(pDM_Odm,ODM_CMNINFO_RF_TYPE,ODM_2T2R);
	}
	else if (pHalData->rf_type == RF_1T2R){
		ODM_CmnInfoUpdate(pDM_Odm,ODM_CMNINFO_RF_TYPE,ODM_1T2R);
	}

	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_RF_ANTENNA_TYPE, pHalData->TRxAntDivType);

	#ifdef CONFIG_DISABLE_ODM
	pdmpriv->InitODMFlag = 0;
	#else
	pdmpriv->InitODMFlag =	ODM_RF_CALIBRATION		|
							ODM_RF_TX_PWR_TRACK	/*  */
							;
	#endif

	ODM_CmnInfoUpdate(pDM_Odm,ODM_CMNINFO_ABILITY,pdmpriv->InitODMFlag);

}
static void Update_ODM_ComInfo_88E(PADAPTER	Adapter)
{
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;
	struct pwrctrl_priv *pwrctrlpriv = &Adapter->pwrctrlpriv;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &(pHalData->odmpriv);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	int i;
	#ifdef CONFIG_DISABLE_ODM
	pdmpriv->InitODMFlag = 0;
	#else /* CONFIG_DISABLE_ODM */

	pdmpriv->InitODMFlag =	ODM_BB_DIG				|
#ifdef	CONFIG_ODM_REFRESH_RAMASK
							ODM_BB_RA_MASK		|
#endif
							ODM_BB_DYNAMIC_TXPWR	|
							ODM_BB_FA_CNT			|
							ODM_BB_RSSI_MONITOR	|
							ODM_BB_CCK_PD			|
							ODM_BB_PWR_SAVE		|
							ODM_MAC_EDCA_TURBO	|
							ODM_RF_CALIBRATION		|
							ODM_RF_TX_PWR_TRACK
							;
	if (pHalData->AntDivCfg)
		pdmpriv->InitODMFlag |= ODM_BB_ANT_DIV;

	#if (MP_DRIVER==1)
		if (Adapter->registrypriv.mp_mode == 1)
		{
		pdmpriv->InitODMFlag =	ODM_RF_CALIBRATION	|
								ODM_RF_TX_PWR_TRACK;
		}
	#endif/* MP_DRIVER==1) */

	#endif/* CONFIG_DISABLE_ODM */
	ODM_CmnInfoUpdate(pDM_Odm,ODM_CMNINFO_ABILITY,pdmpriv->InitODMFlag);

	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_TX_UNI,&(Adapter->xmitpriv.tx_bytes));
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_RX_UNI,&(Adapter->recvpriv.rx_bytes));
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_WM_MODE,&(pmlmeext->cur_wireless_mode));
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_SEC_CHNL_OFFSET,&(pHalData->nCur40MhzPrimeSC));
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_SEC_MODE,&(Adapter->securitypriv.dot11PrivacyAlgrthm));
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_BW,&(pHalData->CurrentChannelBW ));
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_CHNL,&( pHalData->CurrentChannel));
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_NET_CLOSED,&( Adapter->net_closed));
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_MP_MODE,&(Adapter->registrypriv.mp_mode));
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_SCAN,&(pmlmepriv->bScanInProcess));
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_POWER_SAVING,&(pwrctrlpriv->bpower_saving));
	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_RF_ANTENNA_TYPE, pHalData->TRxAntDivType);

	for (i=0; i< NUM_STA; i++)
		ODM_CmnInfoPtrArrayHook(pDM_Odm, ODM_CMNINFO_STA_STATUS,i,NULL);
}

void
rtl8188e_InitHalDm(
		PADAPTER	Adapter
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	PDM_ODM_T		pDM_Odm = &(pHalData->odmpriv);
	u8	i;

#ifdef CONFIG_USB_HCI
	dm_InitGPIOSetting(Adapter);
#endif

	pdmpriv->DM_Type = DM_Type_ByDriver;
	pdmpriv->DMFlag = DYNAMIC_FUNC_DISABLE;

	Update_ODM_ComInfo_88E(Adapter);
	ODM_DMInit(pDM_Odm);

	Adapter->fix_rate = 0xFF;

}


void
rtl8188e_HalDmWatchDog(
		PADAPTER	Adapter
	)
{
	bool		bFwCurrentInPSMode = false;
	bool		bFwPSAwake = true;
	u8 hw_init_completed = false;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	PDM_ODM_T		pDM_Odm = &(pHalData->odmpriv);
#ifdef CONFIG_CONCURRENT_MODE
	PADAPTER pbuddy_adapter = Adapter->pbuddy_adapter;
#endif /* CONFIG_CONCURRENT_MODE */

	_func_enter_;

	#if defined(CONFIG_CONCURRENT_MODE)
	if (Adapter->isprimary == false && pbuddy_adapter) {
		hw_init_completed = pbuddy_adapter->hw_init_completed;
	} else
	#endif
	{
		hw_init_completed = Adapter->hw_init_completed;
	}

	if (hw_init_completed == false)
		goto skip_dm;

#ifdef CONFIG_LPS
	#if defined(CONFIG_CONCURRENT_MODE)
	if (Adapter->iface_type != IFACE_PORT0 && pbuddy_adapter) {
		bFwCurrentInPSMode = pbuddy_adapter->pwrctrlpriv.bFwCurrentInPSMode;
		rtw_hal_get_hwreg(pbuddy_adapter, HW_VAR_FWLPS_RF_ON, (u8 *)(&bFwPSAwake));
	} else
	#endif
	{
		bFwCurrentInPSMode = Adapter->pwrctrlpriv.bFwCurrentInPSMode;
		rtw_hal_get_hwreg(Adapter, HW_VAR_FWLPS_RF_ON, (u8 *)(&bFwPSAwake));
	}
#endif

#ifdef CONFIG_P2P_PS
	/*  Fw is under p2p powersaving mode, driver should stop dynamic mechanism. */
	/*  modifed by thomas. 2011.06.11. */
	if (Adapter->wdinfo.p2p_ps_mode)
		bFwPSAwake = false;
#endif /* CONFIG_P2P_PS */

	if ( (hw_init_completed == true)
		&& ((!bFwCurrentInPSMode) && bFwPSAwake))
	{
		/*  */
		/*  Calculate Tx/Rx statistics. */
		/*  */
		dm_CheckStatistics(Adapter);

#ifdef CONFIG_CONCURRENT_MODE
		if (Adapter->adapter_type > PRIMARY_ADAPTER)
			goto _record_initrate;
#endif

_record_initrate:
	_func_exit_;
	}


	/* ODM */
	if (hw_init_completed == true)
	{
		struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;
		u8	bLinked=false;
		#ifdef CONFIG_DISABLE_ODM
		pHalData->odmpriv.SupportAbility = 0;
		#endif

		if (	(check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) ||
			(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE) == true))
		{
			if (Adapter->stapriv.asoc_sta_count > 2)
				bLinked = true;
		}
		else{/* Station mode */
			if (check_fwstate(pmlmepriv, _FW_LINKED)== true)
				bLinked = true;
		}

#ifdef CONFIG_CONCURRENT_MODE
		if (check_buddy_fw_link(Adapter))
			bLinked = true;
#endif /* CONFIG_CONCURRENT_MODE */

		ODM_CmnInfoUpdate(&pHalData->odmpriv ,ODM_CMNINFO_LINK, bLinked);
		ODM_DMWatchdog(&pHalData->odmpriv);

	}

skip_dm:

	/*  Check GPIO to determine current RF on/off and Pbc status. */
	/*  Check Hardware Radio ON/OFF or not */
#ifdef CONFIG_PCI_HCI
	if (pHalData->bGpioHwWpsPbc)
#endif
	{
		/* temp removed */
		/* dm_CheckPbcGPIO(Adapter); */
	}
	return;
}

void rtl8188e_init_dm_priv(PADAPTER Adapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	PDM_ODM_T		podmpriv = &pHalData->odmpriv;
	_rtw_memset(pdmpriv, 0, sizeof(struct dm_priv));
	Init_ODM_ComInfo_88E(Adapter);
#ifdef CONFIG_SW_ANTENNA_DIVERSITY
	ODM_InitAllTimers(podmpriv );
#endif
	ODM_InitDebugSetting(podmpriv);
}

void rtl8188e_deinit_dm_priv(PADAPTER Adapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	PDM_ODM_T		podmpriv = &pHalData->odmpriv;
#ifdef CONFIG_SW_ANTENNA_DIVERSITY
	ODM_CancelAllTimers(podmpriv);
#endif
}


#ifdef CONFIG_ANTENNA_DIVERSITY
/*  Add new function to reset the state of antenna diversity before link. */
/*  */
/*  Compare RSSI for deciding antenna */
void	AntDivCompare8188E(PADAPTER Adapter, WLAN_BSSID_EX *dst, WLAN_BSSID_EX *src)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	if (0 != pHalData->AntDivCfg )
	{
		/* select optimum_antenna for before linked =>For antenna diversity */
		if (dst->Rssi >=  src->Rssi )/* keep org parameter */
		{
			src->Rssi = dst->Rssi;
			src->PhyInfo.Optimum_antenna = dst->PhyInfo.Optimum_antenna;
		}
	}
}

/*  Add new function to reset the state of antenna diversity before link. */
u8 AntDivBeforeLink8188E(PADAPTER Adapter )
{

	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T	pDM_Odm =&pHalData->odmpriv;
	SWAT_T		*pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);

	/*  Condition that does not need to use antenna diversity. */
	if (pHalData->AntDivCfg==0)
		return false;

	if (check_fwstate(pmlmepriv, _FW_LINKED) == true)
	{
		return false;
	}


	if (pDM_SWAT_Table->SWAS_NoLink_State == 0){
		/* switch channel */
		pDM_SWAT_Table->SWAS_NoLink_State = 1;
		pDM_SWAT_Table->CurAntenna = (pDM_SWAT_Table->CurAntenna==Antenna_A)?Antenna_B:Antenna_A;

		rtw_antenna_select_cmd(Adapter, pDM_SWAT_Table->CurAntenna, false);
		return true;
	} else {
		pDM_SWAT_Table->SWAS_NoLink_State = 0;
		return false;
	}

}
#endif
