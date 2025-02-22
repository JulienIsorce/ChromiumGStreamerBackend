// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/chrome/browser/chrome_paths.h"

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/threading/thread_restrictions.h"

namespace ios {
namespace {

#if defined(GOOGLE_CHROME_BUILD)
const base::FilePath::CharType kProductDirName[] =
    FILE_PATH_LITERAL("Google/Chrome");
#else
const base::FilePath::CharType kProductDirName[] =
    FILE_PATH_LITERAL("Chromium");
#endif

bool GetDefaultUserDataDirectory(base::FilePath* result) {
  if (!PathService::Get(base::DIR_APP_DATA, result)) {
    NOTREACHED();
    return false;
  }
  *result = result->Append(kProductDirName);
  return true;
}

bool PathProvider(int key, base::FilePath* result) {
  // Assume that creation of the directory is not required if it does not exist.
  // This flag is set to true for the case where it needs to be created.
  bool create_dir = false;

  base::FilePath cur;
  switch (key) {
    case DIR_USER_DATA:
      if (!GetDefaultUserDataDirectory(&cur))
        return false;
      create_dir = true;
      break;

    case DIR_CRASH_DUMPS:
      if (!GetDefaultUserDataDirectory(&cur))
        return false;
      cur = cur.Append(FILE_PATH_LITERAL("Crash Reports"));
      create_dir = true;
      break;

    case DIR_TEST_DATA:
      if (!PathService::Get(base::DIR_SOURCE_ROOT, &cur))
        return false;
      cur = cur.Append(FILE_PATH_LITERAL("ios"));
      cur = cur.Append(FILE_PATH_LITERAL("chrome"));
      cur = cur.Append(FILE_PATH_LITERAL("test"));
      cur = cur.Append(FILE_PATH_LITERAL("data"));
      break;

    case FILE_LOCAL_STATE:
      if (!PathService::Get(ios::DIR_USER_DATA, &cur))
        return false;
      cur = cur.Append(FILE_PATH_LITERAL("Local State"));
      break;

    default:
      return false;
  }

  if (create_dir && !base::PathExists(cur) && !base::CreateDirectory(cur))
    return false;

  *result = cur;
  return true;
}

}  // namespace

void RegisterPathProvider() {
  PathService::RegisterProvider(PathProvider, PATH_START, PATH_END);
}

}  // namespace ios
