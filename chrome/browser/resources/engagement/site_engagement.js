// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

define('main', [
    'mojo/public/js/connection',
    'chrome/browser/ui/webui/engagement/site_engagement.mojom',
    'content/public/renderer/service_provider',
], function(connection, siteEngagementMojom, serviceProvider) {

  return function() {
    var uiHandler = connection.bindHandleToProxy(
        serviceProvider.connectToService(
            siteEngagementMojom.SiteEngagementUIHandler.name),
        siteEngagementMojom.SiteEngagementUIHandler);

    // Populate engagement table.
    uiHandler.getSiteEngagementInfo().then(response => {
      $('engagement-table').engagementInfo = response.info;
    });
  };
});
