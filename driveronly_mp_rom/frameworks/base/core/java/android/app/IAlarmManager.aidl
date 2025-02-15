/* //device/java/android/android/app/IAlarmManager.aidl
**
** Copyright 2006, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License"); 
** you may not use this file except in compliance with the License. 
** You may obtain a copy of the License at 
**
**     http://www.apache.org/licenses/LICENSE-2.0 
**
** Unless required by applicable law or agreed to in writing, software 
** distributed under the License is distributed on an "AS IS" BASIS, 
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
** See the License for the specific language governing permissions and 
** limitations under the License.
*/
package android.app;

import android.app.PendingIntent;

/**
 * System private API for talking with the alarm manager service.
 *
 * {@hide}
 */
interface IAlarmManager {
    void set(int type, long triggerAtTime, in PendingIntent operation);
    void setRepeating(int type, long triggerAtTime, long interval, in PendingIntent operation);
    void setInexactRepeating(int type, long triggerAtTime, long interval, in PendingIntent operation);
    void setTime(long millis);
    void setTimeZone(String zone);
    void remove(in PendingIntent operation);

    //#ifdef VENDOR_EDIT 
    //Yuanliu.Tang@PersonalA.Alarm, 2013/02/22, Add for for SecureSafe remove alarms by packages 20120129
    void removePackagesAlarms(in String[] pkgList);
    //#endif /* VENDOR_EDIT */
    void cancelPoweroffAlarm(String name);
    boolean bootFromPoweroffAlarm();
}


