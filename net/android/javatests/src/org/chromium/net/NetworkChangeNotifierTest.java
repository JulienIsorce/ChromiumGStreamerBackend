// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net;

import android.annotation.SuppressLint;
import android.content.Context;
import android.content.Intent;
import android.net.ConnectivityManager;
import android.net.ConnectivityManager.NetworkCallback;
import android.net.Network;
import android.net.NetworkRequest;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.telephony.TelephonyManager;
import android.test.InstrumentationTestCase;
import android.test.UiThreadTest;
import android.test.suitebuilder.annotation.MediumTest;

import org.chromium.base.ApplicationState;
import org.chromium.base.ThreadUtils;
import org.chromium.base.library_loader.LibraryLoader;
import org.chromium.base.library_loader.LibraryProcessType;
import org.chromium.base.test.util.Feature;
import org.chromium.net.NetworkChangeNotifierAutoDetect.ConnectivityManagerDelegate;
import org.chromium.net.NetworkChangeNotifierAutoDetect.NetworkState;
import org.chromium.net.test.util.NetworkChangeNotifierTestUtil;

import java.lang.reflect.Constructor;
import java.util.concurrent.Callable;
import java.util.concurrent.FutureTask;

/**
 * Tests for org.chromium.net.NetworkChangeNotifier.
 */
@SuppressLint("NewApi")
public class NetworkChangeNotifierTest extends InstrumentationTestCase {
    /**
     * Listens for alerts fired by the NetworkChangeNotifier when network status changes.
     */
    private static class NetworkChangeNotifierTestObserver
            implements NetworkChangeNotifier.ConnectionTypeObserver {
        private boolean mReceivedNotification = false;

        @Override
        public void onConnectionTypeChanged(int connectionType) {
            mReceivedNotification = true;
        }

        public boolean hasReceivedNotification() {
            return mReceivedNotification;
        }

        public void resetHasReceivedNotification() {
            mReceivedNotification = false;
        }
    }

    /**
     * Mocks out calls to the ConnectivityManager.
     */
    private static class MockConnectivityManagerDelegate extends ConnectivityManagerDelegate {
        private boolean mActiveNetworkExists;
        private int mNetworkType;
        private int mNetworkSubtype;
        private NetworkCallback mLastRegisteredNetworkCallback;

        @Override
        public NetworkState getNetworkState() {
            return new NetworkState(mActiveNetworkExists, mNetworkType, mNetworkSubtype);
        }

        // Dummy implementations to avoid NullPointerExceptions in default implementations:

        @Override
        public int getDefaultNetId() {
            return NetId.INVALID;
        }

        @Override
        public Network[] getAllNetworks() {
            return new Network[0];
        }

        @Override
        public NetworkState getNetworkState(Network network) {
            return new NetworkState(false, -1, -1);
        }

        @Override
        public void unregisterNetworkCallback(NetworkCallback networkCallback) {}

        // Dummy implementation that also records the last registered callback.
        @Override
        public void registerNetworkCallback(
                NetworkRequest networkRequest, NetworkCallback networkCallback) {
            mLastRegisteredNetworkCallback = networkCallback;
        }

        public void setActiveNetworkExists(boolean networkExists) {
            mActiveNetworkExists = networkExists;
        }

        public void setNetworkType(int networkType) {
            mNetworkType = networkType;
        }

        public void setNetworkSubtype(int networkSubtype) {
            mNetworkSubtype = networkSubtype;
        }

        public NetworkCallback getLastRegisteredNetworkCallback() {
            return mLastRegisteredNetworkCallback;
        }
    }

    /**
     * Mocks out calls to the WifiManager.
     */
    private static class MockWifiManagerDelegate
            extends NetworkChangeNotifierAutoDetect.WifiManagerDelegate {
        private String mWifiSSID;
        private int mLinkSpeedMbps;

        @Override
        public String getWifiSSID() {
            return mWifiSSID;
        }

        public void setWifiSSID(String wifiSSID) {
            mWifiSSID = wifiSSID;
        }

        @Override
        public int getLinkSpeedInMbps() {
            return mLinkSpeedMbps;
        }

        public void setLinkSpeedInMbps(int linkSpeedInMbps) {
            mLinkSpeedMbps = linkSpeedInMbps;
        }
    }

    // Types of network changes. Each is associated with a NetworkChangeNotifierAutoDetect.Observer
    // callback, and NONE is provided to indicate no callback observed.
    private static enum ChangeType { NONE, CONNECT, SOON_TO_DISCONNECT, DISCONNECT, PURGE_LIST }

    // NetworkChangeNotifierAutoDetect.Observer used to verify proper notifications are sent out.
    // Notifications come back on UI thread. assertLastChange() called on test thread.
    private static class TestNetworkChangeNotifierAutoDetectObserver
            implements NetworkChangeNotifierAutoDetect.Observer {
        private volatile ChangeType mLastChangeSeen = ChangeType.NONE;
        private volatile int mLastNetIdSeen = NetId.INVALID;

        @Override
        public void onConnectionTypeChanged(int newConnectionType) {}
        @Override
        public void onMaxBandwidthChanged(double maxBandwidthMbps) {}

        @Override
        public void onNetworkConnect(int netId, int connectionType) {
            ThreadUtils.assertOnUiThread();
            assertEquals(mLastChangeSeen, ChangeType.NONE);
            assertEquals(mLastNetIdSeen, NetId.INVALID);
            mLastChangeSeen = ChangeType.CONNECT;
            mLastNetIdSeen = netId;
        }

        @Override
        public void onNetworkSoonToDisconnect(int netId) {
            ThreadUtils.assertOnUiThread();
            assertEquals(mLastChangeSeen, ChangeType.NONE);
            assertEquals(mLastNetIdSeen, NetId.INVALID);
            mLastChangeSeen = ChangeType.SOON_TO_DISCONNECT;
            mLastNetIdSeen = netId;
        }

        @Override
        public void onNetworkDisconnect(int netId) {
            ThreadUtils.assertOnUiThread();
            assertEquals(mLastChangeSeen, ChangeType.NONE);
            assertEquals(mLastNetIdSeen, NetId.INVALID);
            mLastChangeSeen = ChangeType.DISCONNECT;
            mLastNetIdSeen = netId;
        }

        @Override
        public void updateActiveNetworkList(int[] activeNetIds) {
            ThreadUtils.assertOnUiThread();
            assertEquals(mLastChangeSeen, ChangeType.NONE);
            assertEquals(mLastNetIdSeen, NetId.INVALID);
            mLastChangeSeen = ChangeType.PURGE_LIST;
            if (activeNetIds.length >= 1) {
                mLastNetIdSeen = activeNetIds[0];
            } else {
                mLastNetIdSeen = NetId.INVALID;
            }
        }

        // Verify last notification was the expected one.
        public void assertLastChange(ChangeType type, int netId) throws Exception {
            // Make sure notification processed.
            NetworkChangeNotifierTestUtil.flushUiThreadTaskQueue();
            assertEquals(type, mLastChangeSeen);
            assertEquals(netId, mLastNetIdSeen);
            mLastChangeSeen = ChangeType.NONE;
            mLastNetIdSeen = NetId.INVALID;
        }
    }

    // Network.Network(int netId) pointer.
    private Constructor<Network> mNetworkConstructor;
    private NetworkChangeNotifierAutoDetect mReceiver;
    private MockConnectivityManagerDelegate mConnectivityDelegate;
    private MockWifiManagerDelegate mWifiDelegate;

    private static enum WatchForChanges {
        ALWAYS,
        ONLY_WHEN_APP_IN_FOREGROUND,
    }

    /**
     * Helper method to create a notifier and delegates for testing.
     * @param watchForChanges indicates whether app wants to watch for changes always or only when
     *            it is in the foreground.
     */
    private void createTestNotifier(WatchForChanges watchForChanges) {
        Context context = getInstrumentation().getTargetContext();
        NetworkChangeNotifier.resetInstanceForTests(context);
        if (watchForChanges == WatchForChanges.ALWAYS) {
            NetworkChangeNotifier.registerToReceiveNotificationsAlways();
        } else {
            NetworkChangeNotifier.setAutoDetectConnectivityState(true);
        }
        mReceiver = NetworkChangeNotifier.getAutoDetectorForTest();
        assertNotNull(mReceiver);

        mConnectivityDelegate =
                new MockConnectivityManagerDelegate();
        mConnectivityDelegate.setActiveNetworkExists(true);
        mReceiver.setConnectivityManagerDelegateForTests(mConnectivityDelegate);

        mWifiDelegate = new MockWifiManagerDelegate();
        mReceiver.setWifiManagerDelegateForTests(mWifiDelegate);
        mWifiDelegate.setWifiSSID("foo");
    }

    private double getCurrentMaxBandwidthInMbps() {
        final NetworkChangeNotifierAutoDetect.NetworkState networkState =
                mReceiver.getCurrentNetworkState();
        return mReceiver.getCurrentMaxBandwidthInMbps(networkState);
    }

    private int getCurrentConnectionType() {
        final NetworkChangeNotifierAutoDetect.NetworkState networkState =
                mReceiver.getCurrentNetworkState();
        return mReceiver.getCurrentConnectionType(networkState);
    }

    // Create Network object given a NetID.
    private Network netIdToNetwork(int netId) throws Exception {
        return mNetworkConstructor.newInstance(netId);
    }

    @Override
    protected void setUp() throws Exception {
        super.setUp();
        LibraryLoader.get(LibraryProcessType.PROCESS_BROWSER)
                .ensureInitialized(getInstrumentation().getTargetContext());
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            // Find Network.Network(int netId) using reflection.
            mNetworkConstructor = Network.class.getConstructor(Integer.TYPE);
        }
        ThreadUtils.postOnUiThread(new Runnable() {
            public void run() {
                createTestNotifier(WatchForChanges.ONLY_WHEN_APP_IN_FOREGROUND);
            }
        });
    }

    /**
     * Tests that the receiver registers for connectivity intents during construction.
     */
    @UiThreadTest
    @MediumTest
    @Feature({"Android-AppBase"})
    public void testNetworkChangeNotifierRegistersInConstructor() throws InterruptedException {
        Context context = getInstrumentation().getTargetContext();

        NetworkChangeNotifierAutoDetect.Observer observer =
                new TestNetworkChangeNotifierAutoDetectObserver();

        NetworkChangeNotifierAutoDetect receiver = new NetworkChangeNotifierAutoDetect(
                observer, context, new RegistrationPolicyApplicationStatus() {
                    @Override
                    int getApplicationState() {
                        return ApplicationState.HAS_RUNNING_ACTIVITIES;
                    }
                });

        assertTrue(receiver.isReceiverRegisteredForTesting());
    }

    /**
     * Tests that the receiver toggles registration for connectivity intents based on activity
     * state.
     */
    @UiThreadTest
    @MediumTest
    @Feature({"Android-AppBase"})
    public void testNetworkChangeNotifierRegistersForIntents() throws InterruptedException {
        RegistrationPolicyApplicationStatus policy =
                (RegistrationPolicyApplicationStatus) mReceiver.getRegistrationPolicy();
        policy.onApplicationStateChange(ApplicationState.HAS_RUNNING_ACTIVITIES);
        assertTrue(mReceiver.isReceiverRegisteredForTesting());

        policy.onApplicationStateChange(ApplicationState.HAS_PAUSED_ACTIVITIES);
        assertFalse(mReceiver.isReceiverRegisteredForTesting());

        policy.onApplicationStateChange(ApplicationState.HAS_RUNNING_ACTIVITIES);
        assertTrue(mReceiver.isReceiverRegisteredForTesting());
    }

    /**
     * Tests that changing the RSSI_CHANGED_ACTION intent updates MaxBandwidth.
     */
    @UiThreadTest
    @MediumTest
    @Feature({"Android-AppBase"})
    public void testNetworkChangeNotifierRSSIEventUpdatesMaxBandwidthForWiFi()
            throws InterruptedException {
        NetworkChangeNotifier notifier = NetworkChangeNotifier.getInstance();
        mConnectivityDelegate.setNetworkType(ConnectivityManager.TYPE_WIFI);
        mWifiDelegate.setLinkSpeedInMbps(42);
        Intent intent = new Intent(WifiManager.RSSI_CHANGED_ACTION);
        mReceiver.onReceive(getInstrumentation().getTargetContext(), intent);

        assertEquals(42.0, notifier.getCurrentMaxBandwidthInMbps());

        // Changing the link speed has no effect until the intent fires.
        mWifiDelegate.setLinkSpeedInMbps(80);
        assertEquals(42.0, notifier.getCurrentMaxBandwidthInMbps());

        // Fire the intent.
        mReceiver.onReceive(getInstrumentation().getTargetContext(), intent);
        assertEquals(80.0, notifier.getCurrentMaxBandwidthInMbps());

        // Firing a network type change intent also causes max bandwidth to update.
        mWifiDelegate.setLinkSpeedInMbps(20);
        intent = new Intent(ConnectivityManager.CONNECTIVITY_ACTION);
        mReceiver.onReceive(getInstrumentation().getTargetContext(), intent);
        assertEquals(20.0, notifier.getCurrentMaxBandwidthInMbps());
    }

    /**
     * Tests that changing the network type changes the maxBandwidth.
     */
    @UiThreadTest
    @MediumTest
    @Feature({"Android-AppBase"})
    public void testNetworkChangeNotifierMaxBandwidthEthernet() throws InterruptedException {
        // Show that for Ethernet the link speed is unknown (+Infinity).
        mConnectivityDelegate.setNetworkType(ConnectivityManager.TYPE_ETHERNET);
        assertEquals(ConnectionType.CONNECTION_ETHERNET, getCurrentConnectionType());
        assertEquals(Double.POSITIVE_INFINITY, getCurrentMaxBandwidthInMbps());
    }

    @UiThreadTest
    @MediumTest
    @Feature({"Android-AppBase"})
    public void testNetworkChangeNotifierMaxBandwidthWifi() throws InterruptedException {
        // Test that for wifi types the link speed is read from the WifiManager.
        mWifiDelegate.setLinkSpeedInMbps(42);
        mConnectivityDelegate.setNetworkType(ConnectivityManager.TYPE_WIFI);
        assertEquals(ConnectionType.CONNECTION_WIFI, getCurrentConnectionType());
        assertEquals(42.0, getCurrentMaxBandwidthInMbps());
    }

    @UiThreadTest
    @MediumTest
    @Feature({"Android-AppBase"})
    public void testNetworkChangeNotifierMaxBandwidthWiMax() throws InterruptedException {
        // Show that for WiMax the link speed is unknown (+Infinity), although the type is 4g.
        // TODO(jkarlin): Add support for CONNECTION_WIMAX as specified in
        // http://w3c.github.io/netinfo/.
        mConnectivityDelegate.setNetworkType(ConnectivityManager.TYPE_WIMAX);
        assertEquals(ConnectionType.CONNECTION_4G, getCurrentConnectionType());
        assertEquals(Double.POSITIVE_INFINITY, getCurrentMaxBandwidthInMbps());
    }

    @UiThreadTest
    @MediumTest
    @Feature({"Android-AppBase"})
    public void testNetworkChangeNotifierMaxBandwidthBluetooth() throws InterruptedException {
        // Show that for bluetooth the link speed is unknown (+Infinity).
        mConnectivityDelegate.setNetworkType(ConnectivityManager.TYPE_BLUETOOTH);
        assertEquals(ConnectionType.CONNECTION_BLUETOOTH, getCurrentConnectionType());
        assertEquals(Double.POSITIVE_INFINITY, getCurrentMaxBandwidthInMbps());
    }

    @UiThreadTest
    @MediumTest
    @Feature({"Android-AppBase"})
    public void testNetworkChangeNotifierMaxBandwidthMobile() throws InterruptedException {
        // Test that for mobile types the subtype is used to determine the maxBandwidth.
        mConnectivityDelegate.setNetworkType(ConnectivityManager.TYPE_MOBILE);
        mConnectivityDelegate.setNetworkSubtype(TelephonyManager.NETWORK_TYPE_LTE);
        assertEquals(ConnectionType.CONNECTION_4G, getCurrentConnectionType());
        assertEquals(100.0, getCurrentMaxBandwidthInMbps());
    }

    /**
     * Tests that when Chrome gets an intent indicating a change in network connectivity, it sends a
     * notification to Java observers.
     */
    @UiThreadTest
    @MediumTest
    @Feature({"Android-AppBase"})
    public void testNetworkChangeNotifierJavaObservers() throws InterruptedException {
        // Initialize the NetworkChangeNotifier with a connection.
        Intent connectivityIntent = new Intent(ConnectivityManager.CONNECTIVITY_ACTION);
        mReceiver.onReceive(getInstrumentation().getTargetContext(), connectivityIntent);

        // We shouldn't be re-notified if the connection hasn't actually changed.
        NetworkChangeNotifierTestObserver observer = new NetworkChangeNotifierTestObserver();
        NetworkChangeNotifier.addConnectionTypeObserver(observer);
        mReceiver.onReceive(getInstrumentation().getTargetContext(), connectivityIntent);
        assertFalse(observer.hasReceivedNotification());

        // We shouldn't be notified if we're connected to non-Wifi and the Wifi SSID changes.
        mWifiDelegate.setWifiSSID("bar");
        mReceiver.onReceive(getInstrumentation().getTargetContext(), connectivityIntent);
        assertFalse(observer.hasReceivedNotification());
        // We should be notified when we change to Wifi.
        mConnectivityDelegate.setNetworkType(ConnectivityManager.TYPE_WIFI);
        mReceiver.onReceive(getInstrumentation().getTargetContext(), connectivityIntent);
        assertTrue(observer.hasReceivedNotification());
        observer.resetHasReceivedNotification();
        // We should be notified when the Wifi SSID changes.
        mWifiDelegate.setWifiSSID("foo");
        mReceiver.onReceive(getInstrumentation().getTargetContext(), connectivityIntent);
        assertTrue(observer.hasReceivedNotification());
        observer.resetHasReceivedNotification();
        // We shouldn't be re-notified if the Wifi SSID hasn't actually changed.
        mReceiver.onReceive(getInstrumentation().getTargetContext(), connectivityIntent);
        assertFalse(observer.hasReceivedNotification());

        // Mimic that connectivity has been lost and ensure that Chrome notifies our observer.
        mConnectivityDelegate.setActiveNetworkExists(false);
        Intent noConnectivityIntent = new Intent(ConnectivityManager.CONNECTIVITY_ACTION);
        mReceiver.onReceive(getInstrumentation().getTargetContext(), noConnectivityIntent);
        assertTrue(observer.hasReceivedNotification());

        observer.resetHasReceivedNotification();
        // Pretend we got moved to the background.
        final RegistrationPolicyApplicationStatus policy =
                (RegistrationPolicyApplicationStatus) mReceiver.getRegistrationPolicy();
        policy.onApplicationStateChange(ApplicationState.HAS_PAUSED_ACTIVITIES);
        // Change the state.
        mConnectivityDelegate.setActiveNetworkExists(true);
        mConnectivityDelegate.setNetworkType(ConnectivityManager.TYPE_WIFI);
        // The NetworkChangeNotifierAutoDetect doesn't receive any notification while we are in the
        // background, but when we get back to the foreground the state changed should be detected
        // and a notification sent.
        policy.onApplicationStateChange(ApplicationState.HAS_RUNNING_ACTIVITIES);
        assertTrue(observer.hasReceivedNotification());
    }

    /**
     * Tests that when setting {@code registerToReceiveNotificationsAlways()},
     * a NetworkChangeNotifierAutoDetect object is successfully created.
     */
    @UiThreadTest
    @MediumTest
    @Feature({"Android-AppBase"})
    public void testCreateNetworkChangeNotifierAlwaysWatchForChanges() throws InterruptedException {
        createTestNotifier(WatchForChanges.ALWAYS);
        assertTrue(mReceiver.isReceiverRegisteredForTesting());

        // Make sure notifications can be received.
        NetworkChangeNotifierTestObserver observer = new NetworkChangeNotifierTestObserver();
        NetworkChangeNotifier.addConnectionTypeObserver(observer);
        Intent connectivityIntent = new Intent(ConnectivityManager.CONNECTIVITY_ACTION);
        mReceiver.onReceive(getInstrumentation().getTargetContext(), connectivityIntent);
        assertTrue(observer.hasReceivedNotification());
    }

    /**
     * Tests that ConnectivityManagerDelegate doesn't crash. This test cannot rely on having any
     * active network connections so it cannot usefully check results, but it can at least check
     * that the functions don't crash.
     */
    @UiThreadTest
    @MediumTest
    @Feature({"Android-AppBase"})
    public void testConnectivityManagerDelegateDoesNotCrash() {
        ConnectivityManagerDelegate delegate =
                new ConnectivityManagerDelegate(getInstrumentation().getTargetContext());
        delegate.getNetworkState();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            Network[] networks = delegate.getAllNetworks();
            if (networks.length >= 1) {
                delegate.getNetworkState(networks[0]);
                delegate.hasInternetCapability(networks[0]);
            }
            delegate.getDefaultNetId();
            NetworkCallback networkCallback = new NetworkCallback();
            NetworkRequest networkRequest = new NetworkRequest.Builder().build();
            delegate.registerNetworkCallback(networkRequest, networkCallback);
            delegate.unregisterNetworkCallback(networkCallback);
        }
    }

    /**
     * Tests that NetworkChangeNotifierAutoDetect queryable APIs don't crash. This test cannot rely
     * on having any active network connections so it cannot usefully check results, but it can at
     * least check that the functions don't crash.
     */
    @UiThreadTest
    @MediumTest
    @Feature({"Android-AppBase"})
    public void testQueryableAPIsDoNotCrash() {
        NetworkChangeNotifierAutoDetect.Observer observer =
                new TestNetworkChangeNotifierAutoDetectObserver();
        NetworkChangeNotifierAutoDetect ncn = new NetworkChangeNotifierAutoDetect(observer,
                getInstrumentation().getTargetContext(), new RegistrationPolicyAlwaysRegister());
        ncn.getNetworksAndTypes();
        ncn.getDefaultNetId();
    }

    /**
     * Tests that callbacks are issued to Observers when NetworkChangeNotifierAutoDetect receives
     * the right signals (via its NetworkCallback).
     */
    @MediumTest
    @Feature({"Android-AppBase"})
    public void testNetworkCallbacks() throws Exception {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) {
            return;
        }
        // Setup NetworkChangeNotifierAutoDetect
        final Context context = getInstrumentation().getTargetContext();
        final TestNetworkChangeNotifierAutoDetectObserver observer =
                new TestNetworkChangeNotifierAutoDetectObserver();
        Callable<NetworkChangeNotifierAutoDetect> callable =
                new Callable<NetworkChangeNotifierAutoDetect>() {
                    public NetworkChangeNotifierAutoDetect call() {
                        return new NetworkChangeNotifierAutoDetect(
                                observer, context, new RegistrationPolicyApplicationStatus() {
                                    // This override prevents NetworkChangeNotifierAutoDetect from
                                    // registering for events right off the bat. We'll delay this
                                    // until our MockConnectivityManagerDelegate is first installed
                                    // to prevent inadvertent communication with the real
                                    // ConnectivityManager.
                                    @Override
                                    int getApplicationState() {
                                        return ApplicationState.HAS_PAUSED_ACTIVITIES;
                                    }
                                });
                    }
                };
        FutureTask<NetworkChangeNotifierAutoDetect> task =
                new FutureTask<NetworkChangeNotifierAutoDetect>(callable);
        ThreadUtils.postOnUiThread(task);
        NetworkChangeNotifierAutoDetect ncn = task.get();

        // Insert mock ConnectivityDelegate
        mConnectivityDelegate = new MockConnectivityManagerDelegate();
        ncn.setConnectivityManagerDelegateForTests(mConnectivityDelegate);
        // Now that mock ConnectivityDelegate is inserted, pretend app is foregrounded
        // so NetworkChangeNotifierAutoDetect will register its NetworkCallback.
        assertFalse(ncn.isReceiverRegisteredForTesting());

        RegistrationPolicyApplicationStatus policy =
                (RegistrationPolicyApplicationStatus) mReceiver.getRegistrationPolicy();
        policy.onApplicationStateChange(ApplicationState.HAS_RUNNING_ACTIVITIES);
        assertTrue(ncn.isReceiverRegisteredForTesting());

        // Find NetworkChangeNotifierAutoDetect's NetworkCallback, which should have been registered
        // with mConnectivityDelegate.
        NetworkCallback networkCallback = mConnectivityDelegate.getLastRegisteredNetworkCallback();
        assertNotNull(networkCallback);

        // First thing we'll receive is a purge to initialize any network lists.
        observer.assertLastChange(ChangeType.PURGE_LIST, NetId.INVALID);

        // Test connected signal is passed along.
        networkCallback.onAvailable(netIdToNetwork(100));
        observer.assertLastChange(ChangeType.CONNECT, 100);

        // Test soon-to-be-disconnected signal is passed along.
        networkCallback.onLosing(netIdToNetwork(101), 30);
        observer.assertLastChange(ChangeType.SOON_TO_DISCONNECT, 101);

        // Test connected signal is passed along.
        networkCallback.onLost(netIdToNetwork(102));
        observer.assertLastChange(ChangeType.DISCONNECT, 102);

        // Simulate app backgrounding then foregrounding.
        assertTrue(ncn.isReceiverRegisteredForTesting());
        policy.onApplicationStateChange(ApplicationState.HAS_PAUSED_ACTIVITIES);
        assertFalse(ncn.isReceiverRegisteredForTesting());
        policy.onApplicationStateChange(ApplicationState.HAS_RUNNING_ACTIVITIES);
        assertTrue(ncn.isReceiverRegisteredForTesting());
        // Verify network list purged.
        observer.assertLastChange(ChangeType.PURGE_LIST, NetId.INVALID);
    }
}
