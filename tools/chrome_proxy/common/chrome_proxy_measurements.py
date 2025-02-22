# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import base64
import logging

from common import chrome_proxy_metrics as metrics
from telemetry.core import exceptions
from telemetry.page import page_test


def WaitForViaHeader(tab, url="http://check.googlezip.net/test.html"):
  """Wait until responses start coming back with the Chrome Proxy via header.

  Poll |url| in |tab| until the Chrome Proxy via header is present in a
  response.

  This function is useful when testing with the Data Saver API, since Chrome
  won't actually start sending requests to the Data Reduction Proxy until the
  Data Saver API fetch completes. This function can be used to wait for the Data
  Saver API fetch to complete.
  """

  tab.Navigate('data:text/html;base64,%s' % base64.b64encode(
    '<html><body><script>'
    'window.via_header_found = false;'
    'function PollDRPCheck(url, wanted_via) {'
      'if (via_header_found) { return true; }'
      'try {'
        'var xmlhttp = new XMLHttpRequest();'
        'xmlhttp.open("HEAD",url,true);'
        'xmlhttp.onload=function(e) {'
          'var via=xmlhttp.getResponseHeader("via");'
          'if (via && via.indexOf(wanted_via) != -1) {'
            'window.via_header_found = true;'
          '}'
        '};'
        'xmlhttp.timeout=30000;'
        'xmlhttp.send();'
      '} catch (err) {'
        '/* Return normally if the xhr request failed. */'
      '}'
      'return false;'
    '}'
    '</script>'
    'Waiting for Chrome to start using the DRP...'
    '</body></html>'))

  # Ensure the page has started loading before attempting the DRP check.
  tab.WaitForJavaScriptExpression('performance.timing.loadEventStart', 60)
  tab.WaitForJavaScriptExpression(
    'PollDRPCheck("%s", "%s")' % (url, metrics.CHROME_PROXY_VIA_HEADER), 60)


class ChromeProxyValidation(page_test.PageTest):
  """Base class for all chrome proxy correctness measurements."""

  # Value of the extra via header. |None| if no extra via header is expected.
  extra_via_header = None

  def __init__(self, restart_after_each_page=False, metrics=None):
    super(ChromeProxyValidation, self).__init__(
        needs_browser_restart_after_each_page=restart_after_each_page)
    self._metrics = metrics
    self._page = None

  def CustomizeBrowserOptions(self, options):
    # Enable the chrome proxy (data reduction proxy).
    options.AppendExtraBrowserArgs('--enable-spdy-proxy-auth')

  def WillNavigateToPage(self, page, tab):
    WaitForViaHeader(tab)

    tab.ClearCache(force=True)
    assert self._metrics
    self._metrics.Start(page, tab)

  def ValidateAndMeasurePage(self, page, tab, results):
    self._page = page
    # Wait for the load event.
    tab.WaitForJavaScriptExpression('performance.timing.loadEventStart', 300)
    assert self._metrics
    self._metrics.Stop(page, tab)
    if ChromeProxyValidation.extra_via_header:
      self._metrics.AddResultsForExtraViaHeader(
          tab, results, ChromeProxyValidation.extra_via_header)
    self.AddResults(tab, results)

  def AddResults(self, tab, results):
    raise NotImplementedError

  def StopBrowserAfterPage(self, browser, page):  # pylint: disable=W0613
    if hasattr(page, 'restart_after') and page.restart_after:
      return True
    return False
