# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'targets': [
    {
      'target_name': 'openh264_unittests',
      'type': '<(gtest_target_type)',
      'include_dirs': [
        '<(DEPTH)',
        '<(DEPTH)/third_party',
      ],
      'dependencies': [
        '<(DEPTH)/testing/gtest.gyp:gtest',
      ],
      'sources': [
        'openh264_unittests.cc',
      ],
    },
  ],
}
