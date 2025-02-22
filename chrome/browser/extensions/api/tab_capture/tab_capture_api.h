// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Defines the Chrome Extensions Tab Capture API functions for accessing
// tab media streams.

#ifndef CHROME_BROWSER_EXTENSIONS_API_TAB_CAPTURE_TAB_CAPTURE_API_H_
#define CHROME_BROWSER_EXTENSIONS_API_TAB_CAPTURE_TAB_CAPTURE_API_H_

#include "chrome/browser/extensions/api/tab_capture/tab_capture_registry.h"
#include "chrome/browser/extensions/chrome_extension_function.h"
#include "chrome/common/extensions/api/tab_capture.h"

namespace extensions {

// Extension ids for the chromecast.
extern const char* const kChromecastExtensionIds[6];

class TabCaptureCaptureFunction : public ChromeSyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("tabCapture.capture", TABCAPTURE_CAPTURE)

 private:
  ~TabCaptureCaptureFunction() final {}

  // ExtensionFunction:
  bool RunSync() final;
};

class TabCaptureGetCapturedTabsFunction : public ChromeSyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("tabCapture.getCapturedTabs",
                             TABCAPTURE_GETCAPTUREDTABS)

 private:
  ~TabCaptureGetCapturedTabsFunction() final {}

  // ExtensionFunction:
  bool RunSync() final;
};

class TabCaptureCaptureOffscreenTabFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("tabCapture.captureOffscreenTab",
                             TABCAPTURE_CAPTUREOFFSCREENTAB)

  // Examines the min/max width/height constraints in the |options| to determine
  // a suitable initial off-screen tab size.
  static gfx::Size DetermineInitialSize(
      const extensions::api::tab_capture::CaptureOptions& options);

 private:
  ~TabCaptureCaptureOffscreenTabFunction() final {}

  // ExtensionFunction:
  bool RunSync() final;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_TAB_CAPTURE_TAB_CAPTURE_API_H_
