// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.android_webview.test;

import android.graphics.Bitmap;
import android.graphics.Color;
import android.test.suitebuilder.annotation.SmallTest;
import android.view.View;

import org.chromium.android_webview.AwContents;
import org.chromium.android_webview.AwContents.VisualStateCallback;
import org.chromium.android_webview.test.util.GraphicsTestUtils;
import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.Feature;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * AwContents rendering / pixel tests.
 */
public class AwContentsRenderTest extends AwTestBase {

    private TestAwContentsClient mContentsClient;
    private AwContents mAwContents;
    private AwTestContainerView mContainerView;

    @Override
    public void setUp() throws Exception {
        super.setUp();
        mContentsClient = new TestAwContentsClient();
        mContainerView = createAwTestContainerViewOnMainSync(mContentsClient);
        mAwContents = mContainerView.getAwContents();
    }

    void setBackgroundColorOnUiThread(final int c) {
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                mAwContents.setBackgroundColor(c);
            }
        });
    }


    @SmallTest
    @Feature({"AndroidWebView"})
    public void testSetGetBackgroundColor() throws Throwable {
        setBackgroundColorOnUiThread(Color.MAGENTA);
        GraphicsTestUtils.pollForBackgroundColor(mAwContents, Color.MAGENTA);

        setBackgroundColorOnUiThread(Color.CYAN);
        GraphicsTestUtils.pollForBackgroundColor(mAwContents, Color.CYAN);

        loadUrlSync(mAwContents, mContentsClient.getOnPageFinishedHelper(), "about:blank");
        assertEquals(Color.CYAN, GraphicsTestUtils.sampleBackgroundColorOnUiThread(mAwContents));

        setBackgroundColorOnUiThread(Color.YELLOW);
        GraphicsTestUtils.pollForBackgroundColor(mAwContents, Color.YELLOW);

        loadUrlSync(mAwContents, mContentsClient.getOnPageFinishedHelper(),
                "data:text/html,<html><head><style>body {background-color:#227788}</style></head>"
                + "<body></body></html>");
        GraphicsTestUtils.pollForBackgroundColor(mAwContents, Color.rgb(0x22, 0x77, 0x88));

        // Changing the base background should not override CSS background.
        setBackgroundColorOnUiThread(Color.MAGENTA);
        assertEquals(Color.rgb(0x22, 0x77, 0x88),
                GraphicsTestUtils.sampleBackgroundColorOnUiThread(mAwContents));
        // ...setting the background is asynchronous, so pause a bit and retest just to be sure.
        Thread.sleep(500);
        assertEquals(Color.rgb(0x22, 0x77, 0x88),
                GraphicsTestUtils.sampleBackgroundColorOnUiThread(mAwContents));
    }

    @SmallTest
    @Feature({"AndroidWebView"})
    public void testPictureListener() throws Throwable {
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                mAwContents.enableOnNewPicture(true, true);
            }
        });

        int pictureCount = mContentsClient.getPictureListenerHelper().getCallCount();
        loadUrlSync(mAwContents, mContentsClient.getOnPageFinishedHelper(), "about:blank");
        mContentsClient.getPictureListenerHelper().waitForCallback(pictureCount, 1);
        // Invalidation only, so picture should be null.
        assertNull(mContentsClient.getPictureListenerHelper().getPicture());
    }

    @SmallTest
    @Feature({"AndroidWebView"})
    public void testForceDrawWhenInvisible() throws Throwable {
        loadUrlSync(mAwContents, mContentsClient.getOnPageFinishedHelper(),
                "data:text/html,<html><head><style>body {background-color:#227788}</style></head>"
                        + "<body>Hello world!</body></html>");

        Bitmap visibleBitmap = null;
        Bitmap invisibleBitmap = null;
        final CountDownLatch latch = new CountDownLatch(1);
        runTestOnUiThread(new Runnable() {
            @Override
            public void run() {
                final long requestId1 = 1;
                mAwContents.insertVisualStateCallback(requestId1, new VisualStateCallback() {
                    @Override
                    public void onComplete(long id) {
                        assertEquals(requestId1, id);
                        latch.countDown();
                    }
                });
            }
        });
        assertTrue(latch.await(AwTestBase.WAIT_TIMEOUT_MS, TimeUnit.MILLISECONDS));

        final int width = mAwContents.getContentWidthCss();
        final int height = mAwContents.getContentHeightCss();
        visibleBitmap = GraphicsTestUtils.drawAwContentsOnUiThread(mAwContents, width, height);
        runTestOnUiThread(new Runnable() {
            @Override
            public void run() {
                mContainerView.setVisibility(View.INVISIBLE);
            }
        });

        // VisualStateCallback#onComplete won't be called when WebView is
        // invisible. So there is no reliable way to tell if View#setVisibility
        // has taken effect. Just sleep the test thread for 500ms.
        Thread.sleep(500);
        invisibleBitmap = GraphicsTestUtils.drawAwContentsOnUiThread(mAwContents, width, height);
        assertNotNull(invisibleBitmap);
        assertTrue(invisibleBitmap.sameAs(visibleBitmap));
    }
}
