/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.internal.policy.impl.keyguard;

import android.app.ActivityManager;
import android.app.ActivityManager.RunningAppProcessInfo;
import android.app.ActivityManagerNative;
import android.app.AlertDialog;
import android.app.IUserSwitchObserver;
import android.app.admin.DevicePolicyManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.ComponentName;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.database.ContentObserver;
import android.graphics.drawable.Drawable;
import android.media.AudioManager;
import android.net.sip.SipManager;
import android.os.BatteryManager;
import static android.os.BatteryManager.BATTERY_STATUS_FULL;
import static android.os.BatteryManager.BATTERY_STATUS_UNKNOWN;
import static android.os.BatteryManager.BATTERY_HEALTH_UNKNOWN;
import static android.os.BatteryManager.EXTRA_STATUS;
import static android.os.BatteryManager.EXTRA_PLUGGED;
import static android.os.BatteryManager.EXTRA_LEVEL;
import static android.os.BatteryManager.EXTRA_HEALTH;
import android.os.Handler;
import android.os.IBinder;
import android.os.IRemoteCallback;
import android.os.Message;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.os.SystemProperties;
import android.provider.Settings;
import android.provider.Settings.System;
import static android.provider.Telephony.Intents.ACTION_DUAL_SIM_MODE_SELECT;
import static android.provider.Telephony.Intents.EXTRA_PLMN;
import static android.provider.Telephony.Intents.EXTRA_SHOW_PLMN;
import static android.provider.Telephony.Intents.EXTRA_SHOW_SPN;
import static android.provider.Telephony.Intents.EXTRA_SPN;
import static android.provider.Telephony.Intents.SPN_STRINGS_UPDATED_ACTION;
import android.provider.Telephony.SIMInfo;
import android.telephony.PhoneStateListener;
import android.telephony.ServiceState;
import android.telephony.TelephonyManager;
import android.view.LayoutInflater;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.TextView;

import com.android.internal.telephony.gemini.GeminiNetworkSubUtil;
import com.android.internal.telephony.gemini.GeminiPhone;
import com.android.internal.telephony.ITelephony;
import com.android.internal.telephony.ITelephonyRegistry;
import com.android.internal.telephony.IccCardConstants;
import com.android.internal.telephony.PhoneConstants;
import com.android.internal.telephony.TelephonyIntents;
import static com.android.internal.telephony.TelephonyIntents.EXTRA_CALIBRATION_DATA;

import android.util.Log;
import com.android.internal.R;
import com.google.android.collect.Lists;

import com.mediatek.common.dm.DMAgent;

import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.LinkedList;
import java.util.List;
import java.util.Queue;

/**
 * Watches for updates that may be interesting to the keyguard, and provides
 * the up to date information as well as a registration for callbacks that care
 * to be updated.
 *
 * Note: under time crunch, this has been extended to include some stuff that
 * doesn't really belong here.  see {@link #handleBatteryUpdate} where it shutdowns
 * the device, and {@link #getFailedUnlockAttempts()}, {@link #reportFailedAttempt()}
 * and {@link #clearFailedUnlockAttempts()}.  Maybe we should rename this 'KeyguardContext'...
 */
public class KeyguardUpdateMonitor {

    private static final String TAG = "KeyguardUpdateMonitor";
    private static boolean DEBUG = true;
    private static boolean DEBUG_SIM_STATES = DEBUG || false;
    private static final int FAILED_BIOMETRIC_UNLOCK_ATTEMPTS_BEFORE_BACKUP = 3;
    /**
     * package 
     * M: Change the threshold to 16 for mediatek device
     */
    static final int LOW_BATTERY_THRESHOLD = 16;

    // Callback messages
    private static final int MSG_TIME_UPDATE = 301;
    private static final int MSG_BATTERY_UPDATE = 302;
    private static final int MSG_CARRIER_INFO_UPDATE = 303;
    private static final int MSG_SIM_STATE_CHANGE = 304;
    private static final int MSG_RINGER_MODE_CHANGED = 305;
    private static final int MSG_PHONE_STATE_CHANGED = 306;
    private static final int MSG_CLOCK_VISIBILITY_CHANGED = 307;
    private static final int MSG_DEVICE_PROVISIONED = 308;
    private static final int MSG_DPM_STATE_CHANGED = 309;
    private static final int MSG_USER_SWITCHED = 310;
    private static final int MSG_USER_REMOVED = 311;
    private static final int MSG_KEYGUARD_VISIBILITY_CHANGED = 312;
    protected static final int MSG_BOOT_COMPLETED = 313;


    private static KeyguardUpdateMonitor sInstance;

    private final Context mContext;

    // Telephony state
    /// M: Set default sim state to UNKNOWN inseatd of READY
    private IccCardConstants.State mSimState = IccCardConstants.State.UNKNOWN;
    private CharSequence mTelephonyPlmn;
    private CharSequence mTelephonySpn;
    private int mRingMode;
    private int mPhoneState;
    private boolean mKeyguardIsVisible;
    private boolean mBootCompleted;

    // Device provisioning state
    private boolean mDeviceProvisioned;

    // Device provisioning state
    private boolean mSIMResetModem;

    // Battery status
    private BatteryStatus mBatteryStatus;

    // Password attempts
    private int mFailedAttempts = 0;
    private int mFailedBiometricUnlockAttempts = 0;

    private boolean mAlternateUnlockEnabled;

    private boolean mClockVisible;

    private final ArrayList<WeakReference<KeyguardUpdateMonitorCallback>>
            mCallbacks = Lists.newArrayList();
    private ContentObserver mContentObserver;

    /// M: Incoming Indicator for Keyguard Rotation
    private long mQueryBaseTime;
    private static final String CLEAR_NEW_EVENT_VIEW_INTENT = "android.intent.action.KEYGUARD_CLEAR_UREAD_TIPS";

    private final Handler mHandler = new Handler() {
        @Override
        public void handleMessage(Message msg) {
            switch (msg.what) {
                case MSG_TIME_UPDATE:
                    handleTimeUpdate();
                    break;
                case MSG_BATTERY_UPDATE:
                    handleBatteryUpdate((BatteryStatus) msg.obj);
                    break;
                case MSG_CARRIER_INFO_UPDATE:
                    handleCarrierInfoUpdate((SpnUpdate) msg.obj);
                    break;
                case MSG_SIM_STATE_CHANGE:
                    handleSimStateChange((SimArgs) msg.obj);
                    break;
                case MSG_RINGER_MODE_CHANGED:
                    handleRingerModeChange(msg.arg1);
                    break;
                case MSG_PHONE_STATE_CHANGED:
                    handlePhoneStateChanged((String)msg.obj);
                    break;
                case MSG_CLOCK_VISIBILITY_CHANGED:
                    handleClockVisibilityChanged();
                    break;
                case MSG_DEVICE_PROVISIONED:
                    handleDeviceProvisioned();
                    break;
                case MSG_DPM_STATE_CHANGED:
                    handleDevicePolicyManagerStateChanged();
                    break;
                case MSG_USER_SWITCHED:
                    handleUserSwitched(msg.arg1, (IRemoteCallback)msg.obj);
                    break;
                case MSG_USER_REMOVED:
                    handleUserRemoved(msg.arg1);
                    break;
                case MSG_KEYGUARD_VISIBILITY_CHANGED:
                    handleKeyguardVisibilityChanged(msg.arg1);
                    break;
                case MSG_BOOT_COMPLETED:
                    handleBootCompleted();
                    break;
                /// M: Mediatek added message begin @{
                case MSG_CONFIGURATION_CHANGED:
                        updateResources();
                        break;
                case MSG_BOOTUP_MODE_PICK:
                    /* bootup mode picker */
                    handleBootupModePick();
                    break;
                case MSG_SIMINFO_CHANGED:
                    handleSIMInfoChanged(msg.arg1);
                    break;
                case MSG_KEYGUARD_RESET_DISMISS:
                    mPinPukDismissFlag = PIN_PUK_RESET;
                    break;
                case MSG_SIM_DETECTED:
                    handleSIMCardChanged();
                    break;
                case MSG_KEYGUARD_UPDATE_LAYOUT:
                    KeyguardUtils.xlogD(TAG, "MSG_KEYGUARD_UPDATE_LAYOUT, msg.arg1=" + msg.arg1);
                    handleLockScreenUpdateLayout(msg.arg1);
                    break;
                /** add this for sim name workaround for framework**/
                case MSG_KEYGUARD_SIM_NAME_UPDATE:
                    KeyguardUtils.xlogD(TAG, "MSG_KEYGUARD_SIM_NAME_UPDATE, msg.arg1=" + msg.arg1);
                    handleSIMNameUpdate(msg.arg1);
                    break;
                case MSG_MODEM_RESET:
                    KeyguardUtils.xlogD(TAG, "MSG_MODEM_RESET, msg.arg1=" + msg.arg1);
                    handleRadioStateChanged(msg.arg1);
                    break;
                case MSG_PRE_3G_SWITCH:
                    handle3GSwitchEvent();
                    break;
                case MSG_SYSTEM_STATE:
                    KeyguardUtils.xlogD(TAG, "MSG_SYSTEM_STATE, msg.arg1=" + msg.arg1);
                    handleSystemStateChanged(msg.arg1);
                    break;
                case MSG_DOWNLOAD_CALIBRATION_DATA_UPDATE:
                    handleDownloadCalibrationDataUpdate();
                    break;
                /// M: Mediatek mesage end @}
            }
        }
    };

	@android.annotation.OppoHook(level=android.annotation.OppoHook.OppoHookType.CHANGE_CODE,
		                      note="zhangkai change for OppoNewSimDetect")
		                      
    private final BroadcastReceiver mBroadcastReceiver = new BroadcastReceiver() {

        public void onReceive(Context context, Intent intent) {
            final String action = intent.getAction();
            if (DEBUG) Log.d(TAG, "received broadcast " + action);

            if (Intent.ACTION_TIME_TICK.equals(action)
                    || Intent.ACTION_TIME_CHANGED.equals(action)
                    || Intent.ACTION_TIMEZONE_CHANGED.equals(action)) {
                mHandler.sendMessage(mHandler.obtainMessage(MSG_TIME_UPDATE));
            } else if (TelephonyIntents.SPN_STRINGS_UPDATED_ACTION.equals(action)) {
                /// M: Also handle Gemini phone's plmn update
                SpnUpdate spnUpdate = new SpnUpdate();
                if (KeyguardUtils.isGemini()) {
                	if (intent.getIntExtra(PhoneConstants.GEMINI_SIM_ID_KEY, PhoneConstants.GEMINI_SIM_1) == PhoneConstants.GEMINI_SIM_1) {
                       	mTelephonyPlmn = getTelephonyPlmnFrom(intent);
                        mTelephonySpn = getTelephonySpnFrom(intent);
                        spnUpdate.simId = PhoneConstants.GEMINI_SIM_1;
                        KeyguardUtils.xlogD(TAG, "SPN_STRINGS_UPDATED_ACTION, update sim1, plmn="+mTelephonyPlmn
                                +", spn=" + mTelephonySpn);
                    } else {
                        mTelephonyPlmnGemini = getTelephonyPlmnFrom(intent);
                        mTelephonySpnGemini = getTelephonySpnFrom(intent);
                        spnUpdate.simId = PhoneConstants.GEMINI_SIM_2;
                        KeyguardUtils.xlogD(TAG, "SPN_STRINGS_UPDATED_ACTION, update sim2, plmn="+mTelephonyPlmnGemini
                                +", spn=" + mTelephonySpnGemini);
                    }
                } else {
                	mTelephonyPlmn = getTelephonyPlmnFrom(intent);
                    mTelephonySpn = getTelephonySpnFrom(intent);
                    KeyguardUtils.xlogD(TAG, "SPN_STRINGS_UPDATED_ACTION, update sim, plmn="+mTelephonyPlmn
                            +", spn=" + mTelephonySpn);
                }
                mHandler.sendMessage(mHandler.obtainMessage(MSG_CARRIER_INFO_UPDATE, spnUpdate));
            } else if (Intent.ACTION_BATTERY_CHANGED.equals(action)) {
                final int status = intent.getIntExtra(EXTRA_STATUS, BATTERY_STATUS_UNKNOWN);
                final int plugged = intent.getIntExtra(EXTRA_PLUGGED, 0);
                final int level = intent.getIntExtra(EXTRA_LEVEL, 0);
                final int health = intent.getIntExtra(EXTRA_HEALTH, BATTERY_HEALTH_UNKNOWN);
                final Message msg = mHandler.obtainMessage(
                        MSG_BATTERY_UPDATE, new BatteryStatus(status, level, plugged, health));
                mHandler.sendMessage(msg);
            } else if (TelephonyIntents.ACTION_SIM_STATE_CHANGED.equals(action)) {
                int simId = 0;
                    String stateExtra = intent.getStringExtra(IccCardConstants.INTENT_KEY_ICC_STATE);
                    if (KeyguardUtils.isGemini()) {
                       simId = intent.getIntExtra(PhoneConstants.GEMINI_SIM_ID_KEY, PhoneConstants.GEMINI_SIM_1);
                    }
                    if (DEBUG_SIM_STATES) {
                        Log.v(TAG, "action " + action + " state" + stateExtra);
                    }
                    /// M: if sim state change to ready, we reset its dismiss flag, or user may not able 
                    /// to unlock puk @{
                    if (IccCardConstants.INTENT_VALUE_ICC_READY.equals(stateExtra)) {
	                    if (simId== PhoneConstants.GEMINI_SIM_2) {
	                        mPinPukDismissFlag &= ~SIM_2_PIN_DISMISSED;
	                        mPinPukDismissFlag &= ~SIM_2_PUK_DISMISSED;
	                    } else {
	                        mPinPukDismissFlag &= ~SIM_1_PIN_DISMISSED;
	                        mPinPukDismissFlag &= ~SIM_1_PUK_DISMISSED;
	                    }   	
                    }
                    /// @}
                    KeyguardUtils.xlogD(TAG, "ACTION_SIM_STATE_CHANGED, stateExtra="+stateExtra+",simId="+simId);
                    mHandler.sendMessage(mHandler.obtainMessage(
                            MSG_SIM_STATE_CHANGE,
                            SimArgs.fromIntent(intent)));
            } else if (AudioManager.RINGER_MODE_CHANGED_ACTION.equals(action)) {
                 if (DEBUG) KeyguardUtils.xlogD(TAG, "RINGER_MODE_CHANGED_ACTION received");
                mHandler.sendMessage(mHandler.obtainMessage(MSG_RINGER_MODE_CHANGED,
                        intent.getIntExtra(AudioManager.EXTRA_RINGER_MODE, -1), 0));
            } else if (TelephonyManager.ACTION_PHONE_STATE_CHANGED.equals(action)) {
                String state = intent.getStringExtra(TelephonyManager.EXTRA_STATE);
                mHandler.sendMessage(mHandler.obtainMessage(MSG_PHONE_STATE_CHANGED, state));
            } else if (DevicePolicyManager.ACTION_DEVICE_POLICY_MANAGER_STATE_CHANGED
                    .equals(action)) {
                mHandler.sendMessage(mHandler.obtainMessage(MSG_DPM_STATE_CHANGED));
            } else if (Intent.ACTION_USER_REMOVED.equals(action)) {
                mHandler.sendMessage(mHandler.obtainMessage(MSG_USER_REMOVED,
                       intent.getIntExtra(Intent.EXTRA_USER_HANDLE, 0), 0));
            } else if (Intent.ACTION_BOOT_COMPLETED.equals(action)) {
                mHandler.sendMessage(mHandler.obtainMessage(MSG_BOOT_COMPLETED));
            }
            /// M: Gemini Enhancement begin @{
              else if ("android.intent.action.ACTION_SHUTDOWN_IPO".equals(action)) {
                 KeyguardUtils.xlogD(TAG, "received the IPO shutdown message");
                 mHandler.sendMessage(mHandler.obtainMessage(
                         MSG_KEYGUARD_RESET_DISMISS));
                     
                 // ALPS00264727: post a message
                 Message m = mHandler.obtainMessage(MSG_SYSTEM_STATE);
                 m.arg1 = SYSTEM_STATE_SHUTDOWN;
                 mHandler.sendMessage(m);
            } else if (TelephonyIntents.ACTION_RADIO_OFF.equals(action)){ 
                int slotId = intent.getIntExtra("slotId", 0);
                KeyguardUtils.xlogD(TAG, "received ACTION_RADIO_OFF message, slotId="+slotId);
                mHandler.sendMessage(mHandler.obtainMessage(
                        MSG_MODEM_RESET, slotId, 0));
            } else if (GeminiPhone.EVENT_3G_SWITCH_START_MD_RESET.equals(action)) {
                KeyguardUtils.xlogD(TAG, "received GeminiPhone.EVENT_3G_START_MD_RESET message");
                mSIMResetModem = true;
            } else if (GeminiPhone.EVENT_3G_SWITCH_DONE.equals(action)) {
                KeyguardUtils.xlogD(TAG, "received GeminiPhone.EVENT_3G_SWITCH_DONE message, mSIMResetModem="+mSIMResetModem);
                if (mSIMResetModem) {
                    mSIMResetModem = false;
                    mHandler.sendMessage(mHandler.obtainMessage(MSG_PRE_3G_SWITCH));
                }
            } else if (TelephonyIntents.ACTION_SIM_INSERTED_STATUS.equals(action)) {
                int slotId = intent.getIntExtra("slotId", 0); 
                KeyguardUtils.xlogD(TAG, "SIM_INSERTED_STATUS, slotId="+slotId);
                mHandler.sendMessage(mHandler.obtainMessage(
                        MSG_KEYGUARD_UPDATE_LAYOUT, slotId, 0));
            } else if ("android.intent.action.SIM_NAME_UPDATE".equals(action)) {
                int slotId = intent.getIntExtra("slotId", 0);
                KeyguardUtils.xlogD(TAG, "SIM_NAME_UPDATE, slotId="+slotId);
                mHandler.sendMessage(mHandler.obtainMessage(
                        MSG_KEYGUARD_SIM_NAME_UPDATE, slotId, 0));
            } else if (Intent.ACTION_CONFIGURATION_CHANGED.equals(action)) {
                mHandler.sendMessage(mHandler.obtainMessage(
                        MSG_CONFIGURATION_CHANGED));
            } else if (TelephonyIntents.ACTION_SIM_DETECTED.equals(action)) {
                String simDetectStatus = intent.getStringExtra("simDetectStatus");
                int simCount = intent.getIntExtra("simCount", 0);
                int newSIMSlot = intent.getIntExtra("newSIMSlot", 0);
                KeyguardUtils.xlogD(TAG,"detectStatus=" + simDetectStatus + ", simCount=" + simCount
                        + ", newSimSlot=" + newSIMSlot);
                mSimChangedStatus = new SIMStatus(simDetectStatus, simCount, newSIMSlot);
                if (mSimCardChangedDialog != null) {
                       mSimCardChangedDialog.dismiss();
                }
                ///M: show dialog by sequence manager.@{
                KeyguardUtils.xlogD(TAG, this + "Receive ACTION_SIM_DETECTED--requestShowDialog(..)");
				//zhangkai@Plf.DesktopApp.Keyguard, 2013/03/27,add for OppoNewSimDetect
                //requestShowDialog(new NewSimDialogCallback());
                //add end
            } else if ("android.intent.action.normal.boot".equals(action)) {
                Log.i(TAG, "received normal boot");
                /// M: Trigger showing dialog by DialogSequence Manager.@{
                if (null != mSimChangedStatus) {
                    mDialogSequenceManager.handleShowDialog();
                /// @}
                }
            } else if (TelephonyIntents.ACTION_SIM_INFO_UPDATE.equals(action)){
                int slotId = intent.getIntExtra("slotId", 0); 
                KeyguardUtils.xlogD(TAG, "sim info update, slotId="+slotId);
                mHandler.sendMessage(mHandler.obtainMessage(
                        MSG_SIMINFO_CHANGED, slotId, 0));
            } 
            /// Gemini end@}
            /// M: For CTA test @{
            else if (ACTION_DUAL_SIM_MODE_SELECT.equals(action)) {
                KeyguardUtils.xlogD(TAG, "ACTION_DUAL_SIM_MODE_SELECT, received");
                mHandler.sendMessage(mHandler.obtainMessage(
                        MSG_BOOTUP_MODE_PICK));
            } else if (Intent.ACTION_AIRPLANE_MODE_CHANGED.equals(action)) {
                KeyguardUtils.xlogD(TAG, "ACTION_AIRPLANE_MODE_CHANGED, received");
                if (mCtaDialog != null && intent.getExtras().getBoolean("state")) {
                    mCtaDialog.dismiss();
                    mCtaDialog = null;
                }
            }
            /// CTA test end @}
            /// M: For tablet to disable screen orientation when device is to shutdown @{
            else if (Intent.ACTION_SHUTDOWN.equals(action)) {
                 Message m = mHandler.obtainMessage(MSG_SYSTEM_STATE);
                 m.arg1 = SYSTEM_STATE_SHUTDOWN;
                 mHandler.sendMessage(m);
            } else if ("android.intent.action.ACTION_PREBOOT_IPO".equals(action)) {
                 // ALPS00264727: post a message
                 Message m = mHandler.obtainMessage(MSG_SYSTEM_STATE);
                 m.arg1 = SYSTEM_STATE_BOOTUP;
                 mHandler.sendMessage(m);
            }
            /// @}
            /// M: Check whether download calibration data or not @{
            else if(TelephonyIntents.ACTION_DOWNLOAD_CALIBRATION_DATA.equals(action)){
                mCalibrationData = intent.getBooleanExtra(EXTRA_CALIBRATION_DATA, true);
                KeyguardUtils.xlogD(TAG, "mCalibrationData = "+mCalibrationData);
                mHandler.sendMessage(mHandler.obtainMessage(MSG_DOWNLOAD_CALIBRATION_DATA_UPDATE));
            }
            /// @}
            /// M: Incoming Indicator for Rotation @{
            else if(CLEAR_NEW_EVENT_VIEW_INTENT.equals(action)) {
                mQueryBaseTime = java.lang.System.currentTimeMillis();
            }
            /// @}
        }
    };

    /**
     * When we receive a
     * {@link com.android.internal.telephony.TelephonyIntents#ACTION_SIM_STATE_CHANGED} broadcast,
     * and then pass a result via our handler to {@link KeyguardUpdateMonitor#handleSimStateChange},
     * we need a single object to pass to the handler.  This class helps decode
     * the intent and provide a {@link SimCard.State} result.
     * M: Add gemini support
     */
    private static class SimArgs {
        public final IccCardConstants.State simState;
        int simId = 0;

        SimArgs(IccCardConstants.State state) {
            simState = state;
        }

        SimArgs(IccCardConstants.State state, int id) {
           simState = state;
           simId = id;
        }

        static SimArgs fromIntent(Intent intent) {
            IccCardConstants.State state;
            int id = 0;
            if (!TelephonyIntents.ACTION_SIM_STATE_CHANGED.equals(intent.getAction())) {
                throw new IllegalArgumentException("only handles intent ACTION_SIM_STATE_CHANGED");
            }
            String stateExtra = intent.getStringExtra(IccCardConstants.INTENT_KEY_ICC_STATE);
            if (KeyguardUtils.isGemini()) {
                id = intent.getIntExtra(PhoneConstants.GEMINI_SIM_ID_KEY, PhoneConstants.GEMINI_SIM_1);
            }
            if (IccCardConstants.INTENT_VALUE_ICC_ABSENT.equals(stateExtra)) {
                final String absentReason = intent
                    .getStringExtra(IccCardConstants.INTENT_KEY_LOCKED_REASON);

                if (IccCardConstants.INTENT_VALUE_ABSENT_ON_PERM_DISABLED.equals(
                        absentReason)) {
                    state = IccCardConstants.State.PERM_DISABLED;
                } else {
                    state = IccCardConstants.State.ABSENT;
                }
            } else if (IccCardConstants.INTENT_VALUE_ICC_READY.equals(stateExtra)) {
                state = IccCardConstants.State.READY;
            } else if (IccCardConstants.INTENT_VALUE_ICC_LOCKED.equals(stateExtra)) {
                final String lockedReason = intent
                        .getStringExtra(IccCardConstants.INTENT_KEY_LOCKED_REASON);
                if (IccCardConstants.INTENT_VALUE_LOCKED_ON_PIN.equals(lockedReason)) {
                    state = IccCardConstants.State.PIN_REQUIRED;
                } else if (IccCardConstants.INTENT_VALUE_LOCKED_ON_PUK.equals(lockedReason)) {
                    state = IccCardConstants.State.PUK_REQUIRED;
                } else {
                    state = IccCardConstants.State.UNKNOWN;
                }
            } else if (IccCardConstants.INTENT_VALUE_LOCKED_NETWORK.equals(stateExtra)) {
                state = IccCardConstants.State.NETWORK_LOCKED;
            } else if (IccCardConstants.INTENT_VALUE_ICC_LOADED.equals(stateExtra)
                        || IccCardConstants.INTENT_VALUE_ICC_IMSI.equals(stateExtra)) {
                // This is required because telephony doesn't return to "READY" after
                // these state transitions. See bug 7197471.
                state = IccCardConstants.State.READY;
            } else if (IccCardConstants.INTENT_VALUE_ICC_NOT_READY.equals(stateExtra)) {
                state = IccCardConstants.State.NOT_READY;
            } else {
                state = IccCardConstants.State.UNKNOWN;
            }
            return new SimArgs(state, id);
        }

        public String toString() {
            return simState.toString();
        }
    }

    /* package */ static class BatteryStatus {
        public final int status;
        public final int level;
        public final int plugged;
        public final int health;
        public BatteryStatus(int status, int level, int plugged, int health) {
            this.status = status;
            this.level = level;
            this.plugged = plugged;
            this.health = health;
        }

        /**
         * Determine whether the device is plugged in (USB, power, or wireless).
         * @return true if the device is plugged in.
         */
        boolean isPluggedIn() {
            return plugged == BatteryManager.BATTERY_PLUGGED_AC
                    || plugged == BatteryManager.BATTERY_PLUGGED_USB
                    || plugged == BatteryManager.BATTERY_PLUGGED_WIRELESS;
        }

        /**
         * Whether or not the device is charged. Note that some devices never return 100% for
         * battery level, so this allows either battery level or status to determine if the
         * battery is charged.
         * @return true if the device is charged
         */
        public boolean isCharged() {
            return status == BATTERY_STATUS_FULL || level >= 100;
        }

        /**
         * Whether battery is low and needs to be charged.
         * @return true if battery is low
         */
        public boolean isBatteryLow() {
            return level < LOW_BATTERY_THRESHOLD;
        }

    }

    public static KeyguardUpdateMonitor getInstance(Context context) {
        if (sInstance == null) {
            sInstance = new KeyguardUpdateMonitor(context);
        }
        return sInstance;
    }

    private KeyguardUpdateMonitor(Context context) {
        mContext = context;

        /// M: Init dialog sequence manager
        mDialogSequenceManager = new DialogSequenceManager();

        /// M: Check dm lock when boot up
        dmCheckLocked();
        
        /// M: Init phone state listener, used to update sim state
        initPhoneStateListener();

        mDeviceProvisioned = Settings.Global.getInt(
                mContext.getContentResolver(), Settings.Global.DEVICE_PROVISIONED, 0) != 0;

        KeyguardUtils.xlogD(TAG, "mDeviceProvisioned is:" + mDeviceProvisioned);

        // Since device can't be un-provisioned, we only need to register a content observer
        // to update mDeviceProvisioned when we are...
        if (!mDeviceProvisioned) {
            watchForDeviceProvisioning();
        }

        // Take a guess at initial SIM state, battery status and PLMN until we get an update
        /// M: We think the sim card's default state is unknown, so mark this line @{
        //mSimState = IccCardConstants.State.NOT_READY;
        /// @}
        mBatteryStatus = new BatteryStatus(BATTERY_STATUS_UNKNOWN, 100, 0, 0);
        mTelephonyPlmn = getDefaultPlmn();

        /// M: We also update Gemini sim's plmn if needed
        if (KeyguardUtils.isGemini()) {
            mTelephonyPlmnGemini = getDefaultPlmn();
        }

        // Watch for interesting updates
        final IntentFilter filter = new IntentFilter();
        filter.addAction(Intent.ACTION_TIME_TICK);
        filter.addAction(Intent.ACTION_TIME_CHANGED);
        filter.addAction(Intent.ACTION_BATTERY_CHANGED);
        filter.addAction(Intent.ACTION_TIMEZONE_CHANGED);
        filter.addAction(TelephonyIntents.ACTION_SIM_STATE_CHANGED);
        filter.addAction(TelephonyManager.ACTION_PHONE_STATE_CHANGED);
        filter.addAction(TelephonyIntents.SPN_STRINGS_UPDATED_ACTION);
        filter.addAction(AudioManager.RINGER_MODE_CHANGED_ACTION);
        filter.addAction(DevicePolicyManager.ACTION_DEVICE_POLICY_MANAGER_STATE_CHANGED);
        filter.addAction(Intent.ACTION_USER_REMOVED);

        /// M: Gemini Enhancement begin @{
        filter.addAction(TelephonyIntents.ACTION_SIM_DETECTED);
        filter.addAction(TelephonyIntents.ACTION_SIM_INFO_UPDATE);
        filter.addAction(Intent.ACTION_CONFIGURATION_CHANGED);
        filter.addAction("android.intent.action.ACTION_SHUTDOWN_IPO");
        filter.addAction(TelephonyIntents.ACTION_SIM_INSERTED_STATUS);
        filter.addAction("android.intent.action.SIM_NAME_UPDATE");
        filter.addAction(TelephonyIntents.ACTION_RADIO_OFF);
        filter.addAction(GeminiPhone.EVENT_3G_SWITCH_DONE);
        /// @}

        /// M: Added for CTA test to update sim mode
        filter.addAction(ACTION_DUAL_SIM_MODE_SELECT);
        /// M: Added for CTA test to dismiss CTA dialog if user switched to airplane mode
        filter.addAction(Intent.ACTION_AIRPLANE_MODE_CHANGED);
        // M: Add for tablet to disable screen orientation when shutdown @{
        filter.addAction(Intent.ACTION_SHUTDOWN);
        filter.addAction("android.intent.action.ACTION_PREBOOT_IPO");
        /// @}

        /// M: Add for calibration data check
        filter.addAction(TelephonyIntents.ACTION_DOWNLOAD_CALIBRATION_DATA);

        context.registerReceiver(mBroadcastReceiver, filter);

        final IntentFilter bootCompleteFilter = new IntentFilter();
        bootCompleteFilter.setPriority(IntentFilter.SYSTEM_HIGH_PRIORITY);
        bootCompleteFilter.addAction(Intent.ACTION_BOOT_COMPLETED);
        context.registerReceiver(mBroadcastReceiver, bootCompleteFilter);
        /// M: Incoming Indicator for Rotation @{
        filter.addAction(CLEAR_NEW_EVENT_VIEW_INTENT);
        context.registerReceiver(mBroadcastReceiver, filter);
        /// @}
        try {
            ActivityManagerNative.getDefault().registerUserSwitchObserver(
                    new IUserSwitchObserver.Stub() {
                        @Override
                        public void onUserSwitching(int newUserId, IRemoteCallback reply) {
                            mHandler.sendMessage(mHandler.obtainMessage(MSG_USER_SWITCHED,
                                    newUserId, 0, reply));
                        }
                        @Override
                        public void onUserSwitchComplete(int newUserId) throws RemoteException {
                        }
                    });
        } catch (RemoteException e) {
            // TODO Auto-generated catch block
            e.printStackTrace();
        }
    }

    private void watchForDeviceProvisioning() {
        mContentObserver = new ContentObserver(mHandler) {
            @Override
            public void onChange(boolean selfChange) {
                super.onChange(selfChange);
                mDeviceProvisioned = Settings.Global.getInt(mContext.getContentResolver(),
                    Settings.Global.DEVICE_PROVISIONED, 0) != 0;
                if (mDeviceProvisioned) {
                    mHandler.sendMessage(mHandler.obtainMessage(MSG_DEVICE_PROVISIONED));
                }
                if (DEBUG) Log.d(TAG, "DEVICE_PROVISIONED state = " + mDeviceProvisioned);
            }
        };

        mContext.getContentResolver().registerContentObserver(
                Settings.Global.getUriFor(Settings.Global.DEVICE_PROVISIONED),
                false, mContentObserver);

        // prevent a race condition between where we check the flag and where we register the
        // observer by grabbing the value once again...
        boolean provisioned = Settings.Global.getInt(mContext.getContentResolver(),
            Settings.Global.DEVICE_PROVISIONED, 0) != 0;
        if (provisioned != mDeviceProvisioned) {
            mDeviceProvisioned = provisioned;
            if (mDeviceProvisioned) {
                mHandler.sendMessage(mHandler.obtainMessage(MSG_DEVICE_PROVISIONED));
            }
        }
    }

    /**
     * Handle {@link #MSG_DPM_STATE_CHANGED}
     */
    protected void handleDevicePolicyManagerStateChanged() {
        for (int i = mCallbacks.size() - 1; i >= 0; i--) {
            KeyguardUpdateMonitorCallback cb = mCallbacks.get(i).get();
            if (cb != null) {
                cb.onDevicePolicyManagerStateChanged();
            }
        }
    }

    /**
     * Handle {@link #MSG_USER_SWITCHED}
     */
    protected void handleUserSwitched(int userId, IRemoteCallback reply) {
        for (int i = 0; i < mCallbacks.size(); i++) {
            KeyguardUpdateMonitorCallback cb = mCallbacks.get(i).get();
            if (cb != null) {
                cb.onUserSwitched(userId);
            }
        }
        setAlternateUnlockEnabled(false);
        try {
            reply.sendResult(null);
        } catch (RemoteException e) {
        }
    }

    /**
     * Handle {@link #MSG_BOOT_COMPLETED}
     */
    protected void handleBootCompleted() {
        mBootCompleted = true;
        for (int i = 0; i < mCallbacks.size(); i++) {
            KeyguardUpdateMonitorCallback cb = mCallbacks.get(i).get();
            if (cb != null) {
                cb.onBootCompleted();
            }
        }
    }

    /**
     * We need to store this state in the KeyguardUpdateMonitor since this class will not be 
     * destroyed.
     */
    public boolean hasBootCompleted() {
        return mBootCompleted;
    }

    /**
     * Handle {@link #MSG_USER_SWITCHED}
     */
    protected void handleUserRemoved(int userId) {
        for (int i = 0; i < mCallbacks.size(); i++) {
            KeyguardUpdateMonitorCallback cb = mCallbacks.get(i).get();
            if (cb != null) {
                cb.onUserRemoved(userId);
            }
        }
    }

    /**
     * Handle {@link #MSG_DEVICE_PROVISIONED}
     */
    protected void handleDeviceProvisioned() {
        for (int i = 0; i < mCallbacks.size(); i++) {
            KeyguardUpdateMonitorCallback cb = mCallbacks.get(i).get();
            if (cb != null) {
                cb.onDeviceProvisioned();
            }
        }

        /// M: when GMS finished , trigger to show the dialog
        mDialogSequenceManager.handleShowDialog();

        if (mContentObserver != null) {
            // We don't need the observer anymore...
            mContext.getContentResolver().unregisterContentObserver(mContentObserver);
            mContentObserver = null;
        }
    }

    /**
     * Handle {@link #MSG_PHONE_STATE_CHANGED}
     */
    protected void handlePhoneStateChanged(String newState) {
        if (DEBUG) Log.d(TAG, "handlePhoneStateChanged(" + newState + ")");
        /// M: If phone state change, dismiss sim detecd dialog
        if (mSimCardChangedDialog != null) {
            mSimCardChangedDialog.dismiss();
        }
        if (TelephonyManager.EXTRA_STATE_IDLE.equals(newState)) {
            mPhoneState = TelephonyManager.CALL_STATE_IDLE;
        } else if (TelephonyManager.EXTRA_STATE_OFFHOOK.equals(newState)) {
            mPhoneState = TelephonyManager.CALL_STATE_OFFHOOK;
        } else if (TelephonyManager.EXTRA_STATE_RINGING.equals(newState)) {
            mPhoneState = TelephonyManager.CALL_STATE_RINGING;
        }
        for (int i = 0; i < mCallbacks.size(); i++) {
            KeyguardUpdateMonitorCallback cb = mCallbacks.get(i).get();
            if (cb != null) {
                cb.onPhoneStateChanged(mPhoneState);
            }
        }
    }

    /**
     * Handle {@link #MSG_RINGER_MODE_CHANGED}
     */
    protected void handleRingerModeChange(int mode) {
        if (DEBUG) Log.d(TAG, "handleRingerModeChange(" + mode + ")");
        mRingMode = mode;
        for (int i = 0; i < mCallbacks.size(); i++) {
            KeyguardUpdateMonitorCallback cb = mCallbacks.get(i).get();
            if (cb != null) {
                cb.onRingerModeChanged(mode);
            }
        }
    }

    /**
     * Handle {@link #MSG_TIME_UPDATE}
     */
    private void handleTimeUpdate() {
        if (DEBUG) Log.d(TAG, "handleTimeUpdate");
        for (int i = 0; i < mCallbacks.size(); i++) {
            KeyguardUpdateMonitorCallback cb = mCallbacks.get(i).get();
            if (cb != null) {
                cb.onTimeChanged();
            }
        }
    }

    /**
     * Handle {@link #MSG_BATTERY_UPDATE}
     */
    private void handleBatteryUpdate(BatteryStatus status) {
        if (DEBUG) Log.d(TAG, "handleBatteryUpdate");
        final boolean batteryUpdateInteresting = isBatteryUpdateInteresting(mBatteryStatus, status);
        mBatteryStatus = status;
        if (batteryUpdateInteresting) {
            for (int i = 0; i < mCallbacks.size(); i++) {
                KeyguardUpdateMonitorCallback cb = mCallbacks.get(i).get();
                if (cb != null) {
                    cb.onRefreshBatteryInfo(status);
                }
            }
        }
    }

    /**
     * Handle {@link #MSG_CARRIER_INFO_UPDATE}
     */
    private void handleCarrierInfoUpdate(SpnUpdate spnUpdate) {
        if (DEBUG) Log.d(TAG, "handleCarrierInfoUpdate: plmn = " + mTelephonyPlmn
            + ", spn = " + mTelephonySpn);

        for (int i = 0; i < mCallbacks.size(); i++) {
            KeyguardUpdateMonitorCallback cb = mCallbacks.get(i).get();
            if (cb != null) {
                if (KeyguardUtils.isGemini()) {
                    if (PhoneConstants.GEMINI_SIM_1 == spnUpdate.simId) {
                        cb.onRefreshCarrierInfoGemini(mTelephonyPlmn, mTelephonySpn, spnUpdate.simId);
                    } else {
                        cb.onRefreshCarrierInfoGemini(mTelephonyPlmnGemini, mTelephonySpnGemini, spnUpdate.simId);
                    }
                } else {
                    cb.onRefreshCarrierInfo(mTelephonyPlmn, mTelephonySpn);
                }
            }
        }
    }

    /**
     * Handle {@link #MSG_SIM_STATE_CHANGE}
     */
    private void handleSimStateChange(SimArgs simArgs) {
        final IccCardConstants.State state = simArgs.simState;

        if (DEBUG) {
            Log.d(TAG, "handleSimStateChange: intentValue = " + simArgs + " "
                    + "state resolved to " + state.toString()+", oldssimtate="+mSimState+", oldsim2state="+mSim2State);
        }

        IccCardConstants.State tempState;
        if (simArgs.simId == PhoneConstants.GEMINI_SIM_1) {
            tempState = mSimState;
            mSimLastState = mSimState;
        } else {
            tempState = mSim2State;
            mSim2LastState = mSim2State;
        }

        if (state != IccCardConstants.State.UNKNOWN && state != tempState) {
            if (DEBUG_SIM_STATES) Log.v(TAG, "dispatching state: " + state + " to sim " + simArgs.simId);
            if (simArgs.simId == PhoneConstants.GEMINI_SIM_1) {
                mSimState = state;
                KeyguardUtils.xlogD(TAG, "handleSimStateChange: mSimState = " + mSimState);
            } else {
                mSim2State = state;
                KeyguardUtils.xlogD(TAG, "handleSimStateChange: mSim2State = " + mSim2State);
            }
            if (KeyguardUtils.isGemini()) {
                for (int i = 0; i < mCallbacks.size(); i++) {
                    KeyguardUpdateMonitorCallback cb = mCallbacks.get(i).get();
                    if (cb != null) {
                        cb.onSimStateChangedGemini(state, simArgs.simId);
                    }
                }
            } else {
                for (int i = 0; i < mCallbacks.size(); i++) {
                    KeyguardUpdateMonitorCallback cb = mCallbacks.get(i).get();
                    if (cb != null) {
                        cb.onSimStateChanged(state);
                    }
                }
            }
        }
    }

    /**
     * Handle {@link #MSG_CLOCK_VISIBILITY_CHANGED}
     */
    private void handleClockVisibilityChanged() {
        if (DEBUG) Log.d(TAG, "handleClockVisibilityChanged()");
        for (int i = 0; i < mCallbacks.size(); i++) {
            KeyguardUpdateMonitorCallback cb = mCallbacks.get(i).get();
            if (cb != null) {
                cb.onClockVisibilityChanged();
            }
        }
    }

    /**
     * Handle {@link #MSG_KEYGUARD_VISIBILITY_CHANGED}
     */
    private void handleKeyguardVisibilityChanged(int showing) {
        if (DEBUG) Log.d(TAG, "handleKeyguardVisibilityChanged(" + showing + ")");
        boolean isShowing = (showing == 1);
        mKeyguardIsVisible = isShowing;
        for (int i = 0; i < mCallbacks.size(); i++) {
            KeyguardUpdateMonitorCallback cb = mCallbacks.get(i).get();
            if (cb != null) {
                cb.onKeyguardVisibilityChanged(isShowing);
            }
        }
    }

    public boolean isKeyguardVisible() {
        return mKeyguardIsVisible;
    }

    private static boolean isBatteryUpdateInteresting(BatteryStatus old, BatteryStatus current) {
        final boolean nowPluggedIn = current.isPluggedIn();
        final boolean wasPluggedIn = old.isPluggedIn();
        final boolean stateChangedWhilePluggedIn =
            wasPluggedIn == true && nowPluggedIn == true
            && (old.status != current.status);

        // change in plug state is always interesting
        if (wasPluggedIn != nowPluggedIn || stateChangedWhilePluggedIn) {
            return true;
        }

        // change in battery level while plugged in
        /// M: We remove "nowPluggedIn" condition here.
        /// To fix the issue that if HW give up a low battery level(below threshold)
        /// and then a high battery level(above threshold) while device is not pluggin,
        /// then Keyguard may never be able be show
        /// charging text on screen when pluggin
        if (old.level != current.level) {
            return true;
        }

        // change where battery needs charging
        if (!nowPluggedIn && current.isBatteryLow() && current.level != old.level) {
            return true;
        }
        return false;
    }

    /**
     * @param intent The intent with action {@link TelephonyIntents#SPN_STRINGS_UPDATED_ACTION}
     * @return The string to use for the plmn, or null if it should not be shown.
     */
    private CharSequence getTelephonyPlmnFrom(Intent intent) {
        if (intent.getBooleanExtra(TelephonyIntents.EXTRA_SHOW_PLMN, false)) {
            KeyguardUtils.xlogD(TAG,"EXTRA_SHOW_PLMN =  TRUE ");
            final String plmn = intent.getStringExtra(TelephonyIntents.EXTRA_PLMN);
            return (plmn != null) ? plmn : getDefaultPlmn();
        } else {
            KeyguardUtils.xlogD(TAG,"EXTRA_SHOW_PLMN = FALSE  ");
            return null;
        }
    }

    /**
     * @return The default plmn (no service)
     */
    private CharSequence getDefaultPlmn() {
        return mContext.getResources().getText(R.string.lockscreen_carrier_default);
    }

    /**
     * @param intent The intent with action {@link Telephony.Intents#SPN_STRINGS_UPDATED_ACTION}
     * @return The string to use for the plmn, or null if it should not be shown.
     */
    private CharSequence getTelephonySpnFrom(Intent intent) {
        if (intent.getBooleanExtra(TelephonyIntents.EXTRA_SHOW_SPN, false)) {
            final String spn = intent.getStringExtra(TelephonyIntents.EXTRA_SPN);
            if (spn != null) {
                return spn;
            }
        }
        return null;
    }

    /**
     * Remove the given observer's callback.
     *
     * @param callback The callback to remove
     */
    public void removeCallback(KeyguardUpdateMonitorCallback callback) {
        if (DEBUG) Log.v(TAG, "*** unregister callback for " + callback);
        for (int i = mCallbacks.size() - 1; i >= 0; i--) {
            if (mCallbacks.get(i).get() == callback) {
                mCallbacks.remove(i);
            }
        }
    }

    /**
     * Register to receive notifications about general keyguard information
     * (see {@link KeyguardUpdateMonitorCallback}.
     * @param callback The callback to register
     */
    public void registerCallback(KeyguardUpdateMonitorCallback callback) {
        if (DEBUG) Log.v(TAG, "*** register callback for " + callback);
        // Prevent adding duplicate callbacks
        for (int i = 0; i < mCallbacks.size(); i++) {
            if (mCallbacks.get(i).get() == callback) {
                if (DEBUG) Log.e(TAG, "Object tried to add another callback",
                        new Exception("Called by"));
                return;
            }
        }
        mCallbacks.add(new WeakReference<KeyguardUpdateMonitorCallback>(callback));
        removeCallback(null); // remove unused references
        sendUpdates(callback);
    }

    private void sendUpdates(KeyguardUpdateMonitorCallback callback) {
        // Notify listener of the current state
        callback.onRefreshBatteryInfo(mBatteryStatus);
        callback.onTimeChanged();
        callback.onRingerModeChanged(mRingMode);
        callback.onPhoneStateChanged(mPhoneState);
        /// M: Modify to refresh gemini carrier
        if (KeyguardUtils.isGemini()) {
            callback.onRefreshCarrierInfoGemini(mTelephonyPlmn, mTelephonySpn, PhoneConstants.GEMINI_SIM_1);
            callback.onRefreshCarrierInfoGemini(mTelephonyPlmnGemini, mTelephonySpnGemini, PhoneConstants.GEMINI_SIM_2);
        } else {
            callback.onRefreshCarrierInfo(mTelephonyPlmn, mTelephonySpn);
        }
        callback.onClockVisibilityChanged();
        /// M: Modify phone state change callback to support gemini
        //callback.onSimStateChangedGemini(mSimState);
        if (KeyguardUtils.isGemini()) {
            callback.onSimStateChangedGemini(mSimState, PhoneConstants.GEMINI_SIM_1);
            callback.onSimStateChangedGemini(mSim2State, PhoneConstants.GEMINI_SIM_2);
        } else {
            callback.onSimStateChanged(mSimState);
        }

        /// M: Mediatek added callback
        callback.onDownloadCalibrationDataUpdate(mCalibrationData);
        callback.onSearchNetworkUpdate(PhoneConstants.GEMINI_SIM_1, mNetSearching);
        if (KeyguardUtils.isGemini()) {
            callback.onSearchNetworkUpdate(PhoneConstants.GEMINI_SIM_2, mNetSearchingGemini);
        }
    }

    public void sendKeyguardVisibilityChanged(boolean showing) {
        if (DEBUG) Log.d(TAG, "sendKeyguardVisibilityChanged(" + showing + ")");
        Message message = mHandler.obtainMessage(MSG_KEYGUARD_VISIBILITY_CHANGED);
        message.arg1 = showing ? 1 : 0;
        message.sendToTarget();
    }

    public void reportClockVisible(boolean visible) {
        mClockVisible = visible;
        mHandler.obtainMessage(MSG_CLOCK_VISIBILITY_CHANGED).sendToTarget();
    }

    public IccCardConstants.State getSimState() {
        return mSimState;
    }

    /**
     * Report that the user successfully entered the SIM PIN or PUK/SIM PIN so we
     * have the information earlier than waiting for the intent
     * broadcast from the telephony code.
     *
     * NOTE: Because handleSimStateChange() invokes callbacks immediately without going
     * through mHandler, this *must* be called from the UI thread.
     *
     * M: Mergrate from Android4.0.4, but we don't need it
     *    handleSimStateChange(new SimArgs(mSimState));
     */
    public void reportSimUnlocked() {
        if (mSimState != IccCardConstants.State.NETWORK_LOCKED) {
            mSimState = IccCardConstants.State.READY;
            //mergrate from Android4.0.4, here, we need it. 
            handleSimStateChange(new SimArgs(IccCardConstants.State.READY));
        }
    }

    public CharSequence getTelephonyPlmn() {
        return mTelephonyPlmn;
    }

    public CharSequence getTelephonySpn() {
        return mTelephonySpn;
    }

    /**
     * @return Whether the device is provisioned (whether they have gone through
     *   the setup wizard)
     */
    public boolean isDeviceProvisioned() {
        return mDeviceProvisioned;
    }

    public int getFailedUnlockAttempts() {
        return mFailedAttempts;
    }

    public void clearFailedUnlockAttempts() {
        mFailedAttempts = 0;
        mFailedBiometricUnlockAttempts = 0;
    }

    public void reportFailedUnlockAttempt() {
        mFailedAttempts++;
    }

    public boolean isClockVisible() {
        return mClockVisible;
    }

    public int getPhoneState() {
        return mPhoneState;
    }

    public void reportFailedBiometricUnlockAttempt() {
        mFailedBiometricUnlockAttempts++;
    }

    public boolean getMaxBiometricUnlockAttemptsReached() {
        return mFailedBiometricUnlockAttempts >= FAILED_BIOMETRIC_UNLOCK_ATTEMPTS_BEFORE_BACKUP;
    }

    public boolean isAlternateUnlockEnabled() {
        return mAlternateUnlockEnabled;
    }

    public void setAlternateUnlockEnabled(boolean enabled) {
        mAlternateUnlockEnabled = enabled;
    }

    public boolean isSimLocked() {
        if (KeyguardUtils.isGemini()) {
            return isSimLockedGemini(PhoneConstants.GEMINI_SIM_1) || isSimLockedGemini(PhoneConstants.GEMINI_SIM_2);
        } else {
            return isSimLocked(mSimState);
        }
    }

    public static boolean isSimLocked(IccCardConstants.State state) {
        return state == IccCardConstants.State.PIN_REQUIRED
        || state == IccCardConstants.State.PUK_REQUIRED
        || state == IccCardConstants.State.PERM_DISABLED;
    }

    /// M: Add gemini support
    public boolean isSimPinSecure() {
        if (KeyguardUtils.isGemini()) {
            return isSimPinSecureGemini(mSimState, PhoneConstants.GEMINI_SIM_1) || isSimPinSecureGemini(mSim2State, PhoneConstants.GEMINI_SIM_2);
        } else {
            return isSimPinSecure(mSimState);
        }
    }

    /// M: Also check dismiss flag
    private boolean isSimPinSecure(IccCardConstants.State state) {
        final IccCardConstants.State simState = state;
        return ((simState == IccCardConstants.State.PIN_REQUIRED && !getPINDismissFlag(PhoneConstants.GEMINI_SIM_1, true))
                || (simState == IccCardConstants.State.PUK_REQUIRED && !getPINDismissFlag(PhoneConstants.GEMINI_SIM_1, false))
                || simState == IccCardConstants.State.PERM_DISABLED);
    }
    
    /// M: Add for gemini sim pin secure
    private boolean isSimPinSecureGemini(IccCardConstants.State state, int simId) {
        final IccCardConstants.State simState = state;
        if (PhoneConstants.GEMINI_SIM_1 == simId) {
            return ((simState == IccCardConstants.State.PIN_REQUIRED && !getPINDismissFlag(PhoneConstants.GEMINI_SIM_1, true))
                    || (simState == IccCardConstants.State.PUK_REQUIRED && !getPINDismissFlag(PhoneConstants.GEMINI_SIM_1, false))
                    || simState == IccCardConstants.State.PERM_DISABLED);
        } else {
            return ((simState == IccCardConstants.State.PIN_REQUIRED && !getPINDismissFlag(PhoneConstants.GEMINI_SIM_2, true))
                    || (simState == IccCardConstants.State.PUK_REQUIRED && !getPINDismissFlag(PhoneConstants.GEMINI_SIM_2, false))
                    || simState == IccCardConstants.State.PERM_DISABLED);
        }
    }

    /********************************************************
     ** Mediatek add begin
     ********************************************************/

    /// M: DM lock flag to indicate weather the device is DM locked
    private boolean mKeyguardDMLocked = false;
    
    /// M: For Gemini use, the second sim State
    private IccCardConstants.State mSim2State = IccCardConstants.State.UNKNOWN;
    
    /// M: Save the last sim state, which will be used in KeyguardViewMediator to reset keyguard @{
    private IccCardConstants.State mSimLastState = IccCardConstants.State.UNKNOWN;
    private IccCardConstants.State mSim2LastState = IccCardConstants.State.UNKNOWN;
    /// @}
    
    ///M: The default value of the remaining puk count
    private static final int GET_SIM_RETRY_EMPTY = -1;

    /// M: Gemini SIM_2 phone's plmn and spn @{
    private CharSequence mTelephonyPlmnGemini;
    private CharSequence mTelephonySpnGemini;
    /// @}

    /// M: PhoneStateListenr used to told client to update NetWorkSearching state @{
    private PhoneStateListener mPhoneStateListener;
    private PhoneStateListener mPhoneStateListenerGemini;
    /// @}

    private AlertDialog mCtaDialog = null;
    private AlertDialog mSimCardChangedDialog = null;
    private View mPromptView = null;
    private SIMStatus mSimChangedStatus;
    private static String SIM_DETECT_NEW = "NEW";
    private static String SIM_DETECT_REMOVE = "REMOVE";
    private static String SIM_DETECT_SWAP = "SWAP";
    
    /** M: This flag is used for GlobalActions to check if user has clicked the 
     * dual_sim_mode_setting dialog:
     * -1: dialog has not been created;
     * 0: dialog has been created but has not been clicked;
     * 1: dialog has been created and has been clicked;
     */
    public static int sDualSimSetting = -1; 
    
    /// M: System shutdonw/bootup callback for KeyguardViewManager to update Keyguard window's orientation flag
    private SystemStateCallback mSystemStateCallback;
    
    /// M: Used in System state change callback, to indicate weather this change is bootup or shutdown
    private static final int SYSTEM_STATE_SHUTDOWN = 0;
    private static final int SYSTEM_STATE_BOOTUP = 1;
    /// @}
    
    /// M: Used to notify SimUnlockScreen to dismiss itself when the SIM is Radio off.
    private RadioStateCallback mRadioStateCallback;
    
    /// M: Following variables are used to fix the problem that SIM detectd dialog's locale cannot udpate
    /// when new sim inserted in GMS load, because GMS load will not update locale automatically, so we add
    /// a delay check mechanism to show new SIM inserted dialog after GMS setup is done @{
    private static final String GMS_SETUP_PACKAGE = "com.google.android.setupwizard";
    private static final String GMS_SETUP_COMPONENT = "com.google.android.setupwizard.SetupWizardActivity";
    private PackageManager mPm;
    private ComponentName mComponentName;
    /// @}

    /// M: For Gemini enhancement feature to update sim card when new sim is detected
    private static final int MSG_SIM_DETECTED = 1002;
    
    /// M: For Gemini enhancement feature to update siminfo when we received broadcast from Telephony framework
    private static final int MSG_SIMINFO_CHANGED = 1004;
    
    /// M: For Gemini enhancement feature, when locale changed due to new inserted sim card, 
    /// update all related text in this message handler
    private static final int MSG_CONFIGURATION_CHANGED = 1005;
    
    /// M: For Gemini enhancement feature, after boot up and we saved SimInfo names, Telephony framework may still
    /// update SimInfo names, so we need to handle it in this message hander 
    private static final int MSG_KEYGUARD_SIM_NAME_UPDATE = 1006;
    
    /// M: When we received IPO shutdown broadcast, reset all sim card's pin and puk flag in this message handler 
    private static final int MSG_KEYGUARD_RESET_DISMISS = 1007;
    
    /// M: Workaround for IPO @{
    private static final int MSG_KEYGUARD_UPDATE_LAYOUT = 1008;
    private static final int MSG_MODEM_RESET = 1009;
    private static final int MSG_PRE_3G_SWITCH = 1010;
    /// @}

    /// M: For CTA test, pop up a dialog to let user switch dual sim mode
    private static final int MSG_BOOTUP_MODE_PICK = 1011;
    
    /// M: When shutdown/ipo shutdown broadcast comes, we disable current orientation flag 
    /// and restore it when boot up
    private static final int MSG_SYSTEM_STATE = 1012;

    /// M: check whether download calibration data or not in this message handler
    private static final int MSG_DOWNLOAD_CALIBRATION_DATA_UPDATE = 1013;
        
    /// M: Used in PhoneStateChange listener to notify client to update, these two flag indicate weather
    /// sim card is searching network @{
    boolean mNetSearching = false;
    boolean mNetSearchingGemini = false;
    /// @}
    
    private static final int PIN_PUK_RESET = 0x0000;
    private static final int SIM_1_PIN_PUK_MASK = 0x0001 | 0x0001 << 2;
    private static final int SIM_1_PIN_DISMISSED = 0x0001;
    private static final int SIM_1_PUK_DISMISSED = 0x0001 << 2;
    private static final int SIM_2_PIN_PUK_MASK = 0x0001 << 1 | 0x0001 << 3;
    private static final int SIM_2_PIN_DISMISSED = 0x0001 << 1;
    private static final int SIM_2_PUK_DISMISSED = 0x0001 << 3;

    /// M: Flag used to indicate weather sim1 or sim2 card's pin/puk is dismissed by user.
    private int mPinPukDismissFlag = PIN_PUK_RESET;
    

    /// M: Flag indicates whether calibration data has downloaded
    private boolean mCalibrationData = true;

    /**
     * M: Callback to notify the modem state change.
     */
    interface RadioStateCallback {
        void onRadioStateChanged(int slotId);
    }

    /**
     * M: Callback to notify the system state change.
     */
    interface SystemStateCallback {
        void onSysShutdown();
        void onSysBootup();
    }

    /**
     * M: Either a lock screen (an informational keyguard screen), or an unlock
     * screen (a means for unlocking the device) is shown at any given time.
     */
    private class SIMStatus {
        private int mSimCount = 0;
        private String mSimDetectStatus = SIM_DETECT_NEW;
        private int mNewSimSlot = 0;

        public SIMStatus(final String simDetectStatus, final int simCount, final int newSimSlot) {
            mSimDetectStatus = simDetectStatus;
            mSimCount = simCount;
            mNewSimSlot = newSimSlot;
        }

        public String getSimDetectStatus() {
            return mSimDetectStatus;
        }

        public int getSIMCount() {
            return mSimCount;
        }
        
        public int getNewSimSlot() {
            return mNewSimSlot;
        }
    }

    public boolean isSimLockedGemini(int simId) {
        if (simId == PhoneConstants.GEMINI_SIM_2) {
            return mSim2State == IccCardConstants.State.PIN_REQUIRED
                       || mSim2State == IccCardConstants.State.PUK_REQUIRED
                       || mSim2State == IccCardConstants.State.PERM_DISABLED;
        } else {
            return mSimState == IccCardConstants.State.PIN_REQUIRED
                       || mSimState == IccCardConstants.State.PUK_REQUIRED
                       || mSimState == IccCardConstants.State.PERM_DISABLED;
        }
    }

    public boolean isPhoneAppReady() {
        final ActivityManager am = (ActivityManager) mContext.getSystemService(Context.ACTIVITY_SERVICE);
        
        boolean ready = false;
        List<RunningAppProcessInfo> runningAppInfo = am.getRunningAppProcesses();   
        if (runningAppInfo == null) {
            Log.i(TAG, "runningAppInfo == null");
            return ready;
        }        
        for (RunningAppProcessInfo app : runningAppInfo) {
            if (app.processName.equals("com.android.phone")) {
                ready = true;
                break;
            }
        }
        return ready;
    }

    /// M: DM begin @{
    public boolean dmIsLocked() {
        return mKeyguardDMLocked;
    }
    
    public void setDmLocked(boolean locked) {
        mKeyguardDMLocked = locked;
    }
    
    private void dmCheckLocked() {
        try {
            //for OMA DM
            IBinder binder = ServiceManager.getService("DMAgent");
            if (binder != null) {
                DMAgent agent = DMAgent.Stub.asInterface(binder);
                boolean flag = agent.isLockFlagSet();
                Log.i(TAG,"dmCheckLocked, the lock flag is:" + flag);
                setDmLocked(flag);
            } else {
                Log.i(TAG,"dmCheckLocked, DMAgent doesn't exit");
            }
        } catch (Exception e) {
            Log.e(TAG,"get DM status failed!");
        }   
    }
    /// DM end @}

   public String getOptrNameById(long simId) {
        if (simId > 0) {
            KeyguardUtils.xlogD(TAG, "getOptrNameById, xxsimId=" + simId);
            SIMInfo info = SIMInfo.getSIMInfoById(mContext, (int)simId);
            if (null == info) {
                KeyguardUtils.xlogD(TAG, "getOptrNameBySlotId, return null");
               return null;
            } else {
                KeyguardUtils.xlogD(TAG, "info=" + info.mDisplayName);
               return info.mDisplayName; 
            }
        } else if (-1 == simId) {
            return mContext.getResources().getString(com.mediatek.internal.R.string.keyguard_alwaysask);
        } else if (-2 == simId) {
            return mContext.getResources().getString(com.mediatek.internal.R.string.keyguard_internal_call);
        } else if (0 == simId) {
            return mContext.getResources().getString(com.mediatek.internal.R.string.keyguard_data_none);
        } else {
            return mContext.getResources().getString(com.mediatek.internal.R.string.keyguard_not_set);
        }
    }
    
    public Drawable getOptrDrawableById(long simId) {
        if (simId > 0) {
            KeyguardUtils.xlogD(TAG, "getOptrDrawableById, xxsimId=" + simId);
            SIMInfo info = SIMInfo.getSIMInfoById(mContext, (int)simId); 
            if (null == info) {
                KeyguardUtils.xlogD(TAG, "getOptrDrawableBySlotId, return null");
               return null;
            } else {
               return mContext.getResources().getDrawable(info.mSimBackgroundDarkRes);
            }
        } else {
            return null;
        }
    }
   public String getOptrNameBySlot(long slot) {
        if (slot >= 0) {
            KeyguardUtils.xlogD(TAG, "getOptrNameBySlot, xxSlot=" + slot);
            SIMInfo info = SIMInfo.getSIMInfoBySlot(mContext, (int)slot);
            if (null == info) {
                KeyguardUtils.xlogD(TAG, "getOptrNameBySlotId, return null");
               return null;
            } else {
                KeyguardUtils.xlogD(TAG, "info=" + info.mDisplayName);
               return info.mDisplayName; 
            }
        } else {
            throw new IndexOutOfBoundsException();
        }
    }
    
   public String getOptrNameBySlotForCTA(long slot) {
       if (slot >= 0) {
           KeyguardUtils.xlogD(TAG, "getOptrNameBySlot, xxSlot=" + slot);
           SIMInfo info = SIMInfo.getSIMInfoBySlot(mContext, (int)slot);
           if (null == info || info.mDisplayName == null) {
               KeyguardUtils.xlogD(TAG, "getOptrNameBySlotId, return null");
              if (PhoneConstants.GEMINI_SIM_2 == slot) {
                  return mContext.getResources().getString(com.mediatek.internal.R.string.new_sim) + " 02";
              } else {
                  return mContext.getResources().getString(com.mediatek.internal.R.string.new_sim) + " 01";
              }
           } else {
               KeyguardUtils.xlogD(TAG, "info=" + info.mDisplayName);
              return info.mDisplayName; 
           }
       } else {
           return mContext.getResources().getString(com.mediatek.internal.R.string.keyguard_not_set);
       }
   }
    
    public Drawable getOptrDrawableBySlot(long slot) {
        if (slot >= 0) {
            KeyguardUtils.xlogD(TAG, "getOptrDrawableBySlot, xxslot=" + slot);
            SIMInfo info = SIMInfo.getSIMInfoBySlot(mContext, (int)slot); 
            if (null == info) {
                KeyguardUtils.xlogD(TAG, "getOptrDrawableBySlotId, return null");
                return null;
            } else {
                return mContext.getResources().getDrawable(info.mSimBackgroundDarkRes);
            }
        } else {
            throw new IndexOutOfBoundsException();
        }
    }

    private void initSimChangedPrompt() {
        int newSimSlot = mSimChangedStatus.getNewSimSlot();
        String simDetectStatus = mSimChangedStatus.getSimDetectStatus();
        
        String msg = null;
        int newSimNumber = getSimNumber(newSimSlot);
        TextView firstName = (TextView)mPromptView.findViewById(com.mediatek.internal.R.id.first_sim_name);
        TextView secondName = (TextView)mPromptView.findViewById(com.mediatek.internal.R.id.second_sim_name);
        
        if (SIM_DETECT_NEW.equals(simDetectStatus)) {
          //get prompt message and hide sim name text if it is excess
            if (newSimNumber == 1) {
                secondName.setVisibility(View.GONE);
                msg = mContext.getResources().getString(com.mediatek.internal.R.string.change_setting_for_onenewsim);
            } else {
                msg = mContext.getResources().getString(com.mediatek.internal.R.string.change_setting_for_twonewsim);
            }
            //get sim name
            int simId = 0;
            while (newSimSlot != 0) {
                simId++;
                if ((newSimSlot & 0x01) != 0) {
                    if (simId == 1) {
                        addOptrNameBySlot(firstName, PhoneConstants.GEMINI_SIM_1);
                    } else if (simId == 2) {
                        if (newSimNumber == 1) {
                            addOptrNameBySlot(firstName, PhoneConstants.GEMINI_SIM_2);
                        } else {
                            addOptrNameBySlot(secondName, PhoneConstants.GEMINI_SIM_2);
                        }
                    }
                }
                newSimSlot = newSimSlot >>> 1;
            }
        } else if (SIM_DETECT_REMOVE.equals(simDetectStatus)) {
            msg = mContext.getResources().getString(com.mediatek.internal.R.string.sim_card_removed);
            firstName.setVisibility(View.GONE);
            secondName.setVisibility(View.GONE);
        } else if (SIM_DETECT_SWAP.equals(simDetectStatus)) {
            msg = mContext.getResources().getString(com.mediatek.internal.R.string.sim_card_swapped);
            firstName.setVisibility(View.GONE);
            secondName.setVisibility(View.GONE);
        } else {
            throw new IllegalStateException("Unknown SIMCard Changed:" + simDetectStatus);
        }
        
        ((TextView)mPromptView.findViewById(com.mediatek.internal.R.id.prompt)).setText(msg);
    }
    private void initSimSettingsView() {
        long voiceCallSlot = Settings.System.getLong(mContext.getContentResolver(), 
                     Settings.System.VOICE_CALL_SIM_SETTING, Settings.System.DEFAULT_SIM_NOT_SET);
        long smsSlot = Settings.System.getLong(mContext.getContentResolver(), 
                     Settings.System.SMS_SIM_SETTING, Settings.System.DEFAULT_SIM_NOT_SET);
        long dataSlot = Settings.System.getLong(mContext.getContentResolver(), 
                     Settings.System.GPRS_CONNECTION_SIM_SETTING, Settings.System.DEFAULT_SIM_NOT_SET);
        long videoCallSlot = Settings.System.getLong(mContext.getContentResolver(), 
                     Settings.System.VIDEO_CALL_SIM_SETTING, Settings.System.DEFAULT_SIM_NOT_SET);
        TelephonyManager telephony = (TelephonyManager) mContext.getSystemService(Context.TELEPHONY_SERVICE);
        boolean voiceCapable = (telephony != null && telephony.isVoiceCapable());
        boolean smsCapable = (telephony != null && telephony.isSmsCapable());
        boolean multiSim = mSimChangedStatus.getSIMCount() >= 2;
        
        if (DEBUG) {
            Log.i(TAG, "initSimSettingsView, isVoiceCapable=" + voiceCapable
                    + ", isSmsCapabl=" + smsCapable
                    + ", voicecallSlot=" + voiceCallSlot
                    + ", smsSlot=" + smsSlot
                    + ", dataSlot=" + dataSlot
                    + ", videoSlot=" + videoCallSlot
                    + ", multiSim=" + multiSim);
        }

        ((TextView) mPromptView.findViewById(com.mediatek.internal.R.id.sim_setting_prompt))
                .setText(com.mediatek.internal.R.string.default_sim_setting_prompt);

        TextView voiceCall = (TextView) mPromptView.findViewById(com.mediatek.internal.R.id.voice_call);
        TextView voiceCallOptr = (TextView) mPromptView.findViewById(com.mediatek.internal.R.id.voice_call_opr);
        View voiceCallItem = mPromptView.findViewById(com.mediatek.internal.R.id.voice_call_item);
        if (shouldShowVoiceCall(voiceCapable, multiSim)) {
            voiceCall.setText(com.mediatek.internal.R.string.keyguard_voice_call);
            addOptrNameById(voiceCallOptr, voiceCallSlot);
        } else {
            voiceCallItem.setVisibility(View.GONE);
        }
        
        TextView videoCall = (TextView) mPromptView.findViewById(com.mediatek.internal.R.id.video_call);
        TextView videoCallOptr = (TextView) mPromptView.findViewById(com.mediatek.internal.R.id.video_call_opr);
        View videoCallItem = mPromptView.findViewById(com.mediatek.internal.R.id.video_call_item);        
        if (shouldShowVideoCall(voiceCapable, multiSim)) {
            videoCall.setText(com.mediatek.internal.R.string.keyguard_video_call);
            addOptrNameById(videoCallOptr, videoCallSlot);
        } else {
            videoCallItem.setVisibility(View.GONE);
        }
        
        TextView sms = (TextView) mPromptView.findViewById(com.mediatek.internal.R.id.sms);
        TextView smsOptr = (TextView) mPromptView.findViewById(com.mediatek.internal.R.id.sms_opr);
        View smsItem = mPromptView.findViewById(com.mediatek.internal.R.id.sms_item);
        if (shouldShowSms(smsCapable, multiSim)) {
            sms.setText(com.mediatek.internal.R.string.keyguard_sms);
            addOptrNameById(smsOptr, smsSlot);
        } else {
            smsItem.setVisibility(View.GONE);
        }
        
        TextView data = (TextView) mPromptView.findViewById(com.mediatek.internal.R.id.data);
        TextView dataOptr = (TextView) mPromptView.findViewById(com.mediatek.internal.R.id.data_opr);
        data.setText(com.mediatek.internal.R.string.keyguard_data);
        addOptrNameById(dataOptr, dataSlot);
    }
    
    private boolean shouldShowVoiceCall(boolean voiceCallCapable, boolean multiSim) {
        if (DEBUG) {
            Log.i(TAG, "shouldShowVoiceCall, voiceCallCapable = " + voiceCallCapable + ", multiSim = " + multiSim );
        }
        if (voiceCallCapable && (multiSim || internetCallIsOn())) {
            return true;
        } else {
            return false;
        }
    }
    
    private boolean internetCallIsOn() {
        boolean isSupport = SipManager.isVoipSupported(mContext);
        boolean isOn = Settings.System.getInt(mContext.getContentResolver(), Settings.System.ENABLE_INTERNET_CALL, 0) == 1;
        if (DEBUG) {
            Log.i(TAG, "internetCallIsOn, isSupport = " + isSupport + ", isOn = " + isOn );
        }
        if (isSupport && isOn) {
            return true;
        } else {
            return false;
        }
    }

    private boolean shouldShowVideoCall(boolean voiceCallCapable, boolean multiSim) {
        if (DEBUG) {
            Log.i(TAG, "shouldShowVideoCall, video_SUPPORT = " + KeyguardUtils.isMediatekVT3G324MSupport() 
                    + ", 3G_SWITCH = " + KeyguardUtils.isMediatekGemini3GSwitchSupport()
                    + ", voiceCallCapable = " + voiceCallCapable
                    + "multiSim = " + multiSim);
        }
        if (voiceCallCapable && KeyguardUtils.isMediatekVT3G324MSupport()
                && KeyguardUtils.isMediatekGemini3GSwitchSupport()) {
            return true;
        } else {
            return false;
        }
    }
    private boolean shouldShowSms(boolean smsCapable, boolean multiSim) {
        if (DEBUG) {
            Log.i(TAG, "shouldShowSms, smsCapable = " + smsCapable 
                    + ", multiSim = " + multiSim );
        }
        if (smsCapable && multiSim) {
            return true;
        } else {
            return false;
        }
    }
    public void addOptrNameBySlot(TextView v, int slot) {
        v.setBackground(getOptrDrawableBySlot(slot));
        int simCardNamePadding = mContext.getResources().
                    getDimensionPixelSize(com.mediatek.internal.R.dimen.sim_card_name_padding);
        v.setPadding(simCardNamePadding, 0, simCardNamePadding, 0);
        String optrname = getOptrNameBySlot(slot);
        if (null == optrname) {
            v.setText(com.mediatek.internal.R.string.searching_simcard);
        } else {
            v.setText(optrname);
        }
    }
    
    public void addOptrNameById(TextView v, long simId) {
        v.setBackground(getOptrDrawableById(simId));
        int simCardNamePadding = mContext.getResources().
                    getDimensionPixelSize(com.mediatek.internal.R.dimen.sim_card_name_padding);
        v.setPadding(simCardNamePadding, 0, simCardNamePadding, 0);
        String optrname = getOptrNameById(simId);    
        if (null == optrname) {
            v.setText(com.mediatek.internal.R.string.searching_simcard);
        } else {
            v.setText(optrname);
        }
    }

    /**
     * Whether or not exist SIM card in device.
     * 
     * @return
     */
    public boolean isSIMInserted(int slotId) {
        try {
            final ITelephony phone = 
                ITelephony.Stub.asInterface(ServiceManager.checkService("phone"));
            if (phone != null && !phone.isSimInsert(slotId)) {
                return false;
            }
        } catch (RemoteException ex) {
            KeyguardUtils.xlogE(TAG, "Get sim insert status failure!");
            return false;
        }
        return true;
    }

    public void setDebugFilterStatus(boolean debugFlag) {
        DEBUG = debugFlag;
    }

    /**
     * M: add for power-off alarm Check the boot mode whether alarm boot or
     * normal boot (including ipo boot).
     */
    public static boolean isAlarmBoot() {
        String bootReason = SystemProperties.get("sys.boot.reason");
        boolean ret = (bootReason != null && bootReason.equals("1")) ? true
                : false;
        return ret;
    }

    /**
     * Reload some of our resources when the configuration changes.
     *
     * We don't reload everything when the configuration changes -- we probably
     * should, but getting that smooth is tough.  Someday we'll fix that.  In the
     * meantime, just update the things that we know change.
     */

    boolean isGMSRunning() {
        boolean running = false;
        boolean isExist = true;
        mPm = mContext.getPackageManager();
        mComponentName = new ComponentName(GMS_SETUP_PACKAGE, GMS_SETUP_COMPONENT);

        try {
            mPm .getInstallerPackageName("com.google.android.setupwizard");
        } catch (IllegalArgumentException e) {
           isExist = false;
        }
        if (isExist && (PackageManager.COMPONENT_ENABLED_STATE_ENABLED == 
            mPm.getComponentEnabledSetting(mComponentName)
            || PackageManager.COMPONENT_ENABLED_STATE_DEFAULT ==
            mPm.getComponentEnabledSetting(mComponentName))) {
            running = true;
        }
        KeyguardUtils.xlogD(TAG, "isGMSRunning, isGMSExist = " + isExist + ", running = " + running);
        return running;
    }
    
    void updateResources() {
        if (null != mSimCardChangedDialog && mSimCardChangedDialog.isShowing()) {
            mSimCardChangedDialog.setTitle(com.mediatek.internal.R.string.sim_card_changed_dialog_title);
            
            Button nagbtn = mSimCardChangedDialog.getButton(DialogInterface.BUTTON_NEGATIVE);
            if (null != nagbtn) {
                nagbtn.setText(com.mediatek.internal.R.string.keyguard_close);
            }
            Button posbtn = mSimCardChangedDialog.getButton(DialogInterface.BUTTON_POSITIVE);
            if (null != posbtn) {
                posbtn.setText(com.mediatek.internal.R.string.change_settings);
            }
            
            initSimChangedPrompt();
            initSimSettingsView();
        }
    }

    public boolean getSearchingFlag(int simId) {
       if (KeyguardUtils.isGemini() && PhoneConstants.GEMINI_SIM_2 == simId) {
              return mNetSearchingGemini;
       } else {
          return mNetSearching;
       }
    }

    protected void handleLockScreenUpdateLayout(int slotId) {
        for (int i = 0; i < mCallbacks.size(); i++) {
            KeyguardUpdateMonitorCallback cb = mCallbacks.get(i).get();
            if (cb != null) {
                cb.onLockScreenUpdate(slotId);
            }
        }
    }

    private void handleSIMNameUpdate(int slotId) {
       if (KeyguardUtils.isGemini()) {
           for (int i = 0; i < mCallbacks.size(); i++) {
               KeyguardUpdateMonitorCallback cb = mCallbacks.get(i).get();
               if (cb != null) {
                   cb.onLockScreenUpdate(slotId);
               }
           }
           updateResources();//update the new sim detected or default sim removed
       }
    }

    private void handleSIMInfoChanged(int slotId) { //update the siminfo
        if (KeyguardUtils.isGemini()) {
            for (int i = 0; i < mCallbacks.size(); i++) {
               KeyguardUpdateMonitorCallback cb = mCallbacks.get(i).get();
               if (cb != null) {
                   cb.onSIMInfoChanged(slotId);
               }
            }
        }
    }

    private void handleSIMCardChanged() {
        LayoutInflater factory = LayoutInflater.from(mContext);
        AlertDialog.Builder dialogBuilder = new AlertDialog.Builder(mContext);
        dialogBuilder.setCancelable(false);
        dialogBuilder.setPositiveButton(com.mediatek.internal.R.string.change_settings,
                new AlertDialog.OnClickListener() {
                    public void onClick(DialogInterface arg0, int arg1) {
                    //begin to call setting interface
                    Intent intent = new Intent("android.settings.GEMINI_MANAGEMENT");
                    intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                    mContext.startActivity(intent);
                }
        });
        dialogBuilder.setNegativeButton(com.mediatek.internal.R.string.keyguard_close, null);
        dialogBuilder.setTitle(com.mediatek.internal.R.string.sim_card_changed_dialog_title);
        mPromptView = factory.inflate(com.mediatek.internal.R.layout.prompt, null);

        initSimChangedPrompt();
        initSimSettingsView();
        dialogBuilder.setView(mPromptView);
        mSimCardChangedDialog = dialogBuilder.create();
        mSimCardChangedDialog.getWindow().setType(WindowManager.LayoutParams.TYPE_SYSTEM_DIALOG);
        mSimCardChangedDialog.setOnDismissListener(new DialogInterface.OnDismissListener() {
            @Override
            public void onDismiss(DialogInterface di) {
                // report close
                reportDialogClose();
            }
        });
        mSimCardChangedDialog.show();
    }
    
    private int getSimNumber(int simSlot) {
        int n = 0;
        while (simSlot != 0) {
            if ((simSlot & 0x01) != 0) {
                n++;
            }
            simSlot = simSlot >>> 1;
        }
        return n;
    }

    //MTK-START [mtk80950][1204010][ALPS00266631]check whether download calibration data or not
    private void handleDownloadCalibrationDataUpdate() {
        Log.d(TAG, "handleDownloadCalibrationDataUpdate");
        for (int i = 0; i < mCallbacks.size(); i++) {
            KeyguardUpdateMonitorCallback cb = mCallbacks.get(i).get();
            if (cb != null) {
                cb.onDownloadCalibrationDataUpdate(mCalibrationData);
            }
        }
    }

    /**
     * ALPS00264727: register to receive notification about system state change (shutdown, bootup)
     * for KeyguardViewManager only
     */
    public void registerSystemStateCallback(SystemStateCallback callback) {
        mSystemStateCallback = callback;
    }

    public void unRegisterRadioStateCallback() {
        mRadioStateCallback = null;
    }
    
    /**
     ** M: Used to set specified sim card's pin or puk dismiss flag
     * 
     * @param simId the id of the sim card to set dismiss flag
     * @param pinOrPuk true to dismiss this sim card's pin, false for puk
     * @param dismiss true to dismiss this flag, false to clear
     */
    public void setPINDismiss(int simId, boolean pinOrPuk, boolean dismiss) {
        Log.i(TAG, "setPINDismiss, simId=" + simId + ", pinOrPuk=" + pinOrPuk
                + ", dismiss=" + dismiss + ", mPinPukDismissFlag=" + mPinPukDismissFlag);
        int pinFlag;
        int pukFlag;
        if (simId == PhoneConstants.GEMINI_SIM_1) {
            pinFlag = SIM_1_PIN_DISMISSED;
            pukFlag = SIM_1_PUK_DISMISSED;
        } else {
            pinFlag = SIM_2_PIN_DISMISSED;
            pukFlag = SIM_2_PUK_DISMISSED;
        }
        if (dismiss) {
            mPinPukDismissFlag |= (pinOrPuk ? pinFlag : pukFlag);
        } else {
            mPinPukDismissFlag &= (pinOrPuk ? ~pinFlag : ~pukFlag);
        }
    }

    /**
     ** M: Used to get specified sim card's pin or puk dismiss flag
     * 
     * @param simId the id of the sim card to set dismiss flag
     * @param pinOrPuk true to get falg for this sim card's pin, false for puk
     * @return Returns false if dismiss flag is set.
     */
    public boolean getPINDismissFlag(int simId, boolean pinOrPuk) {
        Log.i(TAG, "getPINDismissFlag, simId=" + simId + ", pinOrPuk="
                + pinOrPuk + ", mPinPukDismissFlag=" + mPinPukDismissFlag);
        int pinFlag;
        int pukFlag;
        if (simId == PhoneConstants.GEMINI_SIM_1) {
            pinFlag = SIM_1_PIN_DISMISSED;
            pukFlag = SIM_1_PUK_DISMISSED;
        } else {
            pinFlag = SIM_2_PIN_DISMISSED;
            pukFlag = SIM_2_PUK_DISMISSED;
        }
        boolean result = false;
        if (pinOrPuk) {
            result = (mPinPukDismissFlag & pinFlag) == pinFlag ? true : false;
        } else {
            result = (mPinPukDismissFlag & pukFlag) == pukFlag ? true : false;
        }
        return result;
    }

    /**
     * M: Used to register phone state callback, will be triggered when ,modem is reset
     */
    public void registerRadioStateCallback(RadioStateCallback callback) {
        mRadioStateCallback = callback;
    }

    public IccCardConstants.State getSimState(int simId) {
        if (simId == PhoneConstants.GEMINI_SIM_2) {
            KeyguardUtils.xlogD(TAG, "mSim2State = " + mSim2State);
            return mSim2State;
        } else {
            KeyguardUtils.xlogD(TAG, "mSimState = " + mSimState);
            return mSimState;
        }
    }

    public IccCardConstants.State getLastSimState(int simId) {
        if (simId == PhoneConstants.GEMINI_SIM_2) {
            KeyguardUtils.xlogD(TAG, "mSim2LastState = " + mSim2LastState);
            return mSim2LastState;
        } else {
            KeyguardUtils.xlogD(TAG, "mSimLastState = " + mSimLastState);
            return mSimLastState;
        }
    }

    public boolean isDeviceCharging() {
        return mBatteryStatus.status != BatteryManager.BATTERY_STATUS_DISCHARGING
                && mBatteryStatus.status != BatteryManager.BATTERY_STATUS_NOT_CHARGING;
    }

    public CharSequence getTelephonyPlmn(int simId) {
        if (simId == PhoneConstants.GEMINI_SIM_2) {
            return mTelephonyPlmnGemini;
        } else {
            return mTelephonyPlmn;
        }
    }

    public CharSequence getTelephonySpn(int simId) {
        if (simId == PhoneConstants.GEMINI_SIM_2) {
            return mTelephonySpnGemini;
        } else {
            return mTelephonySpn;
        }
    }
    /**
     * M: Return device is in encrypte mode or not.
     * If it is in encrypte mode, we will not show lockscreen.
     */
    public boolean isEncryptMode() {
        String state = SystemProperties.get("vold.decrypt");
        return !("".equals(state) || "trigger_restart_framework".equals(state));
    }

    /**
     * M: reportSimUnlocked For Gemini phone
     */
    public void reportSimUnlocked(int simId) {
        //if (DEBUG) KeyguardUtils.xlogD(TAG, "mSimState = "+ IccCardConstants.State.READY + ", simId = " + simId);
        if (simId == PhoneConstants.GEMINI_SIM_2 && (mSim2State != IccCardConstants.State.NETWORK_LOCKED)) {
            handleSimStateChange(new SimArgs(IccCardConstants.State.READY, simId));
        } else if (simId == PhoneConstants.GEMINI_SIM_1 && (mSimState != IccCardConstants.State.NETWORK_LOCKED)) {
            handleSimStateChange(new SimArgs(IccCardConstants.State.READY, simId));
        } else {
            if (DEBUG) KeyguardUtils.xlogD(TAG, "here, SIM "+simId+" state is NETWORK_LOCKED");
        }
    }

    /// M: Initialize phone state listener, used to update sim's network searching state
    private void initPhoneStateListener() {
        mPhoneStateListener = new PhoneStateListener() { // for SIM1 and single
           @Override
           public void onServiceStateChanged(ServiceState state) {
                if (state != null) {
                   int regState = state.getRegState();
                   if (mNetSearching && (regState != ServiceState.REGISTRATION_STATE_NOT_REGISTERED_AND_SEARCHING)) {
                       KeyguardUtils.xlogD(TAG, "PhoneStateListener, sim1 searching finished");
                       mNetSearching = false;
                   }
                   
                   if (ServiceState.REGISTRATION_STATE_NOT_REGISTERED_AND_SEARCHING == regState) {
                       KeyguardUtils.xlogD(TAG, "PhoneStateListener, sim1 searching begin");
                        mNetSearching = true;
                   }
                   for (int i = 0; i < mCallbacks.size(); i++) {
                       KeyguardUpdateMonitorCallback cb = mCallbacks.get(i).get();
                       if (cb != null) {
                           cb.onSearchNetworkUpdate(PhoneConstants.GEMINI_SIM_1, mNetSearching);
                       }
                   }
                }
            }
        };
        
        mPhoneStateListenerGemini = new PhoneStateListener() { //only for SIM2
           @Override
           public void onServiceStateChanged(ServiceState state) {
              if (state != null) {
                  int regState = state.getRegState(); 
                  if (mNetSearchingGemini && (regState != ServiceState.REGISTRATION_STATE_NOT_REGISTERED_AND_SEARCHING)){
                      KeyguardUtils.xlogD(TAG, "PhoneStateListener, sim2 searching finished");
                      mNetSearchingGemini = false;
                  }
                  if (ServiceState.REGISTRATION_STATE_NOT_REGISTERED_AND_SEARCHING == regState) {
                      KeyguardUtils.xlogD(TAG, "PhoneStateListener, sim2 searching begin");
                      mNetSearchingGemini = true;
                  }
                  for (int i = 0; i < mCallbacks.size(); i++) {
                      KeyguardUpdateMonitorCallback cb = mCallbacks.get(i).get();
                      if (cb != null) {
                          cb.onSearchNetworkUpdate(PhoneConstants.GEMINI_SIM_2, mNetSearchingGemini);
                      }
                  }
               }
           }
        };

       if (KeyguardUtils.isGemini()) {
           try {
               ITelephony t = ITelephony.Stub.asInterface(ServiceManager.getService(Context.TELEPHONY_SERVICE));
               Boolean notifyNow = (t != null);
               ITelephonyRegistry tr1 = ITelephonyRegistry.Stub.asInterface(ServiceManager.getService("telephony.registry"));
               tr1.listen(TAG, mPhoneStateListener.getCallback(), PhoneStateListener.LISTEN_SERVICE_STATE, notifyNow);
               ITelephonyRegistry tr2 = ITelephonyRegistry.Stub.asInterface(ServiceManager.getService("telephony.registry2"));
               tr2.listen(TAG, mPhoneStateListenerGemini.getCallback(), PhoneStateListener.LISTEN_SERVICE_STATE, notifyNow);
           } catch (RemoteException e) {
               Log.e(TAG, "Fail to listen GEMINI state", e);
           } catch (NullPointerException e) {
               Log.e(TAG, "The registry is null", e);
           }
       } else {
           ((TelephonyManager)mContext.getSystemService(Context.TELEPHONY_SERVICE))
                .listen(mPhoneStateListener, PhoneStateListener.LISTEN_SERVICE_STATE);
       }
    }

    private void handle3GSwitchEvent() {
        //only for gemini 
        mPinPukDismissFlag = PIN_PUK_RESET;
    }

    private void handleRadioStateChanged(int slotId) {
        KeyguardUtils.xlogD(TAG, "handleRadioStateChanged, slotId=" + slotId + ", mSimState=" + mSimState
                        + ", mSim2State=" + mSim2State);
       setPINDismiss(slotId, true, false);
       setPINDismiss(slotId, false, false);
       if (PhoneConstants.GEMINI_SIM_1 == slotId
           && IccCardConstants.State.PIN_REQUIRED == mSimState || IccCardConstants.State.PUK_REQUIRED == mSimState) {
           if (null != mRadioStateCallback) {
              mRadioStateCallback.onRadioStateChanged(slotId);
           }
       } else if (PhoneConstants.GEMINI_SIM_2 == slotId 
           && IccCardConstants.State.PIN_REQUIRED == mSim2State || IccCardConstants.State.PUK_REQUIRED == mSim2State) {
           if (null != mRadioStateCallback) {
              mRadioStateCallback.onRadioStateChanged(slotId);
           }
       }
    }

    // ALPS00264727: handle system shutdown or bootup intent
    private void handleSystemStateChanged(int state) {
        if (null == mSystemStateCallback) {
            if (DEBUG) {
                KeyguardUtils.xlogD(TAG, "mSystemStateCallback is null, skipped!");
            }
            return;
        }

        switch (state) {
            case SYSTEM_STATE_BOOTUP:
                mSystemStateCallback.onSysBootup();
                break;

            case SYSTEM_STATE_SHUTDOWN:
                mSystemStateCallback.onSysShutdown();
                break;

            default:
                if (DEBUG) {
                    KeyguardUtils.xlogE(TAG, "received unknown system state change event");
                }
                break;
        }
    }

    /**
     * M: Handle {@link #MSG_BOOTUP_MODE_PICK}. This function is for CTA test, do not delete
     */
    private void handleBootupModePick() {
        if (DEBUG) {
            KeyguardUtils.xlogD(TAG, "handleBootupModePick");
        }

        //String[] simname = mContext.getResources().getStringArray(com.mediatek.R.array.bootup_mode);
        String[] simname = new String[2];
        simname[0] = getOptrNameBySlotForCTA(PhoneConstants.GEMINI_SIM_1);
        simname[1] = getOptrNameBySlotForCTA(PhoneConstants.GEMINI_SIM_2);
        mCtaDialog = new AlertDialog.Builder(mContext)
                .setTitle(com.mediatek.R.string.choose_bootup_mode)
                .setCancelable(false)
                .setItems(simname,
                new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface dialog, int which) {
                        if (mCtaDialog != null) {
                            mCtaDialog.dismiss();
                            mCtaDialog = null;
                        }
                        sDualSimSetting = 1; // The dialog item has been selected.
                        Intent intent = new Intent(Intent.ACTION_DUAL_SIM_MODE_CHANGED);
                        switch(which) {
                            //Dual Sim
                            case 0:
                                if (DEBUG) {
                                    Log.d(TAG, "handleBootupModePick, mode = dual sim");
                                }
                                intent.putExtra(Intent.EXTRA_DUAL_SIM_MODE, GeminiNetworkSubUtil.MODE_DUAL_SIM);
                                mContext.sendBroadcast(intent);
                                // Change the system setting
                                Settings.System.putInt(mContext.getContentResolver(),
                                        Settings.System.DUAL_SIM_MODE_SETTING, 3);
                                break;
                                
                            // Sim 1
                            case 1:
                                if (DEBUG) {
                                    KeyguardUtils.xlogD(TAG, "handleBootupModePick, mode = sim 1");
                                }
                                intent.putExtra(Intent.EXTRA_DUAL_SIM_MODE, GeminiNetworkSubUtil.MODE_SIM1_ONLY);
                                mContext.sendBroadcast(intent);
                                // Change the system setting
                                Settings.System.putInt(mContext.getContentResolver(),
                                        Settings.System.DUAL_SIM_MODE_SETTING, 1);
                                break;
                                
                            // Sim 2
                            case 2:
                                if (DEBUG) {
                                    KeyguardUtils.xlogD(TAG, "handleBootupModePick, mode = sim 2");
                                }
                                intent.putExtra(Intent.EXTRA_DUAL_SIM_MODE, GeminiNetworkSubUtil.MODE_SIM2_ONLY);
                                mContext.sendBroadcast(intent);
                                // Change the system setting
                                Settings.System.putInt(mContext.getContentResolver(),
                                        Settings.System.DUAL_SIM_MODE_SETTING, 2);
                                break;
                                
                            //Dont remind me 
                            case 3:
                                if (DEBUG) {
                                    KeyguardUtils.xlogD(TAG, "handleBootupModePick, mode = don't remind me");
                                }
                                // Change the system setting
                                Settings.System.putInt(mContext.getContentResolver(), 
                                        Settings.System.BOOT_UP_SELECT_MODE, 0);
                                break;

                            default:
                                if (DEBUG) {
                                    KeyguardUtils.xlogD(TAG, "handleBootupModePick, default, mode = dual sim");
                                }
                                intent.putExtra(Intent.EXTRA_DUAL_SIM_MODE, GeminiNetworkSubUtil.MODE_DUAL_SIM);
                                mContext.sendBroadcast(intent);
                                // Change the system setting
                                Settings.System.putInt(mContext.getContentResolver(),
                                        Settings.System.DUAL_SIM_MODE_SETTING, 3);
                        }
                    }
                }).create();
        mCtaDialog.getWindow().setType(WindowManager.LayoutParams.TYPE_KEYGUARD_DIALOG);
        if (!mContext.getResources().getBoolean(com.android.internal.R.bool.config_sf_slowBlur)) {
            mCtaDialog.getWindow().setFlags(WindowManager.LayoutParams.FLAG_BLUR_BEHIND,
                    WindowManager.LayoutParams.FLAG_BLUR_BEHIND);
        }
        //when dual sim mode setting dialog shows, dual_sim_setting be initialized to 0,
        // which is used to check if user has clicked the items or not, if user has not clicked the items,
        // the airplane mode should be grayed or disabled. see ALPS00131386
        sDualSimSetting = 0;
        mCtaDialog.show();
    }

    /**
     * M: interface is a call back for the user who need to popup Dialog.
     */
    public static interface DialogShowCallBack {
        public void show();
    }

    /**
     * M: request show dialog
     * @param callback the user need to implement the callback.
     */
    public void requestShowDialog(DialogShowCallBack callback) {
        mDialogSequenceManager.requestShowDialog(callback);
    }

    /**
     * M: when the user close dialog, should report the status. 
     */
    public void reportDialogClose() {
        mDialogSequenceManager.reportDialogClose();
    }

    /**
     * M: interface for showing dialog sequencely manager.
     * 
     */
    public static interface SequenceDialog {
        /**
         * the client  needed to show a dialog should call this
         * @param callback the client should implement the callback.
         */
        public void requestShowDialog(DialogShowCallBack callback);
        /**
         * If the client close the dialog, should call this to report.
         */
        public void reportDialogClose();
    }

    /// M: Manage the dialog sequence.
    private DialogSequenceManager mDialogSequenceManager;

    /**
     * M: Manage the dialog sequence.
     * It implment the main logical of the sequence process.
     */
    private class DialogSequenceManager implements SequenceDialog {
        /// M: log tag for this class
        private static final String CLASS_TAG = "DialogSequenceManager";
        /// M: debug switch for the log.
        private static final boolean CLASS_DEBUG = true;
        /// M: The queue to save the call backs.
        private Queue<DialogShowCallBack> mDialogShowCallbackQueue;
        /// M: Whether the inner dialog is showing
        private boolean mInnerDialogShowing = false;
        /// M: If keyguard set the dialog sequence value, and inner dialog is showing. 
        private boolean mLocked = false;

        public DialogSequenceManager() {
            if (CLASS_DEBUG) {
                KeyguardUtils.xlogD(TAG, CLASS_TAG + " DialogSequenceManager()");
            }
            mDialogShowCallbackQueue = new LinkedList<DialogShowCallBack>();

            mContext.getContentResolver().registerContentObserver(System.getUriFor(System.DIALOG_SEQUENCE_SETTINGS),
                    false, mDialogSequenceObserver);
            mContext.getContentResolver().registerContentObserver(System.getUriFor(System.OOBE_DISPLAY),
                    false, mOOBEObserver);
         }

        public void requestShowDialog(DialogShowCallBack callback) {
            if (CLASS_DEBUG) {
                KeyguardUtils.xlogD(TAG, CLASS_TAG + " --requestShowDialog()");
            }
            mDialogShowCallbackQueue.add(callback);
            handleShowDialog();
        }

        public void handleShowDialog() {
            if (CLASS_DEBUG) {
                KeyguardUtils.xlogD(TAG, CLASS_TAG + " --handleShowDialog()--enableShow() = " + enableShow());
            }
            if (enableShow()) {
                setInnerDialogShowing(true);
                if (getLocked()) {
                    DialogShowCallBack dialogCallBack = mDialogShowCallbackQueue.poll();
                    if (CLASS_DEBUG) {
                        KeyguardUtils.xlogD(TAG, CLASS_TAG + " --handleShowDialog()--dialogCallBack = " + dialogCallBack);
                    }
                    if (dialogCallBack != null) {
                        dialogCallBack.show();
                    }
                } else {
                    if (CLASS_DEBUG) {
                        KeyguardUtils.xlogD(TAG, CLASS_TAG + " --handleShowDialog()--System.putInt( " 
                                + System.DIALOG_SEQUENCE_SETTINGS + " value = " + System.DIALOG_SEQUENCE_KEYGUARD);
                    }
                    System.putInt(mContext.getContentResolver(), System.DIALOG_SEQUENCE_SETTINGS,
                            System.DIALOG_SEQUENCE_KEYGUARD);
                }
            }
        }

        public void reportDialogClose() {
            if (CLASS_DEBUG) {
                KeyguardUtils.xlogD(TAG, CLASS_TAG + " --reportDialogClose()--mDialogShowCallbackQueue.isEmpty() = " 
                        + mDialogShowCallbackQueue.isEmpty());
            }
            setInnerDialogShowing(false);
            
            if (mDialogShowCallbackQueue.isEmpty()) {
                if (CLASS_DEBUG) {
                    KeyguardUtils.xlogD(TAG, CLASS_TAG + " --reportDialogClose()--System.putInt( " 
                            + System.DIALOG_SEQUENCE_SETTINGS + " value = " + System.DIALOG_SEQUENCE_DEFAULT
                            + " --setLocked(false)--");
                }
                System.putInt(mContext.getContentResolver(), System.DIALOG_SEQUENCE_SETTINGS,
                        System.DIALOG_SEQUENCE_DEFAULT);
                setLocked(false);
            } else {
                handleShowDialog();
            }
        }

        /**
         * M : Combine the conditions to deceide whether enable showing or not
         */
        private boolean enableShow() {
            if (CLASS_DEBUG) {
                KeyguardUtils.xlogD(TAG, CLASS_TAG + " --enableShow()-- !mDialogShowCallbackQueue.isEmpty() = " + !mDialogShowCallbackQueue.isEmpty()
                        + " !getInnerDialogShowing() = " + !getInnerDialogShowing()
                        + " !isOtherModuleShowing() = " + !isOtherModuleShowing()
                        + "!isAlarmBoot() = " + !isAlarmBoot()
                        + " !isGMSRunning() = " + !isGMSRunning()
                        + " !isOOBEShowing() = " + !isOOBEShowing());
            }

            return !mDialogShowCallbackQueue.isEmpty() && !getInnerDialogShowing() && !isOtherModuleShowing() 
                    && !isAlarmBoot() && !isGMSRunning() && !isOOBEShowing() && !isEncryptMode();
        }

        /**
         * M : Query the dialog sequence settings to decide whether other module's dialog is showing or not.
         */
        private boolean isOtherModuleShowing() {
            int value = queryDialogSequenceSeetings();
            if (CLASS_DEBUG) {
                KeyguardUtils.xlogD(TAG, CLASS_TAG + " --isOtherModuleShowing()--" + System.DIALOG_SEQUENCE_SETTINGS + " = " + value);
            }
            if (value == System.DIALOG_SEQUENCE_DEFAULT || value == System.DIALOG_SEQUENCE_KEYGUARD) {
                return false;
            }
            return true;
        }

        private void setInnerDialogShowing(boolean show) {
            mInnerDialogShowing = show;
        }

        private boolean getInnerDialogShowing() {
            return mInnerDialogShowing;
        }
        
        private void setLocked(boolean locked) {
            mLocked = locked;
        }

        private boolean getLocked() {
            return mLocked;
        }

        /**
         * M : Query dialog sequence settings value 
         */
        private int queryDialogSequenceSeetings() {
            int value = System.getInt(mContext.getContentResolver(), System.DIALOG_SEQUENCE_SETTINGS,
                    System.DIALOG_SEQUENCE_DEFAULT);
            return value;
        }

        /// M: dialog sequence observer for dialog sequence settings
        private ContentObserver mDialogSequenceObserver = new ContentObserver(new Handler()) {
            @Override
            public void onChange(boolean selfChange) {
                int value = queryDialogSequenceSeetings();
                if (CLASS_DEBUG) {
                    KeyguardUtils.xlogD(TAG, CLASS_TAG + " DialogSequenceObserver--onChange()--"
                            + System.DIALOG_SEQUENCE_SETTINGS + " = " + value);
                }
                if (value == System.DIALOG_SEQUENCE_DEFAULT) {
                    handleShowDialog();
                } else if (value == System.DIALOG_SEQUENCE_KEYGUARD) {
                    DialogShowCallBack dialogCallBack = mDialogShowCallbackQueue.poll();
                    if (CLASS_DEBUG) {
                        KeyguardUtils.xlogD(TAG, CLASS_TAG + " DialogSequenceObserver--onChange()--dialogCallBack = "
                                + dialogCallBack + " --setLocked(true)--");
                    }
                    if (dialogCallBack != null) {
                        dialogCallBack.show();
                    }
                    setLocked(true);
                }
            }
        };

        /**
         * M :Query the OOBE display value
         */
        private int queryOOBEDisplay() {
            int value = System.getInt(mContext.getContentResolver(), System.OOBE_DISPLAY,
                    System.OOBE_DISPLAY_DEFAULT);
            return value;
        }

        /// M: OOBE observer for settings
        private ContentObserver mOOBEObserver = new ContentObserver(new Handler()) {
            @Override
            public void onChange(boolean selfChange) {
                int value = queryOOBEDisplay();
                if (CLASS_DEBUG) {
                    KeyguardUtils.xlogD(TAG, CLASS_TAG + " OOBEObserver--onChange()--" + System.OOBE_DISPLAY 
                            + " = " + value);
                }
                if (value != System.OOBE_DISPLAY_ON) {
                    handleShowDialog();
                }
            }
        };

        /**
         * M :return whether the OOBE is showing or not.
         */
        private boolean isOOBEShowing() {
            int value = queryOOBEDisplay();
            if (CLASS_DEBUG) {
                KeyguardUtils.xlogD(TAG, CLASS_TAG + " OOBEObserver--isOOBEShowing()--" + System.OOBE_DISPLAY + " = " + value);
            }
            return (value == System.OOBE_DISPLAY_ON);
        }
    }

    /**
     *  M:Get the remaining puk count of the sim card with the simId .@{ 
     * @param simId
     */
    int getRetryPukCount(final int simId) {
        if (simId == PhoneConstants.GEMINI_SIM_2) {
            return SystemProperties.getInt("gsm.sim.retry.puk1.2",
                    GET_SIM_RETRY_EMPTY);
        } else {
            return SystemProperties.getInt("gsm.sim.retry.puk1",
                    GET_SIM_RETRY_EMPTY);
        }
    }
    /**  @}*/

    /**
     * M: implement new sim dialog callback
     */
    private class NewSimDialogCallback implements DialogShowCallBack {
        public void show() {
            KeyguardUtils.xlogD(TAG, "NewSimDialogCallback--show()--");
            mHandler.sendMessage(mHandler.obtainMessage(MSG_SIM_DETECTED));
        }
    }
    
    /**
     * When we receive a 
     * {@link com.android.internal.telephony.TelephonyIntents#SPN_STRINGS_UPDATED_ACTION} broadcast,
     * and then pass a result via our handler to {@link KeyguardUpdateMonitor#handleSimStateChange},
     * we need a single object to pass to the handler.  This class saves which sim card's spn string
     * changed.
     * M: Add gemini support
     */
    private static class SpnUpdate {
        int simId;
        
        SpnUpdate() {
            simId = 0;
        }
        
        SpnUpdate(int id) {
            simId = simId;
        }
    }

    /// M: Incoming Indicator for Keyguard Rotation @{
    public void setQueryBaseTime() {
        mQueryBaseTime = java.lang.System.currentTimeMillis();
    }
    /// @}
    
    /// M: Incoming Indicator for Keyguard Rotation @{
    public long getQueryBaseTime() {
        return mQueryBaseTime;
    }
    /// @}
}
