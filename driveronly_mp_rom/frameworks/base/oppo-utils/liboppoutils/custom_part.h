/************************************************************************************
** File: - Z:\work\code\mtkbase\11071\svn2871_whole\development_emmc\oppo\framework\core\jni\custom_part\share\custom_part.h
** VENDOR_EDIT
** Copyright (C), 2008-2012, OPPO Mobile Comm Corp., Ltd
** 
** Description: 
**      Add this file for customizing OPPO_CUSTOM partition!
** 
** Version: 0.1
** Date created: 20:34:23,09/04/2012
** Author: Jinquan.Lin@BasicDrv.BL
** 
** --------------------------- Revision History: --------------------------------
** 	<author>	                    <data>			<desc>
** Jinquan.Lin@BasicDrv.BL          09/04/2012      Init
************************************************************************************/

#ifndef __CUSTOM_PART_H__
#define __CUSTOM_PART_H__


#define D_OPPO_CUST_PART_BUF_SIZE               (1024)
#define D_OPPO_CUST_PART_BLK_SIZE               (512)

#define D_OPPO_CUST_PART_CLR_FLAG_SOURCE_MAX    (8) //  64/8

#define D_OPPO_CUST_PART_MAGIC_NUM              (0x6F70706F)
#define D_OPPO_CUST_PART_CONFIG_MAGIC_NUM       (0x636F6E66)

typedef enum
{
    OPPO_CUST_PART_ITEM__CONFIG     = 0x00,
    OPPO_CUST_PART_ITEM__MAX        
}TOppoCustPartItem;

typedef struct
{
    unsigned int        nMagicNum1;
    unsigned int        nMagicNum2;
    unsigned int        nPlUsbEnumEnabled;
    unsigned int        nUsbAutoSwitch;
    unsigned long long  nClrFlag;
    unsigned char       sMcpId[16];
	unsigned int 		nGsensorCalibrateFlag;
	int					GsensorData[3];
	unsigned int        nRebootNumber;
	unsigned int        nRebootReason;
//#ifndef VENDOR_EDIT//mingqiang.guo@Prd.BasicDrv.Sensor, add 2012/11/15 add for ps revoer send pulse count 
    unsigned int        nAlsPsCalibratePsFlag;
    unsigned int        nAlsPsSendPulseNumber;
    unsigned int        nAlsPsOffsetValue;
//#endif /*VENDOR_EDIT*/
//#ifndef VENDOR_EDIT//LiuPing@Prd.BasicDrv.Sensor, add 2012/12/05 add for ps switch 
    unsigned int        nAlsPsSwitchPsStoreFlag;
    unsigned int        nAlsPsSwitchPsVal;  // 0:on 1:off
//#endif /*VENDOR_EDIT*/
}TOppoCustConfigInf;

typedef enum
{
    OPPO_CUST_CONFIG_ITEM__PL_USB_ENUM,
    OPPO_CUST_CONFIG_ITEM__CLR_FLAG,
    OPPO_CUST_CONFIG_ITEM__MAX
}TOppoCustConfigItem;

typedef enum
{
    OPPO_CUST_USB_VOTER__MANUAL     =   (1 << 0),
    OPPO_CUST_USB_VOTER__AUTO       =   (1 << 1)
}TOppoCustUsbVoter;

typedef enum
{
    MCP_NONE = 0,
    MCP_KMSJS000KM_B308,
    MCP_H9DP32A4JJACGR_KEM,
    MCP_KMKJS000VM_B309,
    MCP_KMNJS000ZM_B205,
    MCP_H9TP32A4GDMCPR_KDM,
    MCP_KMKJS000VM_B604,
    MCP_KMK3U000VM_B410,
    MCP_OTHER
} MCP_DEV;
#endif/*__CUSTOM_PART_H__*/