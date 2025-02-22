// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_MUS_EXAMPLE_WM_WINDOW_MANAGER_APPLICATION_H_
#define COMPONENTS_MUS_EXAMPLE_WM_WINDOW_MANAGER_APPLICATION_H_

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_vector.h"
#include "components/mus/public/cpp/types.h"
#include "components/mus/public/cpp/window_tree_delegate.h"
#include "components/mus/public/interfaces/window_manager.mojom.h"
#include "components/mus/public/interfaces/window_tree_host.mojom.h"
#include "mojo/application/public/cpp/application_delegate.h"
#include "mojo/application/public/cpp/interface_factory_impl.h"
#include "mojo/common/weak_binding_set.h"

enum class Container;
class WindowLayout;

class WindowManagerImpl;

class WindowManagerApplication
    : public mojo::ApplicationDelegate,
      public mus::WindowTreeDelegate,
      public mojo::InterfaceFactory<mus::mojom::WindowManager> {
 public:
  WindowManagerApplication();
  ~WindowManagerApplication() override;

  mus::Window* root() { return root_; }

  int window_count() { return window_count_; }
  void IncrementWindowCount() { ++window_count_; }

  mus::Window* GetWindowForContainer(Container container);
  mus::Window* GetWindowById(mus::Id id);

 private:
  // ApplicationDelegate:
  void Initialize(mojo::ApplicationImpl* app) override;
  bool ConfigureIncomingConnection(
      mojo::ApplicationConnection* connection) override;

  // WindowTreeDelegate:
  void OnEmbed(mus::Window* root) override;
  void OnConnectionLost(mus::WindowTreeConnection* connection) override;

  // InterfaceFactory<mus::mojom::WindowManager>:
  void Create(
      mojo::ApplicationConnection* connection,
      mojo::InterfaceRequest<mus::mojom::WindowManager> request) override;

  // Sets up the window containers used for z-space management.
  void CreateContainers();

  // nullptr until the Mus connection is established via OnEmbed().
  mus::Window* root_;
  int window_count_;

  mus::mojom::WindowTreeHostPtr host_;

  // |window_manager_| is created once OnEmbed() is called. Until that time
  // |requests_| stores any pending WindowManager interface requests.
  scoped_ptr<WindowManagerImpl> window_manager_;
  mojo::WeakBindingSet<mus::mojom::WindowManager> window_manager_binding_;
  ScopedVector<mojo::InterfaceRequest<mus::mojom::WindowManager>> requests_;

  scoped_ptr<WindowLayout> layout_;

  DISALLOW_COPY_AND_ASSIGN(WindowManagerApplication);
};

#endif  // COMPONENTS_MUS_EXAMPLE_WM_WINDOW_MANAGER_APPLICATION_H_
