// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_MUS_PUBLIC_CPP_WINDOW_OBSERVER_H_
#define COMPONENTS_MUS_PUBLIC_CPP_WINDOW_OBSERVER_H_

#include <vector>

#include "components/mus/public/cpp/window.h"
#include "ui/mojo/events/input_events.mojom.h"

namespace mus {

class Window;

// A note on -ing and -ed suffixes:
//
// -ing methods are called before changes are applied to the local window model.
// -ed methods are called after changes are applied to the local window model.
//
// If the change originated from another connection to the window manager, it's
// possible that the change has already been applied to the service-side model
// prior to being called, so for example in the case of OnWindowDestroying(),
// it's
// possible the window has already been destroyed on the service side.

class WindowObserver {
 public:
  struct TreeChangeParams {
    TreeChangeParams();
    Window* target;
    Window* old_parent;
    Window* new_parent;
    Window* receiver;
  };

  virtual void OnTreeChanging(const TreeChangeParams& params) {}
  virtual void OnTreeChanged(const TreeChangeParams& params) {}

  virtual void OnWindowReordering(Window* window,
                                  Window* relative_window,
                                  mojom::OrderDirection direction) {}
  virtual void OnWindowReordered(Window* window,
                                 Window* relative_window,
                                 mojom::OrderDirection direction) {}

  virtual void OnWindowDestroying(Window* window) {}
  virtual void OnWindowDestroyed(Window* window) {}

  virtual void OnWindowBoundsChanging(Window* window,
                                      const mojo::Rect& old_bounds,
                                      const mojo::Rect& new_bounds) {}
  virtual void OnWindowBoundsChanged(Window* window,
                                     const mojo::Rect& old_bounds,
                                     const mojo::Rect& new_bounds) {}
  virtual void OnWindowClientAreaChanged(Window* window,
                                         const mojo::Rect& old_client_area) {}

  virtual void OnWindowViewportMetricsChanged(
      Window* window,
      const mojom::ViewportMetrics& old_metrics,
      const mojom::ViewportMetrics& new_metrics) {}

  virtual void OnWindowFocusChanged(Window* gained_focus, Window* lost_focus) {}

  virtual void OnWindowInputEvent(Window* window, const mojo::EventPtr& event) {
  }

  virtual void OnWindowVisibilityChanging(Window* window) {}
  virtual void OnWindowVisibilityChanged(Window* window) {}

  // Invoked when this Window's shared properties have changed. This can either
  // be caused by SetSharedProperty() being called locally, or by us receiving
  // a mojo message that this property has changed. If this property has been
  // added, |old_data| is null. If this property was removed, |new_data| is
  // null.
  virtual void OnWindowSharedPropertyChanged(
      Window* window,
      const std::string& name,
      const std::vector<uint8_t>* old_data,
      const std::vector<uint8_t>* new_data) {}

  // Invoked when SetProperty() or ClearProperty() is called on the window.
  // |key| is either a WindowProperty<T>* (SetProperty, ClearProperty). Either
  // way, it can simply be compared for equality with the property
  // constant. |old| is the old property value, which must be cast to the
  // appropriate type before use.
  virtual void OnWindowLocalPropertyChanged(Window* window,
                                            const void* key,
                                            intptr_t old) {}

  virtual void OnWindowEmbeddedAppDisconnected(Window* window) {}

  // Sent when the drawn state changes. This is only sent for the root nodes
  // when embedded.
  virtual void OnWindowDrawnChanging(Window* window) {}
  virtual void OnWindowDrawnChanged(Window* window) {}

 protected:
  virtual ~WindowObserver() {}
};

}  // namespace mus

#endif  // COMPONENTS_MUS_PUBLIC_CPP_WINDOW_OBSERVER_H_
