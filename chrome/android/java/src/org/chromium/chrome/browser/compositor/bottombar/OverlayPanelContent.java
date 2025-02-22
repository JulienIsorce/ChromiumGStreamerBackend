// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.compositor.bottombar;

import org.chromium.base.VisibleForTesting;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.chrome.browser.ChromeActivity;
import org.chromium.chrome.browser.WebContentsFactory;
import org.chromium.chrome.browser.contextualsearch.ContextualSearchManager;
import org.chromium.chrome.browser.externalnav.ExternalNavigationHandler;
import org.chromium.components.navigation_interception.InterceptNavigationDelegate;
import org.chromium.components.navigation_interception.NavigationParams;
import org.chromium.components.web_contents_delegate_android.WebContentsDelegateAndroid;
import org.chromium.content.browser.ContentView;
import org.chromium.content.browser.ContentViewClient;
import org.chromium.content.browser.ContentViewCore;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.content_public.browser.WebContents;
import org.chromium.content_public.browser.WebContentsObserver;

/**
 * Content container for an OverlayPanel. This class is responsible for the management of the
 * ContentViewCore displayed inside of a panel and exposes a simple API relevant to actions a
 * panel has.
 */
public class OverlayPanelContent {

    /**
     * The ContentViewCore that this panel will display.
     */
    private ContentViewCore mContentViewCore;

    /**
     * The pointer to the native version of this class.
     */
    private long mNativeOverlayPanelContentPtr;

    /**
     * Used for progress bar events.
     */
    private final WebContentsDelegateAndroid mWebContentsDelegate;

    /**
     * The activity that this content is contained in.
     */
    private ChromeActivity mActivity;

    /**
     * Observer used for tracking loading and navigation.
     */
    private WebContentsObserver mWebContentsObserver;

    /**
     * Whether the ContentViewCore has started loading a URL.
     */
    private boolean mDidStartLoadingUrl;

    /**
     * Whether the ContentViewCore is processing a pending navigation.
     * NOTE(pedrosimonetti): This is being used to prevent redirections on the SERP to
     * interpreted as a regular navigation, which should cause the Contextual Search Panel
     * to be promoted as a Tab. This was added to work around a server bug that has been fixed.
     * Just checking for whether the Content has been touched is enough to determine whether a
     * navigation should be promoted (assuming it was caused by the touch), as done in
     * {@link ContextualSearchManager#shouldPromoteSearchNavigation()}.
     * For more details, see crbug.com/441048
     * TODO(pedrosimonetti): remove this from M48 or move it to Contextual Search Panel.
     */
    private boolean mIsProcessingPendingNavigation;

    /**
     * Whether the content view is currently being displayed.
     */
    private boolean mIsContentViewShowing;

    /**
     * The ContentViewCore responsible for displaying content.
     */
    private ContentViewClient mContentViewClient;

    /**
     * The observer used by this object to inform implementers of different events.
     */
    private OverlayContentDelegate mContentDelegate;

    /**
     * Used to observe progress bar events.
     */
    private OverlayContentProgressObserver mProgressObserver;

    // http://crbug.com/522266 : An instance of InterceptNavigationDelegateImpl should be kept in
    // java layer. Otherwise, the instance could be garbage-collected unexpectedly.
    private InterceptNavigationDelegate mInterceptNavigationDelegate;

    // ============================================================================================
    // InterceptNavigationDelegateImpl
    // ============================================================================================

    // Used to intercept intent navigations.
    // TODO(jeremycho): Consider creating a Tab with the Panel's ContentViewCore,
    // which would also handle functionality like long-press-to-paste.
    private class InterceptNavigationDelegateImpl implements InterceptNavigationDelegate {
        final ExternalNavigationHandler mExternalNavHandler = new ExternalNavigationHandler(
                mActivity);
        @Override
        public boolean shouldIgnoreNavigation(NavigationParams navigationParams) {
            // TODO(mdjones): Rather than passing the two navigation params, instead consider
            // passing a boolean to make this API simpler.
            return !mContentDelegate.shouldInterceptNavigation(mExternalNavHandler,
                    navigationParams);
        }
    }

    // ============================================================================================
    // Constructor
    // ============================================================================================

    /**
     * @param contentDelegate An observer for events that occur on this content. If null is passed
     *                        for this parameter, the default one will be used.
     * @param progressObserver An observer for progress related events.
     * @param activity The ChromeActivity that contains this object.
     */
    public OverlayPanelContent(OverlayContentDelegate contentDelegate,
            OverlayContentProgressObserver progressObserver, ChromeActivity activity) {
        mNativeOverlayPanelContentPtr = nativeInit();
        mContentDelegate = contentDelegate;
        mProgressObserver = progressObserver;
        mActivity = activity;

        mWebContentsDelegate = new WebContentsDelegateAndroid() {
            @Override
            public void onLoadStarted() {
                super.onLoadStarted();
                mProgressObserver.onProgressBarStarted();
            }

            @Override
            public void onLoadStopped() {
                super.onLoadStopped();
                mProgressObserver.onProgressBarFinished();
            }

            @Override
            public void onLoadProgressChanged(int progress) {
                super.onLoadProgressChanged(progress);
                mProgressObserver.onProgressBarUpdated(progress);
            }
        };
    }

    // ============================================================================================
    // ContentViewCore related
    // ============================================================================================

    /**
     * Create a new ContentViewCore that will be managed by this panel.
     */
    private void createNewContentView() {
        if (mContentViewCore != null) {
            // If the ContentViewCore has already been created, but never used,
            // then there's no need to create a new one.
            if (!mDidStartLoadingUrl) return;

            destroyContentView();
        }

        mContentViewCore = new ContentViewCore(mActivity);

        if (mContentViewClient == null) {
            mContentViewClient = new ContentViewClient();
        }

        mContentViewCore.setContentViewClient(mContentViewClient);

        ContentView cv = ContentView.createContentView(mActivity, mContentViewCore);
        // Creates an initially hidden WebContents which gets shown when the panel is opened.
        mContentViewCore.initialize(cv, cv,
                WebContentsFactory.createWebContents(false, true), mActivity.getWindowAndroid());

        // Transfers the ownership of the WebContents to the native OverlayPanelContent.
        nativeSetWebContents(mNativeOverlayPanelContentPtr, mContentViewCore,
                mWebContentsDelegate);

        mWebContentsObserver =
                new WebContentsObserver(mContentViewCore.getWebContents()) {
                    @Override
                    public void didStartLoading(String url) {
                        mContentDelegate.onContentLoadStarted(url);
                    }

                    @Override
                    public void didStartProvisionalLoadForFrame(long frameId, long parentFrameId,
                            boolean isMainFrame, String validatedUrl, boolean isErrorPage,
                            boolean isIframeSrcdoc) {
                        if (isMainFrame) {
                            mContentDelegate.onMainFrameLoadStarted(validatedUrl);
                        }
                    }

                    @Override
                    public void didNavigateMainFrame(String url, String baseUrl,
                            boolean isNavigationToDifferentPage, boolean isNavigationInPage,
                            int httpResultCode) {
                        mIsProcessingPendingNavigation = false;
                        mContentDelegate.onMainFrameNavigation(url,
                                isHttpFailureCode(httpResultCode));
                    }

                    @Override
                    public void didFinishLoad(long frameId, String validatedUrl,
                            boolean isMainFrame) {
                        mContentDelegate.onContentLoadFinished();
                    }
                };

        mInterceptNavigationDelegate = new InterceptNavigationDelegateImpl();
        nativeSetInterceptNavigationDelegate(mNativeOverlayPanelContentPtr,
                mInterceptNavigationDelegate, mContentViewCore.getWebContents());

        mContentDelegate.onContentViewCreated(mContentViewCore);
    }

    /**
     * Destroy this panel's ContentViewCore.
     */
    private void destroyContentView() {
        if (mContentViewCore != null) {
            nativeDestroyWebContents(mNativeOverlayPanelContentPtr);
            mContentViewCore.getWebContents().destroy();
            mContentViewCore.destroy();
            mContentViewCore = null;
            if (mWebContentsObserver != null) {
                mWebContentsObserver.destroy();
                mWebContentsObserver = null;
            }

            mDidStartLoadingUrl = false;
            mIsProcessingPendingNavigation = false;

            setVisibility(false);

            // After everything has been disposed, notify the observer.
            mContentDelegate.onContentViewDestroyed();
        }
    }

    /**
     * @return Whether the ContentViewCore was created.
     */
    @VisibleForTesting
    public boolean didCreateContentView() {
        return mContentViewCore != null;
    }

    /**
     * Load a URL, this will trigger creation of a new ContentViewCore.
     * @param url The URL that should be loaded.
     */
    public void loadUrl(String url) {
        createNewContentView();

        if (mContentViewCore != null && mContentViewCore.getWebContents() != null) {
            mDidStartLoadingUrl = true;
            mIsProcessingPendingNavigation = true;
            mContentViewCore.getWebContents().getNavigationController().loadUrl(
                    new LoadUrlParams(url));
        }
    }

    /**
     * Notifies that the Panel has been touched. Calling this method will turn the Content
     * visible, causing it to be rendered.
     */
    public void notifyPanelTouched() {
        setVisibility(true);
    }

    // ============================================================================================
    // Utilities
    // ============================================================================================

    /**
     * Calls updateTopControlsState on the ContentViewCore.
     * @param enableHiding Enable the toolbar's ability to hide.
     * @param enableShowing If the toolbar is allowed to show.
     * @param animate If the toolbar should animate when showing/hiding.
     */
    public void updateTopControlsState(boolean enableHiding, boolean enableShowing,
            boolean animate) {
        if (mContentViewCore != null && mContentViewCore.getWebContents() != null) {
            mContentViewCore.getWebContents().updateTopControlsState(enableHiding, enableShowing,
                    animate);
        }
    }

    /**
     * @return Whether a pending navigation if being processed.
     */
    public boolean isProcessingPendingNavigation() {
        return mIsProcessingPendingNavigation;
    }

    /**
     * Reset the ContentViewCore's scroll position to (0, 0).
     */
    public void resetContentViewScroll() {
        if (mContentViewCore != null) {
            mContentViewCore.scrollTo(0, 0);
        }
    }

    /**
     * @return The Y scroll position.
     */
    public float getContentVerticalScroll() {
        return mContentViewCore != null
                ? mContentViewCore.computeVerticalScrollOffset() : -1.f;
    }

    /**
     * Sets the visibility of the Search Content View.
     * @param isVisible True to make it visible.
     */
    private void setVisibility(boolean isVisible) {
        if (mIsContentViewShowing == isVisible) return;

        mIsContentViewShowing = isVisible;

        if (isVisible) {
            // The CVC is created with the search request, but if none was made we'll need
            // one in order to display an empty panel.
            if (mContentViewCore == null) {
                createNewContentView();
            }

            // NOTE(pedrosimonetti): Calling onShow() on the ContentViewCore will cause the page
            // to be rendered. This has a side effect of causing the page to be included in
            // your Web History (if enabled). For this reason, onShow() should only be called
            // when we know for sure the page will be seen by the user.
            if (mContentViewCore != null) mContentViewCore.onShow();

            mContentDelegate.onContentViewSeen();
        } else {
            if (mContentViewCore != null) mContentViewCore.onHide();
        }

        mContentDelegate.onVisibilityChanged(isVisible);
    }

    /**
     * @return Whether the given HTTP result code represents a failure or not.
     */
    private boolean isHttpFailureCode(int httpResultCode) {
        return httpResultCode <= 0 || httpResultCode >= 400;
    }

    /**
     * Set a ContentViewClient for this panel to use (will be reused for each new ContentViewCore).
     * @param viewClient The ContentViewClient to use.
     */
    public void setContentViewClient(ContentViewClient viewClient) {
        mContentViewClient = viewClient;
        if (mContentViewCore != null) {
            mContentViewCore.setContentViewClient(mContentViewClient);
        }
    }

    /**
     * @return true if the ContentViewCore is visible on the page.
     */
    public boolean isContentShowing() {
        return mIsContentViewShowing;
    }

    // ============================================================================================
    // Methods for managing this panel's ContentViewCore.
    // ============================================================================================

    /**
     * Reset this object's native pointer to 0;
     */
    @CalledByNative
    private void clearNativePanelContentPtr() {
        assert mNativeOverlayPanelContentPtr != 0;
        mNativeOverlayPanelContentPtr = 0;
    }

    /**
     * @return This panel's ContentViewCore.
     */
    @VisibleForTesting
    public ContentViewCore getContentViewCore() {
        return mContentViewCore;
    }

    /**
     * Remove the list history entry from this panel if it was within a certain timeframe.
     * @param historyUrl The URL to remove.
     * @param urlTimeMs The time the URL was navigated to.
     */
    public void removeLastHistoryEntry(String historyUrl, long urlTimeMs) {
        nativeRemoveLastHistoryEntry(mNativeOverlayPanelContentPtr, historyUrl, urlTimeMs);
    }

    /**
     * Destroy the native component of this class.
     */
    @VisibleForTesting
    public void destroy() {
        if (mContentViewCore != null) {
            destroyContentView();
        }

        // Tests will not create the native pointer, so we need to check if it's not zero
        // otherwise calling nativeDestroy with zero will make Chrome crash.
        if (mNativeOverlayPanelContentPtr != 0L) {
            nativeDestroy(mNativeOverlayPanelContentPtr);
        }
    }

    // Native calls.
    private native long nativeInit();
    private native void nativeDestroy(long nativeOverlayPanelContent);
    private native void nativeRemoveLastHistoryEntry(
            long nativeOverlayPanelContent, String historyUrl, long urlTimeMs);
    private native void nativeSetWebContents(long nativeOverlayPanelContent,
            ContentViewCore contentViewCore, WebContentsDelegateAndroid delegate);
    private native void nativeDestroyWebContents(long nativeOverlayPanelContent);
    private native void nativeSetInterceptNavigationDelegate(long nativeOverlayPanelContent,
            InterceptNavigationDelegate delegate, WebContents webContents);
}
