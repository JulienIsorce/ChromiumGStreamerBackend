// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_P2P_EMPTY_NETWORK_MANAGER_H_
#define CONTENT_RENDERER_P2P_EMPTY_NETWORK_MANAGER_H_

#include "base/memory/weak_ptr.h"
#include "base/threading/thread_checker.h"
#include "content/common/content_export.h"
#include "third_party/webrtc/base/network.h"

namespace content {

// A NetworkManager implementation which handles the case where local address
// enumeration is not requested and just returns empty network lists. This class
// is not thread safe and should only be used by WebRTC's worker thread.
class EmptyNetworkManager : public rtc::NetworkManagerBase {
 public:
  // This class is created by WebRTC's signaling thread but used by WebRTC's
  // worker thread |task_runner|.
  CONTENT_EXPORT EmptyNetworkManager();
  CONTENT_EXPORT ~EmptyNetworkManager() override;

  // rtc::NetworkManager:
  void StartUpdating() override;
  void StopUpdating() override;
  void GetNetworks(NetworkList* networks) const override;

 private:
  void FireEvent();
  void SendNetworksChangedSignal();

  base::ThreadChecker thread_checker_;

  // Track whether StartUpdating() has been called before.
  bool updating_started_ = false;

  base::WeakPtrFactory<EmptyNetworkManager> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(EmptyNetworkManager);
};

}  // namespace content

#endif  // CONTENT_RENDERER_P2P_EMPTY_NETWORK_MANAGER_H_
