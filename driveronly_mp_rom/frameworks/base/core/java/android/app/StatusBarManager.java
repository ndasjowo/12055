/*
 * Copyright (C) 2007 The Android Open Source Project
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


package android.app;

import android.content.ComponentName;
import android.content.Context;
import android.os.Binder;
import android.os.RemoteException;
import android.os.IBinder;
import android.os.ServiceManager;
import android.util.Slog;
import android.view.View;

import com.android.internal.statusbar.IStatusBarService;
import com.mediatek.xlog.Xlog;

/**
 * Allows an app to control the status bar.
 *
 * @hide
 */
public class StatusBarManager {

    public static final int DISABLE_EXPAND = View.STATUS_BAR_DISABLE_EXPAND;
    public static final int DISABLE_NOTIFICATION_ICONS = View.STATUS_BAR_DISABLE_NOTIFICATION_ICONS;
    public static final int DISABLE_NOTIFICATION_ALERTS
            = View.STATUS_BAR_DISABLE_NOTIFICATION_ALERTS;
    public static final int DISABLE_NOTIFICATION_TICKER
            = View.STATUS_BAR_DISABLE_NOTIFICATION_TICKER;
    public static final int DISABLE_SYSTEM_INFO = View.STATUS_BAR_DISABLE_SYSTEM_INFO;
    public static final int DISABLE_HOME = View.STATUS_BAR_DISABLE_HOME;
    public static final int DISABLE_RECENT = View.STATUS_BAR_DISABLE_RECENT;
    public static final int DISABLE_BACK = View.STATUS_BAR_DISABLE_BACK;
    public static final int DISABLE_CLOCK = View.STATUS_BAR_DISABLE_CLOCK;
    public static final int DISABLE_SEARCH = View.STATUS_BAR_DISABLE_SEARCH;

    @Deprecated
    public static final int DISABLE_NAVIGATION = 
            View.STATUS_BAR_DISABLE_HOME | View.STATUS_BAR_DISABLE_RECENT;

    public static final int DISABLE_NONE = 0x00000000;

    public static final int DISABLE_MASK = DISABLE_EXPAND | DISABLE_NOTIFICATION_ICONS
            | DISABLE_NOTIFICATION_ALERTS | DISABLE_NOTIFICATION_TICKER
            | DISABLE_SYSTEM_INFO | DISABLE_RECENT | DISABLE_HOME | DISABLE_BACK | DISABLE_CLOCK
            | DISABLE_SEARCH;

    public static final int NAVIGATION_HINT_BACK_NOP      = 1 << 0;
    public static final int NAVIGATION_HINT_HOME_NOP      = 1 << 1;
    public static final int NAVIGATION_HINT_RECENT_NOP    = 1 << 2;
    public static final int NAVIGATION_HINT_BACK_ALT      = 1 << 3;

    private Context mContext;
    private IStatusBarService mService;
    private IBinder mToken = new Binder();

    StatusBarManager(Context context) {
        mContext = context;
    }

    private synchronized IStatusBarService getService() {
        if (mService == null) {
            mService = IStatusBarService.Stub.asInterface(
                    ServiceManager.getService(Context.STATUS_BAR_SERVICE));
            if (mService == null) {
                Slog.w("StatusBarManager", "warning: no STATUS_BAR_SERVICE");
            }
        }
        return mService;
    }

    /**
     * Disable some features in the status bar.  Pass the bitwise-or of the DISABLE_* flags.
     * To re-enable everything, pass {@link #DISABLE_NONE}.
     */
    public void disable(int what) {
        try {
            final IStatusBarService svc = getService();
            if (svc != null) {
                svc.disable(what, mToken, mContext.getPackageName());
            }
        } catch (RemoteException ex) {
            // system process is dead anyway.
            throw new RuntimeException(ex);
        }
    }
    
    /**
     * Expand the notifications panel.
     */
    public void expandNotificationsPanel() {
        try {
            final IStatusBarService svc = getService();
            if (svc != null) {
                svc.expandNotificationsPanel();
            }
        } catch (RemoteException ex) {
            // system process is dead anyway.
            throw new RuntimeException(ex);
        }
    }
	
    //#ifdef VENDOR_EDIT
	//Jun.Zhang@Plf.Framework.Input, 2013/05/30, add for
	public void collapse() {
        collapsePanels();
    }
	//#endif /* VENDOR_EDIT */
	
    /**
     * Collapse the notifications and settings panels.
     */
    public void collapsePanels() {
        try {
            final IStatusBarService svc = getService();
            if (svc != null) {
                svc.collapsePanels();
            }
        } catch (RemoteException ex) {
            // system process is dead anyway.
            throw new RuntimeException(ex);
        }
    }

    /**
     * Expand the settings panel.
     */
    public void expandSettingsPanel() {
        try {
            final IStatusBarService svc = getService();
            if (svc != null) {
                svc.expandSettingsPanel();
            }
        } catch (RemoteException ex) {
            // system process is dead anyway.
            throw new RuntimeException(ex);
        }
    }

    public void setIcon(String slot, int iconId, int iconLevel, String contentDescription) {
        try {
            final IStatusBarService svc = getService();
            if (svc != null) {
                svc.setIcon(slot, mContext.getPackageName(), iconId, iconLevel,
                    contentDescription);
            }
        } catch (RemoteException ex) {
            // system process is dead anyway.
            throw new RuntimeException(ex);
        }
    }

    public void removeIcon(String slot) {
        try {
            final IStatusBarService svc = getService();
            if (svc != null) {
                svc.removeIcon(slot);
            }
        } catch (RemoteException ex) {
            // system process is dead anyway.
            throw new RuntimeException(ex);
        }
    }

    public void setIconVisibility(String slot, boolean visible) {
        try {
            final IStatusBarService svc = getService();
            if (svc != null) {
                svc.setIconVisibility(slot, visible);
            }
        } catch (RemoteException ex) {
            // system process is dead anyway.
            throw new RuntimeException(ex);
        }
    }

    /** M: Support "SystemUI SIM indicator" feature. @{ */

    private static final String TAG = "StatusBarManager";

    public void showSIMIndicator(ComponentName componentName, String businessType) {
        String pkgName = componentName == null ? "null" : componentName.getPackageName();
        try {
            Xlog.d(TAG, "Show SIM indicator from " + pkgName + ", businiss is " + businessType + ".");
            final IStatusBarService svc = getService();
            if (svc != null) {
                svc.showSIMIndicator(businessType);
            }
        } catch (RemoteException ex) {
            Xlog.d(TAG, "Show SIM indicator from " + pkgName + " occurs exception.");
            /// M: system process is dead anyway.
            throw new RuntimeException(ex);
        }
    }

    public void hideSIMIndicator(ComponentName componentName) {
        String pkgName = componentName == null ? "null" : componentName.getPackageName();
        try {
            Xlog.d(TAG, "Hide SIM indicator from " + pkgName + ".");
            final IStatusBarService svc = getService();
            if (svc != null) {
                mService.hideSIMIndicator();
            }
        } catch (RemoteException ex) {
            Xlog.d(TAG, "Hide SIM indicator from " + pkgName + " occurs exception.");
            /// M: system process is dead anyway.
            throw new RuntimeException(ex);
        }
    }

    /** }@ */

    /** M: [SystemUI] Support Application Guide. @{ */
    public void showApplicationGuide(String appName) {
        try {
            Xlog.d(TAG, "show application guide from " + appName + ".");
            final IStatusBarService svc = getService();
            if (svc != null) {
                mService.showApplicationGuide(appName);
            }
        } catch (RemoteException ex) {
            /// M: system process is dead anyway.
            throw new RuntimeException(ex);
        }
    }
    /** }@ */
}
