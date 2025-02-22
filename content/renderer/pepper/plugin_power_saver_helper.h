// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_PEPPER_PLUGIN_POWER_SAVER_HELPER_H_
#define CONTENT_RENDERER_PEPPER_PLUGIN_POWER_SAVER_HELPER_H_

#include <set>
#include <vector>

#include "base/callback.h"
#include "content/common/content_export.h"
#include "content/public/renderer/render_frame_observer.h"
#include "url/origin.h"

namespace blink {
struct WebPluginParams;
struct WebRect;
}

namespace content {

class CONTENT_EXPORT PluginPowerSaverHelper : public RenderFrameObserver {
 public:
  explicit PluginPowerSaverHelper(RenderFrame* render_frame);
  ~PluginPowerSaverHelper() override;

 private:
  friend class RenderFrameImpl;

  struct PeripheralPlugin {
    PeripheralPlugin(const url::Origin& content_origin,
                     const base::Closure& unthrottle_callback);
    ~PeripheralPlugin();

    url::Origin content_origin;
    base::Closure unthrottle_callback;
  };

  enum OverrideForTesting {
    Normal,
    Never,
    IgnoreList,
    Always
  };

  // See RenderFrame for documentation.
  void RegisterPeripheralPlugin(const url::Origin& content_origin,
                                const base::Closure& unthrottle_callback);
  bool ShouldThrottleContent(const url::Origin& main_frame_origin,
                             const url::Origin& content_origin,
                             const std::string& plugin_module_name,
                             int width,
                             int height,
                             bool* cross_origin_main_content) const;
  void WhitelistContentOrigin(const url::Origin& content_origin);

  // RenderFrameObserver implementation.
  void DidCommitProvisionalLoad(bool is_new_navigation,
                                bool is_same_page_navigation) override;
  bool OnMessageReceived(const IPC::Message& message) override;

  void OnUpdatePluginContentOriginWhitelist(
      const std::set<url::Origin>& origin_whitelist);

  OverrideForTesting override_for_testing_;

  // Local copy of the whitelist for the entire tab.
  std::set<url::Origin> origin_whitelist_;

  // Set of peripheral plugins eligible to be unthrottled ex post facto.
  std::vector<PeripheralPlugin> peripheral_plugins_;

  DISALLOW_COPY_AND_ASSIGN(PluginPowerSaverHelper);
};

}  // namespace content

#endif  // CONTENT_RENDERER_PEPPER_PLUGIN_POWER_SAVER_HELPER_H_
