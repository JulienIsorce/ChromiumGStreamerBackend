// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.media.ui;

import android.app.Notification;
import android.app.PendingIntent;
import android.app.Service;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.Drawable;
import android.os.Build;
import android.os.IBinder;
import android.support.v4.app.NotificationCompat;
import android.support.v4.app.NotificationManagerCompat;
import android.support.v4.media.MediaMetadataCompat;
import android.support.v4.media.session.MediaSessionCompat;
import android.support.v4.media.session.PlaybackStateCompat;
import android.view.KeyEvent;
import android.view.View;
import android.widget.RemoteViews;

import org.chromium.base.ApiCompatibilityUtils;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.tab.Tab;

/**
 * A class for notifications that provide information and optional media controls for a given media.
 * Internally implements a Service for transforming notification Intents into
 * {@link MediaNotificationListener} calls for all registered listeners.
 */
public class MediaNotificationManager {
    private static final Object LOCK = new Object();
    private static MediaNotificationManager sInstance;

    /**
     * Service used to transform intent requests triggered from the notification into
     * {@code Listener} callbacks. Ideally this class should be private, but public is required to
     * create as a service.
     */
    public static class ListenerService extends Service {
        private static final String ACTION_PLAY =
                "NotificationMediaPlaybackControls.ListenerService.PLAY";
        private static final String ACTION_PAUSE =
                "NotificationMediaPlaybackControls.ListenerService.PAUSE";
        private static final String ACTION_STOP =
                "NotificationMediaPlaybackControls.ListenerService.STOP";

        private PendingIntent getPendingIntent(String action) {
            Intent intent = new Intent(this, ListenerService.class);
            intent.setAction(action);
            return PendingIntent.getService(this, 0, intent, PendingIntent.FLAG_CANCEL_CURRENT);
        }

        @Override
        public IBinder onBind(Intent intent) {
            return null;
        }

        @Override
        public void onCreate() {
            super.onCreate();

            // This would only happen if we have been recreated by the OS after Chrome has died.
            // In this case, there can be no media playback happening so we don't have to show
            // the notification.
            if (sInstance == null) return;

            onServiceStarted(this);
        }

        @Override
        public void onDestroy() {
            super.onDestroy();

            if (sInstance == null) return;

            onServiceDestroyed();
        }

        @Override
        public int onStartCommand(Intent intent, int flags, int startId) {
            if (intent == null
                    || sInstance == null
                    || sInstance.mMediaNotificationInfo == null
                    || sInstance.mMediaNotificationInfo.listener == null) {
                stopSelf();
                return START_NOT_STICKY;
            }

            String action = intent.getAction();

            // Before Android L, instead of using the MediaSession callback, the system will fire
            // ACTION_MEDIA_BUTTON intents which stores the information about the key event.
            if (Intent.ACTION_MEDIA_BUTTON.equals(action)) {
                assert Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP;

                KeyEvent event = (KeyEvent) intent.getParcelableExtra(Intent.EXTRA_KEY_EVENT);
                if (event == null) return START_NOT_STICKY;
                if (event.getAction() != KeyEvent.ACTION_DOWN) return START_NOT_STICKY;

                switch (event.getKeyCode()) {
                    case KeyEvent.KEYCODE_MEDIA_PLAY:
                        sInstance.onPlay(
                                MediaNotificationListener.ACTION_SOURCE_MEDIA_SESSION);
                        break;
                    case KeyEvent.KEYCODE_MEDIA_PAUSE:
                        sInstance.onPause(
                                MediaNotificationListener.ACTION_SOURCE_MEDIA_SESSION);
                        break;
                    case KeyEvent.KEYCODE_HEADSETHOOK:
                    case KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE:
                        if (sInstance.mMediaNotificationInfo.isPaused) {
                            sInstance.onPlay(MediaNotificationListener.ACTION_SOURCE_MEDIA_SESSION);
                        } else {
                            sInstance.onPause(
                                    MediaNotificationListener.ACTION_SOURCE_MEDIA_SESSION);
                        }
                        break;
                    default:
                        break;
                }
                return START_NOT_STICKY;
            }

            if (ACTION_STOP.equals(action)) {
                sInstance.onStop(
                        MediaNotificationListener.ACTION_SOURCE_MEDIA_NOTIFICATION);
                stopSelf();
                return START_NOT_STICKY;
            }

            if (ACTION_PLAY.equals(action)) {
                sInstance.onPlay(MediaNotificationListener.ACTION_SOURCE_MEDIA_NOTIFICATION);
            } else if (ACTION_PAUSE.equals(action)) {
                sInstance.onPause(MediaNotificationListener.ACTION_SOURCE_MEDIA_NOTIFICATION);
            }

            return START_NOT_STICKY;
        }
    }

    /**
     * Shows the notification with media controls with the specified media info. Replaces/updates
     * the current notification if already showing. Does nothing if |mediaNotificationInfo| hasn't
     * changed from the last one.
     *
     * @param applicationContext context to create the notification with
     * @param notificationInfoBuilder information to show in the notification
     */
    public static void show(Context applicationContext,
                            MediaNotificationInfo.Builder notificationInfoBuilder) {
        synchronized (LOCK) {
            if (sInstance == null) {
                sInstance = new MediaNotificationManager(applicationContext);
            }
        }
        sInstance.mNotificationInfoBuilder = notificationInfoBuilder;
        sInstance.showNotification(notificationInfoBuilder.build());
    }

    /**
     * Hides the notification for the specified tabId.
     *
     * @param tabId the id of the tab that showed the notification or invalid tab id.
     */
    public static void hide(int tabId) {
        if (sInstance == null) return;
        sInstance.hideNotification(tabId);
    }

    /**
     * Hides any notification if shown by this service.
     */
    public static void clear() {
        if (sInstance == null) return;
        sInstance.clearNotification();
    }

    /**
     * Registers the started {@link Service} with the singleton and creates the notification.
     *
     * @param service the service that was started
     */
    private static void onServiceStarted(ListenerService service) {
        assert sInstance != null;
        assert sInstance.mService == null;
        sInstance.mService = service;
        sInstance.updateNotification();
    }

    /**
     * Handles the destruction
     */
    private static void onServiceDestroyed() {
        assert sInstance != null;
        assert sInstance.mService != null;

        clear();
        sInstance.mNotificationBuilder = null;
        sInstance.mService = null;
    }

    private final Context mContext;

    // ListenerService running for the notification. Only non-null when showing.
    private ListenerService mService;

    private final String mPlayDescription;

    private final String mPauseDescription;

    private NotificationCompat.Builder mNotificationBuilder;

    private Bitmap mNotificationIcon;

    private final Bitmap mMediaSessionIcon;

    private MediaNotificationInfo mMediaNotificationInfo;
    private MediaNotificationInfo.Builder mNotificationInfoBuilder;

    private MediaSessionCompat mMediaSession;

    private final MediaSessionCompat.Callback mMediaSessionCallback =
            new MediaSessionCompat.Callback() {
                @Override
                public void onPlay() {
                    sInstance.onPlay(MediaNotificationListener.ACTION_SOURCE_MEDIA_SESSION);
                }

                @Override
                public void onPause() {
                    sInstance.onPause(MediaNotificationListener.ACTION_SOURCE_MEDIA_SESSION);
                }
    };

    private MediaNotificationManager(Context context) {
        mContext = context;
        mPlayDescription = context.getResources().getString(R.string.accessibility_play);
        mPauseDescription = context.getResources().getString(R.string.accessibility_pause);

        // The MediaSession icon is a plain color.
        int size = context.getResources().getDimensionPixelSize(R.dimen.media_session_icon_size);
        mMediaSessionIcon = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        mMediaSessionIcon.eraseColor(ApiCompatibilityUtils.getColor(
                context.getResources(), R.color.media_session_icon_color));
    }

    private void onPlay(int actionSource) {
        if (!mMediaNotificationInfo.isPaused) return;

        mMediaNotificationInfo = mNotificationInfoBuilder.setPaused(false).build();
        updateNotification();

        mMediaNotificationInfo.listener.onPlay(actionSource);
    }

    private void onPause(int actionSource) {
        if (mMediaNotificationInfo.isPaused) return;

        mMediaNotificationInfo = mNotificationInfoBuilder.setPaused(true).build();
        updateNotification();

        mMediaNotificationInfo.listener.onPause(actionSource);
    }

    private void onStop(int actionSource) {
        // hideNotification() below will clear |mMediaNotificationInfo| but {@link
        // MediaNotificationListener}.onStop() might also clear it so keep the listener and call it
        // later.
        // TODO(avayvod): make the notification delegate update its state, not the notification
        // manager. See https://crbug.com/546981
        MediaNotificationListener listener = mMediaNotificationInfo.listener;

        hideNotification(mMediaNotificationInfo.tabId);

        listener.onStop(actionSource);
    }

    private void showNotification(MediaNotificationInfo mediaNotificationInfo) {
        mContext.startService(new Intent(mContext, ListenerService.class));

        assert mediaNotificationInfo != null;

        if (mediaNotificationInfo.equals(mMediaNotificationInfo)) return;

        mMediaNotificationInfo = mediaNotificationInfo;
        updateNotification();
    }

    private void clearNotification() {
        NotificationManagerCompat manager = NotificationManagerCompat.from(mContext);
        manager.cancel(R.id.media_playback_notification);

        if (mMediaSession != null) {
            mMediaSession.setActive(false);
            mMediaSession.release();
            mMediaSession = null;
        }
        mMediaNotificationInfo = null;
        mContext.stopService(new Intent(mContext, ListenerService.class));
    }

    private void hideNotification(int tabId) {
        if (mMediaNotificationInfo == null || tabId != mMediaNotificationInfo.tabId) return;
        clearNotification();
    }

    private RemoteViews createContentView() {
        RemoteViews contentView =
                new RemoteViews(mContext.getPackageName(), R.layout.playback_notification_bar);

        // On Android pre-L, dismissing the notification when the service is no longer in foreground
        // doesn't work. Instead, a STOP button is shown.
        if (mMediaNotificationInfo.supportsSwipeAway()
                && Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP
                || mMediaNotificationInfo.supportsStop()) {
            contentView.setViewVisibility(R.id.stop, View.VISIBLE);
            contentView.setOnClickPendingIntent(R.id.stop,
                    mService.getPendingIntent(ListenerService.ACTION_STOP));
        }
        return contentView;
    }

    private MediaMetadataCompat createMetadata() {
        MediaMetadataCompat.Builder metadataBuilder = new MediaMetadataCompat.Builder();

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            metadataBuilder.putString(MediaMetadataCompat.METADATA_KEY_DISPLAY_TITLE,
                    mMediaNotificationInfo.title);
            metadataBuilder.putString(MediaMetadataCompat.METADATA_KEY_DISPLAY_SUBTITLE,
                    mMediaNotificationInfo.origin);
            metadataBuilder.putBitmap(MediaMetadataCompat.METADATA_KEY_DISPLAY_ICON,
                    mMediaSessionIcon);
        } else {
            metadataBuilder.putString(MediaMetadataCompat.METADATA_KEY_TITLE,
                    mMediaNotificationInfo.title);
            metadataBuilder.putString(MediaMetadataCompat.METADATA_KEY_ARTIST,
                    mMediaNotificationInfo.origin);
            metadataBuilder.putBitmap(MediaMetadataCompat.METADATA_KEY_ART, mMediaSessionIcon);
        }

        return metadataBuilder.build();
    }

    private void updateNotification() {
        if (mService == null) return;

        if (mMediaNotificationInfo == null) {
            // Notification was hidden before we could update it.
            assert mNotificationBuilder == null;
            return;
        }

        // Android doesn't badge the icons for RemoteViews automatically when
        // running the app under the Work profile.
        if (mNotificationIcon == null) {
            Drawable notificationIconDrawable = ApiCompatibilityUtils.getUserBadgedIcon(
                    mContext, mMediaNotificationInfo.icon);
            mNotificationIcon = drawableToBitmap(notificationIconDrawable);
        }

        if (mNotificationBuilder == null) {
            mNotificationBuilder = new NotificationCompat.Builder(mContext)
                .setSmallIcon(mMediaNotificationInfo.icon)
                .setAutoCancel(false)
                .setLocalOnly(true)
                .setDeleteIntent(mService.getPendingIntent(ListenerService.ACTION_STOP));
        }

        if (mMediaNotificationInfo.supportsSwipeAway()) {
            mNotificationBuilder.setOngoing(!mMediaNotificationInfo.isPaused);
        }

        int tabId = mMediaNotificationInfo.tabId;
        Intent tabIntent = Tab.createBringTabToFrontIntent(tabId);
        if (tabIntent != null) {
            mNotificationBuilder
                    .setContentIntent(PendingIntent.getActivity(mContext, tabId, tabIntent, 0));
        }

        RemoteViews contentView = createContentView();
        contentView.setTextViewText(R.id.title, mMediaNotificationInfo.title);
        contentView.setTextViewText(R.id.status, mMediaNotificationInfo.origin);
        if (mNotificationIcon != null) {
            contentView.setImageViewBitmap(R.id.icon, mNotificationIcon);
        } else {
            contentView.setImageViewResource(R.id.icon, mMediaNotificationInfo.icon);
        }

        if (!mMediaNotificationInfo.supportsPlayPause()) {
            contentView.setViewVisibility(R.id.playpause, View.GONE);
        } else if (mMediaNotificationInfo.isPaused) {
            contentView.setImageViewResource(R.id.playpause, R.drawable.ic_vidcontrol_play);
            contentView.setContentDescription(R.id.playpause, mPlayDescription);
            contentView.setOnClickPendingIntent(R.id.playpause,
                    mService.getPendingIntent(ListenerService.ACTION_PLAY));
        } else {
            // If we're here, the notification supports play/pause button and is playing.
            contentView.setImageViewResource(R.id.playpause, R.drawable.ic_vidcontrol_pause);
            contentView.setContentDescription(R.id.playpause, mPauseDescription);
            contentView.setOnClickPendingIntent(R.id.playpause,
                    mService.getPendingIntent(ListenerService.ACTION_PAUSE));
        }

        mNotificationBuilder.setContent(contentView);
        mNotificationBuilder.setVisibility(
                mMediaNotificationInfo.isPrivate ? NotificationCompat.VISIBILITY_PRIVATE
                                                 : NotificationCompat.VISIBILITY_PUBLIC);

        if (mMediaNotificationInfo.supportsPlayPause()) {
            if (mMediaSession == null) mMediaSession = createMediaSession();

            mMediaSession.setMetadata(createMetadata());

            PlaybackStateCompat.Builder playbackStateBuilder = new PlaybackStateCompat.Builder()
                    .setActions(PlaybackStateCompat.ACTION_PLAY | PlaybackStateCompat.ACTION_PAUSE);
            if (mMediaNotificationInfo.isPaused) {
                playbackStateBuilder.setState(PlaybackStateCompat.STATE_PAUSED,
                        PlaybackStateCompat.PLAYBACK_POSITION_UNKNOWN, 1.0f);
            } else {
                // If notification only supports stop, still pretend
                playbackStateBuilder.setState(PlaybackStateCompat.STATE_PLAYING,
                        PlaybackStateCompat.PLAYBACK_POSITION_UNKNOWN, 1.0f);
            }
            mMediaSession.setPlaybackState(playbackStateBuilder.build());
        }

        Notification notification = mNotificationBuilder.build();

        // We keep the service as a foreground service while the media is playing. When it is not,
        // the service isn't stopped but is no longer in foreground, thus at a lower priority.
        // While the service is in foreground, the associated notification can't be swipped away.
        // Moving it back to background allows the user to remove the notification.
        if (mMediaNotificationInfo.supportsSwipeAway() && mMediaNotificationInfo.isPaused) {
            mService.stopForeground(false /* removeNotification */);

            NotificationManagerCompat manager = NotificationManagerCompat.from(mContext);
            manager.notify(R.id.media_playback_notification, notification);
        } else {
            mService.startForeground(R.id.media_playback_notification, notification);
        }
    }

    private MediaSessionCompat createMediaSession() {
        MediaSessionCompat mediaSession = new MediaSessionCompat(
                mContext,
                mContext.getString(R.string.app_name),
                new ComponentName(mContext.getPackageName(),
                        MediaButtonReceiver.class.getName()),
                null);
        mediaSession.setFlags(MediaSessionCompat.FLAG_HANDLES_MEDIA_BUTTONS
                | MediaSessionCompat.FLAG_HANDLES_TRANSPORT_CONTROLS);
        mediaSession.setCallback(mMediaSessionCallback);

        // TODO(mlamouri): the following code is to work around a bug that hopefully
        // MediaSessionCompat will handle directly. see b/24051980.
        try {
            mediaSession.setActive(true);
        } catch (NullPointerException e) {
            // Some versions of KitKat do not support AudioManager.registerMediaButtonIntent
            // with a PendingIntent. They will throw a NullPointerException, in which case
            // they should be able to activate a MediaSessionCompat with only transport
            // controls.
            mediaSession.setActive(false);
            mediaSession.setFlags(MediaSessionCompat.FLAG_HANDLES_TRANSPORT_CONTROLS);
            mediaSession.setActive(true);
        }
        return mediaSession;
    }

    private Bitmap drawableToBitmap(Drawable drawable) {
        if (!(drawable instanceof BitmapDrawable)) return null;

        BitmapDrawable bitmapDrawable = (BitmapDrawable) drawable;
        return bitmapDrawable.getBitmap();
    }
}
