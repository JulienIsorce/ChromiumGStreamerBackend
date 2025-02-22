// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.compositor.layouts;

import android.graphics.Rect;

import org.chromium.chrome.browser.compositor.layouts.components.LayoutTab;
import org.chromium.ui.resources.ResourceManager;

/**
 * {@link LayoutRenderHost} is the minimal interface the layouts need to know about its host to
 * render.
 */
public interface LayoutRenderHost {
    /**
     * Request layout and draw.
     */
    void requestRender();

    /**
     * Indicates that we are about to draw and final layout changes should be made.
     */
    void onCompositorLayout();

    /**
     * Indicates that a previously rendered frame has been swapped to the OS.
     */
    void onSwapBuffersCompleted(int pendingSwapBuffersCount);

    /**
     * Indicates that the rendering surface has just been created.
     */
    void onSurfaceCreated();

    /**
     * Indicates that the rendering surface has been resized.
     */
    void onPhysicalBackingSizeChanged(int width, int height);

    /**
     * Indicates that the amount the surface is overdrawing on the bottom has changed.
     *
     * This occurs when the surface is larger than the window viewport.
     *
     * @param overdrawHeight The overdraw amount.
     */
    void onOverdrawBottomHeightChanged(int overdrawHeight);

    /**
     * @see #onOverdrawBottomHeightChanged(int)
     * @return The overdraw bottom height of the last frame rendered by the current tab.
     */
    int getCurrentOverdrawBottomHeight();

    /**
     * @return The number of actually drawn {@link LayoutTab}.
     */
    int getLayoutTabsDrawnCount();

    /**
     * Pushes a debug rectangle that will be drawn.
     *
     * @param rect  The rect to be drawn.
     * @param color The color of the rect.
     */
    void pushDebugRect(Rect rect, int color);

    /**
     * Loads the persistent textures if they are not loaded already.
     */
    void loadPersitentTextureDataIfNeeded();

    /**
     * @param rect Rect instance to be used to store the result and return. If null, it uses a new
     *             Rect instance.
     * @return The current visible viewport of the host (takes fullscreen into account).
     */
    Rect getVisibleViewport(Rect rect);

    /**
     * @return The background color of the toolbar.
     */
    int getTopControlsBackgroundColor();

    /**
     * @return Whether or not the toolbar is currently being faked.
     */
    boolean areTopControlsPermanentlyHidden();

    /**
     * @return The height of the top controls in pixels.
     */
    int getTopControlsHeightPixels();

    /**
     * @return The {@link ResourceManager}.
     */
    ResourceManager getResourceManager();

    /**
     * Called when something has changed in the Compositor rendered view system.
     */
    void invalidateAccessibilityProvider();
}
