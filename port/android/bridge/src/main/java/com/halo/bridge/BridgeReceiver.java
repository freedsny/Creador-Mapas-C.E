package com.halo.bridge;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/**
 * The bridge installed over the old app by its updater: BridgeActivity started, which goes on to the game's install.
 * (Android lets an app start itself from here only in some cases; otherwise the package installer's Open button
 * does.)
 */
public class BridgeReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        if (!Intent.ACTION_MY_PACKAGE_REPLACED.equals(intent.getAction()))
            return;
        // (the old app's own download, which its updater left in the cache)
        new java.io.File(new java.io.File(context.getCacheDir(), "update"), "halo.apk").delete();
        try {
            Intent start = new Intent(context, BridgeActivity.class);

            start.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            context.startActivity(start);
        } catch (RuntimeException e) {
            // not allowed from the background here: the installer's Open button starts it
        }
    }
}
