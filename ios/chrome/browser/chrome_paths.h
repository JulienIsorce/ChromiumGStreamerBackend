// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_CHROME_PATHS_H_
#define IOS_CHROME_BROWSER_CHROME_PATHS_H_

// This file declares path keys for the Chrome on iOS application.  These can be
// used with the PathService to access various special directories and files.

namespace ios {

enum {
  PATH_START = 2000,

  DIR_USER_DATA = PATH_START,  // Directory where user data can be written.
  DIR_CRASH_DUMPS,             // Directory where crash dumps are written.
  DIR_TEST_DATA,               // Directory where unit test data resides.

  FILE_LOCAL_STATE,  // Path and filename to the file in which
                     // installation-specific state is saved.

  PATH_END
};

// Call once to register the provider for the path keys defined above.
void RegisterPathProvider();

}  // namespace

#endif  // IOS_CHROME_BROWSER_CHROME_PATHS_H_
