// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_PASSWORDS_MANAGE_PASSWORDS_BUBBLE_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_PASSWORDS_MANAGE_PASSWORDS_BUBBLE_VIEW_H_

#include "chrome/browser/ui/passwords/manage_passwords_bubble_model.h"
#include "chrome/browser/ui/views/managed_full_screen_bubble_delegate_view.h"

class ManagePasswordsIconViews;

namespace content {
class WebContents;
}

// The ManagePasswordsBubbleView controls the contents of the bubble which
// pops up when Chrome offers to save a user's password, or when the user
// interacts with the Omnibox icon. It has two distinct states:
//
// 1. PendingView: Offers the user the possibility of saving credentials.
// 2. ManageView: Displays the current page's saved credentials.
// 3. BlacklistedView: Informs the user that the current page is blacklisted.
//
class ManagePasswordsBubbleView : public ManagedFullScreenBubbleDelegateView {
 public:
  // Shows the bubble.
  static void ShowBubble(content::WebContents* web_contents,
                         ManagePasswordsBubbleModel::DisplayReason reason);

  // Closes the existing bubble.
  static void CloseBubble();

  // Makes the bubble the foreground window.
  static void ActivateBubble();

  // Returns a pointer to the bubble.
  static ManagePasswordsBubbleView* manage_password_bubble() {
    return manage_passwords_bubble_;
  }

  content::WebContents* web_contents() const;

#if defined(UNIT_TEST)
  const View* initially_focused_view() const {
    return initially_focused_view_;
  }

  static void set_auto_signin_toast_timeout(int seconds) {
    auto_signin_toast_timeout_ = seconds;
  }
#endif

  ManagePasswordsBubbleModel* model() { return &model_; }

 private:
  class AccountChooserView;
  class AutoSigninView;
  class BlacklistedView;
  class ManageView;
  class PendingView;
  class SaveConfirmationView;
  class UpdatePendingView;
  class WebContentMouseHandler;

  ManagePasswordsBubbleView(content::WebContents* web_contents,
                            ManagePasswordsIconViews* anchor_view,
                            ManagePasswordsBubbleModel::DisplayReason reason);
  ~ManagePasswordsBubbleView() override;

  // ManagedFullScreenBubbleDelegateView:
  views::View* GetInitiallyFocusedView() override;
  void Init() override;
  void Close() override;

  // WidgetObserver:
  void OnWidgetClosing(views::Widget* widget) override;

  // WidgetDelegate:
  bool ShouldShowCloseButton() const override;

  // Refreshes the bubble's state: called to display a confirmation screen after
  // a user selects "Never for this site", for instance.
  void Refresh();

  void set_initially_focused_view(views::View* view) {
    DCHECK(!initially_focused_view_);
    initially_focused_view_ = view;
  }

  // Singleton instance of the Password bubble. The Password bubble can only be
  // shown on the active browser window, so there is no case in which it will be
  // shown twice at the same time. The instance is owned by the Bubble and will
  // be deleted when the bubble closes.
  static ManagePasswordsBubbleView* manage_passwords_bubble_;

  // The timeout in seconds for the auto sign-in toast.
  static int auto_signin_toast_timeout_;

  ManagePasswordsBubbleModel model_;

  ManagePasswordsIconViews* anchor_view_;

  views::View* initially_focused_view_;

  // A helper to intercept mouse click events on the web contents.
  scoped_ptr<WebContentMouseHandler> mouse_handler_;

  DISALLOW_COPY_AND_ASSIGN(ManagePasswordsBubbleView);
};

#endif  // CHROME_BROWSER_UI_VIEWS_PASSWORDS_MANAGE_PASSWORDS_BUBBLE_VIEW_H_
