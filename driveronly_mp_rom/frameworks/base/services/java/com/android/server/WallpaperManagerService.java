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

package com.android.server;

import static android.os.ParcelFileDescriptor.*;

import android.app.ActivityManagerNative;
import android.app.AppGlobals;
import android.app.IUserSwitchObserver;
import android.app.IWallpaperManager;
import android.app.IWallpaperManagerCallback;
import android.app.PendingIntent;
import android.app.WallpaperInfo;
import android.app.backup.BackupManager;
import android.app.backup.WallpaperBackupHelper;
import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.ServiceConnection;
import android.content.pm.IPackageManager;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.content.pm.ServiceInfo;
import android.content.pm.PackageManager.NameNotFoundException;
import android.content.pm.UserInfo;
import android.content.res.Resources;
import android.os.Binder;
import android.os.Bundle;
import android.os.Environment;
import android.os.FileUtils;
import android.os.IBinder;
import android.os.IRemoteCallback;
import android.os.RemoteException;
import android.os.FileObserver;
import android.os.ParcelFileDescriptor;
import android.os.RemoteCallbackList;
import android.os.SELinux;
import android.os.ServiceManager;
import android.os.SystemClock;
import android.os.UserHandle;
import android.os.UserManager;
import android.provider.Settings;
import android.service.wallpaper.IWallpaperConnection;
import android.service.wallpaper.IWallpaperEngine;
import android.service.wallpaper.IWallpaperService;
import android.service.wallpaper.WallpaperService;
import android.util.Slog;
import android.util.SparseArray;
import android.util.Xml;
import android.view.Display;
import android.view.IWindowManager;
import android.view.WindowManager;

import com.mediatek.xlog.Xlog;

import java.io.FileDescriptor;
import java.io.IOException;
import java.io.InputStream;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.PrintWriter;
import java.util.List;

import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;
import org.xmlpull.v1.XmlSerializer;

import com.android.internal.content.PackageMonitor;
import com.android.internal.util.FastXmlSerializer;
import com.android.internal.util.JournaledFile;
//#ifdef VENDOR_EDIT
//#@OppoHook
//gaoliang@Plf.DesktopApp.Wallpaper, 2012/08/27, Add for getting the width of default wallpaper
import android.os.SystemProperties;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Bitmap;
import android.graphics.Rect;
import android.graphics.Point;
import android.content.res.Configuration;
import android.graphics.Point;
//#endif /* VENDOR_EDIT */

@android.annotation.OppoHook(level=android.annotation.OppoHook.OppoHookType.CHANGE_ACCESS,
                property=android.annotation.OppoHook.OppoRomType.ROM,
                note="gaoliang change to public")
public class WallpaperManagerService extends IWallpaperManager.Stub {
    static final String TAG = "WallpaperService";
    static final boolean DEBUG = false;

    @android.annotation.OppoHook(level=android.annotation.OppoHook.OppoHookType.NEW_CLASS,
								property=android.annotation.OppoHook.OppoRomType.ROM,
								note="gaoliang add for oppo-wallpaper") 
	public class WallpaperHelper {
	    //#ifdef VENDOR_EDIT
	    //gaoliang@Plf.DesktopApp.Wallpaper, 2012/08/27, Add for getting the width and height of default wallpaper
		private int mWidthOfDefaultWallpaper = -1;
		public final float WALLPAPER_SCREENS_SPAN = 2f;
		private final String MTK_PLATFORM_PROP = "ro.mediatek.platform";
		private final int DEFAULT_WALLPAPER_RES_ID = 
	        com.oppo.internal.R.drawable.oppo_default_wallpaper;
	    //#endif /* VENDOR_EDIT */   		


		public WallpaperHelper(Context context) {
			mWidthOfDefaultWallpaper = getDefaultWallpaperWidth(context);
		}

		//#ifdef VENDOR_EDIT
		//gaoliang@Plf.DesktopApp.Wallpaper, 2012/08/27, Add for getting the width of default wallpaper
		private int getDefaultWallpaperWidth(Context context) {
			int width = -1;
	
			try {
				BitmapFactory.Options options = new BitmapFactory.Options();
				options.inJustDecodeBounds = true;
				
				boolean isMtkPlatform = 
				   !"non-mtk".equals(SystemProperties.get(MTK_PLATFORM_PROP, "non-mtk"));
				
				if (isMtkPlatform) {
					// Enable PQ support for all static wallpaper bitmap decoding
	//				  options.inPostProc = true;
	//				  options.inPostProcFlag = 1;					 
				}
	
				Resources r = context.getResources();
				BitmapFactory.decodeResource(r, DEFAULT_WALLPAPER_RES_ID, options);
				width = options.outWidth;
				Slog.w(TAG, "getDefaultWallpaperWidth(): width = " + width);					
			} catch (OutOfMemoryError e) {
				Slog.w(TAG, "getDefaultWallpaperWidth(): Can't decode res:", e);
			}
	
			return width;
		}
		//#endif /* VENDOR_EDIT */
	
		//#ifdef VENDOR_EDIT
		//gaoliang@Plf.DesktopApp.Wallpaper, 2013/03/19, Add for bug223066 to change the 
		//wallpaper dimension between multiScreen and single screen.
		public boolean isScreenLarge(Context context) {
			int screenSize = context.getResources().getConfiguration().screenLayout &
					Configuration.SCREENLAYOUT_SIZE_MASK;
			boolean isScreenLarge = screenSize == Configuration.SCREENLAYOUT_SIZE_LARGE ||
				screenSize == Configuration.SCREENLAYOUT_SIZE_XLARGE;	
		
			if (DEBUG) {
				Slog.v(TAG, "isScreenLarge(): isScreenLarge = " + isScreenLarge);
			}
		
			return isScreenLarge;	 
		}
		//#endif /* VENDOR_EDIT */
	
	
		//#ifdef VENDOR_EDIT
		//gaoliang@Plf.DesktopApp.Wallpaper, 2013/03/19, Add for bug223066
		// As a ratio of screen height, the total distance we want the parallax effect to span
		// horizontally 	 	 
		public float wallpaperTravelToScreenWidthRatio(int width, int height) {
			float aspectRatio = width / (float) height;
	
			// At an aspect ratio of 16/10, the wallpaper parallax effect should span 1.5 * screen width
			// At an aspect ratio of 10/16, the wallpaper parallax effect should span 1.2 * screen width
			// We will use these two data points to extrapolate how much the wallpaper parallax effect
			// to span (ie travel) at any aspect ratio:
	
			final float ASPECT_RATIO_LANDSCAPE = 16/10f;
			final float ASPECT_RATIO_PORTRAIT = 10/16f;
			final float WALLPAPER_WIDTH_TO_SCREEN_RATIO_LANDSCAPE = 1.5f;
			final float WALLPAPER_WIDTH_TO_SCREEN_RATIO_PORTRAIT = 1.2f;
	
			// To find out the desired width at different aspect ratios, we use the following two
			// formulas, where the coefficient on x is the aspect ratio (width/height):
			//	 (16/10)x + y = 1.5
			//	 (10/16)x + y = 1.2
			// We solve for x and y and end up with a final formula:
			final float x =
				(WALLPAPER_WIDTH_TO_SCREEN_RATIO_LANDSCAPE - WALLPAPER_WIDTH_TO_SCREEN_RATIO_PORTRAIT) /
				(ASPECT_RATIO_LANDSCAPE - ASPECT_RATIO_PORTRAIT);
			final float y = WALLPAPER_WIDTH_TO_SCREEN_RATIO_PORTRAIT - x * ASPECT_RATIO_PORTRAIT;
			return x * aspectRatio + y;
		}
		//#endif /* VENDOR_EDIT */
		
		//#ifdef VENDOR_EDIT
		//gaoliang@Plf.DesktopApp.Wallpaper, 2013/03/19, Add for bug223066
		//set the new wallpaper dimension, if use setDimensionHints, the wallpaper will
		//change two times from single wallpaper to normal wallpaper 
		public void setDimensionHints_extra(int width, int height) throws RemoteException {
			checkPermission(android.Manifest.permission.SET_WALLPAPER_HINTS);
			synchronized (mLock) {
				int userId = UserHandle.getCallingUserId();
				WallpaperData wallpaper = mWallpaperMap.get(userId);
				if (wallpaper == null) {
					throw new IllegalStateException("Wallpaper not yet initialized for user " + userId);
				}
				if (width <= 0 || height <= 0) {
					throw new IllegalArgumentException("width and height must be > 0");
				}
		
				if (width != wallpaper.width || height != wallpaper.height) {
					wallpaper.width = width;
					wallpaper.height = height;
					saveSettingsLocked(wallpaper);			   
				}
				
			}
		}
		//#endif /* VENDOR_EDIT */
	
		//#ifdef VENDOR_EDIT
		//gaoliang@Plf.DesktopApp.Wallpaper, 2013/03/19, Add for bug223066 to get 
		//the current Image wallpaper bitmap 
		private void getCurrentImageWallpaperInfo(Point bmpInfo) {
			Bitmap bm = null;
	
			Bundle params = new Bundle();
			IWallpaperManagerCallback wpmcb = new IWallpaperManagerCallback.Stub() {
				public void onWallpaperChanged() {
				}
			};
			
			ParcelFileDescriptor fd = getWallpaper(wpmcb, params);
			if (DEBUG) {
				Slog.v(TAG, "getCurrentImageWallpaperInfo(): fd = " + fd);
			}			
			if (fd != null) {
				try {
					BitmapFactory.Options options = new BitmapFactory.Options();	 
					options.inJustDecodeBounds = true;
					bm = BitmapFactory.decodeFileDescriptor(
							fd.getFileDescriptor(), null, options);
					bmpInfo.x = options.outWidth;
					bmpInfo.y = options.outHeight;
				} catch (OutOfMemoryError e) {
					Slog.w(TAG, "getCurrentImageWallpaperInfo(): Can't decode file", e);
				} finally {
					try {
						fd.close();
					} catch (IOException e) {
						// Ignore
					}
				}
			}
		} 
		//#endif /* VENDOR_EDIT */
		
		//#ifdef VENDOR_EDIT
		//gaoliang@Plf.DesktopApp.Wallpaper, 2013/03/19, Add for bug223066 to check 
		//wheather the bitmap is single wallpaper, then change the wallpaper width and height.
		private void checkSingleWallpaper() {
			WallpaperInfo wpi = getWallpaperInfo();
			if (DEBUG) {
				Slog.d(TAG, "wpi = " + wpi);
			}			 
			if (null == wpi) { //means the current wallpaper is the image wallpaper
				try {
					int desiredMinimumWidth = getWidthHint();
					int dimWidth, dimHeight;
					
					android.util.DisplayMetrics displayMetrics = new android.util.DisplayMetrics();
					WindowManager wm = (WindowManager)mContext.getSystemService(Context.WINDOW_SERVICE);		  
					wm.getDefaultDisplay().getMetrics(displayMetrics);
					int maxDim = 
						Math.max(displayMetrics.widthPixels, displayMetrics.heightPixels);
					int minDim = 
						Math.min(displayMetrics.widthPixels, displayMetrics.heightPixels);
						
					if (DEBUG) {
						Slog.d(TAG, "desiredMinimumWidth = " + desiredMinimumWidth
							+ ", displayMetrics.widthPixels = " + displayMetrics.widthPixels
							+ ", displayMetrics.heightPixels = " + displayMetrics.heightPixels);
					}
									
					dimWidth = minDim;
					dimHeight = maxDim;
	
					Point bitmapSize = new Point(-1, -1);
					getCurrentImageWallpaperInfo(bitmapSize); 
					Slog.d("wallpaperDebug", "new bitmap width = "+bitmapSize.x);
					Slog.d("wallpaperDebug", "new bitmap height = "+bitmapSize.y);
					if (bitmapSize.x > 0) {  
						int bmWidth = bitmapSize.x;
						int bmHeight = bitmapSize.y;
						
						if (DEBUG) {
							Slog.d(TAG, "bmWidth = " + bmWidth 
								+ ", bmHeight = " + bmHeight);
						}
	
						int ratio = 1; 
						if (bmHeight < maxDim) {
							ratio = maxDim / bmHeight;
						}
						
						if (ratio*bmWidth <= minDim
								&& desiredMinimumWidth != minDim) { 								
							setDimensionHints_extra(dimWidth, dimHeight);
						} else if (ratio*bmWidth > minDim
								&& desiredMinimumWidth <= minDim) {
							if (isScreenLarge(mContext)) {
								dimWidth = (int)(maxDim * wallpaperTravelToScreenWidthRatio(maxDim, minDim));
							} else {
								dimWidth = Math.max((int)(minDim * WALLPAPER_SCREENS_SPAN), maxDim);
							}
							
							setDimensionHints_extra(dimWidth, dimHeight);
						}
					} else {
						if (-1 != mWidthOfDefaultWallpaper 
								&&desiredMinimumWidth != mWidthOfDefaultWallpaper) {
							dimWidth = mWidthOfDefaultWallpaper;
							setDimensionHints_extra(dimWidth, dimHeight);							
						}
					}
					bitmapSize = null;
				} catch (RemoteException e) {
	
				}
			}
		}
		//#endif /* VENDOR_EDIT */	
	}
	@android.annotation.OppoHook(level=android.annotation.OppoHook.OppoHookType.NEW_FIELD,
								property=android.annotation.OppoHook.OppoRomType.ROM,
								note="gaoliang add for oppo-wallpaper")
	private WallpaperHelper mWallpaperHelper = null;

	
    final Object mLock = new Object[0];

    /**
     * Minimum time between crashes of a wallpaper service for us to consider
     * restarting it vs. just reverting to the static wallpaper.
     */
    static final long MIN_WALLPAPER_CRASH_TIME = 10000;
    static final String WALLPAPER = "wallpaper";
    static final String WALLPAPER_INFO = "wallpaper_info.xml";

    /**
     * Name of the component used to display bitmap wallpapers from either the gallery or
     * built-in wallpapers.
     */
    static final ComponentName IMAGE_WALLPAPER = new ComponentName("com.android.systemui",
            "com.android.systemui.ImageWallpaper");

    /**
     * Observes the wallpaper for changes and notifies all IWallpaperServiceCallbacks
     * that the wallpaper has changed. The CREATE is triggered when there is no
     * wallpaper set and is created for the first time. The CLOSE_WRITE is triggered
     * everytime the wallpaper is changed.
     */
    private class WallpaperObserver extends FileObserver {

        final WallpaperData mWallpaper;
        final File mWallpaperDir;
        final File mWallpaperFile;

        public WallpaperObserver(WallpaperData wallpaper) {
            super(getWallpaperDir(wallpaper.userId).getAbsolutePath(),
                    CLOSE_WRITE | DELETE | DELETE_SELF);
            mWallpaperDir = getWallpaperDir(wallpaper.userId);
            mWallpaper = wallpaper;
            mWallpaperFile = new File(mWallpaperDir, WALLPAPER);
        }

		@android.annotation.OppoHook(level=android.annotation.OppoHook.OppoHookType.CHANGE_CODE,
								property=android.annotation.OppoHook.OppoRomType.ROM,
								note="gaoliang add for oppo-wallpaper")
        @Override
        public void onEvent(int event, String path) {
            if (path == null) {
                return;
            }
            synchronized (mLock) {
                // changing the wallpaper means we'll need to back up the new one
                long origId = Binder.clearCallingIdentity();
                BackupManager bm = new BackupManager(mContext);
                bm.dataChanged();
                Binder.restoreCallingIdentity(origId);

                File changedFile = new File(mWallpaperDir, path);
                if (mWallpaperFile.equals(changedFile)) {          
					//#ifdef VENDOR_EDIT
					//@#OppoHook
					//gaoliang@Plf.DesktopApp.Wallpaper, 2012/07/22, delete for desk wallpaper mark			
					//notifyCallbacksLocked(mWallpaper);
					//#endif /* VENDOR_EDIT */					
                    if (mWallpaper.wallpaperComponent == null || event != CLOSE_WRITE
                            || mWallpaper.imageWallpaperPending) {
                        if (event == CLOSE_WRITE) {
                            mWallpaper.imageWallpaperPending = false;
                        }
                    	//#ifdef VENDOR_EDIT
						//@#OppoHook
						//gaoliang@Plf.DesktopApp.Wallpaper, 2012/03/19, Add for bug223066
						mWallpaperHelper.checkSingleWallpaper();
						//#endif /* VENDOR_EDIT */
                        bindWallpaperComponentLocked(IMAGE_WALLPAPER, true,
                                false, mWallpaper, null);
                        saveSettingsLocked(mWallpaper);
                    }
                }
            }
        }
    }

    final Context mContext;
    final IWindowManager mIWindowManager;
    final IPackageManager mIPackageManager;
    final MyPackageMonitor mMonitor;
    WallpaperData mLastWallpaper;

    SparseArray<WallpaperData> mWallpaperMap = new SparseArray<WallpaperData>();

    int mCurrentUserId;

    static class WallpaperData {

        int userId;

        File wallpaperFile;

        /**
         * Client is currently writing a new image wallpaper.
         */
        boolean imageWallpaperPending;

        /**
         * Resource name if using a picture from the wallpaper gallery
         */
        String name = "";

        /**
         * The component name of the currently set live wallpaper.
         */
        ComponentName wallpaperComponent;

        /**
         * The component name of the wallpaper that should be set next.
         */
        ComponentName nextWallpaperComponent;

        WallpaperConnection connection;
        long lastDiedTime;
        boolean wallpaperUpdating;
        WallpaperObserver wallpaperObserver;

        /**
         * List of callbacks registered they should each be notified when the wallpaper is changed.
         */
        private RemoteCallbackList<IWallpaperManagerCallback> callbacks
                = new RemoteCallbackList<IWallpaperManagerCallback>();

        int width = -1;
        int height = -1;

        WallpaperData(int userId) {
            this.userId = userId;
            wallpaperFile = new File(getWallpaperDir(userId), WALLPAPER);
        }
    }

    class WallpaperConnection extends IWallpaperConnection.Stub
            implements ServiceConnection {
        final WallpaperInfo mInfo;
        final Binder mToken = new Binder();
        IWallpaperService mService;
        IWallpaperEngine mEngine;
        WallpaperData mWallpaper;
        IRemoteCallback mReply;

        public WallpaperConnection(WallpaperInfo info, WallpaperData wallpaper) {
            mInfo = info;
            mWallpaper = wallpaper;
        }

        @Override
        public void onServiceConnected(ComponentName name, IBinder service) {
            synchronized (mLock) {
                if (mWallpaper.connection == this) {
                    mWallpaper.lastDiedTime = SystemClock.uptimeMillis();
                    mService = IWallpaperService.Stub.asInterface(service);
                    attachServiceLocked(this, mWallpaper);
                    // XXX should probably do saveSettingsLocked() later
                    // when we have an engine, but I'm not sure about
                    // locking there and anyway we always need to be able to
                    // recover if there is something wrong.
                    saveSettingsLocked(mWallpaper);
                    /// M: when save wallpaper Settings, save Settings' wallpaper name. @{
                    updateSettingsComponentName();
                    /// M: @}
                }
            }
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            synchronized (mLock) {
                mService = null;
                mEngine = null;
                if (mWallpaper.connection == this) {
                    Slog.w(TAG, "Wallpaper service gone: " + mWallpaper.wallpaperComponent);
                    if (!mWallpaper.wallpaperUpdating
                            && (mWallpaper.lastDiedTime + MIN_WALLPAPER_CRASH_TIME)
                                > SystemClock.uptimeMillis()
                            && mWallpaper.userId == mCurrentUserId) {
                        Slog.w(TAG, "Reverting to built-in wallpaper!");
                        clearWallpaperLocked(true, mWallpaper.userId, null);
                    }
                }
            }
        }

		@android.annotation.OppoHook(level=android.annotation.OppoHook.OppoHookType.CHANGE_CODE,
								property=android.annotation.OppoHook.OppoRomType.ROM,
								note="gaoliang add for oppo-wallpaper")
        @Override
        public void attachEngine(IWallpaperEngine engine) {
            synchronized (mLock) {
                mEngine = engine;
				//#ifndef VENDOR_EDIT
				//#@OppoHook
				//gaoliang@Plf.DesktopApp.Wallpaper, 2013/07/22, Modify for sending broadcast for once when set wallpaper
			   	notifyCallbacksLocked(mWallpaper);  
				//#endif /* VENDOR_EDIT */
            }
        }

        @Override
        public void engineShown(IWallpaperEngine engine) {
            synchronized (mLock) {
                if (mReply != null) {
                    long ident = Binder.clearCallingIdentity();
                    try {
                        mReply.sendResult(null);
                    } catch (RemoteException e) {
                        Binder.restoreCallingIdentity(ident);
                    }
                    mReply = null;
                }
            }
        }

        @Override
        public ParcelFileDescriptor setWallpaper(String name) {
            synchronized (mLock) {
                if (mWallpaper.connection == this) {
                    return updateWallpaperBitmapLocked(name, mWallpaper);
                }
                return null;
            }
        }
    }

    class MyPackageMonitor extends PackageMonitor {
        @Override
        public void onPackageUpdateFinished(String packageName, int uid) {
            synchronized (mLock) {
                if (mCurrentUserId != getChangingUserId()) {
                    return;
                }
                WallpaperData wallpaper = mWallpaperMap.get(mCurrentUserId);
                if (wallpaper != null) {
                    if (wallpaper.wallpaperComponent != null
                            && wallpaper.wallpaperComponent.getPackageName().equals(packageName)) {
                        wallpaper.wallpaperUpdating = false;
                        ComponentName comp = wallpaper.wallpaperComponent;
                        clearWallpaperComponentLocked(wallpaper);
                        if (!bindWallpaperComponentLocked(comp, false, false,
                                wallpaper, null)) {
                            Slog.w(TAG, "Wallpaper no longer available; reverting to default");
                            clearWallpaperLocked(false, wallpaper.userId, null);
                        }
                    }
                }
            }
        }

        @Override
        public void onPackageModified(String packageName) {
            synchronized (mLock) {
                if (mCurrentUserId != getChangingUserId()) {
                    return;
                }
                WallpaperData wallpaper = mWallpaperMap.get(mCurrentUserId);
                if (wallpaper != null) {
                    if (wallpaper.wallpaperComponent == null
                            || !wallpaper.wallpaperComponent.getPackageName().equals(packageName)) {
                        return;
                    }
                    doPackagesChangedLocked(true, wallpaper);
                }
            }
        }

        @Override
        public void onPackageUpdateStarted(String packageName, int uid) {
            synchronized (mLock) {
                if (mCurrentUserId != getChangingUserId()) {
                    return;
                }
                WallpaperData wallpaper = mWallpaperMap.get(mCurrentUserId);
                if (wallpaper != null) {
                    if (wallpaper.wallpaperComponent != null
                            && wallpaper.wallpaperComponent.getPackageName().equals(packageName)) {
                        wallpaper.wallpaperUpdating = true;
                    }
                }
            }
        }

        @Override
        public boolean onHandleForceStop(Intent intent, String[] packages, int uid, boolean doit) {
            synchronized (mLock) {
                boolean changed = false;
                if (mCurrentUserId != getChangingUserId()) {
                    return false;
                }
                WallpaperData wallpaper = mWallpaperMap.get(mCurrentUserId);
                if (wallpaper != null) {
                    boolean res = doPackagesChangedLocked(doit, wallpaper);
                    changed |= res;
                }
                return changed;
            }
        }

        @Override
        public void onSomePackagesChanged() {
            synchronized (mLock) {
                if (mCurrentUserId != getChangingUserId()) {
                    return;
                }
                WallpaperData wallpaper = mWallpaperMap.get(mCurrentUserId);
                if (wallpaper != null) {
                    doPackagesChangedLocked(true, wallpaper);
                }
            }
        }

        boolean doPackagesChangedLocked(boolean doit, WallpaperData wallpaper) {
            boolean changed = false;
            if (wallpaper.wallpaperComponent != null) {
                int change = isPackageDisappearing(wallpaper.wallpaperComponent
                        .getPackageName());
                if (change == PACKAGE_PERMANENT_CHANGE
                        || change == PACKAGE_TEMPORARY_CHANGE) {
                    changed = true;
                    if (doit) {
                        Slog.w(TAG, "Wallpaper uninstalled, removing: "
                                + wallpaper.wallpaperComponent);
                        clearWallpaperLocked(false, wallpaper.userId, null);
                    }
                }
            }
            if (wallpaper.nextWallpaperComponent != null) {
                int change = isPackageDisappearing(wallpaper.nextWallpaperComponent
                        .getPackageName());
                if (change == PACKAGE_PERMANENT_CHANGE
                        || change == PACKAGE_TEMPORARY_CHANGE) {
                    wallpaper.nextWallpaperComponent = null;
                }
            }
            if (wallpaper.wallpaperComponent != null
                    && isPackageModified(wallpaper.wallpaperComponent.getPackageName())) {
                try {
                    mContext.getPackageManager().getServiceInfo(
                            wallpaper.wallpaperComponent, 0);
                } catch (NameNotFoundException e) {
                    Slog.w(TAG, "Wallpaper component gone, removing: "
                            + wallpaper.wallpaperComponent);
                    clearWallpaperLocked(false, wallpaper.userId, null);
                }
            }
            if (wallpaper.nextWallpaperComponent != null
                    && isPackageModified(wallpaper.nextWallpaperComponent.getPackageName())) {
                try {
                    mContext.getPackageManager().getServiceInfo(
                            wallpaper.nextWallpaperComponent, 0);
                } catch (NameNotFoundException e) {
                    wallpaper.nextWallpaperComponent = null;
                }
            }
            return changed;
        }
    }
    
	@android.annotation.OppoHook(level=android.annotation.OppoHook.OppoHookType.CHANGE_CODE,
								property=android.annotation.OppoHook.OppoRomType.ROM,
								note="gaoliang add for oppo-wallpaper")
    public WallpaperManagerService(Context context) {
        if (DEBUG) Slog.v(TAG, "WallpaperService startup");
        mContext = context;
        mIWindowManager = IWindowManager.Stub.asInterface(
                ServiceManager.getService(Context.WINDOW_SERVICE));
        mIPackageManager = AppGlobals.getPackageManager();
        mMonitor = new MyPackageMonitor();
        mMonitor.register(context, null, UserHandle.ALL, true);
        getWallpaperDir(UserHandle.USER_OWNER).mkdirs();
		//#ifdef VENDOR_EDIT
		//#OppoHook
		//gaoliang@Plf.DesktopApp.Wallpaper, 2012/08/27, Add for getting the width of default wallpaper
		mWallpaperHelper = new WallpaperHelper(mContext);
		//#endif /* VENDOR_EDIT */ 			
        loadSettingsLocked(UserHandle.USER_OWNER);
    }
    
    private static File getWallpaperDir(int userId) {
        return Environment.getUserSystemDirectory(userId);
    }

    @Override
    protected void finalize() throws Throwable {
        super.finalize();
        for (int i = 0; i < mWallpaperMap.size(); i++) {
            WallpaperData wallpaper = mWallpaperMap.valueAt(i);
            wallpaper.wallpaperObserver.stopWatching();
        }
    }

    public void systemReady() {
        if (DEBUG) Slog.v(TAG, "systemReady");
        WallpaperData wallpaper = mWallpaperMap.get(UserHandle.USER_OWNER);
        switchWallpaper(wallpaper, null);
        wallpaper.wallpaperObserver = new WallpaperObserver(wallpaper);
        wallpaper.wallpaperObserver.startWatching();

        IntentFilter userFilter = new IntentFilter();
        userFilter.addAction(Intent.ACTION_USER_REMOVED);
        userFilter.addAction(Intent.ACTION_USER_STOPPING);
        mContext.registerReceiver(new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                String action = intent.getAction();
                if (Intent.ACTION_USER_REMOVED.equals(action)) {
                    onRemoveUser(intent.getIntExtra(Intent.EXTRA_USER_HANDLE,
                            UserHandle.USER_NULL));
                }
                // TODO: Race condition causing problems when cleaning up on stopping a user.
                // Comment this out for now.
                // else if (Intent.ACTION_USER_STOPPING.equals(action)) {
                //     onStoppingUser(intent.getIntExtra(Intent.EXTRA_USER_HANDLE,
                //             UserHandle.USER_NULL));
                // }
            }
        }, userFilter);

        try {
            ActivityManagerNative.getDefault().registerUserSwitchObserver(
                    new IUserSwitchObserver.Stub() {
                        @Override
                        public void onUserSwitching(int newUserId, IRemoteCallback reply) {
                            switchUser(newUserId, reply);
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

    String getName() {
        synchronized (mLock) {
            return mWallpaperMap.get(0).name;
        }
    }

    void onStoppingUser(int userId) {
        if (userId < 1) return;
        synchronized (mLock) {
            WallpaperData wallpaper = mWallpaperMap.get(userId);
            if (wallpaper != null) {
                if (wallpaper.wallpaperObserver != null) {
                    wallpaper.wallpaperObserver.stopWatching();
                    wallpaper.wallpaperObserver = null;
                }
                mWallpaperMap.remove(userId);
            }
        }
    }

    void onRemoveUser(int userId) {
        if (userId < 1) return;
        synchronized (mLock) {
            onStoppingUser(userId);
            File wallpaperFile = new File(getWallpaperDir(userId), WALLPAPER);
            wallpaperFile.delete();
            File wallpaperInfoFile = new File(getWallpaperDir(userId), WALLPAPER_INFO);
            wallpaperInfoFile.delete();
        }
    }

    void switchUser(int userId, IRemoteCallback reply) {
        synchronized (mLock) {
            mCurrentUserId = userId;
            WallpaperData wallpaper = mWallpaperMap.get(userId);
            if (wallpaper == null) {
                wallpaper = new WallpaperData(userId);
                mWallpaperMap.put(userId, wallpaper);
                loadSettingsLocked(userId);
            }
            // Not started watching yet, in case wallpaper data was loaded for other reasons.
            if (wallpaper.wallpaperObserver == null) {
                wallpaper.wallpaperObserver = new WallpaperObserver(wallpaper);
                wallpaper.wallpaperObserver.startWatching();
            }
            switchWallpaper(wallpaper, reply);
        }
    }

    void switchWallpaper(WallpaperData wallpaper, IRemoteCallback reply) {
        synchronized (mLock) {
            RuntimeException e = null;
            try {
                ComponentName cname = wallpaper.wallpaperComponent != null ?
                        wallpaper.wallpaperComponent : wallpaper.nextWallpaperComponent;
                if (bindWallpaperComponentLocked(cname, true, false, wallpaper, reply)) {
                    return;
                }
            } catch (RuntimeException e1) {
                e = e1;
            }
            Slog.w(TAG, "Failure starting previous wallpaper", e);
            clearWallpaperLocked(false, wallpaper.userId, reply);
        }
    }

    public void clearWallpaper() {
        if (DEBUG) Slog.v(TAG, "clearWallpaper");
        synchronized (mLock) {
            clearWallpaperLocked(false, UserHandle.getCallingUserId(), null);
        }
    }

    void clearWallpaperLocked(boolean defaultFailed, int userId, IRemoteCallback reply) {
        WallpaperData wallpaper = mWallpaperMap.get(userId);
        File f = new File(getWallpaperDir(userId), WALLPAPER);
        if (f.exists()) {
            f.delete();
        }
        final long ident = Binder.clearCallingIdentity();
        RuntimeException e = null;
        try {
            wallpaper.imageWallpaperPending = false;
            if (userId != mCurrentUserId) return;
            if (bindWallpaperComponentLocked(defaultFailed
                    ? IMAGE_WALLPAPER
                    : null, true, false, wallpaper, reply)) {
                return;
            }
        } catch (IllegalArgumentException e1) {
            e = e1;
        } finally {
            Binder.restoreCallingIdentity(ident);
        }
        
        // This can happen if the default wallpaper component doesn't
        // exist.  This should be a system configuration problem, but
        // let's not let it crash the system and just live with no
        // wallpaper.
        Slog.e(TAG, "Default wallpaper component not found!", e);
        clearWallpaperComponentLocked(wallpaper);
        if (reply != null) {
            try {
                reply.sendResult(null);
            } catch (RemoteException e1) {
            }
        }
    }

    public boolean hasNamedWallpaper(String name) {
        synchronized (mLock) {
            List<UserInfo> users;
            long ident = Binder.clearCallingIdentity();
            try {
                users = ((UserManager) mContext.getSystemService(Context.USER_SERVICE)).getUsers();
            } finally {
                Binder.restoreCallingIdentity(ident);
            }
            for (UserInfo user: users) {
                WallpaperData wd = mWallpaperMap.get(user.id);
                if (wd == null) {
                    // User hasn't started yet, so load her settings to peek at the wallpaper
                    loadSettingsLocked(user.id);
                    wd = mWallpaperMap.get(user.id);
                }
                if (wd != null && name.equals(wd.name)) {
                    return true;
                }
            }
        }
        return false;
    }

    public void setDimensionHints(int width, int height) throws RemoteException {
        checkPermission(android.Manifest.permission.SET_WALLPAPER_HINTS);
        synchronized (mLock) {
            int userId = UserHandle.getCallingUserId();
            WallpaperData wallpaper = mWallpaperMap.get(userId);
            if (wallpaper == null) {
                throw new IllegalStateException("Wallpaper not yet initialized for user " + userId);
            }
            if (width <= 0 || height <= 0) {
                throw new IllegalArgumentException("width and height must be > 0");
            }

            if (width != wallpaper.width || height != wallpaper.height) {
                wallpaper.width = width;
                wallpaper.height = height;
                saveSettingsLocked(wallpaper);
                if (mCurrentUserId != userId) return; // Don't change the properties now
                if (wallpaper.connection != null) {
                    if (wallpaper.connection.mEngine != null) {
                        try {
                            wallpaper.connection.mEngine.setDesiredSize(
                                    width, height);
                        } catch (RemoteException e) {
                        }
                        notifyCallbacksLocked(wallpaper);
                    }
                }
            }
        }
    }

    public int getWidthHint() throws RemoteException {
        synchronized (mLock) {
            WallpaperData wallpaper = mWallpaperMap.get(UserHandle.getCallingUserId());
            return wallpaper.width;
        }
    }

    public int getHeightHint() throws RemoteException {
        synchronized (mLock) {
            WallpaperData wallpaper = mWallpaperMap.get(UserHandle.getCallingUserId());
            return wallpaper.height;
        }
    }

    public ParcelFileDescriptor getWallpaper(IWallpaperManagerCallback cb,
            Bundle outParams) {
        synchronized (mLock) {
            // This returns the current user's wallpaper, if called by a system service. Else it
            // returns the wallpaper for the calling user.
            int callingUid = Binder.getCallingUid();
            int wallpaperUserId = 0;
            if (callingUid == android.os.Process.SYSTEM_UID) {
                wallpaperUserId = mCurrentUserId;
            } else {
                wallpaperUserId = UserHandle.getUserId(callingUid);
            }
            WallpaperData wallpaper = mWallpaperMap.get(wallpaperUserId);
            try {
                if (outParams != null) {
                    outParams.putInt("width", wallpaper.width);
                    outParams.putInt("height", wallpaper.height);
                }
                wallpaper.callbacks.register(cb);
                File f = new File(getWallpaperDir(wallpaperUserId), WALLPAPER);
                if (!f.exists()) {
                    return null;
                }
                return ParcelFileDescriptor.open(f, MODE_READ_ONLY);
            } catch (FileNotFoundException e) {
                /* Shouldn't happen as we check to see if the file exists */
                Slog.w(TAG, "Error getting wallpaper", e);
            }
            return null;
        }
    }

    public WallpaperInfo getWallpaperInfo() {
        int userId = UserHandle.getCallingUserId();
        synchronized (mLock) {
            WallpaperData wallpaper = mWallpaperMap.get(userId);
            if (wallpaper.connection != null) {
                return wallpaper.connection.mInfo;
            }
            return null;
        }
    }

    public ParcelFileDescriptor setWallpaper(String name) {
        checkPermission(android.Manifest.permission.SET_WALLPAPER);
        synchronized (mLock) {
            if (DEBUG) Slog.v(TAG, "setWallpaper");
            int userId = UserHandle.getCallingUserId();
            WallpaperData wallpaper = mWallpaperMap.get(userId);
            if (wallpaper == null) {
                throw new IllegalStateException("Wallpaper not yet initialized for user " + userId);
            }
            final long ident = Binder.clearCallingIdentity();
            try {
                ParcelFileDescriptor pfd = updateWallpaperBitmapLocked(name, wallpaper);
                if (pfd != null) {
                    wallpaper.imageWallpaperPending = true;
                }
                return pfd;
            } finally {
                Binder.restoreCallingIdentity(ident);
            }
        }
    }

    ParcelFileDescriptor updateWallpaperBitmapLocked(String name, WallpaperData wallpaper) {
        if (name == null) name = "";
        try {
            File dir = getWallpaperDir(wallpaper.userId);
            if (!dir.exists()) {
                /** M: fix the bug that ThemeManager couldn't update wallpaper after clear Setting's data. @{ */
                Xlog.w(TAG, "WALLPAPER_DIR doesn't exist, recreate and start watching it");
                wallpaper.wallpaperObserver.stopWatching();
                dir.mkdir();
                FileUtils.setPermissions(
                        dir.getPath(),
                        FileUtils.S_IRWXU|FileUtils.S_IRWXG|FileUtils.S_IXOTH,
                        -1, -1);
                wallpaper.wallpaperObserver.startWatching();
                /** @} */
            }
            File file = new File(dir, WALLPAPER);
            ParcelFileDescriptor fd = ParcelFileDescriptor.open(file,
                    MODE_CREATE|MODE_READ_WRITE);
            if (!SELinux.restorecon(file)) {
                return null;
            }
            wallpaper.name = name;
            return fd;
        } catch (FileNotFoundException e) {
            Slog.w(TAG, "Error setting wallpaper", e);
        }
        return null;
    }

    public void setWallpaperComponent(ComponentName name) {
        checkPermission(android.Manifest.permission.SET_WALLPAPER_COMPONENT);
        synchronized (mLock) {
            if (DEBUG) Slog.v(TAG, "setWallpaperComponent name=" + name);
            int userId = UserHandle.getCallingUserId();
            WallpaperData wallpaper = mWallpaperMap.get(userId);
            if (wallpaper == null) {
                throw new IllegalStateException("Wallpaper not yet initialized for user " + userId);
            }
            final long ident = Binder.clearCallingIdentity();
            try {
                wallpaper.imageWallpaperPending = false;
                bindWallpaperComponentLocked(name, false, true, wallpaper, null);
            } finally {
                Binder.restoreCallingIdentity(ident);
            }
        }
    }

	@android.annotation.OppoHook(level=android.annotation.OppoHook.OppoHookType.CHANGE_CODE,
							property=android.annotation.OppoHook.OppoRomType.ROM,
							note="gaoliang add for avoiding the problem that the system crash when systemUI is removed or replaced")	
    boolean bindWallpaperComponentLocked(ComponentName componentName, boolean force,
            boolean fromUser, WallpaperData wallpaper, IRemoteCallback reply) {
        if (DEBUG) Slog.v(TAG, "bindWallpaperComponentLocked: componentName=" + componentName);
        // Has the component changed?
        if (!force) {
            if (wallpaper.connection != null) {
                if (wallpaper.wallpaperComponent == null) {
                    if (componentName == null) {
                        if (DEBUG) Slog.v(TAG, "bindWallpaperComponentLocked: still using default");
                        // Still using default wallpaper.
                        return true;
                    }
                } else if (wallpaper.wallpaperComponent.equals(componentName)) {
                    // Changing to same wallpaper.
                    if (DEBUG) Slog.v(TAG, "same wallpaper");
                    return true;
                }
            }
        }
        
        try {
            if (componentName == null) {
                String defaultComponent = 
                    mContext.getString(com.android.internal.R.string.default_wallpaper_component);
                if (defaultComponent != null) {
                    // See if there is a default wallpaper component specified
                    componentName = ComponentName.unflattenFromString(defaultComponent);
                    if (DEBUG) Slog.v(TAG, "Use default component wallpaper:" + componentName);
                }
                if (componentName == null) {
                    // Fall back to static image wallpaper
                    componentName = IMAGE_WALLPAPER;
                    //clearWallpaperComponentLocked();
                    //return;
                    if (DEBUG) Slog.v(TAG, "Using image wallpaper");
                }
            }
            int serviceUserId = wallpaper.userId;
            ServiceInfo si = mIPackageManager.getServiceInfo(componentName,
                    PackageManager.GET_META_DATA | PackageManager.GET_PERMISSIONS, serviceUserId);
			//#ifndef VENDOR_EDIT
			//#@OppoHook
			//gaoliang@Plf.DesktopApp.Wallpaper, 2013/08/13, Modify for  bug324222
			if (null == si) {
				Slog.e(TAG, "SystemUi may be removed or replaced");
				return false;
			}
			//#endif /* VENDOR_EDIT */
            if (!android.Manifest.permission.BIND_WALLPAPER.equals(si.permission)) {
                String msg = "Selected service does not require "
                        + android.Manifest.permission.BIND_WALLPAPER
                        + ": " + componentName;
                if (fromUser) {
                    throw new SecurityException(msg);
                }
                Slog.w(TAG, msg);
                return false;
            }
            
            WallpaperInfo wi = null;
            
            Intent intent = new Intent(WallpaperService.SERVICE_INTERFACE);
            if (componentName != null && !componentName.equals(IMAGE_WALLPAPER)) {
                // Make sure the selected service is actually a wallpaper service.
                List<ResolveInfo> ris =
                        mIPackageManager.queryIntentServices(intent,
                                intent.resolveTypeIfNeeded(mContext.getContentResolver()),
                                PackageManager.GET_META_DATA, serviceUserId);
                for (int i=0; i<ris.size(); i++) {
                    ServiceInfo rsi = ris.get(i).serviceInfo;
                    if (rsi.name.equals(si.name) &&
                            rsi.packageName.equals(si.packageName)) {
                        try {
                            wi = new WallpaperInfo(mContext, ris.get(i));
                        } catch (XmlPullParserException e) {
                            if (fromUser) {
                                throw new IllegalArgumentException(e);
                            }
                            Slog.w(TAG, e);
                            return false;
                        } catch (IOException e) {
                            if (fromUser) {
                                throw new IllegalArgumentException(e);
                            }
                            Slog.w(TAG, e);
                            return false;
                        }
                        break;
                    }
                }
                if (wi == null) {
                    String msg = "Selected service is not a wallpaper: "
                            + componentName;
                    if (fromUser) {
                        throw new SecurityException(msg);
                    }
                    Slog.w(TAG, msg);
                    return false;
                }
            }
            
            // Bind the service!
            if (DEBUG) Slog.v(TAG, "Binding to:" + componentName);
            WallpaperConnection newConn = new WallpaperConnection(wi, wallpaper);
            intent.setComponent(componentName);
            intent.putExtra(Intent.EXTRA_CLIENT_LABEL,
                    com.android.internal.R.string.wallpaper_binding_label);
            intent.putExtra(Intent.EXTRA_CLIENT_INTENT, PendingIntent.getActivityAsUser(
                    mContext, 0,
                    Intent.createChooser(new Intent(Intent.ACTION_SET_WALLPAPER),
                            mContext.getText(com.android.internal.R.string.chooser_wallpaper)),
                    0, null, new UserHandle(serviceUserId)));
            if (!mContext.bindService(intent, newConn, Context.BIND_AUTO_CREATE, serviceUserId)) {
                String msg = "Unable to bind service: "
                        + componentName;
                if (fromUser) {
                    throw new IllegalArgumentException(msg);
                }
                Slog.w(TAG, msg);
                return false;
            }
            if (wallpaper.userId == mCurrentUserId && mLastWallpaper != null) {
                detachWallpaperLocked(mLastWallpaper);
            }
            wallpaper.wallpaperComponent = componentName;
            wallpaper.connection = newConn;
            wallpaper.lastDiedTime = SystemClock.uptimeMillis();
            newConn.mReply = reply;
            try {
                if (wallpaper.userId == mCurrentUserId) {
                    if (DEBUG)
                        Slog.v(TAG, "Adding window token: " + newConn.mToken);
                    mIWindowManager.addWindowToken(newConn.mToken,
                            WindowManager.LayoutParams.TYPE_WALLPAPER);
                    mLastWallpaper = wallpaper;
                }
            } catch (RemoteException e) {
            }
        } catch (RemoteException e) {
            String msg = "Remote exception for " + componentName + "\n" + e;
            if (fromUser) {
                throw new IllegalArgumentException(msg);
            }
            Slog.w(TAG, msg);
            return false;
        }
        return true;
    }

    void detachWallpaperLocked(WallpaperData wallpaper) {
        if (wallpaper.connection != null) {
            if (wallpaper.connection.mReply != null) {
                try {
                    wallpaper.connection.mReply.sendResult(null);
                } catch (RemoteException e) {
                }
                wallpaper.connection.mReply = null;
            }
            if (wallpaper.connection.mEngine != null) {
                try {
                    wallpaper.connection.mEngine.destroy();
                } catch (RemoteException e) {
                }
            }
            mContext.unbindService(wallpaper.connection);
            try {
                if (DEBUG)
                    Slog.v(TAG, "Removing window token: " + wallpaper.connection.mToken);
                mIWindowManager.removeWindowToken(wallpaper.connection.mToken);
            } catch (RemoteException e) {
            }
            wallpaper.connection.mService = null;
            wallpaper.connection.mEngine = null;
            wallpaper.connection = null;
        }
    }

    void clearWallpaperComponentLocked(WallpaperData wallpaper) {
        wallpaper.wallpaperComponent = null;
        detachWallpaperLocked(wallpaper);
    }

    void attachServiceLocked(WallpaperConnection conn, WallpaperData wallpaper) {
        try {
            conn.mService.attach(conn, conn.mToken,
                    WindowManager.LayoutParams.TYPE_WALLPAPER, false,
                    wallpaper.width, wallpaper.height);
        } catch (RemoteException e) {
            Slog.w(TAG, "Failed attaching wallpaper; clearing", e);
            if (!wallpaper.wallpaperUpdating) {
                bindWallpaperComponentLocked(null, false, false, wallpaper, null);
            }
        }
    }

    private void notifyCallbacksLocked(WallpaperData wallpaper) {
        final int n = wallpaper.callbacks.beginBroadcast();
        for (int i = 0; i < n; i++) {
            try {
                wallpaper.callbacks.getBroadcastItem(i).onWallpaperChanged();
            } catch (RemoteException e) {

                // The RemoteCallbackList will take care of removing
                // the dead object for us.
            }
        }
        wallpaper.callbacks.finishBroadcast();
        final Intent intent = new Intent(Intent.ACTION_WALLPAPER_CHANGED);
        mContext.sendBroadcastAsUser(intent, new UserHandle(mCurrentUserId));
    }

    private void checkPermission(String permission) {
        if (PackageManager.PERMISSION_GRANTED!= mContext.checkCallingOrSelfPermission(permission)) {
            throw new SecurityException("Access denied to process: " + Binder.getCallingPid()
                    + ", must have permission " + permission);
        }
    }

    private static JournaledFile makeJournaledFile(int userId) {
        final String base = new File(getWallpaperDir(userId), WALLPAPER_INFO).getAbsolutePath();
        return new JournaledFile(new File(base), new File(base + ".tmp"));
    }
    
    /**
     * M: update current wallpaper info (Default, Video Wallpaper, 
     * or such as Black Hole [For Live Wallpapers, show its name]) to Settings.
     * The method is just called when WpMS bind WallpaperService. @{
     */    
    private void updateSettingsComponentName() {
        PackageManager packageManager = mContext.getPackageManager();
        WallpaperData wallpaperData = null;
        WallpaperInfo wallpaperInfo = null;
        String currentWallpaperName = null;
        String packageName = mContext.getPackageName();
        int resId = com.mediatek.internal.R.string.default_wallpaper_name;
        int userId = UserHandle.getCallingUserId();
        synchronized (mLock) {
            wallpaperData = mWallpaperMap.get(userId);
            if (wallpaperData.connection != null) {
                wallpaperInfo = wallpaperData.connection.mInfo;
            }
        }
        if (wallpaperInfo != null && !IMAGE_WALLPAPER
                .equals(wallpaperData.wallpaperComponent)) {
            packageName = wallpaperInfo.getPackageName();
            resId = wallpaperInfo.getServiceInfo().labelRes;
        }
        currentWallpaperName = packageName + "&" + resId;
        Settings.System.putString(mContext.getContentResolver(),
                Settings.System.CURRENT_WALLPAPER_NAME, currentWallpaperName);
        // for test
        Xlog.w("WallpaperManagerService", "currentWallpaperName = " + currentWallpaperName,
                new Throwable());
    }
    /** M: @} */

    private void saveSettingsLocked(WallpaperData wallpaper) {

        JournaledFile journal = makeJournaledFile(wallpaper.userId);
        FileOutputStream stream = null;
        try {
            stream = new FileOutputStream(journal.chooseForWrite(), false);
            XmlSerializer out = new FastXmlSerializer();
            out.setOutput(stream, "utf-8");
            out.startDocument(null, true);

            out.startTag(null, "wp");
            out.attribute(null, "width", Integer.toString(wallpaper.width));
            out.attribute(null, "height", Integer.toString(wallpaper.height));
            out.attribute(null, "name", wallpaper.name);
            if (wallpaper.wallpaperComponent != null
                    && !wallpaper.wallpaperComponent.equals(IMAGE_WALLPAPER)) {
                out.attribute(null, "component",
                        wallpaper.wallpaperComponent.flattenToShortString());
            }
            out.endTag(null, "wp");

            out.endDocument();
            stream.close();
            journal.commit();
        } catch (IOException e) {
            try {
                if (stream != null) {
                    stream.close();
                }
            } catch (IOException ex) {
                // Ignore
            }
            journal.rollback();
        }
    }

    private void migrateFromOld() {
        File oldWallpaper = new File(WallpaperBackupHelper.WALLPAPER_IMAGE_KEY);
        File oldInfo = new File(WallpaperBackupHelper.WALLPAPER_INFO_KEY);
        if (oldWallpaper.exists()) {
            File newWallpaper = new File(getWallpaperDir(0), WALLPAPER);
            oldWallpaper.renameTo(newWallpaper);
        }
        if (oldInfo.exists()) {
            File newInfo = new File(getWallpaperDir(0), WALLPAPER_INFO);
            oldInfo.renameTo(newInfo);
        }
    }

	@android.annotation.OppoHook(level=android.annotation.OppoHook.OppoHookType.CHANGE_CODE,
								property=android.annotation.OppoHook.OppoRomType.ROM,
								note="gaoliang add for oppo-wallpaper")
    private void loadSettingsLocked(int userId) {
        if (DEBUG) Slog.v(TAG, "loadSettingsLocked");
        
        JournaledFile journal = makeJournaledFile(userId);
        FileInputStream stream = null;
        File file = journal.chooseForRead();
        if (!file.exists()) {
            // This should only happen one time, when upgrading from a legacy system
            migrateFromOld();
        }
        WallpaperData wallpaper = mWallpaperMap.get(userId);
        if (wallpaper == null) {
            wallpaper = new WallpaperData(userId);
            mWallpaperMap.put(userId, wallpaper);
        }
        boolean success = false;
        try {
            stream = new FileInputStream(file);
            XmlPullParser parser = Xml.newPullParser();
            parser.setInput(stream, null);

            int type;
            do {
                type = parser.next();
                if (type == XmlPullParser.START_TAG) {
                    String tag = parser.getName();
                    if ("wp".equals(tag)) {
                        wallpaper.width = Integer.parseInt(parser.getAttributeValue(null, "width"));
                        wallpaper.height = Integer.parseInt(parser
                                .getAttributeValue(null, "height"));
                        wallpaper.name = parser.getAttributeValue(null, "name");
                        String comp = parser.getAttributeValue(null, "component");
                        wallpaper.nextWallpaperComponent = comp != null
                                ? ComponentName.unflattenFromString(comp)
                                : null;
                        if (wallpaper.nextWallpaperComponent == null
                                || "android".equals(wallpaper.nextWallpaperComponent
                                        .getPackageName())) {
                            wallpaper.nextWallpaperComponent = IMAGE_WALLPAPER;
                        }
                          
                        if (DEBUG) {
                            Slog.v(TAG, "mWidth:" + wallpaper.width);
                            Slog.v(TAG, "mHeight:" + wallpaper.height);
                            Slog.v(TAG, "mName:" + wallpaper.name);
                            Slog.v(TAG, "mNextWallpaperComponent:"
                                    + wallpaper.nextWallpaperComponent);
                        }
                    }
                }
            } while (type != XmlPullParser.END_DOCUMENT);
            success = true;
        } catch (FileNotFoundException e) {
            Slog.w(TAG, "no current wallpaper -- first boot?");
        } catch (NullPointerException e) {
            Slog.w(TAG, "failed parsing " + file + " " + e);
        } catch (NumberFormatException e) {
            Slog.w(TAG, "failed parsing " + file + " " + e);
        } catch (XmlPullParserException e) {
            Slog.w(TAG, "failed parsing " + file + " " + e);
        } catch (IOException e) {
            Slog.w(TAG, "failed parsing " + file + " " + e);
        } catch (IndexOutOfBoundsException e) {
            Slog.w(TAG, "failed parsing " + file + " " + e);
        }
        try {
            if (stream != null) {
                stream.close();
            }
        } catch (IOException e) {
            // Ignore
        }

        if (!success) {
            wallpaper.width = -1;
            wallpaper.height = -1;
            wallpaper.name = "";
        }

        // We always want to have some reasonable width hint.
        WindowManager wm = (WindowManager)mContext.getSystemService(Context.WINDOW_SERVICE);
        Display d = wm.getDefaultDisplay();

        //#ifndef VENDOR_EDIT
        //gaoliang@Plf.DesktopApp.Wallpaper, 2012/08/22, Modify for resetting mWidth and mHeight
        /*
            int baseSize = d.getMaximumSizeDimension();
            if (wallpaper.width < baseSize) {
                wallpaper.width = baseSize;
            }
            if (wallpaper.height < baseSize) {
                wallpaper.height = baseSize;
            }

        */
        //#else /* VENDOR_EDIT */
        if (!success) {
	        android.util.DisplayMetrics displayMetrics = new android.util.DisplayMetrics();
	        d.getRealMetrics(displayMetrics);
	        int maxDim = 
	            Math.max(displayMetrics.widthPixels, displayMetrics.heightPixels);
	        int minDim = 
	            Math.min(displayMetrics.widthPixels, displayMetrics.heightPixels);
	        
	        if (wallpaper.width < minDim) {
	            wallpaper.width = -1 == mWallpaperHelper.mWidthOfDefaultWallpaper ? minDim : mWallpaperHelper.mWidthOfDefaultWallpaper;
	        }
	        
	        if (wallpaper.height < maxDim) {
	            wallpaper.height = maxDim;
	        }        
        }
        //#endif /* VENDOR_EDIT */
    }


    // Called by SystemBackupAgent after files are restored to disk.
    void settingsRestored() {
        // TODO: If necessary, make it work for secondary users as well. This currently assumes
        // restores only to the primary user
        if (DEBUG) Slog.v(TAG, "settingsRestored");
        WallpaperData wallpaper = null;
        boolean success = false;
        synchronized (mLock) {
            loadSettingsLocked(0);
            wallpaper = mWallpaperMap.get(0);
            if (wallpaper.nextWallpaperComponent != null
                    && !wallpaper.nextWallpaperComponent.equals(IMAGE_WALLPAPER)) {
                if (!bindWallpaperComponentLocked(wallpaper.nextWallpaperComponent, false, false,
                        wallpaper, null)) {
                    // No such live wallpaper or other failure; fall back to the default
                    // live wallpaper (since the profile being restored indicated that the
                    // user had selected a live rather than static one).
                    bindWallpaperComponentLocked(null, false, false, wallpaper, null);
                }
                success = true;
            } else {
                // If there's a wallpaper name, we use that.  If that can't be loaded, then we
                // use the default.
                if ("".equals(wallpaper.name)) {
                    if (DEBUG) Slog.v(TAG, "settingsRestored: name is empty");
                    success = true;
                } else {
                    if (DEBUG) Slog.v(TAG, "settingsRestored: attempting to restore named resource");
                    success = restoreNamedResourceLocked(wallpaper);
                }
                if (DEBUG) Slog.v(TAG, "settingsRestored: success=" + success);
                if (success) {
                    bindWallpaperComponentLocked(wallpaper.nextWallpaperComponent, false, false,
                            wallpaper, null);
                }
            }
        }

        if (!success) {
            Slog.e(TAG, "Failed to restore wallpaper: '" + wallpaper.name + "'");
            wallpaper.name = "";
            getWallpaperDir(0).delete();
        }

        synchronized (mLock) {
            saveSettingsLocked(wallpaper);
        }
    }

    boolean restoreNamedResourceLocked(WallpaperData wallpaper) {
        if (wallpaper.name.length() > 4 && "res:".equals(wallpaper.name.substring(0, 4))) {
            String resName = wallpaper.name.substring(4);

            String pkg = null;
            int colon = resName.indexOf(':');
            if (colon > 0) {
                pkg = resName.substring(0, colon);
            }

            String ident = null;
            int slash = resName.lastIndexOf('/');
            if (slash > 0) {
                ident = resName.substring(slash+1);
            }

            String type = null;
            if (colon > 0 && slash > 0 && (slash-colon) > 1) {
                type = resName.substring(colon+1, slash);
            }

            if (pkg != null && ident != null && type != null) {
                int resId = -1;
                InputStream res = null;
                FileOutputStream fos = null;
                try {
                    Context c = mContext.createPackageContext(pkg, Context.CONTEXT_RESTRICTED);
                    Resources r = c.getResources();
                    resId = r.getIdentifier(resName, null, null);
                    if (resId == 0) {
                        Slog.e(TAG, "couldn't resolve identifier pkg=" + pkg + " type=" + type
                                + " ident=" + ident);
                        return false;
                    }

                    res = r.openRawResource(resId);
                    if (wallpaper.wallpaperFile.exists()) {
                        wallpaper.wallpaperFile.delete();
                    }
                    fos = new FileOutputStream(wallpaper.wallpaperFile);

                    byte[] buffer = new byte[32768];
                    int amt;
                    while ((amt=res.read(buffer)) > 0) {
                        fos.write(buffer, 0, amt);
                    }
                    // mWallpaperObserver will notice the close and send the change broadcast

                    Slog.v(TAG, "Restored wallpaper: " + resName);
                    return true;
                } catch (NameNotFoundException e) {
                    Slog.e(TAG, "Package name " + pkg + " not found");
                } catch (Resources.NotFoundException e) {
                    Slog.e(TAG, "Resource not found: " + resId);
                } catch (IOException e) {
                    Slog.e(TAG, "IOException while restoring wallpaper ", e);
                } finally {
                    if (res != null) {
                        try {
                            res.close();
                        } catch (IOException ex) {}
                    }
                    if (fos != null) {
                        FileUtils.sync(fos);
                        try {
                            fos.close();
                        } catch (IOException ex) {}
                    }
                }
            }
        }
        return false;
    }

    @Override
    protected void dump(FileDescriptor fd, PrintWriter pw, String[] args) {
        if (mContext.checkCallingOrSelfPermission(android.Manifest.permission.DUMP)
                != PackageManager.PERMISSION_GRANTED) {
            
            pw.println("Permission Denial: can't dump wallpaper service from from pid="
                    + Binder.getCallingPid()
                    + ", uid=" + Binder.getCallingUid());
            return;
        }

        synchronized (mLock) {
            pw.println("Current Wallpaper Service state:");
            for (int i = 0; i < mWallpaperMap.size(); i++) {
                WallpaperData wallpaper = mWallpaperMap.valueAt(i);
                pw.println(" User " + wallpaper.userId + ":");
                pw.print("  mWidth=");
                pw.print(wallpaper.width);
                pw.print(" mHeight=");
                pw.println(wallpaper.height);
                pw.print("  mName=");
                pw.println(wallpaper.name);
                pw.print("  mWallpaperComponent=");
                pw.println(wallpaper.wallpaperComponent);
                if (wallpaper.connection != null) {
                    WallpaperConnection conn = wallpaper.connection;
                    pw.print("  Wallpaper connection ");
                    pw.print(conn);
                    pw.println(":");
                    if (conn.mInfo != null) {
                        pw.print("    mInfo.component=");
                        pw.println(conn.mInfo.getComponent());
                    }
                    pw.print("    mToken=");
                    pw.println(conn.mToken);
                    pw.print("    mService=");
                    pw.println(conn.mService);
                    pw.print("    mEngine=");
                    pw.println(conn.mEngine);
                    pw.print("    mLastDiedTime=");
                    pw.println(wallpaper.lastDiedTime - SystemClock.uptimeMillis());
                }
            }
        }
    }
	
	//#ifdef VENDOR_EDIT
	//gaoliang@plf.DesktopApp.Wallpaper, 2012/12/28, Add for pure houtai
	@android.annotation.OppoHook(level=android.annotation.OppoHook.OppoHookType.NEW_METHOD,
						property=android.annotation.OppoHook.OppoRomType.ROM,
						note="gaoliang add for pure houtai")	
	public ComponentName getLiveComponent() {
	    WallpaperData wallpaper = mWallpaperMap.get(mCurrentUserId);
	    if (wallpaper.wallpaperComponent != null) {
		    if (!wallpaper.wallpaperComponent.getClassName().equals("com.android.systemui.ImageWallpaper")) {
			    return wallpaper.wallpaperComponent;
			} else {
			    return null;
			}
		}
		return null;
	}
    //#endif /* VENDOR_EDIT */
}
