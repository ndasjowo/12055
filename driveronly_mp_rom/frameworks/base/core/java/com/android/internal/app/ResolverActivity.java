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

package com.android.internal.app;

import com.android.internal.R;
import com.android.internal.content.PackageMonitor;

import android.app.ActivityManager;
import android.app.ActivityManagerNative;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ActivityInfo;
import android.content.pm.LabeledIntent;
import android.content.pm.PackageManager;
import android.content.pm.PackageManager.NameNotFoundException;
import android.content.pm.ResolveInfo;
import android.content.res.Resources;
import android.graphics.drawable.Drawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.PatternMatcher;
import android.os.Process;
import android.os.RemoteException;
import android.os.UserHandle;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.BaseAdapter;
import android.widget.Button;
import android.widget.GridView;
import android.widget.ImageView;
import android.widget.ListView;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Set;
import android.app.OppoThemeHelper;
//#ifdef VENDOR_EDIT
//Jianrong.Zheng@Prd.BasicSrv.BatteryManager, 2013/04/19, Add for 
import android.net.Uri;
import android.content.ContentResolver;
import android.database.Cursor;
import android.widget.Toast;
//#endif /* VENDOR_EDIT */
/**
 * This activity is displayed when the system attempts to start an Intent for
 * which there is more than one matching activity, allowing the user to decide
 * which to go to.  It is not normally used directly by application developers.
 */
public class ResolverActivity extends AlertActivity implements AdapterView.OnItemClickListener {
    private static final String TAG = "ResolverActivity";

    private int mLaunchedFromUid;
    private ResolveListAdapter mAdapter;
    private PackageManager mPm;
    private boolean mAlwaysUseOption;
    private boolean mShowExtended;
    private GridView mGrid;
    private Button mAlwaysButton;
    private Button mOnceButton;
    private int mIconDpi;
    private int mIconSize;
    private int mMaxColumns;
    private int mLastSelected = GridView.INVALID_POSITION;

    private boolean mRegistered;
    private final PackageMonitor mPackageMonitor = new PackageMonitor() {
        @Override public void onSomePackagesChanged() {
            mAdapter.handlePackagesChanged();
        }
    };

    private Intent makeMyIntent() {
        Intent intent = new Intent(getIntent());
        // The resolver activity is set to be hidden from recent tasks.
        // we don't want this attribute to be propagated to the next activity
        // being launched.  Note that if the original Intent also had this
        // flag set, we are now losing it.  That should be a very rare case
        // and we can live with this.
        intent.setFlags(intent.getFlags()&~Intent.FLAG_ACTIVITY_EXCLUDE_FROM_RECENTS);
        return intent;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        onCreate(savedInstanceState, makeMyIntent(),
                getResources().getText(com.android.internal.R.string.whichApplication),
                null, null, true);
    }

    @android.annotation.OppoHook(
            level = android.annotation.OppoHook.OppoHookType.CHANGE_CODE,
            property = android.annotation.OppoHook.OppoRomType.ROM,
            note = "Jianjun.Dan|Jianhua.Lin@Plf.Framework.SDK : Modify for change oppo theme ")
    protected void onCreate(Bundle savedInstanceState, Intent intent,
            CharSequence title, Intent[] initialIntents, List<ResolveInfo> rList,
            boolean alwaysUseOption) {
        //#ifndef VENDOR_EDIT
        //Jianjun.Dan@Plf.Framework.SDK : Modify for change oppo theme
        /*
        setTheme(R.style.Theme_DeviceDefault_Light_Dialog_Alert);
        */
        //#else
        setTheme(com.oppo.internal.R.style.Theme_OPPO_Dialog_Alert); 
        //#endif /* VENDOR_EDIT */
        super.onCreate(savedInstanceState);
        try {
            mLaunchedFromUid = ActivityManagerNative.getDefault().getLaunchedFromUid(
                    getActivityToken());
        } catch (RemoteException e) {
            mLaunchedFromUid = -1;
        }
        mPm = getPackageManager();
        mAlwaysUseOption = alwaysUseOption;
        mMaxColumns = getResources().getInteger(R.integer.config_maxResolverActivityColumns);
        intent.setComponent(null);

        AlertController.AlertParams ap = mAlertParams;

        ap.mTitle = title;

        mPackageMonitor.register(this, getMainLooper(), false);
        mRegistered = true;

        final ActivityManager am = (ActivityManager) getSystemService(ACTIVITY_SERVICE);
        mIconDpi = am.getLauncherLargeIconDensity();
        
        //#ifdef VENDOR_EDIT
        //Jianhua.Lin@Plf.Framework.SDK, 2013-04-13,Modify for the size of an application icon
        /*
        mIconSize = am.getLauncherLargeIconSize();
        */
        //#else
        mIconSize = (int)getResources().getDimension(oppo.R.dimen.oppo_resolve_app_icon_size);
        //#endif /* VENDOR_EDIT */

        mAdapter = new ResolveListAdapter(this, intent, initialIntents, rList,
                mLaunchedFromUid);
        int count = mAdapter.getCount();
        if (mLaunchedFromUid < 0 || UserHandle.isIsolated(mLaunchedFromUid)) {
            // Gulp!
            finish();
            return;
        } else if (count > 1) {
            ap.mView = getLayoutInflater().inflate(R.layout.resolver_grid, null);
            mGrid = (GridView) ap.mView.findViewById(R.id.resolver_grid);
            mGrid.setAdapter(mAdapter);
            mGrid.setOnItemClickListener(this);
            mGrid.setOnItemLongClickListener(new ItemLongClickListener());

            if (alwaysUseOption) {
                mGrid.setChoiceMode(ListView.CHOICE_MODE_SINGLE);
            }

            resizeGrid();
        } else if (count == 1) {
            //#ifdef VENDOR_EDIT
            //Jianrong.Zheng@Prd.BasicSrv.BatteryManager, 2013/04/19, Add for 
            if (bIsTargetUriExists(intent))            
            //#endif /* VENDOR_EDIT */
        
            startActivity(mAdapter.intentForPosition(0));
            mPackageMonitor.unregister();
            mRegistered = false;
            finish();
            return;
        } else {
            ap.mMessage = getResources().getText(R.string.noApplications);
        }

        setupAlert();

        if (alwaysUseOption) {
            final ViewGroup buttonLayout = (ViewGroup) findViewById(R.id.button_bar);
            if (buttonLayout != null) {
                buttonLayout.setVisibility(View.VISIBLE);
                mAlwaysButton = (Button) buttonLayout.findViewById(R.id.button_always);
                mOnceButton = (Button) buttonLayout.findViewById(R.id.button_once);
            } else {
                mAlwaysUseOption = false;
            }
        }
    }

    
    //#ifdef VENDOR_EDIT
    //Jianrong.Zheng@Prd.BasicSrv.BatteryManager, 2013/04/18, Add for 
    private boolean bIsTargetUriExists(Intent target) {
        Log.w(TAG, "Target intent: " + target.toString());
        boolean result = true;
        
        if (target.getAction() != null && target.getAction().equals(Intent.ACTION_SEND)) {
            String type = target.getType();
            
            if (type != null && type.equals("image/png")) {
                Uri uri = (Uri)target.getExtra(Intent.EXTRA_STREAM);
                
                Log.w(TAG, "uri : " + uri.toString());
                
                if (uri != null) {
                    ContentResolver resolver = this.getContentResolver();
                    Cursor c = resolver.query(uri, null, null, null, null, null);
                    
                    Log.w(TAG, "c == null ?: " + (c == null));

                    if (c != null) {
                        if (!c.moveToFirst()){
                            Log.w(TAG, "cursor is empty");
                            Toast.makeText(this, com.android.internal.R.string.httpErrorFileNotFound, Toast.LENGTH_SHORT).show();
                            result = false;
                        }        

                        c.close(); 
                    }
                }
            }
        }

        return result;
    }
    //#endif /* VENDOR_EDIT */



    void resizeGrid() {
        final int itemCount = mAdapter.getCount();
        mGrid.setNumColumns(Math.min(itemCount, mMaxColumns));
    }

    Drawable getIcon(Resources res, int resId) {
        Drawable result;
        try {
            result = res.getDrawableForDensity(resId, mIconDpi);
        } catch (Resources.NotFoundException e) {
            result = null;
        }

        return result;
    }

    Drawable loadIconForResolveInfo(ResolveInfo ri) {
        Drawable dr;
        try {
            if (ri.resolvePackageName != null && ri.icon != 0) {
                dr = getIcon(mPm.getResourcesForApplication(ri.resolvePackageName), ri.icon);
                if (dr != null) {
                    return dr;
                }
            }
            final int iconRes = ri.getIconResource();
            if (iconRes != 0) {
                dr = getIcon(mPm.getResourcesForApplication(ri.activityInfo.packageName), iconRes);
                if (dr != null) {
                    return dr;
                }
            }
        } catch (NameNotFoundException e) {
            Log.e(TAG, "Couldn't find resources for package", e);
        }
        return ri.loadIcon(mPm);
    }

    @Override
    protected void onRestart() {
        super.onRestart();
        if (!mRegistered) {
            mPackageMonitor.register(this, getMainLooper(), false);
            mRegistered = true;
        }
        mAdapter.handlePackagesChanged();
    }

    @Override
    protected void onStop() {
        super.onStop();
        if (mRegistered) {
            mPackageMonitor.unregister();
            mRegistered = false;
        }
        if ((getIntent().getFlags()&Intent.FLAG_ACTIVITY_NEW_TASK) != 0) {
            // This resolver is in the unusual situation where it has been
            // launched at the top of a new task.  We don't let it be added
            // to the recent tasks shown to the user, and we need to make sure
            // that each time we are launched we get the correct launching
            // uid (not re-using the same resolver from an old launching uid),
            // so we will now finish ourself since being no longer visible,
            // the user probably can't get back to us.
            if (!isChangingConfigurations()) {
                finish();
            }
        }
    }

    @Override
    protected void onRestoreInstanceState(Bundle savedInstanceState) {
        super.onRestoreInstanceState(savedInstanceState);
        if (mAlwaysUseOption) {
            final int checkedPos = mGrid.getCheckedItemPosition();
            final boolean enabled = checkedPos != GridView.INVALID_POSITION;
            mLastSelected = checkedPos;
            mAlwaysButton.setEnabled(enabled);
            mOnceButton.setEnabled(enabled);
            if (enabled) {
                mGrid.setSelection(checkedPos);
            }
        }
    }

    @Override
    public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
        final int checkedPos = mGrid.getCheckedItemPosition();
        final boolean hasValidSelection = checkedPos != GridView.INVALID_POSITION;
        if (mAlwaysUseOption && (!hasValidSelection || mLastSelected != checkedPos)) {
            mAlwaysButton.setEnabled(hasValidSelection);
            mOnceButton.setEnabled(hasValidSelection);
            if (hasValidSelection) {
                mGrid.smoothScrollToPosition(checkedPos);
            }
            mLastSelected = checkedPos;
        } else {
            startSelected(position, false);
        }
    }

    public void onButtonClick(View v) {
        final int id = v.getId();
        startSelected(mGrid.getCheckedItemPosition(), id == R.id.button_always);
        dismiss();
    }

    void startSelected(int which, boolean always) {
        ResolveInfo ri = mAdapter.resolveInfoForPosition(which);
        Intent intent = mAdapter.intentForPosition(which);
        //#ifdef VENDOR_EDIT
        //Jianrong.Zheng@Prd.BasicSrv.BatteryManager, 2013/04/19, Add for 
        if (bIsTargetUriExists(intent))            
        //#endif /* VENDOR_EDIT */
        
        onIntentSelected(ri, intent, always);
        finish();
    }

    @android.annotation.OppoHook(
            level = android.annotation.OppoHook.OppoHookType.CHANGE_CODE,
            property = android.annotation.OppoHook.OppoRomType.ROM,
            note = "Xiaokang.Feng@Plf.Framework.SDK : Modify for change acitivity animation")
    protected void onIntentSelected(ResolveInfo ri, Intent intent, boolean alwaysCheck) {
        if (alwaysCheck) {
            // Build a reasonable intent filter, based on what matched.
            IntentFilter filter = new IntentFilter();

            if (intent.getAction() != null) {
                filter.addAction(intent.getAction());
            }
            Set<String> categories = intent.getCategories();
            if (categories != null) {
                for (String cat : categories) {
                    filter.addCategory(cat);
                }
            }
            filter.addCategory(Intent.CATEGORY_DEFAULT);

            int cat = ri.match&IntentFilter.MATCH_CATEGORY_MASK;
            Uri data = intent.getData();
            if (cat == IntentFilter.MATCH_CATEGORY_TYPE) {
                String mimeType = intent.resolveType(this);
                if (mimeType != null) {
                    try {
                        filter.addDataType(mimeType);
                    } catch (IntentFilter.MalformedMimeTypeException e) {
                        Log.w("ResolverActivity", e);
                        filter = null;
                    }
                }
            }
            if (data != null && data.getScheme() != null) {
                // We need the data specification if there was no type,
                // OR if the scheme is not one of our magical "file:"
                // or "content:" schemes (see IntentFilter for the reason).
                if (cat != IntentFilter.MATCH_CATEGORY_TYPE
                        || (!"file".equals(data.getScheme())
                                && !"content".equals(data.getScheme()))) {
                    filter.addDataScheme(data.getScheme());
    
                    // Look through the resolved filter to determine which part
                    // of it matched the original Intent.
                    Iterator<IntentFilter.AuthorityEntry> aIt = ri.filter.authoritiesIterator();
                    if (aIt != null) {
                        while (aIt.hasNext()) {
                            IntentFilter.AuthorityEntry a = aIt.next();
                            if (a.match(data) >= 0) {
                                int port = a.getPort();
                                filter.addDataAuthority(a.getHost(),
                                        port >= 0 ? Integer.toString(port) : null);
                                break;
                            }
                        }
                    }
                    Iterator<PatternMatcher> pIt = ri.filter.pathsIterator();
                    if (pIt != null) {
                        String path = data.getPath();
                        while (path != null && pIt.hasNext()) {
                            PatternMatcher p = pIt.next();
                            if (p.match(path)) {
                                filter.addDataPath(p.getPath(), p.getType());
                                break;
                            }
                        }
                    }
                }
            }

            if (filter != null) {
                final int N = mAdapter.mList.size();
                ComponentName[] set = new ComponentName[N];
                int bestMatch = 0;
                for (int i=0; i<N; i++) {
                    ResolveInfo r = mAdapter.mList.get(i).ri;
                    set[i] = new ComponentName(r.activityInfo.packageName,
                            r.activityInfo.name);
                    if (r.match > bestMatch) bestMatch = r.match;
                }
                getPackageManager().addPreferredActivity(filter, bestMatch, set,
                        intent.getComponent());
            }
        }

        if (intent != null) {
            startActivity(intent);
            //#ifdef VENDOR_EDIT
            //Xiaokang.Feng@Plf.Framework.SDK, 2013-06-08,Modify for change acitivity animation
            overridePendingTransition(oppo.R.anim.oppo_dialog_close_enter, oppo.R.anim.oppo_dialog_close_exit);
            //#endif /* VENDOR_EDIT */	
        }
    }

    @android.annotation.OppoHook(
            level = android.annotation.OppoHook.OppoHookType.CHANGE_CODE,
            property = android.annotation.OppoHook.OppoRomType.ROM,
            note = "Xiaokang.Feng@Plf.Framework.SDK : Modify for change acitivity animation")
    void showAppDetails(ResolveInfo ri) {
        Intent in = new Intent().setAction("android.settings.APPLICATION_DETAILS_SETTINGS")
                .setData(Uri.fromParts("package", ri.activityInfo.packageName, null))
                .addFlags(Intent.FLAG_ACTIVITY_CLEAR_WHEN_TASK_RESET);
        startActivity(in);
        //#ifdef VENDOR_EDIT
        //Xiaokang.Feng@Plf.Framework.SDK, 2013-06-08,Modify for change acitivity animation
        overridePendingTransition(oppo.R.anim.oppo_dialog_close_enter,oppo.R.anim.oppo_dialog_close_exit);
        //#endif /* VENDOR_EDIT */	
    }

    private final class DisplayResolveInfo {
        ResolveInfo ri;
        CharSequence displayLabel;
        Drawable displayIcon;
        CharSequence extendedInfo;
        Intent origIntent;

        DisplayResolveInfo(ResolveInfo pri, CharSequence pLabel,
                CharSequence pInfo, Intent pOrigIntent) {
            ri = pri;
            displayLabel = pLabel;
            extendedInfo = pInfo;
            origIntent = pOrigIntent;
        }
    }

    private final class ResolveListAdapter extends BaseAdapter {
        private final Intent[] mInitialIntents;
        private final List<ResolveInfo> mBaseResolveList;
        private final Intent mIntent;
        private final int mLaunchedFromUid;
        private final LayoutInflater mInflater;

        private List<ResolveInfo> mCurrentResolveList;
        private List<DisplayResolveInfo> mList;

        public ResolveListAdapter(Context context, Intent intent,
                Intent[] initialIntents, List<ResolveInfo> rList, int launchedFromUid) {
            mIntent = new Intent(intent);
            mIntent.setComponent(null);
            mInitialIntents = initialIntents;
            mBaseResolveList = rList;
            mLaunchedFromUid = launchedFromUid;
            mInflater = (LayoutInflater)context.getSystemService(Context.LAYOUT_INFLATER_SERVICE);
            rebuildList();
        }

        public void handlePackagesChanged() {
            final int oldItemCount = getCount();
            rebuildList();
            notifyDataSetChanged();
            /// M: [ALPS00344799] The JE pops up when you press Power key
            if (mList == null || mList.size() <= 0) {
                // We no longer have any items...  just finish the activity.
                finish();
            }

            final int newItemCount = getCount();
            if (newItemCount != oldItemCount) {
                /// M: fix ALPS00368603 first issue, if the last selected item has been selected and uninstalled
                /// we should disable always and once buttons since it has beend removed @{
                if (mAlwaysUseOption) {
                    final int checkedPos = mGrid.getCheckedItemPosition();
                    final boolean enabled = checkedPos != GridView.INVALID_POSITION;
                    if (enabled && checkedPos >= newItemCount) {
                        Log.w("ResolverActivity", "handlePackagesChanged: checkedPos " + checkedPos + " >= newItemCount " + newItemCount + ", disable buttons");
                        mAlwaysButton.setEnabled(false);
                        mOnceButton.setEnabled(false);
                    }
                }
                /// @}
                resizeGrid();
            }
        }

        private void rebuildList() {
            if (mBaseResolveList != null) {
                mCurrentResolveList = mBaseResolveList;
            } else {
                mCurrentResolveList = mPm.queryIntentActivities(
                        mIntent, PackageManager.MATCH_DEFAULT_ONLY
                        | (mAlwaysUseOption ? PackageManager.GET_RESOLVED_FILTER : 0));
                // Filter out any activities that the launched uid does not
                // have permission for.  We don't do this when we have an explicit
                // list of resolved activities, because that only happens when
                // we are being subclassed, so we can safely launch whatever
                // they gave us.
                if (mCurrentResolveList != null) {
                    for (int i=mCurrentResolveList.size()-1; i >= 0; i--) {
                        ActivityInfo ai = mCurrentResolveList.get(i).activityInfo;
                        int granted = ActivityManager.checkComponentPermission(
                                ai.permission, mLaunchedFromUid,
                                ai.applicationInfo.uid, ai.exported);
                        if (granted != PackageManager.PERMISSION_GRANTED) {
                            // Access not allowed!
                            mCurrentResolveList.remove(i);
                        }
                    }
                }
            }
            int N;
            if ((mCurrentResolveList != null) && ((N = mCurrentResolveList.size()) > 0)) {
                // Only display the first matches that are either of equal
                // priority or have asked to be default options.
                ResolveInfo r0 = mCurrentResolveList.get(0);
                for (int i=1; i<N; i++) {
                    ResolveInfo ri = mCurrentResolveList.get(i);
                    if (false) Log.v(
                        "ResolveListActivity",
                        r0.activityInfo.name + "=" +
                        r0.priority + "/" + r0.isDefault + " vs " +
                        ri.activityInfo.name + "=" +
                        ri.priority + "/" + ri.isDefault);
                   if (r0.priority != ri.priority ||
                        r0.isDefault != ri.isDefault) {
                        while (i < N) {
                            mCurrentResolveList.remove(i);
                            N--;
                        }
                    }
                }
                if (N > 1) {
                    ResolveInfo.DisplayNameComparator rComparator =
                            new ResolveInfo.DisplayNameComparator(mPm);
                    Collections.sort(mCurrentResolveList, rComparator);
                }
                
                mList = new ArrayList<DisplayResolveInfo>();
                
                // First put the initial items at the top.
                if (mInitialIntents != null) {
                    for (int i=0; i<mInitialIntents.length; i++) {
                        Intent ii = mInitialIntents[i];
                        if (ii == null) {
                            continue;
                        }
                        ActivityInfo ai = ii.resolveActivityInfo(
                                getPackageManager(), 0);
                        if (ai == null) {
                            Log.w("ResolverActivity", "No activity found for "
                                    + ii);
                            continue;
                        }
                        ResolveInfo ri = new ResolveInfo();
                        ri.activityInfo = ai;
                        if (ii instanceof LabeledIntent) {
                            LabeledIntent li = (LabeledIntent)ii;
                            ri.resolvePackageName = li.getSourcePackage();
                            ri.labelRes = li.getLabelResource();
                            ri.nonLocalizedLabel = li.getNonLocalizedLabel();
                            ri.icon = li.getIconResource();
                        }
                        mList.add(new DisplayResolveInfo(ri,
                                ri.loadLabel(getPackageManager()), null, ii));
                    }
                }
                
                // Check for applications with same name and use application name or
                // package name if necessary
                r0 = mCurrentResolveList.get(0);
                int start = 0;
                CharSequence r0Label =  r0.loadLabel(mPm);
                mShowExtended = false;
                for (int i = 1; i < N; i++) {
                    if (r0Label == null) {
                        r0Label = r0.activityInfo.packageName;
                    }
                    ResolveInfo ri = mCurrentResolveList.get(i);
                    CharSequence riLabel = ri.loadLabel(mPm);
                    if (riLabel == null) {
                        riLabel = ri.activityInfo.packageName;
                    }
                    if (riLabel.equals(r0Label)) {
                        continue;
                    }
                    processGroup(mCurrentResolveList, start, (i-1), r0, r0Label);
                    r0 = ri;
                    r0Label = riLabel;
                    start = i;
                }
                // Process last group
                processGroup(mCurrentResolveList, start, (N-1), r0, r0Label);
            }
        }

        private void processGroup(List<ResolveInfo> rList, int start, int end, ResolveInfo ro,
                CharSequence roLabel) {
            // Process labels from start to i
            int num = end - start+1;
            if (num == 1) {
                // No duplicate labels. Use label for entry at start
                mList.add(new DisplayResolveInfo(ro, roLabel, null, null));
            } else {
                mShowExtended = true;
                boolean usePkg = false;
                CharSequence startApp = ro.activityInfo.applicationInfo.loadLabel(mPm);
                if (startApp == null) {
                    usePkg = true;
                }
                if (!usePkg) {
                    // Use HashSet to track duplicates
                    HashSet<CharSequence> duplicates =
                        new HashSet<CharSequence>();
                    duplicates.add(startApp);
                    for (int j = start+1; j <= end ; j++) {
                        ResolveInfo jRi = rList.get(j);
                        CharSequence jApp = jRi.activityInfo.applicationInfo.loadLabel(mPm);
                        if ( (jApp == null) || (duplicates.contains(jApp))) {
                            usePkg = true;
                            break;
                        } else {
                            duplicates.add(jApp);
                        }
                    }
                    // Clear HashSet for later use
                    duplicates.clear();
                }
                for (int k = start; k <= end; k++) {
                    ResolveInfo add = rList.get(k);
                    if (usePkg) {
                        // Use application name for all entries from start to end-1
                        mList.add(new DisplayResolveInfo(add, roLabel,
                                add.activityInfo.packageName, null));
                    } else {
                        // Use package name for all entries from start to end-1
                        mList.add(new DisplayResolveInfo(add, roLabel,
                                add.activityInfo.applicationInfo.loadLabel(mPm), null));
                    }
                }
            }
        }

        public ResolveInfo resolveInfoForPosition(int position) {
            if (mList == null) {
                return null;
            }

            return mList.get(position).ri;
        }

        public Intent intentForPosition(int position) {
            if (mList == null) {
                return null;
            }

            DisplayResolveInfo dri = mList.get(position);
            
            Intent intent = new Intent(dri.origIntent != null
                    ? dri.origIntent : mIntent);
            intent.addFlags(Intent.FLAG_ACTIVITY_FORWARD_RESULT
                    |Intent.FLAG_ACTIVITY_PREVIOUS_IS_TOP);
            ActivityInfo ai = dri.ri.activityInfo;
            intent.setComponent(new ComponentName(
                    ai.applicationInfo.packageName, ai.name));
            return intent;
        }

        public int getCount() {
            return mList != null ? mList.size() : 0;
        }

        public Object getItem(int position) {
            return position;
        }

        public long getItemId(int position) {
            return position;
        }

        public View getView(int position, View convertView, ViewGroup parent) {
            View view;
            if (convertView == null) {
                view = mInflater.inflate(
                        com.android.internal.R.layout.resolve_list_item, parent, false);

                // Fix the icon size even if we have different sized resources
                ImageView icon = (ImageView)view.findViewById(R.id.icon);
                ViewGroup.LayoutParams lp = (ViewGroup.LayoutParams) icon.getLayoutParams();
                lp.width = lp.height = mIconSize;
            } else {
                view = convertView;
            }
            bindView(view, mList.get(position));
            return view;
        }

        @android.annotation.OppoHook(
                level = android.annotation.OppoHook.OppoHookType.CHANGE_CODE,
                property = android.annotation.OppoHook.OppoRomType.ROM,
                note = "Jianhua.Lin|Xiaokang.Feng@Plf.Framework.SDK,2013-04-12,Modify for the layout of item && ConvertIcon")
        private final void bindView(View view, DisplayResolveInfo info) {
            //#ifdef VENDOR_EDIT
            //#@OppoHook
            //Jianhua.Lin@Plf.Framework.SDK, 2013-08-23 : Add for the layout of item            
            Drawable pickerBg = getResources().getDrawable(oppo.R.drawable.oppo_activity_picker_bg);
            view.setBackground(pickerBg);
            //#endif /* VENDOR_EDIT */
            TextView text = (TextView)view.findViewById(com.android.internal.R.id.text1);
            TextView text2 = (TextView)view.findViewById(com.android.internal.R.id.text2);
            ImageView icon = (ImageView)view.findViewById(R.id.icon);
            text.setText(info.displayLabel);
            if (mShowExtended) {
                text2.setVisibility(View.VISIBLE);
                text2.setText(info.extendedInfo);
            } else {
                text2.setVisibility(View.GONE);
            }
            if (info.displayIcon == null) {
                //#ifndef VENDOR_EDIT
                //Xiaokang.Feng@Plf.Framework.SDK, 2013/07/09, Modify for ConvertIcon
                /*
                info.displayIcon = loadIconForResolveInfo(info.ri);
                */
                //#else /* VENDOR_EDIT */
                info.displayIcon = oppoLoadIconForResolveInfo(info.ri);
                //#endif /* VENDOR_EDIT */
            }
            icon.setImageDrawable(info.displayIcon);
        }
    }

    class ItemLongClickListener implements AdapterView.OnItemLongClickListener {

        @Override
        public boolean onItemLongClick(AdapterView<?> parent, View view, int position, long id) {
            ResolveInfo ri = mAdapter.resolveInfoForPosition(position);
            showAppDetails(ri);
            return true;
        }

    }

//#ifdef VENDOR_EDIT
//Xiaokang.Feng@Plf.Framework.SDK, 2013-07-09, Add for ConvertIcon
    @android.annotation.OppoHook(
            level = android.annotation.OppoHook.OppoHookType.NEW_METHOD,
            property = android.annotation.OppoHook.OppoRomType.ROM,
            note = "Xiaokang.Feng@Plf.Framework.SDK, add for ConvertIcon")
    Drawable oppoLoadIconForResolveInfo(ResolveInfo ri) {
        Drawable dr;
        if (ri.resolvePackageName != null && ri.icon != 0) {
           dr = OppoThemeHelper.getDrawable(mPm, ri.activityInfo.packageName, ri.icon, ri.activityInfo.applicationInfo, null);
            if (dr != null) {
                return dr;
            }
        }
        final int iconRes = ri.getIconResource();
        if (iconRes != 0) {
            dr = OppoThemeHelper.getDrawable(mPm, ri.activityInfo.packageName, iconRes, ri.activityInfo.applicationInfo, null);
            if (dr != null) {
                return dr;
            }
        }
        return ri.loadIcon(mPm);
    }
//#endif /* VENDOR_EDIT */
}

