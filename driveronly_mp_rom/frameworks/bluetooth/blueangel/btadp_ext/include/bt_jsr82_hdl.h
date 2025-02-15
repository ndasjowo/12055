/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein
 * is confidential and proprietary to MediaTek Inc. and/or its licensors.
 * Without the prior written permission of MediaTek inc. and/or its licensors,
 * any reproduction, modification, use or disclosure of MediaTek Software,
 * and information contained herein, in whole or in part, shall be strictly prohibited.
 *
 * MediaTek Inc. (C) 2010. All rights reserved.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER ON
 * AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
 * NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
 * SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
 * SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
 * THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
 * THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
 * CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
 * SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
 * CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
 * AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
 * OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
 * MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek Software")
 * have been modified by MediaTek Inc. All revisions are subject to any receiver's
 * applicable license agreements with MediaTek Inc.
 */

/* 
 * bt_jsr82_hdl.h
 * 
 * This file is the header file of External Adaptation handler of JSR82 used by Virtual Machine.
 * Flow direction: VM <-- external ADP handler
 */


#ifndef __BT_JSR82_HDL_H__
#define __BT_JSR82_HDL_H__

#include "bt_mmi.h"


void jbt_handle_session_enabled_result(ilm_struct *message);
void jbt_handle_session_deregistration_result(ilm_struct *message);
void jbt_handle_session_turnon_result(ilm_struct *message);
void jbt_handle_session_turnoff_result(ilm_struct *message);
void jbt_handle_session_connect_ind(ilm_struct *message);
void jbt_handle_session_disconnect_ind(ilm_struct *message);
void jbt_handle_session_connect_req_cnf(ilm_struct *message);
void jbt_handle_session_rx_ready_ind(ilm_struct *message);
void jbt_handle_session_tx_ready_ind(ilm_struct *message);
void jbt_handle_session_put_bytes_cnf(ilm_struct *message);
void jbt_handle_session_get_bytes_cnf(ilm_struct *message);
void jbt_handle_session_data_available_ind(ilm_struct *message);
void jbt_handle_session_assign_buffer_cnf(ilm_struct *message);
void jbt_handle_session_send_tx_ready_ind(U8 index, U16 l2cap_id, U8 ps_type, BT_BOOL isTxEmpty);
void jbt_handle_session_send_rx_ready_ind(U8 index, U16 l2cap_id, U16 length, U8 ps_type);
void btmtk_jsr82_handle_message(ilm_struct *message);


#endif	/* __BT_JSR82_HDL_H__ */


