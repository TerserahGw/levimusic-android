package com.reaaurelis.levimusic.bridge;
import android.content.Context;
import android.service.notification.NotificationListenerService;

/** Optional system media-session permission; local LeviMusic API runs independently. */
public final class BridgeService extends NotificationListenerService {
    public static String getOrCreateToken(Context c) {
        return BridgeApi.getOrCreateToken(c);
    }
    @Override public void onListenerConnected() {
        super.onListenerConnected();
        BridgeApi.ensureStarted(getApplicationContext());
    }
}
